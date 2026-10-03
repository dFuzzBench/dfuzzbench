/**
 * THIS FILE IS AUTO-GENERATED from the following files:
 *   typenames.witx, wasi_snapshot_preview1.witx
 *
 * @file
 * This file describes the [WASI] interface, consisting of functions, types,
 * and defined values (macros).
 *
 * The interface described here is greatly inspired by [CloudABI]'s clean,
 * thoughtfully-designed, capability-oriented, POSIX-style API.
 *
 * [CloudABI]: https://github.com/NuxiNL/cloudlibc
 * [WASI]: https://github.com/WebAssembly/WASI/
 */

/*
 * File origin:
 *   https://github.com/CraneStation/wasi-libc/blob/master/libc-bottom-half/headers/public/wasi/api.h
 * Revision:
 *   2c2fc9a2fddd0927a66f1c142e65c8dab6f5c5d7
 * License:
 *   CC0 1.0 Universal (CC0 1.0) Public Domain Dedication
 *   https://creativecommons.org/publicdomain/zero/1.0/
 */

#ifndef __wasi_api_h
#define __wasi_api_h

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
// Hacks alert
#undef _Static_assert
#define _Static_assert(...)
#undef _Noreturn
#define _Noreturn
#endif

_Static_assert(_Alignof(int8_t) == 1, "non-wasi data layout");
_Static_assert(_Alignof(uint8_t) == 1, "non-wasi data layout");
_Static_assert(_Alignof(int16_t) == 2, "non-wasi data layout");
_Static_assert(_Alignof(uint16_t) == 2, "non-wasi data layout");
_Static_assert(_Alignof(int32_t) == 4, "non-wasi data layout");
_Static_assert(_Alignof(uint32_t) == 4, "non-wasi data layout");
_Static_assert(_Alignof(int64_t) == 8, "non-wasi data layout");
_Static_assert(_Alignof(uint64_t) == 8, "non-wasi data layout");
// _Static_assert(_Alignof(void*) == 4, "non-wasi data layout");

#ifdef __cplusplus
extern "C" {
#endif

// TODO: Encoding this in witx.
#define __WASI_DIRCOOKIE_START (UINT64_C(0))
// typedef __SIZE_TYPE__ __wasi_size_t;
typedef uint32_t __wasi_size_t; // fixme for wasm64

// _Static_assert(sizeof(__wasi_size_t) == 4, "witx calculated size");
// _Static_assert(_Alignof(__wasi_size_t) == 4, "witx calculated align");

/**
 * Non-negative file size or length of a region within a file.
 */
typedef uint64_t __wasi_filesize_t;

_Static_assert(sizeof(__wasi_filesize_t) == 8, "witx calculated size");
_Static_assert(_Alignof(__wasi_filesize_t) == 8, "witx calculated align");

/**
 * Timestamp in nanoseconds.
 */
typedef uint64_t __wasi_timestamp_t;

_Static_assert(sizeof(__wasi_timestamp_t) == 8, "witx calculated size");
_Static_assert(_Alignof(__wasi_timestamp_t) == 8, "witx calculated align");

/**
 * Identifiers for clocks.
 */
typedef uint32_t __wasi_clockid_t;

/**
 * The clock measuring real time. Time value zero corresponds with
 * 1970-01-01T00:00:00Z.
 */
#define __WASI_CLOCKID_REALTIME (UINT32_C(0))

/**
 * The store-wide monotonic clock, which is defined as a clock measuring
 * real time, whose value cannot be adjusted and which cannot have negative
 * clock jumps. The epoch of this clock is undefined. The absolute time
 * value of this clock therefore has no meaning.
 */
#define __WASI_CLOCKID_MONOTONIC (UINT32_C(1))

/**
 * The CPU-time clock associated with the current process.
 */
#define __WASI_CLOCKID_PROCESS_CPUTIME_ID (UINT32_C(2))

/**
 * The CPU-time clock associated with the current thread.
 */
#define __WASI_CLOCKID_THREAD_CPUTIME_ID (UINT32_C(3))

_Static_assert(sizeof(__wasi_clockid_t) == 4, "witx calculated size");
_Static_assert(_Alignof(__wasi_clockid_t) == 4, "witx calculated align");

/**
 * Error codes returned by functions.
 * Not all of these error codes are returned by the functions provided by this
 * API; some are used in higher-level library layers, and others are provided
 * merely for alignment with POSIX.
 */
typedef uint16_t __wasi_errno_t;

/**
 * No error occurred. System call completed successfully.
 */
#define __WASI_ERRNO_SUCCESS (UINT16_C(0))

/**
 * Argument list too long.
 */
#define __WASI_ERRNO_2BIG (UINT16_C(1))

/**
 * Permission denied.
 */
#define __WASI_ERRNO_ACCES (UINT16_C(2))

/**
 * Address in use.
 */
#define __WASI_ERRNO_ADDRINUSE (UINT16_C(3))

/**
 * Address not available.
 */
#define __WASI_ERRNO_ADDRNOTAVAIL (UINT16_C(4))

/**
 * Address family not supported.
 */
#define __WASI_ERRNO_AFNOSUPPORT (UINT16_C(5))

/**
 * Resource unavailable, or operation would block.
 */
#define __WASI_ERRNO_AGAIN (UINT16_C(6))

/**
 * Connection already in progress.
 */
#define __WASI_ERRNO_ALREADY (UINT16_C(7))

/**
 * Bad file descriptor.
 */
#define __WASI_ERRNO_BADF (UINT16_C(8))

/**
 * Bad message.
 */
#define __WASI_ERRNO_BADMSG (UINT16_C(9))

/**
 * Device or resource busy.
 */
#define __WASI_ERRNO_BUSY (UINT16_C(10))

/**
 * Operation canceled.
 */
#define __WASI_ERRNO_CANCELED (UINT16_C(11))

/**
 * No child processes.
 */
#define __WASI_ERRNO_CHILD (UINT16_C(12))

/**
 * Connection aborted.
 */
#define __WASI_ERRNO_CONNABORTED (UINT16_C(13))

/**
 * Connection refused.
 */
#define __WASI_ERRNO_CONNREFUSED (UINT16_C(14))

/**
 * Connection reset.
 */
#define __WASI_ERRNO_CONNRESET (UINT16_C(15))

/**
 * Resource deadlock would occur.
 */
#define __WASI_ERRNO_DEADLK (UINT16_C(16))

/**
 * Destination address required.
 */
#define __WASI_ERRNO_DESTADDRREQ (UINT16_C(17))

/**
 * Mathematics argument out of domain of function.
 */
#define __WASI_ERRNO_DOM (UINT16_C(18))

/**
 * Reserved.
 */
#define __WASI_ERRNO_DQUOT (UINT16_C(19))

/**
 * File exists.
 */
#define __WASI_ERRNO_EXIST (UINT16_C(20))

/**
 * Bad address.
 */
#define __WASI_ERRNO_FAULT (UINT16_C(21))

/**
 * File too large.
 */
#define __WASI_ERRNO_FBIG (UINT16_C(22))

/**
 * Host is unreachable.
 */
#define __WASI_ERRNO_HOSTUNREACH (UINT16_C(23))

/**
 * Identifier removed.
 */
#define __WASI_ERRNO_IDRM (UINT16_C(24))

/**
 * Illegal byte sequence.
 */
#define __WASI_ERRNO_ILSEQ (UINT16_C(25))

/**
 * Operation in progress.
 */
#define __WASI_ERRNO_INPROGRESS (UINT16_C(26))

/**
 * Interrupted function.
 */
#define __WASI_ERRNO_INTR (UINT16_C(27))

/**
 * Invalid argument.
 */
#define __WASI_ERRNO_INVAL (UINT16_C(28))

/**
 * I/O error.
 */
#define __WASI_ERRNO_IO (UINT16_C(29))

/**
 * Socket is connected.
 */
#define __WASI_ERRNO_ISCONN (UINT16_C(30))

/**
 * Is a directory.
 */
#define __WASI_ERRNO_ISDIR (UINT16_C(31))

/**
 * Too many levels of symbolic links.
 */
#define __WASI_ERRNO_LOOP (UINT16_C(32))

/**
 * File descriptor value too large.
 */
#define __WASI_ERRNO_MFILE (UINT16_C(33))

/**
 * Too many links.
 */
#define __WASI_ERRNO_MLINK (UINT16_C(34))

/**
 * Message too large.
 */
#define __WASI_ERRNO_MSGSIZE (UINT16_C(35))

/**
 * Reserved.
 */
#define __WASI_ERRNO_MULTIHOP (UINT16_C(36))

/**
 * Filename too long.
 */
#define __WASI_ERRNO_NAMETOOLONG (UINT16_C(37))

/**
 * Network is down.
 */
#define __WASI_ERRNO_NETDOWN (UINT16_C(38))

/**
 * Connection aborted by network.
 */
#define __WASI_ERRNO_NETRESET (UINT16_C(39))

/**
 * Network unreachable.
 */
#define __WASI_ERRNO_NETUNREACH (UINT16_C(40))

/**
 * Too many files open in system.
 */
#define __WASI_ERRNO_NFILE (UINT16_C(41))

/**
 * No buffer space available.
 */
#define __WASI_ERRNO_NOBUFS (UINT16_C(42))

/**
 * No such device.
 */
#define __WASI_ERRNO_NODEV (UINT16_C(43))

/**
 * No such file or directory.
 */
#define __WASI_ERRNO_NOENT (UINT16_C(44))

/**
 * Executable file format error.
 */
#define __WASI_ERRNO_NOEXEC (UINT16_C(45))

/**
 * No locks available.
 */
#define __WASI_ERRNO_NOLCK (UINT16_C(46))

/**
 * Reserved.
 */
#define __WASI_ERRNO_NOLINK (UINT16_C(47))

/**
 * Not enough space.
 */
#define __WASI_ERRNO_NOMEM (UINT16_C(48))

/**
 * No message of the desired type.
 */
#define __WASI_ERRNO_NOMSG (UINT16_C(49))

/**
 * Protocol not available.
 */
#define __WASI_ERRNO_NOPROTOOPT (UINT16_C(50))

/**
 * No space left on device.
 */
#define __WASI_ERRNO_NOSPC (UINT16_C(51))

/**
 * Function not supported.
 */
#define __WASI_ERRNO_NOSYS (UINT16_C(52))

/**
 * The socket is not connected.
 */
#define __WASI_ERRNO_NOTCONN (UINT16_C(53))

/**
 * Not a directory or a symbolic link to a directory.
 */
#define __WASI_ERRNO_NOTDIR (UINT16_C(54))

/**
 * Directory not empty.
 */
#define __WASI_ERRNO_NOTEMPTY (UINT16_C(55))

/**
 * State not recoverable.
 */
#define __WASI_ERRNO_NOTRECOVERABLE (UINT16_C(56))

/**
 * Not a socket.
 */
#define __WASI_ERRNO_NOTSOCK (UINT16_C(57))

/**
 * Not supported, or operation not supported on socket.
 */
#define __WASI_ERRNO_NOTSUP (UINT16_C(58))

/**
 * Inappropriate I/O control operation.
 */
#define __WASI_ERRNO_NOTTY (UINT16_C(59))

/**
 * No such device or address.
 */
#define __WASI_ERRNO_NXIO (UINT16_C(60))

/**
 * Value too large to be stored in data type.
 */
#define __WASI_ERRNO_OVERFLOW (UINT16_C(61))

/**
 * Previous owner died.
 */
#define __WASI_ERRNO_OWNERDEAD (UINT16_C(62))

/**
 * Operation not permitted.
 */
#define __WASI_ERRNO_PERM (UINT16_C(63))

/**
 * Broken pipe.
 */
#define __WASI_ERRNO_PIPE (UINT16_C(64))

/**
 * Protocol error.
 */
#define __WASI_ERRNO_PROTO (UINT16_C(65))

/**
 * Protocol not supported.
 */
#define __WASI_ERRNO_PROTONOSUPPORT (UINT16_C(66))

/**
 * Protocol wrong type for socket.
 */
#define __WASI_ERRNO_PROTOTYPE (UINT16_C(67))

/**
 * Result too large.
 */
#define __WASI_ERRNO_RANGE (UINT16_C(68))

/**
 * Read-only file system.
 */
#define __WASI_ERRNO_ROFS (UINT16_C(69))

/**
 * Invalid seek.
 */
#define __WASI_ERRNO_SPIPE (UINT16_C(70))

/**
 * No such process.
 */
#define __WASI_ERRNO_SRCH (UINT16_C(71))

/**
 * Reserved.
 */
#define __WASI_ERRNO_STALE (UINT16_C(72))

/**
 * Connection timed out.
 */
#define __WASI_ERRNO_TIMEDOUT (UINT16_C(73))

/**
 * Text file busy.
 */
#define __WASI_ERRNO_TXTBSY (UINT16_C(74))

/**
 * Cross-device link.
 */
#define __WASI_ERRNO_XDEV (UINT16_C(75))

/**
 * Extension: Capabilities insufficient.
 */
#define __WASI_ERRNO_NOTCAPABLE (UINT16_C(76))

_Static_assert(sizeof(__wasi_errno_t) == 2, "witx calculated size");
_Static_assert(_Alignof(__wasi_errno_t) == 2, "witx calculated align");

/**
 * File descriptor rights, determining which actions may be performed.
 */
typedef uint64_t __wasi_rights_t;

/**
 * The right to invoke `fd_datasync`.
 * If `path_open` is set, includes the right to invoke
 * `path_open` with `fdflags::dsync`.
 */
#define __WASI_RIGHTS_FD_DATASYNC (UINT64_C(1))

/**
 * The right to invoke `fd_read` and `sock_recv`.
 * If `rights::fd_seek` is set, includes the right to invoke `fd_pread`.
 */
#define __WASI_RIGHTS_FD_READ (UINT64_C(2))

/**
 * The right to invoke `fd_seek`. This flag implies `rights::fd_tell`.
 */
#define __WASI_RIGHTS_FD_SEEK (UINT64_C(4))

/**
 * The right to invoke `fd_fdstat_set_flags`.
 */
#define __WASI_RIGHTS_FD_FDSTAT_SET_FLAGS (UINT64_C(8))

/**
 * The right to invoke `fd_sync`.
 * If `path_open` is set, includes the right to invoke
 * `path_open` with `fdflags::rsync` and `fdflags::dsync`.
 */
#define __WASI_RIGHTS_FD_SYNC (UINT64_C(16))

/**
 * The right to invoke `fd_seek` in such a way that the file offset
 * remains unaltered (i.e., `whence::cur` with offset zero), or to
 * invoke `fd_tell`.
 */
#define __WASI_RIGHTS_FD_TELL (UINT64_C(32))

/**
 * The right to invoke `fd_write` and `sock_send`.
 * If `rights::fd_seek` is set, includes the right to invoke `fd_pwrite`.
 */
#define __WASI_RIGHTS_FD_WRITE (UINT64_C(64))

/**
 * The right to invoke `fd_advise`.
 */
#define __WASI_RIGHTS_FD_ADVISE (UINT64_C(128))

/**
 * The right to invoke `fd_allocate`.
 */
#define __WASI_RIGHTS_FD_ALLOCATE (UINT64_C(256))

/**
 * The right to invoke `path_create_directory`.
 */
#define __WASI_RIGHTS_PATH_CREATE_DIRECTORY (UINT64_C(512))

/**
 * If `path_open` is set, the right to invoke `path_open` with `oflags::creat`.
 */
#define __WASI_RIGHTS_PATH_CREATE_FILE (UINT64_C(1024))

/**
 * The right to invoke `path_link` with the file descriptor as the
 * source directory.
 */
#define __WASI_RIGHTS_PATH_LINK_SOURCE (UINT64_C(2048))

/**
 * The right to invoke `path_link` with the file descriptor as the
 * target directory.
 */
#define __WASI_RIGHTS_PATH_LINK_TARGET (UINT64_C(4096))

/**
 * The right to invoke `path_open`.
 */
#define __WASI_RIGHTS_PATH_OPEN (UINT64_C(8192))

/**
 * The right to invoke `fd_readdir`.
 */
#define __WASI_RIGHTS_FD_READDIR (UINT64_C(16384))

/**
 * The right to invoke `path_readlink`.
 */
#define __WASI_RIGHTS_PATH_READLINK (UINT64_C(32768))

/**
 * The right to invoke `path_rename` with the file descriptor as the source directory.
 */
#define __WASI_RIGHTS_PATH_RENAME_SOURCE (UINT64_C(65536))

/**
 * The right to invoke `path_rename` with the file descriptor as the target directory.
 */
#define __WASI_RIGHTS_PATH_RENAME_TARGET (UINT64_C(131072))

/**
 * The right to invoke `path_filestat_get`.
 */
#define __WASI_RIGHTS_PATH_FILESTAT_GET (UINT64_C(262144))

/**
 * The right to change a file's size (there is no `path_filestat_set_size`).
 * If `path_open` is set, includes the right to invoke `path_open` with `oflags::trunc`.
 */
#define __WASI_RIGHTS_PATH_FILESTAT_SET_SIZE (UINT64_C(524288))

/**
 * The right to invoke `path_filestat_set_times`.
 */
#define __WASI_RIGHTS_PATH_FILESTAT_SET_TIMES (UINT64_C(1048576))

/**
 * The right to invoke `fd_filestat_get`.
 */
#define __WASI_RIGHTS_FD_FILESTAT_GET (UINT64_C(2097152))

/**
 * The right to invoke `fd_filestat_set_size`.
 */
#define __WASI_RIGHTS_FD_FILESTAT_SET_SIZE (UINT64_C(4194304))

/**
 * The right to invoke `fd_filestat_set_times`.
 */
#define __WASI_RIGHTS_FD_FILESTAT_SET_TIMES (UINT64_C(8388608))

/**
 * The right to invoke `path_symlink`.
 */
#define __WASI_RIGHTS_PATH_SYMLINK (UINT64_C(16777216))

/**
 * The right to invoke `path_remove_directory`.
 */
#define __WASI_RIGHTS_PATH_REMOVE_DIRECTORY (UINT64_C(33554432))

/**
 * The right to invoke `path_unlink_file`.
 */
#define __WASI_RIGHTS_PATH_UNLINK_FILE (UINT64_C(67108864))

/**
 * If `rights::fd_read` is set, includes the right to invoke `poll_oneoff` to subscribe to `eventtype::fd_read`.
 * If `rights::fd_write` is set, includes the right to invoke `poll_oneoff` to subscribe to `eventtype::fd_write`.
 */
#define __WASI_RIGHTS_POLL_FD_READWRITE (UINT64_C(134217728))

/**
 * The right to invoke `sock_shutdown`.
 */
#define __WASI_RIGHTS_SOCK_SHUTDOWN (UINT64_C(268435456))

_Static_assert(sizeof(__wasi_rights_t) == 8, "witx calculated size");
_Static_assert(_Alignof(__wasi_rights_t) == 8, "witx calculated align");

/**
 * A file descriptor index.
 */
typedef uint32_t __wasi_fd_t;

_Static_assert(sizeof(__wasi_fd_t) == 4, "witx calculated size");
_Static_assert(_Alignof(__wasi_fd_t) == 4, "witx calculated align");

/**
 * A region of memory for scatter/gather reads.
 */
typedef struct __wasi_iovec_t {
    /**
     * The address of the buffer to be filled.
     */
    uint8_t * buf;

    /**
     * The length of the buffer to be filled.
     */
    __wasi_size_t buf_len;

} __wasi_iovec_t;

// _Static_assert(sizeof(__wasi_iovec_t) == 8, "witx calculated size");
// _Static_assert(_Alignof(__wasi_iovec_t) == 4, "witx calculated align");
// _Static_assert(offsetof(__wasi_iovec_t, buf) == 0, "witx calculated offset");
// _Static_assert(offsetof(__wasi_iovec_t, buf_len) == 4, "witx calculated offset");

/**
 * A region of memory for scatter/gather writes.
 */
typedef struct __wasi_ciovec_t {
    /**
     * The address of the buffer to be written.
     */
    const uint8_t * buf;

    /**
     * The length of the buffer to be written.
     */
    __wasi_size_t buf_len;

} __wasi_ciovec_t;

// _Static_assert(sizeof(__wasi_ciovec_t) == 8, "witx calculated size");
// _Static_assert(_Alignof(__wasi_ciovec_t) == 4, "witx calculated align");
// _Static_assert(offsetof(__wasi_ciovec_t, buf) == 0, "witx calculated offset");
// _Static_assert(offsetof(__wasi_ciovec_t, buf_len) == 4, "witx calculated offset");

/**
 * Relative offset within a file.
 */
typedef int64_t __wasi_filedelta_t;

_Static_assert(sizeof(__wasi_filedelta_t) == 8, "witx calculated size");
_Static_assert(_Alignof(__wasi_filedelta_t) == 8, "witx calculated align");

/**
 * The position relative to which to set the offset of the file descriptor.
 */
typedef uint8_t __wasi_whence_t;

/**
 * Seek relative to start-of-file.
 */
#define __WASI_WHENCE_SET (UINT8_C(0))

/**
 * Seek relative to current position.
 */
#define __WASI_WHENCE_CUR (UINT8_C(1))

/**
 * Seek relative to end-of-file.
 */
#define __WASI_WHENCE_END (UINT8_C(2))

_Static_assert(sizeof(__wasi_whence_t) == 1, "witx calculated size");
_Static_assert(_Alignof(__wasi_whence_t) == 1, "witx calculated align");

/**
 * A reference to the offset of a directory entry.
 *
 * The value 0 signifies the start of the directory.
 */
typedef uint64_t __wasi_dircookie_t;

_Static_assert(sizeof(__wasi_dircookie_t) == 8, "witx calculated size");
_Static_assert(_Alignof(__wasi_dircookie_t) == 8, "witx calculated align");

/**
 * The type for the $d_namlen field of $dirent.
 */
typedef uint32_t __wasi_dirnamlen_t;

_Static_assert(sizeof(__wasi_dirnamlen_t) == 4, "witx calculated size");
_Static_assert(_Alignof(__wasi_dirnamlen_t) == 4, "witx calculated align");

/**
 * File serial number that is unique within its file system.
 */
typedef uint64_t __wasi_inode_t;

_Static_assert(sizeof(__wasi_inode_t) == 8, "witx calculated size");
_Static_assert(_Alignof(__wasi_inode_t) == 8, "witx calculated align");

/**
 * The type of a file descriptor or file.
 */
typedef uint8_t __wasi_filetype_t;

/**
 * The type of the file descriptor or file is unknown or is different from any of the other types specified.
 */
#define __WASI_FILETYPE_UNKNOWN (UINT8_C(0))

/**
 * The file descriptor or file refers to a block device inode.
 */
#define __WASI_FILETYPE_BLOCK_DEVICE (UINT8_C(1))

/**
 * The file descriptor or file refers to a character device inode.
 */
#define __WASI_FILETYPE_CHARACTER_DEVICE (UINT8_C(2))

/**
 * The file descriptor or file refers to a directory inode.
 */
#define __WASI_FILETYPE_DIRECTORY (UINT8_C(3))

/**
 * The file descriptor or file refers to a regular file inode.
 */
#define __WASI_FILETYPE_REGULAR_FILE (UINT8_C(4))

/**
 * The file descriptor or file refers to a datagram socket.
 */
#define __WASI_FILETYPE_SOCKET_DGRAM (UINT8_C(5))

/**
 * The file descriptor or file refers to a byte-stream socket.
 */
