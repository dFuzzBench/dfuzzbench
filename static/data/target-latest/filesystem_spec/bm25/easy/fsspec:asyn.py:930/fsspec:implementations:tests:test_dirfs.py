import pytest

from fsspec.asyn import AsyncFileSystem
from fsspec.implementations.dirfs import DirFileSystem
from fsspec.spec import AbstractFileSystem

PATH = "path/to/dir"
ARGS = ["foo", "bar"]
KWARGS = {"baz": "baz", "qux": "qux"}


@pytest.fixture
def make_fs(mocker):
    def _make_fs(async_impl=False, asynchronous=False):
        attrs = {
            "sep": "/",
            "async_impl": async_impl,
            "_strip_protocol": lambda path: path,
        }

        if async_impl:
            attrs["asynchronous"] = asynchronous
            cls = AsyncFileSystem
        else:
            cls = AbstractFileSystem

        fs = mocker.MagicMock(spec=cls, **attrs)

        return fs

    return _make_fs


@pytest.fixture(
    params=[
        pytest.param(False, id="sync"),
        pytest.param(True, id="async"),
    ]
)
def fs(make_fs, request):
    return make_fs(async_impl=request.param)


@pytest.fixture
def asyncfs(make_fs):
    return make_fs(async_impl=True, asynchronous=True)


@pytest.fixture
def make_dirfs():
    def _make_dirfs(fs, asynchronous=False):
        return DirFileSystem(PATH, fs, asynchronous=asynchronous)

    return _make_dirfs


@pytest.fixture
def dirfs(make_dirfs, fs):
    return make_dirfs(fs)


@pytest.fixture
def adirfs(make_dirfs, asyncfs):
    return make_dirfs(asyncfs, asynchronous=True)


def test_dirfs(fs, asyncfs):
    DirFileSystem("path", fs)
    DirFileSystem("path", asyncfs, asynchronous=True)

    with pytest.raises(ValueError):
        DirFileSystem("path", asyncfs)

    with pytest.raises(ValueError):
        DirFileSystem("path", fs, asynchronous=True)


@pytest.mark.parametrize(
    "root, rel, full",
    [
        ("", "", ""),
        ("", "foo", "foo"),
        ("root", "", "root"),
        ("root", "foo", "root/foo"),
    ],
)
def test_path(fs, root, rel, full):
    dirfs = DirFileSystem(root, fs)
    assert dirfs._join(rel) == full
    assert dirfs._relpath(full) == rel


def test_sep(mocker, dirfs):
    sep = mocker.Mock()
    dirfs.fs.sep = sep
    assert dirfs.sep == sep


@pytest.mark.asyncio
async def test_set_session(mocker, adirfs):
    adirfs.fs.set_session = mocker.AsyncMock()
    assert (
        await adirfs.set_session(*ARGS, **KWARGS) == adirfs.fs.set_session.return_value
    )
    adirfs.fs.set_session.assert_called_once_with(*ARGS, **KWARGS)


@pytest.mark.asyncio
async def test_async_rm_file(adirfs):
    await adirfs._rm_file("file", **KWARGS)
    adirfs.fs._rm_file.assert_called_once_with(f"{PATH}/file", **KWARGS)


def test_rm_file(dirfs):
    dirfs.rm_file("file", **KWARGS)
    dirfs.fs.rm_file.assert_called_once_with("path/to/dir/file", **KWARGS)


@pytest.mark.asyncio
async def test_async_rm(adirfs):
    await adirfs._rm("file", *ARGS, **KWARGS)
    adirfs.fs._rm.assert_called_once_with("path/to/dir/file", *ARGS, **KWARGS)


def test_rm(dirfs):
    dirfs.rm("file", *ARGS, **KWARGS)
    dirfs.fs.rm.assert_called_once_with("path/to/dir/file", *ARGS, **KWARGS)


@pytest.mark.asyncio
async def test_async_cp_file(adirfs):
    await adirfs._cp_file("one", "two", **KWARGS)
    adirfs.fs._cp_file.assert_called_once_with(f"{PATH}/one", f"{PATH}/two", **KWARGS)


def test_cp_file(dirfs):
    dirfs.cp_file("one", "two", **KWARGS)
    dirfs.fs.cp_file.assert_called_once_with(f"{PATH}/one", f"{PATH}/two", **KWARGS)


@pytest.mark.asyncio
async def test_async_copy(adirfs):
    await adirfs._copy("one", "two", *ARGS, **KWARGS)
    adirfs.fs._copy.assert_called_once_with(
        f"{PATH}/one", f"{PATH}/two", *ARGS, **KWARGS
    )


def test_copy(dirfs):
    dirfs.copy("one", "two", *ARGS, **KWARGS)
    dirfs.fs.copy.assert_called_once_with(f"{PATH}/one", f"{PATH}/two", *ARGS, **KWARGS)


@pytest.mark.asyncio
async def test_async_pipe(adirfs):
    await adirfs._pipe("file", *ARGS, **KWARGS)
    adirfs.fs._pipe.assert_called_once_with(f"{PATH}/file", *ARGS, **KWARGS)


def test_pipe(dirfs):
    dirfs.pipe("file", *ARGS, **KWARGS)
    dirfs.fs.pipe.assert_called_once_with(f"{PATH}/file", *ARGS, **KWARGS)


@pytest.mark.asyncio
async def test_async_pipe_file(adirfs):
    await adirfs._pipe_file("file", *ARGS, **KWARGS)
    adirfs.fs._pipe_file.assert_called_once_with(f"{PATH}/file", *ARGS, **KWARGS)


def test_pipe_file(dirfs):
    dirfs.pipe_file("file", *ARGS, **KWARGS)
    dirfs.fs.pipe_file.assert_called_once_with(f"{PATH}/file", *ARGS, **KWARGS)


@pytest.mark.asyncio
async def test_async_cat_file(adirfs):
    assert (
        await adirfs._cat_file("file", *ARGS, **KWARGS)
        == adirfs.fs._cat_file.return_value
    )
    adirfs.fs._cat_file.assert_called_once_with(f"{PATH}/file", *ARGS, **KWARGS)


def test_cat_file(dirfs):
    assert dirfs.cat_file("file", *ARGS, **KWARGS) == dirfs.fs.cat_file.return_value
    dirfs.fs.cat_file.assert_called_once_with(f"{PATH}/file", *ARGS, **KWARGS)


@pytest.mark.asyncio
async def test_async_cat(adirfs):
    assert await adirfs._cat("file", *ARGS, **KWARGS) == adirfs.fs._cat.return_value
    adirfs.fs._cat.assert_called_once_with(f"{PATH}/file", *ARGS, **KWARGS)


def test_cat(dirfs):
    assert dirfs.cat("file", *ARGS, **KWARGS) == dirfs.fs.cat.return_value
    dirfs.fs.cat.assert_called_once_with(f"{PATH}/file", *ARGS, **KWARGS)


@pytest.mark.asyncio
async def test