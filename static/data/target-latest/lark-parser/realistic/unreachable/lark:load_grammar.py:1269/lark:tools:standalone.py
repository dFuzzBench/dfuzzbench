

from copy import deepcopy
from abc import ABC, abstractmethod
from types import ModuleType
from typing import (
    TypeVar, Generic, Type, Tuple, List, Dict, Iterator, Collection, Callable, Optional, FrozenSet, Any,
    Union, Iterable, IO, TYPE_CHECKING, overload, Sequence,
    Pattern as REPattern, ClassVar, Set, Mapping
)
###}

import sys
import token, tokenize
import os
from os import path
from collections import defaultdict
from functools import partial
from argparse import ArgumentParser

import lark
from lark.tools import lalr_argparser, build_lalr, make_warnings_comments


from lark.grammar import Rule
from lark.lexer import TerminalDef

_dir = path.dirname(__file__)
_larkdir = path.join(_dir, path.pardir)


EXTRACT_STANDALONE_FILES = [
    'tools/standalone.py',
    'exceptions.py',
    '


