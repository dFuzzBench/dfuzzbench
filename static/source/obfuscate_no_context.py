#!/usr/bin/env python3
"""Write the prompt data of `--no_context placeholder` (the No-source (perturbed) setting), which the
evaluation reads from data/obfuscated-placeholder/. The default output is results/obfuscated-placeholder/, to
compare with the shipped files; `--out_dir ../data/obfuscated-placeholder` replaces them.

The perturbed no-context prompt is the no-context prompt with every identifier replaced by a numbered
placeholder by kind: the project name (project1), the target's file path (dir1/file1.c, the extension is kept)
and function, the harness file name (prog1.<ext>), and every name in the harness -- func1 (a name the harness
calls, or a target's function), Type1 (capitalized), CONST1 (all caps), module1 (a Python import), var1
(anything else). Language keywords, the standard library, the fuzzing-engine API (libFuzzer entry points,
atheris) and literals are kept, and comments are removed. A name always gets the same placeholder within a
project, so the output is reproducible and consistent across a project's targets.

    python obfuscate_no_context.py [--out_dir DIR] [--benchmark JSON ...] [--keep DIR]

writes, for each T1-T5 project and each CVE of the benchmark json(s) (default: T6, data/target-arvo/benchmark.json):
    <out_dir>/target-latest/<project>/{prompt.json, harness.<ext>}
    <out_dir>/target-arvo/<cve_id>/{prompt.json, harness.<ext>}
prompt.json also lists every renamed identifier.

A target's own function name (the last identifier before its parameter list) is always replaced, even when it is
also a standard or builtin name kept elsewhere: exiv2's XmpData::begin and lark's Grammar::compile otherwise kept
`begin` and `compile`.

Placeholders are numbered per project in the order the CVEs are given (--benchmark takes several jsons), so the
numbering of a project's target-arvo placeholders depends on which CVEs are in the input and in what order.
Likewise, T1-T5 placeholders are numbered in the order of the project's targets in TARGETS.

--keep DIR keeps the placeholders of the files in DIR: a name or path that a project's (or a CVE's) prompt.json in
DIR renames keeps its placeholder, and a new one gets the next free number of its kind, so adding targets or CVEs
leaves the prompts of the others unchanged. `--keep ../data/obfuscated-placeholder` reproduces the shipped files.
"""
import argparse
import builtins
import io
import json
import keyword
import os
import re
import sys
import tokenize

from const import HARNESS_PROMPTS, TARGETS
from evaluation_arvo import arvo_target

STATIC_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
DATA_DIR = os.path.join(STATIC_DIR, "data")
OUT_DIR = os.path.join(STATIC_DIR, "results", "obfuscated-placeholder")
HARNESS_FILES = {  # as in evaluation_multi_projects_conc.py
    "bleach": "fuzzer.py", "clib": "fuzzer.c", "cmark": "fuzzer.c", "cpp-httplib": "fuzzer.cc",
    "exiv2": "fuzzer.cpp", "filesystem_spec": "fuzzer.py", "guetzli": "fuzzer.cc", "html5lib-python": "fuzzer.py",
    "lark-parser": "fuzzer.py", "libbpf": "fuzzer.c", "libpng": "fuzzer.cc", "md4c": "fuzzer.c", "rich": "fuzzer.py",
    "varnish": "fuzzer.c", "wamr": "fuzzer.cc",
}

# --- names that are kept -------------------------------------------------------------------------------------

ATHERIS_API = {
    "atheris", "FuzzedDataProvider", "ConsumeBytes", "ConsumeUnicode", "ConsumeUnicodeNoSurrogates", "ConsumeString",
    "ConsumeInt", "ConsumeIntInRange", "ConsumeIntList", "ConsumeIntListInRange", "ConsumeFloat",
    "ConsumeFloatInRange", "ConsumeFloatList", "ConsumeFloatListInRange", "ConsumeProbability", "ConsumeBool",
    "ConsumeRegularFloat", "PickValueInList", "remaining_bytes", "ALL_REMAINING", "Setup", "Fuzz",
    "instrument_all", "instrument_imports", "instrument_func", "enable_python_coverage", "TestOneInput",
}
PY_STDLIB_MEMBERS = {"argv", "maxsize", "Path", "StringIO", "BytesIO", "write_bytes", "read_bytes", "unlink", "read"}
PY_KEEP = (set(keyword.kwlist) | set(dir(builtins)) | set(sys.stdlib_module_names) | ATHERIS_API
           | PY_STDLIB_MEMBERS | {"main", "__name__", "__main__"})

C_KEYWORDS = {
    # C and C++
    "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class",
    "const", "const_cast", "constexpr", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast",
    "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int",
    "long", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override", "private", "protected",
    "public", "register", "reinterpret_cast", "restrict", "return", "short", "signed", "sizeof", "static",
    "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try",
    "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while",
    "final", "anonymous",
    # preprocessor
    "include", "define", "undef", "ifdef", "ifndef", "endif", "elif", "pragma", "error", "defined", "line",
    # compiler builtins and attributes
    "__attribute__", "packed", "unused", "__builtin_trap", "__builtin_expect",
}
C_STD = {
    # types and macros
    "size_t", "ssize_t", "ptrdiff_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", "intptr_t", "uintptr_t", "time_t", "va_list", "FILE", "NULL", "EOF", "SEEK_SET", "SEEK_CUR",
    "SEEK_END", "errno", "stdin", "stdout", "stderr", "jmp_buf", "_POSIX_C_SOURCE", "_GNU_SOURCE", "HAVE_CONFIG_H",
    # functions
    "malloc", "calloc", "realloc", "free", "memcpy", "memmove", "memset", "memcmp", "strlen", "strcmp", "strncmp",
    "strcpy", "strncpy", "strdup", "strndup", "strchr", "strstr", "printf", "fprintf", "sprintf", "snprintf",
    "vsnprintf", "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "fflush", "fmemopen", "fileno", "exit",
    "abort", "atexit", "assert", "setjmp", "longjmp", "unlink", "close", "mkstemp", "getenv", "abs", "access",
    "remove", "system",
    # C++
    "std", "string", "vector", "unique_ptr", "shared_ptr", "make_unique", "make_shared", "ostringstream",
    "stringstream", "istringstream", "cout", "cerr", "endl", "substr", "c_str", "get", "push_back", "begin", "end",
    # headers
    "stdio", "stdlib", "string", "stddef", "stdint", "stdbool", "unistd", "setjmp", "limits", "cassert", "cstdint",
    "cstddef", "cstdlib", "cstdio", "cstring", "iomanip", "iostream", "sstream", "memory", "algorithm", "h",
}
LIBFUZZER_API = {
    "LLVMFuzzerTestOneInput", "LLVMFuzzerInitialize", "LLVMFuzzerCustomMutator", "LLVMFuzzerCustomCrossOver",
    "LLVMFuzzerMutate", "__asan_default_options", "main",
    "FuzzedDataProvider", "ConsumeIntegral", "ConsumeIntegralInRange", "ConsumeBytes", "ConsumeBytesAsString",
    "ConsumeRemainingBytes", "ConsumeRemainingBytesAsString", "ConsumeRandomLengthString", "ConsumeBool",
    "ConsumeEnum", "ConsumeProbability", "ConsumeFloatingPoint", "ConsumeFloatingPointInRange", "PickValueInArray",
    "ConsumeData", "remaining_bytes",
}
C_KEEP = C_KEYWORDS | C_STD | LIBFUZZER_API
STD_HEADERS = {"fuzzer/FuzzedDataProvider.h"}  # besides <name> / <name.h> made of C_STD words, e.g. <stdio.h>
# Members of the standard containers and strings. They are kept only when called on a variable the harness
# declares with a std:: type (v.data() of a std::vector v), since the same names are the harness's own
# elsewhere (the libFuzzer parameters data and size).
STD_MEMBERS = {
    "data", "size", "length", "empty", "clear", "resize", "reserve", "capacity", "append", "assign", "find",
    "insert", "erase", "front", "back", "at", "c_str", "substr", "get", "reset", "release", "push_back",
    "pop_back", "emplace_back", "begin", "end", "str", "swap", "compare",
}
STD_DECL = re.compile(r"\bstd::\w+\s*(?:<[^;{}()]*>)?\s*[&*]*\s*([A-Za-z_]\w*)")  # std::vector<char> &v -> v


class Renamer:
    """Replaces names by numbered placeholders: func1, Type1, CONST1, module1, var1, dir1/file1.c, project1,
    prog1. The same name always gets the same placeholder. `functions` and `modules` say which names are
    functions and Python modules."""

    def __init__(self, keep, functions=(), modules=()):
        self.keep, self.renamed = keep, {}
        self.functions, self.modules, self.counts, self.path_renamed = set(functions), set(modules), {}, {}
        self.used, self.used_paths = set(), set()  # what the output uses (with seed(), not all of renamed)

    def seed(self, renamed, renamed_paths):
        """Start from earlier placeholders (prompt.json's renamed and renamed_paths): a name keeps its placeholder,
        and a new name gets a number above the highest one of its kind."""
        paths = {tuple(key.split(":", 1)): alias for key, alias in renamed_paths.items()}
        for old, new in ((self.renamed, renamed), (self.path_renamed, paths)):
            clash = {k for k in new if k in old and old[k] != new[k]}
            if clash:
                raise ValueError(f"conflicting earlier placeholders for {sorted(clash)}")
            old.update(new)
        for alias in list(renamed.values()) + list(paths.values()):
            kind, number = re.fullmatch(r"([A-Za-z]+?)(\d+)", alias).groups()
            self.counts[kind] = max(self.counts.get(kind, 0), int(number))

    def next(self, kind):
        self.counts[kind] = self.counts.get(kind, 0) + 1
        return f"{kind}{self.counts[kind]}"

    def __call__(self, name, keep=None):
        if name in (self.keep if keep is None else keep):
            return name
        self.used.add(name)
        if name not in self.renamed:
            kind = ("module" if name in self.modules else "func" if name in self.functions else
                    "CONST" if name.isupper() and any(c.isalpha() for c in name) else
                    "Type" if name[0].isupper() else "var")
            self.renamed[name] = self.next(kind)
        return self.renamed[name]

    def path(self, path):
        """a/b/file.ext -> dir1/dir2/file1.ext. Every file and directory name is replaced, even one that matches
        a standard name (http.py, io/)."""
        *dirs, base = path.split("/")
        stem, ext = os.path.splitext(base)

        def one(part, kind):
            self.used_paths.add((kind, part))
            if (kind, part) not in self.path_renamed:
                self.path_renamed[(kind, part)] = self.next(kind)
            return self.path_renamed[(kind, part)]
        return "/".join([one(d, "dir") if d not in ("", ".", "..") else d for d in dirs] + [one(stem, "file") + ext])

    def words(self, text, attribute_keep=None):
        """Every name in text; with attribute_keep, a name after a dot is kept only if in attribute_keep."""
        def one(m):
            if attribute_keep is not None and m.start() > 0 and m.string[m.start() - 1] == ".":
                return self(m.group(), keep=attribute_keep)
            return self(m.group())
        return re.sub(r"[A-Za-z_]\w*", one, text)

    def project(self, name):
        self.used.add(name)
        if name not in self.renamed:
            self.renamed[name] = self.next("project")
        return self.renamed[name]

    def program(self, name):
        """The harness file's stem."""
        self.used_paths.add(("prog", name))
        if ("prog", name) not in self.path_renamed:
            self.path_renamed[("prog", name)] = self.next("prog")
        return self.path_renamed[("prog", name)]


CALLED = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
PY_IMPORT = re.compile(r"^\s*(?:from\s+([\w.]+)\s+import\s+([\w, ()]+)|import\s+([\w., ]+))", re.M)


def split_params(function):
    """(qualified name, trailing parameter list): A::f(char const*, int) -> ("A::f", "(char const*, int)").
    A parenthesized group elsewhere stays in the name (XMLValidator()::setError)."""
    if not function.endswith(")"):
        return function, ""
    depth = 0
    for i in range(len(function) - 1, -1, -1):
        depth += {")": 1, "(": -1}.get(function[i], 0)
        if depth == 0:
            return function[:i], function[i:]
    return function, ""


def own_name(function):
    """A target function's own name, the last identifier of its qualified name: Grammar::compile -> compile,
    A::B::f(std::vector<char>&) -> f, XMLValidator()::setError -> setError."""
    return re.findall(r"[A-Za-z_]\w*", split_params(function)[0])[-1]


def placeholder_hints(codes, functions, python):
    """(names called in the harness code or given as target functions, Python modules imported)."""
    called, modules = set(own_name(f) for f in functions), set()
    for code in codes:
        called |= set(CALLED.findall(code))
        if python:
            for m in PY_IMPORT.finditer(code):
                if m.group(1):
                    modules.add(m.group(1).split(".")[0])
                if m.group(3):
                    modules |= {w.strip().split(" as ")[0].split(".")[0] for w in m.group(3).split(",")}
    return called, modules


def make_renamer(keep, codes=(), functions=(), python=False):
    called, modules = placeholder_hints(codes, functions, python)
    return Renamer(keep, called, modules)


FILE_NAME = re.compile(r"(?<![\w.-])(?:[\w-]+/)*[\w-]*[A-Za-z_][\w-]*\.[A-Za-z]\w*(?![\w-])")
# Words inside string literals that name a facility of the project rather than describe data: a program and
# its options (wamr's harness runs `wasm-tools mutate --preserve-semantics`), a protocol, color system or
# export format registered in the project's API (fsspec's "http", rich's "truecolor", assimp's "fbx"). Other
# literal text -- messages, file modes, format strings, addresses, the name of the function a wasm input must
# export -- is the program's data and stays.
LITERAL_WORDS = {"wasm-tools", "mutate", "preserve-semantics", "http", "truecolor", "fbx"}
LITERAL_WORD = re.compile(r"(?<!\w)(?:" + "|".join(map(re.escape, sorted(LITERAL_WORDS, key=len, reverse=True)))
                          + r")(?!\w)")


def obfuscate_literal(text, rename):
    """File names in a string literal get placeholders (the extension stays), like paths elsewhere; so do
    the LITERAL_WORDS."""
    text = FILE_NAME.sub(lambda m: rename.path(m.group()), text)
    return LITERAL_WORD.sub(lambda m: rename(m.group(), keep=set()), text)


def tidy(code):
    """Trailing spaces off, runs of blank lines down to one, no blank lines at the start or end."""
    return re.sub(r"\n{3,}", "\n\n", "\n".join(line.rstrip() for line in code.splitlines())).strip("\n") + "\n"


PY_ATTRIBUTE_KEEP = ATHERIS_API | PY_STDLIB_MEMBERS  # after a dot, only these are not the project's own names


def obfuscate_python(code, rename):
    """Names after a dot are attributes -- kept only if atheris or standard library ones -- and names imported
    from a package that is not the standard library are the project's, even if they look like stdlib names."""
    lines = code.splitlines(keepends=True)
    edits, commented = [], set()
    prev, importing_from, project_import = None, False, False
    for tok in tokenize.generate_tokens(io.StringIO(code).readline):
        if tok.type == tokenize.COMMENT:
            edits.append((tok.start, tok.end, ""))
            if not lines[tok.start[0] - 1][:tok.start[1]].strip():
                commented.add(tok.start[0])  # a line holding only a comment goes
            continue
        if tok.type in (tokenize.NEWLINE, tokenize.NL):
            importing_from = project_import = False
        elif tok.type == tokenize.STRING and obfuscate_literal(tok.string, rename) != tok.string:
            edits.append((tok.start, tok.end, obfuscate_literal(tok.string, rename)))
        elif tok.type == tokenize.NAME:
            if tok.string == "from" and prev is None:
                importing_from = True
            elif importing_from and prev == "from":  # the package: stdlib or not decides the imported names
                project_import = tok.string not in sys.stdlib_module_names and tok.string != "atheris"
            if prev == ".":
                new = rename(tok.string, keep=PY_ATTRIBUTE_KEEP)
            elif project_import and tok.string not in ("import", "as"):
                new = rename(tok.string, keep=set(keyword.kwlist) | ATHERIS_API)
            else:
                new = rename(tok.string)
            if new != tok.string:
                edits.append((tok.start, tok.end, new))
        if tok.type not in (tokenize.INDENT, tokenize.DEDENT, tokenize.NL, tokenize.NEWLINE):
            prev = tok.string
        elif tok.type in (tokenize.NEWLINE, tokenize.NL):
            prev = None
    for (row, col), (_, end), new in sorted(edits, reverse=True):
        lines[row - 1] = lines[row - 1][:col] + new + lines[row - 1][end:]
    return tidy("".join(line for n, line in enumerate(lines, 1) if n not in commented))


C_TOKEN = re.compile(r"""
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<include>^[ \t]*\#[ \t]*include[ \t]*[<"][^>"\n]*[>"])
  | (?P<raw>(?:u8|[uUL])?R"(?P<delim>[^()\\\s]{0,16})\(.*?\)(?P=delim)")
  | (?P<string>(?:u8|[uUL])?"(?:\\.|[^"\\\n])*")
  | (?P<char>'(?:\\.|[^'\\\n])*')
  | (?P<number>\.?\d(?:[eEpP][+-]|[\w.])*)
  | (?P<ident>[A-Za-z_]\w*)
""", re.S | re.M | re.X)


def strip_c_comments(code):
    """A comment becomes one space (strings are respected), and a line that held only comments goes."""
    mask = bytearray(len(code))
    for m in C_TOKEN.finditer(code):
        if m.lastgroup == "comment":
            mask[m.start():m.end()] = b"\1" * (m.end() - m.start())
    lines, start = [], 0
    for line in code.split("\n"):
        end = start + len(line)
        kept = "".join(ch if not mask[start + i] else " " if i == 0 or not mask[start + i - 1] else ""
                       for i, ch in enumerate(line))
        if not (any(mask[start:end]) and not kept.strip()):
            lines.append(kept)
        start = end + 1
    return "\n".join(lines)


def obfuscate_c(code, rename):
    code = strip_c_comments(code)
    std_vars = set(STD_DECL.findall(code))  # variables of a std:: type, whose members stay
    out, pos, prev = [], 0, None  # prev: the previous token if it was an identifier, to keep std::<name>
    for m in C_TOKEN.finditer(code):
        between, text, kind = code[pos:m.start()], m.group(), m.lastgroup
        out.append(between)
        pos = m.end()
        if kind == "include":
            head, path = re.match(r'(.*[<"])([^>"]*)[>"]$', text, re.S).groups()
            if path not in STD_HEADERS and not all(w in C_STD for w in re.findall(r"[A-Za-z_]\w*", path)):
                path = rename.path(path)
            out.append(head + path + text[-1])
        elif kind == "ident":
            std_name = prev == "std" and between.strip() == "::"
            std_member = prev in std_vars and between.strip() in (".", "->") and text in STD_MEMBERS
            out.append(text if std_name or std_member else rename(text))
            prev = text
            continue
        elif kind == "string":
            out.append(obfuscate_literal(text, rename))
        else:
            out.append(text)
        prev = None
    out.append(code[pos:])
    return tidy("".join(out))


def obfuscate_example(text, rename, attribute_keep=None):
    """The prompt's input example names harness variables and calls, in `code` spans or as name( / a.b. The
    ``` blocks showing the answer format are left alone."""
    code_like = re.compile(r"[A-Za-z_]\w*(?=\s*\()|[A-Za-z_]\w*(?=\.[A-Za-z_])|(?<=\.)[A-Za-z_]\w*")
    parts = re.split(r"(```.*?```|`[^`\n]*`)", text, flags=re.S)
    for i, part in enumerate(parts):
        if part.startswith("```"):
            continue
        if part.startswith("`"):
            parts[i] = rename.words(part, attribute_keep)
        else:
            parts[i] = code_like.sub(lambda m: rename.words(m.group(), attribute_keep)
                                     if m.start() == 0 or part[m.start() - 1] != "." else
                                     rename(m.group(), keep=attribute_keep) if attribute_keep is not None else
                                     rename(m.group()), part)
    return "".join(parts)


def function_name(name, rename, attribute_keep=None):
    """HTMLTokenizer.emitCurrentToken, (anonymous namespace)::A::b, ... -> each identifier renamed. The
    function's own name is renamed even when it is a standard or builtin name kept elsewhere; only a language
    keyword stays."""
    head, params = split_params(name)
    idents = list(re.finditer(r"[A-Za-z_]\w*", head))
    if not idents:
        return rename.words(name, attribute_keep)
    last = idents[-1]
    own = rename(last.group(), keep=C_KEYWORDS | set(keyword.kwlist))
    return (rename.words(head[:last.start()], attribute_keep) + own + rename.words(head[last.end():], attribute_keep)
            + rename.words(params, attribute_keep))


def write(dirname, harness_code, harness_ext, info):
    os.makedirs(dirname, exist_ok=True)
    info["harness_file"] = "harness" + harness_ext
    with open(os.path.join(dirname, info["harness_file"]), "w") as f:
        f.write(harness_code)
    with open(os.path.join(dirname, "prompt.json"), "w") as f:
        json.dump(info, f, indent=1, sort_keys=True)
        f.write("\n")


def earlier(keep_dir, *parts):
    """The prompt.json under keep_dir/<parts>, or None."""
    path = os.path.join(keep_dir, *parts, "prompt.json") if keep_dir else None
    if path and os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    return None


def latest(project, out_dir=OUT_DIR, keep_dir=None):
    harness = HARNESS_FILES[project]
    ext = os.path.splitext(harness)[1]
    python = ext == ".py"
    with open(os.path.join(DATA_DIR, "target-latest", project, "base-env", harness), encoding="utf-8") as f:
        code = f.read()
    functions = [spec[-1] for level_targets in TARGETS[project].values() for spec in level_targets.values()]
    rename = make_renamer(PY_KEEP if python else C_KEEP, [code], functions, python)
    prev = earlier(keep_dir, "target-latest", project)
    if prev:
        rename.seed(prev["renamed"], prev["renamed_paths"])
    project_alias = rename.project(project.replace("-", "_"))
    code = obfuscate_python(code, rename) if python else obfuscate_c(code, rename)
    targets = {}
    for level, level_targets in TARGETS[project].items():
        for key, spec in level_targets.items():
            path = "/".join(key.split(":")[:-1])
            targets.setdefault(level, {})[key] = {
                "path": rename.path(path),
                "function": function_name(spec[-1], rename, PY_ATTRIBUTE_KEEP if python else None)}
    info = {
        "project": project_alias,
        "harness_name": rename.program(os.path.splitext(harness)[0]) + ext,
        "input_example": obfuscate_example(HARNESS_PROMPTS[project][1], rename, PY_ATTRIBUTE_KEEP if python else None),
        "targets": targets,
    }
    info["renamed"] = {n: a for n, a in sorted(rename.renamed.items()) if n in rename.used}
    # directories, file stems and the harness file, by kind
    info["renamed_paths"] = {f"{kind}:{name}": alias for (kind, name), alias in sorted(rename.path_renamed.items())
                             if (kind, name) in rename.used_paths}
    write(os.path.join(out_dir, "target-latest", project), code, ext, info)


def arvo(entries, out_dir=OUT_DIR, keep_dir=None):
    renamers = {}
    for e in entries:
        project, cve_id = e["project_name"], e["cve_id"]
        if project not in renamers:
            mine = [x for x in entries if x["project_name"] == project]
            renamers[project] = make_renamer(C_KEEP, [x["harness_code"] for x in mine],
                                             [arvo_target(x)[2] for x in mine])
            for x in mine:
                prev = earlier(keep_dir, "target-arvo", x["cve_id"])
                if prev:
                    renamers[project].seed(prev["renamed"], prev["renamed_paths"])
        rename = renamers[project]
        path, _, function, _ = arvo_target(e)
        harness_name = e["fuzz_target"].split("/")[-1]
        code = obfuscate_c(e["harness_code"], rename)
        info = {
            "harness_name": rename.program(harness_name),
            "target": {"path": rename.path(path), "function": function_name(function, rename)},
        }
        shown = code + json.dumps(info)  # the names this CVE's prompt uses (the renamer spans the project's CVEs)
        info["renamed"] = {n: a for n, a in sorted(rename.renamed.items()) if re.search(rf"\b{a}\b", shown)}
        info["renamed_paths"] = {f"{kind}:{name}": alias for (kind, name), alias in sorted(rename.path_renamed.items())
                                 if re.search(rf"\b{alias}\b", shown)}
        write(os.path.join(out_dir, "target-arvo", cve_id), code, ".cc", info)


def main():
    ap = argparse.ArgumentParser(description="Write the prompt data of --no_context placeholder.")
    ap.add_argument("--out_dir", default=OUT_DIR,
                    help="default: results/obfuscated-placeholder (the evaluation reads data/obfuscated-placeholder)")
    ap.add_argument("--benchmark", nargs="+", default=[os.path.join(DATA_DIR, "target-arvo", "benchmark.json")],
                    help="the CVE lists, in order (default: T6)")
    ap.add_argument("--keep", metavar="DIR",
                    help="keep the placeholders of the files in DIR (e.g. ../data/obfuscated-placeholder); new names "
                         "get the next free number")
    args = ap.parse_args()
    for project in HARNESS_FILES:
        latest(project, args.out_dir, args.keep)
    entries = []
    for benchmark in args.benchmark:
        with open(benchmark) as f:
            entries += json.load(f)
    arvo(entries, args.out_dir, args.keep)
    print(f"wrote {args.out_dir}: {len(HARNESS_FILES)} projects, {len(entries)} CVEs")


if __name__ == "__main__":
    main()
