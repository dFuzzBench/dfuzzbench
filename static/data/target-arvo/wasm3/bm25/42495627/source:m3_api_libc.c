//
//  m3_api_libc.c
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#define _POSIX_C_SOURCE 200809L

#include "m3_api_libc.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_exception.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>

typedef uint32_t wasm_ptr_t;
typedef uint32_t wasm_size_t;

m3ApiRawFunction(m3_libc_abort)
{
    m3ApiTrap(m3Err_trapAbort);
}

m3ApiRawFunction(m3_libc_exit)
{
    m3ApiGetArg     (int32_t, code)

    m3ApiTrap(m3Err_trapExit);
}


m3ApiRawFunction(m3_libc_memset)
{
    m3ApiReturnType (int32_t)

    m3ApiGetArgMem  (void*,           i_ptr)
    m3ApiGetArg     (int32_t,         i_value)
    m3ApiGetArg     (wasm_size_t,     i_size)

    m3ApiCheckMem(i_ptr, i_size);

    u32 result = m3ApiPtrToOffset(memset (i_ptr, i_value, i_size));
    m3ApiReturn(result);
}

m3ApiRawFunction(m3_libc_memmove)
{
    m3ApiReturnType (int32_t)

    m3ApiGetArgMem  (void*,           o_dst)
    m3ApiGetArgMem  (void*,           i_src)
    m3ApiGetArg     (wasm_size_t,     i_size)

    m3ApiCheckMem(o_dst, i_size);
    m3ApiCheckMem(i_src, i_size);

