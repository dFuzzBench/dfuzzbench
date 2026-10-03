/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2020, assimp team


All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------
*/

/** @file  FBXParser.cpp
 *  @brief Implementation of the FBX parser and the rudimentary DOM that we use
 */

#ifndef ASSIMP_BUILD_NO_FBX_IMPORTER

#ifdef ASSIMP_BUILD_NO_OWN_ZLIB
#   include <zlib.h>
#else
#   include "../contrib/zlib/zlib.h"
#endif

#include "FBXTokenizer.h"
#include "FBXParser.h"
#include "FBXUtil.h"

#include <assimp/ParsingUtils.h>
#include <assimp/fast_atof.h>
#include <assimp/ByteSwapper.h>
#include <assimp/DefaultLogger.hpp>

#include <iostream>

using namespace Assimp;
using namespace Assimp::FBX;

namespace {

    // ------------------------------------------------------------------------------------------------
    // signal parse error, this is always unrecoverable. Throws DeadlyImportError.
    AI_WONT_RETURN void ParseError(const std::string& message, const Token& token) AI_WONT_RETURN_SUFFIX;
    AI_WONT_RETURN void ParseError(const std::string& message, const Token& token)
    {
        throw DeadlyImportError(Util::AddTokenText("FBX-Parser",message,&token));
    }

    // ------------------------------------------------------------------------------------------------
    AI_WONT_RETURN void ParseError(const std::string &message, const Element *element = nullptr) AI_WONT_RETURN_SUFFIX;
    AI_WONT_RETURN void ParseError(const std::string& message, const Element* element)
    {
        if(element) {
            ParseError(message,element->KeyToken());
        }
        throw DeadlyImportError("FBX-Parser " + message);
    }


    // ------------------------------------------------------------------------------------------------
    void ParseError(const std::string& message, TokenPtr token)
    {
        if(token) {
            ParseError(message, *token);
        }
        ParseError(message);
    }

    // Initially, we did reinterpret_cast, breaking strict aliasing rules.
    // This actually caused trouble on Android, so let's be safe this time.
    // https://github.com/assimp/assimp/issues/24
    template <typename T>
    T SafeParse(const char* data, const char* end) {
        // Actual size validation happens during Tokenization so
        // this is valid as an assertion.
        (void)(end);
        ai_assert(static_cast<size_t>(end - data) >= sizeof(T));
        T result = static_cast<T>(0);
        ::memcpy(&result, data, sizeof(T));
        return result;
    }
}

namespace Assimp {
namespace FBX {

// ------------------------------------------------------------------------------------------------
Element::Element(const Token& key_token, Parser& parser)
: key_token(key_token)
{
    TokenPtr n = nullptr;
    do {
        n = parser.AdvanceToNextToken();
        if(!n) {
            ParseError("unexpected end of file, expected closing bracket",parser.LastToken());
        }

        if (n->Type() == TokenType_DATA) {
            tokens.push_back(n);
			TokenPtr prev = n;
            n = parser.AdvanceToNextToken();
            if(!n) {
                ParseError("unexpected end of file, expected bracket, comma or key",parser.LastToken());
            }

			const TokenType ty = n->Type();

			// some exporters are missing a comma on the next line
			if (ty == TokenType_DATA && prev->Type() == TokenType_DATA && (n->Line() == prev->Line() + 1)) {
				tokens.push_back(n);
				continue;
			}

            if (ty != TokenType_OPEN_BRACKET && ty != TokenType_CLOSE_BRACKET && ty != TokenType_COMMA && ty != TokenType_KEY) {
                ParseError("unexpected token; expected bracket, comma or key",n);
            }
        }

        if (n->Type() == TokenType_OPEN_BRACKET) {
            compound.reset(new Scope(parser));

            // current token should be a TOK_CLOSE_BRACKET
            n = parser.CurrentToken();
            ai_assert(n);

            if (n->Type() != TokenType_CLOSE_BRACKET) {
                ParseError("expected closing bracket",n);
            }

            parser.AdvanceToNextToken();
            return;
        }
    }
    while(n->Type() != TokenType_KEY && n->Type() != TokenType_CLOSE_BRACKET);
}

// ------------------------------------------------------------------------------------------------
Element::~Element()
{
     // no need to delete tokens, they are owned by the parser
}

// ------------------------------------------------------------------------------------------------
Scope::Scope(Parser& parser,bool topLevel)
{
    if(!topLevel) {
        TokenPtr t = parser.CurrentToken();
        if (t->Type() != TokenType_OPEN_BRACKET) {
            ParseError("expected open bracket",t);
        }
    }

    TokenPtr n = parser.AdvanceToNextToken();
    if (n == nullptr) {
        ParseError("unexpected end of file");
    }

    // note: empty scopes are allowed
    while(n->Type() != TokenType_CLOSE_BRACKET) {
        if (n->Type() != TokenType_KEY) {
            ParseError("unexpected token, expected TOK_KEY",n);
        }

        const std::string& str = n->StringContents();
        elements.insert(ElementMap::value_type(str,new_Element(*n,parser)));

        // Element() should stop at the next Key token (or right after a Close token)
        n = parser.CurrentToken();
        if (n == nullptr) {
            if (topLevel) {
                return;
            }
            ParseError("unexpected end of file",parser.LastToken());
        }
    }
}

// ------------------------------------------------------------------------------------------------
Scope::~Scope()
{
    for(ElementMap::value_type& v : elements) {
        delete v.second;
    }
}

// ------------------------------------------------------------------------------------------------
Parser::Parser (const TokenList& tokens, bool is_binary)
: tokens(tokens)
, last()
, current()
, cursor(tokens.begin())
, is_binary(is_binary)
{
    ASSIMP_LOG_DEBUG("Parsing FBX tokens");
    root.reset(new Scope(*this,true));
}

// ------------------------------------------------------------------------------------------------
Parser::~Parser()
{
    // empty
}

// ------------------------------------------------------------------------------------------------
TokenPtr Parser::AdvanceToNextToken()
{
    last = current;
    if (cursor == tokens.end()) {
        current = nullptr;
    } else {
        current = *cursor++;
    }
    return current;
}

// ------------------------------------------------------------------------------------------------
TokenPtr Parser::CurrentToken() const
{
    return current;
}

// ------------------------------------------------------------------------------------------------
TokenPtr Parser::LastToken() const
{
    return last;
}

// ------------------------------------------------------------------------------------------------
uint64_t ParseTokenAsID(const Token& t, const char*& err_out)
{
    err_out = nullptr;

    if (t.Type() != TokenType_DATA) {
        err_out = "expected TOK_DATA token";
        return 0L;
    }

    if(t.IsBinary())
    {
        const char* data = t.begin();
        if (data[0] != 'L') {
            err_out = "failed to parse ID, unexpected data type, expected L(ong) (binary)";
            return 0L;
        }

        BE_NCONST uint64_t id = SafeParse<uint64_t>(data+1, t.end());
        AI_SWAP8(id);
        return id;
    }

    // XXX: should use size_t here
    unsigned int length = static_cast<unsigned int>(t.end() - t.begin());
    ai_assert(length > 0);

    const char* out = nullptr;
    const uint64_t id = strtoul10_64(t.begin(),&out,&length);
    if (out > t.end()) {
        err_out = "failed to parse ID (text)";
        return 0L;
    }

    return id;
}

// ------------------------------------------------------------------------------------------------
size_t ParseTokenAsDim(const Token& t, const char*& err_out)
{
    // same as ID parsing, except there is a trailing asterisk
    err_out = nullptr;

    if (t.Type() != TokenType_DATA) {
        err_out = "expected TOK_DATA token";
        return 0;
    }

    if(t.IsBinary())
    {
        const char* data = t.begin();
        if (data[0] != 'L') {
            err_out = "failed to parse ID, unexpected data type, expected L(ong) (binary)";
            return 0;
        }

        BE_NCONST uint64_t id = SafeParse<uint64_t>(data+1, t.end());
        AI_SWAP8(id);
        return static_cast<size_t>(id);
    }

    if(*t.begin() != '*') {
        err_out = "expected asterisk before array dimension";
        return 0;
    }

    // XXX: should use size_t here
    unsigned int length = static_cast<unsigned int>(t.end() - t.begin());
    if(length == 0) {
        err_out = "expected valid integer number after asterisk";
        return 0;
    }

    const char* out = nullptr;
    const size_t id = static_cast<size_t>(strtoul10_64(t.begin() + 1,&out,&length));
    if (out > t.end()) {
        err_out = "failed to parse ID";
        return 0;
    }

    return id;
}


// ------------------------------------------------------------------------------------------------
float ParseTokenAsFloat(const Token& t, const char*& err_out)
{
    err_out = nullptr;

    if (t.Type() != TokenType_DATA) {
        err_out = "expected TOK_DATA token";
        return 0.0f;
    }

    if(t.IsBinary())
    {
        const char* data = t.begin();
        if (data[0] != 'F' && data[0] != 'D') {
            err_out = "failed to parse F(loat) or D(ouble), unexpected data type (binary)";
            return 0.0f;
        }

        if (data[0] == 'F') {
            return SafeParse<float>(data+1, t.end());
        }
        else {
            return static_cast<float>( SafeParse<double>(data+1, t.end()) );
        }
    }

    // need to copy the input string to a temporary buffer
    // first - next in the fbx token stream comes ',',
    // which fast_atof could interpret as decimal point.
#define MAX_FLOAT_LENGTH 31
    const size_t length = static_cast<size_t>(t.end()-t.begin());
    if (length > MAX_FLOAT_LENGTH) {
        return 0.f;
    }

    char temp[MAX_FLOAT_LENGTH + 1];
    std::copy(t.begin(), t.end(), temp);
    temp[std::min(static_cast<size_t>(MAX_FLOAT_LENGTH),length)] = '\0';

    return fast_atof(temp);
}


// ------------------------------------------------------------------------------------------------
int ParseTokenAsInt(const Token& t, const char*& err_out)
{
    err_out = nullptr;

    if (t.Type() != TokenType_DATA) {
        err_out = "expected TOK_DATA token";
        return 0;
    }

    if(t.IsBinary())
    {
        const char* data = t.begin();
        if (data[0] != 'I') {
            err_out = "failed to parse I(nt), unexpected data type (binary)";
            return 0;
        }

        BE_NCONST int32_t ival = SafeParse<int32_t>(data+1, t.end());
        AI_SWAP4(ival);
        return static_cast<int>(ival);
    }

    ai_assert(static_cast<size_t>(t.end() - t.begin()) > 0);

    const char* out;
    const int intval = strtol10(t.begin(),&out);
    if (out != t.end()) {
        err_out = "failed to parse ID";
        return 0;
    }

    return intval;
}


// ------------------------------------------------------------------------------------------------
int64_t ParseTokenAsInt64(const Token& t, const char*& err_out)
{
    err_out = nullptr;

    if (t.Type() != TokenType_DATA) {
