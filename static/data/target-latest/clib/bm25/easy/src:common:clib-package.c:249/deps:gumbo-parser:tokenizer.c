// Copyright 2010 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: jdtang@google.com (Jonathan Tang)
//
// Coding conventions specific to this file:
//
// 1. Functions that fill in a token should be named emit_*, and should be
// followed immediately by a return from the tokenizer (true if no error
// occurred, false if an error occurred).  Sometimes the emit functions
// themselves return a boolean so that they can be combined with the return
// statement; in this case, they should match this convention.
// 2. Functions that shuffle data from temporaries to final API structures
// should be named finish_*, and be called just before the tokenizer exits the
// state that accumulates the temporary.
// 3. All internal data structures should be kept in an initialized state from
// tokenizer creation onwards, ready to accept input.  When a buffer's flushed
// and reset, it should be deallocated and immediately reinitialized.
// 4. Make sure there are appropriate break statements following each state.
// 5. Assertions on the state of the temporary and tag buffers are usually a
// good idea, and should go at the entry point of each state when added.
// 6. Statement order within states goes:
//    1. Add parse errors, if appropriate.
//    2. Call finish_* functions to build up tag state.
//    2. Switch to new state.  Set _reconsume flag if appropriate.
//    3. Perform any other temporary buffer manipulation.
//    4. Emit tokens
//    5. Return/break.
// This order ensures that we can verify that every emit is followed by a
// return, ensures that the correct state is recorded with any parse errors, and
// prevents parse error position from being messed up by possible mark/resets in
// temporary buffer manipulation.


#include "tokenizer.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "attribute.h"
#include "char_ref.h"
#include "error.h"
#include "gumbo.h"
#include "parser.h"
#include "string_buffer.h"
#include "string_piece.h"
#include "token_type.h"
#include "tokenizer_states.h"
#include "utf8.h"
#include "util.h"
#include "vector.h"

// Compared against _script_data_buffer to determine if we're in double-escaped
// script mode.
const GumboStringPiece kScriptTag = { "script", 6 };

// An enum for the return value of each individual state.
typedef enum {
  RETURN_ERROR,         // Return false (error) from the tokenizer.
  RETURN_SUCCESS,       // Return true (success) from the tokenizer.
  NEXT_CHAR             // Proceed to the next character and continue lexing.
} StateResult;

// This is a struct containing state necessary to build up a tag token,
// character by character.
typedef struct GumboInternalTagState {
  // A buffer to accumulate characters for various GumboStringPiece fields.
  GumboStringBuffer _buffer;

  // A pointer to the start of the original text corresponding to the contents
  // of the buffer.
  const char* _original_text;

  // The current tag enum, computed once the tag name state has finished so that
  // the buffer can be re-used for building up attributes.
  GumboTag _tag;

  // The starting location of the text in the buffer.
  GumboSourcePosition _start_pos;

  // The current list of attributes.  This is copied (and ownership of its data
  // transferred) to the GumboStartTag token upon completion of the tag.  New
  // attributes are added as soon as their attribute name state is complete, and
  // values are filled in by operating on _attributes.data[attributes.length-1].
  GumboVector /* GumboAttribute */ _attributes;

  // If true, the next attribute value to be finished should be dropped.  This
  // happens if a duplicate attribute name is encountered - we want to consume
  // the attribute value, but shouldn't overwrite the existing value.
  bool _drop_next_attr_value;

  // The state that caused the tokenizer to switch into a character reference in
  // attribute value state.  This is used to set the additional allowed
  // character, and is switched back to on completion.  Initialized as the
  // tokenizer enters the character reference state.
  GumboTokenizerEnum _attr_value_state;

  // The last start tag to have been emitted by the tokenizer.  This is
  // necessary to check for appropriate end tags.
  GumboTag _last_start_tag;

  // If true, then this is a start tag.  If false, it's an end tag.  This is
  // necessary to generate the appropriate token type at tag-closing time.
  bool _is_start_tag;

  // If true, then this tag is "self-closing" and doesn't have an end tag.
  bool _is_self_closing;
} GumboTagState;

// This is the main tokenizer state struct, containing all state used by in
// tokenizing the input stream.
typedef struct GumboInternalTokenizerState {
  // The current lexer state.  Starts in GUMBO_LEX_DATA.
  GumboTokenizerEnum _state;

  // A flag indicating whether the current input character needs to reconsumed
  // in another state, or whether the next input character should be read for
  // the next iteration of the state loop.  This is set when the spec reads
  // "Reconsume the current input character in..."
  bool _reconsume_current_input;

  // A flag indicating whether the current node is a foreign element.  This is
  // set by gumbo_tokenizer_set_is_current_node_foreign and checked in the
  // markup declaration state.
  bool _is_current_node_foreign;

  // Certain states (notably character references) may emit two character tokens
  // at once, but the contract for lex() fills in only one token at a time.  The
  // extra character is buffered here, and then this is checked on entry to
  // lex().  If a character is stored here, it's immediately emitted and control
  // returns from the lexer.  kGumboNoChar is used to represent 'no character
  // stored.'
  //
  // Note that characters emitted through this mechanism will have their source
  // position marked as the character under the mark, i.e. multiple characters
  // may be emitted with the same position.  This is desirable for character
  // references, but unsuitable for many other cases.  Use the _temporary_buffer
  // mechanism if the buffered characters must have their original positions in
  // the document.
  int _buffered_emit_char;

  // A temporary buffer to accumulate characters, as described by the "temporary
  // buffer" phrase in the tokenizer spec.  We use this in a somewhat unorthodox
  // way: we record the specific character to go into the buffer, which may
  // sometimes be a lowercased version of the actual input character.  However,
  // we *also* use utf8iterator_mark() to record the position at tag start.
  // When we start flushing the temporary buffer, we set _temporary_buffer_emit
  // to the start of it, and then increment it for each call to the tokenizer.
  // We also call utf8iterator_reset(), and utf8iterator_next() through the
  // input stream, so that tokens emitted by emit_char have the correct position
  // and original text.
  GumboStringBuffer _temporary_buffer;

  // The current cursor position we're emitting from within
  // _temporary_buffer.data.  NULL whenever we're not flushing the buffer.
  const char* _temporary_buffer_emit;

  // The temporary buffer is also used by the spec to check whether we should
  // enter the script data double escaped state, but we can't use the same
  // buffer for both because we have to flush out "<s" as emits while still
  // maintaining the context that will eventually become "script".  This is a
  // separate buffer that's used in place of the temporary buffer for states
  // that may enter the script data double escape start state.
  GumboStringBuffer _script_data_buffer;

  // Pointer to the beginning of the current token in the original buffer; used
  // to record the original text.
  const char* _token_start;

  // GumboSourcePosition recording the source location of the start of the
  // current token.
  GumboSourcePosition _token_start_pos;

  // Current tag state.
  GumboTagState _tag_state;

  // Doctype state.  We use the temporary buffer to accumulate characters (it's
  // not used for anything else in the doctype states), and then freshly
  // allocate the strings in the doctype token, then copy it over on emit.
  GumboTokenDocType _doc_type_state;

  // The UTF8Iterator over the tokenizer input.
  Utf8Iterator _input;
} GumboTokenizerState;

// Adds an ERR_UNEXPECTED_CODE_POINT parse error to the parser's error struct.
static void add_parse_error(GumboParser* parser, GumboErrorType type) {
  GumboError* error = gumbo_add_error(parser);
  if (!error) {
    return;
  }
  GumboTokenizerState* tokenizer = parser->_tokenizer_state;
  utf8iterator_get_position(&tokenizer->_input, &error->position);
  error->original_text = utf8iterator_get_char_pointer(&tokenizer->_input);
  error->type = type;
  error->v.tokenizer.codepoint = utf8iterator_current(&tokenizer->_input);
  switch (tokenizer->_state) {
    case GUMBO_LEX_DATA:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_DATA;
      break;
    case GUMBO_LEX_CHAR_REF_IN_DATA:
    case GUMBO_LEX_CHAR_REF_IN_RCDATA:
    case GUMBO_LEX_CHAR_REF_IN_ATTR_VALUE:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_CHAR_REF;
      break;
    case GUMBO_LEX_RCDATA:
    case GUMBO_LEX_RCDATA_LT:
    case GUMBO_LEX_RCDATA_END_TAG_OPEN:
    case GUMBO_LEX_RCDATA_END_TAG_NAME:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_RCDATA;
      break;
    case GUMBO_LEX_RAWTEXT:
    case GUMBO_LEX_RAWTEXT_LT:
    case GUMBO_LEX_RAWTEXT_END_TAG_OPEN:
    case GUMBO_LEX_RAWTEXT_END_TAG_NAME:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_RAWTEXT;
      break;
    case GUMBO_LEX_PLAINTEXT:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_PLAINTEXT;
      break;
    case GUMBO_LEX_SCRIPT:
    case GUMBO_LEX_SCRIPT_LT:
    case GUMBO_LEX_SCRIPT_END_TAG_OPEN:
    case GUMBO_LEX_SCRIPT_END_TAG_NAME:
    case GUMBO_LEX_SCRIPT_ESCAPED_START:
    case GUMBO_LEX_SCRIPT_ESCAPED_START_DASH:
    case GUMBO_LEX_SCRIPT_ESCAPED:
    case GUMBO_LEX_SCRIPT_ESCAPED_DASH:
    case GUMBO_LEX_SCRIPT_ESCAPED_DASH_DASH:
    case GUMBO_LEX_SCRIPT_ESCAPED_LT:
    case GUMBO_LEX_SCRIPT_ESCAPED_END_TAG_OPEN:
    case GUMBO_LEX_SCRIPT_ESCAPED_END_TAG_NAME:
    case GUMBO_LEX_SCRIPT_DOUBLE_ESCAPED_START:
    case GUMBO_LEX_SCRIPT_DOUBLE_ESCAPED:
    case GUMBO_LEX_SCRIPT_DOUBLE_ESCAPED_DASH:
    case GUMBO_LEX_SCRIPT_DOUBLE_ESCAPED_DASH_DASH:
    case GUMBO_LEX_SCRIPT_DOUBLE_ESCAPED_LT:
    case GUMBO_LEX_SCRIPT_DOUBLE_ESCAPED_END:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_SCRIPT;
      break;
    case GUMBO_LEX_TAG_OPEN:
    case GUMBO_LEX_END_TAG_OPEN:
    case GUMBO_LEX_TAG_NAME:
    case GUMBO_LEX_BEFORE_ATTR_NAME:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_TAG;
      break;
    case GUMBO_LEX_SELF_CLOSING_START_TAG:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_SELF_CLOSING_TAG;
      break;
    case GUMBO_LEX_ATTR_NAME:
    case GUMBO_LEX_AFTER_ATTR_NAME:
    case GUMBO_LEX_BEFORE_ATTR_VALUE:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_ATTR_NAME;
      break;
    case GUMBO_LEX_ATTR_VALUE_DOUBLE_QUOTED:
    case GUMBO_LEX_ATTR_VALUE_SINGLE_QUOTED:
    case GUMBO_LEX_ATTR_VALUE_UNQUOTED:
    case GUMBO_LEX_AFTER_ATTR_VALUE_QUOTED:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_ATTR_VALUE;
      break;
    case GUMBO_LEX_BOGUS_COMMENT:
    case GUMBO_LEX_COMMENT_START:
    case GUMBO_LEX_COMMENT_START_DASH:
    case GUMBO_LEX_COMMENT:
    case GUMBO_LEX_COMMENT_END_DASH:
    case GUMBO_LEX_COMMENT_END:
    case GUMBO_LEX_COMMENT_END_BANG:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_COMMENT;
      break;
    case GUMBO_LEX_MARKUP_DECLARATION:
    case GUMBO_LEX_DOCTYPE:
    case GUMBO_LEX_BEFORE_DOCTYPE_NAME:
    case GUMBO_LEX_DOCTYPE_NAME:
    case GUMBO_LEX_AFTER_DOCTYPE_NAME:
    case GUMBO_LEX_AFTER_DOCTYPE_PUBLIC_KEYWORD:
    case GUMBO_LEX_BEFORE_DOCTYPE_PUBLIC_ID:
    case GUMBO_LEX_DOCTYPE_PUBLIC_ID_DOUBLE_QUOTED:
    case GUMBO_LEX_DOCTYPE_PUBLIC_ID_SINGLE_QUOTED:
    case GUMBO_LEX_AFTER_DOCTYPE_PUBLIC_ID:
    case GUMBO_LEX_BETWEEN_DOCTYPE_PUBLIC_SYSTEM_ID:
    case GUMBO_LEX_AFTER_DOCTYPE_SYSTEM_KEYWORD:
    case GUMBO_LEX_BEFORE_DOCTYPE_SYSTEM_ID:
    case GUMBO_LEX_DOCTYPE_SYSTEM_ID_DOUBLE_QUOTED:
    case GUMBO_LEX_DOCTYPE_SYSTEM_ID_SINGLE_QUOTED:
    case GUMBO_LEX_AFTER_DOCTYPE_SYSTEM_ID:
    case GUMBO_LEX_BOGUS_DOCTYPE:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_DOCTYPE;
      break;
    case GUMBO_LEX_CDATA:
      error->v.tokenizer.state = GUMBO_ERR_TOKENIZER_CDATA;
      break;
  }
}

static bool is_alpha(int c) {
  // We don't use ISO C isupper/islower functions here because they
  // depend upon the program's locale, while the behavior of the HTML5 spec is
  // independent of which locale the program is run in.
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int ensure_lowercase(int c) {
  return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
}

static GumboTokenType get_char_token_type(int c) {
  switch (c) {
    case '\t':
    case '\n':
    case '\r':
    case '\f':
    case ' ':
      return GUMBO_TOKEN_WHITESPACE;
    case 0:
      gumbo_debug("Emitted null byte.\n");
      return GUMBO_TOKEN_NULL;
    case -1:
      return GUMBO_TOKEN_EOF;
    default:
      return GUMBO_TOKEN_CHARACTER;
  }
}

// Starts recording characters in the temporary buffer.
// Because this needs to reset the utf8iterator_mark to the beginning of the
// text that will eventually be emitted, it needs to be called a couple of
// states before the spec says "Set the temporary buffer to the empty string".
// In general, this should be called whenever there's a transition to a
// "less-than sign state".  The initial < and possibly / then need to be
// appended to the temporary buffer, their presence needs to be accounted for in
// states that compare the temporary buffer against a literal value, and
// spec stanzas that say "emit a < and / character token along with a character
// token for each character in the temporary buffer" need to be adjusted to
// account for the presence of the < and / inside the temporary buffer.
static void clear_temporary_buffer(GumboParser* parser) {
  GumboTokenizerState* tokenizer = parser->_tokenizer_state;
  assert(!tokenizer->_temporary_buffer_emit);
  utf8iterator_mark(&tokenizer->_input);
  gumbo_string_buffer_destroy(parser, &tokenizer->_temporary_buffer);
  gumbo_string_buffer_init(parser, &tokenizer->_temporary_buffer);
  // The temporary buffer and script data buffer are the same object in the
  // spec, so the script data buffer should be cleared as well.
  gumbo_string_buffer_destroy(parser, &tokenizer->_script_data_buffer);
  gumbo_string_buffer_init(parser, &tokenizer->_script_data_buffer);
}

// Appends a codepoint to the temporary buffer.
static void append_char_to_temporary_buffer(
    GumboParser* parser, int codepoint) {
  gumbo_string_buffer_append_codepoint(
      parser, codepoint, &parser->_tokenizer_state->_temporary_buffer);
}

// Checks to see if the temporary buffer equals a certain string.
// Make sure this remains side-effect free; it's used in assertions.
#ifndef NDEBUG
static bool temporary_buffer_equals(
    GumboParser* parser, const char* text) {
  GumboStringBuffer* buffer = &parser->_tokenizer_state->_temporary_buffer;
  // TODO(jdtang): See if the extra strlen is a performance problem, and replace
  // it with an explicit sizeof(literal) if necessary.  I don't think it will
  // be, as this is only used in a couple of rare states.
  int text_len = strlen(text);
  return text_len == buffer->length &&
      memcmp(buffer->data, text, text_len) == 0;
}
#endif

static void doc_type_state_init(GumboParser* parser) {
  GumboTokenDocType* doc_type_state =
      &parser->_tokenizer_state->_doc_type_state;
  // We initialize these to NULL here so that we don't end up leaking memory if
  // we never see a doctype token.  When we do see a doctype token, we reset
  // them to a freshly-allocated empty string so that we can present a uniform
  // interface to client code and not make them check for null.  Ownership is
  // transferred to the doctype token when it's emitted.
  doc_type_state->name = NULL;
  doc_type_state->public_identifier = NULL;
  doc_type_state->system_identifier = NULL;
  doc_type_state->force_quirks = false;
  doc_type_state->has_public_identifier = false;
  doc_type_state->has_system_identifier = false;
}

// Sets the token original_text and position to the current iterator position.
// This is necessary because [CDATA[ sections may include text that is ignored
// by the tokenizer.
static void reset_token_start_point(GumboTokenizerState* tokenizer) {
  tokenizer->_token_start = utf8iterator_get_char_pointer(&tokenizer->_input);
  utf8iterator_get_position(&tokenizer->_input, &tokenizer->_token_start_pos);
}

// Sets the tag buffer original text and start point to the current iterator
// position.  This is necessary because attribute names & values may have
// whitespace preceeding them, and so we can't assume that the actual token
// starting point was the end of the last tag buffer usage.
static void reset_tag_buffer_start_point(GumboParser* parser) {
  GumboTokenizerState* tokenizer = parser->_tokenizer_state;
  GumboTagState* tag_state = &tokenizer->_tag_state;

  utf8iterator_get_position(&tokenizer->_input, &tag_state->_start_pos);
  tag_state->_original_text = utf8iterator_get_char_pointer(&tokenizer->_input);
}

// Moves the temporary buffer contents over to the specified output string,
// and clears the temporary buffer.
static void finish_temporary_buffer(GumboParser* parser, const char** output) {
  GumboTokenizerState* tokenizer = parser->_tokenizer_state;
  *output =
      gumbo_string_buffer_to_string(parser, &tokenizer->_temporary_buffer);
  clear_temporary_buffer(parser);
}

// Advances the iterator past the end of the token, and then fills in the
// relevant position fields.  It's assumed that after every emit, the tokenizer
// will immediately return (letting the tree-construction stage read the filled
// in Token).  Thus, it's safe to advance the input stream here, since it will
// bypass the advance at the bottom of the state machine loop.
//
// Since this advances the iterator and resets the current input, make sure to
// call it after you've recorded any other data you need for the token.
static void finish_token(GumboParser* parser, GumboToken* token) {
  GumboTokenizerState* tokenizer = parser->_tokenizer_state;
  if (!tokenizer->_reconsume_current_input) {
    utf8iterator_next(&tokenizer->_input);
  }

  token->position = tokenizer->_token_start_pos;
  token->original_text.data = tokenizer->_token_start;
  reset_token_start_point(tokenizer);
  token->original_text.length =
      tokenizer->_token_start - token->original_text.data;
  if (token->original_text.length > 0 && token->original_text.data[token->original_text.length - 1] == '\r') {
    // The UTF8 iterator will ignore carriage returns in the input stream, which
    // means that the next token may start one past a \r character.  The pointer
    // arithmetic above results in that \r being appended to the original text
    // of the preceding token, so we have to adjust its length here to chop the
    // \r off.
    --token->original_text.length;
  }
}

// Records the doctype public ID, assumed to be in the temporary buffer.
// Convenience method that also sets has_public_identifier to true.
static void finish_doctype_public_id(GumboParser* parser) {
  GumboTokenDocType* doc_type_state =
      &parser->_tokenizer_state->_doc_type_state;
  gumbo_parser_deallocate(parser, (void*) doc_type_state->public_identifier);
  finish_temporary_buffer(parser, &doc_type_state->public_identifier);
  doc_type_state->has_public_identifier = true;
}

// Records the doctype system ID, assumed to be in the temporary buffer.
// Convenience method that also sets has_system_identifier to true.
static void finish_doctype_system_id(GumboParser* parser) {
  GumboTokenDocType* doc_type_state =
      &parser->_tokenizer_state->_doc_type_state;
  gumbo_parser_deallocate(parser, (void*) doc_type_state->system_identifier);
  finish_temporary_buffer(parser, &doc_type_state->system_identifier);
  doc_type_state->has_system_identifier = true;
}

// Writes a single specified character to the output token.
static void emit_char(GumboParser* parser, int c, GumboToken* output) {
  output->type = get_char_token_type(c);
  output->v.character = c;
  finish_token(parser, output);
}

// Writes a replacement character token and records a parse error.
// Always returns RETURN_ERROR, per gumbo_lex return value.
static StateResult emit_replacement_char(
    GumboParser* parser, GumboToken* output) {
  // In all cases, this is because of a null byte in the input stream.
  add_parse_error(parser, GUMBO_ERR_UTF8_NULL);
  emit_char(parser, kUtf8ReplacementChar, output);
  return RETURN_ERROR;
}

// Writes an EOF character token.  Always returns RETURN_SUCCESS.
static StateResult emit_eof(GumboParser* parser, GumboToken* output) {
  emit_char(parser, -1, output);
  return RETURN_SUCCESS;
}

// Writes the current input character out as a character token.
// Always returns RETURN_SUCCESS.
static bool emit_current_char(GumboParser* parser, GumboToken* output) {
  emit_char(
      parser, utf8iterator_current(&parser->_tokenizer_state->_input), output);
  return RETURN_SUCCESS;
}

// Writes out a doctype token, copying it from the tokenizer state.
static void emit_doctype(GumboParser* parser, GumboToken* output) {
  output->type = GUMBO_TOKEN_DOCTYPE;
  output->v.doc_type = parser->_tokenizer_state->_doc_type_state;
  finish_token(parser, output);
  doc_type_state_init(parser);
}

// Debug-only function that explicitly sets the attribute vector data to NULL so
// it can be asserted on tag creation, verifying that there are no memory leaks.
static void mark_tag_state_as_empty(GumboTagState* tag_state) {
#ifndef NDEBUG
  tag_state->_attributes = kGumboEmptyVector;
#endif
}

// Writes out the current tag as a start or end tag token.
// Always returns RETURN_SUCCESS.
static StateResult emit_current_tag(GumboParser* parser, GumboToken* output) {
  GumboTagState* tag_state = &parser->_tokenizer_state->_tag_state;
  if (tag_state->_is_start_tag) {
    output->type = GUMBO_TOKEN_START_TAG;
    output->v.start_tag.tag = tag_state->_tag;
    output->v.start_tag.attributes = tag_state->_attributes;
    output->v.start_tag.is_self_closing = tag_state->_is_self_closing;
    tag_state->_last_start_tag = tag_state->_tag;
    mark_tag_state_as_empty(tag_state);
    gumbo_debug("Emitted start tag %s.\n",
               gumbo_normalized_tagname(tag_state->_tag));
  } else {
    output->type = GUMBO_TOKEN_END_TAG;
    output->v.end_tag = tag_state->_tag;
    // In end tags, ownership of the attributes vector is not transferred to the
    // token, but it's still initialized as normal, so it must be manually
    // deallocated.  There may also be attributes to destroy, in certain broken
    // cases like </div</th> (the "th" is an attribute there).
    for (int i = 0; i < tag_state->_attributes.length; ++i) {
      gumbo_destroy_attribute(parser, tag_state->_attributes.data[i]);
    }
    gumbo_parser_deallocate(parser, tag_state->_attributes.data);
    mark_tag_state_as_empty(tag_state);
    gumbo_debug("Emitted end tag %s.\n",
               gumbo_normalized_tagname(tag_state->_tag));
  }
  gumbo_string_buffer_destroy(parser, &tag_state->_buffer);
  finish_token(parser, output);
  gumbo_debug("Original text = %.*s.\n", output->original_text.length, output->original_text.data);
  assert(output->original_text.length >= 2);
  assert(output->original_text.data[0] == '<');
  assert(output->original_text.data[output->original_text.length - 1] == '>');
  return RETURN_SUCCESS;
}

// In some states, we speculatively start a tag, but don't know whether it'll be
// emitted as tag token or as a series of character tokens until we finish it.
// We need to abandon the tag we'd started & free its memory in that case to
// avoid a memory leak.
static void abandon_current_tag(GumboParser* parser) {
  GumboTagState* tag_state = &parser->_tokenizer_state->_tag_state;
  for (int i = 0; i < tag_state->_attributes.length; ++i) {
    gumbo_destroy_attribute(parser, tag_state->_attributes.data[i]);
  }
  gumbo_parser_deallocate(parser, tag_state->_attributes.data);
  mark_tag_state_as_empty(tag_state);
  gumbo_string_buffer_destroy(parser, &tag_state->_buffer);
  gumbo_debug("Abandoning current tag.\n");
}

// Wraps the consume_char_ref function to handle its output and make the
// appropriate TokenizerState modifications.  Returns RETURN_ERROR if a parse
// error occurred, RETURN_SUCCESS otherwise.
static StateResult emit_char_ref(
    GumboParser* parser, int additional_allowed_char,
    bool is_in_attribute, GumboToken* output) {
  GumboTokenizerState* tokenizer = parser->_tokenizer_state;
  OneOrTwoCodepoints char_ref;
  bool status = consume_char_ref(
      parser, &tokenizer->_input, additional_allowed_char, false, &char_ref);
  if (char_ref.first != kGumboNoChar) {
    // consume_char_ref ends with the iterator pointing at the next character,
    // so we need to be sure not advance it again before reading the next token.
    tokenizer->_reconsume_current_input = true;
    emit_char(parser, char_ref.first, output);
    tokenizer->_buffered_emit_char = char_ref.second;
  } else {
    emit_char(parser, '&', output);
  }
  return status ? RETURN