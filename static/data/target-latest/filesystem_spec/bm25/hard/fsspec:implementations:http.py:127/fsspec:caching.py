from __future__ import annotations

import collections
import functools
import logging
import math
import os
import threading
import warnings
from concurrent.futures import Future, ThreadPoolExecutor
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    ClassVar,
    Generic,
    NamedTuple,
    Optional,
    OrderedDict,
    TypeVar,
)

if TYPE_CHECKING:
    import mmap

    from typing_extensions import ParamSpec

    P = ParamSpec("P")
else:
    P = TypeVar("P")

T = TypeVar("T")


logger = logging.getLogger("fsspec")

Fetcher = Callable[[int, int], bytes]  # Maps (start, end) to bytes


class BaseCache:
    """Pass-though cache: doesn't keep anything, calls every time

    Acts as base class for other cachers

    Parameters
    ----------
    blocksize: int
        How far to read ahead in numbers of bytes
    fetcher: func
        Function of the form f(start, end) which gets bytes from remote as
        specified
    size: int
        How big this file is
    """

    name: ClassVar[str] = "none"

    def __init__(self, blocksize: int, fetcher: Fetcher, size: int) -> None:
        self.blocksize = blocksize
        self.nblocks = 0
        self.fetcher = fetcher
        self.size = size
        self.hit_count = 0
        self.miss_count = 0
        # the bytes that we actually requested
        self.total_requested_bytes = 0

    def _fetch(self, start: int | None, stop: int | None) -> bytes:
        if start is None:
            start = 0
        if stop is None:
            stop = self.size
        if start >= self.size or start >= stop:
            return b""
        return self.fetcher(start, stop)

    def _reset_stats(self) -> None:
        """Reset hit and miss counts for a more ganular report e.g. by file."""
        self.hit_count = 0
        self.miss_count = 0
        self.total_requested_bytes = 0

    def _log_stats(self) -> str:
        """Return a formatted string of the cache statistics."""
        if self.hit_count == 0 and self.miss_count == 0:
            # a cache that does nothing, this is for logs only
            return ""
        return " , %s: %d hits, %d misses, %d total requested bytes" % (
            self.name,
            self.hit_count,
            self.miss_count,
            self.total_requested_bytes,
        )

    def __repr__(self) -> str:
        # TODO: use rich for better formatting
        return f"""
        <{self.__class__.__name__}:
            block size  :   {self.blocksize}
            block count :   {self.nblocks}
            file size   :   {self.size}
            cache hits  :   {self.hit_count}
            cache misses:   {self.miss_count}
            total requested bytes: {self.total_requested_bytes}>
        """


class MMapCache(BaseCache):
    """memory-mapped sparse file cache

    Opens temporary file, which is filled blocks-wise when data is requested.
    Ensure there is enough disc space in the temporary location.

    This cache method might only work on posix
    """

    name = "mmap"

    def __init__(
        self,
        blocksize: int,
        fetcher: Fetcher,
        size: int,
        location: str | None = None,
        blocks: set[int] | None = None,
    ) -> None:
        super().__init__(blocksize, fetcher, size)
        self.blocks = set() if blocks is None else blocks
        self.location = location
        self.cache = self._makefile()

    def _makefile(self) -> mmap.mmap | bytearray:
        import mmap
        import tempfile

        if self.size == 0:
            return bytearray()

        # posix version
        if self.location is None or not os.path.exists(self.location):
            if self.location is None:
                fd = tempfile.TemporaryFile()
                self.blocks = set()
            else:
                fd = open(self.location, "wb+")
            fd.seek(self.size - 1)
            fd.write(b"1")
            fd.flush()
        else:
            fd = open(self.location, "r+b")

        return mmap.mmap(fd.fileno(), self.size)

    def _fetch(self, start: int | None, end: int | None) -> bytes:
        logger.debug(f"MMap cache fetching {start}-{end}")
        if start is None:
            start = 0
        if end is None:
            end = self.size
        if start >= self.size or start >= end:
            return b""
        start_block = start // self.blocksize
        end_block = end // self.blocksize
        need = [i for i in range(start_block, end_block + 1) if i not in self.blocks]
        hits = [i for i in range(start_block, end_block + 1) if i in self.blocks]
        self.miss_count += len(need)
        self.hit_count += len(hits)
        while need:
            # TODO: not a for loop so we can consolidate blocks later to
            # make fewer fetch calls; this could be parallel
            i = need.pop(0)

            sstart = i * self.blocksize
            send = min(sstart + self.blocksize, self.size)
            self.total_requested_bytes += send - sstart
            logger.debug(f"MMap get block #{i} ({sstart}-{send})")
            self.cache[sstart:send] = self.fetcher(sstart, send)
            self.blocks.add(i)

        return self.cache[start:end]

    def __getstate__(self) -> dict[str, Any]:
        state = self.__dict__.copy()
        # Remove the unpicklable entries.
        del state["cache"]
        return state

    def __setstate__(self, state: dict[str, Any]) -> None:
        # Restore instance attributes
        self.__dict__.update(state)
        self.cache = self._makefile()


class ReadAheadCache(BaseCache):
    """Cache which reads only when we get beyond a block of data

    This is a much simpler version of BytesCache, and does not attempt to
    fill holes in the cache or keep fragments alive. It is best suited to
    many small reads in a sequential order (e.g., reading lines from a file).
    """

    name = "readahead"

    def __init__(self, blocksize: int, fetcher: Fetcher, size: int) -> None:
        super().__init__(blocksize, fetcher, size)
        self.cache = b""
        self.start = 0
        self.end = 0

    def _fetch(self, start: int | None, end: int | None) -> bytes:
        if start is None:
            start = 0
        if end is None or end > self.size:
            end = self.size
        if start >= self.size or start >= end:
            return b""
        l = end - start
        if start >= self.start and end <= self.end:
            # cache hit
            self.hit_count += 1
            return self.cache[start - self.start : end - self.start]
        elif self.start <= start < self.end:
            # partial hit
            self.miss_count += 1
            part = self.cache[start - self.start :]
            l -= len(part)
            start = self.end
        else:
            # miss
            self.miss_count += 1
            part = b""
        end = min(self.size, end + self.blocksize)
        self.total_requested_bytes += end - start
        self.cache = self.fetcher(start, end)  # new block replaces old
        self.start = start
        self.end = self.start + len(self.cache)
        return part + self.cache[:l]


class FirstChunkCache(BaseCache):
    """Caches the first block of a file only

    This may be useful for file types where the metadata is stored in the header,
    but is randomly accessed.
    """

    name = "first"

    def __init__(self, blocksize: int, fetcher: Fetcher, size: int) -> None:
        if blocksize > size:
            # this will buffer the whole thing
            blocksize = size
        super().__init__(blocksize, fetcher, size)
        self.cache: bytes | None = None

    def _fetch(self, start: int | None, end: int | None) -> bytes:
        start = start or 0
        if start > self.size:
            logger.debug("FirstChunkCache: requested start > file size")
            return b""

        end = min(end, self.size)

        if start < self.blocksize:
            if self.cache is None:
                self.miss_count += 1
                if end > self.blocksize:
                    self.total_requested_bytes += end
                    data = self.fetcher(0, end)
                    self.cache = data[: self.blocksize]
                    return data[start:]
                self.cache = self.fetcher(0, self.blocksize)
                self.total_requested_bytes += self.blocksize
            part = self.cache[start:end]
            if end > self.blocksize:
                self.total_requested_bytes += end - self.blocksize
                part += self.fetcher(self.blocksize, end)
            self.hit_count += 1
            return part
        else:
            self.miss_count += 1
            self.total_requested_bytes += end - start
            return self.fetcher(start, end)


class BlockCache(BaseCache):
    """
    Cache holding memory as a set of blocks.

    Requests are only ever made ``blocksize`` at a time, and are
    stored in an LRU cache. The least recently accessed block is
    discarded when more than ``maxblocks`` are stored.

    Parameters
    ----------
    blocksize : int
        The number of bytes to store in each block.
        Requests are only ever made for ``blocksize``, so this
        should balance the overhead of making a request against
        the granularity of the blocks.
    fetcher : Callable
    size : int
        The total size of the file being cached.
    maxblocks : int
        The maximum number of blocks to cache for. The maximum memory
        use for this cache is then ``blocksize * maxblocks``.
    """

    name = "blockcache"

    def __init__(
        self, blocksize: int, fetcher: Fetcher, size: int, maxblocks: int = 32
    ) -> None:
        super().__init__(blocksize, fetcher, size)
        self.nblocks = math.ceil(size / blocksize)
        self.maxblocks = maxblocks
        self._fetch_block_cached = functools.lru_cache(maxblocks)(self._fetch_block)

    def cache_info(self):
        """
        The statistics on the block cache.

        Returns
        -------
        NamedTuple
            Returned directly from the LRU Cache used internally.
        """
        return self._fetch_block_cached.cache_info()

    def __getstate__(self) -> dict[str, Any]:
        state = self.__dict__
        del state["_fetch_block_cached"]
        return state

    def __setstate__(self, state: dict[str, Any]) -> None:
        self.__dict__.update(state)
        self._fetch_block_cached = functools.lru_cache(state["maxblocks"])(
            self._fetch_block
        )

    def _fetch(self, start: int | None, end: int | None) -> bytes:
        if start is None:
            start = 0
        if end is None:
            end = self.size
        if start >= self.size or start >= end:
            return b""

        # byte position -> block numbers
        start_block_number = start // self.blocksize
        end_block_number = end // self.blocksize

        # these are cached, so safe to do multiple calls for the same start and end.
        for block_number in range(start_block_number, end_block_number + 1):
            self._fetch_block_cached(block_number)

        return self._read_cache(
            start,
            end,
            start_block_number=start_block_number,
            end_block_number=end_block_number,
        )

    def _fetch_block(self, block_number: int) -> bytes:
        """
        Fetch the block of data for `block_number`.
        """
        if block_number > self.nblocks:
            raise ValueError(
                f"'block_number={block_number}' is greater than "
                f"the number of blocks ({self.nblocks})"
            )

        start = block_number * self.blocksize
        end = start + self.blocksize
        self.total_requested_bytes += end - start
        self.miss_count += 1
        logger.info("BlockCache fetching block %d", block_number)
        block_contents = super()._fetch(start, end)
        return block_contents

    def _read_cache(
        self, start: int, end: int, start_block_number: int, end_block_number: int
    ) -> bytes:
        """
        Read from our block cache.

        Parameters
        ----------
        start, end : int
            The start and end byte positions.
        start_block_number, end_block_number : int
            The start and end block numbers.
        """
        start_pos = start % self.blocksize
        end_pos = end % self.blocksize

        self.hit_count += 1
        if start_block_number == end_block_number:
            block: bytes = self._fetch_block_cached(start_block_number)
            return block[start_pos:end_pos]

        else:
            # read from the initial
            out = [self._fetch_block_cached(start_block_number)[start_pos:]]

            # intermediate blocks
            # Note: it'd be nice to combine these into one big request. However
            # that doesn't play nicely with our LRU cache.
            out.extend(
                map(
                    self._fetch_block_cached,
                    range(start_block_number + 1, end_block_number),
                )
            )

            # final block
            out.append(self._fetch_block_cached(end_block_number)[:end_pos])

            return b"".join(out)


class BytesCache(BaseCache):
    """Cache which holds data in a in-memory bytes object

    Implements read-ahead by the block size, for semi-random reads progressing
    through the file.

    Parameters
    ----------
    trim: bool
        As we read more data, whether to discard the start of the buffer when
        we are more than a blocksize ahead of it.
    """

    name: ClassVar[str] = "bytes"

    def __init__(
        self, blocksize: int, fetcher: Fetcher, size: int, trim: bool = True
    ) -> None:
        super().__init__(blocksize, fetcher, size)
        self.cache = b""
        self.start: int | None = None
        self.end: int | None = None
        self.trim = trim

    def _fetch(self, start: int | None, end: int | None) -> bytes:
        # TODO: only set start/end after fetch, in case it fails?
        # is this where retry logic might go?
        if start is None:
            start = 0
        if end is None:
            end = self.size
        if start >= self.size or start >= end:
            return b""
        if (
            self.start is not None
            and start >= self.start
            and self.end is not None
            and end < self.end
        ):
            # cache hit: we have all the required data
            offset = start - self.start
            self.hit_count += 1
            return self.cache[offset : offset + end - start]

        if self.blocksize:
            bend = min(self.size, end + self.blocksize)
        else:
            bend = end

        if bend == start or start > self.size:
            return b""

        if (self.start is None or start < self.start) and (
            self.end is None or end > self.end
        ):
            # First read, or extending both before and after
            self.total_requested_bytes += bend - start
            self.miss_count += 1
            self.cache = self.fetcher(start, bend)
            self.start = start
        else:
            assert self.start is not None
            assert self.end is not None
            self.miss_count += 1

            if start < self.start:
                if self.end is None or self.end - end > self.blocksize:
                    self.total_requested_bytes += bend - start
                    self.cache = self.fetcher(start, bend)
                    self.start = start
                else:
                    self.total_requested_bytes += self.start - start
                    new = self.fetcher(start, self.start)
                    self.start = start
                    self.cache = new + self.cache
            elif self.end is not None and bend > self.end:
                if self.end > self.size:
                    pass
                elif end - self.end > self.blocksize:
                    self.total_requested_bytes += bend - start
                    self.cache = self.fetcher(start, bend)
                    self.start = start
                else:
                    self.total_requested_bytes += bend - self.end
                    new = self.fetcher(self.end, bend)
                    self.cache = self.cache + new

        self.end = self.start + len(self.cache)
        offset = start - self.start
        out = self.cache[offset : offset + end - start]
        if self.trim:
            num = (self.end - self.start) // (self.blocksize + 1)
            if num > 1:
                self.start += self.blocksize * num
                self.cache = self.cache[self.blocksize * num :]
        return out

    def __len__(self) -> int:
        return len(self.cache)


class AllBytes(BaseCache):
    """Cache entire contents of the file"""

    name: ClassVar[str] = "all"

    def __init__(
        self,
        blocksize: int | None = None,
        fetcher: Fetcher | None = None,
        size: int | None = None,
        data: bytes | None = None,
    ) -> None:
        super().__init__(blocksize, fetcher, size)  # type: ignore[arg-type]
        if data is None:
            self.miss_count += 1
            self.total_requested_bytes += self.size
            data = self.fetcher(0, self.size)
        self.data = data

    def _fetch(self, start: int | None, stop: int | None) -> bytes:
        self.hit_count += 1
        return self.data[start:stop]


class KnownPartsOfAFile(BaseCache):
    """
    Cache holding known file parts.

    Parameters
    ----------
    blocksize: int
        How far to read ahead in numbers of bytes
    fetcher: func
        Function of the form f(start, end) which gets bytes from remote as
        specified
    size: int
        How big this file is
    data: dict
        A dictionary mapping explicit `(start, stop)` file-offset tuples
        with known bytes.
    strict: bool, default True
        Whether to fetch reads that go beyond a known byte-range boundary.
        If `False`, any read that ends outside a known part will be zero
        padded. Note that zero padding will not be used for reads that
        begin outside a known byte-range.
    """

    name: ClassVar[str] = "parts"

    def __init__(
        self,
        blocksize: int,
        fetcher: Fetcher,
        size: int,
        data: Optional[dict[tuple[int, int], bytes]] = None,
        strict: bool = True,
        **_: Any,
    ):
        super().__init__(blocksize, fetcher, size)
        self.strict = strict

        # simple consolidation of contiguous blocks
        if data:
            old_offsets = sorted(data.keys())
            offsets = [old_offsets[0]]
            blocks = [data.pop(old_offsets[0])]
            for start, stop in old_offsets[1:]:
                start0, stop0 = offsets[-1]
                if