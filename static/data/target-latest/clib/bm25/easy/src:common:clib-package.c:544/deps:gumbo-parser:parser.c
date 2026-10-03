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

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "attribute.h"
#include "error.h"
#include "gumbo.h"
#include "insertion_mode.h"
#include "parser.h"
#include "tokenizer.h"
#include "tokenizer_states.h"
#include "utf8.h"
#include "util.h"
#include "vector.h"


#define AVOID_UNUSED_VARIABLE_WARNING(i) (void)(i)

#define GUMBO_STRING(literal) { literal, sizeof(literal) - 1 }
#define TERMINATOR { "", 0 }

static void* malloc_wrapper(void* unused, size_t size) {
  return malloc(size);
}

static void free_wrapper(void* unused, void* ptr) {
  return free(ptr);
}

const GumboOptions kGumboDefaultOptions = {
  &malloc_wrapper,
  &free_wrapper,
  NULL,
  8,
  false,
  -1,
};

static const GumboStringPiece kDoctypeHtml = GUMBO_STRING("html");
static const GumboStringPiece kPublicIdHtml4_0 = GUMBO_STRING(
    "-//W3C//DTD HTML 4.0//EN");
static const GumboStringPiece kPublicIdHtml4_01 = GUMBO_STRING(
    "-//W3C//DTD HTML 4.01//EN");
static const GumboStringPiece kPublicIdXhtml1_0 = GUMBO_STRING(
    "-//W3C//DTD XHTML 1.0 Strict//EN");
static const GumboStringPiece kPublicIdXhtml1_1 = GUMBO_STRING(
    "-//W3C//DTD XHTML 1.1//EN");
static const GumboStringPiece kSystemIdRecHtml4_0 = GUMBO_STRING(
    "http://www.w3.org/TR/REC-html40/strict.dtd");
static const GumboStringPiece kSystemIdHtml4 = GUMBO_STRING(
    "http://www.w3.org/TR/html4/strict.dtd");
static const GumboStringPiece kSystemIdXhtmlStrict1_1 = GUMBO_STRING(
    "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd");
static const GumboStringPiece kSystemIdXhtml1_1 = GUMBO_STRING(
    "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd");
static const GumboStringPiece kSystemIdLegacyCompat = GUMBO_STRING(
    "about:legacy-compat");

// The doctype arrays have an explicit terminator because we want to pass them
// to a helper function, and passing them as a pointer discards sizeof
// information.  The SVG arrays are used only by one-off functions, and so loops
// over them use sizeof directly instead of a terminator.

static const GumboStringPiece kQuirksModePublicIdPrefixes[] = {
  GUMBO_STRING("+//Silmaril//dtd html Pro v0r11 19970101//"),
  GUMBO_STRING("-//AdvaSoft Ltd//DTD HTML 3.0 asWedit + extensions//"),
  GUMBO_STRING("-//AS//DTD HTML 3.0 asWedit + extensions//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Level 1//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Level 2//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Strict Level 1//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Strict Level 2//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Strict//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.1E//"),
  GUMBO_STRING("-//IETF//DTD HTML 3.0//"),
  GUMBO_STRING("-//IETF//DTD HTML 3.2 Final//"),
  GUMBO_STRING("-//IETF//DTD HTML 3.2//"),
  GUMBO_STRING("-//IETF//DTD HTML 3//"),
  GUMBO_STRING("-//IETF//DTD HTML Level 0//"),
  GUMBO_STRING("-//IETF//DTD HTML Level 1//"),
  GUMBO_STRING("-//IETF//DTD HTML Level 2//"),
  GUMBO_STRING("-//IETF//DTD HTML Level 3//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict Level 0//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict Level 1//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict Level 2//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict Level 3//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict//"),
  GUMBO_STRING("-//IETF//DTD HTML//"),
  GUMBO_STRING("-//Metrius//DTD Metrius Presentational//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 2.0 HTML Strict//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 2.0 HTML//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 2.0 Tables//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 3.0 HTML Strict//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 3.0 HTML//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 3.0 Tables//"),
  GUMBO_STRING("-//Netscape Comm. Corp.//DTD HTML//"),
  GUMBO_STRING("-//Netscape Comm. Corp.//DTD Strict HTML//"),
  GUMBO_STRING("-//O'Reilly and Associates//DTD HTML 2.0//"),
  GUMBO_STRING("-//O'Reilly and Associates//DTD HTML Extended 1.0//"),
  GUMBO_STRING("-//O'Reilly and Associates//DTD HTML Extended Relaxed 1.0//"),
  GUMBO_STRING("-//SoftQuad Software//DTD HoTMetaL PRO 6.0::19990601::)"
      "extensions to HTML 4.0//"),
  GUMBO_STRING("-//SoftQuad//DTD HoTMetaL PRO 4.0::19971010::"
      "extensions to HTML 4.0//"),
  GUMBO_STRING("-//Spyglass//DTD HTML 2.0 Extended//"),
  GUMBO_STRING("-//SQ//DTD HTML 2.0 HoTMetaL + extensions//"),
  GUMBO_STRING("-//Sun Microsystems Corp.//DTD HotJava HTML//"),
  GUMBO_STRING("-//Sun Microsystems Corp.//DTD HotJava Strict HTML//"),
  GUMBO_STRING("-//W3C//DTD HTML 3 1995-03-24//"),
  GUMBO_STRING("-//W3C//DTD HTML 3.2 Draft//"),
  GUMBO_STRING("-//W3C//DTD HTML 3.2 Final//"),
  GUMBO_STRING("-//W3C//DTD HTML 3.2//"),
  GUMBO_STRING("-//W3C//DTD HTML 3.2S Draft//"),
  GUMBO_STRING("-//W3C//DTD HTML 4.0 Frameset//"),
  GUMBO_STRING("-//W3C//DTD HTML 4.0 Transitional//"),
  GUMBO_STRING("-//W3C//DTD HTML Experimental 19960712//"),
  GUMBO_STRING("-//W3C//DTD HTML Experimental 970421//"),
  GUMBO_STRING("-//W3C//DTD W3 HTML//"),
  GUMBO_STRING("-//W3O//DTD W3 HTML 3.0//"),
  GUMBO_STRING("-//WebTechs//DTD Mozilla HTML 2.0//"),
  GUMBO_STRING("-//WebTechs//DTD Mozilla HTML//"),
  TERMINATOR
};

static const GumboStringPiece kQuirksModePublicIdExactMatches[] = {
  GUMBO_STRING("-//W3O//DTD W3 HTML Strict 3.0//EN//"),
  GUMBO_STRING("-/W3C/DTD HTML 4.0 Transitional/EN"),
  GUMBO_STRING("HTML"),
  TERMINATOR
};

static const GumboStringPiece kQuirksModeSystemIdExactMatches[] = {
  GUMBO_STRING("http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd"),
  TERMINATOR
};

static const GumboStringPiece kLimitedQuirksPublicIdPrefixes[] = {
  GUMBO_STRING("-//W3C//DTD XHTML 1.0 Frameset//"),
  GUMBO_STRING("-//W3C//DTD XHTML 1.0 Transitional//"),
  TERMINATOR
};

static const GumboStringPiece kLimitedQuirksRequiresSystemIdPublicIdPrefixes[] = {
  GUMBO_STRING("-//W3C//DTD HTML 4.01 Frameset//"),
  GUMBO_STRING("-//W3C//DTD HTML 4.01 Transitional//"),
  TERMINATOR
};

// Indexed by GumboNamespaceEnum; keep in sync with that.
static const char* kLegalXmlns[] = {
  "http://www.w3.org/1999/xhtml",
  "http://www.w3.org/2000/svg",
  "http://www.w3.org/1998/Math/MathML"
};

typedef struct _ReplacementEntry {
  const GumboStringPiece from;
  const GumboStringPiece to;
} ReplacementEntry;

#define REPLACEMENT_ENTRY(from, to) \
    { GUMBO_STRING(from), GUMBO_STRING(to) }

// Static data for SVG attribute replacements.
// http://www.whatwg.org/specs/web-apps/current-work/multipage/tree-construction.html#adjust-svg-attributes
static const ReplacementEntry kSvgAttributeReplacements[] = {
  REPLACEMENT_ENTRY("attributename", "attributeName"),
  REPLACEMENT_ENTRY("attributetype", "attributeType"),
  REPLACEMENT_ENTRY("basefrequency", "baseFrequency"),
  REPLACEMENT_ENTRY("baseprofile", "baseProfile"),
  REPLACEMENT_ENTRY("calcmode", "calcMode"),
  REPLACEMENT_ENTRY("clippathunits", "clipPathUnits"),
  REPLACEMENT_ENTRY("contentscripttype", "contentScriptType"),
  REPLACEMENT_ENTRY("contentstyletype", "contentStyleType"),
  REPLACEMENT_ENTRY("diffuseconstant", "diffuseConstant"),
  REPLACEMENT_ENTRY("edgemode", "edgeMode"),
  REPLACEMENT_ENTRY("externalresourcesrequired", "externalResourcesRequired"),
  REPLACEMENT_ENTRY("filterres", "filterRes"),
  REPLACEMENT_ENTRY("filterunits", "filterUnits"),
  REPLACEMENT_ENTRY("glyphref", "glyphRef"),
  REPLACEMENT_ENTRY("gradienttransform", "gradientTransform"),
  REPLACEMENT_ENTRY("gradientunits", "gradientUnits"),
  REPLACEMENT_ENTRY("kernelmatrix", "kernelMatrix"),
  REPLACEMENT_ENTRY("kernelunitlength", "kernelUnitLength"),
  REPLACEMENT_ENTRY("keypoints", "keyPoints"),
  REPLACEMENT_ENTRY("keysplines", "keySplines"),
  REPLACEMENT_ENTRY("keytimes", "keyTimes"),
  REPLACEMENT_ENTRY("lengthadjust", "lengthAdjust"),
  REPLACEMENT_ENTRY("limitingconeangle", "limitingConeAngle"),
  REPLACEMENT_ENTRY("markerheight", "markerHeight"),
  REPLACEMENT_ENTRY("markerunits", "markerUnits"),
  REPLACEMENT_ENTRY("markerwidth", "markerWidth"),
  REPLACEMENT_ENTRY("maskcontentunits", "maskContentUnits"),
  REPLACEMENT_ENTRY("maskunits", "maskUnits"),
  REPLACEMENT_ENTRY("numoctaves", "numOctaves"),
  REPLACEMENT_ENTRY("pathlength", "pathLength"),
  REPLACEMENT_ENTRY("patterncontentunits", "patternContentUnits"),
  REPLACEMENT_ENTRY("patterntransform", "patternTransform"),
  REPLACEMENT_ENTRY("patternunits", "patternUnits"),
  REPLACEMENT_ENTRY("pointsatx", "pointsAtX"),
  REPLACEMENT_ENTRY("pointsaty", "pointsAtY"),
  REPLACEMENT_ENTRY("pointsatz", "pointsAtZ"),
  REPLACEMENT_ENTRY("preservealpha", "preserveAlpha"),
  REPLACEMENT_ENTRY("preserveaspectratio", "preserveAspectRatio"),
  REPLACEMENT_ENTRY("primitiveunits", "primitiveUnits"),
  REPLACEMENT_ENTRY("refx", "refX"),
  REPLACEMENT_ENTRY("refy", "refY"),
  REPLACEMENT_ENTRY("repeatcount", "repeatCount"),
  REPLACEMENT_ENTRY("repeatdur", "repeatDur"),
  REPLACEMENT_ENTRY("requiredextensions", "requiredExtensions"),
  REPLACEMENT_ENTRY("requiredfeatures", "requiredFeatures"),
  REPLACEMENT_ENTRY("specularconstant", "specularConstant"),
  REPLACEMENT_ENTRY("specularexponent", "specularExponent"),
  REPLACEMENT_ENTRY("spreadmethod", "spreadMethod"),
  REPLACEMENT_ENTRY("startoffset", "startOffset"),
  REPLACEMENT_ENTRY("stddeviation", "stdDeviation"),
  REPLACEMENT_ENTRY("stitchtiles", "stitchTiles"),
  REPLACEMENT_ENTRY("surfacescale", "surfaceScale"),
  REPLACEMENT_ENTRY("systemlanguage", "systemLanguage"),
  REPLACEMENT_ENTRY("tablevalues", "tableValues"),
  REPLACEMENT_ENTRY("targetx", "targetX"),
  REPLACEMENT_ENTRY("targety", "targetY"),
  REPLACEMENT_ENTRY("textlength", "textLength"),
  REPLACEMENT_ENTRY("viewbox", "viewBox"),
  REPLACEMENT_ENTRY("viewtarget", "viewTarget"),
  REPLACEMENT_ENTRY("xchannelselector", "xChannelSelector"),
  REPLACEMENT_ENTRY("ychannelselector", "yChannelSelector"),
  REPLACEMENT_ENTRY("zoomandpan", "zoomAndPan"),
};

static const ReplacementEntry kSvgTagReplacements[] = {
  REPLACEMENT_ENTRY("altglyph", "altGlyph"),
  REPLACEMENT_ENTRY("altglyphdef", "altGlyphDef"),
  REPLACEMENT_ENTRY("altglyphitem", "altGlyphItem"),
  REPLACEMENT_ENTRY("animatecolor", "animateColor"),
  REPLACEMENT_ENTRY("animatemotion", "animateMotion"),
  REPLACEMENT_ENTRY("animatetransform", "animateTransform"),
  REPLACEMENT_ENTRY("clippath", "clipPath"),
  REPLACEMENT_ENTRY("feblend", "feBlend"),
  REPLACEMENT_ENTRY("fecolormatrix", "feColorMatrix"),
  REPLACEMENT_ENTRY("fecomponenttransfer", "feComponentTransfer"),
  REPLACEMENT_ENTRY("fecomposite", "feComposite"),
  REPLACEMENT_ENTRY("feconvolvematrix", "feConvolveMatrix"),
  REPLACEMENT_ENTRY("fediffuselighting", "feDiffuseLighting"),
  REPLACEMENT_ENTRY("fedisplacementmap", "feDisplacementMap"),
  REPLACEMENT_ENTRY("fedistantlight", "feDistantLight"),
  REPLACEMENT_ENTRY("feflood", "feFlood"),
  REPLACEMENT_ENTRY("fefunca", "feFuncA"),
  REPLACEMENT_ENTRY("fefuncb", "feFuncB"),
  REPLACEMENT_ENTRY("fefuncg", "feFuncG"),
  REPLACEMENT_ENTRY("fefuncr", "feFuncR"),
  REPLACEMENT_ENTRY("fegaussianblur", "feGaussianBlur"),
  REPLACEMENT_ENTRY("feimage", "feImage"),
  REPLACEMENT_ENTRY("femerge", "feMerge"),
  REPLACEMENT_ENTRY("femergenode", "feMergeNode"),
  REPLACEMENT_ENTRY("femorphology", "feMorphology"),
  REPLACEMENT_ENTRY("feoffset", "feOffset"),
  REPLACEMENT_ENTRY("fepointlight", "fePointLight"),
  REPLACEMENT_ENTRY("fespecularlighting", "feSpecularLighting"),
  REPLACEMENT_ENTRY("fespotlight", "feSpotLight"),
  REPLACEMENT_ENTRY("fetile", "feTile"),
  REPLACEMENT_ENTRY("feturbulence", "feTurbulence"),
  REPLACEMENT_ENTRY("foreignobject", "foreignObject"),
  REPLACEMENT_ENTRY("glyphref", "glyphRef"),
  REPLACEMENT_ENTRY("lineargradient", "linearGradient"),
  REPLACEMENT_ENTRY("radialgradient", "radialGradient"),
  REPLACEMENT_ENTRY("textpath", "textPath"),
};

typedef struct _NamespacedAttributeReplacement {
  const char* from;
  const char* local_name;
  const GumboAttributeNamespaceEnum attr_namespace;
} NamespacedAttributeReplacement;

static const NamespacedAttributeReplacement kForeignAttributeReplacements[] = {
  { "xlink:actuate", "actuate", GUMBO_ATTR_NAMESPACE_XLINK },
  { "xlink:actuate", "actuate", GUMBO_ATTR_NAMESPACE_XLINK },
  { "xlink:href", "href", GUMBO_ATTR_NAMESPACE_XLINK },
  { "xlink:role", "role", GUMBO_ATTR_NAMESPACE_XLINK },
  { "xlink:show", "show", GUMBO_ATTR_NAMESPACE_XLINK },
  { "xlink:title", "title", GUMBO_ATTR_NAMESPACE_XLINK },
  { "xlink:type", "type", GUMBO_ATTR_NAMESPACE_XLINK },
  { "xml:base", "base", GUMBO_ATTR_NAMESPACE_XML },
  { "xml:lang", "lang", GUMBO_ATTR_NAMESPACE_XML },
  { "xml:space", "space", GUMBO_ATTR_NAMESPACE_XML },
  { "xmlns", "xmlns", GUMBO_ATTR_NAMESPACE_XMLNS },
  { "xmlns:xlink", "xlink", GUMBO_ATTR_NAMESPACE_XMLNS },
};

// The "scope marker" for the list of active formatting elements.  We use a
// pointer to this as a generic marker element, since the particular element
// scope doesn't matter.
static const GumboNode kActiveFormattingScopeMarker;

// The tag_is and tag_in function use true & false to denote start & end tags,
// but for readability, we define constants for them here.
static const bool kStartTag = true;
static const bool kEndTag = false;

// Because GumboStringPieces are immutable, we can't insert a character directly
// into a text node.  Instead, we accumulate all pending characters here and
// flush them out to a text node whenever a new element is inserted.
//
// http://www.whatwg.org/specs/web-apps/current-work/complete/tokenization.html#insert-a-character
typedef struct _TextNodeBufferState {
  // The accumulated text to be inserted into the current text node.
  GumboStringBuffer _buffer;

  // A pointer to the original text represented by this text node.  Note that
  // because of foster parenting and other strange DOM manipulations, this may
  // include other non-text HTML tags in it; it is defined as the span of
  // original text from the first character in this text node to the last
  // character in this text node.
  const char* _start_original_text;

  // The source position of the start of this text node.
  GumboSourcePosition _start_position;

  // The type of node that will be inserted (TEXT or WHITESPACE).
  GumboNodeType _type;
} TextNodeBufferState;

typedef struct GumboInternalParserState {
  // http://www.whatwg.org/specs/web-apps/current-work/complete/parsing.html#insertion-mode
  GumboInsertionMode _insertion_mode;

  // Used for run_generic_parsing_algorithm, which needs to switch back to the
  // original insertion mode at its conclusion.
  GumboInsertionMode _original_insertion_mode;

  // http://www.whatwg.org/specs/web-apps/current-work/complete/parsing.html#the-stack-of-open-elements
  GumboVector /*GumboNode*/ _open_elements;

  // http://www.whatwg.org/specs/web-apps/current-work/complete/parsing.html#the-list-of-active-formatting-elements
  GumboVector /*GumboNode*/ _active_formatting_elements;

  // The stack of template insertion modes.
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/parsing.html#the-insertion-mode
  GumboVector /*InsertionMode*/ _template_insertion_modes;

  // http://www.whatwg.org/specs/web-apps/current-work/complete/parsing.html#the-element-pointers
  GumboNode* _head_element;
  GumboNode* _form_element;

  // The flag for when the spec says "Reprocess the current token in..."
  bool _reprocess_current_token;

  // The flag for "acknowledge the token's self-closing flag".
  bool _self_closing_flag_acknowledged;

  // The "frameset-ok" flag from the spec.
  bool _frameset_ok;

  // The flag for "If the next token is a LINE FEED, ignore that token...".
  bool _ignore_next_linefeed;

  // The flag for "whenever a node would be inserted into the current node, it
  // must instead be foster parented".  This is used for misnested table
  // content, which needs to be handled according to "in body" rules yet foster
  // parented outside of the table.
  // It would perhaps be more explicit to have this as a parameter to
  // handle_in_body and insert_element, but given how special-purpose this is
  // and the number of call-sites that would need to take the extra parameter,
  // it's easier just to have a state flag.
  bool _foster_parent_insertions;

  // The accumulated text node buffer state.
  TextNodeBufferState _text_node;

  // The current token.
  GumboToken* _current_token;

  // The way that the spec is written, the </body> and </html> tags are *always*
  // implicit, because encountering one of those tokens merely switches the
  // insertion mode out of "in body".  So we have individual state flags for
  // those end tags that are then inspected by pop_current_node when the <body>
  // and <html> nodes are popped to set the GUMBO_INSERTION_IMPLICIT_END_TAG
  // flag appropriately.
  bool _closed_body_tag;
  bool _closed_html_tag;
} GumboParserState;

static bool token_has_attribute(const GumboToken* token, const char* name) {
  assert(token->type == GUMBO_TOKEN_START_TAG);
  return gumbo_get_attribute(&token->v.start_tag.attributes, name) != NULL;
}

// Checks if the value of the specified attribute is a case-insensitive match
// for the specified string.
static bool attribute_matches(
    const GumboVector* attributes, const char* name, const char* value) {
  const GumboAttribute* attr = gumbo_get_attribute(attributes, name);
  return attr ? strcasecmp(value, attr->value) == 0 : false;
}

// Checks if the value of the specified attribute is a case-sensitive match
// for the specified string.
static bool attribute_matches_case_sensitive(
    const GumboVector* attributes, const char* name, const char* value) {
  const GumboAttribute* attr = gumbo_get_attribute(attributes, name);
  return attr ?  strcmp(value, attr->value) == 0 : false;
}

// Checks if the specified attribute vectors are identical.
static bool all_attributes_match(
    const GumboVector* attr1, const GumboVector* attr2) {
  int num_unmatched_attr2_elements = attr2->length;
  for (int i = 0; i < attr1->length; ++i) {
    const GumboAttribute* attr = attr1->data[i];
    if (attribute_matches_case_sensitive(attr2, attr->name, attr->value)) {
      --num_unmatched_attr2_elements;
    } else {
      return false;
    }
  }
  return num_unmatched_attr2_elements == 0;
}

static void set_frameset_not_ok(GumboParser* parser) {
  gumbo_debug("Setting frameset_ok to false.\n");
  parser->_parser_state->_frameset_ok = false;
}

static GumboNode* create_node(GumboParser* parser, GumboNodeType type) {
  GumboNode* node = gumbo_parser_allocate(parser, sizeof(GumboNode));
  node->parent = NULL;
  node->index_within_parent = -1;
  node->type = type;
  node->parse_flags = GUMBO_INSERTION_NORMAL;
  return node;
}

static GumboNode* new_document_node(GumboParser* parser) {
  GumboNode* document_node = create_node(parser, GUMBO_NODE_DOCUMENT);
  document_node->parse_flags = GUMBO_INSERTION_BY_PARSER;
  gumbo_vector_init(
      parser, 1, &document_node->v.document.children);

  // Must be initialized explicitly, as there's no guarantee that we'll see a
  // doc type token.
  GumboDocument* document = &document_node->v.document;
  document->has_doctype = false;
  document->name = NULL;
  document->public_identifier = NULL;
  document->system_identifier = NULL;
  return document_node;
}

static void output_init(GumboParser* parser) {
  GumboOutput* output = gumbo_parser_allocate(parser, sizeof(GumboOutput));
  output->root = NULL;
  output->document = new_document_node(parser);
  parser->_output = output;
  gumbo_init_errors(parser);
}

static void parser_state_init(GumboParser* parser) {
  GumboParserState* parser_state =
      gumbo_parser_allocate(parser, sizeof(GumboParserState));
  parser_state->_insertion_mode = GUMBO_INSERTION_MODE_INITIAL;
  parser_state->_reprocess_current_token = false;
  parser_state->_frameset_ok = true;
  parser_state->_ignore_next_linefeed = false;
  parser_state->_foster_parent_insertions = false;
  parser_state->_text_node._type = GUMBO_NODE_WHITESPACE;
  gumbo_string_buffer_init(parser, &parser_state->_text_node._buffer);
  gumbo_vector_init(parser, 10, &parser_state->_open_elements);
  gumbo_vector_init(parser, 5, &parser_state->_active_formatting_elements);
  gumbo_vector_init(parser, 5, &parser_state->_template_insertion_modes);
  parser_state->_head_element = NULL;
  parser_state->_form_element = NULL;
  parser_state->_current_token = NULL;
  parser_state->_closed_body_tag = false;
  parser_state->_closed_html_tag = false;
  parser->_parser_state = parser_state;
}

static void parser_state_destroy(GumboParser* parser) {
  GumboParserState* state = parser->_parser_state;
  gumbo_vector_destroy(parser, &state->_active_formatting_elements);
  gumbo_vector_destroy(parser, &state->_open_elements);
  gumbo_vector_destroy(parser, &state->_template_insertion_modes);
  gumbo_string_buffer_destroy(parser, &state->_text_node._buffer);
  gumbo_parser_deallocate(parser, state);
}

static GumboNode* get_document_node(GumboParser* parser) {
  return parser->_output->document;
}

// Returns the node at the bottom of the stack of open elements, or NULL if no
// elements have been added yet.
static GumboNode* get_current_node(GumboParser* parser) {
  GumboVector* open_elements = &parser->_parser_state->_open_elements;
  if (open_elements->length == 0) {
    assert(!parser->_output->root);
    return NULL;
  }
  assert(open_elements->length > 0);
  assert(open_elements->data != NULL);
  return open_elements->data[open_elements->length - 1];
}

// Returns true if the given needle is in the given array of literal
// GumboStringPieces.  If exact_match is true, this requires that they match
// exactly; otherwise, this performs a prefix match to check if any of the
// elements in haystack start with needle.  This always performs a
// case-insensitive match.
static bool is_in_static_list(
    const char* needle, const GumboStringPiece* haystack, bool exact_match) {
  for (int i = 0; haystack[i].length > 0; ++i) {
    if ((exact_match && !strcmp(needle, haystack[i].data)) ||
        (!exact_match && !strcasecmp(needle, haystack[i].data))) {
      return true;
    }
  }
  return false;
}

static void push_template_insertion_mode(
    GumboParser* parser, GumboInsertionMode mode) {
  gumbo_vector_add(
      parser, (void*) mode, &parser->_parser_state->_template_insertion_modes);
}

static void pop_template_insertion_mode(GumboParser* parser) {
  gumbo_vector_pop(parser, &parser->_parser_state->_template_insertion_modes);
}

static GumboInsertionMode get_current_template_insertion_mode(
    GumboParser* parser) {
  GumboVector* template_insertion_modes =
      &parser->_parser_state->_template_insertion_modes;
  assert(template_insertion_modes->length > 0);
  return (GumboInsertionMode) template_insertion_modes->data[
      template_insertion_modes->length - 1];
}

static void set_insertion_mode(GumboParser* parser, GumboInsertionMode mode) {
  parser->_parser_state->_insertion_mode = mode;
}

// http://www.whatwg.org/specs/web-apps/current-work/complete/parsing.html#reset-the-insertion-mode-appropriately
// This is a helper function that returns the appropriate insertion mode instead
// of setting it.  Returns GUMBO_INSERTION_MODE_INITIAL as a sentinel value to
// indicate that there is no appropriate insertion mode, and the loop should
// continue.
static GumboInsertionMode get_appropriate_insertion_mode(
    const GumboNode* node, bool is_last) {
  assert(node->type == GUMBO_NODE_ELEMENT);
  switch (node->v.element.tag) {
    case GUMBO_TAG_SELECT:
      return GUMBO_INSERTION_MODE_IN_SELECT;
    case GUMBO_TAG_TD:
    case GUMBO_TAG_TH:
      return is_last ?
          GUMBO_INSERTION_MODE_IN_BODY : GUMBO_INSERTION_MODE_IN_CELL;
    case GUMBO_TAG_TR:
      return GUMBO_INSERTION_MODE_IN_ROW;
    case GUMBO_TAG_TBODY:
    case GUMBO_TAG_THEAD:
    case GUMBO_TAG_TFOOT:
      return GUMBO_INSERTION_MODE_IN_TABLE_BODY;
    case GUMBO_TAG_CAPTION:
      return GUMBO_INSERTION_MODE_IN_CAPTION;
    case GUMBO_TAG_COLGROUP:
      return GUMBO_INSERTION_MODE_IN_COLUMN_GROUP;
    case GUMBO_TAG_TABLE:
      return GUMBO_INSERTION_MODE_IN_TABLE;
    case GUMBO_TAG_HEAD:
    case GUMBO_TAG_BODY:
      return GUMBO_INSERTION_MODE_IN_BODY;
    case GUMBO_TAG_FRAMESET:
      return GUMBO_INSERTION_MODE_IN_FRAMESET;
    case GUMBO_TAG_HTML:
      return GUMBO_INSERTION_MODE_BEFORE_HEAD;
    default:
      return is_last ?
          GUMBO_INSERTION_MODE_IN_BODY : GUMBO_INSERTION_MODE_INITIAL;
  }
}

// This performs the actual "reset the insertion mode" loop.
static void reset_insertion_mode_appropriately(GumboParser* parser) {
  const GumboVector* open_elements = &parser->_parser_state->_open_elements;
  for (int i = open_elements->length; --i >= 0; ) {
    GumboInsertionMode mode =
        get_appropriate_insertion_mode(open_elements->data[i], i == 0);
    if (mode != GUMBO_INSERTION_MODE_INITIAL) {
      set_insertion_mode(parser, mode);
      return;
    }
  }
  // Should never get here, because is_last will be set on the last iteration
  // and will force GUMBO_INSERTION_MODE_IN_BODY.
  assert(0);
}

static GumboError* add_parse_error(GumboParser* parser, const GumboToken* token) {
  gumbo_debug("Adding parse error.\n");
  GumboError* error = gumbo_add_error(parser);
  if (!error) {
    return NULL;
  }
  error->type = GUMBO_ERR_PARSER;
  error->position = token->position;
  error->original_text = token->original_text.data;
  GumboParserError* extra_data = &error->v.parser;
  extra_data->input_type = token->type;
  extra_data->input_tag = GUMBO_TAG_UNKNOWN;
  if (token->type == GUMBO_TOKEN_START_TAG) {
    extra_data->input_tag = token->v.start_tag.tag;
  } else if (token->type == GUMBO_TOKEN_END_TAG) {
    extra_data->input_tag = token->v.end_tag;
  }
  GumboParserState* state = parser->_parser_state;
  extra_data->parser_state = state->_insertion_mode;
  gumbo_vector_init(parser, state->_open_elements.length,
                   &extra_data->tag_stack);
  for (int i = 0; i < state->_open_elements.length; ++i) {
    const GumboNode* node = state->_open_elements.data[i];
    assert(node->type == GUMBO_NODE_ELEMENT);
    gumbo_vector_add(parser, (void*) node->v.element.tag,
                    &extra_data->tag_stack);
  }
  return error;
}

// Returns true if the specified token is either a start or end tag (specified
// by is_start) with one of the tag types in the varargs list.  Terminate the
// list with GUMBO_TAG_LAST; this functions as a sentinel since no portion of
// the spec references tags that are not in the spec.
// TODO(jdtang): A lot of the tag lists for this function are repeated in many
// places in the code.  This is how it's written in the spec (and it's done this
// way so it's easy to verify the code against the spec), but it may be worth
// coming up with a notion of a "tag set" that includes a list of tags, and
// using that in many places.  It'd probably also help performance, but I want
// to profile before optimizing.
static bool tag_in(const GumboToken* token, bool is_start, ...) {
  GumboTag token_tag;
  if (is_start && token->type == GUMBO_TOKEN_START_TAG) {
    token_tag = token->v.start_tag.tag;
  } else if (!is_start && token->type == GUMBO_TOKEN_END_TAG) {
    token_tag = token->v.end_tag;
  } else {
    return false;
  }

  va_list tags;
  va_start(tags, is_start);
  bool result = false;
  for (GumboTag tag = va_arg(tags, GumboTag); tag != GUMBO_TAG_LAST;
       tag = va_arg(tags, GumboTag)) {
    if (tag == token_tag) {
      result = true;
      break;
    }
  }
  va_end(tags);
  return result;
}

// Like tag_in, but for the single-tag case.
static bool tag_is(const GumboToken* token, bool is_start, GumboTag tag) {
  if (is_start && token->type == GUMBO_TOKEN_START_TAG) {
    return token->v.start_tag.tag == tag;
  } else if (!is_start && token->type == GUMBO_TOKEN_END_TAG) {
    return token->v.end_tag == tag;
  } else {
    return false;
  }
}

// Like tag_in, but checks for the tag of a node, rather than a token.
static bool node_tag_in(const GumboNode* node, ...) {
  assert(node != NULL);
  if (node->type != GUMBO_NODE_ELEMENT) {
    return false;
  }
  GumboTag node_tag = node->v.element.tag;

  va_list tags;
  va_start(tags, node);
  bool result = false;
  for (GumboTag tag = va_arg(tags, GumboTag); tag != GUMBO_TAG_LAST;
       tag = va_arg(tags, GumboTag)) {
    assert(tag <= GUMBO_TAG_LAST);
    if (tag == node_tag) {
      result = true;
      break;
    }
  }
  va_end(tags);
  return result;
}

// Like node_tag_in, but for the single-tag case.
static bool node_tag_is(const GumboNode* node, GumboTag tag) {
  return node->type == GUMBO_NODE_ELEMENT && node->v.element.tag == tag;
}

// http://www.whatwg.org/specs/web-apps/current-work/multipage/tree-construction.html#mathml-text-integration-point
static bool is_mathml_integration_point(const GumboNode* node) {
  return node_tag_in(node, GUMBO_TAG_MI, GUMBO_TAG_MO, GUMBO_TAG_MN,
                     GUMBO_TAG_MS, GUMBO_TAG_MTEXT, GUMBO_TAG_LAST) &&
      node->v.element.tag_namespace == GUMBO_NAMESPACE_MATHML;
}

// http://www.whatwg.org/specs/web-apps/current-work/multipage/tree-construction.html#html-integration-point
static bool is_html_integration_point(const GumboNode* node) {
  return (node_tag_in(node, GUMBO_TAG_FOREIGNOBJECT, GUMBO_TAG_DESC,
                      GUMBO_TAG_TITLE, GUMBO_TAG_LAST) &&
      node->v.element.tag_namespace == GUMBO_NAMESPACE_SVG) ||
      (node_tag_is(node, GUMBO_TAG_ANNOTATION_XML) && (
          attribute_matches(&node->v.element.attributes,
                            "encoding", "text/html") ||
          attribute_matches(&node->v.element.attributes,
                            "encoding", "application/xhtml+xml")));
}

// Appends a node to the end of its parent, setting the "parent" and
// "index_within_parent" fields appropriately.
static void append_node(
    GumboParser* parser, GumboNode* parent, GumboNode* node) {
  assert(node->parent == NULL);
  assert(node->index_within_parent == -1);
  GumboVector* children;
  if (parent->type == GUMBO_NODE_ELEMENT) {
    children = &parent->v.element.children;
  } else {
    assert(parent->type == GUMBO_NODE_DOCUMENT);
    children = &parent->v.document.children;
  }
  node->parent = parent;
  node->index_within_parent = children->length;
  gumbo_vector_add(parser, (void*) node, children);
  assert(node->index_within_parent < children->length);
}

// Inserts a node at the specified index within its parent, updating the
// "parent" and "index_within_parent" fields of it and all its siblings.
static void insert_node(
    GumboParser* parser, GumboNode* parent, int index, GumboNode* node) {
  assert(node->parent == NULL);
  assert(node->index_within_parent == -1);
  assert(parent->type == GUMBO_NODE_ELEMENT);
  GumboVector* children = &parent->v.element.children;
  assert(index >= 0);
  assert(index < children->length);
  node->parent = parent;
  node->index_within_parent = index;
  gumbo_vector_insert_at(parser, (void*) node, index, children);
  assert(node->index_within_parent < children->length);
  for (int i = index + 1; i < children->length; ++i) {
    GumboNode* sibling = children->data[i];
    sibling->index_within_parent = i;
    assert(sibling->index_within_parent < children->length);
  }
}

// http://www.whatwg.org/specs/web-apps/current-work/complete/tokenization.html#foster-parenting
static void foster_parent_element(GumboParser* parser, GumboNode* node) {
  GumboVector* open_elements = &parser->_parser_state->_open_elements;
  assert(open_elements->length > 2);

  node->parse_flags |= GUMBO_INSERTION_FOSTER_PARENTED;
  GumboNode* foster_parent_element = open_elements->data[0];
  assert(foster_parent_element->type == GUMBO_NODE_ELEMENT);
  assert(node_tag_is(foster_parent_element, GUMBO_TAG_HTML));
  for (int i = open_elements->length; --i > 1; ) {
    GumboNode* table_element = open_elements->data[i];
    if (node_tag_is(table_element, GUMBO_TAG_TABLE)) {
      foster_parent_element = table_element->parent;
      if (!foster_parent_element ||
          foster_parent_element->type != GUMBO_NODE_ELEMENT) {
        // Table has no parent; spec says it's possible if a script manipulated
        // the DOM, although I don't think we have to worry about this case.
        gumbo_debug("Table has no parent.\n");
        foster_parent_element = open_elements->data[i - 1];
        break;
      }
      assert(foster_parent_element->type == GUMBO_NODE_ELEMENT);
      gumbo_debug("Found enclosing table (%x) at %d; parent=%s, index=%d.\n",
                 table_element, i, gumbo_normalized_tagname(
                     foster_parent_element->v.element.tag),
                 table_element->index_within_parent);
      assert(foster_parent_element->v.element.children.data[
             table_element->index_within_parent] == table_element);
      insert_node(parser, foster_parent_element,
                  table_element->index_within_parent, node);
      return;
    }
  }
  if (node->type == GUMBO_NODE_ELEMENT) {
    gumbo_vector_add(parser, (void*) node, open_elements);
  }
  append_node(parser, foster_parent_element, node);
}

static void maybe_flush_text_node_buffer(GumboParser* parser) {
  GumboParserState* state = parser->_parser_state;
  TextNodeBufferState* buffer_state = &state->_text_node;
  if (buffer_state->_buffer.length == 0) {
    return;
  }

  assert(buffer_state->_type == GUMBO_NODE_WHITESPACE ||
         buffer_state->_type == GUMBO_NODE_TEXT);
  GumboNode* text_node = create_node(parser, buffer_state->_type);
  GumboText* text_node_data = &text_node->v.text;
  text_node_data->text = gumbo_string_buffer_to_string(
      parser, &buffer_state->_buffer);
  text_node_data->original_text.data = buffer_state->_start_original_text;
  text_node_data->original_text.length =
      state->_current_token->original_text.data -
      buffer_state->_start_original_text;
  text_node_data->start_pos = buffer_state->_start_position;
  if (state->_foster_parent_insertions && node_tag_in(
      get_current_node(parser), GUMBO_TAG_TABLE, GUMBO_TAG_TBODY,
      GUMBO_TAG_TFOOT, GUMBO_TAG_THEAD, GUMBO_TAG_TR, GUMBO_TAG_LAST)) {
    foster_parent_element(parser, text_node);
  } else {
    append_node(
        parser, parser->_output->root ?
        get_current_node(parser) : parser->_output->document, text_node);
  }
  gumbo_debug("Flushing text node buffer of %.*s.\n",
             (int) buffer_state->_buffer.length, buffer_state->_buffer.data);

  gumbo_string_buffer_destroy(parser, &buffer_state->_buffer);
  gumbo_string_buffer_init(parser, &buffer_state->_buffer);
  buffer_state->_type = GUMBO_NODE_WHITESPACE;
  assert(buffer_state->_buffer.length == 0);
}

static void record_end_of_element(
    GumboToken* current_token, GumboElement* element) {
  element->end_pos = current_token->position;
  element->original_end_tag =
      current_token->type == GUMBO_TOKEN_END_TAG ?
      current_token->original_text : kGumboEmptyString;
}

static GumboNode* pop_current_node(GumboParser* parser) {
  GumboParserState* state = parser->_parser_state;
  maybe_flush_text_node_buffer(parser);
  if (state->_open_elements.length > 0) {
    assert(node_tag_is(state->_open_elements.data[0], GUMBO_TAG_HTML));
    gumbo_debug(
        "Popping %s node.\n",
        gumbo_normalized_tagname(get_current_node(parser)->v.element.tag));
  }
  GumboNode* current_node = gumbo_vector_pop(parser, &state->_open_elements);
  if (!current_node) {
    assert(state->_open_elements.length == 0);
    return NULL;
  }
  assert(current_node->type == GUMBO_NODE_ELEMENT);
  bool is_closed_body_or_html_tag =
      (node_tag_is(current_node, GUMBO_TAG_BODY) && state->_closed_body_tag) ||
      (node_tag_is(current_node, GUMBO_TAG_HTML) && state->_closed_html_tag);
  if ((state->_current_token->type != GUMBO_TOKEN_END_TAG ||
       !node_tag_is(current_node, state->_current_token->v.end_tag)) &&
       !is_closed_body_or_html_tag) {
    current_node->parse_flags |= GUMBO_INSERTION_IMPLICIT_END_TAG;
  }
  if (!is_closed_body_or_html_tag) {
    record_end_of_element(state->_current_token, &current_node->v.element);
  }
  return current_node;
}

static void append_comment_node(
    GumboParser* parser, GumboNode* node, const GumboToken* token) {
  maybe_flush_text_node_buffer(parser);
  GumboNode* comment = create_node(parser, GUMBO_NODE_COMMENT);
  comment->type = GUMBO_NODE_COMMENT;
  comment->parse_flags = GUMBO_INSERTION_NORMAL;
  comment->v.text.text = token->v.text;
  comment->v.text.original_text = token->original_text;
  comment->v.text.start_pos = token->position;
  append_node(parser, node, comment);
}

// http://www.whatwg.org/specs/web-apps/current-work/complete/tokenization.html#clear-the-stack-back-to-a-table-row-context
static void clear_stack_to_table_row_context(GumboParser* parser) {
  while (!node_tag_in(get_current_node(parser),
                      GUMBO_TAG_HTML, GUMBO_TAG_TR, GUMBO_TAG_LAST)) {
    pop_current_node(parser);
  }
}

// http://www.whatwg.org/specs/web-apps/current-work/complete/tokenization.html#clear-the-stack-back-to-a-table-context
static void clear_stack_to_table_context(GumboParser* parser) {
  while (!node_tag_in(get_current_node(parser),
                      GUMBO_TAG_HTML, GUMBO_TAG_TABLE, GUMBO_TAG_LAST)) {
    pop_current_node(parser);
  }
}

// http://www.whatwg.org/specs/web-apps/current-work/complete/tokenization.html#clear-the-stack-back-to-a-table-body-context
void clear_stack_to_table_body_context(GumboParser* parser) {
  while (!node_tag_in(get_current_node(parser), GUMBO_TAG_HTML,
                      GUMBO_TAG_TBODY, GUMBO_TAG_TFOOT, GUMBO_TAG_THEAD,
                      GUMBO_TAG_LAST)) {
    pop_current_node(parser);
  }
}

// Creates a parser-inserted element in the HTML namespace and returns it.
static GumboNode* create_element(GumboParser* parser, GumboTag tag) {
  GumboNode* node = create_node(parser, GUMBO_NODE_ELEMENT);
  GumboElement* element = &node->v.element;
  gumbo_vector_init(parser, 1, &element->children);
  gumbo_vector_init(parser, 0, &element->attributes);
  element->tag = tag;
  element->tag_namespace = GUMBO_NAMESPACE_HTML;
  element->original_tag = kGumboEmptyString;
  element->original_end_tag = kGumboEmptyString;
  element->start_pos = parser->_parser_state->_current_token->position;
  element->end_pos = kGumboEmptySourcePosition;
  return node;
}

// Constructs an element from the given start tag token.
static GumboNode* create_element_from_token(
    GumboParser* parser, GumboToken* token, GumboNamespaceEnum tag_namespace) {
  assert(token->type == GUMBO_TOKEN_START_TAG);
  GumboTokenStartTag* start_tag = &token->v.start_tag;

  GumboNode* node = create_node(parser, GUMBO_NODE_ELEMENT);
  GumboElement* element = &node->v.element;
  gumbo_vector_init(parser, 1, &element->children);
  element->attributes = start_tag->attributes;
  element->tag = start_tag->tag;
  element->tag_namespace = tag_namespace;

  assert(token->original_text.length >= 2);
  assert(token->original_text.data[0] == '<');
  assert(token->original_text.data[token->original_text.length - 1] == '>');
  element->original_tag = token->original_text;
  element->start_pos = token->position;
  element->original_end_tag = kGumboEmptyString;
  element->end_pos = kGumboEmptySourcePosition;

  // The element takes ownership of the attributes from the token, so any
  // allocated-memory fields should be nulled out.
  start_tag->attributes = kGumboEmptyVector;
  return node;
}

// http://www.whatwg.org/specs/web-apps/current-work/complete/tokenization.html#insert-an-html-element
static void insert_element(GumboParser* parser, GumboNode* node,
                           bool is_reconstructing_formatting_elements) {
  GumboParserState* state = parser->_parser_state;
  // NOTE(jdtang): The text node buffer must always be flushed before inserting
  // a node, otherwise we're handling nodes in a different order than the spec
  // mandated.  However, one clause of the spec (character tokens in the body)
  // requires that we reconstruct the active formatting elements *before* adding
  // the character, and reconstructing the active formatting elements may itself
  // result in the insertion of new elements (which should be pushed onto the
  // stack of open elements before the buffer is flushed).  We solve this (for
  // the time being, the spec has been rewritten for <template> and the new
  // version may be simpler here) with a boolean flag to this method.
  if (!is_reconstructing_formatting_elements) {
    maybe_flush_text_node_buffer(parser);
  }
  if (state->_foster_parent_insertions && node_tag_in(
      get_current_node(parser), GUMBO_TAG_TABLE, GUMBO_TAG_TBODY,
      GUMBO_TAG_TFOOT, GUMBO_TAG_THEAD, GUMBO_TAG_TR, GUMBO_TAG_LAST)) {
    foster_parent_element(parser, node);
    gumbo_vector_add(parser, (void*) node, &state->_open_elements);
    return;
  }

  // This is called to insert the root HTML element, but get_current_node
  // assumes the stack of open elements is non-empty, so we need special
  // handling for this case.
  append_node(
      parser, parser->_output->root ?
      get_current_node(parser) : parser->_output->document, node);
  gumbo_vector_add(parser, (void*) node, &state->_open_elements);
}

// Convenience method that combines create_element_from_token and
// insert_element, inserting the generated element directly into the current
// node.  Returns the node inserted.
static GumboNode* insert_element_from_token(
    GumboParser* parser, GumboToken* token) {
  GumboNode* element =
      create_element_from_token(parser, token, GUMBO_NAMESPACE_HTML);
  insert_element(parser, element, false);
  gumbo_debug("Inserting <%s> element (@%x) from token.\n",
             gumbo_normalized_tagname(element->v.element.tag), element);
  return element;
}

// Convenience method that combines create_element and insert_element, inserting
// a parser-generated element of a specific tag type.  Returns the node
// inserted.
static GumboNode* insert_element_of_tag_type(
    GumboParser* parser, GumboTag tag, GumboParseFlags reason) {
  GumboNode* element = create_element(parser, tag);
  element->parse_flags |= GUMBO_INSERTION_BY_PARSER | reason;
  insert_element(parser, element, false);
  gumbo_debug("Inserting %s element (@%x) from tag type.\n",
             gumbo_normalized_tagname(tag), element);
  return element;
}

// Convenience method for creating foreign namespaced element.  Returns the node
// inserted.
static GumboNode* insert_foreign_element(
    GumboParser* parser, GumboToken* token, GumboNamespaceEnum tag_namespace) {
  assert(token->type == GUMBO_TOKEN_START_TAG);
  GumboNode* element = create_element_from_token(parser, token, tag_namespace);
  insert_element(parser, element, false);
  if (token_has_attribute(token, "xmlns") &&
      !attribute_matches_case_sensitive(
          &token->v.start_tag.attributes, "xmlns",
          kLegalXmlns[tag_namespace])) {
    // TODO(jdtang): Since there're multiple possible error codes here, we
    // eventually need reason codes to differentiate them.
    add_parse_error(parser, token);
  }
  if (token_has_attribute(token, "xmlns:xlink") &&
      !attribute_matches_case_sensitive(
          &token->v.start_tag.attributes,
          "xmlns:xlink", "http://www.w3.org/1999/xlink")) {
    add_parse_error(parser, token);
  }
  return element;
}

static void insert_text_token(GumboParser* parser, GumboToken* token) {
  assert(token->type == GUMBO_TOKEN_WHITESPACE ||
         token->type == GUMBO_TOKEN_CHARACTER);
  TextNode