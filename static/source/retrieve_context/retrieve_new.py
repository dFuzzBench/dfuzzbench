import os
import sys
import argparse
import json
import logging
import shutil
import subprocess
from collections import defaultdict
from typing import List, Dict, Any, Tuple, Optional

# Third-party imports
import bm25s
import tiktoken

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

RESULTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__)))),
                           "results", "retrieve_context")

# Extensions to look for
VALID_EXTENSIONS =('.py', '.c', '.cpp', '.cc', '.h', '.hpp')
# Directories to ignore
IGNORE_DIRS = {'doc', 'docs', 'test', 'tests', 'example', 'examples', 'node_modules', 'venv', '.git'}

# --- Configuration Data ---
PROJECT_INFO = {
    "bleach": {
        "github_link": "https://github.com/mozilla/bleach.git",
        "commit_hash": "4bc1bff36841c04152525b8e57fd3740f34ec796",
    },
    "clib": {
        "github_link": "https://github.com/clibs/clib.git",
        "commit_hash": "6d96e53349784087d1f0c13aca2c9b6cdf3f4c4c",
    },
    "cmark": {
        "github_link": "https://github.com/commonmark/cmark.git",
        "commit_hash": "9d74662f4b12f0c7c2f610231a47348fe8b7b619",
    },
    "cpp-httplib": {
        "github_link": "https://github.com/yhirose/cpp-httplib",
        "commit_hash": "4b2b851dbb91f9d6c2299976b5d03f7c5c1a312d",
    },
    "exiv2": {
        "github_link": "https://github.com/Exiv2/exiv2",
        "commit_hash": "66c3cda1835248df8ece24464b4546314d0233a7",
    },
    "filesystem_spec": {
        "github_link": "https://github.com/fsspec/filesystem_spec",
        "commit_hash": "3675a7cd163a3d7bc82f83e238ddc967f7b145c8",
    },
    "guetzli": {
        "github_link": "https://github.com/google/guetzli",
        "commit_hash": "214f2bb42abf5a577c079d00add5d6cc470620d3",
    },
    "html5lib-python": {
        "github_link": "https://github.com/html5lib/html5lib-python",
        "commit_hash": "fd4f032bc090d44fb11a84b352dad7cbee0a4745",
    },
    "lark-parser": {
        "github_link": "https://github.com/lark-parser/lark",
        "commit_hash": "24f19a35f376b9320d53f4d987793fb8b1765f37",
    },
    "libbpf": {
        "github_link": "https://github.com/libbpf/libbpf",
        "commit_hash": "c5f22aca0f3aa855daa159b2777472b35e721804",
    },
    "libpng": {
        "github_link": "https://github.com/pnggroup/libpng.git",
        "commit_hash": "c1cc0f3f4c3d4abd11ca68c59446a29ff6f95003",
    },
    "md4c": {
        "github_link": "https://github.com/mity/md4c",
        "commit_hash": "481fbfbdf72daab2912380d62bb5f2187d438408",
    },
    "rich": {
        "github_link": "https://github.com/Textualize/rich",
        "commit_hash": "72e3bb33d44fd96881f7742b77137983907a942f",
    },    
    "varnish": {
        "github_link": "https://github.com/varnishcache/varnish-cache",
        "commit_hash": "41059cf6815d0f366a5145fdb69d310e37e6d380",
    },
    "wamr": {
        "github_link": "https://github.com/bytecodealliance/wasm-micro-runtime",
        "commit_hash": "0119b17526ae447c5c57784d9deb90c04af51fe5",
    },
}

TARGETS = {
    "lark-parser": {
        "easy": {
            "lark:load_grammar.py:887": ("lark/load_grammar.py", 887, "resolve_term_references"),
        },
        "medium": {
            "lark:utils.py:169": ("lark/utils.py", 169, "_test_unicode_category"),
        },
    },
    "bleach": {
        "easy": {
            "bleach:_vendor:html5lib:_tokenizer.py:247": ("bleach/_vendor/html5lib/_tokenizer.py", 247, "HTMLTokenizer.emitCurrentToken"),
            "bleach:_vendor:html5lib:_tokenizer.py:263": ("bleach/_vendor/html5lib/_tokenizer.py", 263, "HTMLTokenizer.dataState"),
            "bleach:_vendor:html5lib:_tokenizer.py:269": ("bleach/_vendor/html5lib/_tokenizer.py", 269, "HTMLTokenizer.dataState"),
            "bleach:_vendor:html5lib:_tokenizer.py:378": ("bleach/_vendor/html5lib/_tokenizer.py", 378, "HTMLTokenizer.tagOpenState"),
            "bleach:_vendor:html5lib:_tokenizer.py:397": ("bleach/_vendor/html5lib/_tokenizer.py", 397, "HTMLTokenizer.tagOpenState"),
            "bleach:_vendor:html5lib:html5parser.py:980": ("bleach/_vendor/html5lib/html5parser.py", 980, "InBodyPhase::processEOF"),
            "bleach:html5lib_shim.py:374": ("bleach/html5lib_shim.py", 374, "BleachHTMLTokenizer.__iter__"),
            "bleach:html5lib_shim.py:748": ("bleach/html5lib_shim.py", 748, "BleachHTMLSerializer.serialize"),
        },
        "medium": {
            "bleach:_vendor:html5lib:_tokenizer.py:1247": ("bleach/_vendor/html5lib/_tokenizer.py", 1247, "HTMLTokenizer::commentEndDashState"),
            "bleach:_vendor:html5lib:_tokenizer.py:1333": ("bleach/_vendor/html5lib/_tokenizer.py", 1333, "HTMLTokenizer.beforeDoctypeNameState"),
            "bleach:_vendor:html5lib:_tokenizer.py:1358": ("bleach/_vendor/html5lib/_tokenizer.py", 1358, "HTMLTokenizer.doctypeNameState"),
            "bleach:_vendor:html5lib:_tokenizer.py:1416": ("bleach/_vendor/html5lib/_tokenizer.py", 1416, "HTMLTokenizer::afterDoctypeNameState"),
            "bleach:html5lib_shim.py:736": ("bleach/html5lib_shim.py", 736, "BleachHTMLSerializer.serialize"),
            "bleach:html5lib_shim.py:742": ("bleach/html5lib_shim.py", 742, "BleachHTMLSerializer.serialize"),
        },
        "hard": {
            "bleach:html5lib_shim.py:580": ("bleach/html5lib_shim.py", 580, "convert_entities"),
        },
        "unreachable": {
            "bleach:html5lib_shim.py:470": ("bleach/html5lib_shim.py", 470, "BleachHTMLTokenizer.emitCurrentToken"),
            "bleach:html5lib_shim.py:544": ("bleach/html5lib_shim.py", 544, "convert_entity"),
            "bleach:html5lib_shim.py:588": ("bleach/html5lib_shim.py", 588, "convert_entities"),
            "bleach:html5lib_shim.py:634": ("bleach/html5lib_shim.py", 634, "match_entity"),
        },
    },
    "html5lib-python": {
        "easy": {
            "html5lib:_tokenizer.py:67": ("html5lib/_tokenizer.py", 67, "HTMLTokenizer.__iter__"),
            "html5lib:_tokenizer.py:261": ("html5lib/_tokenizer.py", 261, "HTMLTokenizer.dataState"),
            "html5lib:_tokenizer.py:263": ("html5lib/_tokenizer.py", 263, "HTMLTokenizer.dataState"),
            "html5lib:_tokenizer.py:393": ("html5lib/_tokenizer.py", 393, "HTMLTokenizer.tagOpenState"),
            "html5lib:html5parser.py:600": ("html5lib/html5parser.py", 600, "InitialPhase.processEOF"),
        },
        "medium": {
            "html5lib:html5parser.py:1052": ("html5lib/html5parser.py", 1052, "InBodyPhase.startTagListItem"),
            "html5lib:html5parser.py:1076": ("html5lib/html5parser.py", 1076, "InBodyPhase.startTagHeading"),
        },
        "extreme-hard": {
            "html5lib:html5parser.py:373": ("html5lib/html5parser.py", 373, "HTMLParser.resetInsertionMode"),
            "html5lib:html5parser.py:885": ("html5lib/html5parser.py", 885, "AfterHeadPhase.startTagFromHead"),
            "html5lib:html5parser.py:1099": ("html5lib/html5parser.py", 1099, "InBodyPhase.startTagNobr"),
        },
        "unreachable": {
            "html5lib:html5parser.py:490": ("html5lib/html5parser.py", 490, "InitialPhase.processDoctype"),
            "html5lib:html5parser.py:976": ("html5lib/html5parser.py", 976, "InBodyPhase.processSpaceCharactersDropNewline"),
            "html5lib:html5parser.py:1349": ("html5lib/html5parser.py", 1349, "InBodyPhase.endTagForm"),
            "html5lib:html5parser.py:1512": ("html5lib/html5parser.py", 1512, "InBodyPhase.endTagFormatting"),
            "html5lib:html5parser.py:1783": ("html5lib/html5parser.py", 1783, "InTablePhase.endTagTable"),
            "html5lib:html5parser.py:1904": ("html5lib/html5parser.py", 1904, "InCaptionPhase.endTagCaption"),
            "html5lib:html5parser.py:2240": ("html5lib/html5parser.py", 2240, "InCellPhase.endTagTableCell"),
        },
    },
    "rich": {
        "easy": {
            "rich:cells.py:89": ("rich/cells.py", 89, "get_character_cell_size"),
            "rich:console.py:2121": ("rich/console.py", 2121, "Console._render_buffer"),
            "rich:markdown.py:352": ("rich/markdown.py", 352, "ListElement.__rich_console__"),
            "rich:markdown.py:613": ("rich/markdown.py", 613, "Markdown.__rich_console__"),
            "rich:segment.py:317": ("rich/segment.py", 317, "Segment.split_and_crop_lines"),
            "rich:segment.py:351": ("rich/segment.py", 351, "Segment.adjust_line_length"),
            "rich:style.py:400": ("rich/style.py", 400, "Style.pick_first"),
            "rich:syntax.py:483": ("rich/syntax.py", 483, "Syntax.highlight"),
            "rich:syntax.py:841": ("rich/syntax.py", 841, "Syntax._get_code_index_for_syntax_position"),
            "rich:text.py:413": ("rich/text.py", 413, "Text.plain"),
        },
        "medium": {
            "rich:style.py:362": ("rich/style.py", 362, "Style._make_ansi_codes"),
            "rich:text.py:583": ("rich/text.py", 583, "Text.extend_style"),
            "rich:text.py:1156": ("rich/text.py", 1156, "Text.divide"),
        },
        "hard": {
            "rich:cells.py:162": ("rich/cells.py", 162, "chop_cells"),
            "rich:segment.py:457": ("rich/segment.py", 457, "Segment.align_top"),
            "rich:syntax.py:160": ("rich/syntax.py", 160, "PygmentsSyntaxTheme.get_style_for_token"),
        },
        "unreachable": {
            "rich:table.py:649": ("rich/table.py", 649, "Table._get_cells"),
            "rich:table.py:655": ("rich/table.py", 655, "Table._get_cells"),
            "rich:table.py:661": ("rich/table.py", 661, "Table._get_cells"),
        },
    },
    "wamr": {
        "hard": {
            "core:iwasm:aot:aot_loader.c:417": ("core/iwasm/aot/aot_loader.c", 417, "get_aot_file_target"),
        },
    },
    "md4c": {
        "easy": {
            "src:md4c-html.c:163": ("src/md4c-html.c", 163, "hex_val"),
            "src:md4c-html.c:304": ("src/md4c-html.c", 304, "render_open_code_block"),
            "src:md4c-html.c:320": ("src/md4c-html.c", 320, "render_open_td_block"),
            "src:md4c-html.c:386": ("src/md4c-html.c", 386, "enter_block_callback"),
            "src:md4c-html.c:464": ("src/md4c-html.c", 464, "enter_span_callback"),
            "src:md4c-html.c:479": ("src/md4c-html.c", 479, "leave_span_callback"),
        },
        "unreachable": {
            "src:md4c-html.c:353": ("src/md4c-html.c", 353, "render_close_img_span"),
        },
    },
    "varnish": {
        "easy": {
            "bin:varnishd:cache:cache_esi_parse.c:756": ("bin/varnishd/cache/cache_esi_parse.c", 756, "VEP_Parse"),
            "bin:varnishd:cache:cache_esi_parse.c:876": ("bin/varnishd/cache/cache_esi_parse.c", 876, "VEP_Parse"),
            "bin:varnishd:cache:cache_esi_parse.c:932": ("bin/varnishd/cache/cache_esi_parse.c", 932, "VEP_Parse"),
        },
        "hard": {
            "bin:varnishd:cache:cache_esi_parse.c:662": ("bin/varnishd/cache/cache_esi_parse.c", 662, "VEP_Parse"),
            "bin:varnishd:cache:cache_esi_parse.c:682": ("bin/varnishd/cache/cache_esi_parse.c", 682, "VEP_Parse"),
            "bin:varnishd:cache:cache_esi_parse.c:709": ("bin/varnishd/cache/cache_esi_parse.c", 709, "VEP_Parse"),
        },
    },
    "cmark": {
        "easy": {
            "src:blocks.c:188": ("src/blocks.c", 188, "add_line"),
            "src:blocks.c:326": ("src/blocks.c", 326, "finalize"),
            "src:blocks.c:531": ("src/blocks.c", 531, "finalize_document"),
            "src:blocks.c:1035": ("src/blocks.c", 1035, "open_new_blocks"),
        },
    },
}

VIBE_FEATURE_ONLY_TARGETS = {
    "bleach": {
        "easy": {
            "bleach:linkifier.py:183": ("bleach/linkifier.py", 183, "Linker.linkify"),
            "bleach:linkifier.py:273": ("bleach/linkifier.py", 273, "LinkifyFilter.extract_character_data"),
            "bleach:linkifier.py:278": ("bleach/linkifier.py", 278, "LinkifyFilter.extract_character_data"),
            "bleach:linkifier.py:284": ("bleach/linkifier.py", 284, "LinkifyFilter.extract_character_data"),
            "bleach:linkifier.py:286": ("bleach/linkifier.py", 286, "LinkifyFilter.extract_character_data"),
        },
        "medium": {
            "bleach:linkifier.py:280": ("bleach/linkifier.py", 280, "LinkifyFilter.extract_character_data"),
        },
        "unreachable": {
            "bleach:linkifier.py:276": ("bleach/linkifier.py", 276, "LinkifyFilter.extract_character_data"),
            "bleach:linkifier.py:282": ("bleach/linkifier.py", 282, "LinkifyFilter.extract_character_data"),
            "bleach:linkifier.py:288": ("bleach/linkifier.py", 288, "LinkifyFilter.extract_character_data"),
        },
    },
    "html5lib-python": {
        "easy": {
            "html5lib:_tokenizer.py:63": ("html5lib/_tokenizer.py", 63, "HTMLTokenizer.__iter__"),
            "html5lib:_tokenizer.py:235": ("html5lib/_tokenizer.py", 235, "HTMLTokenizer.emitCurrentToken"),
            "html5lib:html5parser.py:983": ("html5lib/html5parser.py", 983, "InBodyPhase.processCharacters"),
            "html5lib:html5parser.py:992": ("html5lib/html5parser.py", 992, "InBodyPhase.processSpaceCharactersNonPre"),
            "html5lib:treebuilders:base.py:304": ("html5lib/treebuilders/base.py", 304, "TreeBuilder.insertComment"),
            "html5lib:treebuilders:base.py:340": ("html5lib/treebuilders/base.py", 340, "TreeBuilder.insertElementNormal"),
            "html5lib:treebuilders:base.py:371": ("html5lib/treebuilders/base.py", 371, "TreeBuilder.insertText"),
        },
        "medium": {
            "html5lib:html5parser.py:426": ("html5lib/html5parser.py", 426, "Phase.processSpaceCharacters"),
        },
        "unreachable": {
            "html5lib:custom_deque.py:5": ("html5lib/custom_deque.py", 5, "TokenQueue.__init__"),
            "html5lib:custom_deque.py:10": ("html5lib/custom_deque.py", 10, "TokenQueue.append"),
            "html5lib:html5parser.py:423": ("html5lib/html5parser.py", 423, "Phase.processCharacters"),
            "html5lib:html5parser.py:426": ("html5lib/html5parser.py", 426, "Phase.processSpaceCharacters"),
            "html5lib:html5parser.py:976": ("html5lib/html5parser.py", 976, "InBodyPhase.processSpaceCharactersDropNewline"),
            "html5lib:html5parser.py:1651": ("html5lib/html5parser.py", 1651, "TextPhase.processCharacters"),
            "html5lib:html5parser.py:1841": ("html5lib/html5parser.py", 1841, "InTableTextPhase.flushCharacters"),
            "html5lib:html5parser.py:2290": ("html5lib/html5parser.py", 2290, "InSelectPhase.processCharacters"),
            "html5lib:treebuilders:base.py:287": ("html5lib/treebuilders/base.py", 287, "TreeBuilder.insertRoot"),
            "html5lib:treebuilders:base.py:298": ("html5lib/treebuilders/base.py", 298, "TreeBuilder.insertDoctype"),
            "html5lib:treebuilders:base.py:316": ("html5lib/treebuilders/base.py", 316, "TreeBuilder.createElement"),
            "html5lib:treebuilders:base.py:349": ("html5lib/treebuilders/base.py", 349, "TreeBuilder.insertElementTable"),
            "html5lib:treebuilders:base.py:376": ("html5lib/treebuilders/base.py", 376, "TreeBuilder.insertText"),
        },
    },
    "rich": {
        "easy": {
            "rich:markdown.py:556": ("rich/markdown.py", 556, "Markdown.__init__"),
            "rich:markdown.py:560": ("rich/markdown.py", 560, "Markdown.__init__"),
            "rich:syntax.py:285": ("rich/syntax.py", 285, "Syntax.__init__"),
            "rich:text.py:1213": ("rich/text.py", 1213, "Text.wrap"),
        },
        "medium": {
            "rich:table.py:479": ("rich/table.py", 479, "Table.__rich_console__"),
        },
        "unreachable": {
            "rich:markdown.py:554": ("rich/markdown.py", 554, "Markdown.__init__"),
            "rich:markdown.py:558": ("rich/markdown.py", 558, "Markdown.__init__"),
            "rich:panel.py:22": ("rich/panel.py", 22, "HolographicCompositor.add_panel"),
            "rich:syntax.py:283": ("rich/syntax.py", 283, "Syntax.__init__"),
            "rich:text.py:1214": ("rich/text.py", 1214, "Text.wrap"),
        },
    },
}

VIBE_OVERALL_TARGETS = {
    "bleach": {
        "easy": {
            "bleach:linkifier.py:438": ("bleach/linkifier.py", 438, "LinkifyFilter.handle_links"),
            "bleach:linkifier.py:444": ("bleach/linkifier.py", 444, "LinkifyFilter.handle_links"),
            "bleach:linkifier.py:449": ("bleach/linkifier.py", 449, "LinkifyFilter.handle_links"),
            "bleach:linkifier.py:607": ("bleach/linkifier.py", 607, "LinkifyFilter._should_skip"),
            "bleach:linkifier.py:648": ("bleach/linkifier.py", 648, "LinkifyFilter._handle_normal_state"),
            "bleach:linkifier.py:652": ("bleach/linkifier.py", 652, "LinkifyFilter._handle_buffering_state"),
            "bleach:linkifier.py:658": ("bleach/linkifier.py", 658, "LinkifyFilter._handle_buffering_state"),
            "bleach:linkifier.py:661": ("bleach/linkifier.py", 661, "LinkifyFilter._handle_start_tag"),
            "bleach:linkifier.py:662": ("bleach/linkifier.py", 662, "LinkifyFilter._handle_start_tag"),
            "bleach:linkifier.py:669": ("bleach/linkifier.py", 669, "LinkifyFilter._handle_start_tag"),
            "bleach:linkifier.py:673": ("bleach/linkifier.py", 673, "LinkifyFilter._handle_end_tag"),
            "bleach:linkifier.py:678": ("bleach/linkifier.py", 678, "LinkifyFilter._handle_end_tag"),
            "bleach:linkifier.py:698": ("bleach/linkifier.py", 698, "LinkifyFilter._handle_other_token"),
            "bleach:linkifier.py:718": ("bleach/linkifier.py", 718, "LinkifyFilter.__iter__"),
            "bleach:linkifier.py:720": ("bleach/linkifier.py", 720, "LinkifyFilter.__iter__"),
        },
        "unreachable": {
            "bleach:linkifier.py:436": ("bleach/linkifier.py", 436, "LinkifyFilter.handle_links"),
            "bleach:linkifier.py:608": ("bleach/linkifier.py", 608, "LinkifyFilter._should_skip"),
            "bleach:linkifier.py:667": ("bleach/linkifier.py", 667, "LinkifyFilter._handle_start_tag"),
            "bleach:linkifier.py:676": ("bleach/linkifier.py", 676, "LinkifyFilter._handle_end_tag"),
            "bleach:linkifier.py:685": ("bleach/linkifier.py", 685, "LinkifyFilter._handle_characters"),
            "bleach:linkifier.py:696": ("bleach/linkifier.py", 696, "LinkifyFilter._handle_other_token"),
        },
    },
    "html5lib-python": {
        "unreachable": {
            "html5lib:_inputstream.py:190": ("html5lib/_inputstream.py", 190, "HTMLUnicodeInputStream.__init__"),
            "html5lib:_inputstream.py:265": ("html5lib/_inputstream.py", 265, "HTMLUnicodeInputStream.readChunk"),
            "html5lib:html5parser.py:137": ("html5lib/html5parser.py", 137, "HTMLParser._initialize_parsing_context"),
            "html5lib:html5parser.py:217": ("html5lib/html5parser.py", 217, "HTMLParser._ensure_handlers_initialized"),
            "html5lib:html5parser.py:219": ("html5lib/html5parser.py", 219, "HTMLParser._ensure_handlers_initialized"),
            "html5lib:html5parser.py:229": ("html5lib/html5parser.py", 229, "HTMLParser._validate_integrity"),
            "html5lib:html5parser.py:232": ("html5lib/html5parser.py", 232, "HTMLParser._validate_integrity"),
            "html5lib:html5parser.py:250": ("html5lib/html5parser.py", 250, "HTMLParser.mainLoop"),
            "html5lib:html5parser.py:262": ("html5lib/html5parser.py", 262, "HTMLParser.mainLoop"),
            "html5lib:html5parser.py:266": ("html5lib/html5parser.py", 266, "HTMLParser.mainLoop"),
            "html5lib:html5parser.py:279": ("html5lib/html5parser.py", 279, "HTMLParser.mainLoop"),
            "html5lib:html5parser.py:1337": ("html5lib/html5parser.py", 1337, "InBodyPhase.startTagTemplate"),
            "html5lib:html5parser.py:1339": ("html5lib/html5parser.py", 1339, "InBodyPhase.startTagTemplate"),
            "html5lib:treebuilders:base.py:334": ("html5lib/treebuilders/base.py", 334, "TreeBuilder.insertShadowRoot"),
        },
    },
    "rich": {
        "easy": {
            "rich:markdown.py:175": ("rich/markdown.py", 175, "DispatchManager.__init__"),
            "rich:markdown.py:177": ("rich/markdown.py", 177, "DispatchManager.__init__"),
            "rich:markdown.py:187": ("rich/markdown.py", 187, "DispatchManager.get_handler"),
        },
        "unreachable": {
            "rich:accordion.py:8": ("rich/accordion.py", 8, "Accordion.__init__"),
            "rich:accordion.py:12": ("rich/accordion.py", 12, "Accordion.__rich_console__"),
            "rich:accordion.py:15": ("rich/accordion.py", 15, "Accordion.__rich_console__"),
            "rich:gradient.py:10": ("rich/gradient.py", 10, "Gradient.__init__"),
            "rich:gradient.py:15": ("rich/gradient.py", 15, "Gradient.__rich_console__"),
            "rich:gradient.py:20": ("rich/gradient.py", 20, "Gradient.__rich_console__"),
            "rich:gradient.py:30": ("rich/gradient.py", 30, "Gradient.__rich_console__"),
            "rich:gradient.py:32": ("rich/gradient.py", 32, "Gradient.__rich_console__"),
            "rich:markdown.py:185": ("rich/markdown.py", 185, "DispatchManager.get_handler"),
            "rich:markdown.py:191": ("rich/markdown.py", 191, "DispatchManager._handle_sparkline"),
            "rich:markdown.py:200": ("rich/markdown.py", 200, "DispatchManager._parse_sparkline_data"),
            "rich:markdown.py:202": ("rich/markdown.py", 202, "DispatchManager._parse_sparkline_data"),
            "rich:markdown.py:206": ("rich/markdown.py", 206, "DispatchManager._parse_sparkline_data"),
            "rich:markdown.py:208": ("rich/markdown.py", 208, "DispatchManager._parse_sparkline_data"),
            "rich:markdown.py:211": ("rich/markdown.py", 211, "DispatchManager._parse_sparkline_data"),
            "rich:markdown.py:215": ("rich/markdown.py", 215, "DispatchManager._parse_sparkline_data"),
            "rich:markdown.py:222": ("rich/markdown.py", 222, "DispatchManager._handle_gradient"),
            "rich:markdown.py:239": ("rich/markdown.py", 239, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:241": ("rich/markdown.py", 241, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:246": ("rich/markdown.py", 246, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:248": ("rich/markdown.py", 248, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:250": ("rich/markdown.py", 250, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:253": ("rich/markdown.py", 253, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:254": ("rich/markdown.py", 254, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:258": ("rich/markdown.py", 258, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:261": ("rich/markdown.py", 261, "DispatchManager._parse_gradient_colors"),
            "rich:markdown.py:268": ("rich/markdown.py", 268, "DispatchManager._handle_accordion"),
            "rich:markdown.py:272": ("rich/markdown.py", 272, "DispatchManager._handle_accordion"),
        },
    },
}


# ---------------------------------------------------------------------------
# Patch paths for VIBE targets: <vibe_patch_dir>/{project}/patches/{feature,overall}.patch
# (the vibe-coded features; not shipped -- every vibe target's target.patch already includes its feature)
# ---------------------------------------------------------------------------

def get_vibe_patch_path(vibe_patch_dir: Optional[str], project_name: str, target_set: str) -> Optional[str]:
    """
    Returns the patch file path for a given project and target set,
    or None if the target set does not require a pre-patch.
    """
    if target_set == "vibe_feature_only":
        patch_kind = "feature"
    elif target_set == "vibe_overall":
        patch_kind = "overall"
    else:
        return None
    if not vibe_patch_dir:
        logger.error(f"--target_set {target_set} needs --vibe_patch_dir")
        sys.exit(-1)
    return os.path.join(vibe_patch_dir, project_name, "patches", f"{patch_kind}.patch")
 
 
def apply_patch(project_dir: str, patch_path: str):
    """Applies a git patch file to the project directory."""
    if not os.path.exists(patch_path):
        logger.error(f"Patch file not found: {patch_path}")
        sys.exit(-1)
 
    logger.info(f"Applying patch: {patch_path}")
    try:
        subprocess.run(
            ["git", "apply", "--whitespace=nowarn", patch_path],
            cwd=project_dir,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        logger.error(f"Failed to apply patch {patch_path}\nstderr: {e.stderr}")
        sys.exit(-1)
    logger.info("Patch applied successfully.")
 
 
def get_encoding():
    """Returns the tokenizer encoding (cl100k_base for GPT-4/3.5)."""
    return tiktoken.get_encoding("cl100k_base")
 
 
def run_command(command: List[str], cwd: str = None):
    """Runs a shell command."""
    try:
        subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as e:
        logger.error(f"Command failed: {' '.join(command)}\nError: {e.stderr}")
        raise
 
 
def setup_repository(base_dir: str, project_name: str, info: Dict[str, str]) -> str:
    """Clones the repo and resets to the specific commit."""
    project_dir = os.path.join(base_dir, project_name)
 
    if not os.path.exists(project_dir):
        logger.info(f"Cloning {project_name}...")
        run_command(["git", "clone", info["github_link"], project_dir])
 
    logger.info(f"Resetting {project_name} to {info['commit_hash']}...")
    run_command(["git", "clean", "-fdx"], cwd=project_dir)
    run_command(["git", "reset", "--hard", info["commit_hash"]], cwd=project_dir)
 
    return project_dir
 
 
def get_target_context(project_dir: str, rel_path: str, line_num: int, func_name: str, window: int = 20) -> str:
    """
    Reads the target file and extracts a window of code around the target line.
    Returns a query string: Filename + Function + Snippet.
 
    Must be called on the CLEAN file before instrument_target() modifies it,
    to avoid line number drift from inserted import statements.
    """
    file_path = os.path.join(project_dir, rel_path)
 
    if not os.path.exists(file_path):
        logger.warning(f"Target file not found: {file_path}")
        return f"{rel_path} {func_name}"
 
    try:
        with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
 
        start_idx = max(0, line_num - 1 - window)
        end_idx = min(len(lines), line_num - 1 + window)
 
        snippet = "".join(lines[start_idx:end_idx])
        query = f"File: {rel_path}\nFunction: {func_name}\nContext:\n{snippet}"
        return query
    except Exception as e:
        logger.error(f"Error reading target context: {e}")
        return f"{rel_path} {func_name}"
 
 
def read_file_content(filepath: str) -> str:
    """Reads file content with error handling."""
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except Exception as e:
        logger.warning(f"Failed to read {filepath}: {e}")
        return ""
 
 
def collect_files(project_dir: str) -> List[Dict[str, str]]:
    """Traverses the directory and returns a list of file dictionaries."""
    documents = []
 
    for root, dirs, files in os.walk(project_dir):
        dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]
 
        for filename in files:
            if filename.endswith(VALID_EXTENSIONS):
                filepath = os.path.join(root, filename)
                content = read_file_content(filepath)
                if not content:
                    continue
 
                full_text_for_index = f"{content}"
                documents.append({
                    "path": filepath,
                    "content": content,
                    "full_text_for_index": full_text_for_index
                })
 
    return documents
 
 
def build_bm25_index(documents: List[Dict[str, str]]) -> Tuple[bm25s.BM25, Any]:
    """
    Tokenizes the corpus and builds a BM25 index.
 
    Separated from retrieval so the index can be built ONCE per project
    in main() and reused across all targets, rather than rebuilt per target.
    This reduces indexing time from O(N * T) to O(N) where T is the number
    of targets — critical for large repos like CPython or Linux.
    """
    corpus_texts = [doc['full_text_for_index'] for doc in documents]
    corpus_tokens = bm25s.tokenize(corpus_texts)
    retriever = bm25s.BM25()
    retriever.index(corpus_tokens)
    return retriever, corpus_tokens
 
 
def retrieve_and_filter(
    documents: List[Dict[str, str]],
    retriever: bm25s.BM25,
    query: str,
    token_limit: int = 100000
) -> List[Dict[str, Any]]:
    """
    Ranks pre-indexed documents by BM25 relevance to query, then greedily
    fills the token budget. The last file is truncated at a newline boundary
    rather than mid-token to avoid splitting code mid-statement.
    """
    query_tokens = bm25s.tokenize(query)
    results, scores = retriever.retrieve(query_tokens, k=len(documents))
 
    enc = get_encoding()
    current_token_count = 0
    selected_files = []
    ranked_indices = results[0]
 
    for rank, doc_idx in enumerate(ranked_indices):
        doc = documents[doc_idx]
        file_path = doc['path']
        
        # Read current content from disk to ensure we capture the instrumented 
        # version of the target file, while keeping other files clean.
        content = read_file_content(file_path)
 
        tokens = enc.encode(content)
        num_tokens = len(tokens)
 
        if current_token_count + num_tokens <= token_limit:
            selected_files.append({
                "path": file_path,
                "content": content,
                "tokens": num_tokens,
                "score": scores[0, rank]
            })
            current_token_count += num_tokens
        else:
            remaining_budget = token_limit - current_token_count
            if remaining_budget > 0:
                # Truncate at nearest newline rather than mid-token so we don't
                # split in the middle of a word or statement.
                truncated_tokens = tokens[:remaining_budget]
                truncated_text = enc.decode(truncated_tokens)
                last_newline = truncated_text.rfind('\n')
                if last_newline != -1:
                    truncated_text = truncated_text[:last_newline + 1]
                selected_files.append({
                    "path": file_path,
                    "content": truncated_text,
                    "tokens": len(enc.encode(truncated_text)),
                    "score": scores[0, rank],
                    "truncated": True
                })
                current_token_count += remaining_budget
            break
 
    return selected_files
 
 
def save_results(results: List[Dict], output_dir: str, project_dir: str):
    """Saves retrieved files to the output directory with flattened names."""
    os.makedirs(output_dir, exist_ok=True)
 
    for res in results:
        try:
            rel_path = os.path.relpath(res['path'], project_dir)
        except ValueError:
            rel_path = os.path.basename(res['path'])
 
        # Use ':' as separator instead of '_'. 
        safe_filename = rel_path.replace(os.sep, ":").replace('/', ':').replace('\\', ':')
 
        output_path = os.path.join(output_dir, safe_filename)
 
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(res['content'])
 
 
# ---------------------------------------------------------------------------
# Instrumentation helpers
# ---------------------------------------------------------------------------
 
def check_global_var_target(lines: List[str], line_index: int) -> bool:
    """
    Returns True if the line at line_index looks like a global variable
    declaration (no leading whitespace, ends with ';', no parentheses).
    """
    if line_index < 0 or line_index >= len(lines):
        return False
    line = lines[line_index]
    stripped = line.strip()
    if not stripped or stripped.startswith('#') or stripped.startswith('//'):
        return False
    has_no_indent = len(line) == len(line.lstrip())
    looks_like_decl = stripped.endswith(';') and '(' not in stripped
    return has_no_indent and looks_like_decl
 
 
def instrument_target(file_path: str, line_number: int) -> Optional[str]:
    """
    Inserts a 'HIT TARGET' print + exit() just before the target line.
    Returns the original target line string, or None on unsupported file type.
 
    IMPORTANT: Call get_target_context() BEFORE this function. The injected
    import lines shift physical line numbers, which would corrupt the context
    window if get_target_context() ran on the modified file.
    """
    original_target_line_str = None
    print("Instrumenting target...")
 
    _, ext = os.path.splitext(file_path)
    if ext not in ['.c', '.cc', '.cpp', '.h', '.hpp', '.py']:
        print(f"Unsupported file type: {ext}")
        return None
 
    if ext in ['.c', '.h']:
        comment_symbol = "//"
        target_line_str = 'fprintf(stderr, "HIT TARGET\\n"); '
        import_str = "#include <stdio.h>\n#include <stdlib.h>\n"
        exit_str = "exit(0);\n"
    elif ext in ['.cc', '.cpp', '.hpp']:
        comment_symbol = "//"
        target_line_str = 'fprintf(stderr, "HIT TARGET\\n"); '
        import_str = "#include <stdio.h>\n#include <cstdlib>\n"
        exit_str = "exit(0);\n"
    elif ext == '.py':
        comment_symbol = "#"
        target_line_str = 'print("HIT TARGET")'
        import_str = None
        exit_str = "exit(0)\n"
    else:
        print(f"Unsupported file type: {ext}")
        return None
 
    target_line_str += f" {comment_symbol} target\n"
 
    try:
        with open(file_path, 'r') as file:
            lines = file.readlines()
 
        if line_number < 1 or line_number > len(lines) + 1:
            raise Exception(f"Line number {line_number} is out of range for file {file_path}")
 
        if check_global_var_target(lines, line_number - 1):
            raise Exception("Target line is a global variable declaration, handling it manually")
 
        # Determine indentation from the target line, or fall back to previous line
        if line_number - 1 < len(lines) and lines[line_number - 1].strip():
            indentation = lines[line_number - 1][:len(lines[line_number - 1]) - len(lines[line_number - 1].lstrip())]
        elif line_number - 2 >= 0:
            indentation = lines[line_number - 2][:len(lines[line_number - 2]) - len(lines[line_number - 2].lstrip())]
        else:
            indentation = ''
 
        def func_def_end(line):
            if '):' in line:
                return True
            if ')' in line and '->' in line and ':' in line:
                return True
            return False
 
        # Handle Python function definitions with a safe bounds check instead
        # of a bare assert — avoids IndexError when the function is at EOF.
        if ext == '.py' and "def " in lines[line_number - 1]:
            logger.info("Target line is a function definition, handling it differently")
            insert_line_number = line_number - 1
            while insert_line_number < len(lines) and not func_def_end(lines[insert_line_number]):
                insert_line_number += 1
            insert_line_number += 1
            if insert_line_number >= len(lines):
                raise Exception(
                    f"Function definition at line {line_number} of {file_path} "
                    "extends to end of file; cannot safely insert after it."
                )
            indentation = lines[insert_line_number][:len(lines[insert_line_number]) - len(lines[insert_line_number].lstrip())]
            line_number = insert_line_number + 1
 
        target_line_str = indentation + target_line_str + indentation + exit_str
        original_target_line_str = lines[line_number - 1] + target_line_str
        print(f"Original target line: {original_target_line_str}")
 
        lines.insert(line_number - 1, target_line_str)
        if import_str is not None:
            lines.insert(0, import_str)
 
        with open(file_path, 'w') as file:
            file.writelines(lines)
 
    except Exception as e:
        print(f"Error instrumenting target: {e}")
        sys.exit(-1)
 
    logger.info(f"Target instrumented at {file_path}:{line_number}")
    return original_target_line_str
 
 
def generate_patch(project_dir: str, data_dir: str):
    """Runs `git diff` in project_dir and writes the patch to data_dir/target.patch."""
    print("Generating patch...")
    os.makedirs(data_dir, exist_ok=True)
    patch_path = os.path.join(data_dir, "target.patch")
    try:
        # Use a context manager so the file handle is always closed — prevents
        # "Too many open files" OSError on large benchmark runs.
        with open(patch_path, "w") as patch_file:
            subprocess.run(
                ["git", "-C", project_dir, "diff"],
                check=True,
                stdout=patch_file,
                stderr=sys.stderr
            )
    except subprocess.CalledProcessError as e:
        print(f"Error generating patch: {e}")
        return
    logger.info(f"Patch generated: {patch_path}")
 
 
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# ARVO support
# ---------------------------------------------------------------------------

def load_arvo_targets(json_path: str) -> Dict[str, List[Dict]]:
    """
    Reads an ARVO benchmark.json and groups entries by (project_name, commit_id)
    so the BM25 index can be built once per unique commit.

    Returns::

        {
            "serenity": [
                {
                    "repo_link": "https://...",
                    "commit_id": "abc123...",
                    "targets": {
                        "42490094": ("Libraries/LibRegex/RegexLexer.cpp", 69,
                                     "regex::Lexer::back(unsigned long)",
                                     ["call_path_string", ...]),
                        ...
                    }
                },
                ...
            ],
            ...
        }
    """
    with open(json_path, "r", encoding="utf-8") as f:
        entries = json.load(f)

    # Group by (project_name, commit_id)
    groups: Dict[str, Dict[str, Dict]] = defaultdict(dict)  # project -> {commit_id -> group}

    for entry in entries:
        project = entry["project_name"]
        commit = entry["commit_id"]
        cve_id = str(entry["cve_id"])

        # Parse target_line  e.g. "Libraries/LibRegex/RegexLexer.cpp:69"
        last_colon = entry["target_line"].rfind(":")
        rel_path = os.path.normpath(entry["target_line"][:last_colon])
        line_num = int(entry["target_line"][last_colon + 1:])

        # Strip absolute Docker-style paths like /src/<project>/...
        if rel_path.startswith("/"):
            parts = rel_path.strip("/").split("/")
            # Skip /src/<project_name>/ prefix
            if len(parts) > 2 and parts[0] == "src":
                rel_path = os.path.join(*parts[2:])
            else:
                rel_path = os.path.join(*parts[1:]) if len(parts) > 1 else parts[0]
        func_name = entry["target_function"]
        call_paths = entry.get("call_paths", [])

        if commit not in groups[project]:
            groups[project][commit] = {
                "repo_link": entry["repo_link"],
                "commit_id": commit,
                "targets": {},
            }
        groups[project][commit]["targets"][cve_id] = (rel_path, line_num, func_name, call_paths)

    # Convert inner dict to list
    result: Dict[str, List[Dict]] = {}
    for project, commit_dict in groups.items():
        result[project] = list(commit_dict.values())

    return result


def parse_call_path_files(call_paths: List[str]) -> List[str]:
    """
    Extracts unique source file paths from call_path strings.

    Each call_path looks like:
        "FuzzShell.cpp::LLVMFuzzerTestOneInput -> Parser -> Shell/Parser.cpp::parse_toplevel -> ..."

    Returns a deduplicated, order-preserving list of file paths.
    """
    seen = set()
    files = []
    for path_str in call_paths:
        nodes = path_str.split(" -> ")
        for node in nodes:
            # Take part before '::' (if present) as the file path
            file_part = node.split("::")[0].strip()
            # Must look like a file path (has an extension)
            if "." in file_part and file_part not in seen:
                seen.add(file_part)
                files.append(file_part)
    return files


def retrieve_realistic(
    documents: List[Dict[str, str]],
    retriever: bm25s.BM25,
    query: str,
    call_paths: List[str],
    project_dir: str,
    token_limit: int = 100000,
) -> List[Dict[str, Any]]:
    """
    Call-path-aware retrieval: include files from call_paths first,
    then backfill with BM25-ranked files up to token_limit.
    If call_paths is empty, falls back to pure BM25.
    """
    if not call_paths:
        return retrieve_and_filter(documents, retriever, query, token_limit)

    enc = get_encoding()
    selected_files = []
    selected_paths = set()
    current_tokens = 0

    # Phase 1: Add call-path files in order
    call_path_files = parse_call_path_files(call_paths)
    for cp_file in call_path_files:
        # Find matching document (path ends with the call-path file)
        for doc in documents:
            rel = os.path.relpath(doc["path"], project_dir)
            if rel == cp_file or doc["path"].endswith(os.sep + cp_file) or doc["path"].endswith("/" + cp_file):
                content = read_file_content(doc["path"])
                tokens = enc.encode(content)
                num_tokens = len(tokens)

                if current_tokens + num_tokens <= token_limit:
                    selected_files.append({
                        "path": doc["path"],
                        "content": content,
                        "tokens": num_tokens,
                        "score": 0.0,
                    })
                    selected_paths.add(doc["path"])
                    current_tokens += num_tokens
                else:
                    # Truncate at newline boundary
                    remaining = token_limit - current_tokens
                    if remaining > 0:
                        truncated_text = enc.decode(tokens[:remaining])
                        last_nl = truncated_text.rfind("\n")
                        if last_nl != -1:
                            truncated_text = truncated_text[:last_nl + 1]
                        selected_files.append({
                            "path": doc["path"],
                            "content": truncated_text,
                            "tokens": len(enc.encode(truncated_text)),
                            "score": 0.0,
                            "truncated": True,
                        })
                        selected_paths.add(doc["path"])
                        current_tokens = token_limit
                    return selected_files
                break  # found the matching doc, move to next call-path file

    # Phase 2: Backfill with BM25
    if current_tokens < token_limit:
        query_tokens = bm25s.tokenize(query)
        results, scores = retriever.retrieve(query_tokens, k=len(documents))
        ranked_indices = results[0]

        for rank, doc_idx in enumerate(ranked_indices):
            doc = documents[doc_idx]
            if doc["path"] in selected_paths:
                continue

            content = read_file_content(doc["path"])
            tokens = enc.encode(content)
            num_tokens = len(tokens)

            if current_tokens + num_tokens <= token_limit:
                selected_files.append({
                    "path": doc["path"],
                    "content": content,
                    "tokens": num_tokens,
                    "score": scores[0, rank],
                })
                current_tokens += num_tokens
            else:
                remaining = token_limit - current_tokens
                if remaining > 0:
                    truncated_text = enc.decode(tokens[:remaining])
                    last_nl = truncated_text.rfind("\n")
                    if last_nl != -1:
                        truncated_text = truncated_text[:last_nl + 1]
                    selected_files.append({
                        "path": doc["path"],
                        "content": truncated_text,
                        "tokens": len(enc.encode(truncated_text)),
                        "score": scores[0, rank],
                        "truncated": True,
                    })
                break

    return selected_files


def process_arvo(arvo_json: str, base_work_dir: str, output_root: str):
    """Process all ARVO CVEs: build bm25/, realistic/, and oracle/ for each."""
    arvo_data = load_arvo_targets(arvo_json)

    for project_name, commit_groups in arvo_data.items():
        logger.info(f"Processing ARVO project: {project_name}")

        # Clone once
        first_group = commit_groups[0]
        project_dir = os.path.join(base_work_dir, project_name)
        if not os.path.exists(project_dir):
            logger.info(f"  Cloning {project_name}...")
            run_command(["git", "clone", first_group["repo_link"], project_dir])

        for group in commit_groups:
            commit_id = group["commit_id"]
            logger.info(f"  Processing commit {commit_id[:12]}...")

            # Reset to this commit and build index
            run_command(["git", "clean", "-fdx"], cwd=project_dir)
            run_command(["git", "reset", "--hard", commit_id], cwd=project_dir)

            logger.info(f"    Building BM25 index...")
            documents = collect_files(project_dir)
            retriever, _ = build_bm25_index(documents)
            logger.info(f"    Indexed {len(documents)} files.")

            for cve_id, target_info in group["targets"].items():
                rel_path, line_num, func_name, call_paths = target_info
                logger.info(f"    CVE {cve_id}: {rel_path}:{line_num} ({func_name})")

                # Reset to clean state (undo previous instrumentation)
                run_command(["git", "reset", "--hard", commit_id], cwd=project_dir)
                run_command(["git", "clean", "-fd"], cwd=project_dir)

                # Step 1: Extract query BEFORE instrumentation
                query = get_target_context(project_dir, rel_path, line_num, func_name)

                # Step 2: Instrument + generate patch
                abs_target = os.path.join(project_dir, rel_path)
                original_line = instrument_target(abs_target, line_num)
                logger.info(f"      Original target line: {original_line!r}")

                # Generate patch to a temp location, then copy into each dir
                bm25_dir = os.path.join(output_root, project_name, "bm25", cve_id)
                generate_patch(project_dir, bm25_dir)

                # Step 3: BM25 retrieval
                bm25_files = retrieve_and_filter(documents, retriever, query, token_limit=100000)
                save_results(bm25_files, bm25_dir, project_dir)
                logger.info(f"      BM25: saved {len(bm25_files)} files to {bm25_dir}")

                # Step 4: Realistic retrieval
                realistic_dir = os.path.join(output_root, project_name, "realistic", cve_id)
                realistic_files = retrieve_realistic(
                    documents, retriever, query, call_paths, project_dir, token_limit=100000
                )
                save_results(realistic_files, realistic_dir, project_dir)
                # Copy target.patch from bm25 dir
                os.makedirs(realistic_dir, exist_ok=True)
                shutil.copy2(os.path.join(bm25_dir, "target.patch"),
                             os.path.join(realistic_dir, "target.patch"))
                logger.info(f"      Realistic: saved {len(realistic_files)} files to {realistic_dir}")

                # Step 5: Oracle = copy of realistic
                oracle_dir = os.path.join(output_root, project_name, "oracle", cve_id)
                if os.path.exists(oracle_dir):
                    shutil.rmtree(oracle_dir)
                shutil.copytree(realistic_dir, oracle_dir)
                logger.info(f"      Oracle: copied to {oracle_dir}")

    print(f"\nARVO processing completed. Data saved to {os.path.abspath(output_root)}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

TARGET_SETS = {
    "default": TARGETS,
    "vibe_feature_only": VIBE_FEATURE_ONLY_TARGETS,
    "vibe_overall": VIBE_OVERALL_TARGETS,
}



def main():
    parser = argparse.ArgumentParser(description="End-to-End BM25 Retrieval for Benchmarks")
    parser.add_argument("--base_work_dir", type=str, default=os.path.join(RESULTS_DIR, "work_dir"),
                        help="Working directory for cloning repos (default: static/results/retrieve_context/work_dir)")
    parser.add_argument("--output_root", type=str, default=os.path.join(RESULTS_DIR, "benchmark_data"),
                        help="Root directory for saving output (default: static/results/retrieve_context/benchmark_data)")
    parser.add_argument(
        "--target_set", type=str, default="default",
        choices=list(TARGET_SETS.keys()),
        help="Which target dictionary to run: default, vibe_feature_only, or vibe_overall",
    )
    parser.add_argument(
        "--arvo", type=str, default=None,
        help="Path to ARVO benchmark.json. When provided, processes ARVO CVEs instead of the default targets.",
    )
    parser.add_argument(
        "--vibe_patch_dir", type=str, default=os.environ.get("DFUZZ_VIBE_PATCH_DIR"),
        help="With --target_set vibe_*: the dir holding <project>/patches/{feature,overall}.patch",
    )
    args = parser.parse_args()

    if args.arvo:
        process_arvo(args.arvo, args.base_work_dir, args.output_root)
        return

    active_targets = TARGET_SETS[args.target_set]
 
    for project_name, difficulties in active_targets.items():
        if project_name not in PROJECT_INFO:
            logger.warning(f"No project info found for {project_name}, skipping.")
            continue
 
        logger.info(f"Processing project: {project_name}")
        project_dir = setup_repository(args.base_work_dir, project_name, PROJECT_INFO[project_name])
 
        # For VIBE target sets, apply the corresponding patch on top of
        # the clean commit before collecting files or instrumenting targets.
        patch_path = get_vibe_patch_path(args.vibe_patch_dir, project_name, args.target_set)
        if patch_path is not None:
            apply_patch(project_dir, patch_path)
 
        # Build the BM25 index ONCE per project — not once per target.
        # For VIBE targets this indexes the PATCHED state of the repo, so the
        # retriever sees the new/modified files introduced by the patch.
        logger.info(f"  Building BM25 index for {project_name}...")
        documents = collect_files(project_dir)
        retriever, _ = build_bm25_index(documents)
        logger.info(f"  Indexed {len(documents)} files.")
 
        for difficulty, targets in difficulties.items():
            logger.info(f"  Processing difficulty: {difficulty}")
 
            for target_key, target_info in targets.items():
                rel_path, line_num, func_name = target_info
                logger.info(f"    Target: {target_key} ({func_name}:{line_num})")
 
                # Reset repo to a clean state before touching anything.
                # This undoes the previous target's instrumentation while
                # preserving the base commit.
                run_command(["git", "reset", "--hard", "HEAD"], cwd=project_dir)
                run_command(["git", "clean", "-fd"], cwd=project_dir)
 
                # Re-apply the VIBE patch so we're back to the patched
                # (but un-instrumented) state for this target.
                if patch_path is not None:
                    apply_patch(project_dir, patch_path)
 
                # Build output directory path (shared by patch + retrieved files)
                safe_target_key = target_key.replace("/", ":").replace("\\", ":")
                target_output_dir = os.path.join(
                    args.output_root,
                    project_name,
                    difficulty,
                    safe_target_key
                )
 
                # Step 1: Extract query from the PATCHED-but-clean file
                # BEFORE instrumentation to avoid line number drift.
                query = get_target_context(project_dir, rel_path, line_num, func_name)
 
                # Step 2: Instrument the target line, then capture the patch.
                # For VIBE targets, generate_patch records the diff from the
                # base commit — this includes BOTH the pre-applied VIBE patch
                # AND the instrumentation, which is exactly what we want:
                # the target.patch must replay both on a clean checkout.
                abs_target_path = os.path.join(project_dir, rel_path)
                original_line = instrument_target(abs_target_path, line_num)
                logger.info(f"    Original target line: {original_line!r}")
                generate_patch(project_dir, target_output_dir)
 
                # Step 3: Retrieve and save context files using the pre-built index
                retrieved_files = retrieve_and_filter(documents, retriever, query, token_limit=100000)
                save_results(retrieved_files, target_output_dir, project_dir)
 
                logger.info(f"    Saved {len(retrieved_files)} files + patch to {target_output_dir}")
 
    print(f"\nAll tasks completed. Data saved to {os.path.abspath(args.output_root)}")
 
 
if __name__ == "__main__":
    main()