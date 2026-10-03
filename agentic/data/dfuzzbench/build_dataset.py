"""Build the T1-T5 dataset that the reach agent loads: one row per target line.

Each row is one T1-T5 target of data/<project>/realistic/<tier>/<target>/ (target.patch plus the
realistic-context files) together with the project's base-env/ and its static-call-graph paths
(CALL_PATHS). realistic_context joins the target's files in sorted file-name order, so a build is
deterministic. The per-target Docker images are built with scripts/build_dfuzzbench_image.sh.

Run from agentic/ before the first T1-T5 run:

    python data/dfuzzbench/build_dataset.py --out-dir data/dfuzzbench/dataset   # the dataset_id of the configs
    python data/dfuzzbench/build_dataset.py --json-out dataset.json             # the rows as one JSON list

--out-dir writes <dir>/test.jsonl (the split "test", one JSON object per line), which the dfuzzbench
env reads from that local directory (debug_gym/gym/envs/local_dataset.py). The directory is
git-ignored; rebuild it after changing anything under data/dfuzzbench/data/.
"""
import argparse
import json
import os
from json import dumps
from pathlib import Path

DATA_DIR = Path(__file__).resolve().parent / "data"

PROJECTS = [
    "lark-parser",
    "bleach",
    "html5lib-python",
    "rich",
    "filesystem_spec",
    "wamr",
    "md4c",
    "libbpf",
    "varnish",
    "clib",
    "cmark",
    "cpp-httplib",
    "libpng",
    "guetzli",
    "exiv2",
]

root_directories = [str(DATA_DIR / project / "realistic") for project in PROJECTS]

DOWNLOAD_LINK = {
    "bleach": {
        "url": "https://github.com/mozilla/bleach.git",
        "commit": "4bc1bff36841c04152525b8e57fd3740f34ec796",
        "harness": "fuzzer.py",
    },
    "clib": {
        "url": "https://github.com/clibs/clib",
        "commit": "6d96e53349784087d1f0c13aca2c9b6cdf3f4c4c",
        "harness": "fuzzer.c"
    },
    "cmark": {
        "url": "https://github.com/commonmark/cmark.git",
        "commit": "9d74662f4b12f0c7c2f610231a47348fe8b7b619",
        "harness": "fuzzer.c",        
    },
    "connexion": {
        "url": "https://github.com/spec-first/connexion",
        "commit": "1844a2fb3f1d09a37edfeab7a7200d52e97abbd9",
        "harness": "fuzzer.py",
    },
    "cpp-httplib": {
        "url": "https://github.com/yhirose/cpp-httplib.git",
        "commit": "4b2b851dbb91f9d6c2299976b5d03f7c5c1a312d",
        "harness": "fuzzer.cc"
    },
    "exiv2": {
        "url": "https://github.com/Exiv2/exiv2",
        "commit": "66c3cda1835248df8ece24464b4546314d0233a7",
        "harness": "fuzzer.cpp",        
    },
    "filesystem_spec": {
        "url": "https://github.com/fsspec/filesystem_spec",
        "commit": "3675a7cd163a3d7bc82f83e238ddc967f7b145c8",
        "harness": "fuzzer.py",
    },
    "glslang": {
        "url": "https://github.com/khronosgroup/glslang",
        "commit": "9d764997360b202d2ba7aaad9a401e57d8df56b3",
        "harness": "fuzzer.cc",
    },
    "guetzli": {
        "url": "https://github.com/google/guetzli",
        "commit": "214f2bb42abf5a577c079d00add5d6cc470620d3",
        "harness": "fuzzer.cc",
    },
    "html2text": {
        "url": "https://github.com/Alir3z4/html2text",
        "commit": "8917f5c83d8cf013110124a6b37331b2c29a0fff",
        "harness": "fuzzer.py",        
    },
    "html5lib-python": {
        "url": "https://github.com/html5lib/html5lib-python",
        "commit": "fd4f032bc090d44fb11a84b352dad7cbee0a4745",
        "harness": "fuzzer.py",        
    },
    "rich": {
        "url": "https://github.com/Textualize/rich",
        "commit": "72e3bb33d44fd96881f7742b77137983907a942f",
        "harness": "fuzzer.py",
    },
    "lark-parser": {
        "url": "https://github.com/lark-parser/lark",
        "commit": "24f19a35f376b9320d53f4d987793fb8b1765f37",
        "harness": "fuzzer.py",
    },
    "libbpf": {
        "url": "https://github.com/libbpf/libbpf",
        "commit": "c5f22aca0f3aa855daa159b2777472b35e721804",
        "harness": "fuzzer.c",
    },
    "libpg_query": {
        "url": "https://github.com/pganalyze/libpg_query",
        "commit": "e1a98c31d90981cdfdba3c734a60e5abf9c522c2",
        "harness": "fuzzer.c",
    },
    "libpng": {
        "url": "https://github.com/pnggroup/libpng.git",
        "commit": "c1cc0f3f4c3d4abd11ca68c59446a29ff6f95003",
        "harness": "fuzzer.cc",
    },
    "md4c": {
        "url": "https://github.com/mity/md4c",
        "commit": "481fbfbdf72daab2912380d62bb5f2187d438408",
        "harness": "fuzzer.c",
    },    
    "ninja": {
        "url": "https://github.com/ninja-build/ninja",
        "commit": "a3fda2b06c027f19c7ec68c08e21859e44c15cde",
        "harness": "fuzzer.cc",        
    },
    "openh264": {
        "url": "https://github.com/cisco/openh264.git",
        "commit": "6746bc48f1ee9b3165200a8fad329acfdf01621b",
        "harness": "fuzzer.cpp",        
    },
    "protobuf-c": {
        "url": "https://github.com/protobuf-c/protobuf-c.git",
        "commit": "e05528c871ef89d6578b9b1b911f0d774de910eb",
        "harness": "fuzzer.cpp",        
    },
    "python-markdown": {
        "url": "https://github.com/python-markdown/markdown",
        "commit": "0b5e80efbb83f119e0e38801bf5b5b5864c67cd0",
        "harness": "fuzzer.py",
    },
    "requests": {
        "url": "https://github.com/psf/requests.git",
        "commit": "0e322af87745eff34caffe4df68456ebc20d9068",
        "harness": "fuzzer.py",
    },
    "varnish": {
        "url": "https://github.com/varnishcache/varnish-cache",
        "commit": "41059cf6815d0f366a5145fdb69d310e37e6d380",
        "harness": "fuzzer.c",
    },    
    "w3m": {
        "url": "https://github.com/tats/w3m",
        "commit": "ee66aabc3987000c2851bce6ade4dcbb0b037d81",
        "harness": "fuzzer.c",
    },
    "wamr": {
        "url": "https://github.com/bytecodealliance/wasm-micro-runtime",
        "commit": "0119b17526ae447c5c57784d9deb90c04af51fe5",
        "harness": "fuzzer.cc",
    },
}

CALL_PATHS = {
    # bleach
    # easy      
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:247': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.state -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagNameState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.emitCurrentToken"],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:263': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.state -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState"],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:397': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.state -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagNameState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState"],
	'bleach::bleach:_vendor:html5lib:html5parser.py:216': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop"],
	'bleach::bleach:_vendor:html5lib:html5parser.py:318': [
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEOF -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagA -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagA -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagA -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagOther -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagCloseP -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagP -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagListItem -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagListItem -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagOther -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagListItem -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagOther -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagListItem -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagP -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagListItem -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagListItem -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagOther -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagP -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagListItem -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.processDocType -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseError",
    ],
    'bleach::bleach:_vendor:html5lib:html5parser.py:462': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::Phase.processStartTag"],
    'bleach::bleach:_vendor:html5lib:html5parser.py:970': [
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagA -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.addFormattingElement",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagFormatting -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.addFormattingElement",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagNobr -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.addFormattingElement",
    ],
    'bleach::bleach:_vendor:html5lib:html5parser.py:980': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEOF"],
    'bleach::bleach:_vendor:html5lib:html5parser.py:1000': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processCharacters"],
    'bleach::bleach:html5lib_shim.py:374': [],
    # medium
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1205': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.markupDeclarationOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentStartState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentStartDashState"],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1247': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.markupDeclarationOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentStartState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentEndDashState"],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1266': [
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.markupDeclarationOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentStartState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentStartDashState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer..commentEndState",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.markupDeclarationOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentStartState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.commentEndDashState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer..commentEndState",
    ],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1333': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.markupDeclarationOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.doctypeState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.beforeDoctypeNameState"],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1358': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.markupDeclarationOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.doctypeState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.beforeDoctypeNameState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.doctypeNameState"],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1416': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.markupDeclarationOpenState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.doctypeState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.beforeDoctypeNameState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.doctypeNameState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.afterDoctypeNameState"],
    'bleach::bleach:_vendor:html5lib:html5parser.py:1544': [
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagA -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processStartTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.startTagListItem -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting",
        "instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagFormatting",
    ], 
    'bleach::bleach:_vendor:html5lib:html5parser.py:1779': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InTablePhase.startTagInput"],
    # hard
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:588': [],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:826': [],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1434': [],
    'bleach::bleach:_vendor:html5lib:html5parser.py:1591': [],
    'bleach::bleach:html5lib_shim.py:580': [],
    # extremely-hard
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1482': [],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1608': [],
    'bleach::bleach:_vendor:html5lib:_tokenizer.py:1635': [],
    # fuzzer-unreachable
	'bleach::bleach:_vendor:html5lib:_tokenizer.py:250': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.state -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.dataState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.tagNameState -> bleach/_vendor/html5lib/_tokenizer.py::HTMLTokenizer.emitCurrentToken"],
	'bleach::bleach:_vendor:html5lib:html5parser.py:966': [],
    'bleach::bleach:_vendor:html5lib:html5parser.py:1378': ["instrumented_fuzzer.py::TestOneInput -> bleach/__init__.py::linkify -> bleach/linkifier.py::Linker.linkify -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.parseFragment -> bleach/_vendor/html5lib/html5parser.py::HTMLParser._parse -> bleach/_vendor/html5lib/html5parser.py::HTMLParser.mainLoop -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.processEndTag -> bleach/_vendor/html5lib/html5parser.py::InBodyPhase.endTagListItem"],
    'bleach::bleach:html5lib_shim.py:470': [],
    'bleach::bleach:html5lib_shim.py:544': [],
    'bleach::bleach:html5lib_shim.py:588': [],
    'bleach::bleach:html5lib_shim.py:634': [],

    # clib
    # easy
    'clib::src:common:clib-package.c:184': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_load_from_manifest -> src/common/clib-package.c::clib_package_new -> src/common/clib-package.c::json_object_get_string_safe"],
    'clib::src:common:clib-package.c:249': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_load_from_manifest"],
    'clib::src:common:clib-package.c:323': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_load_from_manifest -> src/common/clib-package.c::clib_package_new -> src/common/clib-package.c::parse_package_deps"],
    'clib::src:common:clib-package.c:345': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_load_from_manifest -> src/common/clib-package.c::clib_package_new -> src/common/clib-package.c::parse_package_deps"],
    'clib::src:common:clib-package.c:470': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:477': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:544': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:564': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:571': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:1705': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    # medium
    'clib::src:common:clib-package.c:565': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:573': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    # unreachable
    'clib::src:common:clib-package.c:479': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:483': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:519': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:582': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    'clib::src:common:clib-package.c:591': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/common/clib-package.c::clib_package_new"],
    # cmark
    # easy
    'cmark::src:blocks.c:144': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
    ],
    "cmark::src:blocks.c:151": [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize  -> src/blocks.c::resolve_reference_link_definitions -> src/blocks.c::is_blank",
    ],
    "cmark::src:blocks.c:188": ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::add_line"],
	'cmark::src:blocks.c:217': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::remove_trailing_blank_lines",
    ],
	'cmark::src:blocks.c:277': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
    ],
    "cmark::src:blocks.c:316": [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
    ],	
    "cmark::src:blocks.c:326": [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
    ],	
    "cmark::src:blocks.c:531": ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::finalize_document"],
    "cmark::src:blocks.c:644": ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed"],
    "cmark::src:blocks.c:1035": [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks",
    ],
    # unreachable
	'cmark::src:blocks.c:228': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",

        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize -> src/blocks.c::S_ends_with_blank_line",
    ],
	'cmark::src:blocks.c:303': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::add_child -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::finalize_document -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::check_open_blocks -> src/blocks.c::parse_code_block_prefix -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::add_text_to_container -> src/blocks.c::finalize",
    ],
	'cmark::src:blocks.c:488': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::parse_list_marker",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::parse_list_marker",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::parse_list_marker",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::parse_list_marker",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::parse_list_marker",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::cmark_parser_finish -> src/blocks.c::S_process_line -> src/blocks.c::open_new_blocks -> src/blocks.c::parse_list_marker",
    ],
	'cmark::src:blocks.c:590': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed",
    ],
	'cmark::src:blocks.c:593': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parser_feed -> src/blocks.c::S_parser_feed",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_file -> src/blocks.c::S_parser_feed",
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/blocks.c::cmark_parse_document -> src/blocks.c::S_parser_feed",
    ],

    # cpp-httplib
    "cpp-httplib::httplib.h:3878": [],
    "cpp-httplib::httplib.h:3986": [],
    "cpp-httplib::httplib.h:4068": [],
    "cpp-httplib::httplib.h:4080": [],
    "cpp-httplib::httplib.h:4145": [],
    "cpp-httplib::httplib.h:4705": [],
    "cpp-httplib::httplib.h:5244": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::Server.write_response -> httplib.h::Server.write_response_core -> httplib.h::Server.write_content_with_provider -> httplib.h::detail.write_content_chunked"],
    "cpp-httplib::httplib.h:5689": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::Server.routing -> httplib.h::Server.read_content -> httplib.h::Server.read_content_core -> httplib.h::detail.FormDataParser.parse"],
    "cpp-httplib::httplib.h:6624": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::Server.parse_request_line -> httplib.h::detail.decode_path_component"],
    "cpp-httplib::httplib.h:7612": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::Server.parse_request_line"],
    "cpp-httplib::httplib.h:5528": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::detail.parse_accept_header"],
    "cpp-httplib::httplib.h:6725": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::detail.decode_path_component"],
    "cpp-httplib::httplib.h:7855": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::Server.routing -> httplib.h::Server.read_content"],
    "cpp-httplib::httplib.h:2711": [],
    "cpp-httplib::httplib.h:5503": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::detail.parse_accept_header"],
    "cpp-httplib::httplib.h:5618": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::Server.routing -> httplib.h::Server.read_content -> httplib.h::Server.read_content_core -> httplib.h::detail.FormDataParser.parse"],
    "cpp-httplib::httplib.h:5680": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::Server.routing -> httplib.h::Server.read_content -> httplib.h::Server.read_content_core -> httplib.h::detail.FormDataParser.parse"],
    "cpp-httplib::httplib.h:7640": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> instrumented_fuzzer.cpp::FuzzableServer.ProcessFuzzedRequest -> httplib.h::Server.process_request -> httplib.h::Server.parse_request_line"],
    # exiv2
	'exiv2::src:xmp.cpp:513': ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/xmp.cpp::XmpParser.initialize"],
	'exiv2::src:xmp.cpp:841': [
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/pngimage.cpp::PngImage.writeMetadata -> src/pngimage.cpp::PngImage.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.writeMetadata -> src/jpgimage.cpp::JpegBase.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
    ],
	'exiv2::src:xmp.cpp:849': [
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/pngimage.cpp::PngImage.writeMetadata -> src/pngimage.cpp::PngImage.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.writeMetadata -> src/jpgimage.cpp::JpegBase.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
    ],
	'exiv2::src:xmp.cpp:856': [
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/pngimage.cpp::PngImage.writeMetadata -> src/pngimage.cpp::PngImage.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.writeMetadata -> src/jpgimage.cpp::JpegBase.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
    ],
	'exiv2::src:xmp.cpp:873': [
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/pngimage.cpp::PngImage.writeMetadata -> src/pngimage.cpp::PngImage.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.writeMetadata -> src/jpgimage.cpp::JpegBase.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
    ],
	'exiv2::src:xmp.cpp:133': [
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.printStructure -> src/jpgimage.cpp::JpegBase.readMetadata -> src/xmp.cpp::XmpParser.decode",
    ],
	'exiv2::src:xmp.cpp:457': [],
	'exiv2::src:xmp.cpp:747': [
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.printStructure -> src/jpgimage.cpp::JpegBase.readMetadata -> src/xmp.cpp::XmpParser.decode",
    ],
	'exiv2::src:xmp.cpp:870': [
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/pngimage.cpp::PngImage.writeMetadata -> src/pngimage.cpp::PngImage.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.writeMetadata -> src/jpgimage.cpp::JpegBase.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
    ],
	'exiv2::src:xmp.cpp:90': ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.printStructure -> src/jpgimage.cpp::JpegBase.readMetadata -> src/xmp.cpp::XmpParser.decode -> src/xmp.cpp::check -> src/xmp.cpp::check_internal -> src/xmp.cpp::setError"],
    "exiv2::src:xmp.cpp:112": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.printStructure -> src/jpgimage.cpp::JpegBase.readMetadata -> src/xmp.cpp::XmpParser.decode -> src/xmp.cpp::check -> src/xmp.cpp::check_internal"],
    "exiv2::src:xmp.cpp:497": [],
    "exiv2::src:xmp.cpp:587": [],
    "exiv2::src:xmp.cpp:613": [],
    "exiv2::src:xmp.cpp:839": [
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/pngimage.cpp::PngImage.writeMetadata -> src/pngimage.cpp::PngImage.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",
        "instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/jpgimage.cpp::JpegBase.writeMetadata -> src/jpgimage.cpp::JpegBase.doWriteMetadata -> src/xmp.cpp::XmpParser.encode",        
    ],
    # filesystem_spec
    "filesystem_spec::fsspec:asyn.py:34": ["instrumented_fuzzer.py::TestOneInput -> fsspec/registry.py::filesystem -> fsspec/implementations/http.py::HTTPFileSystem.__init__ -> fsspec/implementations/asyn.py::AsyncFileSystem.__init__ -> fsspec/implementations/asyn.py::get_loop -> fsspec/implementations/asyn.py::get_lock"],
    "filesystem_spec::fsspec:asyn.py:92": ["instrumented_fuzzer.py::TestOneInput -> fsspec/core.py::open -> fsspec/implementations/http.py::HTTPFileSystem._open -> fsspec/implementations/asyn.py::sync"],
    "filesystem_spec::fsspec:asyn.py:930": [],
    "filesystem_spec::fsspec:asyn.py:940": [],
    "filesystem_spec::fsspec:implementations:http.py:124": ["instrumented_fuzzer.py::TestOneInput -> fsspec/core.py::open -> fsspec/implementations/http.py::HTTPFileSystem._open -> fsspec/implementations/http.py::HTTPFileSystem.set_session -> fsspec/implementations/http.py::HTTPFileSystem.close_session"],
    "filesystem_spec::fsspec:implementations:http.py:137": ["instrumented_fuzzer.py::TestOneInput -> fsspec/core.py::open -> fsspec/implementations/http.py::HTTPFileSystem._open -> fsspec/implementations/http.py::HTTPFileSystem.set_session"],
    "filesystem_spec::fsspec:spec.py:77": [],
    "filesystem_spec::fsspec:spec.py:86": [],
    "filesystem_spec::fsspec:asyn.py:96": ["instrumented_fuzzer.py::TestOneInput -> fsspec/core.py::open -> fsspec/implementations/http.py::HTTPFileSystem._open -> fsspec/implementations/asyn.py::sync"],
    "filesystem_spec::fsspec:asyn.py:101": ["instrumented_fuzzer.py::TestOneInput -> fsspec/core.py::open -> fsspec/implementations/http.py::HTTPFileSystem._open -> fsspec/implementations/asyn.py::sync"],
    "filesystem_spec::fsspec:implementations:http.py:127": ["instrumented_fuzzer.py::TestOneInput -> fsspec/core.py::open -> fsspec/implementations/http.py::HTTPFileSystem._open -> fsspec/implementations/http.py::HTTPFileSystem.set_session -> fsspec/implementations/http.py::HTTPFileSystem.close_session"],
    "filesystem_spec::fsspec:implementations:http.py:131": ["instrumented_fuzzer.py::TestOneInput -> fsspec/core.py::open -> fsspec/implementations/http.py::HTTPFileSystem._open -> fsspec/implementations/http.py::HTTPFileSystem.set_session -> fsspec/implementations/http.py::HTTPFileSystem.close_session"],
    "filesystem_spec::fsspec:spec.py:307": [],
    # guetzli
    "guetzli::guetzli:jpeg_data_reader.cc:163": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessSOF"],
    "guetzli::guetzli:jpeg_data_reader.cc:243": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessScan -> guetzli/jpeg_data_reader.cc::ProcessSOS"],
    "guetzli::guetzli:jpeg_data_reader.cc:338": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessDHT"],
    "guetzli::guetzli:jpeg_data_reader.cc:386": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessDRI"],
    "guetzli::guetzli:jpeg_data_reader.cc:649": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessScan -> guetzli/jpeg_data_reader.cc::RefineDCTBlock"],
    "guetzli::guetzli:jpeg_data_reader.cc:743": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessScan -> guetzli/jpeg_data_reader.cc::ProcessRestart"],
    "guetzli::guetzli:jpeg_data_reader.cc:939": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg"],
    "guetzli::guetzli:jpeg_data_reader.cc:1022": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg"],
    "guetzli::guetzli:jpeg_data_reader.cc:1036": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg"],
    "guetzli::guetzli:jpeg_data_reader.cc:1065": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg"],
    "guetzli::guetzli:jpeg_data_reader.cc:198": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessScan -> guetzli/jpeg_data_reader.cc::ProcessSOS"],
    "guetzli::guetzli:jpeg_data_reader.cc:231": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessScan -> guetzli/jpeg_data_reader.cc::ProcessSOS"],
    "guetzli::guetzli:jpeg_data_reader.cc:287": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessDHT"],
    "guetzli::guetzli:jpeg_data_reader.cc:185": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessScan -> guetzli/jpeg_data_reader.cc::ProcessSOS"],
    "guetzli::guetzli:jpeg_data_reader.cc:320": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessDHT"],
    "guetzli::guetzli:jpeg_data_reader.cc:351": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessDQT"],
    "guetzli::guetzli:jpeg_data_reader.cc:655": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessScan -> guetzli/jpeg_data_reader.cc::RefineDCTBlock"],
    "guetzli::guetzli:jpeg_data_reader.cc:700": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg -> guetzli/jpeg_data_reader.cc::ProcessScan -> guetzli/jpeg_data_reader.cc::RefineDCTBlock"],
    "guetzli::guetzli:jpeg_data_reader.cc:956": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> guetzli/jpeg_data_reader.cc::ReadJpeg"],
	# html5lib-python
    # easy
    'html5lib-python::html5lib:_tokenizer.py:67': ["instrumented_fuzzer.py::TestOneInput -> html5lib/html5parser.py::parse -> html5lib/html5parser.py::HTMLParser.parse -> html5lib/html5parser.py::HTMLParser._parse -> html5lib/html5parser.py::HTMLParser.mainLoop -> html5lib/_tokenizer.py::HTMLTokenizer.__iter__"],
    'html5lib-python::html5lib:_tokenizer.py:150': [],
    'html5lib-python::html5lib:_tokenizer.py:263': ["instrumented_fuzzer.py::TestOneInput -> html5lib/html5parser.py::parse -> html5lib/html5parser.py::HTMLParser.parse -> html5lib/html5parser.py::HTMLParser._parse -> html5lib/html5parser.py::HTMLParser.mainLoop -> html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> html5lib/_tokenizer.py::HTMLTokenizer.dataState"],
    'html5lib-python::html5lib:_tokenizer.py:393': ["instrumented_fuzzer.py::TestOneInput -> html5lib/html5parser.py::parse -> html5lib/html5parser.py::HTMLParser.parse -> html5lib/html5parser.py::HTMLParser._parse -> html5lib/html5parser.py::HTMLParser.mainLoop -> html5lib/_tokenizer.py::HTMLTokenizer.__iter__ -> html5lib/_tokenizer.py::HTMLTokenizer.dataState -> html5lib/_tokenizer.py::HTMLTokenizer.tagOpenState"],
    'html5lib-python::html5lib:html5parser.py:209': ["instrumented_fuzzer.py::TestOneInput -> html5lib/html5parser.py::parse -> html5lib/html5parser.py::HTMLParser.parse -> html5lib/html5parser.py::HTMLParser._parse -> html5lib/html5parser.py::HTMLParser.mainLoop"],
    'html5lib-python::html5lib:html5parser.py:600': [],
    'html5lib-python::html5lib:html5parser.py:981': [],
    # medium
    'html5lib-python::html5lib:html5parser.py:254': ["instrumented_fuzzer.py::TestOneInput -> html5lib/html5parser.py::parse -> html5lib/html5parser.py::HTMLParser.parse -> html5lib/html5parser.py::HTMLParser._parse -> html5lib/html5parser.py::HTMLParser.mainLoop"],
    'html5lib-python::html5lib:html5parser.py:635': [],
    'html5lib-python::html5lib:html5parser.py:1052': [],
    'html5lib-python::html5lib:html5parser.py:1054': [],
    'html5lib-python::html5lib:html5parser.py:1073': [],
    'html5lib-python::html5lib:html5parser.py:1076': [],
    'html5lib-python::html5lib:html5parser.py:1086': [],
    'html5lib-python::html5lib:html5parser.py:1249': [],
    'html5lib-python::html5lib:html5parser.py:1297': [],
    'html5lib-python::html5lib:html5parser.py:1572': [],
    # hard
    'html5lib-python::html5lib:html5parser.py:248': [],
    'html5lib-python::html5lib:html5parser.py:1029': [],
    'html5lib-python::html5lib:html5parser.py:1125': [],
    'html5lib-python::html5lib:html5parser.py:1329': [],
    'html5lib-python::html5lib:html5parser.py:1334': [],
    # extremely-hard
    'html5lib-python::html5lib:_tokenizer.py:1582': [],
    'html5lib-python::html5lib:_tokenizer.py:1631': [],
    'html5lib-python::html5lib:_tokenizer.py:1635': [],
    'html5lib-python::html5lib:html5parser.py:373': [],
    'html5lib-python::html5lib:html5parser.py:1099': [],
    # fuzzer-unreachable
    'html5lib-python::html5lib:html5parser.py:490': [],
    'html5lib-python::html5lib:html5parser.py:988': [],
    'html5lib-python::html5lib:html5parser.py:1133': [],
    'html5lib-python::html5lib:html5parser.py:1349': [],
    'html5lib-python::html5lib:html5parser.py:1512': [],
    'html5lib-python::html5lib:html5parser.py:1783': [],
    'html5lib-python::html5lib:html5parser.py:1904': [],
    'html5lib-python::html5lib:html5parser.py:2240': [],

    # lark-parser
    # easy
    'lark-parser::lark:lark.py:365': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__"],
    'lark-parser::lark:lark.py:376': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__"],
    'lark-parser::lark:lark.py:395': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__"],
    'lark-parser::lark:lexer.py:348': [],
    'lark-parser::lark:lexer.py:381': [],
    'lark-parser::lark:load_grammar.py:518': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::Grammar.compile"],
    'lark-parser::lark:load_grammar.py:707': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::Grammar.compile"],
    'lark-parser::lark:load_grammar.py:788': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::Grammar.compile"],
    'lark-parser::lark:load_grammar.py:806': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::Grammar.compile"],
    'lark-parser::lark:load_grammar.py:887': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::resolve_term_references"],
    # medium
	'lark-parser::lark:load_grammar.py:658': [],
	'lark-parser::lark:load_grammar.py:661': [],
	'lark-parser::lark:load_grammar.py:777': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::Grammar.compile"],
	'lark-parser::lark:load_grammar.py:778': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::Grammar.compile"],
	'lark-parser::lark:load_grammar.py:780': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::Grammar.compile"],
	'lark-parser::lark:load_grammar.py:785': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::Grammar.compile"],
	'lark-parser::lark:load_grammar.py:1167': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::GrammarBuilder._ignore"],
	'lark-parser::lark:load_grammar.py:1169': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::GrammarBuilder._ignore"],
	'lark-parser::lark:load_grammar.py:1170': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::GrammarBuilder._ignore"],
	'lark-parser::lark:utils.py:169': [],
    # hard
    'lark-parser::lark:load_grammar.py:305': [],
	'lark-parser::lark:load_grammar.py:308': [],
	'lark-parser::lark:load_grammar.py:338': [],
	'lark-parser::lark:load_grammar.py:344': [],
	'lark-parser::lark:load_grammar.py:368': [],
    # extremely-hard
    'lark-parser::lark:parsers:earley_forest.py:196': [],
	'lark-parser::lark:parsers:earley_forest.py:337': [],
	'lark-parser::lark:parsers:earley_forest.py:343': [],
	'lark-parser::lark:parsers:earley_forest.py:573': [],
	'lark-parser::lark:parsers:earley_forest.py:583': [],
    # fuzzer-unreachable
    'lark-parser::lark:load_grammar.py:1269': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar"],
    'lark-parser::lark:load_grammar.py:1280': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar"],
    'lark-parser::lark:load_grammar.py:1314': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::do_import"],
    'lark-parser::lark:load_grammar.py:1351': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::validate"],
    'lark-parser::lark:load_grammar.py:1369': ["instrumented_fuzzer.py::TestOneInput -> lark/lark.py::Lark.__init__ -> lark/load_grammar.py::load_grammar -> lark/load_grammar.py::validate"],

    # libbpf
	'libbpf::src:libbpf.c:10601': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::libbpf_get_error"],
	'libbpf::src:libbpf.c:8079': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem"],
	'libbpf::src:libbpf.c:8082': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem"],
	'libbpf::src:libbpf.c:9053': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__close"],
	'libbpf::src:libbpf.c:9089': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__close"],
	'libbpf::src:libbpf.c:1065': [
        "instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem -> src/libbpf.c::bpf_object_open -> src/libbpf.c::bpf_object__collect_relos -> src/libbpf.c::bpf_object__collect_st_ops_relos -> src/libbpf.c::find_struct_ops_map_by_offset -> src/libbpf.c::bpf_map__is_struct_ops",
    ],
	'libbpf::src:libbpf.c:1412': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem -> src/libbpf.c::bpf_object_open -> src/libbpf.c::bpf_object__init_maps -> src/libbpf.c::bpf_object_init_struct_ops -> src/libbpf.c::init_struct_ops_maps"],
	'libbpf::src:libbpf.c:1659': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem -> src/libbpf.c::bpf_object_open -> src/libbpf.c::bpf_object__elf_collect -> src/libbpf.c::bpf_object__init_kversion"],
	'libbpf::src:libbpf.c:1999': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem -> src/libbpf.c::bpf_object_open -> src/libbpf.c::bpf_object__init_global_data_maps"],
	'libbpf::src:libbpf.c:827': ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem -> src/libbpf.c::bpf_object_open -> src/libbpf.c::bpf_object__elf_collect -> src/libbpf.c::bpf_object__add_programs -> src/libbpf.c::bpf_object__init_prog"],
    "libbpf::src:libbpf.c:244": [],
    "libbpf::src:libbpf.c:313": [],
    "libbpf::src:libbpf.c:879": ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem -> src/libbpf.c::bpf_object_open -> src/libbpf.c::bpf_object__elf_collect -> src/libbpf.c::bpf_object__add_programs"],
    "libbpf::src:libbpf.c:894": ["instrumented_fuzzer.c::LLVMFuzzerTestOneInput -> src/libbpf.c::bpf_object__open_mem -> src/libbpf.c::bpf_object_open -> src/libbpf.c::bpf_object__elf_collect -> src/libbpf.c::bpf_object__add_programs"],
    "libbpf::src:libbpf.c:983": [],

    # libpng
	'libpng::png.c:188': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> png.c::png_create_read_struct -> png.c::png_create_png_struct -> png.c::png_user_version_check"],
	'libpng::png.c:340': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> png.c::png_create_info_struct"],
	'libpng::png.c:351': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> png.c::png_create_info_struct"],
	'libpng::png.c:58': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> png.c::png_sig_cmp"],
	'libpng::png.c:61': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> png.c::png_sig_cmp"],
	'libpng::png.c:2197': [],
	'libpng::png.c:2222': [],
	'libpng::png.c:2232': [],
	'libpng::png.c:2240': [],
	'libpng::png.c:465': [],
    "libpng::png.c:36": ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> png.c::png_set_sig_bytes"],
    "libpng::png.c:86": [],
    "libpng::png.c:421": [],
    "libpng::png.c:578": [],
    "libpng::png.c:775": [],
    # md4c
    # easy
    "md4c::src:md4c-html.c:134": [],
    "md4c::src:md4c-html.c:163": [],
    "md4c::src:md4c-html.c:240": [],
    "md4c::src:md4c-html.c:275": [],
    "md4c::src:md4c-html.c:290": [],
    "md4c::src:md4c-html.c:304": [],
    "md4c::src:md4c-html.c:320": [],
    "md4c::src:md4c-html.c:333": [],
    "md4c::src:md4c-html.c:464": [],
    "md4c::src:md4c-html.c:479": [],
    "md4c::src:md4c-html.c:522": [],
    # unreachable
    "md4c::src:md4c-html.c:190": [],
    "md4c::src:md4c-html.c:210": [],
    "md4c::src:md4c-html.c:353": [],
    "md4c::src:md4c-html.c:560": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> src/md4c-html.c::md_html"],

    # rich
    # easy
    "rich::rich:cells.py:89": ["instrumented_fuzzer.py::TestOneInput -> rich/console.py::Console.print -> rich/console.py::Console.render -> rich/text.py::Text.__rich_console__ -> rich/text.py::Text.wrap -> rich/_wrap.py::divide_line -> rich/cells.py::chop_cells -> rich/cells.py::get_character_cell_size"],
    "rich::rich:console.py:1029": [],
    "rich::rich:console.py:1036": [],
    "rich::rich:console.py:1568": ["instrumented_fuzzer.py::TestOneInput -> rich:console.py::Console.print -> rich:console.py::collect_renderables -> rich:console.py::check_text"],
    "rich::rich:console.py:2057": ["instrumented_fuzzer.py::TestOneInput -> rich:console.py::Console.__exit__ -> rich:console.py::Console._exit_buffer -> rich:console.py::Console._check_buffer -> rich:console.py::Console._write_buffer"],
    "rich::rich:markdown.py:352": ["instrumented_fuzzer.py::TestOneInput -> rich/console.py::Console.print -> rich/console.py::Console.render -> rich/markdown.py::Markdown.__rich_console__ -> rich/markdown.py::ListElement.__rich_console__"],
    "rich::rich:markdown.py:613": ["instrumented_fuzzer.py::TestOneInput -> rich:console.py::Console.print -> rich:console.py::Console.render -> rich:markdown.py::Markdown.__rich_console__"],
    "rich::rich:segment.py:351": ["instrumented_fuzzer.py::TestOneInput -> rich/console.py::Console.print -> rich/segment.py::Segment.split_and_crop_lines -> rich/segment.py::Segment.adjust_line_length"],
    "rich::rich:style.py:400": ["Console::print -> Console::render -> Markdown::__rich_console__ -> Table::__rich_console__ -> Style::pick_first"],
    "rich::rich:syntax.py:483": ["Console::print -> Console::render -> Markdown::__rich_console__ -> Syntax::__rich_console -> Syntax::_get_syntax -> Syntax::highlight"],
    "rich::rich:syntax.py:841": ["Console::print -> Console::render -> Markdown::__rich_console__ -> Syntax::__rich_console -> Syntax::_get_syntax -> Syntax::highlight -> Syntax::_apply_stylized_ranges -> Syntax::_get_code_index_for_syntax_position"],
    "rich::rich:text.py:413": [],
    
    # medium
    "rich::rich:style.py:362": ["Console::print -> Console::render -> Markdown::__rich_console__ -> Table::__rich_console__ -> Style::render -> Style::_make_ansi_codes"],
    "rich::rich:syntax.py:162": ["instrumented_fuzzer.py::TestOneInput -> rich:console.py::Console.print -> rich:console.py::Console.render -> rich:markdown.py::Markdown.__rich_console__ -> rich:syntax.py::Syntax.__rich_console__ -> rich:syntax.py::get_syntax -> rich:syntax.py::highlight -> rich:syntax.py::PygmentsSyntaxTheme.get_style_for_token"],
    "rich::rich:syntax.py:440": [],
    "rich::rich:syntax.py:445": [],
    "rich::rich:text.py:583": [],
    "rich::rich:text.py:796": [],
    "rich::rich:text.py:1156": [],
    "rich::rich:text.py:1162": [],
    # hard
    "rich::rich:syntax.py:160": ["instrumented_fuzzer.py::TestOneInput -> rich:console.py::Console.print -> rich:console.py::Console.render -> rich:markdown.py::Markdown.__rich_console__ -> rich:syntax.py::Syntax.__rich_console__ -> rich:syntax.py::get_syntax -> rich:syntax.py::highlight -> rich:syntax.py::PygmentsSyntaxTheme.get_style_for_token"],
    # extremely-hard
    "rich::rich:cells.py:106": ["instrumented_fuzzer.py::TestOneInput -> rich:console.py::Console.print -> rich:console.py::Console.render -> rich:markdown.py::Markdown.__rich_console__ -> rich:rule.py::Rule.__rich_console__ -> rich:cells.py::set_cell_size"],
    # fuzzer-unreachable
    "rich::rich:cells.py:109": ["instrumented_fuzzer.py::TestOneInput -> rich:console.py::Console.print -> rich:console.py::Console.render -> rich:markdown.py::Markdown.__rich_console__ -> rich:rule.py::Rule.__rich_console__ -> rich:cells.py::set_cell_size"],
    "rich::rich:table.py:649": ["Console::print -> Console::render -> Markdown::__rich_console__ -> Table::__rich_console__ -> Table::_calculate_column_widths -> Table::_get_cells"],
    "rich::rich:table.py:655": ["Console::print -> Console::render -> Markdown::__rich_console__ -> Table::__rich_console__ -> Table::_calculate_column_widths -> Table::_get_cells"],
    "rich::rich:table.py:661": ["Console::print -> Console::render -> Markdown::__rich_console__ -> Table::__rich_console__ -> Table::_calculate_column_widths -> Table::_get_cells"],
    "rich::rich:text.py:480": [],

    # varnish
    # easy
    "varnish::bin:varnishd:cache:cache_esi_parse.c:287": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Finish -> bin/varnishd/cache/cache_esi_parse.c::vep_emit_common -> bin/varnishd/cache/cache_esi_parse.c::vep_emit_verbatim"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:347": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_mark_common"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:409": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_do_comment"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:450": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_do_include -> bin/varnishd/cache/cache_esi_parse.c::include_attr_src"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:583": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_do_includ"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:774": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:876": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:1008": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    # medium
    "varnish::bin:varnishd:cache:cache_esi_parse.c:240": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_match"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:458": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_do_include -> bin/varnishd/cache/cache_esi_parse.c::include_attr_src"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:536": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_do_includ"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:715": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:1117": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Finish"],
    # hard
    "varnish::bin:varnishd:cache:cache_esi_parse.c:344": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_mark_common"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:557": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_do_includ"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:568": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse -> bin/varnishd/cache/cache_esi_parse.c::vep_do_includ"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:662": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:682": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:709": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:900": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    # extremely-hard
    "varnish::bin:varnishd:cache:cache_esi_parse.c:695": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    # unreachable
    "varnish::bin:varnishd:cache:cache_esi_parse.c:672": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:803": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:856": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    "varnish::bin:varnishd:cache:cache_esi_parse.c:977": ["instrumented_fuzzer.cpp::LLVMFuzzerTestOneInput -> bin/varnishd/cache/cache_esi_parse.c::VEP_Parse"],
    
    # wamr
    # easy
	'wamr::core:iwasm:aot:aot_loader.c:95': [
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_type_info -> core/iwasm/aot/aot_loader.c::read_uint32 -> core/iwasm/aot/aot_loader.c::TEMPLATE_READ -> core/iwasm/aot/aot_loader.c::CHECK_BUF -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_custom_section -> core/iwasm/aot/aot_loader.c::load_native_symbol_section -> core/iwasm/aot/aot_loader.c::read_string -> core/iwasm/aot/aot_loader.c::load_string -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_custom_section -> core/iwasm/aot/aot_loader.c::load_name_section -> core/iwasm/aot/aot_loader.c::read_string -> core/iwasm/aot/aot_loader.c::load_string -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_custom_section -> core/iwasm/aot/aot_loader.c::read_string -> core/iwasm/aot/aot_loader.c::load_string -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_import_global_info -> core/iwasm/aot/aot_loader.c::load_import_globals -> core/iwasm/aot/aot_loader.c::read_string -> core/iwasm/aot/aot_loader.c::load_string -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_import_func_info -> core/iwasm/aot/aot_loader.c::load_import_funcs -> core/iwasm/aot/aot_loader.c::read_string -> core/iwasm/aot/aot_loader.c::load_string -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_object_data_sections_info -> core/iwasm/aot/aot_loader.c::load_object_data_sections -> core/iwasm/aot/aot_loader.c::read_string -> core/iwasm/aot/aot_loader.c::load_string -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_export_section -> core/iwasm/aot/aot_loader.c::load_exports -> core/iwasm/aot/aot_loader.c::read_string -> core/iwasm/aot/aot_loader.c::load_string -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_relocation_section -> core/iwasm/aot/aot_loader.c::read_string -> core/iwasm/aot/aot_loader.c::load_string -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_custom_section -> core/iwasm/aot/aot_loader.c::load_name_section -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_table_info -> core/iwasm/aot/aot_loader.c::load_table_list -> core/iwasm/aot/aot_loader.c::load_init_expr -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_table_info -> core/iwasm/aot/aot_loader.c::load_table_init_data_list -> core/iwasm/aot/aot_loader.c::load_init_expr -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_global_info -> core/iwasm/aot/aot_loader.c::load_globals -> core/iwasm/aot/aot_loader.c::load_init_expr -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_memory_info -> core/iwasm/aot/aot_loader.c::load_mem_init_data_list -> core/iwasm/aot/aot_loader.c::load_init_expr -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_object_data_sections_info -> core/iwasm/aot/aot_loader.c::load_object_data_sections -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_function_section -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_relocation_section -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::create_sections -> core/iwasm/aot/aot_loader.c::resolve_execute_mode -> core/iwasm/aot/aot_loader.c::check_buf",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::create_sections -> core/iwasm/aot/aot_loader.c::check_buf",
    ],
    "wamr::core:iwasm:aot:aot_loader.c:319": [
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::create_sections -> core/iwasm/aot/aot_loader.c::loader_mmap",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_object_data_sections_info -> core/iwasm/aot/aot_loader.c::load_object_data_sections -> core/iwasm/aot/aot_loader.c::loader_mmap",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::try_merge_data_and_text -> core/iwasm/aot/aot_loader.c::loader_mmap",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_relocation_section -> core/iwasm/aot/aot_loader.c::loader_mmap",
    ],
	'wamr::core:iwasm:aot:aot_loader.c:325': [
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::create_sections -> core/iwasm/aot/aot_loader.c::loader_mmap",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_object_data_sections_info -> core/iwasm/aot/aot_loader.c::load_object_data_sections -> core/iwasm/aot/aot_loader.c::loader_mmap",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::try_merge_data_and_text -> core/iwasm/aot/aot_loader.c::loader_mmap",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_relocation_section -> core/iwasm/aot/aot_loader.c::loader_mmap",
    ],
    "wamr::core:iwasm:aot:aot_loader.c:367": [],
    "wamr::core:iwasm:aot:aot_loader.c:376": [],
	'wamr::core:iwasm:aot:aot_loader.c:456': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_target_info_section -> core/iwasm/aot/aot_loader.c::check_machine_info"],
	'wamr::core:iwasm:aot:aot_loader.c:547': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_target_info_section"],
    "wamr::core:iwasm:aot:aot_loader.c:588": ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_target_info_section -> core/iwasm/aot/aot_loader.c::check_machine_info"],
	'wamr::core:iwasm:aot:aot_loader.c:605': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_target_info_section"],
    "wamr::core:iwasm:aot:aot_loader.c:666": ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_custom_section -> core/iwasm/aot/aot_loader.c::load_native_symbol_section"],
    # medium
	'wamr::core:iwasm:aot:aot_loader.c:4120': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::create_sections -> core/iwasm/aot/aot_loader.c::resolve_execute_mode"],
	'wamr::core:iwasm:aot:aot_loader.c:4236': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file  -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::create_sections"],
	'wamr::core:iwasm:aot:aot_loader.c:4312': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load"],
    # hard
    "wamr::core:iwasm:aot:aot_loader.c:417": ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_target_info_section -> core/iwasm/aot/aot_loader.c::check_machine_info -> core/iwasm/aot/aot_loader.c::get_aot_file_target"],
    # extremely-hard
    "wamr::core:iwasm:aot:aot_loader.c:916": ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_custom_section"],
    "wamr::core:iwasm:aot:aot_loader.c:1132": [
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_table_info -> core/iwasm/aot/aot_loader.c::load_table_list -> core/iwasm/aot/aot_loader.c::load_init_expr",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_table_info -> core/iwasm/aot/aot_loader.c::load_table_init_data_list -> core/iwasm/aot/aot_loader.c::load_init_expr",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_global_info -> core/iwasm/aot/aot_loader.c::load_globals -> core/iwasm/aot/aot_loader.c::load_init_expr",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_memory_info -> core/iwasm/aot/aot_loader.c::load_mem_init_data_list -> core/iwasm/aot/aot_loader.c::load_init_expr",
    ],
    "wamr::core:iwasm:aot:aot_loader.c:1304": [
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_table_info -> core/iwasm/aot/aot_loader.c::load_table_list -> core/iwasm/aot/aot_loader.c::load_init_expr",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_table_info -> core/iwasm/aot/aot_loader.c::load_table_init_data_list -> core/iwasm/aot/aot_loader.c::load_init_expr",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_global_info -> core/iwasm/aot/aot_loader.c::load_globals -> core/iwasm/aot/aot_loader.c::load_init_expr",
        "instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_init_data_section -> core/iwasm/aot/aot_loader.c::load_memory_info -> core/iwasm/aot/aot_loader.c::load_mem_init_data_list -> core/iwasm/aot/aot_loader.c::load_init_expr",
    ],
    "wamr::core:iwasm:aot:aot_loader.c:4013": ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections"],
	# unreachable
	'wamr::core:iwasm:aot:aot_loader.c:660': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_custom_section -> core/iwasm/aot/aot_loader.c::load_native_symbol_section"],
	'wamr::core:iwasm:aot:aot_loader.c:698': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_custom_section -> core/iwasm/aot/aot_loader.c::load_native_symbol_section"],
	'wamr::core:iwasm:aot:aot_loader.c:967': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::aot_unload -> core/iwasm/aot/aot_loader.c::destroy_import_memories"],
	'wamr::core:iwasm:aot:aot_loader.c:1013': ["instrumented_fuzzer.cc::LLVMFuzzerTestOneInput -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load -> core/iwasm/common/wasm_runtime_common.c::wasm_runtime_load_ex -> core/iwasm/aot/aot_loader.c::aot_load_from_aot_file -> core/iwasm/aot/aot_loader.c::load -> core/iwasm/aot/aot_loader.c::load_from_sections -> core/iwasm/aot/aot_loader.c::load_memory_info -> core/iwasm/aot/aot_loader.c::load_mem_init_data_list"],
}

def build_rows():
    data = []

    for root_directory in root_directories:
        deepest_dirs = []
        for dirpath, dirnames, filenames in os.walk(root_directory):
            if not dirnames:
                deepest_dirs.append(dirpath)

        for path in sorted(deepest_dirs):
            project = path.split('/')[-4]
            level = path.split('/')[-2]
            target = path.split('/')[-1]
            target_file, target_line = target.rsplit(':', 1)
            target_file = target_file.replace(':', '/')
            target_line = int(target_line)

            url = DOWNLOAD_LINK[project]['url']
            commit = DOWNLOAD_LINK[project]['commit']

            patch_file = os.path.join(path, 'target.patch')
            assert os.path.exists(patch_file), f"Patch file does not exist: {patch_file}"
            with open(patch_file, 'r') as f:
                patch_content = f.read()

            index = path.find('/realistic/')
            assert index != -1, f"'/realistic/' not found in path: {path}"
            docker_path = os.path.join(path[:index], 'base-env', 'Dockerfile')
            fuzzing_harness_path = os.path.join(path[:index], 'base-env', DOWNLOAD_LINK[project]['harness'])
            build_path = os.path.join(path[:index], 'base-env', 'build.sh')
            assert os.path.exists(docker_path), f"Dockerfile does not exist: {docker_path}"
            assert os.path.exists(fuzzing_harness_path), f"Fuzzing harness does not exist: {fuzzing_harness_path}"
            assert os.path.exists(build_path), f"Build script does not exist: {build_path}"

            with open(docker_path, 'r') as f:
                dockerfile_content = f.read()
            with open(fuzzing_harness_path, 'r') as f:
                fuzzing_harness_content = f.read()
            with open(build_path, 'r') as f:
                build_script_content = f.read()

            # The realistic context: every file of the target directory except target.patch.
            realistic_context = {}
            for file in sorted(os.listdir(path)):
                file_path = os.path.join(path, file)
                if os.path.isfile(file_path) and file != 'target.patch':
                    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()
                    realistic_context[file] = content
            realistic_context_str = "<code>\n"
            for context_filename, context_code in realistic_context.items():
                realistic_context_str += f"[start of {context_filename}]\n"
                realistic_context_str += context_code
                realistic_context_str += f"\n[end of {context_filename}]\n"
            realistic_context_str += "</code>"

            realistic_call_paths = CALL_PATHS[f"{project}::{target}"]
            formatted_call_paths = []
            for call_path in realistic_call_paths:
                current_path = []
                for step in call_path.split('->'):
                    classname = None
                    assert step.count('::') == 1, f"{project}::{target} contains a call path w/o filename: {step}"
                    filename, funcname = step.strip().split("::", 1)
                    if '.' in funcname:
                        classname, funcname = funcname.split('.', 1)
                    if classname is None:
                        item = {'file': filename, 'function': funcname}
                    else:
                        item = {'file': filename, 'class': classname, 'function': funcname}
                    current_path.append(item)
                formatted_call_paths.append(current_path)

            call_paths_str = dumps(formatted_call_paths, indent=2)

            data.append({
                "target_id": f"{project}::{target}",
                "project": project,
                "level": level,
                "target_file": target_file,
                "target_line": target_line,
                "url": url,
                "commit": commit,
                "patch": patch_content,
                "dockerfile": dockerfile_content,
                "fuzzing_harness": fuzzing_harness_content,
                "build_script": build_script_content,
                "harness_name": DOWNLOAD_LINK[project]['harness'].replace('fuzzer.', 'instrumented_fuzzer.'),
                "realistic_context": realistic_context_str,
                "realistic_call_paths": call_paths_str,
            })

    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", help="write the dataset to this directory (test.jsonl, the split 'test')")
    parser.add_argument("--json-out", help="write the rows to this JSON file")
    args = parser.parse_args()

    data = build_rows()
    ids = [row["target_id"] for row in data]
    assert len(ids) == len(set(ids)), f"duplicate target ids: {sorted({i for i in ids if ids.count(i) > 1})}"
    print(f"{len(data)} targets")

    if args.out_dir:
        os.makedirs(args.out_dir, exist_ok=True)
        out_file = os.path.join(args.out_dir, "test.jsonl")
        with open(out_file, 'w', encoding='utf-8') as f:
            for row in data:
                f.write(json.dumps(row, ensure_ascii=False) + "\n")
        print(f"Wrote {out_file}")

    if args.json_out:
        with open(args.json_out, 'w') as f:
            json.dump(data, f, indent=4)
        print(f"Wrote {args.json_out}")


if __name__ == "__main__":
    main()
