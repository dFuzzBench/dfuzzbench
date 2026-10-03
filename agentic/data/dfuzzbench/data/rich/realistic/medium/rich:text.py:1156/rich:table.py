from dataclasses import dataclass, field, replace
from typing import (
    TYPE_CHECKING,
    Dict,
    Iterable,
    List,
    NamedTuple,
    Optional,
    Sequence,
    Tuple,
    Union,
)

from . import box, errors
from ._loop import loop_first_last, loop_last
from ._pick import pick_bool
from ._ratio import ratio_distribute, ratio_reduce
from .align import VerticalAlignMethod
from .jupyter import JupyterMixin
from .measure import Measurement
from .padding import Padding, PaddingDimensions
from .protocol import is_renderable
from .segment import Segment
from .style import Style, StyleType
from .text import Text, TextType

if TYPE_CHECKING:
    from .console import (
        Console,
        ConsoleOptions,
        JustifyMethod,
        OverflowMethod,
        RenderableType,
        RenderResult,
    )


@dataclass
class Column:
    """Defines a column within a ~Table.

    Args:
        title (Union[str, Text], optional): The title of the table rendered at the top. Defaults to None.
        caption (Union[str, Text], optional): The table caption rendered below. Defaults to None.
        width (int, optional): The width in characters of the table, or ``None`` to automatically fit. Defaults to None.
        min_width (Optional[int], optional): The minimum width of the table, or ``None`` for no minimum. Defaults to None.
        box (box.Box, optional): One of the constants in box.py used to draw the edges (see :ref:`appendix_box`), or ``None`` for no box lines. Defaults to box.HEAVY_HEAD.
        safe_box (Optional[bool], optional): Disable box characters that don't display on windows legacy terminal with *raster* fonts. Defaults to True.
        padding (PaddingDimensions, optional): Padding for cells (top, right, bottom, left). Defaults to (0, 1).
        collapse_padding (bool, optional): Enable collapsing of padding around cells. Defaults to False.
        pad_edge (bool, optional): Enable padding of edge cells. Defaults to True.
        expand (bool, optional): Expand the table to fit the available space if ``True``, otherwise the table width will be auto-calculated. Defaults to False.
        show_header (bool, optional): Show a header row. Defaults to True.
        show_footer (bool, optional): Show a footer row. Defaults to False.
        show_edge (bool, optional): Draw a box around the outside of the table. Defaults to True.
        show_lines (bool, optional): Draw lines between every row. Defaults to False.
        leading (int, optional): Number of blank lines between rows (precludes ``show_lines``). Defaults to 0.
        style (Union[str, Style], optional): Default style for the table. Defaults to "none".
        row_styles (List[Union, str], optional): Optional list of row styles, if more than one style is given then the styles will alternate. Defaults to None.
        header_style (Union[str, Style], optional): Style of the header. Defaults to "table.header".
        footer_style (Union[str, Style], optional): Style of the footer. Defaults to "table.footer".
        border_style (Union[str, Style], optional): Style of the border. Defaults to None.
        title_style (Union[str, Style], optional): Style of the title. Defaults to None.
        caption_style (Union[str, Style], optional): Style of the caption. Defaults to None.
        title_justify (str, optional): Justify method for title. Defaults to "center".
        caption_justify (str, optional): Justify method for caption. Defaults to "center".
        highlight (bool, optional): Highlight cell contents (if str). Defaults to False.
    """

    header: "RenderableType" = ""
    """RenderableType: Renderable for the header (typically a string)"""

    footer: "RenderableType" = ""
    """RenderableType: Renderable for the footer (typically a string)"""

    header_style: StyleType = ""
    """StyleType: The style of the header."""

    footer_style: StyleType = ""
    """StyleType: The style of the footer."""

    style: StyleType = ""
    """StyleType: The style of the column."""

    justify: "JustifyMethod" = "left"
    """str: How to justify text within the column ("left", "center", "right", or "full")"""

    vertical: "VerticalAlignMethod" = "top"
    """str: How to vertically align content ("top", "middle", or "bottom")"""

    overflow: "OverflowMethod" = "ellipsis"
    """str: Overflow method."""

    width: Optional[int] = None
    """Optional[int]: Width of the column, or ``None`` (default) to auto calculate width."""

    min_width: Optional[int] = None
    """Optional[int]: Minimum width of column, or ``None`` for no minimum. Defaults to None."""

    max_width: Optional[int] = None
    """Optional[int]: Maximum width of column, or ``None`` for no maximum. Defaults to None."""

    ratio: Optional[int] = None
    """Optional[int]: Ratio to use when calculating column width, or ``None`` (default) to adapt to column contents."""

    no_wrap: bool = False
    """bool: Prevent wrapping of text within the column. Defaults to ``False``."""

    highlight: bool = False
    """bool: Apply highlighter to column. Defaults to ``False``."""

    _index: int = 0
    """Index of column."""

    _cells: List["RenderableType"] = field(default_factory=list)

    def copy(self) -> "Column":
        """Return a copy of this Column."""
        return replace(self, _cells=[])

    @property
    def cells(self) -> Iterable["RenderableType"]:
        """Get all cells in the column, not including header."""
        yield from self._cells

    @property
    def flexible(self) -> bool:
        """Check if this column is flexible."""
        return self.ratio is not None


@dataclass
class Row:
    """Information regarding a row."""

    style: Optional[StyleType] = None
    """Style to apply to row."""

    end_section: bool = False
    """Indicated end of section, which will force a line beneath the row."""


class _Cell(NamedTuple):
    """A single cell in a table."""

    style: StyleType
    """Style to apply to cell."""
    renderable: "RenderableType"
    """Cell renderable."""
    vertical: VerticalAlignMethod
    """Cell vertical alignment."""


class Table(JupyterMixin):
    """A console renderable to draw a table.

    Args:
        *headers (Union[Column, str]): Column headers, either as a string, or :class:`~rich.table.Column` instance.
        title (Union[str, Text], optional): The title of the table rendered at the top. Defaults to None.
        caption (Union[str, Text], optional): The table caption rendered below. Defaults to None.
        width (int, optional): The width in characters of the table, or ``None`` to automatically fit. Defaults to None.
        min_width (Optional[int], optional): The minimum width of the table, or ``None`` for no minimum. Defaults to None.
        box (box.Box, optional): One of the constants in box.py used to draw the edges (see :ref:`appendix_box`), or ``None`` for no box lines. Defaults to box.HEAVY_HEAD.
        safe_box (Optional[bool], optional): Disable box characters that don't display on windows legacy terminal with *raster* fonts. Defaults to True.
        padding (PaddingDimensions, optional): Padding for cells (top, right, bottom, left). Defaults to (0, 1).
        collapse_padding (bool, optional): Enable collapsing of padding around cells. Defaults to False.
        pad_edge (bool, optional): Enable padding of edge cells. Defaults to True.
        expand (bool, optional): Expand the table to fit the available space if ``True``, otherwise the table width will be auto-calculated. Defaults to False.
        show_header (bool, optional): Show a header row. Defaults to True.
        show_footer (bool, optional): Show a footer row. Defaults to False.
        show_edge (bool, optional): Draw a box around the outside of the table. Defaults to True.
        show_lines (bool, optional): Draw lines between every row. Defaults to False.
        leading (int, optional): Number of blank lines between rows (precludes ``show_lines``). Defaults to 0.
        style (Union[str, Style], optional): Default style for the table. Defaults to "none".
        row_styles (List[Union, str], optional): Optional list of row styles, if more than one style is given then the styles will alternate. Defaults to None.
        header_style (Union[str, Style], optional): Style of the header. Defaults to "table.header".
        footer_style (Union[str, Style], optional): Style of the footer. Defaults to "table.footer".
        border_style (Union[str, Style], optional): Style of the border. Defaults to None.
        title_style (Union[str, Style], optional): Style of the title. Defaults to None.
        caption_style (Union[str, Style], optional): Style of the caption. Defaults to None.
        title_justify (str, optional): Justify method for title. Defaults to "center".
        caption_justify (str, optional): Justify method for caption. Defaults to "center".
        highlight (bool, optional): Highlight cell contents (if str). Defaults to False.
    """

    columns: List[Column]
    rows: List[Row]

    def __init__(
        self,
        *headers: Union[Column, str],
        title: Optional[TextType] = None,
        caption: Optional[TextType] = None,
        width: Optional[int] = None,
        min_width: Optional[int] = None,
        box: Optional[box.Box] = box.HEAVY_HEAD,
        safe_box: Optional[bool] = None,
        padding: PaddingDimensions = (0, 1),
        collapse_padding: bool = False,
        pad_edge: bool = True,
        expand: bool = False,
        show_header: bool = True,
        show_footer: bool = False,
        show_edge: bool = True,
        show_lines: bool = False,
        leading: int = 0,
        style: StyleType = "none",
        row_styles: Optional[Iterable[StyleType]] = None,
        header_style: Optional[StyleType] = "table.header",
        footer_style: Optional[StyleType] = "table.footer",
        border_style: Optional[StyleType] = None,
        title_style: Optional[StyleType] = None,
        caption_style: Optional[StyleType] = None,
        title_justify: "JustifyMethod" = "center",
        caption_justify: "JustifyMethod" = "center",
        highlight: bool = False,
    ) -> None:
        self.columns: List[Column] = []
        self.rows: List[Row] = []
        self.title = title
        self.caption = caption
        self.width = width
        self.min_width = min_width
        self.box = box
        self.safe_box = safe_box
        self._padding = Padding.unpack(padding)
        self.pad_edge = pad_edge
        self._expand = expand
        self.show_header = show_header
        self.show_footer = show_footer
        self.show_edge = show_edge
        self.show_lines = show_lines
        self.leading = leading
        self.collapse_padding = collapse_padding
        self.style = style
        self.header_style = header_style or ""
        self.footer_style = footer_style or ""
        self.border_style = border_style
        self.title_style = title_style
        self.caption_style = caption_style
        self.title_justify: "JustifyMethod" = title_justify
        self.caption_justify: "JustifyMethod" = caption_justify
        self.highlight = highlight
        self.row_styles: Sequence[StyleType] = list(row_styles or [])
        append_column = self.columns.append
        for header in headers:
            if isinstance(header, str):
                self.add_column(header=header)
            else:
                header._index = len(self.columns)
                append_column(header)

    @classmethod
    def grid(
        cls,
        *headers: Union[Column, str],
        padding: PaddingDimensions = 0,
        collapse_padding: bool = True,
        pad_edge: bool = False,
        expand: bool = False,
    ) -> "Table":
        """Get a table with no lines, headers, or footer.

        Args:
            *headers (Union[Column, str]): Column headers, either as a string, or :class:`~rich.table.Column` instance.
            padding (PaddingDimensions, optional): Get padding around cells. Defaults to 0.
            collapse_padding (bool, optional): Enable collapsing of padding around cells. Defaults to True.
            pad_edge (bool, optional): Enable padding around edges of table. Defaults to False.
            expand (bool, optional): Expand the table to fit the available space if ``True``, otherwise the table width will be auto-calculated. Defaults to False.

        Returns:
            Table: A table instance.
        """
        return cls(
            *headers,
            box=None,
            padding=padding,
            collapse_padding=collapse_padding,
            show_header=False,
            show_footer=False,
            show_edge=False,
            pad_edge=pad_edge,
            expand=expand,
        )

    @property
    def expand(self) -> bool:
        """Setting a non-None self.width implies expand."""
        return self._expand or self.width is not None

    @expand.setter
    def expand(self, expand: bool) -> None:
        """Set expand."""
        self._expand = expand

    @property
    def _extra_width(self) -> int:
        """Get extra width to add to cell content."""
        width = 0
        if self.box and self.show_edge:
            width += 2
        if self.box:
            width += len(self.columns) - 1
        return width

    @property
    def row_count(self) -> int:
        """Get the current number of rows."""
        return len(self.rows)

    def get_row_style(self, console: "Console", index: int) -> StyleType:
        """Get the current row style."""
        style = Style.null()
        if self.row_styles:
            style += console.get_style(self.row_styles[index % len(self.row_styles)])
        row_style = self.rows[index].style
        if row_style is not None:
            style += console.get_style(row_style)
        return style

    def __rich_measure__(
        self, console: "Console", options: "ConsoleOptions"
    ) -> Measurement:
        max_width = options.max_width
        if self.width is not None:
            max_width = self.width
        if max_width < 0:
            return Measurement(0, 0)

        extra_width = self._extra_width
        max_width = sum(
            self._calculate_column_widths(
                console, options.update_width(max_width - extra_width)
            )
        )
        _measure_column = self._measure_column

        measurements = [
            _measure_column(console, options.update_width(max_width), column)
            for column in self.columns
        ]
        minimum_width = (
            sum(measurement.minimum for measurement in measurements) + extra_width
        )
        maximum_width = (
            sum(measurement.maximum for measurement in measurements) + extra_width
            if (self.width is None)
            else self.width
        )
        measurement = Measurement(minimum_width, maximum_width)
        measurement = measurement.clamp(self.min_width)
        return measurement

    @property
    def padding(self) -> Tuple[int, int, int, int]:
        """Get cell padding."""
        return self._padding

    @padding.setter
    def padding(self, padding: PaddingDimensions) -> "Table":
        """Set cell padding."""
        self._padding = Padding.unpack(padding)
        return self

    def add_column(
        self,
        header: "RenderableType" = "",
        footer: "RenderableType" = "",
        *,
        header_style: Optional[StyleType] = None,
        highlight: Optional[bool] = None,
        footer_style: Optional[StyleType] = None,
        style: Optional[StyleType] = None,
        justify: "JustifyMethod" = "left",
        vertical: "VerticalAlignMethod" = "top",
        overflow: "OverflowMethod" = "ellipsis",
        width: Optional[int] = None,
        min_width: Optional[int] = None,
        max_width: Optional[int] = None,
        ratio: Optional[int] = None,
        no_wrap: bool = False,
    ) -> None:
        """Add a column to the table.

        Args:
            header (RenderableType, optional): Text or renderable for the header.
                Defaults to "".
            footer (RenderableType, optional): Text or renderable for the footer.
                Defaults to "".
            header_style (Union[str, Style], optional): Style for the header, or None for default. Defaults to None.
            highlight (bool, optional): Whether to highlight the text. The default of None uses the value of the table (self) object.
            footer_style (Union[str, Style], optional): Style for the footer, or None for default. Defaults to None.
            style (Union[str, Style], optional): Style for the column cells, or None for default. Defaults to None.
            justify (JustifyMethod, optional): Alignment for cells. Defaults to "left".
            vertical (VerticalAlignMethod, optional): Vertical alignment, one of "top", "middle", or "bottom". Defaults to "top".
            overflow (OverflowMethod): Overflow method: "crop", "fold", "ellipsis". Defaults to "ellipsis".
            width (int, optional): Desired width of column in characters, or None to fit to contents. Defaults to None.
            min_width (Optional[int], optional): Minimum width of column, or ``None`` for no minimum. Defaults to None.
            max_width (Optional[int], optional): Maximum width of column, or ``None`` for no maximum. Defaults to None.
            ratio (int, optional): Flexible ratio for the column (requires ``Table.expand`` or ``Table.width``). Defaults to None.
            no_wrap (bool, optional): Set to ``True`` to disable wrapping of this column.
        """

        column = Column(
            _index=len(self.columns),
            header=header,
            footer=footer,
            header_style=header_style or "",
            highlight=highlight if highlight is not None else self.highlight,
            footer_style=footer_style or "",
            style=style or "",
            justify=justify,
            vertical=vertical,
            overflow=overflow,
            width=width,
            min_width=min_width,
            max_width=max_width,
            ratio=ratio,
            no_wrap=no_wrap,
        )
        self.columns.append(column)

    def add_row(
        self,
        *renderables: Optional["RenderableType"],
        style: Optional[StyleType] = None,
        end_section: bool = False,
    ) -> None:
        """Add a row of renderables.

        Args:
            *renderables (None or renderable): Each cell in a row must be a renderable object (including str),
                or ``None`` for a blank cell.
            style (StyleType, optional): An optional style to apply to the entire row. Defaults to None.
            end_section (bool, optional): End a section and draw a line. Defaults to False.

        Raises:
            errors.NotRenderableError: If you add something that can't be rendered.
        """

        def add_cell(column: Column, renderable: "RenderableType") -> None:
            column._cells.append(renderable)

        cell_renderables: List[Optional["RenderableType"]] = list(renderables)

        columns = self.columns
        if len(cell_renderables) < len(columns):
            cell_renderables = [
                *cell_renderables,
                *[None] * (len(columns) - len(cell_renderables)),
            ]
        for index, renderable in enumerate(cell_renderables):
            if index == len(columns):
                column = Column(_index=index, highlight=self.highlight)
                for _ in self.rows:
                    add_cell(column, Text(""))
                self.columns.append(column)
            else:
                column = columns[index]
            if renderable is None:
                add_cell(column, "")
            elif is_renderable(renderable):
