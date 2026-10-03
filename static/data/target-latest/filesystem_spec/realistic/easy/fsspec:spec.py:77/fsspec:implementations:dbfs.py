import base64
import urllib

import requests
import requests.exceptions
from requests.adapters import HTTPAdapter, Retry

from fsspec import AbstractFileSystem
from fsspec.spec import AbstractBufferedFile


class DatabricksException(Exception):
    """
    Helper class for exceptions raised in this module.
    """

    def __init__(self, error_code, message):
        """Create a new DatabricksException"""
        super().__init__(message)

        self.error_code = error_code
        self.message = message


class DatabricksFileSystem(AbstractFileSystem):
    """
    Get access to the Databricks filesystem implementation over HTTP.
    Can be used inside and outside of a databricks cluster.
    """

    def __init__(self, instance, token, **kwargs):
        """
        Create a new DatabricksFileSystem.

        Parameters
        ----------
        instance: str
            The instance URL of the databricks cluster.
            For example for an Azure databricks cluster, this
            has the form adb-<some-number>.<two digits>.azuredatabricks.net.
        token: str
            Your personal token. Find out more
            here: https://docs.databricks.com/dev-tools/api/latest/authentication.html
        """
        self.instance = instance
        self.token = token
        self.session = requests.Session()
        self.retries = Retry(
            total=10,
            backoff_factor=0.05,
            status_forcelist=[408, 429, 500, 502, 503, 504],
        )

        self.session.mount("https://", HTTPAdapter(max_retries=self.retries))
        self.session.headers.update({"Authorization": f"Bearer {self.token}"})

        super().__init__(**kwargs)

    def ls(self, path, detail=True, **kwargs):
        """
        List the contents of the given path.

        Parameters
        ----------
        path: str
            Absolute path
        detail: bool
            Return not only the list of filenames,
            but also additional information on file sizes
            and types.
        """
        out = self._ls_from_cache(path)
        if not out:
            try:
                r = self._send_to_api(
                    method="get", endpoint="list", json={"path": path}
                )
            except DatabricksException as e:
                if e.error_code == "RESOURCE_DOES_NOT_EXIST":
                    raise FileNotFoundError(e.message)

                raise e
            files = r["files"]
            out = [
                {
                    "name": o["path"],
                    "type": "directory" if o["is_dir"] else "file",
                    "size": o["file_size"],
                }
                for o in files
            ]
            self.dircache[path] = out

        if detail:
            return out
        return [o["name"] for o in out]

    def makedirs(self, path, exist_ok=True):
        """
        Create a given absolute path and all of its parents.

        Parameters
        ----------
        path: str
            Absolute path to create
        exist_ok: bool
            If false, checks if the folder
            exists before creating it (and raises an
            Exception if this is the case)
        """
        if not exist_ok:
            try:
                # If the following succeeds, the path is already present
                self._send_to_api(
                    method="get", endpoint="get-status", json={"path": path}
                )
                raise FileExistsError(f"Path {path} already exists")
            except DatabricksException as e:
                if e.error_code == "RESOURCE_DOES_NOT_EXIST":
                    pass

        try:
            self._send_to_api(method="post", endpoint="mkdirs", json={"path": path})
        except DatabricksException as e:
            if e.error_code == "RESOURCE_ALREADY_EXISTS":
                raise FileExistsError(e.message)

            raise e
        self.invalidate_cache(self._parent(path))

    def mkdir(self, path, create_parents=True, **kwargs):
        """
        Create a given absolute path and all of its parents.

        Parameters
        ----------
        path: str
            Absolute path to create
        create_parents: bool
            Whether to create all parents or not.
            "False" is not implemented so far.
        """
        if not create_parents:
            raise NotImplementedError

        self.mkdirs(path, **kwargs)

    def rm(self, path, recursive=False, **kwargs):
        """
        Remove the file or folder at the given absolute path.

        Parameters
        ----------
        path: str
            Absolute path what to remove
        recursive: bool
            Recursively delete all files in a folder.
        """
        try:
            self._send_to_api(
                method="post",
                endpoint="delete",
                json={"path": path, "recursive": recursive},
            )
        except DatabricksException as e:
            # This is not really an exception, it just means
            # not everything was deleted so far
            if e.error_code == "PARTIAL_DELETE":
                self.rm(path=path, recursive=recursive)
            elif e.error_code == "IO_ERROR":
                # Using the same exception as the os module would use here
                raise OSError(e.message)

            raise e
        self.invalidate_cache(self._parent(path))

    def mv(
        self, source_path, destination_path, recursive=False, maxdepth=None, **kwargs
    ):
        """
        Move a source to a destination path.

        A note from the original [databricks API manual]
        (https://docs.databricks.com/dev-tools/api/latest/dbfs.html#move).

        When moving a large number of files the API call will time out after
        approximately 60s, potentially resulting in partially moved data.
        Therefore, for operations that move more than 10k files, we strongly
        discourage using the DBFS REST API.

        Parameters