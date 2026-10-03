//
//  m3_api_tracer.c
//
//  Created by Volodymyr Shymanskyy on 02/18/20.
//  Copyright © 2020 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_api_tracer.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasTracer)


static FILE* trace = NULL;

m3ApiRawFunction(m3_env_log_execution)
{
    m3ApiGetArg      (uint32_t, id)
    fprintf(trace, "exec;%d\n", id);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_env_log_exec_enter)
{
    m3ApiGetArg      (uint32_t, id)
    m3ApiGetArg      (uint32_t, func)
    fprintf(trace, "enter;%d;%d\n", id, func);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_env_log_exec_exit)
{
    m3ApiGetArg      (uint32_t, id)
    m3ApiGetArg      (uint32_t, func)
    fprintf(trace, "exit;%d;%d\n", id, func);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_env_log_exec_loop)
{
    m3ApiGetArg      (uint32_t, id)
    fprintf(trace, "loop;%d\n", id);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_env_load_ptr)
{
    m3ApiReturnType (uint32_t)
    m3ApiGetArg      (uint32_t, id)
    m3ApiGetArg      (uint32_t, align)
    m3ApiGetArg      (uint32_t, offset)
    m3ApiGetArg      (uint32_t, address)
    fprintf(trace, "load ptr;%d;%d;%d;%d\n", id, align, offset, address);
    m3ApiReturn(address);
}

m3ApiRawFunction(m3_env_store_ptr)
{
    m3ApiReturnType (uint32_t)
    m3ApiGetArg      (uint32_t, id)
    m3ApiGetArg      (uint32_t, align)
    m3ApiGetArg      (uint32_t, offset)
    m3ApiGetArg      (uint32_t, address)
    fprintf(trace, "store ptr;%d;%d;%d;%d\n", id, align, offset, address);
    m3ApiReturn(address);
}


#define d_m3TraceMemory(FUNC, NAME, TYPE, FMT)                \
m3ApiRawFunction(m3_env_##FUNC)                               \
{                                                             \
    m3ApiReturnType (TYPE)                                    \
    m3ApiGetArg      (uint32_t, id)                           \
    m3ApiGetArg      (TYPE,     val)                          \
