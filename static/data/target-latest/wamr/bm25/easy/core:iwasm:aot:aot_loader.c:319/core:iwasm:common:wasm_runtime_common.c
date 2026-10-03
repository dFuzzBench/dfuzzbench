/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "bh_platform.h"
#include "bh_common.h"
#include "bh_assert.h"
#include "bh_log.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"
#include "wasm_memory.h"
#if WASM_ENABLE_INTERP != 0
#include "../interpreter/wasm_runtime.h"
#endif
#if WASM_ENABLE_AOT != 0
#include "../aot/aot_runtime.h"
#if WASM_ENABLE_DEBUG_AOT != 0
#include "../aot/debug/jit_debug.h"
#endif
#endif
#if WASM_ENABLE_GC != 0
#include "gc/gc_object.h"
#endif
#if WASM_ENABLE_THREAD_MGR != 0
#include "../libraries/thread-mgr/thread_manager.h"
#if WASM_ENABLE_DEBUG_INTERP != 0
#include "../libraries/debug-engine/debug_engine.h"
#endif
#endif
#if WASM_ENABLE_SHARED_MEMORY != 0
#include "wasm_shared_memory.h"
#endif
#if WASM_ENABLE_FAST_JIT != 0
#include "../fast-jit/jit_compiler.h"
#endif
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
#include "../compilation/aot_llvm.h"
#endif
#include "../common/wasm_c_api_internal.h"
#include "../../version.h"

/**
 * For runtime build, BH_MALLOC/BH_FREE should be defined as
 * wasm_runtime_malloc/wasm_runtime_free.
 */
#define CHECK(a) CHECK1(a)
#define CHECK1(a) SHOULD_BE_##a

#define SHOULD_BE_wasm_runtime_malloc 1
#if !CHECK(BH_MALLOC)
#error unexpected BH_MALLOC
#endif
#undef SHOULD_BE_wasm_runtime_malloc

#define SHOULD_BE_wasm_runtime_free 1
#if !CHECK(BH_FREE)
#error unexpected BH_FREE
#endif
#undef SHOULD_BE_wasm_runtime_free

#undef CHECK
#undef CHECK1

#if WASM_ENABLE_MULTI_MODULE != 0
/**
 * A safety insurance to prevent
 * circular dependencies which leads stack overflow
 * try to break early
 */
typedef struct LoadingModule {
    bh_list_link l;
    /* point to a string pool */
    const char *module_name;
} LoadingModule;

static bh_list loading_module_list_head;
static bh_list *const loading_module_list = &loading_module_list_head;
static korp_mutex loading_module_list_lock;

/**
 * A list to store all exported functions/globals/memories/tables
 * of every fully loaded module
 */
static bh_list registered_module_list_head;
static bh_list *const registered_module_list = &registered_module_list_head;
static korp_mutex registered_module_list_lock;
static void
wasm_runtime_destroy_registered_module_list(void);
#endif /* WASM_ENABLE_MULTI_MODULE */

#define E_TYPE_XIP 4

static uint8
val_type_to_val_kind(uint8 value_type);

#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
/* Initialize externref hashmap */
static bool
wasm_externref_map_init(void);

/* Destroy externref hashmap */
static void
wasm_externref_map_destroy(void);
#endif /* end of WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0 */

static void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string)
{
    if (error_buf != NULL)
        snprintf(error_buf, error_buf_size, "%s", string);
}

static void *
runtime_malloc(uint64 size, WASMModuleInstanceCommon *module_inst,
               char *error_buf, uint32 error_buf_size)
{
    void *mem;

    if (size >= UINT32_MAX || !(mem = wasm_runtime_malloc((uint32)size))) {
        if (module_inst != NULL) {
            wasm_runtime_set_exception(module_inst, "allocate memory failed");
        }
        else if (error_buf != NULL) {
            set_error_buf(error_buf, error_buf_size, "allocate memory failed");
        }
        return NULL;
    }

    memset(mem, 0, (uint32)size);
    return mem;
}

#if WASM_ENABLE_MULTI_MODULE != 0
/* TODO: Let loader_malloc be a general API both for AOT and WASM. */

#define loader_malloc(size, error_buf, error_buf_size) \
    runtime_malloc(size, NULL, error_buf, error_buf_size)

static void
set_error_buf_v(const WASMModuleCommon *module, char *error_buf,
                uint32 error_buf_size, const char *format, ...)
{
    va_list args;
    char buf[128];
    if (error_buf != NULL) {
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        if (module->module_type == Wasm_Module_AoT) {
            snprintf(error_buf, error_buf_size, "AOT module load failed: %s",
                     buf);
        }
        else if (module->module_type == Wasm_Module_Bytecode) {
            snprintf(error_buf, error_buf_size, "WASM module load failed: %s",
                     buf);
        }
    }
}
#endif

#if WASM_ENABLE_FAST_JIT != 0
static JitCompOptions jit_options = { 0 };
#endif

#if WASM_ENABLE_JIT != 0
/* opt_level: 3, size_level: 3, segue-flags: 0,
   quick_invoke_c_api_import: false */
static LLVMJITOptions llvm_jit_options = { 3, 3, 0, false };
#endif

#if WASM_ENABLE_GC != 0
static uint32 gc_heap_size_default = GC_HEAP_SIZE_DEFAULT;
#endif

static RunningMode runtime_running_mode = Mode_Default;

#ifdef OS_ENABLE_HW_BOUND_CHECK
/* The exec_env of thread local storage, set before calling function
   and used in signal handler, as we cannot get it from the argument
   of signal handler */
static os_thread_local_attribute WASMExecEnv *exec_env_tls = NULL;

static bool
is_sig_addr_in_guard_pages(void *sig_addr, WASMModuleInstance *module_inst)
{
    WASMMemoryInstance *memory_inst;
#if WASM_ENABLE_SHARED_HEAP != 0
    WASMSharedHeap *shared_heap;
#endif
    uint8 *mapped_mem_start_addr = NULL;
    uint8 *mapped_mem_end_addr = NULL;
    uint32 i;

    for (i = 0; i < module_inst->memory_count; ++i) {
        /* To be compatible with multi memory, get the ith memory instance */
        memory_inst = wasm_get_memory_with_idx(module_inst, i);
        mapped_mem_start_addr = memory_inst->memory_data;
        mapped_mem_end_addr = memory_inst->memory_data + 8 * (uint64)BH_GB;
        if (mapped_mem_start_addr <= (uint8 *)sig_addr
            && (uint8 *)sig_addr < mapped_mem_end_addr) {
            /* The address which causes segmentation fault is inside
               the memory instance's guard regions */
            return true;
        }
    }

#if WASM_ENABLE_SHARED_HEAP != 0
    shared_heap =
        wasm_runtime_get_shared_heap((WASMModuleInstanceCommon *)module_inst);
    if (shared_heap) {
        mapped_mem_start_addr = shared_heap->base_addr;
        mapped_mem_end_addr = shared_heap->base_addr + 8 * (uint64)BH_GB;
        if (mapped_mem_start_addr <= (uint8 *)sig_addr
            && (uint8 *)sig_addr < mapped_mem_end_addr) {
            /* The address which causes segmentation fault is inside
               the shared heap's guard regions */
            return true;
        }
    }
#endif

    return false;
}

#ifndef BH_PLATFORM_WINDOWS
static void
runtime_signal_handler(void *sig_addr)
{
    WASMModuleInstance *module_inst;
    WASMJmpBuf *jmpbuf_node;
    uint32 page_size = os_getpagesize();
#if WASM_DISABLE_STACK_HW_BOUND_CHECK == 0
    uint8 *stack_min_addr;
    uint32 guard_page_count = STACK_OVERFLOW_CHECK_GUARD_PAGE_COUNT;
#endif

    /* Check whether current thread is running wasm function */
    if (exec_env_tls && exec_env_tls->handle == os_self_thread()
        && (jmpbuf_node = exec_env_tls->jmpbuf_stack_top)) {
        /* Get mapped mem info of current instance */
        module_inst = (WASMModuleInstance *)exec_env_tls->module_inst;

#if WASM_DISABLE_STACK_HW_BOUND_CHECK == 0
        /* Get stack info of current thread */
        stack_min_addr = os_thread_get_stack_boundary();
#endif

        if (is_sig_addr_in_guard_pages(sig_addr, module_inst)) {
            wasm_set_exception(module_inst, "out of bounds memory access");
            os_longjmp(jmpbuf_node->jmpbuf, 1);
        }
#if WASM_DISABLE_STACK_HW_BOUND_CHECK == 0
        else if (stack_min_addr <= (uint8 *)sig_addr
                 && (uint8 *)sig_addr
                        < stack_min_addr + page_size * guard_page_count) {
            /* The address which causes segmentation fault is inside
               native thread's guard page */
            wasm_set_exception(module_inst, "native stack overflow");
            os_longjmp(jmpbuf_node->jmpbuf, 1);
        }
#endif
        else if (exec_env_tls->exce_check_guard_page <= (uint8 *)sig_addr
                 && (uint8 *)sig_addr
                        < exec_env_tls->exce_check_guard_page + page_size) {
            bh_assert(wasm_copy_exception(module_inst, NULL));
            os_longjmp(jmpbuf_node->jmpbuf, 1);
        }
    }
}
#else /* else of BH_PLATFORM_WINDOWS */

#if WASM_ENABLE_AOT != 0
#include <Zydis/Zydis.h>

static uint32
decode_insn(uint8 *insn)
{
    uint8 *data = (uint8 *)insn;
    uint32 length = 32; /* reserve enough size */

    /* Initialize decoder context */
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                     ZYDIS_STACK_WIDTH_64);

    /* Initialize formatter */
    ZydisFormatter formatter;
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

    /* Loop over the instructions in our buffer */
    ZyanU64 runtime_address = (ZyanU64)(uintptr_t)data;
    ZyanUSize offset = 0;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT_VISIBLE];
    char buffer[256];

    if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(
            &decoder, data + offset, length - offset, &instruction, operands,
            ZYDIS_MAX_OPERAND_COUNT_VISIBLE,
            ZYDIS_DFLAG_VISIBLE_OPERANDS_ONLY))) {

        /* Format & print the binary instruction structure to
           human readable format */
        ZydisFormatterFormatInstruction(&formatter, &instruction, operands,
                                        instruction.operand_count_visible,
                                        buffer, sizeof(buffer),
                                        runtime_address);

#if 0
        /* Print current instruction */
        os_printf("%012" PRIX64 "  ", runtime_address);
        puts(buffer);
#endif

        return instruction.length;
    }

    /* Decode failed */
    return 0;
}
#endif /* end of WASM_ENABLE_AOT != 0 */

static LONG
next_action(WASMModuleInstance *module_inst, EXCEPTION_POINTERS *exce_info)
{
#if WASM_ENABLE_AOT != 0
    uint32 insn_size;
#endif

    if (module_inst->module_type == Wasm_Module_Bytecode
        && module_inst->e->running_mode == Mode_Interp) {
        /* Continue to search next exception handler for
           interpreter mode as it can be caught by
           `__try { .. } __except { .. }` sentences in
           wasm_runtime.c */
        return EXCEPTION_CONTINUE_SEARCH;
    }

#if WASM_ENABLE_AOT != 0
    /* Skip current instruction and continue to run for AOT/JIT mode.
       TODO: implement unwind support for AOT/JIT code in Windows platform */
    insn_size = decode_insn((uint8 *)exce_info->ContextRecord->Rip);
    if (insn_size > 0) {
        exce_info->ContextRecord->Rip += insn_size;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif

    /* return different value from EXCEPTION_CONTINUE_SEARCH (= 0)
       and EXCEPTION_CONTINUE_EXECUTION (= -1) */
    return -2;
}

static LONG
runtime_exception_handler(EXCEPTION_POINTERS *exce_info)
{
    PEXCEPTION_RECORD ExceptionRecord = exce_info->ExceptionRecord;
    uint8 *sig_addr = (uint8 *)ExceptionRecord->ExceptionInformation[1];
    WASMModuleInstance *module_inst;
    WASMJmpBuf *jmpbuf_node