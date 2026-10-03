import bz2
import gzip
import os
import os.path
import pickle
import posixpath
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import patch

import pytest

import fsspec
from fsspec import compression
from fsspec.core import OpenFile, get_fs_token_paths, open_files
from fsspec.implementations.local import LocalFileSystem, make_path_posix
from fsspec.tests.test_utils import WIN

files = {
    ".test.accounts.1.json": (
        b'{"amount": 100, "name": "Alice"}\n'
        b'{"amount": 200, "name": "Bob"}\n'
        b'{"amount": 300, "name": "Charlie"}\n'
        b'{"amount": 400, "name": "Dennis"}\n'
    ),
    ".test.accounts.2.json": (
        b'{"amount": 500, "name": "Alice"}\n'
        b'{"amount": 600, "name": "Bob"}\n'
        b'{"amount": 700, "name": "Charlie"}\n'
        b'{"amount": 800, "name": "Dennis"}\n'
    ),
}

csv_files = {
    ".test.fakedata.1.csv": (b"a,b\n1,2\n"),
    ".test.fakedata.2.csv": (b"a,b\n3,4\n"),
}
odir = os.getcwd()


@pytest.fixture()
def cwd():
    pth = os.getcwd().replace("\\", "/")
    assert not pth.endswith("/")
    yield pth


@pytest.fixture()
def current_drive(cwd):
    drive = os.path.splitdrive(cwd)[0]
    assert not drive or (len(drive) == 2 and drive.endswith(":"))
    yield drive


@pytest.fixture()
def user_home():
    pth = os.path.expanduser("~").replace("\\", "/")
    assert not pth.endswith("/")
    yield pth


def winonly(*args):
    return pytest.param(*args, marks=pytest.mark.skipif(not WIN, reason="Windows only"))


def posixonly(*args):
    return pytest.param(*args, marks=pytest.mark.skipif(WIN, reason="Posix only"))


@contextmanager
def filetexts(d, open=open, mode="t"):
    """Dumps a number of textfiles to disk

    d - dict
        a mapping from filename to text like {'a.csv': '1,1\n2,2'}

    Since this is meant for use in tests, this context manager will
    automatically switch to a temporary current directory, to avoid
    race conditions when running tests in parallel.
    """
    dirname = tempfile.mkdtemp()
    try:
        os.chdir(dirname)
        for filename, text in d.items():
            if dirname := os.path.dirname(filename):
                os.makedirs(dirname, exist_ok=True)
            f = open(filename, f"w{mode}")
            try:
                f.write(text)
            finally:
                try:
                    f.close()
                except AttributeError:
                    pass

        yield list(d)

        for filename in d:
            if os.path.exists(filename):
                try:
                    os.remove(filename)
                except OSError:
                    pass
    finally:
        os.chdir(odir)


def test_urlpath_inference_strips_protocol(tmpdir):
    tmpdir = make_path_posix(str(tmpdir))
    paths = ["/".join([tmpdir, f"test.{i:02d}.csv"]) for i in range(20)]

    for path in paths:
        with open(path, "wb") as f:
            f.write(b"1,2,3\n" * 10)

    # globstring
    protocol = "file:///" if sys.platform == "win32" else "file://"
    urlpath = protocol + os.path.join(tmpdir, "test.*.csv")
    _, _, paths2 = get_fs_token_paths(urlpath)
    assert paths2 == paths

    # list of paths
    _, _, paths2 = get_fs_token_paths([protocol + p for p in paths])
    assert paths2 == paths


def test_urlpath_inference_errors():
    # Empty list
    with pytest.raises(ValueError) as err:
       