import dataclasses
import sys
from unittest import mock
from unittest.mock import patch

import pytest

from rich.style import Style

if sys.platform == "win32":
    from rich import _win32_console
    from rich._win32_console import COORD, LegacyWindowsTerm, WindowsCoordinates

    CURSOR_X = 1
    CURSOR_Y = 2
    CURSOR_POSITION = WindowsCoordinates(row=CURSOR_Y, col=CURSOR_X)
    SCREEN_WIDTH = 20
    SCREEN_HEIGHT = 30
    DEFAULT_STYLE_ATTRIBUTE = 16
    CURSOR_SIZE = 25

    @dataclasses.dataclass
    class StubScreenBufferInfo:
        dwCursorPosition: COORD = COORD(CURSOR_X, CURSOR_Y)
        dwSize: COORD = COORD(SCREEN_WIDTH, SCREEN_HEIGHT)
        wAttributes: int = DEFAULT_STYLE_ATTRIBUTE

    pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="windows only")

    def test_windows_coordinates_to_ctype():
        coord = WindowsCoordinates.from_param(WindowsCoordinates(row=1, col=2))
        assert coord.X == 2
        assert coord.Y == 1

    @pytest.fixture
    def win32_handle():
        handle = mock.sentinel
        with mock.patch.object(_win32_console, "GetStdHandle", return_value=handle):
            yield handle

    @pytest.fixture
    def win32_console_getters():
        def stub_console_cursor_info(std_handle, cursor_info):
            cursor_info.dwSize = CURSOR_SIZE
            cursor_info.bVisible = True

        with mock.patch.object(
            _win32_console,
            "GetConsoleScreenBufferInfo",
            return_value=StubScreenBufferInfo,
        ) as GetConsoleScreenBufferInfo, mock.patch.object(
            _win32_console, "GetConsoleCursorInfo", side_effect=stub_console_cursor_info
        ) as GetConsoleCursorInfo:
            yield {
                "GetConsoleScreenBufferInfo": GetConsoleScreenBufferInfo,
                "GetConsoleCursorInfo": GetConsoleCursorInfo,
            }

    def test_cursor_position(win32_console_getters):
        term = LegacyWindowsTerm(sys.stdout)
        assert term.cursor_position == WindowsCoordinates(row=CURSOR_Y, col=CURSOR_X)

    def test_screen_size(win32_console_getters):
        term = LegacyWindowsTerm(sys.stdout)
        assert term.screen_size == WindowsCoordinates(
            row=SCREEN_HEIGHT, col=SCREEN_WIDTH
        )

    def test_write_text(win32_console_getters, win32_handle, capsys):
        text = "Hello, world!"
        term = LegacyWindowsTerm(sys.stdout)

        term.write_text(text)

        captured = capsys.readouterr()
        assert captured.out == text

    @patch.object(_win32_console, "SetConsoleTextAttribute")
    def test_write_styled(
        SetConsoleTextAttribute,
        win32_console_getters,
        win32_handle,
        capsys,
    ):
        style = Style.parse("black on red")
        text = "Hello, world!"
        term = LegacyWindowsTerm(sys.stdout)

        term.write_styled(text, style)

        captured = capsys.readouterr()
        assert captured.out == text

        # Ensure we set the text attributes and then reset them after writing styled text
        call_args = SetConsoleTextAttribute.call_args_list
        assert len(call_args) == 2
        first_args, first_kwargs = call_args[0]
        second_args, second_kwargs = call_args[1]

        assert first_args == (win32_handle,)
        assert first_kwargs["attributes"].value == 64
        assert second_args == (win32_handle,)
        assert second_kwargs["attributes"] == DEFAULT_STYLE_ATTRIBUTE

    @patch.object(_win32_console, "SetConsoleTextAttribute")
    def test_write_styled_bold(
        SetConsoleTextAttribute, win32_console_getters, win32_handle
    ):
        style = Style.parse("bold black on red")
        text = "Hello, world!"
        term = LegacyWindowsTerm(sys.stdout)

        term.write_styled(text, style)

        call_args = SetConsoleTextAttribute.call_args_list
        first_args, first_kwargs = call_args[0]

        expected_attr = 64 + 8  # 64 for red bg, +8 for bright black
        assert first_args == (win32_handle,)
        assert first_kwargs["attributes"].value == expected_attr

    @patch.object(_win32_console, "SetConsoleTextAttribute")
    def test_write_styled_reverse(
        SetConsoleTextAttribute, win32_console_getters, win32_handle
    ):
        style = Style.parse("reverse red on blue")
        text = "Hello, world!"
        term = LegacyWindowsTerm(sys.stdout)

        term.write_styled(text, style)

        call_args = SetConsoleTextAttribute.call_args_list
        first_args, first_kwargs = call_args[0]

        expected_attr = 64 + 1  # 64 for red bg (after reverse), +1 for blue fg
        assert first_args == (win32_handle,)
        assert first_kwargs["attributes"].value == expected_attr

    @patch.object(_win32_console, "SetConsoleTextAttribute")
    def test_write_styled_reverse(
        SetConsoleTextAttribute, win32_console_getters, win32_handle
    ):
        style = Style.parse("dim bright_red on blue")
        text = "Hello, world!"
        term = LegacyWindowsTerm(sys.stdout)

        term.write_styled(text, style)

        call_args = SetConsoleTextAttribute.call_args_list
        first_args, first_kwargs = call_args[0]

        expected_attr = 4 + 16  # 4 for red text (after dim), +16 for blue bg
        assert first_args == (win32_handle,)
        assert first_kwargs["attributes"].value == expected_attr

    @patch.object(_win32_console, "SetConsoleTextAttribute")
    def test_write_styled_no_foreground_color(
        SetConsoleTextAttribute, win32_console_getters, win32_handle
    ):
        style = Style.parse("on blue")
        text = "Hello, world!"
        term = LegacyWindowsTerm(sys.stdout)

        term.write_styled(text, style)

        call_args = SetConsoleTextAttribute.call_args_list
        first_args, first_kwargs = call_args[0]

        expected_attr = 16 | term._default_fore  # 16 for blue bg, plus default fg color
        assert first_args == (win32_handle,)
        assert first_kwargs["attributes"].value == expected_attr

    @patch.object(_win32_console, "SetConsoleTextAttribute")
    def test_write_styled_no_background_color(
        SetConsoleTextAttribute, win32_console_getters, win32_handle
    ):
        style = Style.parse("blue")
        text = "Hello, world!"
        term = LegacyWindowsTerm(sys.stdout)

        term.write_styled(text, style)

        call_args = SetConsoleTextAttribute.call_args_list
        first_args, first_kwargs = call_args[0]

        expected_attr = (
            16 | term._default_back
        )  # 16 for blue foreground, plus default bg color
        assert first_args == (win32_handle,)
        assert first_kwargs["attributes"].value == expected_attr

    @patch.object(_win32_console, "FillConsoleOutputCharacter", return_value=None)
    @patch.object(_win32_console, "FillConsoleOutputAttribute", return_value=None)
    def test_erase_line(
        FillConsoleOutputAttribute,
        FillConsoleOutputCharacter,
        win32_console_getters,
        win32_handle,
    ):
        term = LegacyWindowsTerm(sys.stdout)
        term.erase_line()
        start = WindowsCoordinates(row=CURSOR_Y, col=0)
        FillConsoleOutputCharacter.assert_called_once_with(
            win32_handle, " ", length=SCREEN_WIDTH, start=start
        )
        FillConsoleOutputAttribute.assert_called_once_with(
            win32_handle, DEFAULT_STYLE_ATTRIBUTE, length=SCREEN_WIDTH, start=start
        )

    @patch.object(_win32_console, "FillConsoleOutputCharacter", return_value=None)
    @patch.object(_win32_console, "FillConsoleOutputAttribute", return_value=None)
    def test_erase_end_of_line(
        FillConsoleOutputAttribute,
        FillConsoleOutputCharacter,
        win32_console_getters,
        win32_handle,
    ):
        term = LegacyWindowsTerm(sys.stdout)
        term.erase_end_of_line()

        FillConsoleOutputCharacter.assert_called_once_with(
            win32_handle, " ", length=SCREEN_WIDTH - CURSOR_X, start=CURSOR_POSITION
        )
        FillConsoleOutputAttribute.assert_called_once_with(
            win32