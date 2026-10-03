/*
 * Copyright (c) 2020, the SerenityOS developers.
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

#include "Spreadsheet.h"
#include "JSIntegration.h"
#include "Workbook.h"
#include <AK/ByteBuffer.h>
#include <AK/GenericLexer.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonParser.h>
#include <AK/ScopeGuard.h>
#include <AK/TemporaryChange.h>
#include <AK/URL.h>
#include <LibCore/File.h>
#include <LibJS/Parser.h>
#include <LibJS/Runtime/Function.h>
#include <ctype.h>

//#define COPY_DEBUG

namespace Spreadsheet {

Sheet::Sheet(const StringView& name, Workbook& workbook)
    : Sheet(workbook)
{
    m_name = name;

    for (size_t i = 0; i < 20; ++i)
        add_row();

    for (size_t i = 0; i < 16; ++i)
        add_column();
}

Sheet::Sheet(Workbook& workbook)
    : m_workbook(workbook)
{
    m_global_object = m_workbook.interpreter().heap().allocate_without_global_object<SheetGlobalObject>(*this);
    m_global_object->set_prototype(&m_workbook.global_object());
    m_global_object->initialize();
    m_global_object->put("thisSheet", m_global_object); // Self-reference is unfortunate, but required.

    // Sadly, these have to be evaluated once per sheet.
    auto file_or_error = Core::File::open("/res/js/Spreadsheet/runtime.js", Core::IODevice::OpenMode::ReadOnly);
    if (!file_or_error.is_error()) {
        auto buffer = file_or_error.value()->read_all();
        JS::Parser parser { JS::Lexer(buffer) };
        if (parser.has_errors()) {
            dbgln("Spreadsheet: Failed to parse runtime code");
            for (auto& error : parser.errors())
                dbgln("Error: {}\n{}", error.to_string(), error.source_location_hint(buffer));
        } else {
            interpreter().run(global_object(), parser.parse_program());
            if (auto exc = interpreter().exception()) {
                dbgln("Spreadsheet: Failed to run runtime code: ");
                for (auto& t : exc->trace())
                    dbgln("{}", t);
                interpreter().vm().clear_exception();
            }
        }
    }
}

Sheet::~Sheet()
{
}

JS::Interpreter& Sheet::interpreter() const
{
    return m_workbook.interpreter();
}

size_t Sheet::add_row()
{
    return m_rows++;
}

String Sheet::add_column()
{
    if (m_current_column_name_length == 0) {
        m_current_column_name_length = 1;
        m_columns.append("A");
        return "A";
    }

    if (m_current_column_name_length == 1) {
        auto last_char = m_columns.last()[0];
        if (last_char == 'Z') {
            m_current_column_name_length = 2;
            m_columns.append("AA");
            return "AA";
        }

        last_char++;
        m_columns.append({ &last_char, 1 });
        return m_columns.last();
    }

    TODO();
}

void Sheet::update()
{
    m_visited_cells_in_update.clear();
    Vector<Cell*> cells_copy;

    // Grab a copy as updates might insert cells into the table.
    for (auto& it : m_cells)
        cells_copy.append(it.value);

    for (auto& cell : cells_copy) {
        if (has_been_visited(cell))
            continue;
        m_visited_cells_in_update.set(cell);
        if (cell->dirty) {
            // Re-evaluate the cell value, if any.
            cell->update({});
        }
    }

    m_visited_cells_in_update.clear();
}

void Sheet::update(Cell& cell)
{
    if (has_been_visited(&cell))
        return;

    m_visited_cells_in_update.set(&cell);
    cell.update({});
}

JS::Value Sheet::evaluate(const StringView& source, Cell* on_behalf_of)
{
    TemporaryChange cell_change { m_current_cell_being_evaluated, on_behalf_of };

    auto parser = JS::Parser(JS::Lexer(source));
    if (parser.has_errors())
        return JS::js_undefined();

    auto program = parser.parse_program();
    interpreter().run(global_object(), program);
    if (interpreter().exception()) {
        auto exc = interpreter().exception()->value();
        interpreter().vm().clear_exception();
        return exc;
    }

    auto value = interpreter().vm().last_value();
    if (value.is_empty())
        return JS::js_undefined();
    return value;
}

Cell* Sheet::at(const StringView& name)
{
    auto pos = parse_cell_name(name);
    if (pos.has_value())
        return at(pos.value());

    return nullptr;
}

Cell* Sheet::at(const Position& position)
{
    auto it = m_cells.find(position);

    if (it == m_cells.end())
        return nullptr;

    return it->value;
}

Optional<Position> Sheet::parse_cell_name(const StringView& name)
{
    GenericLexer lexer(name);
    auto col = lexer.consume_while(isalpha);
    auto row = lexer.consume_while(isdigit);

    if (!lexer.is_eof() || row.is_empty() || col.is_empty())
        return {};

    return Position { col, row.to_uint().value() };
}

Cell* Sheet::from_url(const URL& url)
{
    auto maybe_position = position_from_url(url);
    if (!maybe_position.has_value())
        return nullptr;

    return at(maybe_position.value());
}

Optional<Position> Sheet::position_from_url(const URL& url) const
{
    if (!url.is_valid()) {
        dbgln("Invalid url: {}", url.to_string());
        return {};
    }

    if (url.protocol() != "spreadsheet" || url.host() != "cell") {
        dbgln("Bad url: {}", url.to_string());
        return {};
    }

    // FIXME: Figure out a way to do this cross-process.
    ASSERT(url.path() == String::formatted("/{}", getpid()));

    return parse_cell_name(url.fragment());
}

Position Sheet::offset_relative_to(const Position& base, const Position& offset, const Position& offset_base) const
{
    auto offset_column_it = m_columns.find(offset.column);
    auto offset_base_column_it = m_columns.find(offset_base.column);
    auto base_column_it = m_columns.find(base.column);

    if (offset_column_it.is_end()) {
        dbg() << "Column '" << offset.column << "' does not exist!";
        return base;
    }
    if (offset_base_column_it.is_end()) {
        dbg() << "Column '" << offset_base.column << "' does not exist!";
        return base;
    }
    if (base_column_it.is_end()) {
        dbg() << "Column '" << base.column << "' does not exist!";
        return offset;
    }

