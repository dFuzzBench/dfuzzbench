import inspect
import linecache
import os
import sys
from dataclasses import dataclass, field
from itertools import islice
from traceback import walk_tb
from types import ModuleType, TracebackType
from typing import (
    Any,
    Callable,
    Dict,
    Iterable,
    List,
    Optional,
    Sequence,
    Tuple,
    Type,
    Union,
)

from pygments.lexers import guess_lexer_for_filename
from pygments.token import Comment, Keyword, Name, Number, Operator, String
from pygments.token import Text as TextToken
from pygments.token import Token
from pygments.util import ClassNotFound

from . import pretty
from ._loop import loop_first_last, loop_last
from .columns import Columns
from .console import (
    Console,
    ConsoleOptions,
    ConsoleRenderable,
    Group,
    RenderResult,
    group,
)
from .constrain import Constrain
from .highlighter import RegexHighlighter, ReprHighlighter
from .panel import Panel
from .scope import render_scope
from .style import Style
from .syntax import Syntax, SyntaxPosition
from .text import Text
from .theme import Theme

WINDOWS = sys.platform == "win32"

LOCALS_MAX_LENGTH = 10
LOCALS_MAX_STRING = 80


def _iter_syntax_lines(
    start: SyntaxPosition, end: SyntaxPosition
) -> Iterable[Tuple[int, int, int]]:
    """Yield start and end positions per line.

    Args:
        start: Start position.
        end: End position.

    Returns:
        Iterable of (LINE, COLUMN1, COLUMN2).
    """

    line1, column1 = start
    line2, column2 = end

    if line1 == line2:
        yield line1, column1, column2
    else:
        for first, last, line_no in loop_first_last(range(line1, line2 + 1)):
            if first:
                yield line_no, column1, -1
            elif last:
                yield line_no, 0, column2
            else:
                yield line_no, 0, -1


def install(
    *,
    console: Optional[Console] = None,
    width: Optional[int] = 100,
    code_width: Optional[int] = 88,
    extra_lines: int = 3,
    theme: Optional[str] = None,
    word_wrap: bool = False,
    show_locals: bool = False,
    locals_max_length: int = LOCALS_MAX_LENGTH,
    locals_max_string: int = LOCALS_MAX_STRING,
    locals_hide_dunder: bool = True,
    locals_hide_sunder: Optional[bool] = None,
    indent_guides: bool = True,
    suppress: Iterable[Union[str, ModuleType]] = (),
    max_frames: int = 100,
) -> Callable[[Type[BaseException], BaseException, Optional[TracebackType]], Any]:
    """Install a rich traceback handler.

    Once installed, any tracebacks will be printed with syntax highlighting and rich formatting.


    Args:
        console (Optional[Console], optional): Console to write exception to. Default uses internal Console instance.
        width (Optional[int], optional): Width (in characters) of traceback. Defaults to 100.
        code_width (Optional[int], optional): Code width (in characters) of traceback. Defaults to 88.
        extra_lines (int, optional): Extra lines of code. Defaults to 3.
        theme (Optional[str], optional): Pygments theme to use in traceback. Defaults to ``None`` which will pick
            a theme appropriate for the platform.
        word_wrap (bool, optional): Enable word wrapping of long lines. Defaults to False.
        show_locals (bool, optional): Enable display of local variables. Defaults to False.
        locals_max_length (int, optional): Maximum length of containers before abbreviating, or None for no abbreviation.
            Defaults to 10.
        locals_max_string (int, optional): Maximum length of string before truncating, or None to disable. Defaults to 80.
        locals_hide_dunder (bool, optional): Hide locals prefixed with double underscore. Defaults to True.
        locals_hide_sunder (bool, optional): Hide locals prefixed with single underscore. Defaults to False.
        indent_guides (bool, optional): Enable indent guides in code and locals. Defaults to True.
        suppress (Sequence[Union[str, ModuleType]]): Optional sequence of modules or paths to exclude from traceback.

    Returns:
        Callable: The previous exception handler that was replaced.

    """
    traceback_console = Console(stderr=True) if console is None else console

    locals_hide_sunder = (
        True
        if (traceback_console.is_jupyter and locals_hide_sunder is None)
        else locals_hide_sunder
    )

    def excepthook(
        type_: Type[BaseException],
        value: BaseException,
        traceback: Optional[TracebackType],
    ) -> None:
        exception_traceback = Traceback.from_exception(
            type_,
            value,
            traceback,
            width=width,
            code_width=code_width,
            extra_lines=extra_lines,
            theme=theme,
            word_wrap=word_wrap,
            show_locals=show_locals,
            locals_max_length=locals_max_length,
            locals_max_string=locals_max_string,
            locals_hide_dunder=locals_hide_dunder,
            locals_hide_sunder=bool(locals_hide_sunder),
            indent_guides=indent_guides,
            suppress=suppress,
            max_frames=max_frames,
        )
        traceback_console.print(exception_traceback)

    def ipy_excepthook_closure(ip: Any) -> None:  # pragma: no cover
        tb_data = {}  # store information about showtraceback call
        default_showtraceback = ip.showtraceback  # keep reference of default traceback

        def ipy_show_traceback(*args: Any, **kwargs: Any) -> None:
            """wrap the default ip.showtraceback to store info for ip._showtraceback"""
            nonlocal tb_data
            tb_data = kwargs
            default_showtraceback(*args, **kwargs)

        def ipy_display_traceback(
            *args: Any, is_syntax: bool = False, **kwargs: Any
        ) -> None:
            """Internally called traceback from ip._showtraceback"""
            nonlocal tb_data
            exc_tuple = ip._get_exc_info()

            # do not display trace on syntax error
            tb: Optional[TracebackType] = None if is_syntax else exc_tuple[2]

            # determine correct tb_offset
            compiled = tb_data.get("running_compiled_code", False)
            tb_offset = tb_data.get("tb_offset", 1 if compiled else 0)
            # remove ipython internal frames from trace with tb_offset
            for _ in range(tb_offset):
                if tb is None:
                    break
                tb = tb.tb_next

            excepthook(exc_tuple[0], exc_tuple[1], tb)
            tb_data = {}  # clear data upon usage

        # replace _showtraceback instead of showtraceback to allow ipython features such as debugging to work
        # this is also what the ipython docs recommends to modify when subclassing InteractiveShell
        ip._showtraceback = ipy_display_traceback
        # add wrapper to capture tb_data
        ip.showtraceback = ipy_show_traceback
        ip.showsyntaxerror = lambda *args, **kwargs: ipy_display_traceback(
            *args, is_syntax=True, **kwargs
        )

    try:  # pragma: no cover
        # if within ipython, use customized traceback
        ip = get_ipython()  # type: ignore[name-defined]
        ipy_excepthook_closure(ip)
        return sys.excepthook
    except Exception:
        # otherwise use default system hook
        old_excepthook = sys.excepthook
        sys.excepthook = excepthook
        return old_excepthook


@dataclass
class Frame:
    filename: str
    lineno: int
    name: str
    line: str = ""
    locals: Optional[Dict[str, pretty.Node]] = None
    last_instruction: Optional[Tuple[Tuple[int, int], Tuple[int, int]]] = None


@dataclass
class _SyntaxError:
    offset: int
    filename: str
    line: str
    lineno: int
    msg: str
    notes: List[str] = field(default_factory=list)


@dataclass
class Stack:
    exc_type: str
    exc_value: str
    syntax_error: Optional[_SyntaxError] = None
    is_cause: bool = False
    frames: List[Frame] = field(default_factory=list)
    notes: List[str] = field(default_factory=list)
    is_group: bool = False
    exceptions: List["Trace"] = field(default_factory=list)


@dataclass
class Trace:
    stacks: List[Stack]


class PathHighlighter(RegexHighlighter):
    highlights = [r"(?P<dim>.*/)(?P<bold>.+)"]


class Traceback:
    """A Console renderable that renders a traceback.

    Args:
        trace (Trace, optional): A `Trace` object produced from `extract`. Defaults to None, which uses
            the last exception.
        width (Optional[int], optional): Number of characters used to traceback. Defaults to 100.
        code_width (Optional[int], optional): Number of code characters used to traceback. Defaults to 88.
        extra_lines (int, optional): Additional lines of code to render. Defaults to 3.
        theme (str, optional): Override pygments theme used in traceback.
        word_wrap (bool, optional): Enable word wrapping of long lines. Defaults to False.
        show_locals (bool, optional): Enable display of local variables. Defaults to False.
        indent_guides (bool, optional): Enable indent guides in code and locals. Defaults to True.
        locals_max_length (int, optional): Maximum length of containers before abbreviating, or None for no abbreviation.
            Defaults to 10.
        locals_max_string (int, optional): Maximum length of string before truncating, or None to disable. Defaults to 80.
        locals_hide_dunder (bool, optional): Hide locals prefixed with double underscore. Defaults to True.
        locals_hide_sunder (bool, optional): Hide locals prefixed with single underscore. Defaults to False.
        suppress (Sequence[Union[str, ModuleType]]): Optional sequence of modules or paths to exclude from traceback.
        max_frames (int): Maximum number of frames to show in a traceback, 0 for no maximum. Defaults to 100.

    """

    LEXERS = {
        "": "text",
        ".py": "python",
        ".pxd": "cython",
        ".pyx": "cython",
        ".pxi": "pyrex",
    }

    def __init__(
        self,
        trace: Optional[Trace] = None,
        *,
        width: Optional[int] = 100,
        code_width: Optional[int] = 88,
        extra_lines: int = 3,
        theme: Optional[str] = None,
        word_wrap: bool = False,
        show_locals: bool = False,
        locals_max_length: int = LOCALS_MAX_LENGTH,
        locals_max_string: int = LOCALS_MAX_STRING,
        locals_hide_dunder: bool = True,
        locals_hide_sunder: bool = False,
        indent_guides: bool = True,
        suppress: Iterable[Union[str, ModuleType]] = (),
        max_frames: int = 100,
    ):
        if trace is None:
            exc_type, exc_value, traceback = sys.exc_info()
            if exc_type is None or exc_value is None or traceback is None:
                raise ValueError(
                    "Value for 'trace' required if not called in except: block"
                )
            trace = self.extract(
                exc_type, exc_value, traceback, show_locals=show_locals
            )
        self.trace = trace
        self.width = width
        self.code_width = code_width
        self.extra_lines = extra_lines
        self.theme = Syntax.get_theme(theme or "ansi_dark")
        self.word_wrap = word_wrap
        self.show_locals = show_locals
        self.indent_guides = indent_guides
        self.locals_max_length = locals_max_length
        self.locals_max_string = locals_max_string
        self.locals_hide_dunder = locals_hide_dunder
        self.locals_hide_sunder = locals_hide_sunder

        self.suppress: Sequence[str] = []
        for suppress_entity in suppress:
            if not isinstance(suppress_entity, str):
                assert (
                    suppress_entity.__file__ is not None
                ), f"{suppress_entity!r} must be a module with '__file__' attribute"
                path = os.path.dirname(suppress_entity.__file__)
            else:
                path = suppress_entity
            path = os.path.normpath(os.path.abspath(path))
            self.suppress.append(path)
        self.max_frames = max(4, max_frames) if max_frames > 0 else 0

    @classmethod
    def from_exception(
        cls,
        exc_type: Type[Any],
        exc_value: BaseException,
        traceback: Optional[TracebackType],
        *,
        width: Optional[int] = 100,
        code_width: Optional[int] = 88,
        extra_lines: int = 3,
        theme: Optional[str] = None,
        word_wrap: bool = False,
        show_locals: bool = False,
        locals_max_length: int = LOCALS_MAX_LENGTH,
        locals_max_string: int = LOCALS_MAX_STRING,
        locals_hide_dunder: bool = True,
        locals_hide_sunder: bool = False,
        indent_guides: bool = True,
        suppress: Iterable[Union[str, ModuleType]] = (),
        max_frames: int = 100,
    ) -> "Traceback":
        """Create a traceback from exception info

        Args:
            exc_type (Type[BaseException]): Exception type.
            exc_value (BaseException): Exception value.
            traceback (TracebackType): Python Traceback object.
            width (Optional[int], optional): Number of characters used to traceback. Defaults to 100.
            code_width (Optional[int], optional): Number of code characters used to traceback. Defaults to 88.
            extra_lines (int, optional): Additional lines of code to render. Defaults to 3.
            theme (str, optional): Override pygments theme used in traceback.
            word_wrap (bool, optional): Enable word wrapping of long lines. Defaults to False.
            show_locals (bool, optional): Enable display of local variables. Defaults to False.
            indent_guides (bool, optional): Enable indent guides in code and locals. Defaults to True.
            locals_max_length (int, optional): Maximum length of containers before abbreviating, or None for no abbreviation.
                Defaults to 10.
            locals_max_string (int, optional): Maximum length of string before truncating, or None to disable. Defaults to 80.
            locals_hide_dunder (bool, optional): Hide locals prefixed with double underscore. Defaults to True.
            locals_hide_sunder (bool, optional): Hide locals prefixed with single underscore. Defaults to False.
            suppress (Iterable[Union[str, ModuleType]]): Optional sequence of modules or paths to exclude from traceback.
            max_frames (int): Maximum number of frames to show in a traceback, 0 for no maximum. Defaults to 100.

        Returns:
            Traceback: A Traceback instance that may be printed.
        """
        rich_traceback = cls.extract(
            exc_type,
            exc_value,
            traceback,
            show_locals=show_locals,
            locals_max_length=locals_max_length,
            locals_max_string=locals_max_string,
            locals_hide_dunder=locals_hide_dunder,
            locals_hide_sunder=locals_hide_sunder,
        )

        return cls(
            rich_traceback,
            width=width,
            code_width=code_width,
            extra_lines=extra_lines,
            theme=theme,
            word_wrap=word_wrap,
            show_locals=show_locals,
            indent_guides=indent_guides,
            locals_max_length=locals_max_length,
            locals_max_string=locals_max_string,
            locals_hide_dunder=locals_hide_dunder,
            locals_hide_sunder=locals_hide_sunder,
            suppress=suppress,
            max_frames=max_frames,
        )

    @classmethod
    def extract(
        cls,
        exc_type: Type[BaseException],
        exc_value: BaseException,
        traceback: Optional[TracebackType],
        *,
        show_locals: bool = False,
        locals_max_length: int = LOCALS_MAX_LENGTH,
        locals_max_string: int = LOCALS_MAX_STRING,
        locals_hide_dunder: bool = True,
        locals_hide_sunder: bool = False,
    ) -> Trace:
        """Extract traceback information.

        Args:
            exc_type (Type[BaseException]): Exception type.
            exc_value (BaseException): Exception value.
            traceback (TracebackType): Python Traceback object.
            show_locals (bool, optional): Enable display of local variables. Defaults to False.
            locals_max_length (int, optional): Maximum length of containers before abbreviating, or None for no abbreviation.
                Defaults to 10.
            locals_max_string (int, optional): Maximum length of string before truncating, or None to disable. Defaults to 80.
            locals_hide_dunder (bool, optional): Hide locals prefixed with double underscore. Defaults to True.
            locals_hide_sunder (bool, optional): Hide locals prefixed with single underscore. Defaults to False.

        Returns:
            Trace: A Trace instance which you can use to construct a `Traceback`.
        """

        stacks: List[Stack] = []
        is_cause = False

        from rich import _IMPORT_CWD

        notes: List[str] = getattr(exc_value, "__notes__", None) or []

        def safe_str(_object: Any) -> str:
            """Don't allow exceptions from __str__ to propagate."""
            try:
                return str(_object)
            except Exception:
                return "<exception str() failed>"

        while True:
            stack = Stack(
                exc_type=safe_str(exc_type.__name__),
                exc_value=safe_str(exc_value),
                is_cause=is_cause,
                notes=notes,
            )

            if sys.version_info >= (3, 11):
                if isinstance(exc_value, (BaseExceptionGroup, ExceptionGroup)):
                    stack.is_group = True
                    for exception in exc_value.exceptions:
                        stack.exceptions.append(
                            Traceback.extract(
                                type(exception),
                                exception,
                                exception.__traceback__,
                                show_locals=show_locals,
                                locals_max_length=locals_max_length,
                                locals_hide_dunder=locals_hide_dunder,
                                locals_hide_sunder=locals_hide_sunder,
                            )
                        )

            if isinstance(exc_value, SyntaxError):
                stack.syntax_error = _SyntaxError(
                    offset=exc_value.offset or 0,
                    filename=exc_value.filename or "?",
                    lineno=exc_value.lineno or 0,
                    line=exc_value.text or "",
                    msg=exc_value.msg,
                    notes=notes,
                )

            stacks.append(stack)
            append = stack.frames.append

            def get_locals(
                iter_locals: Iterable[Tuple[str, object]],
            ) -> Iterable[Tuple[str, object]]:
                """Extract locals from an iterator of key pairs."""
                if not (locals_hide_dunder or locals_hide_sunder):
                    yield from iter_locals
                    return
                for key, value in iter_locals:
                    if locals_hide_dunder and key.startswith("__"):
                        continue
                    if locals_hide_sunder and key.startswith("_"):
                        continue
                    yield key, value

            for frame_summary, line_no in walk_tb(traceback):
                filename = frame_summary.f_code.co_filename

                last_instruction: Optional[Tuple[Tuple[int, int], Tuple[int, int]]]
                last_instruction = None
                if sys.version_info >= (3, 11):
                    instruction_index = frame_summary.f_lasti // 2
                    instruction_position = next(
                        islice(
                            frame_summary.f_code.co_positions(),
                            instruction_index,
                            instruction_index + 1,
                        )
                    )
                    (
                        start_line,
                        end_line,
                        start_column,
                        end_column,
                    ) = instruction_position
                    if (
                        start_line is not None
                        and end_line is not None
                        and start_column is not None
                        and end_column is not None
                    ):
                        last_instruction = (
                            (start_line, start_column),
                            (end_line, end_column),
                        )

                if filename and not filename.startswith("<"):
                    if not os.path.isabs(filename):
                        filename = os.path.join(_IMPORT_CWD, filename)
                if frame_summary.f_locals.get("_rich_traceback_omit", False):
                    continue

                frame = Frame(
                    filename=filename or "?",
                    lineno=line_no,
                    name=frame_summary.f_code.co_name,
                    locals=(
                        {
                            key: pretty.traverse(
                                value,
                                max_length=locals_max_length,
                                max_string=locals_max_string,
                            )
                            for key, value in get_locals(frame_summary.f_locals.items())
                            if not (inspect.isfunction(value) or inspect.isclass(value))
                        }
                        if show_locals
                        else None
                    ),
                    last_instruction=last_instruction,
                )
                append(frame)
                if frame_summary.f_locals.get("_rich_traceback_guard", False):
                    del stack.frames[:]

            cause = getattr(exc_value, "__cause__", None)
            if cause:
                exc_type = cause.__class__
                exc_value = cause
                # __traceback__ can be None, e.g. for exceptions raised by the
                # 'multiprocessing' module
                traceback = cause.__traceback__
                is_cause = True
                continue

            cause = exc_value.__context__
            if cause and not getattr(exc_value, "__suppress_context__", False):
                exc_type = cause.__class__
                exc_value = cause
                traceback = cause.__traceback__
                is_cause = False
                continue
            # No cover, code is reached but coverage doesn't recognize it.
            break  # pragma: no cover

        trace = Trace(stacks=stacks)

        return trace

    def __rich_console__(
        self, console: Console, options: ConsoleOptions
    ) -> RenderResult:
        theme = self.theme
        background_style = theme.get_background_style()
        token_style = theme.get_style_for_token

        traceback_theme = Theme(
            {
                "pretty": token_style(TextToken),
                "pygments.text": token_style(Token),
                "pygments.string": token_style(String),
                "pygments.function": token_style(Name.Function),
                "pygments.number": token_style(Number),
                "repr.indent": token_style(Comment) + Style(dim=True),
                "repr.str": token_style(String),
                "repr.brace": token_style(TextToken) + Style(bold=True),
                "repr.number": token_style(Number),
                "repr.bool_true": token_style(Keyword.Constant),
                "repr.bool_false": token_style(Keyword.Constant),
                "repr.none": token_style(Keyword.Constant),
                "scope.border": token_style(String.Delimiter),
                "scope.equals": token_style(Operator),
                "scope.key": token_style(Name),
                "scope.key.special": token_style(Name.Constant) + Style(dim=True),
            },
            inherit=False,
        )

        highlighter = ReprHighlighter()

        @group()
        def render_stack(stack: Stack, last: bool) -> RenderResult:
            if stack.frames:
                stack_renderable: ConsoleRenderable = Panel(
                    self._render_stack(stack),
                    title="[traceback.title]Traceback [dim](most recent call last)",
                    style=background_style,
                    border_style="traceback.border",
                    expand=True,
                    padding=(0, 1),
                )
                stack_renderable = Constrain(stack_renderable, self.width)
                with console.use_theme(traceback_theme):
                    yield stack_renderable

            if stack.syntax_error is not None:
                with console.use_theme(traceback_theme):
                    yield Constrain(
                        Panel(
                            self._render_syntax_error(stack.syntax_error),
                            style=background_style,
                            border_style="traceback.border.syntax_error",
                            expand=True,
                            padding=(0, 1),
                            width=self.width,
                        ),
                        self.width,
                    )
                yield Text.assemble(
                    (f"{stack.exc_type}: ", "traceback.exc_type"),
