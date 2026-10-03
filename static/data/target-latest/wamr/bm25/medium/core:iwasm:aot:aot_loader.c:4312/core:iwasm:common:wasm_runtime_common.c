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
    WASMJmpBuf *jmpbuf_node;
    uint8 *mapped_mem_start_addr = NULL;
    uint8 *mapped_mem_end_addr = NULL;
    uint32 page_size = os_getpagesize();
    LONG ret;

    if (exec_env_tls && exec_env_tls->handle == os_self_thread()
        && (jmpbuf_node = exec_env_tls->jmpbuf_stack_top)) {
        module_inst = (WASMModuleInstance *)exec_env_tls->module_inst;
        if (ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
            if (is_sig_addr_in_guard_pages(sig_addr, module_inst)) {
                /* The address which causes segmentation fault is inside
                   the memory instance's guard regions.
                   Set exception and let the wasm func continue to run, when
                   the wasm func returns, the caller will check whether the
                   exception is thrown and return to runtime. */
                wasm_set_exception(module_inst, "out of bounds memory access");
                ret = next_action(module_inst, exce_info);
                if (ret == EXCEPTION_CONTINUE_SEARCH
                    || ret == EXCEPTION_CONTINUE_EXECUTION)
                    return ret;
            }
            else if (exec_env_tls->exce_check_guard_page <= (uint8 *)sig_addr
                     && (uint8 *)sig_addr
                            < exec_env_tls->exce_check_guard_page + page_size) {
                bh_assert(wasm_copy_exception(module_inst, NULL));
                ret = next_action(module_inst, exce_info);
                if (ret == EXCEPTION_CONTINUE_SEARCH
                    || ret == EXCEPTION_CONTINUE_EXECUTION)
                    return ret;
            }
        }
#if WASM_DISABLE_STACK_HW_BOUND_CHECK == 0
        else if (ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
            /* Set stack overflow exception and let the wasm func continue
               to run, when the wasm func returns, the caller will check
               whether the exception is thrown and return to runtime, and
               the damaged stack will be recovered by _resetstkoflw(). */
            wasm_set_exception(module_inst, "native stack overflow");
            ret = next_action(module_inst, exce_info);
            if (ret == EXCEPTION_CONTINUE_SEARCH
                || ret == EXCEPTION_CONTINUE_EXECUTION)
                return ret;
        }
#endif
        else {
            LOG_WARNING("Unhandled exception thrown:  exception code: 0x%lx, "
                        "exception address: %p, exception information: %p\n",
                        ExceptionRecord->ExceptionCode,
                        ExceptionRecord->ExceptionAddress, sig_addr);
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
#endif /* end of BH_PLATFORM_WINDOWS */

static bool
runtime_signal_init()
{
#ifndef BH_PLATFORM_WINDOWS
    return os_thread_signal_init(runtime_signal_handler) == 0 ? true : false;
#else
    if (os_thread_signal_init() != 0)
        return false;

    if (!AddVectoredExceptionHandler(1, runtime_exception_handler)) {
        os_thread_signal_destroy();
        return false;
    }
#endif
    return true;
}

static void
runtime_signal_destroy()
{
#ifdef BH_PLATFORM_WINDOWS
    RemoveVectoredExceptionHandler(runtime_exception_handler);
#endif
    os_thread_signal_destroy();
}

void
wasm_runtime_set_exec_env_tls(WASMExecEnv *exec_env)
{
    exec_env_tls = exec_env;
}

WASMExecEnv *
wasm_runtime_get_exec_env_tls()
{
    return exec_env_tls;
}
#endif /* end of OS_ENABLE_HW_BOUND_CHECK */

static bool
wasm_runtime_env_init(void)
{
    if (bh_platform_init() != 0)
        return false;

    if (wasm_native_init() == false) {
        goto fail1;
    }

#if WASM_ENABLE_MULTI_MODULE
    if (BHT_OK != os_mutex_init(&registered_module_list_lock)) {
        goto fail2;
    }

    if (BHT_OK != os_mutex_init(&loading_module_list_lock)) {
        goto fail3;
    }
#endif

#if WASM_ENABLE_SHARED_MEMORY
    if (!wasm_shared_memory_init()) {
        goto fail4;
    }
#endif

#if (WASM_ENABLE_WAMR_COMPILER == 0) && (WASM_ENABLE_THREAD_MGR != 0)
    if (!thread_manager_init()) {
        goto fail5;
    }
#endif

#ifdef OS_ENABLE_HW_BOUND_CHECK
    if (!runtime_signal_init()) {
        goto fail6;
    }
#endif

#if WASM_ENABLE_AOT != 0
#if WASM_ENABLE_DEBUG_AOT != 0
    if (!jit_debug_engine_init()) {
        goto fail7;
    }
#endif
#endif

#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    if (!wasm_externref_map_init()) {
        goto fail8;
    }
#endif

#if WASM_ENABLE_FAST_JIT != 0
    if (!jit_compiler_init(&jit_options)) {
        goto fail9;
    }
#endif

#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
    if (!aot_compiler_init()) {
        goto fail10;
    }
#endif

#if WASM_ENABLE_THREAD_MGR != 0 && defined(OS_ENABLE_WAKEUP_BLOCKING_OP)
    if (os_blocking_op_init() != BHT_OK) {
        goto fail11;
    }
    os_end_blocking_op();
#endif

    return true;

#if WASM_ENABLE_THREAD_MGR != 0 && defined(OS_ENABLE_WAKEUP_BLOCKING_OP)
fail11:
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
    aot_compiler_destroy();
#endif
#endif
#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
fail10:
#if WASM_ENABLE_FAST_JIT != 0
    jit_compiler_destroy();
#endif
#endif
#if WASM_ENABLE_FAST_JIT != 0
fail9:
#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    wasm_externref_map_destroy();
#endif
#endif
#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
fail8:
#endif
#if WASM_ENABLE_AOT != 0
#if WASM_ENABLE_DEBUG_AOT != 0
    jit_debug_engine_destroy();
fail7:
#endif
#endif
#ifdef OS_ENABLE_HW_BOUND_CHECK
    runtime_signal_destroy();
fail6:
#endif
#if (WASM_ENABLE_WAMR_COMPILER == 0) && (WASM_ENABLE_THREAD_MGR != 0)
    thread_manager_destroy();
fail5:
#endif
#if WASM_ENABLE_SHARED_MEMORY
    wasm_shared_memory_destroy();
fail4:
#endif
#if WASM_ENABLE_MULTI_MODULE
    os_mutex_destroy(&loading_module_list_lock);
fail3:
    os_mutex_destroy(&registered_module_list_lock);
fail2:
#endif
    wasm_native_destroy();
fail1:
    bh_platform_destroy();

    return false;
}

static bool
wasm_runtime_exec_env_check(WASMExecEnv *exec_env)
{
    return exec_env && exec_env->module_inst && exec_env->wasm_stack_size > 0
           && exec_env->wasm_stack.top_boundary
                  == exec_env->wasm_stack.bottom + exec_env->wasm_stack_size
           && exec_env->wasm_stack.top <= exec_env->wasm_stack.top_boundary;
}

#if defined(OS_THREAD_MUTEX_INITIALIZER)
/**
 * lock for wasm_runtime_init/wasm_runtime_full_init and runtime_ref_count
 * Note: if the platform has mutex initializer, we use a global lock to
 * lock the operations of runtime init/full_init, otherwise when there are
 * operations happening simultaneously in multiple threads, developer
 * must create the lock by himself, and use it to lock the operations
 */
static korp_mutex runtime_lock = OS_THREAD_MUTEX_INITIALIZER;
#endif
static int32 runtime_ref_count = 0;

static bool
wasm_runtime_init_internal(void)
{
    if (!wasm_runtime_memory_init(Alloc_With_System_Allocator, NULL))
        return false;

    if (!wasm_runtime_env_init()) {
        wasm_runtime_memory_destroy();
        return false;
    }

    return true;
}

bool
wasm_runtime_init()
{
    bool ret = true;

#if defined(OS_THREAD_MUTEX_INITIALIZER)
    os_mutex_lock(&runtime_lock);
#endif

    bh_assert(runtime_ref_count >= 0);
    if (runtime_ref_count == 0) {
        ret = wasm_runtime_init_internal();
    }
    if (ret) {
        runtime_ref_count++;
    }

#if defined(OS_THREAD_MUTEX_INITIALIZER)
    os_mutex_unlock(&runtime_lock);
#endif

    return ret;
}

static void
wasm_runtime_destroy_internal(void)
{
#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    wasm_externref_map_destroy();
#endif

#if WASM_ENABLE_AOT != 0
#if WASM_ENABLE_DEBUG_AOT != 0
    jit_debug_engine_destroy();
#endif
#endif

#ifdef OS_ENABLE_HW_BOUND_CHECK
    runtime_signal_destroy();
#endif

    /* runtime env destroy */
#if WASM_ENABLE_MULTI_MODULE
    wasm_runtime_destroy_loading_module_list();
    os_mutex_destroy(&loading_module_list_lock);

    wasm_runtime_destroy_registered_module_list();
    os_mutex_destroy(&registered_module_list_lock);
#endif

#if WASM_ENABLE_JIT != 0 || WASM_ENABLE_WAMR_COMPILER != 0
    /* Destroy LLVM-JIT compiler after destroying the modules
     * loaded by multi-module feature, since these modules may
     * create backend threads to compile the wasm functions,
     * which may access the LLVM resources. We wait until they
     * finish the compilation to avoid accessing the destroyed
     * resources in the compilation threads.
     */
    aot_compiler_destroy();
#endif

#if WASM_ENABLE_FAST_JIT != 0
    /* Destroy Fast-JIT compiler after destroying the modules
     * loaded by multi-module feature, since the Fast JIT's
     * code cache allocator may be used by these modules.
     */
    jit_compiler_destroy();
#endif

#if WASM_ENABLE_SHARED_MEMORY
    wasm_shared_memory_destroy();
#endif

#if (WASM_ENABLE_WAMR_COMPILER == 0) && (WASM_ENABLE_THREAD_MGR != 0)
#if WASM_ENABLE_DEBUG_INTERP != 0
    wasm_debug_engine_destroy();
#endif
    thread_manager_destroy();
#endif

    wasm_native_destroy();
    bh_platform_destroy();

    wasm_runtime_memory_destroy();
}

void
wasm_runtime_destroy()
{
#if defined(OS_THREAD_MUTEX_INITIALIZER)
    os_mutex_lock(&runtime_lock);
#endif

    bh_assert(runtime_ref_count > 0);
    runtime_ref_count--;
    if (runtime_ref_count == 0) {
        wasm_runtime_destroy_internal();
    }

#if defined(OS_THREAD_MUTEX_INITIALIZER)
    os_mutex_unlock(&runtime_lock);
#endif
}

RunningMode
wasm_runtime_get_default_running_mode(void)
{
    return runtime_running_mode;
}

#if WASM_ENABLE_JIT != 0
LLVMJITOptions *
wasm_runtime_get_llvm_jit_options(void)
{
    return &llvm_jit_options;
}
#endif

#if WASM_ENABLE_GC != 0
uint32
wasm_runtime_get_gc_heap_size_default(void)
{
    return gc_heap_size_default;
}
#endif

static bool
wasm_runtime_full_init_internal(RuntimeInitArgs *init_args)
{
    if (!wasm_runtime_memory_init(init_args->mem_alloc_type,
                                  &init_args->mem_alloc_option))
        return false;

    if (!wasm_runtime_set_default_running_mode(init_args->running_mode)) {
        wasm_runtime_memory_destroy();
        return false;
    }

#if WASM_ENABLE_FAST_JIT != 0
    jit_options.code_cache_size = init_args->fast_jit_code_cache_size;
#endif

#if WASM_ENABLE_GC != 0
    gc_heap_size_default = init_args->gc_heap_size;
#endif

#if WASM_ENABLE_JIT != 0
    llvm_jit_options.size_level = init_args->llvm_jit_size_level;
    llvm_jit_options.opt_level = init_args->llvm_jit_opt_level;
    llvm_jit_options.segue_flags = init_args->segue_flags;
#endif

#if WASM_ENABLE_LINUX_PERF != 0
    wasm_runtime_set_linux_perf(init_args->enable_linux_perf);
#else
    if (init_args->enable_linux_perf)
        LOG_WARNING("warning: to enable linux perf support, please recompile "
                    "with -DWAMR_BUILD_LINUX_PERF=1");
#endif

    if (!wasm_runtime_env_init()) {
        wasm_runtime_memory_destroy();
        return false;
    }

#if WASM_ENABLE_DEBUG_INTERP != 0
    if (strlen(init_args->ip_addr))
        if (!wasm_debug_engine_init(init_args->ip_addr,
                                    init_args->instance_port)) {
            wasm_runtime_destroy();
            return false;
        }
#endif

    if (init_args->n_native_symbols > 0
        && !wasm_runtime_register_natives(init_args->native_module_name,
                                          init_args->native_symbols,
                                          init_args->n_native_symbols)) {
        wasm_runtime_destroy();
        return false;
    }

#if WASM_ENABLE_THREAD_MGR != 0
    wasm_cluster_set_max_thread_num(init_args->max_thread_num);
#endif

    return true;
}

bool
wasm_runtime_full_init(RuntimeInitArgs *init_args)
{
    bool ret = true;

#if defined(OS_THREAD_MUTEX_INITIALIZER)
    os_mutex_lock(&runtime_lock);
#endif

    bh_assert(runtime_ref_count >= 0);
    if (runtime_ref_count == 0) {
        ret = wasm_runtime_full_init_internal(init_args);
    }
    if (ret) {
        runtime_ref_count++;
    }

#if defined(OS_THREAD_MUTEX_INITIALIZER)
    os_mutex_unlock(&runtime_lock);
#endif

    return ret;
}

void
wasm_runtime_set_log_level(log_level_t level)
{
    bh_log_set_verbose_level(level);
}

bool
wasm_runtime_is_running_mode_supported(RunningMode running_mode)
{
    if (running_mode == Mode_Default) {
        return true;
    }
    else if (running_mode == Mode_Interp) {
#if WASM_ENABLE_INTERP != 0
        return true;
#endif
    }
    else if (running_mode == Mode_Fast_JIT) {
#if WASM_ENABLE_FAST_JIT != 0
        return true;
#endif
    }
    else if (running_mode == Mode_LLVM_JIT) {
#if WASM_ENABLE_JIT != 0
        return true;
#endif
    }
    else if (running_mode == Mode_Multi_Tier_JIT) {
#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT != 0 \
    && WASM_ENABLE_LAZY_JIT != 0
        return true;
#endif
    }

    return false;
}

bool
wasm_runtime_set_default_running_mode(RunningMode running_mode)
{
    if (wasm_runtime_is_running_mode_supported(running_mode)) {
        runtime_running_mode = running_mode;
        return true;
    }
    return false;
}

PackageType
get_package_type(const uint8 *buf, uint32 size)
{
    if (buf && size >= 4) {
#if (WASM_ENABLE_WORD_ALIGN_READ != 0)
        uint32 buf32 = *(uint32 *)buf;
        buf = (const uint8 *)&buf32;
#endif
        if (buf[0] == '\0' && buf[1] == 'a' && buf[2] == 's' && buf[3] == 'm')
            return Wasm_Module_Bytecode;
        if (buf[0] == '\0' && buf[1] == 'a' && buf[2] == 'o' && buf[3] == 't')
            return Wasm_Module_AoT;
    }
    return Package_Type_Unknown;
}

PackageType
wasm_runtime_get_file_package_type(const uint8 *buf, uint32 size)
{
    return get_package_type(buf, size);
}

PackageType
wasm_runtime_get_module_package_type(WASMModuleCommon *const module)
{
    if (!module) {
        return Package_Type_Unknown;
    }

    return module->module_type;
}

uint32
wasm_runtime_get_file_package_version(const uint8 *buf, uint32 size)
{
    if (buf && size >= 8) {
        uint32 version;
#if (WASM_ENABLE_WORD_ALIGN_READ != 0)
        uint32 buf32 = *(uint32 *)(buf + sizeof(uint32));
        buf = (const uint8 *)&buf32;
        version = buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24;
#else
        version = buf[4] | buf[5] << 8 | buf[6] << 16 | buf[7] << 24;
#endif
        return version;
    }

    return 0;
}

uint32
wasm_runtime_get_module_package_version(WASMModuleCommon *const module)
{
    if (!module) {
        return 0;
    }

#if WASM_ENABLE_INTERP != 0
    if (module->module_type == Wasm_Module_Bytecode) {
        WASMModule *wasm_module = (WASMModule *)module;
        return wasm_module->package_version;
    }
#endif

#if WASM_ENABLE_AOT != 0
    if (module->module_type == Wasm_Module_AoT) {
        AOTModule *aot_module = (AOTModule *)module;
        return aot_module->package_version;
    }
#endif

    return 0;
}

uint32
wasm_runtime_get_current_package_version(package_type_t package_type)
{
    switch (package_type) {
        case Wasm_Module_Bytecode:
            return WASM_CURRENT_VERSION;
        case Wasm_Module_AoT:
            return AOT_CURRENT_VERSION;
        case Package_Type_Unknown:
        default:
            return 0;
    }
}

#if WASM_ENABLE_AOT != 0
static uint8 *
align_ptr(const uint8 *p, uint32 b)
{
    uintptr_t v = (uintptr_t)p;
    uintptr_t m = b - 1;
    return (uint8 *)((v + m) & ~m);
}

#define CHECK_BUF(buf, buf_end, length)                      \
    do {                                                     \
        if ((uintptr_t)buf + length < (uintptr_t)buf         \
            || (uintptr_t)buf + length > (uintptr_t)buf_end) \
            return false;                                    \
    } while (0)

/* NOLINTNEXTLINE */
#define read_uint16(p, p_end, res)                 \
    do {                                           \
        p = (uint8 *)align_ptr(p, sizeof(uint16)); \
        CHECK_BUF(p, p_end, sizeof(uint16));       \
        res = *(uint16 *)p;                        \
        p += sizeof(uint16);                       \
    } while (0)

/* NOLINTNEXTLINE */
#define read_uint32(p, p_end, res)                 \
    do {                                           \
        p = (uint8 *)align_ptr(p, sizeof(uint32)); \
        CHECK_BUF(p, p_end, sizeof(uint32));       \
        res = *(uint32 *)p;                        \
        p += sizeof(uint32);                       \
    } while (0)

bool
wasm_runtime_is_xip_file(const uint8 *buf, uint32 size)
{
    const uint8 *p = buf, *p_end = buf + size;
    uint32 section_type, section_size;
    uint16 e_type;

    if (get_package_type(buf, size) != Wasm_Module_AoT)
        return false;

    CHECK_BUF(p, p_end, 8);
    p += 8;
    while (p < p_end) {
        read_uint32(p, p_end, section_type);
        read_uint32(p, p_end, section_size);
        CHECK_BUF(p, p_end, section_size);

        if (section_type == AOT_SECTION_TYPE_TARGET_INFO) {
            p += 4;
            read_uint16(p, p_end, e_type);
            return (e_type == E_TYPE_XIP) ? true : false;
        }
        else if (section_type >= AOT_SECTION_TYPE_SIGNATURE) {
            return false;
        }
        p += section_size;
    }

    return false;
}
#endif /* end of WASM_ENABLE_AOT */

#if (WASM_ENABLE_THREAD_MGR != 0) && (WASM_ENABLE_DEBUG_INTERP != 0)
uint32
wasm_runtime_start_debug_instance_with_port(WASMExecEnv *exec_env, int32_t port)
{
    WASMModuleInstanceCommon *module_inst =
        wasm_runtime_get_module_inst(exec_env);
    WASMCluster *cluster = wasm_exec_env_get_cluster(exec_env);
    bh_assert(module_inst);
    bh_assert(cluster);

    if (module_inst->module_type != Wasm_Module_Bytecode) {
        LOG_WARNING("Attempt to create a debug instance for an AOT module");
        return 0;
    }

    if (cluster->debug_inst) {
        LOG_WARNING("Cluster already bind to a debug instance");
        return cluster->debug_inst->control_thread->port;
    }

    if (wasm_debug_instance_create(cluster, port)) {
        return cluster->debug_inst->control_thread->port;
    }

    return 0;
}

uint32
wasm_runtime_start_debug_instance(WASMExecEnv *exec_env)
{
    return wasm_runtime_start_debug_instance_with_port(exec_env, -1);
}
#endif

#if WASM_ENABLE_MULTI_MODULE != 0
static module_reader reader;
static module_destroyer destroyer;
void
wasm_runtime_set_module_reader(const module_reader reader_cb,
                               const module_destroyer destroyer_cb)
{
    reader = reader_cb;
    destroyer = destroyer_cb;
}

module_reader
wasm_runtime_get_module_reader()
{
    return reader;
}

module_destroyer
wasm_runtime_get_module_destroyer()
{
    return destroyer;
}

static WASMRegisteredModule *
wasm_runtime_find_module_registered_by_reference(WASMModuleCommon *module)
{
    WASMRegisteredModule *reg_module = NULL;

    os_mutex_lock(&registered_module_list_lock);
    reg_module = bh_list_first_elem(registered_module_list);
    while (reg_module && module != reg_module->module) {
        reg_module = bh_list_elem_next(reg_module);
    }
    os_mutex_unlock(&registered_module_list_lock);

    return reg_module;
}

bool
wasm_runtime_register_module_internal(const char *module_name,
                                      WASMModuleCommon *module,
                                      uint8 *orig_file_buf,
                                      uint32 orig_file_buf_size,
                                      char *error_buf, uint32 error_buf_size)
{
    WASMRegisteredModule *node = NULL;

    node = wasm_runtime_find_module_registered_by_reference(module);
    if (node) {                  /* module has been registered */
        if (node->module_name) { /* module has name */
            if (!module_name || strcmp(node->module_name, module_name)) {
                /* module has different name */
                LOG_DEBUG("module(%p) has been registered with name %s", module,
                          node->module_name);
                set_error_buf(error_buf, error_buf_size,
                              "Register module failed: "
                              "failed to rename the module");
                return false;
            }
            else {
                /* module has the same name */
                LOG_DEBUG(
                    "module(%p) has been registered with the same name %s",
                    module, node->module_name);
                return true;
            }
        }
        else {
            /* module has empty name, reset it */
            node->module_name = module_name;
            return true;
        }
    }

    /* module hasn't been registered */
    node = runtime_malloc(sizeof(WASMRegisteredModule), NULL, NULL, 0);
    if (!node) {
        LOG_DEBUG("malloc WASMRegisteredModule failed. SZ=%zu",
                  sizeof(WASMRegisteredModule));
        return false;
    }

    /* share the string and the module */
    node->module_name = module_name;
    node->module = module;
    node->orig_file_buf = orig_file_buf;
    node->orig_file_buf_size = orig_file_buf_size;

    os_mutex_lock(&registered_module_list_lock);
    bh_list_status ret = bh_list_insert(registered_module_list, node);
    bh_assert(BH_LIST_SUCCESS == ret);
    (void)ret;
    os_mutex_unlock(&registered_module_list_lock);
    return true;
}

bool
wasm_runtime_register_module(const char *module_name, WASMModuleCommon *module,
                             char *error_buf, uint32 error_buf_size)
{
    if (!error_buf || !error_buf_size) {
        LOG_ERROR("error buffer is required");
        return false;
    }

    if (!module_name || !module) {
        LOG_DEBUG("module_name and module are required");
        set_error_buf(error_buf, error_buf_size,
                      "Register module failed: "
                      "module_name and module are required");
        return false;
    }

    if (wasm_runtime_is_built_in_module(module_name)) {
        LOG_DEBUG("%s is a built-in module name", module_name);
        set_error_buf(error_buf, error_buf_size,
                      "Register module failed: "
                      "can not register as a built-in module");
        return false;
    }

    return wasm_runtime_register_module_internal(module_name, module, NULL, 0,
                                                 error_buf, error_buf_size);
}

void
wasm_runtime_unregister_module(const WASMModuleCommon *module)
{
    WASMRegisteredModule *registered_module = NULL;

    os_mutex_lock(&registered_module_list_lock);
    registered_module = bh_list_first_elem(registered_module_list);
    while (registered_module && module != registered_module->module) {
        registered_module = bh_list_elem_next(registered_module);
    }

    /* it does not matter if it is not exist. after all, it is gone */
    if (registered_module) {
        bh_list_remove(registered_module_list, registered_module);
        wasm_runtime_free(registered_module);
    }
    os_mutex_unlock(&registered_module_list_lock);
}

WASMModuleCommon *
wasm_runtime_find_module_registered(const char *module_name)
{
    WASMRegisteredModule *module = NULL, *module_next;

    os_mutex_lock(&registered_module_list_lock);
    module = bh_list_first_elem(registered_module_list);
    while (module) {
        module_next = bh_list_elem_next(module);
        if (module->module_name && !strcmp(module_name, module->module_name)) {
            break;
        }
        module = module_next;
    }
    os_mutex_unlock(&registered_module_list_lock);

    return module ? module->module : NULL;
}

/*
 * simply destroy all
 */
static void
wasm_runtime_destroy_registered_module_list()
{
    WASMRegisteredModule *reg_module = NULL;

    os_mutex_lock(&registered_module_list_lock);
    reg_module = bh_list_first_elem(registered_module_list);
    while (reg_module) {
        WASMRegisteredModule *next_reg_module = bh_list_elem_next(reg_module);

        bh_list_remove(registered_module_list, reg_module);

        /* now, it is time to release every module in the runtime */
        if (reg_module->module->module_type == Wasm_Module_Bytecode) {
#if WASM_ENABLE_INTERP != 0
            wasm_unload((WASMModule *)reg_module->module);
#endif
        }
        else {
#if WASM_ENABLE_AOT != 0
            aot_unload((AOTModule *)reg_module->module);
#endif
        }

        /* destroy the file buffer */
        if (destroyer && reg_module->orig_file_buf) {
            destroyer(reg_module->orig_file_buf,
                      reg_module->orig_file_buf_size);
            reg_module->orig_file_buf = NULL;
            reg_module->orig_file_buf_size = 0;
        }

        wasm_runtime_free(reg_module);
        reg_module = next_reg_module;
    }
    os_mutex_unlock(&registered_module_list_lock);
}

bool
wasm_runtime_add_loading_module(const char *module_name, char *error_buf,
                                uint32 error_buf_size)
{
    LOG_DEBUG("add %s into a loading list", module_name);
    LoadingModule *loadingModule =
        runtime_malloc(sizeof(LoadingModule), NULL, error_buf, error_buf_size);

    if (!loadingModule) {
        return false;
    }

    /* share the incoming string */
    loadingModule->module_name = module_name;

    os_mutex_lock(&loading_module_list_lock);
    bh_list_status ret = bh_list_insert(loading_module_list, loadingModule);
    bh_assert(BH_LIST_SUCCESS == ret);
    (void)ret;
    os_mutex_unlock(&loading_module_list_lock);
    return true;
}

void
wasm_runtime_delete_loading_module(const char *module_name)
{
    LOG_DEBUG("delete %s from a loading list", module_name);

    LoadingModule *module = NULL;

    os_mutex_lock(&loading_module_list_lock);
    module = bh_list_first_elem(loading_module_list);
    while (module && strcmp(module->module_name, module_name)) {
        module = bh_list_elem_next(module);
    }

    /* it does not matter if it is not exist. after all, it is gone */
    if (module) {
        bh_list_remove(loading_module_list, module);
        wasm_runtime_free(module);
    }
    os_mutex_unlock(&loading_module_list_lock);
}

bool
wasm_runtime_is_loading_module(const char *module_name)
{
    LOG_DEBUG("find %s in a loading list", module_name);

    LoadingModule *module = NULL;

    os_mutex_lock(&loading_module_list_lock);
    module = bh_list_first_elem(loading_module_list);
    while (module && strcmp(module_name, module->module_name)) {
        module = bh_list_elem_next(module);
    }
    os_mutex_unlock(&loading_module_list_lock);

    return module != NULL;
}

void
wasm_runtime_destroy_loading_module_list()
{
    LoadingModule *module = NULL;

    os_mutex_lock(&loading_module_list_lock);
    module = bh_list_first_elem(loading_module_list);
    while (module) {
        LoadingModule *next_module = bh_list_elem_next(module);

        bh_list_remove(loading_module_list, module);
        /*
         * will not free the module_name since it is
         * shared one of the const string pool
         */
        wasm_runtime_free(module);

        module = next_module;
    }

    os_mutex_unlock(&loading_module_list_lock);
}
#endif /* WASM_ENABLE_MULTI_MODULE */

bool
wasm_runtime_is_built_in_module(const char *module_name)
{
    return (!strcmp("env", module_name) || !strcmp("wasi_unstable", module_name)
            || !strcmp("wasi_snapshot_preview1", module_name)
#if WASM_ENABLE_SPEC_TEST != 0
            || !strcmp("spectest", module_name)
#endif
#if WASM_ENABLE_WASI_TEST != 0
            || !strcmp("foo", module_name)
#endif
            || !strcmp("", module_name));
}

#if WASM_ENABLE_THREAD_MGR != 0
bool
wasm_exec_env_set_aux_stack(WASMExecEnv *exec_env, uint64 start_offset,
                            uint32 size)
{
    WASMModuleInstanceCommon *module_inst =
        wasm_exec_env_get_module_inst(exec_env);
#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        return wasm_set_aux_stack(exec_env, start_offset, size);
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT) {
        return aot_set_aux_stack(exec_env, start_offset, size);
    }
#endif
    return false;
}

bool
wasm_exec_env_get_aux_stack(WASMExecEnv *exec_env, uint64 *start_offset,
                            uint32 *size)
{
    WASMModuleInstanceCommon *module_inst =
        wasm_exec_env_get_module_inst(exec_env);
#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        return wasm_get_aux_stack(exec_env, start_offset, size);
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT) {
        return aot_get_aux_stack(exec_env, start_offset, size);
    }
#endif
    return false;
}

void
wasm_runtime_set_max_thread_num(uint32 num)
{
    wasm_cluster_set_max_thread_num(num);
}
#endif /* end of WASM_ENABLE_THREAD_MGR */

static WASMModuleCommon *
register_module_with_null_name(WASMModuleCommon *module_common, char *error_buf,
                               uint32 error_buf_size)
{
#if WASM_ENABLE_MULTI_MODULE != 0
    if (module_common) {
        if (!wasm_runtime_register_module_internal(NULL, module_common, NULL, 0,
                                                   error_buf, error_buf_size)) {
            wasm_runtime_unload(module_common);
            return NULL;
        }
        return module_common;
    }
    else
        return NULL;
#else
    return module_common;
#endif
}

WASMModuleCommon *
wasm_runtime_load_ex(uint8 *buf, uint32 size, const LoadArgs *args,
                     char *error_buf, uint32 error_buf_size)
{
    WASMModuleCommon *module_common = NULL;
    uint32 package_type;
    bool magic_header_detected = false;

    if (!args) {
        set_error_buf(error_buf, error_buf_size,
                      "WASM module load failed: null load arguments");
        return NULL;
    }

    if (size < 4) {
        set_error_buf(error_buf, error_buf_size,
                      "WASM module load failed: unexpected end");
        return NULL;
    }

    package_type = get_package_type(buf, size);
    if (package_type == Wasm_Module_Bytecode) {
#if WASM_ENABLE_INTERP != 0
        magic_header_detected = true;
#endif
    }
    else if (package_type == Wasm_Module_AoT) {
#if WASM_ENABLE_AOT != 0
        magic_header_detected = true;
#endif
    }
    if (!magic_header_detected) {
        set_error_buf(error_buf, error_buf_size,
                      "WASM module load failed: magic header not detected");
        return NULL;
    }

    if (package_type == Wasm_Module_Bytecode) {
#if WASM_ENABLE_INTERP != 0
        module_common =
            (WASMModuleCommon *)wasm_load(buf, size,
#if WASM_ENABLE_MULTI_MODULE != 0
                                          true,
#endif
                                          args, error_buf, error_buf_size);
        if (module_common)
            ((WASMModule *)module_common)->is_binary_freeable =
                args->wasm_binary_freeable;
#endif
    }
    else if (package_type == Wasm_Module_AoT) {
#if WASM_ENABLE_AOT != 0
        module_common = (WASMModuleCommon *)aot_load_from_aot_file(
            buf, size, args, error_buf, error_buf_size);
        if (module_common)
            ((AOTModule *)module_common)->is_binary_freeable =
                args->wasm_binary_freeable;
#endif
    }

    if (!module_common) {
        LOG_DEBUG("WASM module load failed");
        return NULL;
    }

    /*TODO: use file name as name and register with name? */
    return register_module_with_null_name(module_common, error_buf,
                                          error_buf_size);
}

WASM_RUNTIME_API_EXTERN bool
wasm_runtime_resolve_symbols(WASMModuleCommon *module)
{
#if WASM_ENABLE_INTERP != 0
    if (module->module_type == Wasm_Module_Bytecode) {
        return wasm_resolve_symbols((WASMModule *)module);
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module->module_type == Wasm_Module_AoT) {
        return aot_resolve_symbols((AOTModule *)module);
    }
#endif
    return false;
}

WASMModuleCommon *
wasm_runtime_load(uint8 *buf, uint32 size, char *error_buf,
                  uint32 error_buf_size)
{
    LoadArgs args = { 0 };
    args.name = "";
    args.wasm_binary_freeable = false;
    return wasm_runtime_load_ex(buf, size, &args, error_buf, error_buf_size);
}

WASMModuleCommon *
wasm_runtime_load_from_sections(WASMSection *section_list, bool is_aot,
                                char *error_buf, uint32 error_buf_size)
{
    WASMModuleCommon *module_common;

    if (!is_aot) {
#if WASM_ENABLE_INTERP != 0
        module_common = (WASMModuleCommon *)wasm_load_from_sections(
            section_list, error_buf, error_buf_size);
        if (!module_common) {
            LOG_DEBUG("WASM module load failed from sections");
            return NULL;
        }
        ((WASMModule *)module_common)->is_binary_freeable = true;
        return register_module_with_null_name(module_common, error_buf,
                                              error_buf_size);
#endif
    }
    else {
#if WASM_ENABLE_AOT != 0
        module_common = (WASMModuleCommon *)aot_load_from_sections(
            section_list, error_buf, error_buf_size);
        if (!module_common) {
            LOG_DEBUG("WASM module load failed from sections");
            return NULL;
        }
        ((AOTModule *)module_common)->is_binary_freeable = true;
        return register_module_with_null_name(module_common, error_buf,
                                              error_buf_size);
#endif
    }

#if WASM_ENABLE_INTERP == 0 || WASM_ENABLE_AOT == 0
    set_error_buf(error_buf, error_buf_size,
                  "WASM module load failed: invalid section list type");
    return NULL;
#endif
}

void
wasm_runtime_unload(WASMModuleCommon *module)
{
#if WASM_ENABLE_MULTI_MODULE != 0
    /**
     * since we will unload and free all module when runtime_destroy()
     * we don't want users to unwillingly disrupt it
     */
    return;
#endif

#if WASM_ENABLE_INTERP != 0
    if (module->module_type == Wasm_Module_Bytecode) {
        wasm_unload((WASMModule *)module);
        return;
    }
#endif

#if WASM_ENABLE_AOT != 0
    if (module->module_type == Wasm_Module_AoT) {
        aot_unload((AOTModule *)module);
        return;
    }
#endif
}

uint32
wasm_runtime_get_max_mem(uint32 max_memory_pages, uint32 module_init_page_count,
                         uint32 module_max_page_count)
{
    if (max_memory_pages == 0) {
        /* Max memory not overwritten by runtime, use value from wasm module */
        return module_max_page_count;
    }

    if (max_memory_pages < module_init_page_count) {
        LOG_WARNING("Cannot override max memory with value lower than module "
                    "initial memory");
        return module_init_page_count;
    }

    if (max_memory_pages > module_max_page_count) {
        LOG_WARNING("Cannot override max memory with value greater than module "
                    "max memory");
        return module_max_page_count;
    }

    return max_memory_pages;
}

WASMModuleInstanceCommon *
wasm_runtime_instantiate_internal(WASMModuleCommon *module,
                                  WASMModuleInstanceCommon *parent,
                                  WASMExecEnv *exec_env_main, uint32 stack_size,
                                  uint32 heap_size, uint32 max_memory_pages,
                                  char *error_buf, uint32 error_buf_size)
{
#if WASM_ENABLE_INTERP != 0
    if (module->module_type == Wasm_Module_Bytecode)
        return (WASMModuleInstanceCommon *)wasm_instantiate(
            (WASMModule *)module, (WASMModuleInstance *)parent, exec_env_main,
            stack_size, heap_size, max_memory_pages, error_buf, error_buf_size);
#endif
#if WASM_ENABLE_AOT != 0
    if (module->module_type == Wasm_Module_AoT)
        return (WASMModuleInstanceCommon *)aot_instantiate(
            (AOTModule *)module, (AOTModuleInstance *)parent, exec_env_main,
            stack_size, heap_size, max_memory_pages, error_buf, error_buf_size);
#endif
    set_error_buf(error_buf, error_buf_size,
                  "Instantiate module failed, invalid module type");
    return NULL;
}

WASMModuleInstanceCommon *
wasm_runtime_instantiate(WASMModuleCommon *module, uint32 stack_size,
                         uint32 heap_size, char *error_buf,
                         uint32 error_buf_size)
{
    return wasm_runtime_instantiate_internal(module, NULL, NULL, stack_size,
                                             heap_size, 0, error_buf,
                                             error_buf_size);
}

WASMModuleInstanceCommon *
wasm_runtime_instantiate_ex(WASMModuleCommon *module,
                            const InstantiationArgs *args, char *error_buf,
                            uint32 error_buf_size)
{
    return wasm_runtime_instantiate_internal(
        module, NULL, NULL, args->default_stack_size,
        args->host_managed_heap_size, args->max_memory_pages, error_buf,
        error_buf_size);
}

void
wasm_runtime_deinstantiate_internal(WASMModuleInstanceCommon *module_inst,
                                    bool is_sub_inst)
{
#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        wasm_deinstantiate((WASMModuleInstance *)module_inst, is_sub_inst);
        return;
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT) {
        aot_deinstantiate((AOTModuleInstance *)module_inst, is_sub_inst);
        return;
    }
#endif
}

bool
wasm_runtime_set_running_mode(wasm_module_inst_t module_inst,
                              RunningMode running_mode)
{
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT)
        return true;
#endif

#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        WASMModuleInstance *module_inst_interp =
            (WASMModuleInstance *)module_inst;

        return wasm_set_running_mode(module_inst_interp, running_mode);
    }
#endif

    return false;
}

RunningMode
wasm_runtime_get_running_mode(wasm_module_inst_t module_inst)
{
#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        WASMModuleInstance *module_inst_interp =
            (WASMModuleInstance *)module_inst;
        return module_inst_interp->e->running_mode;
    }
#endif

    return Mode_Default;
}

void
wasm_runtime_deinstantiate(WASMModuleInstanceCommon *module_inst)
{
    wasm_runtime_deinstantiate_internal(module_inst, false);
}

WASMModuleCommon *
wasm_runtime_get_module(WASMModuleInstanceCommon *module_inst)
{
    return (WASMModuleCommon *)((WASMModuleInstance *)module_inst)->module;
}

WASMExecEnv *
wasm_runtime_create_exec_env(WASMModuleInstanceCommon *module_inst,
                             uint32 stack_size)
{
    return wasm_exec_env_create(module_inst, stack_size);
}

void
wasm_runtime_destroy_exec_env(WASMExecEnv *exec_env)
{
    wasm_exec_env_destroy(exec_env);
}

bool
wasm_runtime_init_thread_env(void)
{
#ifdef BH_PLATFORM_WINDOWS
    if (os_thread_env_init() != 0)
        return false;
#endif

#ifdef OS_ENABLE_HW_BOUND_CHECK
    if (!runtime_signal_init()) {
#ifdef BH_PLATFORM_WINDOWS
        os_thread_env_destroy();
#endif
        return false;
    }
#endif

#if WASM_ENABLE_THREAD_MGR != 0 && defined(OS_ENABLE_WAKEUP_BLOCKING_OP)
    os_end_blocking_op();
#endif

    return true;
}

void
wasm_runtime_destroy_thread_env(void)
{
#ifdef OS_ENABLE_HW_BOUND_CHECK
    runtime_signal_destroy();
#endif

#ifdef BH_PLATFORM_WINDOWS
    os_thread_env_destroy();
#endif
}

bool
wasm_runtime_thread_env_inited(void)
{
#ifdef BH_PLATFORM_WINDOWS
    if (!os_thread_env_inited())
        return false;
#endif

#if WASM_ENABLE_AOT != 0
#ifdef OS_ENABLE_HW_BOUND_CHECK
    if (!os_thread_signal_inited())
        return false;
#endif
#endif
    return true;
}

#if (WASM_ENABLE_MEMORY_PROFILING != 0) || (WASM_ENABLE_MEMORY_TRACING != 0)
void
wasm_runtime_dump_module_mem_consumption(const WASMModuleCommon *module)
{
    WASMModuleMemConsumption mem_conspn = { 0 };

#if WASM_ENABLE_INTERP != 0
    if (module->module_type == Wasm_Module_Bytecode) {
        wasm_get_module_mem_consumption((WASMModule *)module, &mem_conspn);
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module->module_type == Wasm_Module_AoT) {
        aot_get_module_mem_consumption((AOTModule *)module, &mem_conspn);
    }
#endif

    os_printf("WASM module memory consumption, total size: %u\n",
              mem_conspn.total_size);
    os_printf("    module struct size: %u\n", mem_conspn.module_struct_size);
    os_printf("    types size: %u\n", mem_conspn.types_size);
    os_printf("    imports size: %u\n", mem_conspn.imports_size);
    os_printf("    funcs size: %u\n", mem_conspn.functions_size);
    os_printf("    tables size: %u\n", mem_conspn.tables_size);
    os_printf("    memories size: %u\n", mem_conspn.memories_size);
    os_printf("    globals size: %u\n", mem_conspn.globals_size);
    os_printf("    exports size: %u\n", mem_conspn.exports_size);
    os_printf("    table segs size: %u\n", mem_conspn.table_segs_size);
    os_printf("    data segs size: %u\n", mem_conspn.data_segs_size);
    os_printf("    const strings size: %u\n", mem_conspn.const_strs_size);
#if WASM_ENABLE_AOT != 0
    os_printf("    aot code size: %u\n", mem_conspn.aot_code_size);
#endif
}

void
wasm_runtime_dump_module_inst_mem_consumption(
    const WASMModuleInstanceCommon *module_inst)
{
    WASMModuleInstMemConsumption mem_conspn = { 0 };

#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        wasm_get_module_inst_mem_consumption((WASMModuleInstance *)module_inst,
                                             &mem_conspn);
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT) {
        aot_get_module_inst_mem_consumption((AOTModuleInstance *)module_inst,
                                            &mem_conspn);
    }
#endif

    os_printf("WASM module inst memory consumption, total size: %lu\n",
              mem_conspn.total_size);
    os_printf("    module inst struct size: %u\n",
              mem_conspn.module_inst_struct_size);
    os_printf("    memories size: %lu\n", mem_conspn.memories_size);
    os_printf("        app heap size: %u\n", mem_conspn.app_heap_size);
    os_printf("    tables size: %u\n", mem_conspn.tables_size);
    os_printf("    functions size: %u\n", mem_conspn.functions_size);
    os_printf("    globals size: %u\n", mem_conspn.globals_size);
    os_printf("    exports size: %u\n", mem_conspn.exports_size);
}

void
wasm_runtime_dump_exec_env_mem_consumption(const WASMExecEnv *exec_env)
{
    uint32 total_size =
        offsetof(WASMExecEnv, wasm_stack_u.bottom) + exec_env->wasm_stack_size;

    os_printf("Exec env memory consumption, total size: %u\n", total_size);
    os_printf("    exec env struct size: %u\n",
              offsetof(WASMExecEnv, wasm_stack_u.bottom));
#if WASM_ENABLE_INTERP != 0 && WASM_ENABLE_FAST_INTERP == 0
    os_printf("        block addr cache size: %u\n",
              sizeof(exec_env->block_addr_cache));
#endif
    os_printf("    stack size: %u\n", exec_env->wasm_stack_size);
}

uint32
gc_get_heap_highmark_size(void *heap);

void
wasm_runtime_dump_mem_consumption(WASMExecEnv *exec_env)
{
    WASMModuleInstMemConsumption module_inst_mem_consps;
    WASMModuleMemConsumption module_mem_consps;
    WASMModuleInstanceCommon *module_inst_common;
    WASMModuleCommon *module_common = NULL;
    void *heap_handle = NULL;
    uint32 app_heap_peak_size = 0;
    uint32 max_aux_stack_used = -1;
    uint64 total_size = 0;

    module_inst_common = exec_env->module_inst;
#if WASM_ENABLE_INTERP != 0
    if (module_inst_common->module_type == Wasm_Module_Bytecode) {
        WASMModuleInstance *wasm_module_inst =
            (WASMModuleInstance *)module_inst_common;
        WASMModule *wasm_module = wasm_module_inst->module;
        module_common = (WASMModuleCommon *)wasm_module;
        if (wasm_module_inst->memories) {
            heap_handle = wasm_module_inst->memories[0]->heap_handle;
        }
        wasm_get_module_inst_mem_consumption(wasm_module_inst,
                                             &module_inst_mem_consps);
        wasm_get_module_mem_consumption(wasm_module, &module_mem_consps);
        if (wasm_module_inst->module->aux_stack_top_global_index != (uint32)-1)
            max_aux_stack_used = wasm_module_inst->e->max_aux_stack_used;
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst_common->module_type == Wasm_Module_AoT) {
        AOTModuleInstance *aot_module_inst =
            (AOTModuleInstance *)module_inst_common;
        AOTModule *aot_module = (AOTModule *)aot_module_inst->module;
        module_common = (WASMModuleCommon *)aot_module;
        if (aot_module_inst->memories) {
            AOTMemoryInstance **memories = aot_module_inst->memories;
            heap_handle = memories[0]->heap_handle;
        }
        aot_get_module_inst_mem_consumption(aot_module_inst,
                                            &module_inst_mem_consps);
        aot_get_module_mem_consumption(aot_module, &module_mem_consps);
    }
#endif

    bh_assert(module_common != NULL);

    if (heap_handle) {
        app_heap_peak_size = gc_get_heap_highmark_size(heap_handle);
    }

    total_size = offsetof(WASMExecEnv, wasm_stack_u.bottom)
                 + exec_env->wasm_stack_size + module_mem_consps.total_size
                 + module_inst_mem_consps.total_size;

    os_printf("\nMemory consumption summary (bytes):\n");
    wasm_runtime_dump_module_mem_consumption(module_common);
    wasm_runtime_dump_module_inst_mem_consumption(module_inst_common);
    wasm_runtime_dump_exec_env_mem_consumption(exec_env);
    os_printf("\nTotal memory consumption of module, module inst and "
              "exec env: %" PRIu64 "\n",
              total_size);
    os_printf("Total interpreter stack used: %u\n",
              exec_env->max_wasm_stack_used);

    if (max_aux_stack_used != (uint32)-1)
        os_printf("Total auxiliary stack used: %u\n", max_aux_stack_used);
    else
        os_printf("Total aux stack used: no enough info to profile\n");

    /*
     * Report the native stack usage estimation.
     *
     * Unlike the aux stack above, we report the amount unused
     * because we don't know the stack "bottom".
     *
     * Note that this is just about what the runtime itself observed.
     * It doesn't cover host func implementations, signal handlers, etc.
     */
    if (exec_env->native_stack_top_min != (void *)UINTPTR_MAX)
        os_printf("Native stack left: %zd\n",
                  exec_env->native_stack_top_min
                      - exec_env->native_stack_boundary);
    else
        os_printf("Native stack left: no enough info to profile\n");

    os_printf("Total app heap used: %u\n", app_heap_peak_size);
}
#endif /* end of (WASM_ENABLE_MEMORY_PROFILING != 0) \
                 || (WASM_ENABLE_MEMORY_TRACING != 0) */

#if WASM_ENABLE_PERF_PROFILING != 0
void
wasm_runtime_dump_perf_profiling(WASMModuleInstanceCommon *module_inst)
{
#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        wasm_dump_perf_profiling((WASMModuleInstance *)module_inst);
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT) {
        aot_dump_perf_profiling((AOTModuleInstance *)module_inst);
    }
#endif
}

double
wasm_runtime_sum_wasm_exec_time(WASMModuleInstanceCommon *inst)
{
#if WASM_ENABLE_INTERP != 0
    if (inst->module_type == Wasm_Module_Bytecode)
        return wasm_summarize_wasm_execute_time((WASMModuleInstance *)inst);
#endif

#if WASM_ENABLE_AOT != 0
    if (inst->module_type == Wasm_Module_AoT)
        return aot_summarize_wasm_execute_time((AOTModuleInstance *)inst);
#endif

    return 0.0;
}

double
wasm_runtime_get_wasm_func_exec_time(WASMModuleInstanceCommon *inst,
                                     const char *func_name)
{
#if WASM_ENABLE_INTERP != 0
    if (inst->module_type == Wasm_Module_Bytecode)
        return wasm_get_wasm_func_exec_time((WASMModuleInstance *)inst,
                                            func_name);
#endif

#if WASM_ENABLE_AOT != 0
    if (inst->module_type == Wasm_Module_AoT)
        return aot_get_wasm_func_exec_time((AOTModuleInstance *)inst,
                                           func_name);
#endif

    return 0.0;
}
#endif /* WASM_ENABLE_PERF_PROFILING != 0 */

WASMModuleInstanceCommon *
wasm_runtime_get_module_inst(WASMExecEnv *exec_env)
{
    return wasm_exec_env_get_module_inst(exec_env);
}

void
wasm_runtime_set_module_inst(WASMExecEnv *exec_env,
                             WASMModuleInstanceCommon *const module_inst)
{
    wasm_exec_env_set_module_inst(exec_env, module_inst);
}

bool
wasm_runtime_get_export_global_inst(WASMModuleInstanceCommon *const module_inst,
                                    char const *name,
                                    wasm_global_inst_t *global_inst)
{
#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        const WASMModuleInstance *wasm_module_inst =
            (const WASMModuleInstance *)module_inst;
        const WASMModule *wasm_module = wasm_module_inst->module;
        uint32 i;
        for (i = 0; i < wasm_module->export_count; i++) {
            const WASMExport *wasm_export = &wasm_module->exports[i];
            if ((wasm_export->kind == WASM_IMPORT_EXPORT_KIND_GLOBAL)
                && !strcmp(wasm_export->name, name)) {
                const WASMModuleInstanceExtra *e =
                    (WASMModuleInstanceExtra *)wasm_module_inst->e;
                const WASMGlobalInstance *global =
                    &e->globals[wasm_export->index];
                global_inst->kind = val_type_to_val_kind(global->type);
                global_inst->is_mutable = global->is_mutable;
#if WASM_ENABLE_MULTI_MODULE == 0
                global_inst->global_data =
                    wasm_module_inst->global_data + global->data_offset;
#else
                global_inst->global_data =
                    global->import_global_inst
                        ? global->import_module_inst->global_data
                              + global->import_global_inst->data_offset
                        : wasm_module_inst->global_data + global->data_offset;
#endif
                return true;
            }
        }
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT) {
        const AOTModuleInstance *aot_module_inst =
            (AOTModuleInstance *)module_inst;
        const AOTModule *aot_module = (AOTModule *)aot_module_inst->module;
        uint32 i;
        for (i = 0; i < aot_module->export_count; i++) {
            const AOTExport *aot_export = &aot_module->exports[i];
            if ((aot_export->kind == WASM_IMPORT_EXPORT_KIND_GLOBAL)
                && !strcmp(aot_export->name, name)) {
                const AOTGlobal *global =
                    &aot_module->globals[aot_export->index];
                global_inst->kind = val_type_to_val_kind(global->type.val_type);
                global_inst->is_mutable = global->type.is_mutable;
                global_inst->global_data =
                    aot_module_inst->global_data + global->data_offset;
                return true;
            }
        }
    }
#endif

    return false;
}

bool
wasm_runtime_get_export_table_inst(WASMModuleInstanceCommon *const module_inst,
                                   char const *name,
                                   wasm_table_inst_t *table_inst)
{
#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        const WASMModuleInstance *wasm_module_inst =
            (const WASMModuleInstance *)module_inst;
        const WASMModule *wasm_module = wasm_module_inst->module;
        uint32 i;
        for (i = 0; i < wasm_module->export_count; i++) {
            const WASMExport *wasm_export = &wasm_module->exports[i];
            if ((wasm_export->kind == WASM_IMPORT_EXPORT_KIND_TABLE)
                && !strcmp(wasm_export->name, name)) {
                const WASMTableInstance *wasm_table_inst =
                    wasm_module_inst->tables[wasm_export->index];
                table_inst->elem_kind =
                    val_type_to_val_kind(wasm_table_inst->elem_type);
                table_inst->cur_size = wasm_table_inst->cur_size;
                table_inst->max_size = wasm_table_inst->max_size;
                table_inst->elems = (void *)wasm_table_inst->elems;
                return true;
            }
        }
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT) {
        const AOTModuleInstance *aot_module_inst =
            (AOTModuleInstance *)module_inst;
        const AOTModule *aot_module = (AOTModule *)aot_module_inst->module;
        uint32 i;
        for (i = 0; i < aot_module->export_count; i++) {
            const AOTExport *aot_export = &aot_module->exports[i];
            if ((aot_export->kind == WASM_IMPORT_EXPORT_KIND_TABLE)
                && !strcmp(aot_export->name, name)) {
                const AOTTableInstance *aot_table_inst =
                    aot_module_inst->tables[aot_export->index];
                table_inst->elem_kind =
                    val_type_to_val_kind(aot_table_inst->elem_type);
                table_inst->cur_size = aot_table_inst->cur_size;
                table_inst->max_size = aot_table_inst->max_size;
                table_inst->elems = (void *)aot_table_inst->elems;
                return true;
            }
        }
    }
#endif

    return false;
}

WASMFunctionInstanceCommon *
wasm_table_get_func_inst(struct WASMModuleInstanceCommon *const module_inst,
                         const wasm_table_inst_t *table_inst, uint32_t idx)
{
    if (!table_inst) {
        bh_assert(0);
        return NULL;
    }

    if (idx >= table_inst->cur_size) {
        bh_assert(0);
        return NULL;
    }

#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode) {
        const WASMModuleInstance *wasm_module_inst =
            (const WASMModuleInstance *)module_inst;
        table_elem_type_t tbl_elem_val =
            ((table_elem_type_t *)table_inst->elems)[idx];
        if (tbl_elem_val == NULL_REF) {
            return NULL;
        }

#if WASM_ENABLE_GC == 0
        uint32 func_idx = (uint32)tbl_elem_val;
#else
        uint32 func_idx =
            wasm_func_obj_get_func_idx_bound((WASMFuncObjectRef)tbl_elem_val);
#endif

        bh_assert(func_idx < wasm_module_inst->e->function_count);
        return wasm_module_inst->e->functions + func_idx;
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT) {
        AOTModuleInstance *aot_module_inst = (AOTModuleInstance *)module_inst;
        uint32 func_idx;
        table_elem_type_t tbl_elem_val =
            ((table_elem_type_t *)table_inst->elems)[idx];
        if (tbl_elem_val == NULL_REF) {
            return NULL;
        }

#if WASM_ENABLE_GC == 0
        func_idx = (uint32)tbl_elem_val;
#else
        func_idx =
            wasm_func_obj_get_func_idx_bound((WASMFuncObjectRef)tbl_elem_val);
#endif

        return aot_get_function_instance(aot_module_inst, func_idx);
    }
#endif

    return NULL;
}

void *
wasm_runtime_get_function_attachment(WASMExecEnv *exec_env)
{
    return exec_env->attachment;
}

void
wasm_runtime_set_user_data(WASMExecEnv *exec_env, void *user_data)
{
    exec_env->user_data = user_data;
}

void *
wasm_runtime_get_user_data(WASMExecEnv *exec_env)
{
    return exec_env->user_data;
}

void
wasm_runtime_set_native_stack_boundary(WASMExecEnv *exec_env,
                                       uint8 *native_stack_boundary)
{
    exec_env->user_native_stack_boundary = native_stack_boundary;
}

#ifdef OS_ENABLE_HW_BOUND_CHECK
void
wasm_runtime_access_exce_check_guard_page()
{
    if (exec_env_tls && exec_env_tls->handle == os_self_thread()) {
        uint32 page_size = os_getpagesize();
        memset(exec_env_tls->exce_check_guard_page, 0, page_size);
    }
}
#endif

WASMFuncType *
wasm_runtime_get_function_type(const WASMFunctionInstanceCommon *function,
                               uint32 module_type)
{
    WASMFuncType *type = NULL;

#if WASM_ENABLE_INTERP != 0
    if (module_type == Wasm_Module_Bytecode) {
        WASMFunctionInstance *wasm_func = (WASMFunctionInstance *)function;
        type = wasm_func->is_import_func ? wasm_func->u.func_import->func_type
                                         : wasm_func->u.func->func_type;
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module_type == Wasm_Module_AoT) {
        AOTFunctionInstance *aot_func = (AOTFunctionInstance *)function;
        type = aot_func->is_import_func ? aot_func->u.func_import->func_type
                                        : aot_func->u.func.func_type;
    }
#endif

    return type;
}

WASMFunctionInstanceCommon *
wasm_runtime_lookup_function(WASMModuleInstanceCommon *const module_inst,
                             const char *name)
{
#if WASM_ENABLE_INTERP != 0
    if (module_inst->module_type == Wasm_Module_Bytecode)
        return (WASMFunctionInstanceCommon *)wasm_lookup_function(
            (const WASMModuleInstance *)module_inst, name);
#endif
#if WASM_ENABLE_AOT != 0
    if (module_inst->module_type == Wasm_Module_AoT)
        return (WASMFunctionInstanceCommon *)aot_lookup_function(
            (const AOTModuleInstance *)module_inst, name);
#endif
    return NULL;
}

uint32
wasm_func_get_param_count(WASMFunctionInstanceCommon *const func_inst,
                          WASMModuleInstanceCommon *const module_inst)
{
    WASMFuncType *type =
        wasm_runtime_get_function_type(func_inst, module_inst->module_type);
    bh_assert(type);

    return type->param_count;
}

uint32
wasm_func_get_result_count(WASMFunctionInstanceCommon *const func_inst,
                           WASMModuleInstanceCommon *const module_inst)
{
    WASMFuncType *type =
        wasm_runtime_get_function_type(func_inst, module_inst->module_type);
    bh_assert(type);

    return type->result_count;
}

static uint8
val_type_to_val_kind(uint8 value_type)
{
    switch (value_type) {
        case VALUE_TYPE_I32:
            return WASM_I32;
        case VALUE_TYPE_I64:
            return WASM_I64;
        case VALUE_TYPE_F32:
            return WASM_F32;
        case VALUE_TYPE_F64:
            return WASM_F64;
        case VALUE_TYPE_V128:
            return WASM_V128;
        case VALUE_TYPE_FUNCREF:
            return WASM_FUNCREF;
        case VALUE_TYPE_EXTERNREF:
            return WASM_EXTERNREF;
        default:
            bh_assert(0);
            return 0;
    }
}

void
wasm_func_get_param_types(WASMFunctionInstanceCommon *const func_inst,
                          WASMModuleInstanceCommon *const module_inst,
                          wasm_valkind_t *param_types)
{
    WASMFuncType *type =
        wasm_runtime_get_function_type(func_inst, module_inst->module_type);
    uint32 i;

    bh_assert(type);

    for (i = 0; i < type->param_count; i++) {
        param_types[i] = val_type_to_val_kind(type->types[i]);
    }
}

void
wasm_func_get_result_types(WASMFunctionInstanceCommon *const func_inst,
                           WASMModuleInstanceCommon *const module_inst,
                           wasm_valkind_t *result_types)
{
    WASMFuncType *type =
        wasm_runtime_get_function_type(func_inst, module_inst->module_type);
    uint32 i;

    bh_assert(type);

    for (i = 0; i < type->result_count; i++) {
        result_types[i] =
            val_type_to_val_kind(type->types[type->param_count + i]);
    }
}

#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
/* (uintptr_t)externref -> (uint32)index */
/*   argv               ->   *ret_argv */
static bool
wasm_runtime_prepare_call_function(WASMExecEnv *exec_env,
                                   WASMFunctionInstanceCommon *function,
                                   uint32 *argv, uint32 argc, uint32 **ret_argv,
                                   uint32 *ret_argc_param,
                                   uint32 *ret_argc_result)
{
    uint32 *new_argv = NULL, argv_i = 0, new_argv_i = 0, param_i = 0,
           result_i = 0;
    bool need_param_transform = false, need_result_transform = false;
    uint64 size = 0;
    WASMFuncType *func_type = wasm_runtime_get_function_type(
        function, exec_env->module_inst->module_type);

    bh_assert(func_type);

    *ret_argc_param = func_type->param_cell_num;
    *ret_argc_result = func_type->ret_cell_num;
    for (param_i = 0; param_i < func_type->param_count; param_i++) {
        if (VALUE_TYPE_EXTERNREF == func_type->types[param_i]) {
            need_param_transform = true;
        }
    }

    for (result_i = 0; result_i < func_type->result_count; result_i++) {
        if (VALUE_TYPE_EXTERNREF
            == func_type->types[func_type->param_count + result_i]) {
            need_result_transform = true;
        }
    }

    if (!need_param_transform && !need_result_transform) {
        *ret_argv = argv;
        return true;
    }

    if (func_type->param_cell_num >= func_type->ret_cell_num) {
        size = sizeof(uint32) * func_type->param_cell_num;
    }
    else {
        size = sizeof(uint32) * func_type->ret_cell_num;
    }

    if (!(new_argv = runtime_malloc(size, exec_env->module_inst, NULL, 0))) {
        return false;
    }

    if (!need_param_transform) {
        bh_memcpy_s(new_argv, (uint32)size, argv, (uint32)size);
    }
    else {
        for (param_i = 0; param_i < func_type->param_count && argv_i < argc
                          && new_argv_i < func_type->param_cell_num;
             param_i++) {
            uint8 param_type = func_type->types[param_i];
            if (VALUE_TYPE_EXTERNREF == param_type) {
                void *externref_obj;
                uint32 externref_index;

#if UINTPTR_MAX == UINT32_MAX
                externref_obj = (void *)argv[argv_i];
#else
                union {
                    uintptr_t val;
                    uint32 parts[2];
                } u;

                u.parts[0] = argv[argv_i];
                u.parts[1] = argv[argv_i + 1];
                externref_obj = (void *)u.val;
#endif
                if (!wasm_externref_obj2ref(exec_env->module_inst,
                                            externref_obj, &externref_index)) {
                    wasm_runtime_free(new_argv);
                    return false;
                }

                new_argv[new_argv_i] = externref_index;
                argv_i += sizeof(uintptr_t) / sizeof(uint32);
                new_argv_i++;
            }
            else {
                uint16 param_cell_num = wasm_value_type_cell_num(param_type);
                uint32 param_size = sizeof(uint32) * param_cell_num;
                bh_memcpy_s(new_argv + new_argv_i, param_size, argv + argv_i,
                            param_size);
                argv_i += param_cell_num;
                new_argv_i += param_cell_num;
            }
        }
    }

    *ret_argv = new_argv;
    return true;
}

/* (uintptr_t)externref <- (uint32)index */
/*   argv               <-   new_argv */
static bool
wasm_runtime_finalize_call_function(WASMExecEnv *exec_env,
                                    WASMFunctionInstanceCommon *function,
                                    uint32 *argv, uint32 argc, uint32 *ret_argv)
{
    uint32 argv_i = 0, result_i = 0, ret_argv_i = 0;
    WASMFuncType *func_type;

    bh_assert((argv && ret_argv) || (argc == 0));

    if (argv == ret_argv) {
        /* no need to transform externref results */
        return true;
    }

    func_type = wasm_runtime_get_function_type(
        function, exec_env->module_inst->module_type);
    bh_assert(func_type);

    for (result_i = 0; result_i < func_type->result_count && argv_i < argc;
         result_i++) {
        uint8 result_type = func_type->types[func_type->param_count + result_i];
        if (result_type == VALUE_TYPE_EXTERNREF) {
            void *externref_obj;
#if UINTPTR_MAX != UINT32_MAX
            union {
                uintptr_t val;
                uint32 parts[2];
            } u;
#endif

            if (!wasm_externref_ref2obj(argv[argv_i], &externref_obj)) {
                wasm_runtime_free(argv);
                return false;
            }

#if UINTPTR_MAX == UINT32_MAX
            ret_argv[ret_argv_i] = (uintptr_t)externref_obj;
#else
            u.val = (uintptr_t)externref_obj;
            ret_argv[ret_argv_i] = u.parts[0];
            ret_argv[ret_argv_i + 1] = u.parts[1];
#endif
            argv_i += 1;
            ret_argv_i += sizeof(uintptr_t) / sizeof(uint32);
        }
        else {
            uint16 result_cell_num = wasm_value_type_cell_num(result_type);
            uint32 result_size = sizeof(uint32) * result_cell_num;
            bh_memcpy_s(ret_argv + ret_argv_i, result_size, argv + argv_i,
                        result_size);
            argv_i += result_cell_num;
            ret_argv_i += result_cell_num;
        }
    }

    wasm_runtime_free(argv);
    return true;
}
#endif

bool
wasm_runtime_call_wasm(WASMExecEnv *exec_env,
                       WASMFunctionInstanceCommon *function, uint32 argc,
                       uint32 argv[])
{
    bool ret = false;
    uint32 *new_argv = NULL, param_argc;
#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    uint32 result_argc = 0;
#endif

    if (!wasm_runtime_exec_env_check(exec_env)) {
        LOG_ERROR("Invalid exec env stack info.");
        return false;
    }

#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    if (!wasm_runtime_prepare_call_function(exec_env, function, argv, argc,
                                            &new_argv, &param_argc,
                                            &result_argc)) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "the arguments conversion is failed");
        return false;
    }
#else
    new_argv = argv;
    param_argc = argc;
#endif

#if WASM_ENABLE_INTERP != 0
    if (exec_env->module_inst->module_type == Wasm_Module_Bytecode)
        ret = wasm_call_function(exec_env, (WASMFunctionInstance *)function,
                                 param_argc, new_argv);
#endif
#if WASM_ENABLE_AOT != 0
    if (exec_env->module_inst->module_type == Wasm_Module_AoT)
        ret = aot_call_function(exec_env, (AOTFunctionInstance *)function,
                                param_argc, new_argv);
#endif
    if (!ret) {
        if (new_argv != argv) {
            wasm_runtime_free(new_argv);
        }
        return false;
    }

#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    if (!wasm_runtime_finalize_call_function(exec_env, function, new_argv,
                                             result_argc, argv)) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "the result conversion is failed");
        return false;
    }
#endif

    return ret;
}

static void
parse_args_to_uint32_array(WASMFuncType *type, wasm_val_t *args,
                           uint32 *out_argv)
{
    uint32 i, p;

    for (i = 0, p = 0; i < type->param_count; i++) {
        switch (args[i].kind) {
            case WASM_I32:
                out_argv[p++] = args[i].of.i32;
                break;
            case WASM_I64:
            {
                union {
                    uint64 val;
                    uint32 parts[2];
                } u;
                u.val = args[i].of.i64;
                out_argv[p++] = u.parts[0];
                out_argv[p++] = u.parts[1];
                break;
            }
            case WASM_F32:
            {
                union {
                    float32 val;
                    uint32 part;
                } u;
                u.val = args[i].of.f32;
                out_argv[p++] = u.part;
                break;
            }
            case WASM_F64:
            {
                union {
                    float64 val;
                    uint32 parts[2];
                } u;
                u.val = args[i].of.f64;
                out_argv[p++] = u.parts[0];
                out_argv[p++] = u.parts[1];
                break;
            }
            case WASM_V128:
            {
                bh_assert(0);
                break;
            }
#if WASM_ENABLE_REF_TYPES != 0
#if WASM_ENABLE_GC == 0
            case WASM_FUNCREF:
            {
                out_argv[p++] = args[i].of.i32;
                break;
            }
#else
            case WASM_FUNCREF:
#endif
            case WASM_EXTERNREF:
            {
#if UINTPTR_MAX == UINT32_MAX
                out_argv[p++] = args[i].of.foreign;
#else
                union {
                    uintptr_t val;
                    uint32 parts[2];
                } u;

                u.val = (uintptr_t)args[i].of.foreign;
                out_argv[p++] = u.parts[0];
                out_argv[p++] = u.parts[1];
#endif
                break;
            }
#endif
            default:
                bh_assert(0);
                break;
        }
    }
}

static void
parse_uint32_array_to_results(WASMFuncType *type, uint32 *argv,
                              wasm_val_t *out_results)
{
    uint32 i, p;

    for (i = 0, p = 0; i < type->result_count; i++) {
        switch (type->types[type->param_count + i]) {
            case VALUE_TYPE_I32:
                out_results[i].kind = WASM_I32;
                out_results[i].of.i32 = (int32)argv[p++];
                break;
            case VALUE_TYPE_I64:
            {
                union {
                    uint64 val;
                    uint32 parts[2];
                } u;
                u.parts[0] = argv[p++];
                u.parts[1] = argv[p++];
                out_results[i].kind = WASM_I64;
                out_results[i].of.i64 = u.val;
                break;
            }
            case VALUE_TYPE_F32:
            {
                union {
                    float32 val;
                    uint32 part;
                } u;
                u.part = argv[p++];
                out_results[i].kind = WASM_F32;
                out_results[i].of.f32 = u.val;
                break;
            }
            case VALUE_TYPE_F64:
            {
                union {
                    float64 val;
                    uint32 parts[2];
                } u;
                u.parts[0] = argv[p++];
                u.parts[1] = argv[p++];
                out_results[i].kind = WASM_F64;
                out_results[i].of.f64 = u.val;
                break;
            }
            case VALUE_TYPE_V128:
            {
                bh_assert(0);
                break;
            }
#if WASM_ENABLE_REF_TYPES != 0
#if WASM_ENABLE_GC == 0
            case VALUE_TYPE_FUNCREF:
            {
                out_results[i].kind = WASM_I32;
                out_results[i].of.i32 = (int32)argv[p++];
                break;
            }
            case VALUE_TYPE_EXTERNREF:
#else
            case REF_TYPE_FUNCREF:
            case REF_TYPE_EXTERNREF:
            case REF_TYPE_ANYREF:
            case REF_TYPE_EQREF:
            case REF_TYPE_HT_NULLABLE:
            case REF_TYPE_HT_NON_NULLABLE:
            case REF_TYPE_I31REF:
            case REF_TYPE_NULLFUNCREF:
            case REF_TYPE_NULLEXTERNREF:
            case REF_TYPE_STRUCTREF:
            case REF_TYPE_ARRAYREF:
            case REF_TYPE_NULLREF:
#endif /* end of WASM_ENABLE_GC == 0 */
            {
#if UINTPTR_MAX == UINT32_MAX
                out_results[i].kind = WASM_EXTERNREF;
                out_results[i].of.foreign = (uintptr_t)argv[p++];
#else
                union {
                    uintptr_t val;
                    uint32 parts[2];
                } u;
                u.parts[0] = argv[p++];
                u.parts[1] = argv[p++];
                out_results[i].kind = WASM_EXTERNREF;
                out_results[i].of.foreign = u.val;
#endif
                break;
            }
#endif /* end of WASM_ENABLE_REF_TYPES != 0 */
            default:
                bh_assert(0);
                break;
        }
    }
}

bool
wasm_runtime_call_wasm_a(WASMExecEnv *exec_env,
                         WASMFunctionInstanceCommon *function,
                         uint32 num_results, wasm_val_t results[],
                         uint32 num_args, wasm_val_t args[])
{
    uint32 argc, argv_buf[16] = { 0 }, *argv = argv_buf, cell_num, module_type;
#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    uint32 i, param_size_in_double_world = 0, result_size_in_double_world = 0;
#endif
    uint64 total_size;
    WASMFuncType *type;
    bool ret = false;

    module_type = exec_env->module_inst->module_type;
    type = wasm_runtime_get_function_type(function, module_type);

    if (!type) {
        LOG_ERROR("Function type get failed, WAMR Interpreter and AOT must be "
                  "enabled at least one.");
        goto fail1;
    }

#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    for (i = 0; i < type->param_count; i++) {
        param_size_in_double_world +=
            wasm_value_type_cell_num_outside(type->types[i]);
    }
    for (i = 0; i < type->result_count; i++) {
        result_size_in_double_world += wasm_value_type_cell_num_outside(
            type->types[type->param_count + i]);
    }
    argc = param_size_in_double_world;
    cell_num = (argc >= result_size_in_double_world)
                   ? argc
                   : result_size_in_double_world;
#else
    argc = type->param_cell_num;
    cell_num = (argc > type->ret_cell_num) ? argc : type->ret_cell_num;
#endif

    if (num_results != type->result_count) {
        LOG_ERROR(
            "The result value number does not match the function declaration.");
        goto fail1;
    }

    if (num_args != type->param_count) {
        LOG_ERROR("The argument value number does not match the function "
                  "declaration.");
        goto fail1;
    }

    total_size = sizeof(uint32) * (uint64)(cell_num > 2 ? cell_num : 2);
    if (total_size > sizeof(argv_buf)) {
        if (!(argv =
                  runtime_malloc(total_size, exec_env->module_inst, NULL, 0))) {
            goto fail1;
        }
    }

    parse_args_to_uint32_array(type, args, argv);
    if (!(ret = wasm_runtime_call_wasm(exec_env, function, argc, argv)))
        goto fail2;

    parse_uint32_array_to_results(type, argv, results);

fail2:
    if (argv != argv_buf)
        wasm_runtime_free(argv);
fail1:
    return ret;
}

bool
wasm_runtime_call_wasm_v(WASMExecEnv *exec_env,
                         WASMFunctionInstanceCommon *function,
                         uint32 num_results, wasm_val_t results[],
                         uint32 num_args, ...)
{
    wasm_val_t args_buf[8] = { 0 }, *args = args_buf;
    WASMFuncType *type = NULL;
    bool ret = false;
    uint64 total_size;
    uint32 i = 0, module_type;
    va_list vargs;

    module_type = exec_env->module_inst->module_type;
    type = wasm_runtime_get_function_type(function, module_type);

    if (!type) {
        LOG_ERROR("Function type get failed, WAMR Interpreter and AOT "
                  "must be enabled at least one.");
        goto fail1;
    }

    if (num_args != type->param_count) {
        LOG_ERROR("The argument value number does not match the "
                  "function declaration.");
        goto fail1;
    }

    total_size = sizeof(wasm_val_t) * (uint64)num_args;
    if (total_size > sizeof(args_buf)) {
        if (!(args =
                  runtime_malloc(total_size, exec_env->module_inst, NULL, 0))) {
            goto fail1;
        }
    }

    va_start(vargs, num_args);
    for (i = 0; i < num_args; i++) {
        switch (type->types[i]) {
            case VALUE_TYPE_I32:
                args[i].kind = WASM_I32;
                args[i].of.i32 = va_arg(vargs, uint32);
                break;
            case VALUE_TYPE_I64:
                args[i].kind = WASM_I64;
                args[i].of.i64 = va_arg(vargs, uint64);
                break;
            case VALUE_TYPE_F32:
                args[i].kind = WASM_F32;
                args[i].of.f32 = (float32)va