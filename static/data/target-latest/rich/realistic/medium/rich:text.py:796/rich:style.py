import sys
from functools import lru_cache
from marshal import dumps, loads
from random import randint
from typing import Any, Dict, Iterable, List, Optional, Type, Union, cast

from . import errors
from .color import Color, ColorParseError, ColorSystem, blend_rgb
from .repr import Result, rich_repr
from .terminal_theme import DEFAULT_TERMINAL_THEME, TerminalTheme

# Style instances and style definitions are often interchangeable
StyleType = Union[str, "Style"]


class _Bit:
    """A descriptor to get/set a style attribute bit."""

    __slots__ = ["bit"]

    def __init__(self, bit_no: int) -> None:
        self.bit = 1 << bit_no

    def __get__(self, obj: "Style", objtype: Type["Style"]) -> Optional[bool]:
        if obj._set_attributes & self.bit:
            return obj._attributes & self.bit != 0
        return None


@rich_repr
class Style:
    """A terminal style.

    A terminal style consists of a color (`color`), a background color (`bgcolor`), and a number of attributes, such
    as bold, italic etc. The attributes have 3 states: they can either be on
    (``True``), off (``False``), or not set (``None``).

    Args:
        color (Union[Color, str], optional): Color of terminal text. Defaults to None.
        bgcolor (Union[Color, str], optional): Color of terminal background. Defaults to None.
        bold (bool, optional): Enable bold text. Defaults to None.
        dim (bool, optional): Enable dim text. Defaults to None.
        italic (bool, optional): Enable italic text. Defaults to None.
        underline (bool, optional): Enable underlined text. Defaults to None.
        blink (bool, optional): Enabled blinking text. Defaults to None.
        blink2 (bool, optional): Enable fast blinking text. Defaults to None.
        reverse (bool, optional): Enabled reverse text. Defaults to None.
        conceal (bool, optional): Enable concealed text. Defaults to None.
        strike (bool, optional): Enable strikethrough text. Defaults to None.
        underline2 (bool, optional): Enable doubly underlined text. Defaults to None.
        frame (bool, optional): Enable framed text. Defaults to None.
        encircle (bool, optional): Enable encircled text. Defaults to None.
        overline (bool, optional): Enable overlined text. Defaults to None.
        link (str, link): Link URL. Defaults to None.

    """

    _color: Optional[Color]
    _bgcolor: Optional[Color]
    _attributes: int
    _set_attributes: int
    _hash: Optional[int]
    _null: bool
    _meta: Optional[bytes]

    __slots__ = [
        "_color",
        "_bgcolor",
        "_attributes",
        "_set_attributes",
        "_link",
        "_link_id",
        "_ansi",
        "_style_definition",
        "_hash",
        "_null",
        "_meta",
    ]

    # maps bits on to SGR parameter
    _style_map = {
        0: "1",
        1: "2",
        2: "3",
        3: "4",
        4: "5",
        5: "6",
        6: "7",
        7: "8",
        8: "9",
        9: "21",
        10: "51",
        11: "52",
        12: "53",
    }

    STYLE_ATTRIBUTES = {
        "dim": "dim",
        "d": "dim",
        "bold": "bold",
        "b": "bold",
        "italic": "italic",
        "i": "italic",
        "underline": "underline",
        "u": "underline",
        "blink": "blink",
        "blink2": "blink2",
        "reverse": "reverse",
        "r": "reverse",
        "conceal": "conceal",
        "c": "conceal",
        "strike": "strike",
        "s": "strike",
        "underline2": "underline2",
        "uu": "underline2",
        "frame": "frame",
        "encircle": "encircle