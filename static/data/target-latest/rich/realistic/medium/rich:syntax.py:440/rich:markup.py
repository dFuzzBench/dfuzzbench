import re
from ast import literal_eval
from operator import attrgetter
from typing import Callable, Iterable, List, Match, NamedTuple, Optional, Tuple, Union

from ._emoji_replace import _emoji_replace
from .emoji import EmojiVariant
from .errors import MarkupError
from .style import Style
from .text import Span, Text

RE_TAGS = re.compile(
    r"""((\\*)\[([a-z#/@][^[]*?)])""",
    re.VERBOSE,
)

RE_HANDLER = re.compile(r"^([\w.]*?)(\(.*?\))?$")


class Tag(NamedTuple):
    """A tag in console markup."""

    name: str
    """The tag name. e.g. 'bold'."""
    parameters: Optional[str]
    """Any additional parameters after the name."""

    def __str__(self) -> str:
        return (
            self.name if self.parameters is None else f"{self.name} {self.parameters}"
        )

    @property
    def markup(self) -> str:
        """Get the string representation of this tag."""
        return (
            f"[{self.name}]"
            if self.parameters is None
            else f"[{self.name}={self.parameters}]"
        )


_ReStringMatch = Match[str]  # regex match object
_ReSubCallable = Callable[[_ReStringMatch], str]  # Callable invoked by re.sub
_EscapeSubMethod = Callable[[_ReSubCallable, str], str]  # Sub method of a compiled re


def escape(
    markup: str,
    _escape: _EscapeSubMethod = re.compile(r"(\\*)(\[[a-z#/@][^[]*?])").sub,
) -> str:
    """Escapes text so that it won't be interpreted as markup.

    Args:
        markup (str): Content to be inserted in to markup.

    Returns:
        str: Markup with square brackets escaped.
    """

    def escape_backslashes(match: Match[str]) -> str:
        """Called by re.sub replace matches."""
        backslashes, text = match.groups()
        return f"{backslashes}{backslashes}\\{text}"

    markup = _escape(escape_backslashes, markup)
    if markup.endswith("\\") and not markup.endswith("\\\\"):
        return markup + "\\"

    return markup


def _parse(markup: str) -> Iterable[Tuple[int, Optional[str], Optional[Tag]]]:
    """Parse markup in to an iterable of tuples of (position, text, tag).

    Args:
        markup (str): A string containing console markup

    """
    position = 0
    _divmod = divmod
    _Tag = Tag
    for match in RE_TAGS.finditer(markup):
        full_text, escapes, tag_text = match.groups()
        start, end = match.span()
        if start > position:
            yield start, markup[position:start], None
        if escapes:
            backslashes, escaped = _divmod(len(escapes), 2)
            if backslashes:
                # Literal backslashes
                yield start, "\\" * backslashes, None
                start += backslashes * 2
            if escaped:
                # Escape of tag
                yield start, full_text[len(escapes) :], None
                position = end
                continue
        text, equals, parameters = tag_text.partition("=")
        yield start, None, _Tag(text, parameters if equals else None)
        position = end
    if position < len(markup):
        yield position, markup[position:], None


def render(
    markup: str,
    style: Union[str, Style] = "",
    emoji: bool = True,
    emoji_variant: Optional[EmojiVariant] = None,
) -> Text:
    """Render console markup in to a Text instance.

    Args:
        markup (str): A string containing console markup.
        style: (Union[str, Style]): The style to use.
        emoji (bool, optional): Also render emoji code. Defaults to True.
        emoji_variant (str, optional): Optional emoji variant, either "text" or "emoji". Defaults to None.


    Raises:
        MarkupError: If there is a syntax error in the markup.

    Returns:
        Text: A test instance.
    """
    emoji_replace = _emoji_replace
    if "[" not in markup:
        return Text(
            emoji_replace(markup, default_variant=emoji_variant) if emoji else markup,
            style=style,
        )
    text = Text(style=style)
    append = text.append
    normalize = Style.normalize

    style_stack: List[Tuple[int, Tag]] = []
    pop = style