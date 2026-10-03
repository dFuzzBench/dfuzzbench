import colorsys
import io
from time import process_time

from rich import box
from rich.color import Color
from rich.console import Console, ConsoleOptions, Group, RenderableType, RenderResult
from rich.markdown import Markdown
from rich.measure import Measurement
from rich.pretty import Pretty
from rich.segment import Segment
from rich.style import Style
from rich.syntax import Syntax
from rich.table import Table
from rich.text import Text


class ColorBox:
    def __rich_console__(
        self, console: Console, options: ConsoleOptions
    ) -> RenderResult:
        for y in range(0, 5):
            for x in range(options.max_width):
                h = x / options.max_width
                l = 0.1 + ((y / 5) * 0.7)
                r1, g1, b1 = colorsys.hls_to_rgb(h, l, 1.0)
                r2, g2, b2 = colorsys.hls_to_rgb(h, l + 0.7 / 10, 1.0)
                bgcolor = Color.from_rgb(r1 * 255, g1 * 255, b1 * 255)
                color = Color.from_rgb(r2 * 255, g2 * 255, b2 * 255)
                yield Segment("▄", Style(color=color, bgcolor=bgcolor))
            yield Segment.line()

    def __rich_measure__(
        self, console: "Console", options: ConsoleOptions
    ) -> Measurement:
        return Measurement(1, options.max_width)


def make_test_card() -> Table:
    """Get a renderable that demonstrates a number of features."""
    table = Table.grid(padding=1, pad_edge=True)
    table.title = "Rich features"
    table.add_column("Feature", no_wrap=True, justify="center", style="bold red")
    table.add_column("Demonstration")

    color_table = Table(
        box=None,
        expand=False,
        show_header=False,
        show_edge=False,
        pad_edge=False,
    )
    color_table.add_row(
        (
            "✓ [bold green]4-bit color[/]\n"
            "✓ [bold blue]8-bit color[/]\n"
            "✓ [bold magenta]Truecolor (16.7 million)[/]\n"
            "✓ [bold yellow]Dumb terminals[/]\n"
            "✓ [bold cyan]Automatic color conversion"
        ),
        ColorBox(),
    )

    table.add_row("Colors", color_table)

    table.add_row(
        "Styles",
        "All ansi styles: [bold]bold[/], [dim]dim[/], [italic]italic[/italic], [underline]underline[/], [strike]strikethrough[/], [reverse]reverse[/], and even [blink]blink[/].",
    )

    lorem = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Quisque in metus sed sapien ultricies pretium a at justo. Maecenas luctus velit et auctor maximus."
    lorem_table = Table.grid(padding=1, collapse_padding=True)
    lorem_table.pad_edge = False
    lorem_table.add_row(
        Text(lorem, justify="left", style="green"),
        Text(lorem, justify="center", style="yellow"),
        Text(lorem, justify="right", style="blue"),
        Text(lorem, justify="full", style="red"),
    )
    table.add_row(
        "Text",
        Group(
            Text.from_markup(
                """Word wrap text. Justify [green]left[/], [yellow]center[/], [blue]right[/] or [red]full[/].\n"""
            ),
            lorem_table,
        ),
    )

    def comparison(renderable1: RenderableType, renderable2: RenderableType) -> Table:
        table = Table(show_header=False, pad_edge=False, box=None, expand=True)
        table.add_column("1", ratio=1)
        table.add_column("2", ratio=1)
