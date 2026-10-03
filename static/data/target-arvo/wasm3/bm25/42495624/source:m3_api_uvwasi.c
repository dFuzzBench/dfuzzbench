//
//  m3_api_uvwasi.c
//
//  Created by Colin J. Ihrig on 4/20/20.
//  Copyright © 2020 Colin J. Ihrig, Volodymyr Shymanskyy. All rights reserved.
//

#define _POSIX_C_SOURCE 200809L

#include "m3_api_wasi.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasUVWASI)

#include <stdio.h>
#include <string.h>

#include "uvwasi.h"

#ifdef __APPLE__
# include <crt_externs.h>
# define environ (*_NSGetEnviron())
#elif !defined(_MSC_VER)
extern char** environ;
#endif

static m3_wasi_context_t* wasi_context;
static uvwasi_t uvwasi;

typedef struct wasi_iovec_t
{
    uvwasi_size_t buf;
    uvwasi_size_t buf_len;
} wasi_iovec_t;

/*
 * WASI API implementation
 */

m3ApiRawFunction(m3_wasi_unstable_args_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t *           , argv)
    m3ApiGetArgMem   (char *               , argv_buf)

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) { m3ApiReturn(UVWASI_EINVAL); }

    m3ApiCheckMem(argv, context->argc * sizeof(uint32_t));

    for (u32 i = 0; i < context->argc; ++i)
    {
        m3ApiWriteMem32(&argv[i], m3ApiPtrToOffset(argv_buf));

        size_t len = strlen (context->argv[i]);

        m3ApiCheckMem(argv_buf, len);
        memcpy (argv_buf, context->argv[i], len);
        argv_buf += len;
        * argv_buf++ = 0;
    }

    m3ApiReturn(UVWASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_args_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uvwasi_size_t *      , argc)
    m3ApiGetArgMem   (uvwasi_size_t *      , argv_buf_size)

    m3ApiCheckMem(argc,             sizeof(uvwasi_size_t));
    m3ApiCheckMem(argv_buf_size,    sizeof(uvwasi_size_t));

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) { m3ApiReturn(UVWASI_EINVAL); }

    uvwasi_size_t buf_len = 0;
    for (u32 i = 0; i < context->argc; ++i)
    {
        buf_len += strlen (context->argv[i]) + 1;
    }

    m3ApiWriteMem32(argc, context->argc);
    m3ApiWriteMem32(argv_buf_size, buf_len);

    m3ApiReturn(UVWASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_environ_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t *           , env)
    m3ApiGetArgMem   (char *               , env_buf)

    char **environment;
    uvwasi_errno_t ret;
    uvwasi_size_t env_count, env_buf_size;

    ret = uvwasi_environ_sizes_get(&uvwasi, &env_count, &env_buf_size);
    if (ret != UVWASI_ESUCCESS) {
        m3ApiReturn(ret);
    }

    m3ApiCheckMem(env,      env_count * sizeof(uint32_t));
    m3ApiCheckMem(env_buf,  env_buf_size);

    environment = calloc(env_count, sizeof(char *));
    if (environment == NULL) {
        m3ApiReturn(UVWASI_ENOMEM);
    }

    ret = uvwasi_environ_get(&uvwasi, environment, env_buf);
    if (ret != UVWASI_ESUCCESS) {
        free(environment);
        m3ApiReturn(ret);
    }

    uint32_t environ_buf_offset = m3ApiPtrToOffset(env_buf);

    for (u32 i = 0; i < env_count; ++i)
    {
        uint32_t offset = environ_buf_offset +
                          (environment[i] - environment[0]);
        m3ApiWriteMem32(&env[i], offset);
    }

    free(environment);
    m3ApiReturn(UVWASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_environ_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uvwasi_size_t *      , env_count)
    m3ApiGetArgMem   (uvwasi_size_t *      , env_buf_size)

    m3ApiCheckMem(env_count,    sizeof(uvwasi_size_t));
    m3ApiCheckMem(env_buf_size, sizeof(uvwasi_size_t));

    uvwasi_size_t count;
    uvwasi_size_t buf_size;

    uvwasi_errno_t ret = uvwasi_environ_sizes_get(&uvwasi, &count, &buf_size);

    m3ApiWriteMem32(env_count,    count);
    m3ApiWriteMem32(env_buf_size, buf_size);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_dir_name)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (char *               , path)
    m3ApiGetArg      (uvwasi_size_t        , path_len)

    m3ApiCheckMem(path, path_len);

    uvwasi_errno_t ret = uvwasi_fd_prestat_dir_name(&uvwasi, fd, path, path_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (uint32_t *           , buf)

    m3ApiCheckMem(buf, 2*sizeof(uint32_t));

    uvwasi_prestat_t prestat;
    uvwasi_errno_t ret;

    ret = uvwasi_fd_prestat_get(&uvwasi, fd, &prestat);
    if (ret != UVWASI_ESUCCESS) {
        m3ApiReturn(ret);
    }

    m3ApiWriteMem32(buf, prestat.pr_type);
    m3ApiWriteMem32(buf+1, prestat.u.dir.pr_name_len);
    m3ApiReturn(UVWASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (uint8_t *            , fdstat)

    m3ApiCheckMem(fdstat, 24);

    uvwasi_fdstat_t stat;
    uvwasi_errno_t ret = uvwasi_fd_fdstat_get(&uvwasi, fd, &stat);

    if (ret != UVWASI_ESUCCESS) {
        m3ApiReturn(ret);
    }

    m3ApiWriteMem16(fdstat, stat.fs_filetype);
    m3ApiWriteMem16(fdstat+2, stat.fs_flags);
    m3ApiWriteMem64(fdstat+8, stat.fs_rights_base);
    m3ApiWriteMem64(fdstat+16, stat.fs_rights_inheriting);
    m3ApiReturn(UVWASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_set_flags)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArg      (uvwasi_fdflags_t     , flags)

    uvwasi_errno_t ret = uvwasi_fd_fdstat_set_flags(&uvwasi, fd, flags);

    m3ApiReturn(ret);
}

// TODO: Remove this at some point
m3ApiRawFunction(m3_wasi_unstable_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArg      (uvwasi_filedelta_t   , offset)
    m3ApiGetArg      (uint32_t             , wasi_whence)
    m3ApiGetArgMem   (uvwasi_filesize_t *  , result)

    m3ApiCheckMem(result, sizeof(uvwasi_filesize_t));

    uvwasi_whence_t whence = wasi_whence;

    switch (wasi_whence) {
    case 0: whence = UVWASI_WHENCE_CUR; break;
    case 1: whence = UVWASI_WHENCE_END; break;
    case 2: whence = UVWASI_WHENCE_SET; break;
    default:                m3ApiReturn(UVWASI_EINVAL);
    }

    uvwasi_errno_t ret = uvwasi_fd_seek(&uvwasi, fd, offset, whence, result);

    //TODO: m3ApiWriteMem

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_snapshot_preview1_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArg      (uvwasi_filedelta_t   , offset)
    m3ApiGetArg      (uint32_t             , whence)
    m3ApiGetArgMem   (uvwasi_filesize_t *  , result)

    m3ApiCheckMem(result, sizeof(uvwasi_filesize_t));

    uvwasi_errno_t ret = uvwasi_fd_seek(&uvwasi, fd, offset, whence, result);

    //TODO: m3ApiWriteMem

    m3ApiReturn(ret);
}


m3ApiRawFunction(m3_wasi_unstable_path_open)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , dirfd)
    m3ApiGetArg      (uvwasi_lookupflags_t , dirflags)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (uvwasi_size_t        , path_len)
    m3ApiGetArg      (uvwasi_oflags_t      , oflags)
    m3ApiGetArg      (uvwasi_rights_t      , fs_rights_base)
    m3ApiGetArg      (uvwasi_rights_t      , fs_rights_inheriting)
    m3ApiGetArg      (uvwasi_fdflags_t     , fs_flags)
    m3ApiGetArgMem   (uvwasi_fd_t *        , fd)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(fd,   sizeof(uvwasi_fd_t));

    uvwasi_errno_t ret = uvwasi_path_open(&uvwasi,
                                 dirfd,
                                 dirflags,
                                 path,
                                 path_len,
                                 oflags,
                                 fs_rights_base,
                                 fs_rights_inheriting,
                                 fs_flags,
                                 fd);

    //TODO: m3ApiWriteMem

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_path_filestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArg      (uvwasi_lookupflags_t , flags)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (uint32_t             , path_len)
    m3ApiGetArgMem   (uvwasi_filestat_t *  , buf)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(buf,  sizeof(uvwasi_filestat_t));

    uvwasi_errno_t ret = uvwasi_path_filestat_get(&uvwasi, fd, flags, path, path_len, buf);

    //TODO: m3ApiWriteMem

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_read)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (wasi_iovec_t *       , wasi_iovs)
    m3ApiGetArg      (uvwasi_size_t        , iovs_len)
    m3ApiGetArgMem   (uvwasi_size_t *      , nread)

    m3ApiCheckMem(wasi_iovs,    iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nread,        sizeof(uvwasi_size_t));

#if defined(M3_COMPILER_MSVC)
    if (iovs_len > 32) m3ApiReturn(UVWASI_EINVAL);
    uvwasi_ciovec_t  iovs[32];
#else
    if (iovs_len > 128) m3ApiReturn(UVWASI_EINVAL);
    uvwasi_ciovec_t  iovs[iovs_len];
#endif
    uvwasi_size_t num_read;
    uvwasi_errno_t ret;

    for (uvwasi_size_t i = 0; i < iovs_len; ++i) {
        iovs[i].buf = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iovs[i].buf));
        iovs[i].buf_len = m3ApiReadMem32(&wasi_iovs[i].buf_len);
    }

    ret = uvwasi_fd_read(&uvwasi, fd, (const uvwasi_iovec_t *) iovs, iovs_len, &num_read);
    m3ApiWriteMem32(nread, num_read);
    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_write)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (wasi_iovec_t *       , wasi_iovs)
    m3ApiGetArg      (uvwasi_size_t        , iovs_len)
    m3ApiGetArgMem   (uvwasi_size_t *      , nwritten)

    m3ApiCheckMem(wasi_iovs,    iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nwritten,     sizeof(uvwasi_size_t));

#if defined(M3_COMPILER_MSVC)
    if (iovs_len > 32) m3ApiReturn(UVWASI_EINVAL);
    uvwasi_ciovec_t  iovs[32];
#else
    if (iovs_len > 128) m3ApiReturn(UVWASI_EINVAL);
    uvwasi_ciovec_t  iovs[iovs_len];
#endif
    uvwasi_size_t num_written;
    uvwasi_errno_t ret;

    for (uvwasi_size_t i = 0; i < iovs_len; ++i) {
        iovs[i].buf = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iovs[i].buf));
        iovs[i].buf_len = m3ApiReadMem32(&wasi_iovs[i].buf_len);
    }

    ret = uvwasi_fd_write(&uvwasi, fd, iovs, iovs_len, &num_written);
    m3ApiWriteMem32(nwritten, num_written);
    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_readdir)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (void *               , buf)
    m3ApiGetArg      (uvwasi_size_t        , buf_len)
    m3ApiGetArg      (uvwasi_dircookie_t   , cookie)
    m3ApiGetArgMem   (uvwasi_size_t *      , bufused)

    m3ApiCheckMem(buf,      buf_len);
    m3ApiCheckMem(bufused,  sizeof(uvwasi_size_t));

    uvwasi_size_t uvbufused;
    uvwasi_errno_t ret = uvwasi_fd_readdir(&uvwasi, fd, buf, buf_len, cookie, &uvbufused);

    m3ApiWriteMem32(bufused, uvbufused);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_close)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t, fd)

    uvwasi_errno_t ret = uvwasi_fd_close(&uvwasi, fd);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_datasync)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t, fd)

    uvwasi_errno_t ret = uvwasi_fd_datasync(&uvwasi, fd);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_random_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint8_t *            , buf)
    m3ApiGetArg      (uvwasi_size_t        , buf_len)

    m3ApiCheckMem(buf, buf_len);

    uvwasi_errno_t ret = uvwasi_random_get(&uvwasi, buf, buf_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_clock_res_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_clockid_t     , wasi_clk_id)
    m3ApiGetArgMem   (uvwasi_timestamp_t * , resolution)

    m3ApiCheckMem(resolution, sizeof(uvwasi_timestamp_t));

    uvwasi_errno_t ret = uvwasi_clock_res_get(&uvwasi, wasi_clk_id, resolution);

    //TODO: m3ApiWriteMem64

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_clock_time_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_clockid_t     , wasi_clk_id)
    m3ApiGetArg      (uvwasi_timestamp_t   , precision)
    m3ApiGetArgMem   (uvwasi_timestamp_t * , time)

    m3ApiCheckMem(time, sizeof(uvwasi_timestamp_t));

    uvwasi_errno_t ret = uvwasi_clock_time_get(&uvwasi, wasi_clk_id, precision, time);

