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
    assert not drive or (len(drive) == 2 and drive.endswith