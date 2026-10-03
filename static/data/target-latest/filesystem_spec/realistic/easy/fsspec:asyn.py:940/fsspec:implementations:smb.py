"""
This module contains SMBFileSystem class responsible for handling access to
Windows Samba network shares by using package smbprotocol
"""

import datetime
import uuid
from stat import S_ISDIR, S_ISLNK

import smbclient

from .. import AbstractFileSystem
from ..utils import infer_storage_options

# ! pylint: disable=bad-continuation


class SMBFileSystem(AbstractFileSystem):
    """Allow reading and writing to Windows and Samba network shares.

    When using `fsspec.open()` for getting a file-like object the URI
    should be specified as this format:
    ``smb://workgroup;user:password@server:port/share/folder/file.csv``.

    Example::

        >>> import fsspec
        >>> with fsspec.open(
        ...     'smb://myuser:mypassword@myserver.com/' 'share/folder/file.csv'
        ... ) as smbfile:
        ...     df = pd.read_csv(smbfile, sep='|', header=None)

    Note that you need to pass in a valid hostname or IP address for the host
    component of the URL. Do not use the Windows/NetBIOS machine name for the
    host component.

    The first component of the path in the URL points to the name of the shared
    folder. Subsequent path components will point to the directory/folder/file.

    The URL components ``workgroup`` , ``user``, ``password`` and ``port`` may be
    optional.

    .. note::

        For working this source require `smbprotocol`_ to be installed, e.g.::

            $ pip install smbprotocol
            # or
            # pip install smbprotocol[kerberos]

    .. _smbprotocol: https://github.com/jborean93/smbprotocol#requirements

    Note: if using this with the ``open`` or ``open_files``, with full URLs,
    there is no way to tell if a path is relative, so all paths are assumed
    to be absolute.
    """

    protocol = "smb"

    # pylint: disable=too-many-arguments
    def __init__(
        self,
        host,
        port=None,
        username=None,
        password=None,
        timeout=60,
        encrypt=None,
        share_access=None,
        register_session_retries=5,
        auto_mkdir=False,
        **kwargs,
    ):
        """
        You can use _get_kwargs_from_urls to get some kwargs from
        a reasonable SMB url.

        Authentication will be anonymous or integrated if username/password are not
        given.

        Parameters
        ----------
        host: str
            The remote server name/ip to connect to
        port: int or None
            Port to connect with. Usually 445, sometimes 139.
        username: str or None
            Username to connect with. Required if Kerberos auth is not being used.
        password: str or None
            User's password on the server, if using username
        timeout: int
            Connection timeout in seconds
        encrypt: bool
            Whether to force encryption or not, once this has been set to True
            the session cannot be changed back to False.
        share_access: str or None
            Specifies the default access applied to file open operations
            performed with this file system object.
            This affects whether other processes can concurrently open a handle
            to the same file.

            - None (the default): exclusively locks the file until closed.
            - 'r': Allow other