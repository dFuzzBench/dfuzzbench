//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include "wm_include.h"

#include "m3/wasm3.h"

#include "m3/extra/fib32.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

unsigned int millis(void)
{
    return tls_os_get_time()*2;
}

void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    uint32_t fsize = fib32_wasm_len;

    printf("Loading WebAssembly...\n");
    IM3Environment env = m3_NewEnvironment ();
