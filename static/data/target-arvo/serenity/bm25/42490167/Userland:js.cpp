/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/ByteBuffer.h>
#include <AK/Format.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/StringBuilder.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibCore/StandardPaths.h>
#include <LibJS/AST.h>
#include <LibJS/Console.h>
#include <LibJS/Interpreter.h>
#include <LibJS/Parser.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/Date.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/Function.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/RegExpObject.h>
#include <LibJS/Runtime/Shape.h>
#include <LibJS/Runtime/Value.h>
#include <LibLine/Editor.h>
#include <signal.h>
#include <stdio.h>

RefPtr<JS::VM> vm;
Vector<String> repl_statements;

class ReplObject : public JS::GlobalObject {
public:
    ReplObject();
    virtual void initialize() override;
    virtual ~ReplObject() override;

private:
    virtual const char* class_name() const override { return "ReplObject"; }

    JS_DECLARE_NATIVE_FUNCTION(exit_interpreter);
    JS_DECLARE_NATIVE_FUNCTION(repl_help);
    JS_DECLARE_NATIVE_FUNCTION(load_file);
    JS_DECLARE_NATIVE_FUNCTION(save_to_file);
};

static bool s_dump_ast = false;
static bool s_print_last_result = false;
static RefPtr<Line::Editor> s_editor;
static String s_history_path = String::formatted("{}/.js-history", Core::StandardPaths::home_directory());
static int s_repl_line_level = 0;
static bool s_fail_repl = false;

static String prompt_for_level(int level)
{
    static StringBuilder prompt_builder;
    prompt_builder.clear();
    prompt_builder.append("> ");

    for (auto i = 0; i < level; ++i)
        prompt_builder.append("    ");

    return prompt_builder.build();
}

static String read_next_piece()
{
    StringBuilder piece;

    auto line_level_delta_for_next_line { 0 };

    do {
        auto line_result = s_editor->get_line(prompt_for_level(s_repl_line_level));

        line_level_delta_for_next_line = 0;

        if (line_result.is_error()) {
            s_fail_repl = true;
            return "";
        }

        auto& line = line_result.value();
        s_editor->add_to_history(line);

        piece.append(line);
        auto lexer = JS::Lexer(line);

        enum {
            NotInLabelOrObjectKey,
            InLabelOrObjectKeyIdentifier,
            InLabelOrObjectKey
        } label_state { NotInLabelOrObjectKey };

        for (JS::Token token = lexer.next(); token.type() != JS::TokenType::Eof; token = lexer.next()) {
            switch (token.type()) {
            case JS::TokenType::BracketOpen:
            case JS::TokenType::CurlyOpen:
            case JS::TokenType::ParenOpen:
                label_state = NotInLabelOrObjectKey;
                s_repl_line_level++;
                break;
            case JS::TokenType::BracketClose:
            case JS::TokenType::CurlyClose:
            case JS::TokenType::ParenClose:
                label_state = NotInLabelOrObjectKey;
                s_repl_line_level--;
                break;

            case JS::TokenType::Identifier:
            case JS::TokenType::StringLiteral:
                if (label_state == NotInLabelOrObjectKey)
                    label_state = InLabelOrObjectKeyIdentifier;
                else
                    label_state = NotInLabelOrObjectKey;
                break;
            case JS::TokenType::Colon:
                if (label_state == InLabelOrObjectKeyIdentifier)
                    label_state = InLabelOrObjectKey;
                else
                    label_state = NotInLabelOrObjectKey;
                break;
            default:
                break;
            }
        }

        if (label_state == InLabelOrObjectKey) {
            // If there's a label or object literal key at the end of this line,
            // prompt for more lines but do not change the line level.
            line_level_delta_for_next_line += 1;
        }
    } while (s_repl_line_level + line_level_delta_for_next_line > 0);

    return piece.to_string();
}

static void print_value(JS::Value value, HashTable<JS::Object*>& seen_objects);

static void print_array(JS::Array& array, HashTable<JS::Object*>& seen_objects)
{
    bool first = true;
    out("[ ");
    for (auto it = array.indexed_properties().begin(false); it != array.indexed_properties().end(); ++it) {
        if (!first)
            out(", ");
        first = false;
        auto value = it.value_and_attributes(&array).value;
        // The V8 repl doesn't throw an exception here, and instead just
        // prints 'undefined'. We may choose to replicate that behavior in
        // the future, but for now lets just catch the error
        if (vm->exception())
            return;
        print_value(value, seen_objects);
    }
    out(" ]");
}

static void print_object(JS::Object& object, HashTable<JS::Object*>& seen_objects)
{
    out("{{ ");
    bool first = true;
    for (auto& entry : object.indexed_properties()) {
        if (!first)
            out(", ");
        first = false;
        out("\"\033[33;1m{}\033[0m\": ", entry.index());
        auto value = entry.value_and_attributes(&object).value;
        // The V8 repl doesn't throw an exception here, and instead just
        // prints 'undefined'. We may choose to replicate that behavior in
        // the future, but for now lets just catch the error
        if (vm->exception())
            return;
        print_value(value, seen_objects);
    }

    if (!object.indexed_properties().is_empty() && object.shape().property_count())
        out(", ");

    size_t index = 0;
    for (auto& it : object.shape().property_table_ordered()) {
        if (it.key.is_string()) {
            out("\"\033[33;1m{}\033[0m\": ", it.key.to_display_string());
        } else {
            out("\033[33;1m{}\033[0m: ", it.key.to_display_string());
        }
        print_value(object.get_direct(it.value.offset), seen_objects);
        if (index != object.shape().property_count() - 1)
            out(", ");
        ++index;
    }
    out(" }}");
}

static void print_function(const JS::Object& function, HashTable<JS::Object*>&)
{
    out("\033[34;1m[{}]\033[0m", function.class_name());
}

static void print_date(const JS::Object& date, HashTable<JS::Object*>&)
{
    out("\033[34;1mDate {}\033[0m", static_cast<const JS::Date&>(date).string());
}

static void print_error(const JS::Object& object, HashTable<JS::Object*>&)
{
    auto& error = static_cast<const JS::Error&>(object);
    out("\033[34;1m[{}]\033[0m", error.name());
    if (!error.message().is_empty())
        out(": {}", error.message());
}

static void print_regexp(const JS::Object& object, HashTable<JS::Object*>&)
{
    auto& regexp = static_cast<const JS::RegExpObject&>(object);
    out("\033[34;1m/{}/{}\033[0m", regexp.pattern(), regexp.flags());
}

static void print_value(JS::Value value, HashTable<JS::Object*>& seen_objects)
{
    if (value.is_empty()) {
        out("\033[34;1m<empty>\033[0m");
        return;
    }

    if (value.is_object()) {
        if (seen_objects.contains(&value.as_object())) {
            // FIXME: Maybe we should only do this for circular references,
            //        not for all reoccurring objects.
            out("<already printed Object {}>", &value.as_object());
            return;
        }
        seen_objects.set(&value.as_object());
    }

    if (value.is_array())
        return print_array(static_cast<JS::Array&>(value.as_object()), seen_objects);

    if (value.is_object()) {
        auto& object = value.as_object();
        if (object.is_function())
            return print_function(object, seen_objects);
        if (object.is_date())
            return print_date(object, seen_objects);
        if (object.is_error())
            return print_error(object, seen_objects);
        if (object.is_regexp_object())
            return print_regexp(object, seen_objects);
        return print_object(object, seen_objects);
    }

    if (value.is_string())
        out("\033[32;1m");
    else if (value.is_number() || value.is_bigint())
        out("\033[35;1m");
    else if (value.is_boolean())
        out("\033[33;1m");
    else if (value.is_null())
        out("\033[33;1m");
    else if (value.is_undefined())
        out("\033[34;1m");
    if (value.is_string())
        out("\"");
    else if (value.is_negative_zero())
        out("-");
    out("{}", value.to_string_without_side_effects());
    if (value.is_string())
        out("\"");
