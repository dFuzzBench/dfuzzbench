from copy import deepcopy
import sys
from types import ModuleType
from typing import Callable, Collection, Dict, Optional, TYPE_CHECKING, List

if TYPE_CHECKING:
    from .lark import PostLex
    from .lexer import Lexer
    from .grammar import Rule
    from typing import Union, Type
    from typing import Literal
    if sys.version_info >= (3, 10):
        from typing import TypeAlias
    else:
        from typing_extensions import TypeAlias

from .utils import Serialize
from .lexer import TerminalDef, Token

###{standalone

_ParserArgType: 'TypeAlias' = 'Literal["earley", "lalr", "cyk", "auto"]'
_LexerArgType: 'TypeAlias' = 'Union[Literal["auto", "basic", "contextual", "dynamic", "dynamic_complete"], Type[Lexer]]'
_LexerCallback = Callable[[Token], Token]
ParserCallbacks = Dict[str, Callable]

class LexerConf(Serialize):
    __serialize_fields__ = 'terminals', 'ignore', 'g_regex_flags', 'use_bytes', 'lexer_type'
    __serialize_namespace__ = TerminalDef,

    terminals: Collection[TerminalDef]
    re_module: ModuleType
    ignore: Collection[str]
    postlex: 'Optional[PostLex]'
    callbacks: Dict[str, _LexerCallback]
    g_regex_flags: int
    skip_validation: bool
    use_bytes: bool
    lexer_type: Optional[_LexerArgType]
    strict: bool

    def