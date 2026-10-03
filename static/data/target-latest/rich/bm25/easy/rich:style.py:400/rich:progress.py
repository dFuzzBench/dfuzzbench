import io
import sys
import typing
import warnings
from abc import ABC, abstractmethod
from collections import deque
from dataclasses import dataclass, field
from datetime import timedelta
from io import RawIOBase, UnsupportedOperation
from math import ceil
from mmap import mmap
from operator import length_hint
from os import PathLike, stat
from threading import Event, RLock, Thread
from types import TracebackType
from typing import (
    Any,
    BinaryIO,
    Callable,
    ContextManager,
    Deque,
    Dict,
    Generic,
    Iterable,
    List,
    NamedTuple,
    NewType,
    Optional,
    Sequence,
    TextIO,
    Tuple,
    Type,
    TypeVar,
    Union,
)

if sys.version_info >= (3, 8):
    from typing import Literal
else:
    from typing_extensions import Literal  # pragma: no cover

if sys.version_info >= (3, 11):
    from typing import Self
else:
    from typing_extensions import Self  # pragma: no cover

from . import filesize, get_console
from .console import Console, Group, JustifyMethod, RenderableType
from .highlighter import Highlighter
from .jupyter import JupyterMixin
from .live import Live
from .progress_bar import ProgressBar
from .spinner import Spinner
from .style import StyleType
from .table import Column, Table
from .text import Text, TextType

TaskID = NewType("TaskID", int)

ProgressType = TypeVar("ProgressType")

GetTimeCallable = Callable[[], float]


_I = typing.TypeVar("_I", TextIO, BinaryIO)


class _TrackThread(Thread):
    """A thread to periodically update progress."""

    def __init__(self, progress: "Progress", task_id: "TaskID", update_period: float):
        self.progress = progress
        self.task_id = task_id
        self.update_period = update_period
        self.done = Event()

        self.completed = 0
        super().__init__(daemon=True)

    def run(self) -> None:
        task_id = self.task_id
        advance = self.progress.advance
        update_period = self.update_period
        last_completed = 0
        wait = self.done.wait
        while not wait(update_period) and self.progress.live.is_started:
            completed = self.completed
            if last_completed != completed:
                advance(task_id, completed - last_completed)
                last_completed = completed

        self.progress.update(self.task_id, completed=self.completed, refresh=True)

    def __enter__(self) -> "_TrackThread":
        self.start()
        return self

    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc_val: Optional[BaseException],
        exc_tb: Optional[TracebackType],
    ) -> None:
        self.done.set()
        self.join()


def track(
    sequence: Union[Sequence[ProgressType], Iterable[ProgressType]],
    description: str = "Working...",
    total: Optional[float] = None,
    completed: int = 0,
    auto_refresh: bool = True,
    console: Optional[Console] = None,
    transient: bool = False,
    get_time: Optional[Callable[[], float]] = None,
    refresh_per_second: float = 10,
    style: StyleType = "bar.back",
    complete_style: StyleType = "bar.complete",
    finished_style: StyleType = "bar.finished",
    pulse_style: StyleType = "bar.pulse",
    update_period: float = 0.1,
    disable: bool = False,
    show_speed: bool = True,
) -> Iterable[ProgressType]:
    """Track progress by iterating over a sequence.

    Args:
        sequence (Iterable[ProgressType]): A sequence (must support "len") you wish to iterate over.
        description (str, optional): Description of task show next to progress bar. Defaults to "Working".
        total: (float, optional): Total number of steps. Default is len(sequence).
        completed (int, optional): Number of steps completed so far. Defaults to 0.
        auto_refresh (bool, optional): Automatic refresh, disable to force a refresh after each iteration. Default is True.
        transient: (bool, optional): Clear the progress on exit. Defaults to False.
        console (Console, optional): Console to write to. Default creates internal Console instance.
        refresh_per_second (float): Number of times per second to refresh the progress information. Defaults to 10.
        style (StyleType, optional): Style for the bar background. Defaults to "bar.back".
        complete_style (StyleType, optional): Style for the completed bar. Defaults to "bar.complete".
        finished_style (StyleType, optional): Style for a finished bar. Defaults to "bar.finished".
        pulse_style (StyleType, optional): Style for pulsing bars. Defaults to "bar.pulse".
        update_period (float, optional): Minimum time (in seconds) between calls to update(). Defaults to 0.1.
        disable (bool, optional): Disable display of progress.
        show_speed (bool, optional): Show speed if total isn't known. Defaults to True.
    Returns:
        Iterable[ProgressType]: An iterable of the values in the sequence.

    """

    columns: List["ProgressColumn"] = (
        [TextColumn("[progress.description]{task.description}")] if description else []
    )
    columns.extend(
        (
            BarColumn(
                style=style,
                complete_style=complete_style,
                finished_style=finished_style,
                pulse_style=pulse_style,
            ),
            TaskProgressColumn(show_speed=show_speed),
            TimeRemainingColumn(elapsed_when_finished=True),
        )
    )
    progress = Progress(
        *columns,
        auto_refresh=auto_refresh,
        console=console,
        transient=transient,
        get_time=get_time,
        refresh_per_second=refresh_per_second or 10,
        disable=disable,
    )

    with progress:
        yield from progress.track(
            sequence,
            total=total,
            completed=completed,
            description=description,
            update_period=update_period,
        )


class _Reader(RawIOBase, BinaryIO):
    """A reader that tracks progress while it's being read from."""

    def __init__(
        self,
        handle: BinaryIO,
        progress: "Progress",
        task: TaskID,
        close_handle: bool = True,
    ) -> None:
        self.handle = handle
        self.progress = progress
        self.task = task
        self.close_handle = close_handle
        self._closed = False

    def __enter__(self) -> "_Reader":
        self.handle.__enter__()
        return self

    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc_val: Optional[BaseException],
        exc_tb: Optional[TracebackType],
    ) -> None:
        self.close()

    def __iter__(self) -> BinaryIO:
        return self

    def __next__(self) -> bytes:
        line = next(self.handle)
        self.progress.advance(self.task, advance=len(line))
        return line

    @property
    def closed(self) -> bool:
        return self._closed

    def fileno(self) -> int:
        return self.handle.fileno()

    def isatty(self) -> bool:
        return self.handle.isatty()

    @property
    def mode(self) -> str:
        return self.handle.mode

    @property
    def name(self) -> str:
        return self.handle.name

    def readable(self) -> bool:
        return self.handle.readable()

    def seekable(self) -> bool:
        return self.handle.seekable()

    def writable(self) -> bool:
        return False

    def read(self, size: int = -1) -> bytes:
        block = self.handle.read(size)
        self.progress.advance(self.task, advance=len(block))
        return block

    def readinto(self, b: Union[bytearray, memoryview, mmap]):  # type: ignore[no-untyped-def, override]
        n = self.handle.readinto(b)  # type: ignore[attr-defined]
        self.progress.advance(self.task, advance=n)
        return n

    def readline(self, size: int = -1) -> bytes:  # type: ignore[override]
        line = self.handle.readline(size)
        self.progress.advance(self.task, advance=len(line))
        return line

    def readlines(self, hint: int = -1) -> List[bytes]:
        lines = self.handle.readlines(hint)
        self.progress.advance(self.task, advance=sum(map(len, lines)))
        return lines

    def close(self) -> None:
        if self.close_handle:
            self.handle.close()
        self._closed = True

    def seek(self, offset: int, whence: int = 0) -> int:
        pos = self.handle.seek(offset, whence)
        self.progress.update(self.task, completed=pos)
        return pos

    def tell(self) -> int:
        return self.handle.tell()

    def write(self, s: Any) -> int:
        raise UnsupportedOperation("write")

    def writelines(self, lines: Iterable[Any]) -> None:
        raise UnsupportedOperation("writelines")


class _ReadContext(ContextManager[_I], Generic[_I]):
    """A utility class to handle a context for both a reader and a progress."""

    def __init__(self, progress: "Progress", reader: _I) -> None:
        self.progress = progress
        self.reader: _I = reader

    def __enter__(self) -> _I:
        self.progress.start()
        return self.reader.__enter__()

    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc_val: Optional[BaseException],
        exc_tb: Optional[TracebackType],
    ) -> None:
        self.progress.stop()
        self.reader.__exit__(exc_type, exc_val, exc_tb)


def wrap_file(
    file: BinaryIO,
    total: int,
    *,
    description: str = "Reading...",
    auto_refresh: bool = True,
    console: Optional[Console] = None,
    transient: bool = False,
    get_time: Optional[Callable[[], float]] = None,
    refresh_per_second: float = 10,
    style: StyleType = "bar.back",
    complete_style: StyleType = "bar.complete",
    finished_style: StyleType = "bar.finished",
    pulse_style: StyleType = "bar.pulse",
    disable: bool = False,
) -> ContextManager[BinaryIO]:
    """Read bytes from a file while tracking progress.

    Args:
        file (Union[str, PathLike[str], BinaryIO]): The path to the file to read, or a file-like object in binary mode.
        total (int): Total number of bytes to read.
        description (str, optional): Description of task show next to progress bar. Defaults to "Reading".
        auto_refresh (bool, optional): Automatic refresh, disable to force a refresh after each iteration. Default is True.
        transient: (bool, optional): Clear the progress on exit. Defaults to False.
        console (Console, optional): Console to write to. Default creates internal Console instance.
        refresh_per_second (float): Number of times per second to refresh the progress information. Defaults to 10.
        style (StyleType, optional): Style for the bar background. Defaults to "bar.back".
        complete_style (StyleType, optional): Style for the completed bar. Defaults to "bar.complete".
        finished_style (StyleType, optional): Style for a finished bar. Defaults to "bar.finished".
        pulse_style (StyleType, optional): Style for pulsing bars. Defaults to "bar.pulse".
        disable (bool, optional): Disable display of progress.
    Returns:
        ContextManager[BinaryIO]: A context manager yielding a progress reader.

    """

    columns: List["ProgressColumn"] = (
        [TextColumn("[progress.description]{task.description}")] if description else []
    )
    columns.extend(
        (
            BarColumn(
                style=style,
                complete_style=complete_style,
                finished_style=finished_style,
                pulse_style=pulse_style,
            ),
            DownloadColumn(),
            TimeRemainingColumn(),
        )
    )
    progress = Progress(
        *columns,
        auto_refresh=auto_refresh,
        console=console,
        transient=transient,
        get_time=get_time,
        refresh_per_second=refresh_per_second or 10,
        disable=disable,
    )

    reader = progress.wrap_file(file, total=total, description=description)
    return _ReadContext(progress, reader)


@typing.overload
def open(
    file: Union[str, "PathLike[str]", bytes],
    mode: Union[Literal["rt"], Literal["r"]],
    buffering: int = -1,
    encoding: Optional[str] = None,
    errors: Optional[str] = None,
    newline: Optional[str] = None,
    *,
    total: Optional[int] = None,
    description: str = "Reading...",
    auto_refresh: bool = True,
    console: Optional[Console] = None,
    transient: bool = False,
    get_time: Optional[Callable[[], float]] = None,
    refresh_per_second: float = 10,
    style: StyleType = "bar.back",
    complete_style: StyleType = "bar.complete",
    finished_style: StyleType = "bar.finished",
    pulse_style: StyleType = "bar.pulse",
    disable: bool = False,
) -> ContextManager[TextIO]:
    pass


@typing.overload
def open(
    file: Union[str, "PathLike[str]", bytes],
    mode: Literal["rb"],
    buffering: int = -1,
    encoding: Optional[str] = None,
    errors: Optional[str] = None,
    newline: Optional[str] = None,
    *,
    total: Optional[int] = None,
    description: str = "Reading...",
    auto_refresh: bool = True,
    console: Optional[Console] = None,
    transient: bool = False,
    get_time: Optional[Callable[[], float]] = None,
    refresh_per_second: float = 10,
    style: StyleType = "bar.back",
    complete_style: StyleType = "bar.complete",
    finished_style: StyleType = "bar.finished",
    pulse_style: StyleType = "bar.pulse",
    disable: bool = False,
) -> ContextManager[BinaryIO]:
    pass


def open(
    file: Union[str, "PathLike[str]", bytes],
    mode: Union[Literal["rb"], Literal["rt"], Literal["r"]] = "r",
    buffering: int = -1,
    encoding: Optional[str] = None,
    errors: Optional[str] = None,
    newline: Optional[str] = None,
    *,
    total: Optional[int] = None,
    description: str = "Reading...",
    auto_refresh: bool = True,
    console: Optional[Console] = None,
    transient: bool = False,
    get_time: Optional[Callable[[], float]] = None,
    refresh_per_second: float = 10,
    style: StyleType = "bar.back",
    complete_style: StyleType = "bar.complete",
    finished_style: StyleType = "bar.finished",
    pulse_style: StyleType = "bar.pulse",
    disable: bool = False,
) -> Union[ContextManager[BinaryIO], ContextManager[TextIO]]:
    """Read bytes from a file while tracking progress.

    Args:
        path (Union[str, PathLike[str], BinaryIO]): The path to the file to read, or a file-like object in binary mode.
        mode (str): The mode to use to open the file. Only supports "r", "rb" or "rt".
        buffering (int): The buffering strategy to use, see :func:`io.open`.
        encoding (str, optional): The encoding to use when reading in text mode, see :func:`io.open`.
        errors (str, optional): The error handling strategy for decoding errors, see :func:`io.open`.
        newline (str, optional): The strategy for handling newlines in text mode, see :func:`io.open`
        total: (int, optional): Total number of bytes to read. Must be provided if reading from a file handle. Default for a path is os.stat(file).st_size.
        description (str, optional): Description of task show next to progress bar. Defaults to "Reading".
        auto_refresh (bool, optional): Automatic refresh, disable to force a refresh after each iteration. Default is True.
        transient: (bool, optional): Clear the progress on exit. Defaults to False.
        console (Console, optional): Console to write to. Default creates internal Console instance.
        refresh_per_second (float): Number of times per second to refresh the progress information. Defaults to 10.
        style (StyleType, optional): Style for the bar background. Defaults to "bar.back".
        complete_style (StyleType, optional): Style for the completed bar. Defaults to "bar.complete".
        finished_style (StyleType, optional): Style for a finished bar. Defaults to "bar.finished".
        pulse_style (StyleType, optional): Style for pulsing bars. Defaults to "bar.pulse".
        disable (bool, optional): Disable display of progress.
        encoding (str, optional): The encoding to use when reading in text mode.

    Returns:
        ContextManager[BinaryIO]: A context manager yielding a progress reader.

    """

    columns: List["ProgressColumn"] = (
        [TextColumn("[progress.description]{task.description}")] if description else []
    )
    columns.extend(
        (
            BarColumn(
                style=style,
                complete_style=complete_style,
                finished_style=finished_style,
                pulse_style=pulse_style,
            ),
            DownloadColumn(),
            TimeRemainingColumn(),
        )
    )
    progress = Progress(
        *columns,
        auto_refresh=auto_refresh,
        console=console,
        transient=transient,
        get_time=get_time,
        refresh_per_second=refresh_per_second or 10,
        disable=disable,
    )

    reader = progress.open(
        file,
        mode=mode,
        buffering=buffering,
        encoding=encoding,
        errors=errors,
        newline=newline,
        total=total,
        description=description,
    )
    return _ReadContext(progress, reader)  # type: ignore[return-value, type-var]


class ProgressColumn(ABC):
    """Base class for a widget to use in progress display."""

    max_refresh: Optional[float] = None

    def __init__(self, table_column: Optional[Column] = None) -> None:
        self._table_column = table_column
        self._renderable_cache: Dict[TaskID, Tuple[float, RenderableType]] = {}
        self._update_time: Optional[float] = None

    def get_table_column(self) -> Column:
        """Get a table column, used to build tasks table."""
        return self._table_column or Column()

    def __call__(self, task: "Task") -> RenderableType:
        """Called by the Progress object to return a renderable for the given task.

        Args:
            task (Task): An object containing information regarding the task.

        Returns:
            RenderableType: Anything renderable (including str).
        """
        current_time = task.get_time()
        if self.max_refresh is not None and not task.completed:
            try:
                timestamp, renderable = self._renderable_cache[task.id]
            except KeyError:
                pass
            else:
                if timestamp + self.max_refresh > current_time:
                    return renderable

        renderable = self.render(task)
        self._renderable_cache[task.id] = (current_time, renderable)
        return renderable

    @abstractmethod
    def render(self, task: "Task") -> RenderableType:
        """Should return a renderable object."""


class RenderableColumn(ProgressColumn):
    """A column to insert an arbitrary column.

    Args:
        renderable (RenderableType, optional): Any renderable. Defaults to empty string.
    """

    def __init__(
        self, renderable: RenderableType = "", *, table_column: Optional[Column] = None
    ):
        self.renderable = renderable
        super().__init__(table_column=table_column)

    def render(self, task: "Task") -> RenderableType:
        return self.renderable


class SpinnerColumn(ProgressColumn):
    """A column with a 'spinner' animation.

    Args:
        spinner_name (str, optional): Name of spinner animation. Defaults to "dots".
        style (StyleType, optional): Style of spinner. Defaults to "progress.spinner".
        speed (float, optional): Speed factor of spinner. Defaults to 1.0.
        finished_text (TextType, optional): Text used when task is finished. Defaults to " ".
    """

    def __init__(
        self,
        spinner_name: str = "dots",
        style: Optional[StyleType] = "progress.spinner",
        speed: float = 1.0,
        finished_text: TextType = " ",
        table_column: Optional[Column] = None,
    ):
        self.spinner = Spinner(spinner_name, style=style, speed=speed)
        self.finished_text = (
            Text.from_markup(finished_text)
            if isinstance(finished_text, str)
            else finished_text
        )
        super().__init__(table_column=table_column)

    def set_spinner(
        self,
        spinner_name: str,
        spinner_style: Optional[StyleType] = "progress.spinner",
        speed: float = 1.0,
    ) -> None:
        """Set a new spinner.

        Args:
            spinner_name (str): Spinner name, see python -m rich.spinner.
            spinner_style (Optional[StyleType], optional): Spinner style. Defaults to "progress.spinner".
            speed (float, optional): Speed factor of spinner. Defaults to 1.0.
        """
        self.spinner = Spinner(spinner_name, style=spinner_style, speed=speed)

    def render(self, task: "Task") -> RenderableType:
        text = (
            self.finished_text
            if task.finished
            else self.spinner.render(task.get_time())
        )
        return text


class TextColumn(ProgressColumn):
    """A column containing text."""

    def __init__(
        self,
        text_format: str,
        style: StyleType = "none",
        justify: JustifyMethod = "left",
        markup: bool = True,
        highlighter: Optional[Highlighter] = None,
        table_column: Optional[Column] = None,
    ) -> None:
        self.text_format = text_format
        self.justify: JustifyMethod = justify
        self.style = style
        self.markup = markup
        self.highlighter = highlighter
        super().__init__(table_column=table_column or Column(no_wrap=True))

    def render(self, task: "Task") -> Text:
        _text = self.text_format.format(task=task)
        if self.markup:
            text = Text.from_markup(_text, style=self.style, justify=self.justify)
        else:
            text = Text(_text, style=self.style, justify=self.justify)
        if self.highlighter:
            self.highlighter.highlight(text)
        return text


class BarColumn(ProgressColumn):
    """Renders a visual progress bar.

    Args:
        bar_width (Optional[int], optional): Width of bar or None for full width. Defaults to 40.
        style (StyleType, optional): Style for the bar background. Defaults to "bar.back".
        complete_style (StyleType, optional): Style for the completed bar. Defaults to "bar.complete".
        finished_style (StyleType, optional): Style for a finished bar. Defaults to "bar.finished".
        pulse_style (StyleType, optional): Style for pulsing bars. Defaults to "bar.pulse".
    """

    def __init__(
        self,
        bar_width: Optional[int] = 40,
        style: StyleType = "bar.back",
        complete_style: StyleType = "bar.complete",
        finished_style: StyleType = "bar.finished",
        pulse_style: StyleType = "bar.pulse",
        table_column: Optional[Column] = None,
    ) -> None:
        self.bar_width = bar_width
        self.style = style
        self.complete_style = complete_style
        self.finished_style = finished_style
        self.pulse_style = pulse_style
        super().__init__(table_column=table_column)

    def render(self, task: "Task") -> ProgressBar:
        """Gets a progress bar widget for a task."""
        return ProgressBar(
            total=max(0, task.total) if task.total is not None else None,
            completed=max(0, task.completed),
            width=None if self.bar_width is None else max(1, self.bar_width),
            pulse=not task.started,
            animation_time=task.get_time(),
            style=self.style,
            complete_style=self.complete_style,
            finished_style=self.finished_style,
            pulse_style=self.pulse_style,
        )


class TimeElapsedColumn(ProgressColumn):
    """Renders time elapsed."""

    def render(self, task: "Task") -> Text:
        """Show time elapsed."""
        elapsed = task.finished_time if task.finished else task.elapsed
        if elapsed is None:
            return Text("-:--:--", style="progress.elapsed")
        delta = timedelta(seconds=max(0, int(elapsed)))
        return Text(str(delta), style="progress.elapsed")


class TaskProgressColumn(TextColumn):
    """Show task progress as a percentage.

    Args:
        text_format (str, optional): Format for percentage display. Defaults to "[progress.percentage]{task.percentage:>3.0f}%".
        text_format_no_percentage (str, optional): Format if percentage is unknown. Defaults to "".
        style (StyleType, optional): Style of output. Defaults to "none".
        justify (JustifyMethod, optional): Text justification. Defaults to "left".
        markup (bool, optional): Enable markup. Defaults to True.
        highlighter (Optional[Highlighter], optional): Highlighter to apply to output. Defaults to None.
        table_column (Optional[Column], optional): Table Column to use. Defaults to None.
        show_speed (bool, optional): Show speed if total is unknown. Defaults to False.
    """

    def __init__(
        self,
        text_format: str = "[progress.percentage]{task.percentage:>3.0f}%",
        text_format_no_percentage: str = "",
        style: StyleType = "none",
        justify: JustifyMethod = "left",
        markup: bool = True,
        highlighter: Optional[Highlighter] = None,
        table_column: Optional[Column] = None,
        show_speed: bool = False,
    ) -> None:
        self.text_format_no_percentage = text_format_no_percentage
        self.show_speed = show_speed
        super().__init__(
            text_format=text_format,
            style=style,
            justify=justify,
            markup=markup,
            highlighter=highlighter,
            table_column=table_column,
        )

    @classmethod
    def render_speed(cls, speed: Optional[float]) -> Text:
        """Render the speed in iterations per second.

        Args:
            task (Task): A Task object.

        Returns:
            Text: Text object containing the task speed.
        """
        if speed is None:
            return Text("", style="progress.percentage")
        unit, suffix = filesize.pick_unit_and_suffix(
            int(speed),
            ["", "×10³", "×10⁶", "×10⁹", "×10¹²"],
            1000,
        )
        data_speed = speed / unit
        return Text(f"{data_speed:.1f}{suffix} it/s", style="progress.percentage")

    def render(self, task: "Task") -> Text:
        if task.total is None and self.show_speed:
            return self.render_speed(task.finished_speed or task.speed)
        text_format = (
            self.text_format_no_percentage if task.total is None else self.text_format
        )
        _text = text_format.format(task=task)
        if self.markup:
            text = Text.from_markup(_text, style=self.style, justify=self.justify)
        else:
            text = Text(_text, style=self.style, justify=self.justify)
        if self.highlighter:
            self.highlighter.highlight(text)
        return text


class TimeRemainingColumn(ProgressColumn):
    """Renders estimated time remaining.

    Args:
        compact (bool, optional): Render MM:SS when time remaining is less than an hour. Defaults to False.
        elapsed_when_finished (bool, optional): Render time elapsed when the task is finished. Defaults to False.
    """

    # Only refresh twice a second to prevent jitter
    max_refresh = 0.5

    def __init__(
        self,
        compact: bool = False,
        elapsed_when_finished: bool = False,
        table_column: Optional[Column] = None,
    ):
        self.compact = compact
        self.elapsed_when_finished = elapsed_when_finished
        super().__init__(table_column=table_column)

    def render(self, task: "Task") -> Text:
        """Show time remaining."""
        if self.elapsed_when_finished and task.finished:
            task_time = task.finished_time
            style = "progress.elapsed"
        else:
            task_time = task.time_remaining
            style = "progress.remaining"

        if task.total is None:
            return Text("", style=style)

        if task_time is None:
            return Text("--:--" if self.compact else "-:--:--", style=style)

        # Based on https://github.com/tqdm/tqdm/blob/master/tqdm/std.py
        minutes, seconds = divmod(int(task_time), 60)
        hours, minutes = divmod(minutes, 60)

        if self.compact and not hours:
            formatted = f"{minutes:02d}:{seconds:02d}"
        else:
            formatted = f"{hours:d}:{minutes:02d}:{seconds:02d}"

        return Text(formatted, style=style)


class FileSizeColumn(ProgressColumn):
    """Renders completed filesize."""

    def render(self, task: "Task") -> Text:
        """Show data completed."""
        data_size = filesize.decimal(int(task.completed))
        return Text(data_size, style="progress.filesize")


class TotalFileSizeColumn(ProgressColumn):
    """Renders total filesize."""

    def render(self, task: "Task") -> Text:
        """Show data completed."""
        data_size = filesize.decimal(int(task.total)) if task.total is not None else ""
        return Text(data_size, style="progress.filesize.total")


class MofNCompleteColumn(ProgressColumn):
    """Renders completed count/total, e.g. '  10/1000'.

    Best for bounded tasks with int quantities.

    Space pads the completed count so that progress length does not change as task progresses
    past powers of 10.

    Args:
        separator (str, optional): Text to separate completed and total values. Defaults to "/".
    """

    def __init__(self, separator: str = "/", table_column: Optional[Column] = None):
        self.separator = separator
        super().__init__(table_column=table_column)

    def render(self, task: "Task") -> Text:
        """Show completed/total."""
        completed = int(task.completed)
        total = int(task.total) if task.total is not None else "?"
        total_width = len(str(total))
        return Text(
            f"{completed:{total_width}d}{self.separator}{total}",
            style="progress.download",
        )


class DownloadColumn(ProgressColumn):
    """Renders file size downloaded and total, e.g. '0.5/2.3 GB'.

    Args:
        binary_units (bool, optional): Use binary units, KiB, MiB etc. Defaults to False.
    """

    def __init__(
        self, binary_units: bool = False, table_column: Optional[Column] = None
    ) -> None:
        self.binary_units = binary_units
        super().__init__(table_column=table_column)

    def render(self, task: "Task") -> Text:
        """Calculate common unit for completed and total."""
        completed = int(task.completed)

        unit_and_suffix_calculation_base = (
            int(task.total) if task.total is not None else completed
        )
        if self.binary_units:
            unit, suffix = filesize.pick_unit_and_suffix(
                unit_and_suffix_calculation_base,
                ["bytes", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"],
                1024,
            )
        else:
            unit, suffix = filesize.pick_unit_and_suffix(
                unit_and_suffix_calculation_base,
                ["bytes", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"],
                1000,
            )
        precision = 0 if unit == 1 else 1

        completed_ratio = completed / unit
        completed_str = f"{completed_ratio:,.{precision}f}"

        if task.total is not None:
            total = int(task.total)
            total_ratio = total / unit
            total_str = f"{total_ratio:,.{precision}f}"
        else:
            total_str = "?"

        download_status = f"{completed_str}/{total_str} {suffix}"
        download_text = Text(download_status, style="progress.download")
        return download_text


class TransferSpeedColumn(ProgressColumn):
    """Renders human readable transfer speed."""

    def render(self, task: "Task") -> Text:
        """Show data transfer speed."""
        speed = task.finished_speed or task.speed
        if speed is None:
            return Text("?", style="progress.data.speed")
        data_speed = filesize.decimal(int(speed))
        return Text(f"{data_speed}/s", style="progress.data.speed")


class ProgressSample(NamedTuple):
    """Sample of progress for a given time."""

    timestamp: float
    """Timestamp of sample."""
    completed: float
    """Number of steps completed."""


@dataclass
class Task:
    """Information regarding a progress task.

    This object should be considered read-only outside of the :class:`~Progress` class.

    """

    id: TaskID
    """Task ID associated with this task (used in Progress methods)."""

    description: str
    """str: Description of the task."""

    total: Optional[float]
    """Optional[float]: Total number of steps in this task."""

    completed: float
    """float: Number of steps completed"""

    _get_time: GetTimeCallable
    """Callable to get the current time."""

    finished_time: Optional[float] = None
    """float: Time task was finished."""

    visible: bool = True
    """bool: Indicates if this task is visible in the progress display."""

    fields: Dict[str, Any] = field(default_factory=dict)
    """dict: Arbitrary fields passed in via Progress.update."""

    start_time: Optional[float] = field(default=None, init=False, repr=False)
    """Optional[float]: Time this task was started, or None if not started."""

    stop_time: Optional[float] = field(default=None, init=False, repr=False)
    """Optional[float]: Time this task was stopped, or None if not stopped."""

    finished_speed: Optional[float] = None
    """Optional[float]: The last speed for a finished task."""

    _progress: Deque[ProgressSample] = field(
        default_factory=lambda: deque(maxlen=1000), init=False, repr=False
    )

    _lock: RLock = field(repr=False, default_factory=RLock)
    """Thread lock."""

    def get_time(self) -> float:
        """float: Get the current time, in seconds."""
        return self._get_time()

    @property
    def started(self) -> bool:
        """bool: Check if the task as started."""
        return self.start_time is not None

    @property
    def remaining(self) -> Optional[float]:
        """Optional[float]: Get the number of steps remaining, if a non-None total was set."""
        if self.total is None:
            return None
        return self.total - self.completed

    @property
    def elapsed(self) -> Optional[float]:
        """Optional[float]: Time elapsed since task was started, or ``None`` if the task hasn't started."""
        if self.start_time is None:
            return None
        if self.stop_time is not None:
            return self.stop_time - self.start_time
        return self.get_time() - self.start_time

    @property
    def finished(self) -> bool:
        """Check if the task has finished."""
        return self.finished_time is not None

    @property
    def percentage(self) -> float:
        """float: Get progress of task as a percentage. If a None total was set, returns 0"""
        if not self.total:
            return 0.0
        completed = (self.completed / self.total) * 100.0
        completed = min(100.0, max(0.0, completed))
        return completed

    @property
    def speed(self) -> Optional[float]:
        """Optional[float]: Get the estimated speed in steps per second."""
        if self.start_time is None:
            return None
        with self._lock:
            progress = self._progress
            if not progress:
                return None
            total_time = progress[-1].timestamp - progress[0].timestamp
            if total_time == 0:
                return None
            iter_progress = iter(progress)
            next(iter_progress)
            total_completed = sum(sample.completed for sample in iter_progress)
            speed = total_completed / total_time
            return speed

    @property
    def time_remaining(self) -> Optional[float]:
        """Optional[float]: Get estimated time to completion, or ``None`` if no data."""
        if self.finished:
            return 0.0
        speed = self.speed
        if not speed:
            return None
        remaining = self.remaining
        if remaining is None:
            return None
        estimate = ceil(remaining / speed)
        return estimate

    def _reset(self) -> None:
        """Reset progress."""
        self._progress.clear()
        self.finished_time = None
        self.finished_speed = None


class Progress(JupyterMixin):
    """Renders an auto-updating progress bar(s).

    Args:
        console (Console, optional): Optional Console instance. Defaults to an internal Console instance writing to stdout.
        auto_refresh (bool, optional): Enable auto refresh. If disabled, you will need to call `refresh()`.
        refresh_per_second (Optional[float], optional): Number of times per second to refresh the progress information or None to use default (10). Defaults to None.
        speed_estimate_period: (float, optional): Period (in seconds) used to calculate the speed estimate. Defaults to 30.
        transient: (bool, optional): Clear the progress on exit. Defaults to False.
        redirect_stdout: (bool, optional): Enable redirection of stdout, so ``print`` may be used. Defaults to True.
        redirect_stderr: (bool, optional): Enable redirection of stderr. Defaults to True.
        get_time: (Callable, optional): A callable that gets the current time, or None to use Console.get_time. Defaults to None.
        disable (bool, optional): Disable progress display. Defaults to False
        expand (bool, optional): Expand tasks table to fit width. Defaults to False.
    """

    def __init__(
        self,
        *columns: Union[str, ProgressColumn],
        console: Optional[Console] = None,
        auto_refresh: bool = True,
        refresh_per_second: float = 10,
        speed_estimate_period: float = 30.0,
        transient: bool = False,
        redirect_stdout: bool = True,
        redirect_stderr: bool = True,
        get_time: Optional[GetTimeCallable] = None,
        disable: bool = False,
        expand: bool = False,
    ) -> None:
        assert refresh_per_second > 0, "refresh_per_second must be > 0"
        self._lock = RLock()
        self.columns = columns or self.get_default_columns()
        self.speed_estimate_period = speed_estimate_period

        self.disable = disable
        self.expand = expand
        self._tasks: Dict[TaskID, Task] = {}
        self._task_index: TaskID = TaskID(0)
        self.live = Live(
            console=console or get_console(),
            auto_refresh=auto_refresh,
            refresh_per_second=refresh_per_second,
            transient=transient,
            redirect_stdout=redirect_stdout,
            redirect_stderr=redirect_stderr,
            get_renderable=self.get_renderable,
        )
        self.get_time = get_time or self.console.get_time
        self.print = self.console.print
        self.log = self.console.log

    @classmethod
    def get_default_columns(cls) -> Tuple[ProgressColumn, ...]:
        """Get the default columns used for a new Progress instance:
           - a text column for the description (TextColumn)
           - the bar itself (BarColumn)
           - a text column showing completion percentage (TextColumn)
           - an estimated-time-remaining column (TimeRemainingColumn)
        If the Progress instance is created without passing a columns argument,
        the default columns defined here will be used.

        You can also create a Progress instance using custom columns before
        and/or after the defaults, as in this example:

            progress = Progress(
                SpinnerColumn(),
                *Progress.get_default_columns(),
                "Elapsed:",
                TimeElapsedColumn(),
            )

        This code shows the creation of a Progress display, containing
        a spinner to the left, the default columns, and a labeled elapsed
        time column.
        """
        return (
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TaskProgressColumn(),
            TimeRemainingColumn(),
        )

    @property
    def console(self) -> Console:
        return self.live.console

    @property
    def tasks(self) -> List[Task]:
        """Get a list of Task instances."""
        with self._lock:
