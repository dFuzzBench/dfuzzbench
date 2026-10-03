//
//  m3_api_meta_wasi.c
//
//  Created by Volodymyr Shymanskyy on 01/08/20.
//  Copyright © 2020 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_api_wasi.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasMetaWASI)

// NOTE: MetaWASI mostly redirects WASI calls to the host WASI environment

#if !defined(__wasi__)
# error "MetaWASI is only supported on WASI target"
#endif

#if __has_include("wasi/api.h")
# include <wasi/api.h>

#elif __has_include("wasi/core.h")
# warning "Using legacy WASI headers"
# include <wasi/core.h>
# define __WASI_ERRNO_SUCCESS   __WASI_ESUCCESS
# define __WASI_ERRNO_INVAL     __WASI_EINVAL

#else
# error "Missing WASI headers"
#endif

static m3_wasi_context_t* wasi_context;

typedef size_t __wasi_size_t;

static inline
void copy_iov_to_host(void* _mem, __wasi_iovec_t* host_iov, __wasi_iovec_t* wasi_iov, int32_t iovs_len)
{
    // Convert wasi memory offsets to host addresses
    for (int i = 0; i < iovs_len; i++) {
        host_iov[i].buf = m3ApiOffsetToPtr(wasi_iov[i].buf);
        host_iov[i].buf_len  = wasi_iov[i].buf_len;
    }
}

/*
 * WASI API implementation
 */

m3ApiRawFunction(m3_wasi_generic_args_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t *           , argv)
    m3ApiGetArgMem   (char *               , argv_buf)

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) { m3ApiReturn(__WASI_ERRNO_INVAL); }

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

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_args_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (__wasi_size_t *      , argc)
    m3ApiGetArgMem   (__wasi_size_t *      , argv_buf_size)

    m3ApiCheckMem(argc,             sizeof(__wasi_size_t));
    m3ApiCheckMem(argv_buf_size,    sizeof(__wasi_size_t));

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) { m3ApiReturn(__WASI_ERRNO_INVAL); }

    __wasi_size_t buf_len = 0;
    for (u32 i = 0; i < context->argc; ++i)
    {
        buf_len += strlen (context->argv[i]) + 1;
    }

    m3ApiWriteMem32(argc, context->argc);
    m3ApiWriteMem32(argv_buf_size, buf_len);

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_environ_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t *           , env)
    m3ApiGetArgMem   (char *               , env_buf)

    __wasi_errno_t ret;
    __wasi_size_t env_count, env_buf_size;

    ret = __wasi_environ_sizes_get(&env_count, &env_buf_size);
    if (ret != __WASI_ERRNO_SUCCESS) m3ApiReturn(ret);

