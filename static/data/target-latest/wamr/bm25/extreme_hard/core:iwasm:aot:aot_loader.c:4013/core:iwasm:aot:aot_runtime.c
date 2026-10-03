/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_runtime.h"
#include "../compilation/aot_stack_frame.h"
#include "bh_log.h"
#include "mem_alloc.h"
#include "../common/wasm_runtime_common.h"
#include "../common/wasm_memory.h"
#include "../interpreter/wasm_runtime.h"
#if WASM_ENABLE_SHARED_MEMORY != 0
#include "../common/wasm_shared_memory.h"
#endif
#if WASM_ENABLE_THREAD_MGR != 0
#include "../libraries/thread-mgr/thread_manager.h"
#endif

/*
 * Note: These offsets need to match the values hardcoded in
 * AoT compilation code: aot_create_func_context, check_suspend_flags.
 */

bh_static_assert(offsetof(WASMExecEnv, cur_frame) == 1 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, module_inst) == 2 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, argv_buf) == 3 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, native_stack_boundary)
                 == 4 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, suspend_flags) == 5 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, aux_stack_boundary)
                 == 6 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, aux_stack_bottom)
                 == 7 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, native_symbol) == 8 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, native_stack_top_min)
                 == 9 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, wasm_stack.top_boundary)
                 == 10 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, wasm_stack.top)
                 == 11 * sizeof(uintptr_t));
bh_static_assert(offsetof(WASMExecEnv, wasm_stack.bottom)
                 == 12 * sizeof(uintptr_t));

bh_static_assert(offsetof(AOTModuleInstance, memories) == 1 * sizeof(uint64));
bh_static_assert(offsetof(AOTModuleInstance, func_ptrs) == 5 * sizeof(uint64));
bh_static_assert(offsetof(AOTModuleInstance, func_type_indexes)
                 == 6 * sizeof(uint64));
bh_static_assert(offsetof(AOTModuleInstance, cur_exception)
                 == 13 * sizeof(uint64));
bh_static_assert(offsetof(AOTModuleInstance, c_api_func_imports)
                 == 13 * sizeof(uint64) + 128 + 7 * sizeof(uint64));
bh_static_assert(offsetof(AOTModuleInstance, global_table_data)
                 == 13 * sizeof(uint64) + 128 + 14 * sizeof(uint64));

bh_static_assert(sizeof(AOTMemoryInstance) == 120);
bh_static_assert(offsetof(AOTTableInstance, elems) == 24);

bh_static_assert(offsetof(AOTModuleInstanceExtra, stack_sizes) == 0);
bh_static_assert(offsetof(AOTModuleInstanceExtra, shared_heap_base_addr_adj)
                 == 8);
bh_static_assert(offsetof(AOTModuleInstanceExtra, shared_heap_start_off) == 16);

bh_static_assert(sizeof(CApiFuncImport) == sizeof(uintptr_t) * 3);

bh_static_assert(sizeof(wasm_val_t) == 16);
bh_static_assert(offsetof(wasm_val_t, of) == 8);

bh_static_assert(offsetof(AOTFrame, prev_frame) == sizeof(uintptr_t) * 0);
bh_static_assert(offsetof(AOTFrame, func_index) == sizeof(uintptr_t) * 1);
bh_static_assert(offsetof(AOTFrame, time_started) == sizeof(uintptr_t) * 2);
bh_static_assert(offsetof(AOTFrame, func_perf_prof_info)
                 == sizeof(uintptr_t) * 3);
bh_static_assert(offsetof(AOTFrame, ip_offset) == sizeof(uintptr_t) * 4);
bh_static_assert(offsetof(AOTFrame, sp) == sizeof(uintptr_t) * 5);
bh_static_assert(offsetof(AOTFrame, frame_ref) == sizeof(uintptr_t) * 6);
bh_static_assert(offsetof(AOTFrame, lp) == sizeof(uintptr_t) * 7);

bh_static_assert(offsetof(AOTTinyFrame, func_index) == sizeof(uint32) * 0);
bh_static_assert(offsetof(AOTTinyFrame, ip_offset) == sizeof(uint32) * 1);
bh_static_assert(sizeof(AOTTinyFrame) == sizeof(uint32) * 2);

static void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string)
{
    if (error_buf != NULL) {
        snprintf(error_buf, error_buf_size, "AOT module instantiate failed: %s",
                 string);
    }
}

static void
set_error_buf_v(char *error_buf, uint32 error_buf_size, const char *format, ...)
{
    va_list args;
    char buf[128];

    if (error_buf != NULL) {
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        snprintf(error_buf, error_buf_size, "AOT module instantiate failed: %s",
                 buf);
    }
}

static void *
runtime_malloc(uint64 size, char *error_buf, uint32 error_buf_size)
{
    void *mem;

    if (size >= UINT32_MAX || !(mem = wasm_runtime_malloc((uint32)size))) {
        set_error_buf(error_buf, error_buf_size, "allocate memory failed");
        return NULL;
    }

    memset(mem, 0, (uint32)size);
    return mem;
}

#if WASM_ENABLE_AOT_STACK_FRAME != 0
static bool
is_tiny_frame(WASMExecEnv *exec_env)
{
    AOTModule *module =
        (AOTModule *)((AOTModuleInstance *)exec_env->module_inst)->module;

    return module->feature_flags & WASM_FEATURE_TINY_STACK_FRAME;
}

static bool
is_frame_per_function(WASMExecEnv *exec_env)
{
    AOTModule *module =
        (AOTModule *)((AOTModuleInstance *)exec_env->module_inst)->module;

    return module->feature_flags & WASM_FEATURE_FRAME_PER_FUNCTION;
}

#if WASM_ENABLE_DUMP_CALL_STACK != 0
static bool
is_frame_func_idx_disabled(WASMExecEnv *exec_env)
{
    AOTModule *module =
        (AOTModule *)((AOTModuleInstance *)exec_env->module_inst)->module;

    return module->feature_flags & WASM_FEATURE_FRAME_NO_FUNC_IDX;
}
#endif

static void *
get_top_frame(WASMExecEnv *exec_env)
{
    if (is_tiny_frame(exec_env)) {
        return exec_env->wasm_stack.top > exec_env->wasm_stack.bottom
                   ? exec_env->wasm_stack.top - sizeof(AOTTinyFrame)
                   : NULL;
    }
    else {
        return exec_env->cur_frame;
    }
}

static void *
get_prev_frame(WASMExecEnv *exec_env, void *cur_frame)
{
    bh_assert(cur_frame);

    if (is_tiny_frame(exec_env)) {
        if ((uint8 *)cur_frame == exec_env->wasm_stack.bottom) {
            return NULL;
        }
        return ((AOTTinyFrame *)cur_frame) - 1;
    }
    else {
        return ((AOTFrame *)cur_frame)->prev_frame;
    }
}
#endif

static bool
check_global_init_expr(const AOTModule *module, uint32 global_index,
                       char *error_buf, uint32 error_buf_size)
{
    if (global_index >= module->import_global_count + module->global_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown global %d",
                        global_index);
        return false;
    }

    /**
     * Currently, constant expressions occurring as initializers of
     * globals are further constrained in that contained global.get
     * instructions are only allowed to refer to imported globals.
     *
     * And initializer expression cannot reference a mutable global.
     */
    if (global_index >= module->import_global_count
    /* make spec test happy */
#if WASM_ENABLE_GC != 0
                            + module->global_count
#endif
    ) {
        set_error_buf_v(error_buf, error_buf_size, "unknown global %u",
                        global_index);
        return false;
    }

    if (
    /* make spec test happy */
#if WASM_ENABLE_GC != 0
        global_index < module->import_global_count &&
#endif
        module->import_globals[global_index].type.is_mutable) {
        set_error_buf(error_buf, error_buf_size,
                      "constant expression required");
        return false;
    }

    return true;
}

static void
init_global_data(uint8 *global_data, uint8 type, WASMValue *initial_value)
{
    switch (type) {
        case VALUE_TYPE_I32:
        case VALUE_TYPE_F32:
#if WASM_ENABLE_REF_TYPES != 0
        case VALUE_TYPE_FUNCREF:
        case VALUE_TYPE_EXTERNREF:
#endif
            *(int32 *)global_data = initial_value->i32;
            break;
        case VALUE_TYPE_I64:
        case VALUE_TYPE_F64:
            bh_memcpy_s(global_data, sizeof(int64), &initial_value->i64,
                        sizeof(int64));
            break;
#if WASM_ENABLE_SIMD != 0
        case VALUE_TYPE_V128:
            bh_memcpy_s(global_data, sizeof(V128), &initial_value->v128,
                        sizeof(V128));
            break;
#endif
        default:
#if WASM_ENABLE_GC != 0
            if ((type >= (uint8)REF_TYPE_ARRAYREF
                 && type <= (uint8)REF_TYPE_NULLFUNCREF)
                || (type >= (uint8)REF_TYPE_HT_NULLABLE
                    && type <= (uint8)REF_TYPE_HT_NON_NULLABLE)
#if WASM_ENABLE_STRINGREF != 0
                || (type >= (uint8)REF_TYPE_STRINGVIEWWTF8
                    && type <= (uint8)REF_TYPE_STRINGREF)
                || (type >= (uint8)REF_TYPE_STRINGVIEWITER
                    && type <= (uint8)REF_TYPE_STRINGVIEWWTF16)
#endif
            ) {
                bh_memcpy_s(global_data, sizeof(wasm_obj_t),
                            &initial_value->gc_obj, sizeof(wasm_obj_t));
                break;
            }
#endif /* end of WASM_ENABLE_GC */
            bh_assert(0);
    }
}

#if WASM_ENABLE_GC != 0
static bool
assign_table_init_value(AOTModuleInstance *module_inst, AOTModule *module,
                        InitializerExpression *init_expr, void *addr,
                        char *error_buf, uint32 error_buf_size)
{
    uint8 flag = init_expr->init_expr_type;

    bh_assert(flag >= INIT_EXPR_TYPE_GET_GLOBAL
              && flag <= INIT_EXPR_TYPE_EXTERN_CONVERT_ANY);

    switch (flag) {
        case INIT_EXPR_TYPE_GET_GLOBAL:
        {
            if (!check_global_init_expr(module, init_expr->u.global_index,
                                        error_buf, error_buf_size)) {
                return false;
            }
            if (init_expr->u.global_index < module->import_global_count) {
                PUT_REF_TO_ADDR(
                    addr, module->import_globals[init_expr->u.global_index]
                              .global_data_linked.gc_obj);
            }
            else {
                uint32 global_idx =
                    init_expr->u.global_index - module->import_global_count;
                return assign_table_init_value(
                    module_inst, module, &module->globals[global_idx].init_expr,
                    addr, error_buf, error_buf_size);
            }
            break;
        }
        case INIT_EXPR_TYPE_REFNULL_CONST:
        {
            WASMObjectRef gc_obj = NULL_REF;
            PUT_REF_TO_ADDR(addr, gc_obj);
            break;
        }
        case INIT_EXPR_TYPE_FUNCREF_CONST:
        {
            WASMFuncObjectRef func_obj = NULL;
            uint32 func_idx = init_expr->u.u32;

            if (func_idx != UINT32_MAX) {
                if (!(func_obj =
                          aot_create_func_obj(module_inst, func_idx, false,
                                              error_buf, error_buf_size))) {
                    return false;
                }
            }

            PUT_REF_TO_ADDR(addr, func_obj);
            break;
        }
        case INIT_EXPR_TYPE_I31_NEW:
        {
            WASMI31ObjectRef i31_obj = wasm_i31_obj_new(init_expr->u.i32);
            PUT_REF_TO_ADDR(addr, i31_obj);
            break;
        }
        case INIT_EXPR_TYPE_STRUCT_NEW:
        case INIT_EXPR_TYPE_STRUCT_NEW_DEFAULT:
        {
            WASMRttType *rtt_type;
            WASMStructObjectRef struct_obj;
            WASMStructType *struct_type;
            WASMStructNewInitValues *init_values = NULL;
            uint32 type_idx;

            if (flag == INIT_EXPR_TYPE_STRUCT_NEW) {
                init_values = (WASMStructNewInitValues *)init_expr->u.data;
                type_idx = init_values->type_idx;
            }
            else {
                type_idx = init_expr->u.type_index;
            }

            struct_type = (WASMStructType *)module->types[type_idx];

            if (!(rtt_type = wasm_rtt_type_new(
                      (WASMType *)struct_type, type_idx, module->rtt_types,
                      module->type_count, &module->rtt_type_lock))) {
                set_error_buf(error_buf, error_buf_size,
                              "create rtt object failed");
                return false;
            }

            if (!(struct_obj = wasm_struct_obj_new_internal(
                      ((AOTModuleInstanceExtra *)module_inst->e)
                          ->common.gc_heap_handle,
                      rtt_type))) {
                set_error_buf(error_buf, error_buf_size,
                              "create struct object failed");
                return false;
            }

            if (flag == INIT_EXPR_TYPE_STRUCT_NEW) {
                uint32 field_idx;

                bh_assert(init_values->count == struct_type->field_count);

                for (field_idx = 0; field_idx < init_values->count;
                     field_idx++) {
                    wasm_struct_obj_set_field(struct_obj, field_idx,
                                              &init_values->fields[field_idx]);
                }
            }

            PUT_REF_TO_ADDR(addr, struct_obj);
            break;
        }
        case INIT_EXPR_TYPE_ARRAY_NEW:
        case INIT_EXPR_TYPE_ARRAY_NEW_DEFAULT:
        case INIT_EXPR_TYPE_ARRAY_NEW_FIXED:
        {
            WASMRttType *rtt_type;
            WASMArrayObjectRef array_obj;
            WASMArrayType *array_type;
            WASMArrayNewInitValues *init_values = NULL;
            WASMValue *arr_init_val = NULL, empty_val = { 0 };
            uint32 type_idx, len;

            if (flag == INIT_EXPR_TYPE_ARRAY_NEW_DEFAULT) {
                type_idx = init_expr->u.array_new_default.type_index;
                len = init_expr->u.array_new_default.length;
                arr_init_val = &empty_val;
            }
            else {
                init_values = (WASMArrayNewInitValues *)init_expr->u.data;
                type_idx = init_values->type_idx;
                len = init_values->length;

                if (flag == INIT_EXPR_TYPE_ARRAY_NEW) {
                    arr_init_val = init_values->elem_data;
                }
            }

            array_type = (WASMArrayType *)module->types[type_idx];

            if (!(rtt_type = wasm_rtt_type_new(
                      (WASMType *)array_type, type_idx, module->rtt_types,
                      module->type_count, &module->rtt_type_lock))) {
                set_error_buf(error_buf, error_buf_size,
                              "create rtt object failed");
                return false;
            }

            if (!(array_obj = wasm_array_obj_new_internal(
                      ((AOTModuleInstanceExtra *)module_inst->e)
                          ->common.gc_heap_handle,
                      rtt_type, len, arr_init_val))) {
                set_error_buf(error_buf, error_buf_size,
                              "create array object failed");
                return false;
            }

            if (flag == INIT_EXPR_TYPE_ARRAY_NEW_FIXED) {
                uint32 elem_idx;

                bh_assert(init_values);

                for (elem_idx = 0; elem_idx < len; elem_idx++) {
                    wasm_array_obj_set_elem(array_obj, elem_idx,
                                            &init_values->elem_data[elem_idx]);
                }
            }

            PUT_REF_TO_ADDR(addr, array_obj);
            break;
        }
        default:
            set_error_buf(error_buf, error_buf_size, "invalid init expr type.");
            return false;
    }

    return true;
}
#endif /* end of WASM_ENABLE_GC != 0 */

static bool
global_instantiate(AOTModuleInstance *module_inst, AOTModule *module,
                   char *error_buf, uint32 error_buf_size)
{
    uint32 i;
    InitializerExpression *init_expr;
    uint8 *p = module_inst->global_data;
    AOTImportGlobal *import_global = module->import_globals;
    AOTGlobal *global = module->globals;

    /* Initialize import global data */
    for (i = 0; i < module->import_global_count; i++, import_global++) {
        bh_assert(import_global->data_offset
                  == (uint32)(p - module_inst->global_data));
        init_global_data(p, import_global->type.val_type,
                         &import_global->global_data_linked);
        p += import_global->size;
    }

    /* Initialize defined global data */
    for (i = 0; i < module->global_count; i++, global++) {
        uint8 flag;
        bh_assert(global->data_offset
                  == (uint32)(p - module_inst->global_data));
        init_expr = &global->init_expr;
        flag = init_expr->init_expr_type;
        switch (flag) {
            case INIT_EXPR_TYPE_GET_GLOBAL:
            {
                if (!check_global_init_expr(module, init_expr->u.global_index,
                                            error_buf, error_buf_size)) {
                    return false;
                }
#if WASM_ENABLE_GC == 0
                init_global_data(
                    p, global->type.val_type,
                    &module->import_globals[init_expr->u.global_index]
                         .global_data_linked);
#else
                if (init_expr->u.global_index < module->import_global_count) {
                    init_global_data(
                        p, global->type.val_type,
                        &module->import_globals[init_expr->u.global_index]
                             .global_data_linked);
                }
                else {
                    uint32 global_idx =
                        init_expr->u.global_index - module->import_global_count;
                    init_global_data(p, global->type.val_type,
                                     &module->globals[global_idx].init_expr.u);
                }
#endif
                break;
            }
#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
            case INIT_EXPR_TYPE_REFNULL_CONST:
            {
                *(uint32 *)p = NULL_REF;
                break;
            }
#elif WASM_ENABLE_GC != 0
            case INIT_EXPR_TYPE_REFNULL_CONST:
            {
                WASMObjectRef gc_obj = NULL_REF;
                PUT_REF_TO_ADDR(p, gc_obj);
                break;
            }
#endif
#if WASM_ENABLE_GC != 0
            case INIT_EXPR_TYPE_FUNCREF_CONST:
            {
                WASMFuncObjectRef func_obj = NULL;
                uint32 func_idx = init_expr->u.u32;

                if (func_idx != UINT32_MAX) {
                    if (!(func_obj =
                              aot_create_func_obj(module_inst, func_idx, false,
                                                  error_buf, error_buf_size))) {
                        return false;
                    }
                }

                PUT_REF_TO_ADDR(p, func_obj);
                break;
            }
            case INIT_EXPR_TYPE_I31_NEW:
            {
                WASMI31ObjectRef i31_obj = wasm_i31_obj_new(init_expr->u.i32);
                PUT_REF_TO_ADDR(p, i31_obj);
                break;
            }
            case INIT_EXPR_TYPE_STRUCT_NEW:
            case INIT_EXPR_TYPE_STRUCT_NEW_DEFAULT:
            {
                WASMRttType *rtt_type;
                WASMStructObjectRef struct_obj;
                WASMStructType *struct_type;
                WASMStructNewInitValues *init_values = NULL;
                uint32 type_idx;

                if (flag == INIT_EXPR_TYPE_STRUCT_NEW) {
                    init_values = (WASMStructNewInitValues *)init_expr->u.data;
                    type_idx = init_values->type_idx;
                }
                else {
                    type_idx = init_expr->u.type_index;
                }

                struct_type = (WASMStructType *)module->types[type_idx];

                if (!(rtt_type = wasm_rtt_type_new(
                          (WASMType *)struct_type, type_idx, module->rtt_types,
                          module->type_count, &module->rtt_type_lock))) {
                    set_error_buf(error_buf, error_buf_size,
                                  "create rtt object failed");
                    return false;
                }

                if (!(struct_obj = wasm_struct_obj_new_internal(
                          ((AOTModuleInstanceExtra *)module_inst->e)
                              ->common.gc_heap_handle,
                          rtt_type))) {
                    set_error_buf(error_buf, error_buf_size,
                                  "create struct object failed");
                    return false;
                }

                if (flag == INIT_EXPR_TYPE_STRUCT_NEW) {
                    uint32 field_idx;

                    bh_assert(init_values->count == struct_type->field_count);

                    for (field_idx = 0; field_idx < init_values->count;
                         field_idx++) {
                        wasm_struct_obj_set_field(
                            struct_obj, field_idx,
                            &init_values->fields[field_idx]);
                    }
                }

                PUT_REF_TO_ADDR(p, struct_obj);
                break;
            }
            case INIT_EXPR_TYPE_ARRAY_NEW:
            case INIT_EXPR_TYPE_ARRAY_NEW_DEFAULT:
            case INIT_EXPR_TYPE_ARRAY_NEW_FIXED:
            {
                WASMRttType *rtt_type;
                WASMArrayObjectRef array_obj;
                WASMArrayType *array_type;
                WASMArrayNewInitValues *init_values = NULL;
                WASMValue *arr_init_val = NULL, empty_val = { 0 };
                uint32 type_idx, len;

                if (flag == INIT_EXPR_TYPE_ARRAY_NEW_DEFAULT) {
                    type_idx = init_expr->u.array_new_default.type_index;
                    len = init_expr->u.array_new_default.length;
                    arr_init_val = &empty_val;
                }
                else {
                    init_values = (WASMArrayNewInitValues *)init_expr->u.data;
                    type_idx = init_values->type_idx;
                    len = init_values->length;

                    if (flag == INIT_EXPR_TYPE_ARRAY_NEW) {
                        arr_init_val = init_values->elem_data;
                    }
                }

                array_type = (WASMArrayType *)module->types[type_idx];

                if (!(rtt_type = wasm_rtt_type_new(
                          (WASMType *)array_type, type_idx, module->rtt_types,
                          module->type_count, &module->rtt_type_lock))) {
                    set_error_buf(error_buf, error_buf_size,
                                  "create rtt object failed");
                    return false;
                }

                if (!(array_obj = wasm_array_obj_new_internal(
                          ((AOTModuleInstanceExtra *)module_inst->e)
                              ->common.gc_heap_handle,
                          rtt_type, len, arr_init_val))) {
                    set_error_buf(error_buf, error_buf_size,
                                  "create array object failed");
                    return false;
                }

                if (flag == INIT_EXPR_TYPE_ARRAY_NEW_FIXED) {
                    uint32 elem_idx;

                    bh_assert(init_values);

                    for (elem_idx = 0; elem_idx < len; elem_idx++) {
                        wasm_array_obj_set_elem(
                            array_obj, elem_idx,
                            &init_values->elem_data[elem_idx]);
                    }
                }

                PUT_REF_TO_ADDR(p, array_obj);
                break;
            }
#endif /* end of WASM_ENABLE_GC != 0 */
            default:
            {
                init_global_data(p, global->type.val_type, &init_expr->u);
                break;
            }
        }
        p += global->size;
    }

    bh_assert(module_inst->global_data_size
              == (uint32)(p - module_inst->global_data));
    return true;
}

static bool
tables_instantiate(AOTModuleInstance *module_inst, AOTModule *module,
                   AOTTableInstance *first_tbl_inst, char *error_buf,
                   uint32 error_buf_size)
{
    uint32 i, global_index, global_data_offset, base_offset, length;
    uint64 total_size;
    AOTTableInitData *table_seg;
    AOTTableInstance *tbl_inst = first_tbl_inst;

    total_size = (uint64)sizeof(AOTTableInstance *) * module_inst->table_count;
    if (total_size > 0
        && !(module_inst->tables =
                 runtime_malloc(total_size, error_buf, error_buf_size))) {
        return false;
    }

    /*
     * treat import table like a local one until we enable module linking
     * in AOT mode
     */
    for (i = 0; i != module_inst->table_count; ++i) {
        if (i < module->import_table_count) {
            AOTImportTable *import_table = module->import_tables + i;
            tbl_inst->cur_size = import_table->table_type.init_size;
            tbl_inst->max_size =
                aot_get_imp_tbl_data_slots(import_table, false);
            tbl_inst->elem_type = module->tables[i].table_type.elem_type;
#if WASM_ENABLE_GC != 0
            tbl_inst->elem_ref_type.elem_ref_type =
                module->tables[i].