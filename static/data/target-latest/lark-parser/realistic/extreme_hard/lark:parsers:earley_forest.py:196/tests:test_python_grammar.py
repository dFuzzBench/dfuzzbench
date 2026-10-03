import inspect
import textwrap
from unittest import TestCase, main

from lark import Lark
from lark.indenter import PythonIndenter
from lark.exceptions import UnexpectedCharacters, UnexpectedToken, ParseError

valid_DEC_NUMBER = [
    "0",
    "000",
    "0_0_0",
    "4_2",
    "1_0000_0000",
    "123456789012345678901234567890",
]

valid_HEX_NUMBER = [
    "