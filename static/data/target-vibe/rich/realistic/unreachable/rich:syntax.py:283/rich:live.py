import sys
from threading import Event, RLock, Thread
from types import TracebackType
from typing import IO, Any, Callable, List, Optional, TextIO, Type, cast

from . import get_console
from .console import Console, ConsoleRenderable, RenderableType, RenderHook
from .control import Control
from .file_proxy import FileProxy
from .jupyter import JupyterMixin
from .live_render import LiveRender, VerticalOverflowMethod
from .screen import Screen
from .text import Text


class _RefreshThread(Thread):
    """A thread that calls refresh() at regular intervals."""

    def __init__(self, live: "Live", refresh_per_second: float) -> None:
        self.live = live
        self.refresh_per_second = refresh_per_second
        self.done = Event()
        super().__init__(daemon=True)

    def stop(self) -> None:
        self.done.set()

    def run(self) -> None:
        while not self.done.wait(1 / self.refresh_per_second):
            with self.live._lock:
                if not self.done.is_set():
                    self.live.refresh()


class Live(JupyterMixin, RenderHook):
    """Renders an auto-updating live display of any given renderable.

    Args:
        renderable (RenderableType, optional): The renderable to live display. Defaults to displaying nothing.
        console (Console, optional): Optional Console instance. Defaults to an internal Console instance writing to stdout.
        screen (bool, optional): Enable alternate screen mode. Defaults to False.
        auto_refresh (bool, optional): Enable auto refresh. If disabled, you will need to call `refresh()` or `update()` with refresh flag. Defaults to True
        refresh_per_second (float, optional): Number of times per second to refresh the live display. Defaults to 4.
        transient (bool, optional): Clear the renderable on exit (has no effect when screen=True). Defaults to False.
        redirect_stdout (bool, optional): Enable redirection of stdout, so ``print`` may be used. Defaults to True.
        redirect_stderr (bool, optional): Enable redirection of stderr. Defaults to True.
        vertical_overflow (VerticalOverflowMethod, optional): How to handle renderable when it is too tall for the console. Defaults to "ellipsis".
        get_renderable (Callable[[], RenderableType], optional): Optional callable to get renderable. Defaults to None.
    """

    def __init__(
        self,
        renderable: Optional[RenderableType] = None,
        *,
        console: Optional[Console] = None,
        screen: bool = False,
        auto_refresh: bool = True,
        refresh_per_second: float = 4,
        transient: bool = False,
        redirect_stdout: bool = True,
        redirect_stderr: bool = True,
        vertical_overflow: VerticalOverflowMethod = "ellipsis",
        get_renderable: Optional[Callable[[], RenderableType]] = None,
    ) -> None:
        assert refresh_per_second > 0, "refresh_per_second must be > 0"
        self._renderable = renderable
        self.console = console if console is not None else get_console()
        self._screen = screen
        self._alt_screen = False

        self._redirect_stdout = redirect_stdout
        self._redirect_stderr = redirect_stderr
