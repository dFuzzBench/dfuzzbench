from .. import filesystem
from ..asyn import AsyncFileSystem


class DirFileSystem(AsyncFileSystem):
    """Directory prefix filesystem

    The DirFileSystem is a filesystem-wrapper. It assumes every path it is dealing with
    is relative to the `path`. After performing the necessary paths operation it
    delegates everything to the wrapped filesystem.
    """

    protocol = "dir"

    def __init__(
        self,
        path=None,
        fs=None,
        fo=None,
        target_protocol=None,
        target_options=None,
        **storage_options,
    ):
        """
        Parameters
        ----------
        path: str
            Path to the directory.
        fs: AbstractFileSystem
            An instantiated filesystem to wrap.
        target_protocol, target_options:
            if fs is none, construct it from these
        fo: str
            Alternate for path; do not provide both
        """
        super().__init__(**storage_options)
        if fs is None:
            fs = filesystem(protocol=target_protocol, **(target_options or {}))
        if (path is not None) ^ (fo is not None) is False:
            raise ValueError("Provide path or fo, not both")
        path = path or fo

        if self.asynchronous and not fs.async_impl:
            raise ValueError("can't use asynchronous with non-async fs")

        if fs.async_impl and self.asynchronous != fs.asynchronous:
            raise ValueError("both dirfs and fs should be in the same sync/async mode")

        self.path = fs._strip_protocol(path)
        self.fs = fs

    def _join(self, path):
        if isinstance(path, str):
            if not self.path:
                return path
            if not path:
                return self.path
            return self.fs.sep.join((self.path, self._strip_protocol(path)))
        return [self._join(_path) for _path in path]

    def _relpath(self, path):
        if isinstance(path, str):
            if not self.path:
                return path
            if path == self.path:
                return ""
            prefix = self.path + self.fs.sep
            assert path.startswith(prefix)
            return path[len(prefix) :]
        return [self._relpath(_path) for _path in path]

    # Wrappers below

    @property
    def sep(self):
        return self.fs.sep

    async def set_session(self, *args, **kwargs):
        return await self.fs.set_session(*args, **kwargs)

    async def _rm_file(self, path, **kwargs):
        return await self.fs._rm_file(self._join(path), **kwargs)

    def rm_file(self, path, **kwargs):
        return self.fs.rm_file(self._join(path), **kwargs)

    async def _rm(self, path, *args, **kwargs):
        return await self.fs._rm(self._join(path), *args, **kwargs)

    def rm(self, path, *args, **kwargs):
        return self.fs.rm(self._join(path), *args, **kwargs)

    async def _cp_file(self, path1, path2, **kwargs):
        return await self.fs._cp_file(self._join(path1), self._join(path2), **kwargs)

    def cp_file(self, path1, path2, **kwargs):
        return self.fs.cp_file(self._join(path1), self._join(path2), **kwargs)

    async def _copy(
        self,
        path1,
        path2,
        *args,
        **kwargs,
    ):
        return await self.fs._copy(
            self._join(path1),
            self._join(path2),
            *args,
            **kwargs,
        )

    def copy(self, path1, path2, *args, **kwargs):
        return self.fs.copy(
            self._join(path1),
            self._join(path2),
            *args,
            **kwargs,
        )

    async def _pipe(self, path, *args, **kwargs):
        return await self.fs._pipe(self._join(path), *args, **kwargs)

    def pipe(self, path, *args, **kwargs):
        return self.fs.pipe(self._join(path), *args, **kwargs)

    async def _pipe_file(self, path, *args, **kwargs):
        return await self.fs._pipe_file(self._join(path), *args, **kwargs)

    def pipe_file(self, path, *args, **kwargs):
        return self.fs.pipe_file(self._join(path), *args, **kwargs)

    async def _cat_file(self, path, *args, **kwargs):
        return await self.fs._cat_file(self._join(path), *args, **kwargs)

    def cat_file(self, path, *args, **kwargs):
        return self.fs.cat_file(self._join(path), *args, **kwargs)

    async def _cat(self, path, *args, **kwargs):
        ret = await self.fs._cat(
            self._join(path),
            *args,
            **kwargs,
        )

        if isinstance(ret, dict):
            return {self._relpath(key): value for key, value in ret.items()}

        return ret

    def cat(self, path, *args, **kwargs):
        ret = self.fs.cat(
            self._join(path),
            *args,
            **kwargs,
        )

        if isinstance(ret, dict):
            return {self._relpath(key): value for key, value in ret.items()}

        return ret

    async def _put_file(self, lpath, rpath, **kwargs):
        return await self.fs._put_file(lpath, self._join(rpath), **kwargs)

    def put_file(self, lpath, rpath, **kwargs):
        return self.fs.put_file(lpath, self._join(rpath), **kwargs)

    async def _put(
        self,
        lpath,
        rpath,
        *args,
        **kwargs,
    ):
        return await self.fs._put(
           