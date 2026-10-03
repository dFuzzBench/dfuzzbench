/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_loader.h"
#include "bh_platform.h"
#include "wasm.h"
#include "wasm_opcode.h"
#include "wasm_runtime.h"
#include "wasm_loader_common.h"
#include "../common/wasm_native.h"
#include "../common/wasm_memory.h"
#if WASM_ENABLE_GC != 0
#include "../common/gc/gc_type.h"
#include "../common/gc/gc_object.h"
#endif
#if WASM_ENABLE_DEBUG_INTERP != 0
#include "../libraries/debug-engine/debug_engine.h"
#endif
#if WASM_ENABLE_FAST_JIT != 0
#include "../fast-jit/jit_compiler.h"
#include "../fast-jit/jit_codecache.h"
#endif
#if WASM_ENABLE_JIT != 0
#include "../compilation/aot_llvm.h"
#endif

#ifndef TRACE_WASM_LOADER
#define TRACE_WASM_LOADER 0
#endif

/* Read a value of given type from the address pointed to by the given
   pointer and increase the pointer to the position just after the
   value being read.  */
#define TEMPLATE_READ_VALUE(Type, p) \
    (p += sizeof(Type), *(Type *)(p - sizeof(Type)))

#if WASM_ENABLE_MEMORY64 != 0
static bool
has_module_memory64(WASMModule *module)
{
    /* TODO: multi-memories for now assuming the memory idx type is consistent
     * across multi-memories */
    if (module->import_memory_count > 0)
        return !!(module->import_memories[0].u.memory.mem_type.flags
                  & MEMORY64_FLAG);
    else if (module->memory_count > 0)
        return !!(module->memories[0].flags & MEMORY64_FLAG);

    return false;
}

static bool
is_table_64bit(WASMModule *module, uint32 table_idx)
{
    if (table_idx < module->import_table_count)
        return !!(module->import_tables[table_idx].u.table.table_type.flags
                  & TABLE64_FLAG);
    else
        return !!(module->tables[table_idx].table_type.flags & TABLE64_FLAG);

    return false;
}
#endif

static void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string)
{
    wasm_loader_set_error_buf(error_buf, error_buf_size, string, false);
}

#if WASM_ENABLE_MEMORY64 != 0
static void
set_error_buf_mem_offset_out_of_range(char *error_buf, uint32 error_buf_size)
{
    if (error_buf != NULL) {
        snprintf(error_buf, error_buf_size, "offset out of range");
    }
}
#endif

static void
set_error_buf_v(char *error_buf, uint32 error_buf_size, const char *format, ...)
{
    va_list args;
    char buf[128];

    if (error_buf != NULL) {
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        snprintf(error_buf, error_buf_size, "WASM module load failed: %s", buf);
    }
}

static bool
check_buf(const uint8 *buf, const uint8 *buf_end, uint32 length,
          char *error_buf, uint32 error_buf_size)
{
    if ((uintptr_t)buf + length < (uintptr_t)buf
        || (uintptr_t)buf + length > (uintptr_t)buf_end) {
        set_error_buf(error_buf, error_buf_size,
                      "unexpected end of section or function");
        return false;
    }
    return true;
}

static bool
check_buf1(const uint8 *buf, const uint8 *buf_end, uint32 length,
           char *error_buf, uint32 error_buf_size)
{
    if ((uintptr_t)buf + length < (uintptr_t)buf
        || (uintptr_t)buf + length > (uintptr_t)buf_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }
    return true;
}

#define CHECK_BUF(buf, buf_end, length)                                    \
    do {                                                                   \
        if (!check_buf(buf, buf_end, length, error_buf, error_buf_size)) { \
            goto fail;                                                     \
        }                                                                  \
    } while (0)

#define CHECK_BUF1(buf, buf_end, length)                                    \
    do {                                                                    \
        if (!check_buf1(buf, buf_end, length, error_buf, error_buf_size)) { \
            goto fail;                                                      \
        }                                                                   \
    } while (0)

#define skip_leb(p) while (*p++ & 0x80)
#define skip_leb_int64(p, p_end) skip_leb(p)
#define skip_leb_uint32(p, p_end) skip_leb(p)
#define skip_leb_int32(p, p_end) skip_leb(p)
#define skip_leb_mem_offset(p, p_end) skip_leb(p)
#define skip_leb_memidx(p, p_end) skip_leb(p)
#if WASM_ENABLE_MULTI_MEMORY == 0
#define skip_leb_align(p, p_end) skip_leb(p)
#else
/* Skip the following memidx if applicable */
#define skip_leb_align(p, p_end)       \
    do {                               \
        if (*p++ & OPT_MEMIDX_FLAG)    \
            skip_leb_uint32(p, p_end); \
    } while (0)
#endif

#define read_uint8(p) TEMPLATE_READ_VALUE(uint8, p)
#define read_uint32(p) TEMPLATE_READ_VALUE(uint32, p)

#define read_leb_int64(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, 64, true, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (int64)res64;                                             \
    } while (0)

#if WASM_ENABLE_MEMORY64 != 0
#define read_leb_mem_offset(p, p_end, res)                                    \
    do {                                                                      \
        uint64 res64;                                                         \
        if (!read_leb((uint8 **)&p, p_end, is_memory64 ? 64 : 32, false,      \
                      &res64, error_buf, error_buf_size)) {                   \
            set_error_buf_mem_offset_out_of_range(error_buf, error_buf_size); \
            goto fail;                                                        \
        }                                                                     \
        res = (mem_offset_t)res64;                                            \
    } while (0)
#else
#define read_leb_mem_offset(p, p_end, res) read_leb_uint32(p, p_end, res)
#endif

#define read_leb_uint32(p, p_end, res)                                   \
    do {                                                                 \
        uint64 res64;                                                    \
        if (!read_leb((uint8 **)&p, p_end, 32, false, &res64, error_buf, \
                      error_buf_size))                                   \
            goto fail;                                                   \
        res = (uint32)res64;                                             \
    } while (0)

#define read_leb_int32(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, 32, true, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (int32)res64;                                             \
    } while (0)

#define read_leb_memidx(p, p_end, res) read_leb_uint32(p, p_end, res)
#if WASM_ENABLE_MULTI_MEMORY != 0
#define check_memidx(module, memidx)                                        \
    do {                                                                    \
        if (memidx >= module->import_memory_count + module->memory_count) { \
            set_error_buf_v(error_buf, error_buf_size, "unknown memory %d", \
                            memidx);                                        \
            goto fail;                                                      \
        }                                                                   \
    } while (0)
/* Bit 6(0x40) indicating the optional memidx, and reset bit 6 for
 * alignment check */
#define read_leb_memarg(p, p_end, res)                      \
    do {                                                    \
        read_leb_uint32(p, p_end, res);                     \
        if (res & OPT_MEMIDX_FLAG) {                        \
            res &= ~OPT_MEMIDX_FLAG;                        \
            read_leb_uint32(p, p_end, memidx); /* memidx */ \
            check_memidx(module, memidx);                   \
        }                                                   \
    } while (0)
#else
/* reserved byte 0x00 */
#define check_memidx(module, memidx)                                        \
    do {                                                                    \
        (void)module;                                                       \
        if (memidx != 0) {                                                  \
            set_error_buf(error_buf, error_buf_size, "zero byte expected"); \
            goto fail;                                                      \
        }                                                                   \
    } while (0)
#define read_leb_memarg(p, p_end, res) read_leb_uint32(p, p_end, res)
#endif

static char *
type2str(uint8 type)
{
    char *type_str[] = { "v128", "f64", "f32", "i64", "i32" };
#if WASM_ENABLE_GC != 0
    char *type_str_ref[] = { "stringview_iter",
                             "stringview_wtf16",
                             "(ref null ht)",
                             "(ref ht)",
                             "", /* reserved */
                             "stringview_wtf8",
                             "stringref",
                             "", /* reserved */
                             "", /* reserved */
                             "arrayref",
                             "structref",
                             "i32ref",
                             "eqref",
                             "anyref",
                             "externref",
                             "funcref",
                             "nullref",
                             "nullexternref",
                             "nullfuncref" };
#endif

    if (type >= VALUE_TYPE_V128 && type <= VALUE_TYPE_I32)
        return type_str[type - VALUE_TYPE_V128];
#if WASM_ENABLE_GC != 0
    else if (wasm_is_type_reftype(type))
        return type_str_ref[type - REF_TYPE_STRINGVIEWITER];
#endif
    else if (type == VALUE_TYPE_FUNCREF)
        return "funcref";
    else if (type == VALUE_TYPE_EXTERNREF)
        return "externref";
    else
        return "unknown type";
}

static bool
is_32bit_type(uint8 type)
{
    if (type == VALUE_TYPE_I32
        || type == VALUE_TYPE_F32
        /* the operand stack is in polymorphic state */
        || type == VALUE_TYPE_ANY
#if WASM_ENABLE_GC != 0
        || (sizeof(uintptr_t) == 4 && wasm_is_type_reftype(type))
#elif WASM_ENABLE_REF_TYPES != 0
        /* For reference types, we use uint32 index to represent
           the funcref and externref */
        || type == VALUE_TYPE_FUNCREF || type == VALUE_TYPE_EXTERNREF
#endif
    )
        return true;
    return false;
}

static bool
is_64bit_type(uint8 type)
{
    if (type == VALUE_TYPE_I64 || type == VALUE_TYPE_F64
#if WASM_ENABLE_GC != 0
        || (sizeof(uintptr_t) == 8 && wasm_is_type_reftype(type))
#endif
    )
        return true;
    return false;
}

#if WASM_ENABLE_GC != 0
static bool
is_packed_type(uint8 type)
{
    return (type == PACKED_TYPE_I8 || type == PACKED_TYPE_I16) ? true : false;
}
#endif

static bool
is_byte_a_type(uint8 type)
{
    return (is_valid_value_type_for_interpreter(type)
            || (type == VALUE_TYPE_VOID))
               ? true
               : false;
}

#if WASM_ENABLE_SIMD != 0
#if (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0)
static V128
read_i8x16(uint8 *p_buf, char *error_buf, uint32 error_buf_size)
{
    V128 result;
    uint8 i;

    for (i = 0; i != 16; ++i) {
        result.i8x16[i] = read_uint8(p_buf);
    }

    return result;
}
#endif /* end of (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0) */
#endif /* end of WASM_ENABLE_SIMD */

static void *
loader_malloc(uint64 size, char *error_buf, uint32 error_buf_size)
{
    void *mem;

    if (size >= UINT32_MAX || !(mem = wasm_runtime_malloc((uint32)size))) {
        set_error_buf(error_buf, error_buf_size, "allocate memory failed");
        return NULL;
    }

    memset(mem, 0, (uint32)size);
    return mem;
}

static void *
memory_realloc(void *mem_old, uint32 size_old, uint32 size_new, char *error_buf,
               uint32 error_buf_size)
{
    uint8 *mem_new;
    bh_assert(size_new > size_old);
    if ((mem_new = loader_malloc(size_new, error_buf, error_buf_size))) {
        bh_memcpy_s(mem_new, size_new, mem_old, size_old);
        memset(mem_new + size_old, 0, size_new - size_old);
        wasm_runtime_free(mem_old);
    }
    return mem_new;
}

#define MEM_REALLOC(mem, size_old, size_new)                               \
    do {                                                                   \
        void *mem_new = memory_realloc(mem, size_old, size_new, error_buf, \
                                       error_buf_size);                    \
        if (!mem_new)                                                      \
            goto fail;                                                     \
        mem = mem_new;                                                     \
    } while (0)

#if WASM_ENABLE_GC != 0
static bool
check_type_index(const WASMModule *module, uint32 type_count, uint32 type_index,
                 char *error_buf, uint32 error_buf_size)
{
    if (type_index >= type_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown type %d",
                        type_index);
        return false;
    }
    return true;
}

static bool
check_array_type(const WASMModule *module, uint32 type_index, char *error_buf,
                 uint32 error_buf_size)
{
    if (!check_type_index(module, module->type_count, type_index, error_buf,
                          error_buf_size)) {
        return false;
    }
    if (module->types[type_index]->type_flag != WASM_TYPE_ARRAY) {
        set_error_buf(error_buf, error_buf_size, "unkown array type");
        return false;
    }

    return true;
}
#endif

static bool
check_function_index(const WASMModule *module, uint32 function_index,
                     char *error_buf, uint32 error_buf_size)
{
    if (function_index
        >= module->import_function_count + module->function_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown function %u",
                        function_index);
        return false;
    }
    return true;
}

typedef struct InitValue {
    uint8 type;
    uint8 flag;
#if WASM_ENABLE_GC != 0
    uint8 gc_opcode;
    WASMRefType ref_type;
#endif
    WASMValue value;
} InitValue;

typedef struct ConstExprContext {
    uint32 sp;
    uint32 size;
    WASMModule *module;
    InitValue *stack;
    InitValue data[WASM_CONST_EXPR_STACK_SIZE];
} ConstExprContext;

static void
init_const_expr_stack(ConstExprContext *ctx, WASMModule *module)
{
    ctx->sp = 0;
    ctx->module = module;
    ctx->stack = ctx->data;
    ctx->size = WASM_CONST_EXPR_STACK_SIZE;
}

static bool
push_const_expr_stack(ConstExprContext *ctx, uint8 flag, uint8 type,
#if WASM_ENABLE_GC != 0
                      WASMRefType *ref_type, uint8 gc_opcode,
#endif
                      WASMValue *value, char *error_buf, uint32 error_buf_size)
{
    InitValue *cur_value;

    if (ctx->sp >= ctx->size) {
        if (ctx->stack != ctx->data) {
            MEM_REALLOC(ctx->stack, ctx->size * sizeof(InitValue),
                        (ctx->size + 4) * sizeof(InitValue));
        }
        else {
            if (!(ctx->stack =
                      loader_malloc((ctx->size + 4) * (uint64)sizeof(InitValue),
                                    error_buf, error_buf_size))) {
                goto fail;
            }
            bh_memcpy_s(ctx->stack, (ctx->size + 4) * (uint32)sizeof(InitValue),
                        ctx->data, ctx->size * (uint32)sizeof(InitValue));
        }
        ctx->size += 4;
    }

    cur_value = &ctx->stack[ctx->sp++];
    cur_value->type = type;
    cur_value->flag = flag;
    cur_value->value = *value;

#if WASM_ENABLE_GC != 0
    cur_value->gc_opcode = gc_opcode;
    if (wasm_is_type_multi_byte_type(type)) {
        bh_memcpy_s(&cur_value->ref_type, wasm_reftype_struct_size(ref_type),
                    ref_type, wasm_reftype_struct_size(ref_type));
    }
#endif

    return true;
fail:
    return false;
}

#if WASM_ENABLE_GC != 0
static void
destroy_init_expr_data_recursive(WASMModule *module, void *data)
{
    WASMStructNewInitValues *struct_init_values =
        (WASMStructNewInitValues *)data;
    WASMArrayNewInitValues *array_init_values = (WASMArrayNewInitValues *)data;
    WASMType *wasm_type;
    uint32 i;

    if (!data)
        return;

    wasm_type = module->types[struct_init_values->type_idx];

    /* The data can only be type of `WASMStructNewInitValues *`
       or `WASMArrayNewInitValues *` */
    bh_assert(wasm_type->type_flag == WASM_TYPE_STRUCT
              || wasm_type->type_flag == WASM_TYPE_ARRAY);

    if (wasm_type->type_flag == WASM_TYPE_STRUCT) {
        WASMStructType *struct_type = (WASMStructType *)wasm_type;
        WASMRefTypeMap *ref_type_map = struct_type->ref_type_maps;
        WASMRefType *ref_type;
        uint8 field_type;

        for (i = 0; i < struct_init_values->count; i++) {
            field_type = struct_type->fields[i].field_type;
            if (wasm_is_type_multi_byte_type(field_type))
                ref_type = ref_type_map->ref_type;
            else
                ref_type = NULL;
            if (wasm_reftype_is_subtype_of(field_type, ref_type,
                                           REF_TYPE_STRUCTREF, NULL,
                                           module->types, module->type_count)
                || wasm_reftype_is_subtype_of(
                    field_type, ref_type, REF_TYPE_ARRAYREF, NULL,
                    module->types, module->type_count)) {
                destroy_init_expr_data_recursive(
                    module, struct_init_values->fields[i].data);
            }
        }
    }
    else if (wasm_type->type_flag == WASM_TYPE_ARRAY) {
        WASMArrayType *array_type = (WASMArrayType *)wasm_type;
        WASMRefType *elem_ref_type = array_type->elem_ref_type;
        uint8 elem_type = array_type->elem_type;

        for (i = 0; i < array_init_values->length; i++) {
            if (wasm_reftype_is_subtype_of(elem_type, elem_ref_type,
                                           REF_TYPE_STRUCTREF, NULL,
                                           module->types, module->type_count)
                || wasm_reftype_is_subtype_of(
                    elem_type, elem_ref_type, REF_TYPE_ARRAYREF, NULL,
                    module->types, module->type_count)) {
                destroy_init_expr_data_recursive(
                    module, array_init_values->elem_data[i].data);
            }
        }
    }

    wasm_runtime_free(data);
}
#endif

static bool
pop_const_expr_stack(ConstExprContext *ctx, uint8 *p_flag, uint8 type,
#if WASM_ENABLE_GC != 0
                     WASMRefType *ref_type, uint8 *p_gc_opcode,
#endif
                     WASMValue *p_value, char *error_buf, uint32 error_buf_size)
{
    InitValue *cur_value;

    if (ctx->sp == 0) {
        set_error_buf(error_buf, error_buf_size,
                      "type mismatch: const expr stack underflow");
        return false;
    }

    cur_value = &ctx->stack[--ctx->sp];

#if WASM_ENABLE_GC == 0
    if (cur_value->type != type) {
        set_error_buf(error_buf, error_buf_size, "type mismatch");
        return false;
    }
#else
    if (!wasm_reftype_is_subtype_of(cur_value->type, &cur_value->ref_type, type,
                                    ref_type, ctx->module->types,
                                    ctx->module->type_count)) {
        set_error_buf_v(error_buf, error_buf_size, "%s%s%s",
                        "type mismatch: expect ", type2str(type),
                        " but got other");
        goto fail;
    }
#endif

    if (p_flag)
        *p_flag = cur_value->flag;
    if (p_value)
        *p_value = cur_value->value;
#if WASM_ENABLE_GC != 0
    if (p_gc_opcode)
        *p_gc_opcode = cur_value->gc_opcode;
#endif

    return true;

#if WASM_ENABLE_GC != 0
fail:
    if ((cur_value->flag == WASM_OP_GC_PREFIX)
        && (cur_value->gc_opcode == WASM_OP_STRUCT_NEW
            || cur_value->gc_opcode == WASM_OP_ARRAY_NEW
            || cur_value->gc_opcode == WASM_OP_ARRAY_NEW_FIXED)) {
        destroy_init_expr_data_recursive(ctx->module, cur_value->value.data);
    }
    return false;
#endif
}

static void
destroy_const_expr_stack(ConstExprContext *ctx)
{
#if WASM_ENABLE_GC != 0
    uint32 i;

    for (i = 0; i < ctx->sp; i++) {
        if ((ctx->stack[i].flag == WASM_OP_GC_PREFIX)
            && (ctx->stack[i].gc_opcode == WASM_OP_STRUCT_NEW
                || ctx->stack[i].gc_opcode == WASM_OP_ARRAY_NEW
                || ctx->stack[i].gc_opcode == WASM_OP_ARRAY_NEW_FIXED)) {
            destroy_init_expr_data_recursive(ctx->module,
                                             ctx->stack[i].value.data);
        }
    }
#endif

    if (ctx->stack != ctx->data) {
        wasm_runtime_free(ctx->stack);
    }
}

#if WASM_ENABLE_GC != 0
static void
destroy_init_expr(WASMModule *module, InitializerExpression *expr)
{
    if (expr->init_expr_type == INIT_EXPR_TYPE_STRUCT_NEW
        || expr->init_expr_type == INIT_EXPR_TYPE_ARRAY_NEW
        || expr->init_expr_type == INIT_EXPR_TYPE_ARRAY_NEW_FIXED) {
        destroy_init_expr_data_recursive(module, expr->u.data);
    }
}
#endif /* end of WASM_ENABLE_GC != 0 */

static bool
load_init_expr(WASMModule *module, const uint8 **p_buf, const uint8 *buf_end,
               InitializerExpression *init_expr, uint8 type, void *ref_type,
               char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 flag, *p_float;
    uint32 i;
    ConstExprContext const_expr_ctx = { 0 };
    WASMValue cur_value;
#if WASM_ENABLE_GC != 0
    uint32 opcode1, type_idx;
    uint8 opcode;
    WASMRefType cur_ref_type = { 0 };
#endif

    init_const_expr_stack(&const_expr_ctx, module);

    CHECK_BUF(p, p_end, 1);
    flag = read_uint8(p);

    while (flag != WASM_OP_END) {
        switch (flag) {
            /* i32.const */
            case INIT_EXPR_TYPE_I32_CONST:
                read_leb_int32(p, p_end, cur_value.i32);

                if (!push_const_expr_stack(
                        &const_expr_ctx, flag, VALUE_TYPE_I32,
#if WASM_ENABLE_GC != 0
                        NULL, 0,
#endif
                        &cur_value, error_buf, error_buf_size))
                    goto fail;
                break;
            /* i64.const */
            case INIT_EXPR_TYPE_I64_CONST:
                read_leb_int64(p, p_end, cur_value.i64);

                if (!push_const_expr_stack(
                        &const_expr_ctx, flag, VALUE_TYPE_I64,
#if WASM_ENABLE_GC != 0
                        NULL, 0,
#endif
                        &cur_value, error_buf, error_buf_size))
                    goto fail;
                break;
            /* f32.const */
            case INIT_EXPR_TYPE_F32_CONST:
                CHECK_BUF(p, p_end, 4);
                p_float = (uint8 *)&cur_value.f32;
                for (i = 0; i < sizeof(float32); i++)
                    *p_float++ = *p++;

                if (!push_const_expr_stack(
                        &const_expr_ctx, flag, VALUE_TYPE_F32,
#if WASM_ENABLE_GC != 0
                        NULL, 0,
#endif
                        &cur_value, error_buf, error_buf_size))
                    goto fail;
                break;
            /* f64.const */
            case INIT_EXPR_TYPE_F64_CONST:
                CHECK_BUF(p, p_end, 8);
                p_float = (uint8 *)&cur_value.f64;
                for (i = 0; i < sizeof(float64); i++)
                    *p_float++ = *p++;

                if (!push_const_expr_stack(
                        &const_expr_ctx, flag, VALUE_TYPE_F64,
#if WASM_ENABLE_GC != 0
                        NULL, 0,
#endif
                        &cur_value, error_buf, error_buf_size))
                    goto fail;
                break;
#if WASM_ENABLE_SIMD != 0
#if (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0)
            /* v128.const */
            case INIT_EXPR_TYPE_V128_CONST:
            {
                uint64 high, low;

                CHECK_BUF(p, p_end, 1);
                (void)read_uint8(p);

                CHECK_BUF(p, p_end, 16);
                wasm_runtime_read_v128(p, &high, &low);
                p += 16;

                cur_value.v128.i64x2[0] = high;
                cur_value.v128.i64x2[1] = low;

                if (!push_const_expr_stack(
                        &const_expr_ctx, flag, VALUE_TYPE_V128,
#if WASM_ENABLE_GC != 0
                        NULL, 0,
#endif
                        &cur_value, error_buf, error_buf_size))
                    goto fail;
#if WASM_ENABLE_WAMR_COMPILER != 0
                /* If any init_expr is v128.const, mark SIMD used */
                module->is_simd_used = true;
#endif
                break;
            }
#endif /* end of (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0) */
#endif /* end of WASM_ENABLE_SIMD */

#if WASM_ENABLE_REF_TYPES != 0 || WASM_ENABLE_GC != 0
            /* ref.func */
            case INIT_EXPR_TYPE_FUNCREF_CONST:
            {
                uint32 func_idx;
                read_leb_uint32(p, p_end, func_idx);
                cur_value.ref_index = func_idx;
                if (!check_function_index(module, func_idx, error_buf,
                                          error_buf_size)) {
                    goto fail;
                }

#if WASM_ENABLE_GC == 0
                if (!push_const_expr_stack(&const_expr_ctx, flag,
                                           VALUE_TYPE_FUNCREF, &cur_value,
                                           error_buf, error_buf_size))
                    goto fail;
#else
                if (func_idx < module->import_function_count) {
                    type_idx =
                        module->import_functions[func_idx].u.function.type_idx;
                }
                else {
                    type_idx = module
                                   ->functions[func_idx
                                               - module->import_function_count]
                                   ->type_idx;
                }
                wasm_set_refheaptype_typeidx(&cur_ref_type.ref_ht_typeidx,
                                             false, type_idx);
                if (!push_const_expr_stack(&const_expr_ctx, flag,
                                           cur_ref_type.ref_type, &cur_ref_type,
                                           0, &cur_value, error_buf,
                                           error_buf_size))
                    goto fail;
#endif
#if WASM_ENABLE_WAMR_COMPILER != 0
                module->is_ref_types_used = true;
#endif
                break;
            }

            /* ref.null */
            case INIT_EXPR_TYPE_REFNULL_CONST:
            {
                uint8 type1;

                CHECK_BUF(p, p_end, 1);
                type1 = read_uint8(p);

#if WASM_ENABLE_GC == 0
                cur_value.ref_index = NULL_REF;
                if (!push_const_expr_stack(&const_expr_ctx, flag, type1,
                                           &cur_value, error_buf,
                                           error_buf_size))
                    goto fail;
#else
                cur_value.gc_obj = NULL_REF;

                if (!is_byte_a_type(type1)) {
                    p--;
                    read_leb_uint32(p, p_end, type_idx);
                    if (!check_type_index(module, module->type_count, type_idx,
                                          error_buf, error_buf_size))
                        goto fail;

                    wasm_set_refheaptype_typeidx(&cur_ref_type.ref_ht_typeidx,
                                                 true, type_idx);
                    if (!push_const_expr_stack(&const_expr_ctx, flag,
                                               cur_ref_type.ref_type,
                                               &cur_ref_type, 0, &cur_value,
                                               error_buf, error_buf_size))
                        goto fail;
                }
                else {
                    if (!push_const_expr_stack(&const_expr_ctx, flag, type1,
                                               NULL, 0, &cur_value, error_buf,
                                               error_buf_size))
                        goto fail;
                }
#endif
#if WASM_ENABLE_WAMR_COMPILER != 0
                module->is_ref_types_used = true;
#endif
                break;
            }
#endif /* end of WASM_ENABLE_REF_TYPES != 0 || WASM_ENABLE_GC != 0 */

            /* get_global */
            case INIT_EXPR_TYPE_GET_GLOBAL:
            {
                uint32 global_idx;
                uint8 global_type;

                read_leb_uint32(p, p_end, cur_value.global_index);
                global_idx = cur_value.global_index;

                /*
                 * Currently, constant expressions occurring as initializers
                 * of globals are further constrained in that contained
                 * global.get instructions are
                 * only allowed to refer to imported globals.
                 *
                 * https://webassembly.github.io/spec/core/valid/instructions.html#constant-expressions
                 */
                if (global_idx >= module->import_global_count
                /* make spec test happy */
#if WASM_ENABLE_GC != 0
                                      + module->global_count
#endif
                ) {
                    set_error_buf_v(error_buf, error_buf_size,
                                    "unknown global %u", global_idx);
                    goto fail;
                }
                if (
                /* make spec test happy */
#if WASM_ENABLE_GC != 0
                    global_idx < module->import_global_count &&
#endif
                    module->import_globals[global_idx]
                        .u.global.type.is_mutable) {
                    set_error_buf_v(error_buf, error_buf_size,
                                    "constant expression required");
                    goto fail;
                }

                if (global_idx < module->import_global_count) {
                    global_type = module->import_globals[global_idx]
                                      .u.global.type.val_type;
#if WASM_ENABLE_GC != 0
                    if (wasm_is_type_multi_byte_type(global_type)) {
                        WASMRefType *global_ref_type =
                            module->import_globals[global_idx]
                                .u.global.ref_type;
                        bh_memcpy_s(&cur_ref_type,
                                    wasm_reftype_struct_size(global_ref_type),
                                    global_ref_type,
                                    wasm_reftype_struct_size(global_ref_type));
                    }
#endif
                }
                else {
                    global_type =
                        module
                            ->globals[global_idx - module->import_global_count]
                            .type.val_type;
#if WASM_ENABLE_GC != 0
                    if (wasm_is_type_multi_byte_type(global_type)) {
                        WASMRefType *global_ref_type =
                            module
                                ->globals[global_idx
                                          - module->import_global_count]
                                .ref_type;
                        bh_memcpy_s(&cur_ref_type,
                                    wasm_reftype_struct_size(global_ref_type),
                                    global_ref_type,
                                    wasm_reftype_struct_size(global_ref_type));
                    }
#endif
                }

                if (!push_const_expr_stack(&const_expr_ctx, flag, global_type,
#if WASM_ENABLE_GC != 0
                                           &cur_ref_type, 0,
#endif
                                           &cur_value, error_buf,
                                           error_buf_size))
                    goto fail;

                break;
            }

#if WASM_ENABLE_GC != 0
            /* struct.new and array.new */
            case WASM_OP_GC_PREFIX:
            {
                read_leb_uint32(p, p_end, opcode1);

                switch (opcode1) {
                    case WASM_OP_STRUCT_NEW:
                    {
                        WASMStructType *struct_type;
                        WASMStructNewInitValues *struct_init_values = NULL;
                        uint32 field_count;
                        read_leb_uint32(p, p_end, type_idx);

                        if (!check_type_index(module, module->type_count,
                                              type_idx, error_buf,
                                              error_buf_size)) {
                            goto fail;
                        }

                        struct_type = (WASMStructType *)module->types[type_idx];
                        if (struct_type->base_type.type_flag
                            != WASM_TYPE_STRUCT) {
                            set_error_buf(error_buf, error_buf_size,
                                          "unkown struct type");
                            goto fail;
                        }
                        field_count = struct_type->field_count;

                        if (!(struct_init_values = loader_malloc(
                                  offsetof(WASMStructNewInitValues, fields)
                                      + (uint64)field_count * sizeof(WASMValue),
                                  error_buf, error_buf_size))) {
                            goto fail;
                        }
                        struct_init_values->type_idx = type_idx;
                        struct_init_values->count = field_count;

                        for (i = field_count; i > 0; i--) {
                            WASMRefType *field_ref_type = NULL;
                            uint32 field_idx = i - 1;
                            uint8 field_type =
                                struct_type->fields[field_idx].field_type;
                            if (wasm_is_type_multi_byte_type(field_type)) {
                                field_ref_type = wasm_reftype_map_find(
                                    struct_type->ref_type_maps,
                                    struct_type->ref_type_map_count, field_idx);
                            }

                            if (is_packed_type(field_type)) {
                                field_type = VALUE_TYPE_I32;
                            }

                            if (!pop_const_expr_stack(
                                    &const_expr_ctx, NULL, field_type,
                                    field_ref_type, NULL,
                                    &struct_init_values->fields[field_idx],
                                    error_buf, error_buf_size)) {
                                destroy_init_expr_data_recursive(
                                    module, struct_init_values);
                                goto fail;
                            }
                        }

                        cur_value.data = struct_init_values;
                        wasm_set_refheaptype_typeidx(
                            &cur_ref_type.ref_ht_typeidx, false, type_idx);
                        if (!push_const_expr_stack(
                                &const_expr_ctx, flag, cur_ref_type.ref_type,
                                &cur_ref_type, (uint8)opcode1, &cur_value,
                                error_buf, error_buf_size)) {
                            destroy_init_expr_data_recursive(
                                module, struct_init_values);
                            goto fail;
                        }
                        break;
                    }
                    case WASM_OP_STRUCT_NEW_DEFAULT:
                    {
                        read_leb_uint32(p, p_end, cur_value.type_index);
                        type_idx = cur_value.type_index;

                        if (!check_type_index(module, module->type_count,
                                              type_idx, error_buf,
                                              error_buf_size)) {
                            goto fail;
                        }
                        if (module->types[type_idx]->type_flag
                            != WASM_TYPE_STRUCT) {
                            set_error_buf(error_buf, error_buf_size,
                                          "unkown struct type");
                            goto fail;
                        }

                        cur_value.type_index = type_idx;
                        cur_value.data = NULL;
                        wasm_set_refheaptype_typeidx(
                            &cur_ref_type.ref_ht_typeidx, false, type_idx);
                        if (!push_const_expr_stack(
                                &const_expr_ctx, flag, cur_ref_type.ref_type,
                                &cur_ref_type, (uint8)opcode1, &cur_value,
                                error_buf, error_buf_size)) {
                            goto fail;
                        }
                        break;
                    }
                    case WASM_OP_ARRAY_NEW:
                    case WASM_OP_ARRAY_NEW_DEFAULT:
                    case WASM_OP_ARRAY_NEW_FIXED:
                    {
                        WASMArrayNewInitValues *array_init_values = NULL;
                        WASMArrayType *array_type = NULL;
                        WASMRefType *elem_ref_type = NULL;
                        uint64 total_size;
                        uint8 elem_type;

                        read_leb_uint32(p, p_end, cur_value.type_index);
                        type_idx = cur_value.type_index;

                        if (!check_type_index(module, module->type_count,
                                              type_idx, error_buf,
                                              error_buf_size)) {
                            goto fail;
                        }

                        array_type = (WASMArrayType *)module->types[type_idx];
                        if (array_type->base_type.type_flag
                            != WASM_TYPE_ARRAY) {
                            set_error_buf(error_buf, error_buf_size,
                                          "unkown array type");
                            goto fail;
                        }

                        if (opcode1 != WASM_OP_ARRAY_NEW_DEFAULT) {
                            elem_type = array_type->elem_type;
                            if (wasm_is_type_multi_byte_type(elem_type)) {
                                elem_ref_type = array_type->elem_ref_type;
                            }

                            if (is_packed_type(elem_type)) {
                                elem_type = VALUE_TYPE_I32;
                            }

                            if (opcode1 == WASM_OP_ARRAY_NEW) {
                                WASMValue len_val;

                                if (!(array_init_values = loader_malloc(
                                          sizeof(WASMArrayNewInitValues),
                                          error_buf, error_buf_size))) {
                                    goto fail;
                                }
                                array_init_values->type_idx = type_idx;

                                if (!pop_const_expr_stack(
                                        &const_expr_ctx, NULL, VALUE_TYPE_I32,
                                        NULL, NULL, &len_val, error_buf,
                                        error_buf_size)) {
                                    destroy_init_expr_data_recursive(
                                        module, array_init_values);
                                    goto fail;
                                }
                                array_init_values->length = len_val.i32;

                                if (!pop_const_expr_stack(
                                        &const_expr_ctx, NULL, elem_type,
                                        elem_ref_type, NULL,
                                        &array_init_values->elem_data[0],
                                        error_buf, error_buf_size)) {
                                    destroy_init_expr_data_recursive(
                                        module, array_init_values);
                                    goto fail;
                                }

                                cur_value.data = array_init_values;
                            }
                            else {
                                /* WASM_OP_ARRAY_NEW_FIXED */
                                uint32 len;
                                read_leb_uint32(p, p_end, len);

                                total_size =
                                    (uint64)offsetof(WASMArrayNewInitValues,
                                                     elem_data)
                                    + (uint64)sizeof(WASMValue) * len;
                                if (!(array_init_values =
                                          loader_malloc(total_size, error_buf,
                                                        error_buf_size))) {
                                    goto fail;
                                }

                                array_init_values->type_idx = type_idx;
                                array_init_values->length = len;

                                for (i = len; i > 0; i--) {
                                    if (!pop_const_expr_stack(
                                            &const_expr_ctx, NULL, elem_type,
                                            elem_ref_type, NULL,
                                            &array_init_values
                                                 ->elem_data[i - 1],
                                            error_buf, error_buf_size)) {
                                        destroy_init_expr_data_recursive(
                                            module, array_init_values);
                                        goto fail;
                                    }
                                }

                                cur_value.data = array_init_values;
                            }
                        }
                        else {
                            /* WASM_OP_ARRAY_NEW_DEFAULT */
                            WASMValue len_val;
                            uint32 len;

                            /* POP(i32) */
                            if (!pop_const_expr_stack(&const_expr_ctx, NULL,
                                                      VALUE_TYPE_I32, NULL,
                                                      NULL, &len_val, error_buf,
                                                      error_buf_size)) {
                                goto fail;
                            }
                            len = len_val.i32;

                            cur_value.array_new_default.type_index = type_idx;
                            cur_value.array_new_default.length = len;
                        }

                        wasm_set_refheaptype_typeidx(
                            &cur_ref_type.ref_ht_typeidx, false, type_idx);
                        if (!push_const_expr_stack(
                                &const_expr_ctx, flag, cur_ref_type.ref_type,
                                &cur_ref_type, (uint8)opcode1, &cur_value,
                                error_buf, error_buf_size)) {
                            if (array_init_values) {
                                destroy_init_expr_data_recursive(
                                    module, array_init_values);
                            }
                            goto fail;
                        }
                        break;
                    }
                    case WASM_OP_ANY_CONVERT_EXTERN:
                    {
                        set_error_buf(error_buf, error_buf_size,
                                      "unsupported constant expression of "
                                      "extern.internalize");
                        goto fail;
                    }
                    case WASM_OP_EXTERN_CONVERT_ANY:
                    {
                        set_error_buf(error_buf, error_buf_size,
                                      "unsupported constant expression of "
                                      "extern.externalize");
                        goto fail;
                    }
                    case WASM_OP_REF_I31:
                    {
                        /* POP(i32) */
                        if (!pop_const_expr_stack(
                                &const_expr_ctx, NULL, VALUE_TYPE_I32, NULL,
                                NULL, &cur_value, error_buf, error_buf_size)) {
                            goto fail;
                        }

                        wasm_set_refheaptype_common(&cur_ref_type.ref_ht_common,
                                                    false, HEAP_TYPE_I31);
                        if (!push_const_expr_stack(
                                &const_expr_ctx, flag, cur_ref_type.ref_type,
                                &cur_ref_type, (uint8)opcode1, &cur_value,
                                error_buf, error_buf_size)) {
                            goto fail;
                        }
                        break;
                    }
                    default:
                        set_error_buf(
                            error_buf, error_buf_size,
                            "type mismatch or constant expression required");
                        goto fail;
                }

                break;
            }
#endif /* end of WASM_ENABLE_GC != 0 */
            default:
            {
                set_error_buf(error_buf, error_buf_size,
                              "illegal opcode "
                              "or constant expression required "
                              "or type mismatch");
                goto fail;
            }
        }

        CHECK_BUF(p, p_end, 1);
        flag = read_uint8(p);
    }

    /* There should be only one value left on the init value stack */
    if (!pop_const_expr_stack(&const_expr_ctx, &flag, type,
#if WASM_ENABLE_GC != 0
                              ref_type, &opcode,
#endif
                              &cur_value, error_buf, error_buf_size)) {
        goto fail;
    }

    if (const_expr_ctx.sp != 0) {
        set_error_buf(error_buf, error_buf_size,
                      "type mismatch: illegal constant opcode sequence");
        goto fail;
    }

    init_expr->init_expr_type = flag;
    init_expr->u = cur_value;

#if WASM_ENABLE_GC != 0
    if (init_expr->init_expr_type == WASM_OP_GC_PREFIX) {
        switch (opcode) {
            case WASM_OP_STRUCT_NEW:
                init_expr->init_expr_type = INIT_EXPR_TYPE_STRUCT_NEW;
                break;
            case WASM_OP_STRUCT_NEW_DEFAULT:
                init_expr->init_expr_type = INIT_EXPR_TYPE_STRUCT_NEW_DEFAULT;
                break;
            case WASM_OP_ARRAY_NEW:
                init_expr->init_expr_type = INIT_EXPR_TYPE_ARRAY_NEW;
                break;
            case WASM_OP_ARRAY_NEW_DEFAULT:
                init_expr->init_expr_type = INIT_EXPR_TYPE_ARRAY_NEW_DEFAULT;
                break;
            case WASM_OP_ARRAY_NEW_FIXED:
                init_expr->init_expr_type = INIT_EXPR_TYPE_ARRAY_NEW_FIXED;
                break;
            case WASM_OP_REF_I31:
                init_expr->init_expr_type = INIT_EXPR_TYPE_I31_NEW;
                break;
            default:
                bh_assert(0);
                break;
        }
    }
#endif /* end of WASM_ENABLE_GC != 0 */

    *p_buf = p;
    destroy_const_expr_stack(&const_expr_ctx);
    return true;

fail:
    destroy_const_expr_stack(&const_expr_ctx);
    return false;
}

static bool
check_mutability(uint8 mutable, char *error_buf, uint32 error_buf_size)
{
    if (mutable >= 2) {
        set_error_buf(error_buf, error_buf_size, "invalid mutability");
        return false;
    }
    return true;
}

#if WASM_ENABLE_GC != 0
static void
destroy_func_type(WASMFuncType *type)
{
    /* Destroy the reference type hash set */
    if (type->ref_type_maps)
        wasm_runtime_free(type->ref_type_maps);

#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT != 0 \
    && WASM_ENABLE_LAZY_JIT != 0
    if (type->call_to_llvm_jit_from_fast_jit)
        jit_code_cache_free(type->call_to_llvm_jit_from_fast_jit);
#endif
    /* Free the type */
    wasm_runtime_free(type);
}

static void
destroy_struct_type(WASMStructType *type)
{
    if (type->ref_type_maps)
        wasm_runtime_free(type->ref_type_maps);

    wasm_runtime_free(type);
}

static void
destroy_array_type(WASMArrayType *type)
{
    wasm_runtime_free(type);
}

static void
destroy_wasm_type(WASMType *type)
{
    if (type->ref_count > 1) {
        /* The type is referenced by other types
           of current wasm module */
        type->ref_count--;
        return;
    }

    if (type->type_flag == WASM_TYPE_FUNC)
        destroy_func_type((WASMFuncType *)type);
    else if (type->type_flag == WASM_TYPE_STRUCT)
        destroy_struct_type((WASMStructType *)type);
    else if (type->type_flag == WASM_TYPE_ARRAY)
        destroy_array_type((WASMArrayType *)type);
    else {
        bh_assert(0);
    }
}

/* Resolve (ref null ht) or (ref ht) */
static bool
resolve_reftype_htref(const uint8 **p_buf, const uint8 *buf_end,
                      WASMModule *module, uint32 type_count, bool nullable,
                      WASMRefType *ref_type, char *error_buf,
                      uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;

    ref_type->ref_type =
        nullable ? REF_TYPE_HT_NULLABLE : REF_TYPE_HT_NON_NULLABLE;
    ref_type->ref_ht_common.nullable = nullable;
    read_leb_int32(p, p_end, ref_type->ref_ht_common.heap_type);

    if (wasm_is_refheaptype_typeidx(&ref_type->ref_ht_common)) {
        /* heap type is (type i), i : typeidx, >= 0 */
        if (!check_type_index(module, type_count,
                              ref_type->ref_ht_typeidx.type_idx, error_buf,
                              error_buf_size)) {
            return false;
        }
    }
    else if (!wasm_is_refheaptype_common(&ref_type->ref_ht_common)) {
        /* heap type is func, extern, any, eq, i31 or data */
        set_error_buf(error_buf, error_buf_size, "unknown heap type");
        return false;
    }

    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
resolve_value_type(const uint8 **p_buf, const uint8 *buf_end,
                   WASMModule *module, uint32 type_count,
                   bool *p_need_ref_type_map, WASMRefType *ref_type,
                   bool allow_packed_type, char *error_buf,
                   uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 type;

    memset(ref_type, 0, sizeof(WASMRefType));

    CHECK_BUF(p, p_end, 1);
    type = read_uint8(p);

    if (wasm_is_reftype_htref_nullable(type)) {
        /* (ref null ht) */
        if (!resolve_reftype_htref(&p, p_end, module, type_count, true,
                                   ref_type, error_buf, error_buf_size))
            return false;
        if (!wasm_is_refheaptype_common(&ref_type->ref_ht_common))
            *p_need_ref_type_map = true;
        else {
            /* For (ref null func/extern/any/eq/i31/data), they are same as
               funcref/externref/anyref/eqref/i31ref/dataref, we convert the
               multi-byte type to one-byte type to reduce the footprint and
               the complexity of type equal/subtype checking */
            ref_type->ref_type =
                (uint8)((int32)0x80 + ref_type->ref_ht_common.heap_type);
            *p_need_ref_type_map = false;
        }
    }
    else if (wasm_is_reftype_htref_non_nullable(type)) {
        /* (ref ht) */
        if (!resolve_reftype_htref(&p, p_end, module, type_count, false,
                                   ref_type, error_buf, error_buf_size))
            return false;
        *p_need_ref_type_map = true;
#if WASM_ENABLE_STRINGREF != 0
        /* covert (ref string) to stringref */
        if (wasm_is_refheaptype_stringrefs(&ref_type->ref_ht_common)) {
            ref_type->ref_type =
                (uint8)((int32)0x80 + ref_type->ref_ht_common.heap_type);
            *p_need_ref_type_map = false;
        }
#endif
    }
    else {
        /* type which can be represented by one byte */
        if (!is_valid_value_type_for_interpreter(type)
            && !(allow_packed_type && is_packed_type(type))) {
            set_error_buf(error_buf, error_buf_size, "type mismatch");
            return false;
        }
        ref_type->ref_type = type;
        *p_need_ref_type_map = false;
#if WASM_ENABLE_WAMR_COMPILER != 0
        /* If any value's type is v128, mark the module as SIMD used */
        if (type == VALUE_TYPE_V128)
            module->is_simd_used = true;
#endif
    }

    *p_buf = p;
    return true;
fail:
    return false;
}

static WASMRefType *
reftype_set_insert(HashMap *ref_type_set, const WASMRefType *ref_type,
                   char *error_buf, uint32 error_buf_size)
{
    WASMRefType *ret = wasm_reftype_set_insert(ref_type_set, ref_type);

    if (!ret) {
        set_error_buf(error_buf, error_buf_size,
                      "insert ref type to hash set failed");
    }
    return ret;
}

static bool
resolve_func_type(const uint8 **p_buf, const uint8 *buf_end, WASMModule *module,
                  uint32 type_count, uint32 type_idx, char *error_buf,
                  uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
    uint32 param_count, result_count, i, j = 0;
    uint32 param_cell_num, ret_cell_num;
    uint32 ref_type_map_count = 0, result_ref_type_map_count = 0;
    uint64 total_size;
    bool need_ref_type_map;
    WASMRefType ref_type;
    WASMFuncType *type = NULL;

    /* Parse first time to resolve param count, result count and
       ref type map count */
    read_leb_uint32(p, p_end, param_count);
    p_org = p;
    for (i = 0; i < param_count; i++) {
        if (!resolve_value_type(&p, p_end, module, type_count,
                                &need_ref_type_map, &ref_type, false, error_buf,
                                error_buf_size)) {
            return false;
        }
        if (need_ref_type_map)
            ref_type_map_count++;
    }

    read_leb_uint32(p, p_end, result_count);
    for (i = 0; i < result_count; i++) {
        if (!resolve_value_type(&p, p_end, module, type_count,
                                &need_ref_type_map, &ref_type, false, error_buf,
                                error_buf_size)) {
            return false;
        }
        if (need_ref_type_map) {
            ref_type_map_count++;
            result_ref_type_map_count++;
        }
    }

    LOG_VERBOSE("type %u: func, param count: %d, result count: %d, "
                "ref type map count: %d",
                type_idx, param_count, result_count, ref_type_map_count);

    /* Parse second time to resolve param types, result types and
       ref type map info */
    p = p_org;

    total_size = offsetof(WASMFuncType, types)
                 + sizeof(uint8) * (uint64)(param_count + result_count);
    if (!(type = loader_malloc(total_size, error_buf, error_buf_size))) {
        return false;
    }
    if (ref_type_map_count > 0) {
        total_size = sizeof(WASMRefTypeMap) * (uint64)ref_type_map_count;
        if (!(type->ref_type_maps =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            goto fail;
        }
    }

    type->base_type.type_flag = WASM_TYPE_FUNC;
    type->param_count = param_count;
    type->result_count = result_count;
    type->ref_type_map_count = ref_type_map_count;
    if (ref_type_map_count > 0) {
        type->result_ref_type_maps = type->ref_type_maps + ref_type_map_count
                                     - result_ref_type_map_count;
    }

    for (i = 0; i < param_count; i++) {
        if (!resolve_value_type(&p, p_end, module, type_count,
                                &need_ref_type_map, &ref_type, false, error_buf,
                                error_buf_size)) {
            goto fail;
        }
        type->types[i] = ref_type.ref_type;
        if (need_ref_type_map) {
            type->ref_type_maps[j].index = i;
            if (!(type->ref_type_maps[j++].ref_type =
                      reftype_set_insert(module->ref_type_set, &ref_type,
                                         error_buf, error_buf_size))) {
                goto fail;
            }
        }
    }

    read_leb_uint32(p, p_end, result_count);
    for (i = 0; i < result_count; i++) {
        if (!resolve_value_type(&p, p_end, module, type_count,
                                &need_ref_type_map, &ref_type, false, error_buf,
                                error_buf_size)) {
            goto fail;
        }
        type->types[param_count + i] = ref_type.ref_type;
        if (need_ref_type_map) {
            type->ref_type_maps[j].index = param_count + i;
            if (!(type->ref_type_maps[j++].ref_type =
                      reftype_set_insert(module->ref_type_set, &ref_type,
                                         error_buf, error_buf_size))) {
                goto fail;
            }
        }
    }

    bh_assert(j == type->ref_type_map_count);
#if TRACE_WASM_LOADER != 0
    os_printf("type %d = ", type_idx);
    wasm_dump_func_type(type);
#endif

    param_cell_num = wasm_get_cell_num(type->types, param_count);
    ret_cell_num = wasm_get_cell_num(type->types + param_count, result_count);
    if (param_cell_num > UINT16_MAX || ret_cell_num > UINT16_MAX) {
        set_error_buf(error_buf, error_buf_size,
                      "param count or result count too large");
        goto fail;
    }
    type->param_cell_num = (uint16)param_cell_num;
    type->ret_cell_num = (uint16)ret_cell_num;

#if WASM_ENABLE_QUICK_AOT_ENTRY != 0
    type->quick_aot_entry = wasm_native_lookup_quick_aot_entry(type);
#endif

#if WASM_ENABLE_WAMR_COMPILER != 0
    for (i = 0; i < (uint32)(type->param_count + type->result_count); i++) {
        if (type->types[i] == VALUE_TYPE_V128)
            module->is_simd_used = true;
    }
#endif

    *p_buf = p;

    module->types[type_idx] = (WASMType *)type;
    return true;

fail:
    if (type)
        destroy_func_type(type);
    return false;
}

static bool
resolve_struct_type(const uint8 **p_buf, const uint8 *buf_end,
                    WASMModule *module, uint32 type_count, uint32 type_idx,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
    uint32 field_count, ref_type_map_count = 0, ref_field_count = 0;
    uint32 i, j = 0, offset;
    uint16 *reference_table;
    uint64 total_size;
    uint8 mutable;
    bool need_ref_type_map;
    WASMRefType ref_type;
    WASMStructType *type = NULL;

    /* Parse first time to resolve field count and ref type map count */
    read_leb_uint32(p, p_end, field_count);
    p_org = p;
    for (i = 0; i < field_count; i++) {
        if (!resolve_value_type(&p, p_end, module, type_count,
                                &need_ref_type_map, &ref_type, true, error_buf,
                                error_buf_size)) {
            return false;
        }
        if (need_ref_type_map)
            ref_type_map_count++;

        if (wasm_is_type_reftype(ref_type.ref_type))
            ref_field_count++;

        CHECK_BUF(p, p_end, 1);
        mutable = read_uint8(p);
        if (!check_mutability(mutable, error_buf, error_buf_size)) {
            return false;
        }
    }

    LOG_VERBOSE("type %u: struct, field count: %d, ref type map count: %d",
                type_idx, field_count, ref_type_map_count);

    /* Parse second time to resolve field types and ref type map info */
    p = p_org;

    total_size = offsetof(WASMStructType, fields)
                 + sizeof(WASMStructFieldType) * (uint64)field_count
                 + sizeof(uint16) * (uint64)(ref_field_count + 1);
    if (!(type = loader_malloc(total_size, error_buf, error_buf_size))) {
        return false;
    }
    if (ref_type_map_count > 0) {
        total_size = sizeof(WASMRefTypeMap) * (uint64)ref_type_map_count;
        if (!(type->ref_type_maps =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            goto fail;
        }
    }

    type->reference_table = reference_table =
        (uint16 *)((uint8 *)type + offsetof(WASMStructType, fields)
                   + sizeof(WASMStructFieldType) * field_count);
    *reference_table++ = ref_field_count;

    type->base_type.type_flag = WASM_TYPE_STRUCT;
    type->field_count = field_count;
    type->ref_type_map_count = ref_type_map_count;

    offset = (uint32)offsetof(WASMStructObject, field_data);
    for (i = 0; i < field_count; i++) {
        if (!resolve_value_type(&p, p_end, module, type_count,
                                &need_ref_type_map, &ref_type, true, error_buf,
                                error_buf_size)) {
            goto fail;
        }
        type->fields[i].field_type = ref_type.ref_type;
        if (need_ref_type_map) {
            type->ref_type_maps[j].index = i;
            if (!(type->ref_type_maps[j++].ref_type =
                      reftype_set_insert(module->ref_type_set, &ref_type,
                                         error_buf, error_buf_size))) {
                goto fail;
            }
        }

        CHECK_BUF(p, p_end, 1);
        type->fields[i].field_flags = read_uint8(p);
        type->fields[i].field_size =
            (uint8)wasm_reftype_size(ref_type.ref_type);
#if !(defined(BUILD_TARGET_X86_64) || defined(BUILD_TARGET_AMD_64) \
      || defined(BUILD_TARGET_X86_32))
        if (type->fields[i].field_size == 2)
            offset = align_uint(offset, 2);
        else if (type->fields[i].field_size >= 4) /* field size is 4 or 8 */
            offset = align_uint(offset, 4);
#endif
        type->fields[i].field_offset = offset;
        if (wasm_is_type_reftype(ref_type.ref_type))
            *reference_table++ = offset;
        offset += type->fields[i].field_size;

        LOG_VERBOSE("                field: %d, flags: %d, type: %d", i,
                    type->fields[i].field_flags, type->fields[i].field_type);
    }
    type->total_size = offset;

    bh_assert(j == type->ref_type_map_count);
#if TRACE_WASM_LOADER != 0
    os_printf("type %d = ", type_idx);
    wasm_dump_struct_type(type);
#endif

    *p_buf = p;

    module->types[type_idx] = (WASMType *)type;
    return true;

fail:
    if (type)
        destroy_struct_type(type);
    return false;
}

static bool
resolve_array_type(const uint8 **p_buf, const uint8 *buf_end,
                   WASMModule *module, uint32 type_count, uint32 type_idx,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 mutable;
    bool need_ref_type_map;
    WASMRefType ref_type;
    WASMArrayType *type = NULL;

    if (!resolve_value_type(&p, p_end, module, type_count, &need_ref_type_map,
                            &ref_type, true, error_buf, error_buf_size)) {
        return false;
    }

    CHECK_BUF(p, p_end, 1);
    mutable = read_uint8(p);
    if (!check_mutability(mutable, error_buf, error_buf_size)) {
        return false;
    }

    LOG_VERBOSE("type %u: array", type_idx);

    if (!(type = loader_malloc(sizeof(WASMArrayType), error_buf,
                               error_buf_size))) {
        return false;
    }

    type->base_type.type_flag = WASM_TYPE_ARRAY;
    type->elem_flags = mutable;
    type->elem_type = ref_type.ref_type;
    if (need_ref_type_map) {
        if (!(type->elem_ref_type =
                  reftype_set_insert(module->ref_type_set, &ref_type, error_buf,
                                     error_buf_size))) {
            goto fail;
        }
    }

#if TRACE_WASM_LOADER != 0
    os_printf("type %d = ", type_idx);
    wasm_dump_array_type(type);
#endif

    *p_buf = p;

    module->types[type_idx] = (WASMType *)type;
    return true;

fail:
    if (type)
        destroy_array_type(type);
    return false;
}

static bool
init_ref_type(WASMModule *module, WASMRefType *ref_type, bool nullable,
              int32 heap_type, char *error_buf, uint32 error_buf_size)
{
    if (heap_type >= 0) {
        if (!check_type_index(module, module->type_count, heap_type, error_buf,
                              error_buf_size)) {
            return false;
        }
        wasm_set_refheaptype_typeidx(&ref_type->ref_ht_typeidx, nullable,
                                     heap_type);
    }
    else {
        if (!wasm_is_valid_heap_type(heap_type)) {
            set_error_buf(error_buf, error_buf_size, "unknown type");
            return false;
        }
        wasm_set_refheaptype_common(&ref_type->ref_ht_common, nullable,
                                    heap_type);
        if (nullable) {
            /* For (ref null func/extern/any/eq/i31/data),
               they are same as
                funcref/externref/anyref/eqref/i31ref/dataref,
               we convert the multi-byte type to one-byte
               type to reduce the footprint and the
               complexity of type equal/subtype checking */
            ref_type->ref_type =
                (uint8)((int32)0x80 + ref_type->ref_ht_common.heap_type);
        }
    }
    return true;
}

static void
calculate_reftype_diff(WASMRefType *ref_type_diff, WASMRefType *ref_type1,
                       WASMRefType *ref_type2)
{
    /**
     * The difference rt1 ∖ rt2 between two reference types is defined as
     * follows:
     *  (ref null?1 ht1) ∖ (ref null ht2) = (ref ht1) (ref null?1 ht1) ∖
     *  (ref ht2) = (ref null?1 ht1)
     */
    if (wasm_is_type_multi_byte_type(ref_type1->ref_type)) {
        bh_memcpy_s(ref_type_diff, wasm_reftype_struct_size(ref_type1),
                    ref_type1, wasm_reftype_struct_size(ref_type1));
    }
    else {
        ref_type_diff->ref_type = ref_type1->ref_type;
    }

    if (ref_type2->ref_ht_common.nullable) {
        if (wasm_is_type_reftype(ref_type_diff->ref_type)
            && !(wasm_is_type_multi_byte_type(ref_type_diff->ref_type))) {
            wasm_set_refheaptype_typeidx(&ref_type_diff->ref_ht_typeidx, false,
                                         (int32)ref_type_diff->ref_type - 0x80);
        }
        else {
            ref_type_diff->ref_ht_typeidx.nullable = false;
        }
    }
}
#else /* else of WASM_ENABLE_GC != 0 */
static void
destroy_wasm_type(WASMType *type)
{
    if (type->ref_count > 1) {
        /* The type is referenced by other types
           of current wasm module */
        type->ref_count--;
        return;
    }

#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT != 0 \
    && WASM_ENABLE_LAZY_JIT != 0
    if (type->call_to_llvm_jit_from_fast_jit)
        jit_code_cache_free(type->call_to_llvm_jit_from_fast_jit);
#endif

    wasm_runtime_free(type);
}
#endif /* end of WASM_ENABLE_GC != 0 */

static bool
load_type_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 type_count, i;
    uint64 total_size;
    uint8 flag;
#if WASM_ENABLE_GC != 0
    uint32 processed_type_count = 0;
#endif

    read_leb_uint32(p, p_end, type_count);

    if (type_count) {
        module->type_count = type_count;
        total_size = sizeof(WASMType *) * (uint64)type_count;
        if (!(module->types =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

#if WASM_ENABLE_GC == 0
        for (i = 0; i < type_count; i++) {
            WASMFuncType *type;
            const uint8 *p_org;
            uint32 param_count, result_count, j;
            uint32 param_cell_num, ret_cell_num;

            CHECK_BUF(p, p_end, 1);
            flag = read_uint8(p);
            if (flag != 0x60) {
                set_error_buf(error_buf, error_buf_size, "invalid type flag");
                return false;
            }

            read_leb_uint32(p, p_end, param_count);

            /* Resolve param count and result count firstly */
            p_org = p;
            CHECK_BUF(p, p_end, param_count);
            p += param_count;
            read_leb_uint32(p, p_end, result_count);
            CHECK_BUF(p, p_end, result_count);
            p = p_org;

            if (param_count > UINT16_MAX || result_count > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "param count or result count too large");
                return false;
            }

            total_size = offsetof(WASMFuncType, types)
                         + sizeof(uint8) * (uint64)(param_count + result_count);
            if (!(type = module->types[i] =
                      loader_malloc(total_size, error_buf, error_buf_size))) {
                return false;
            }

            /* Resolve param types and result types */
            type->ref_count = 1;
            type->param_count = (uint16)param_count;
            type->result_count = (uint16)result_count;
            for (j = 0; j < param_count; j++) {
                CHECK_BUF(p, p_end, 1);
                type->types[j] = read_uint8(p);
            }
            read_leb_uint32(p, p_end, result_count);
            for (j = 0; j < result_count; j++) {
                CHECK_BUF(p, p_end, 1);
                type->types[param_count + j] = read_uint8(p);
            }
            for (j = 0; j < param_count + result_count; j++) {
                if (!is_valid_value_type_for_interpreter(type->types[j])) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unknown value type");
                    return false;
                }
            }

            param_cell_num = wasm_get_cell_num(type->types, param_count);
            ret_cell_num =
                wasm_get_cell_num(type->types + param_count, result_count);
            if (param_cell_num > UINT16_MAX || ret_cell_num > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "param count or result count too large");
                return false;
            }
            type->param_cell_num = (uint16)param_cell_num;
            type->ret_cell_num = (uint16)ret_cell_num;

#if WASM_ENABLE_QUICK_AOT_ENTRY != 0
            type->quick_aot_entry = wasm_native_lookup_quick_aot_entry(type);
#endif

#if WASM_ENABLE_WAMR_COMPILER != 0
            for (j = 0; j < type->param_count + type->result_count; j++) {
                if (type->types[j] == VALUE_TYPE_V128)
                    module->is_simd_used = true;
                else if (type->types[j] == VALUE_TYPE_FUNCREF
                         || type->types[j] == VALUE_TYPE_EXTERNREF)
                    module->is_ref_types_used = true;
            }
#endif

            /* If there is already a same type created, use it instead */
            for (j = 0; j < i; j++) {
                if (wasm_type_equal(type, module->types[j], module->types, i)) {
                    if (module->types[j]->ref_count == UINT16_MAX) {
                        set_error_buf(error_buf, error_buf_size,
                                      "wasm type's ref count too large");
                        return false;
                    }
                    destroy_wasm_type(type);
                    module->types[i] = module->types[j];
                    module->types[j]->ref_count++;
                    break;
                }
            }
        }
#else  /* else of WASM_ENABLE_GC == 0 */
        for (i = 0; i < type_count; i++) {
            uint32 super_type_count = 0, parent_type_idx = (uint32)-1;
            uint32 rec_count = 1, j;
            bool is_sub_final = true;

            CHECK_BUF(p, p_end, 1);
            flag = read_uint8(p);

            if (flag == DEFINED_TYPE_REC) {
                read_leb_uint32(p, p_end, rec_count);

                if (rec_count > 1) {
                    uint64 new_total_size;

                    /* integer overflow */
                    if (rec_count - 1 > UINT32_MAX - module->type_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "recursive type count too large");
                        return false;
                    }
                    module->type_count += rec_count - 1;
                    new_total_size =
                        sizeof(WASMFuncType *) * (uint64)module->type_count;
                    if (new_total_size > UINT32_MAX) {
                        set_error_buf(error_buf, error_buf_size,
                                      "allocate memory failed");
                        return false;
                    }
                    MEM_REALLOC(module->types, (uint32)total_size,
                                (uint32)new_total_size);
                    total_size = new_total_size;
                }

                LOG_VERBOSE("Processing rec group [%d-%d]",
                            processed_type_count,
                            processed_type_count + rec_count - 1);
            }
            else {
                p--;
            }

            for (j = 0; j < rec_count; j++) {
                WASMType *cur_type = NULL;

                CHECK_BUF(p, p_end, 1);
                flag = read_uint8(p);

                parent_type_idx = -1;

                if (flag == DEFINED_TYPE_SUB
                    || flag == DEFINED_TYPE_SUB_FINAL) {
                    read_leb_uint32(p, p_end, super_type_count);
                    if (super_type_count > 1) {
                        set_error_buf(error_buf, error_buf_size,
                                      "super type count too large");
                        return false;
                    }

                    if (super_type_count > 0) {
                        read_leb_uint32(p, p_end, parent_type_idx);
                        if (parent_type_idx >= processed_type_count + j) {
                            set_error_buf_v(error_buf, error_buf_size,
                                            "unknown type %d", parent_type_idx);
                            return false;
                        }
                        if (module->types[parent_type_idx]->is_sub_final) {
                            set_error_buf(error_buf, error_buf_size,
                                          "sub type can not inherit from "
                                          "a final super type");
                            return false;
                        }
                    }

                    if (flag == DEFINED_TYPE_SUB)
                        is_sub_final = false;

                    CHECK_BUF(p, p_end, 1);
                    flag = read_uint8(p);
                }

                if (flag == DEFINED_TYPE_FUNC) {
                    if (!resolve_func_type(&p, buf_end, module,
                                           processed_type_count + rec_count,
                                           processed_type_count + j, error_buf,
                                           error_buf_size)) {
                        return false;
                    }
                }
                else if (flag == DEFINED_TYPE_STRUCT) {
                    if (!resolve_struct_type(&p, buf_end, module,
                                             processed_type_count + rec_count,
                                             processed_type_count + j,
                                             error_buf, error_buf_size)) {
                        return false;
                    }
                }
                else if (flag == DEFINED_TYPE_ARRAY) {
                    if (!resolve_array_type(&p, buf_end, module,
                                            processed_type_count + rec_count,
                                            processed_type_count + j, error_buf,
                                            error_buf_size)) {
                        return false;
                    }
                }
                else {
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid type flag");
                    return false;
                }

                cur_type = module->types[processed_type_count + j];

                cur_type->ref_count = 1;
                cur_type->parent_type_idx = parent_type_idx;
                cur_type->is_sub_final = is_sub_final;

                cur_type->rec_count = rec_count;
                cur_type->rec_idx = j;
                cur_type->rec_begin_type_idx = processed_type_count;
            }

            /* resolve subtyping relationship in current rec group */
            for (j = 0; j < rec_count; j++) {
                WASMType *cur_type = module->types[processed_type_count + j];

                if (cur_type->parent_type_idx != (uint32)-1) { /* has parent */
                    WASMType *parent_type =
                        module->types[cur_type->parent_type_idx];
                    cur_type->parent_type = parent_type;
                    cur_type->root_type = parent_type->root_type;
                    if (parent_type->inherit_depth == UINT16_MAX) {
                        set_error_buf(error_buf, error_buf_size,
                                      "parent type's inherit depth too large");
                        return false;
                    }
                    cur_type->inherit_depth = parent_type->inherit_depth + 1;
                }
                else {
                    cur_type->parent_type = NULL;
                    cur_type->root_type = cur_type;
                    cur_type->inherit_depth = 0;
                }
            }

            for (j = 0; j < rec_count; j++) {
                WASMType *cur_type = module->types[processed_type_count + j];

                if (cur_type->parent_type_idx != (uint32)-1) { /* has parent */
                    WASMType *parent_type =
                        module->types[cur_type->parent_type_idx];
                    if (!wasm_type_is_subtype_of(cur_type, parent_type,
                                                 module->types,
                                                 module->type_count)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "sub type does not match super type");
                        return false;
                    }
                }
            }

            /* If there is already an equivalence type or a group of equivalence
               recursive types created, use it or them instead */
            for (j = 0; j < processed_type_count;) {
                WASMType *src_type = module->types[j];
                WASMType *cur_type = module->types[processed_type_count];
                uint32 k, src_rec_count;

                src_rec_count = src_type->rec_count;
                if (src_rec_count != rec_count) {
                    /* no type equivalence */
                    j += src_rec_count;
                    continue;
                }

                for (k = 0; k < rec_count; k++) {
                    src_type = module->types[j + k];
                    cur_type = module->types[processed_type_count + k];
                    if (!wasm_type_equal(src_type, cur_type, module->types,
                                         module->type_count)) {
                        break;
                    }
                }
                if (k < rec_count) {
                    /* no type equivalence */
                    j += src_rec_count;
                    continue;
                }

                /* type equivalence */
                for (k = 0; k < rec_count; k++) {
                    if (module->types[j + k]->ref_count == UINT16_MAX) {
                        set_error_buf(error_buf, error_buf_size,
                                      "wasm type's ref count too large");
                        return false;
                    }
                    destroy_wasm_type(module->types[processed_type_count + k]);
                    module->types[processed_type_count + k] =
                        module->types[j + k];
                    module->types[j + k]->ref_count++;
                }
                break;
            }

            if (rec_count > 1) {
                LOG_VERBOSE("Finished processing rec group [%d-%d]",
                            processed_type_count,
                            processed_type_count + rec_count - 1);
            }

            processed_type_count += rec_count;
        }

        if (!(module->rtt_types = loader_malloc((uint64)sizeof(WASMRttType *)
                                                    * module->type_count,
                                                error_buf, error_buf_size))) {
            return false;
        }
#endif /* end of WASM_ENABLE_GC == 0 */
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load type section success.\n");
    return true;
fail:
    return false;
}

static void
adjust_table_max_size(bool is_table64, uint32 init_size, uint32 max_size_flag,
                      uint32 *max_size)
{
    uint32 default_max_size;

    /* TODO: current still use UINT32_MAX as upper limit for table size to keep
     * ABI unchanged */
    (void)is_table64;
    if (UINT32_MAX / 2 > init_size)
        default_max_size = init_size * 2;
    else
        default_max_size = UINT32_MAX;

    if (default_max_size < WASM_TABLE_MAX_SIZE)
        default_max_size = WASM_TABLE_MAX_SIZE;

    if (max_size_flag) {
        /* module defines the table limitation */
        bh_assert(init_size <= *max_size);

        if (init_size < *max_size) {
            *max_size =
                *max_size < default_max_size ? *max_size : default_max_size;
        }
    }
    else {
        /* partial defined table limitation, gives a default value */
        *max_size = default_max_size;
    }
}

#if WASM_ENABLE_LIBC_WASI != 0 || WASM_ENABLE_MULTI_MODULE != 0
/**
 * Find export item of a module with export info:
 *  module name, field name and export kind
 */
static WASMExport *
wasm_loader_find_export(const WASMModule *module, const char *module_name,
                        const char *field_name, uint8 export_kind,
                        char *error_buf, uint32 error_buf_size)
{
    WASMExport *export =
        loader_find_export((WASMModuleCommon *)module, module_name, field_name,
                           export_kind, error_buf, error_buf_size);
    return export;
}
#endif

#if WASM_ENABLE_MULTI_MODULE != 0
static WASMTable *
wasm_loader_resolve_table(const char *module_name, const char *table_name,
                          uint32 init_size, uint32 max_size, char *error_buf,
                          uint32 error_buf_size)
{
    WASMModuleCommon *module_reg;
    WASMTable *table = NULL;
    WASMExport *export = NULL;
    WASMModule *module = NULL;

    module_reg = wasm_runtime_find_module_registered(module_name);
    if (!module_reg || module_reg->module_type != Wasm_Module_Bytecode) {
        LOG_DEBUG("can not find a module named %s for table", module_name);
        set_error_buf(error_buf, error_buf_size, "unknown import");
        return NULL;
    }

    module = (WASMModule *)module_reg;
    export =
        wasm_loader_find_export(module, module_name, table_name,
                                EXPORT_KIND_TABLE, error_buf, error_buf_size);
    if (!export) {
        return NULL;
    }

    /* resolve table and check the init/max size */
    if (export->index < module->import_table_count) {
        table =
            module->import_tables[export->index].u.table.import_table_linked;
    }
    else {
        table = &(module->tables[export->index - module->import_table_count]);
    }
    if (table->table_type.init_size < init_size
        || table->table_type.max_size > max_size) {
        LOG_DEBUG("%s,%s failed type check(%d-%d), expected(%d-%d)",
                  module_name, table_name, table->table_type.init_size,
                  table->table_type.max_size, init_size, max_size);
        set_error_buf(error_buf, error_buf_size, "incompatible import type");
        return NULL;
    }

    return table;
}

static WASMMemory *
wasm_loader_resolve_memory(const char *module_name, const char *memory_name,
                           uint32 init_page_count, uint32 max_page_count,
                           char *error_buf, uint32 error_buf_size)
{
    WASMModuleCommon *module_reg;
    WASMMemory *memory = NULL;
    WASMExport *export = NULL;
    WASMModule *module = NULL;

    module_reg = wasm_runtime_find_module_registered(module_name);
    if (!module_reg || module_reg->module_type != Wasm_Module_Bytecode) {
        LOG_DEBUG("can not find a module named %s for memory", module_name);
        set_error_buf(error_buf, error_buf_size, "unknown import");
        return NULL;
    }

    module = (WASMModule *)module_reg;
    export =
        wasm_loader_find_export(module, module_name, memory_name,
                                EXPORT_KIND_MEMORY, error_buf, error_buf_size);
    if (!export) {
        return NULL;
    }

    /* resolve memory and check the init/max page count */
    if (export->index < module->import_memory_count) {
        memory = module->import_memories[export->index]
                     .u.memory.import_memory_linked;
    }
    else {
        memory =
            &(module->memories[export->index - module->import_memory_count]);
    }
    if (memory->init_page_count < init_page_count
        || memory->max_page_count > max_page_count) {
        LOG_DEBUG("%s,%s failed type check(%d-%d), expected(%d-%d)",
                  module_name, memory_name, memory->init_page_count,
                  memory->max_page_count, init_page_count, max_page_count);
        set_error_buf(error_buf, error_buf_size, "incompatible import type");
        return NULL;
    }
    return memory;
}

static WASMGlobal *
wasm_loader_resolve_global(const char *module_name, const char *global_name,
                           uint8 type, bool is_mutable, char *error_buf,
                           uint32 error_buf_size)
{
    WASMModuleCommon *module_reg;
    WASMGlobal *global = NULL;
    WASMExport *export = NULL;
    WASMModule *module = NULL;

    module_reg = wasm_runtime_find_module_registered(module_name);
    if (!module_reg || module_reg->module_type != Wasm_Module_Bytecode) {
        LOG_DEBUG("can not find a module named %s for global", module_name);
        set_error_buf(error_buf, error_buf_size, "unknown import");
        return NULL;
    }

    module = (WASMModule *)module_reg;
    export =
        wasm_loader_find_export(module, module_name, global_name,
                                EXPORT_KIND_GLOBAL, error_buf, error_buf_size);
    if (!export) {
        return NULL;
    }

    /* resolve and check the global */
    if (export->index < module->import_global_count) {
        global =
            module->import_globals[export->index].u.global.import_global_linked;
    }
    else {
        global =
            &(module->globals[export->index - module->import_global_count]);
    }
    if (global->type.val_type != type
        || global->type.is_mutable != is_mutable) {
        LOG_DEBUG("%s,%s failed type check(%d, %d), expected(%d, %d)",
                  module_name, global_name, global->type.val_type,
                  global->type.is_mutable, type, is_mutable);
        set_error_buf(error_buf, error_buf_size, "incompatible import type");
        return NULL;
    }
    return global;
}

#if WASM_ENABLE_TAGS != 0
static WASMTag *
wasm_loader_resolve_tag(const char *module_name, const char *tag_name,
                        const WASMType *expected_tag_type,
                        uint32 *linked_tag_index, char *error_buf,
                        uint32 error_buf_size)
{
    WASMModuleCommon *module_reg;
    WASMTag *tag = NULL;
    WASMExport *export = NULL;
    WASMModule *module = NULL;

    module_reg = wasm_runtime_find_module_registered(module_name);
    if (!module_reg || module_reg->module_type != Wasm_Module_Bytecode) {
        LOG_DEBUG("can not find a module named %s for tag %s", module_name,
                  tag_name);
        set_error_buf(error_buf, error_buf_size, "unknown import");
        return NULL;
    }

    module = (WASMModule *)module_reg;
    export =
        wasm_loader_find_export(module, module_name, tag_name, EXPORT_KIND_TAG,
                                error_buf, error_buf_size);
    if (!export) {
        return NULL;
    }

    /* resolve tag type and tag */
    if (export->index < module->import_tag_count) {
        /* importing an imported tag from the submodule */
        tag = module->import_tags[export->index].u.tag.import_tag_linked;
    }
    else {
        /* importing an section tag from the submodule */
        tag = module->tags[export->index - module->import_tag_count];
    }

    /* check function type */
    if (!wasm_type_equal(expected_tag_type, tag->tag_type, module->types,
                         module->type_count)) {
        LOG_DEBUG("%s.%s failed the type check", module_name, tag_name);
        set_error_buf(error_buf, error_buf_size, "incompatible import type");
        return NULL;
    }

    if (linked_tag_index != NULL) {
        *linked_tag_index = export->index;
    }

    return tag;
}
#endif /* end of WASM_ENABLE_TAGS != 0 */
#endif /* end of WASM_ENABLE_MULTI_MODULE */

static bool
load_function_import(const uint8 **p_buf, const uint8 *buf_end,
                     const WASMModule *parent_module,
                     const char *sub_module_name, const char *function_name,
                     WASMFunctionImport *function, bool no_resolve,
                     char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 declare_type_index = 0;

    read_leb_uint32(p, p_end, declare_type_index);
    *p_buf = p;

    if (declare_type_index >= parent_module->type_count) {
        set_error_buf(error_buf, error_buf_size, "unknown type");
        return false;
    }

#if WASM_ENABLE_GC != 0
    function->type_idx = declare_type_index;
#endif

#if (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0)
    declare_type_index = wasm_get_smallest_type_idx(
        parent_module->types, parent_module->type_count, declare_type_index);
#endif

    function->func_type =
        (WASMFuncType *)parent_module->types[declare_type_index];

    function->module_name = (char *)sub_module_name;
    function->field_name = (char *)function_name;
    function->attachment = NULL;
    function->signature = NULL;
    function->call_conv_raw = false;

    /* lookup registered native symbols first */
    if (!no_resolve) {
        wasm_resolve_import_func(parent_module, function);
    }
    return true;
fail:
    return false;
}

static bool
check_table_max_size(uint32 init_size, uint32 max_size, char *error_buf,
                     uint32 error_buf_size)
{
    if (max_size < init_size) {
        set_error_buf(error_buf, error_buf_size,
                      "size minimum must not be greater than maximum");
        return false;
    }
    return true;
}

static bool
load_table_import(const uint8 **p_buf, const uint8 *buf_end,
                  WASMModule *parent_module, const char *sub_module_name,
                  const char *table_name, WASMTableImport *table,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
    uint32 declare_elem_type = 0, table_flag = 0, declare_init_size = 0,
           declare_max_size = 0;
#if WASM_ENABLE_MULTI_MODULE != 0
    WASMModule *sub_module = NULL;
    WASMTable *linked_table = NULL;
#endif
#if WASM_ENABLE_GC != 0
    WASMRefType ref_type;
    bool need_ref_type_map;
#endif
    bool is_table64 = false;

#if WASM_ENABLE_GC == 0
    CHECK_BUF(p, p_end, 1);
    /* 0x70 or 0x6F */
    declare_elem_type = read_uint8(p);
    if (VALUE_TYPE_FUNCREF != declare_elem_type
#if WASM_ENABLE_REF_TYPES != 0
        && VALUE_TYPE_EXTERNREF != declare_elem_type
#endif
    ) {
        set_error_buf(error_buf, error_buf_size, "incompatible import type");
        return false;
    }
#else /* else of WASM_ENABLE_GC == 0 */
    if (!resolve_value_type(&p, p_end, parent_module, parent_module->type_count,
                            &need_ref_type_map, &ref_type, false, error_buf,
                            error_buf_size)) {
        return false;
    }
    if (wasm_is_reftype_htref_non_nullable(ref_type.ref_type)) {
        set_error_buf(error_buf, error_buf_size, "type mismatch");
        return false;
    }
    declare_elem_type = ref_type.ref_type;
    if (need_ref_type_map) {
        if (!(table->table_type.elem_ref_type =
                  reftype_set_insert(parent_module->ref_type_set, &ref_type,
                                     error_buf, error_buf_size))) {
            return false;
        }
    }
#if TRACE_WASM_LOADER != 0
    os_printf("import table type: ");
    wasm_dump_value_type(declare_elem_type, table->table_type.elem_ref_type);
    os_printf("\n");
#endif
#endif /* end of WASM_ENABLE_GC == 0 */

    p_org = p;
    read_leb_uint32(p, p_end, table_flag);
    is_table64 = table_flag & TABLE64_FLAG;
    if (p - p_org > 1) {
        LOG_VERBOSE("integer representation too long(import table)");
        set_error_buf(error_buf, error_buf_size, "invalid limits flags");
        return false;
    }

    if (!wasm_table_check_flags(table_flag, error_buf, error_buf_size, false)) {
        return false;
    }

    read_leb_uint32(p, p_end, declare_init_size);
    if (table_flag & MAX_TABLE_SIZE_FLAG) {
        read_leb_uint32(p, p_end, declare_max_size);
        if (!check_table_max_size(declare_init_size, declare_max_size,
                                  error_buf, error_buf_size))
            return false;
    }

    adjust_table_max_size(is_table64, declare_init_size,
                          table_flag & MAX_TABLE_SIZE_FLAG, &declare_max_size);

    *p_buf = p;

#if WASM_ENABLE_MULTI_MODULE != 0
    if (!wasm_runtime_is_built_in_module(sub_module_name)) {
        sub_module = (WASMModule *)wasm_runtime_load_depended_module(
            (WASMModuleCommon *)parent_module, sub_module_name, error_buf,
            error_buf_size);
        if (sub_module) {
            linked_table = wasm_loader_resolve_table(
                sub_module_name, table_name, declare_init_size,
                declare_max_size, error_buf, error_buf_size);
            if (linked_table) {
                /* reset with linked table limit */
                declare_elem_type = linked_table->table_type.elem_type;
                declare_init_size = linked_table->table_type.init_size;
                declare_max_size = linked_table->table_type.max_size;
                table_flag = linked_table->table_type.flags;
                table->import_table_linked = linked_table;
                table->import_module = sub_module;
            }
        }
    }
#endif /* WASM_ENABLE_MULTI_MODULE != 0 */

    /* (table (export "table") 10 20 funcref) */
    /* (table (export "table64") 10 20 funcref) */
    /* we need this section working in wamrc */
    if (!strcmp("spectest", sub_module_name)) {
        const uint32 spectest_table_init_size = 10;
        const uint32 spectest_table_max_size = 20;

        if (strcmp("table", table_name)
#if WASM_ENABLE_MEMORY64 != 0
            && strcmp("table64", table_name)
#endif
        ) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type or unknown import");
            return false;
        }

        if (declare_init_size > spectest_table_init_size
            || declare_max_size < spectest_table_max_size) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type");
            return false;
        }

        declare_init_size = spectest_table_init_size;
        declare_max_size = spectest_table_max_size;
    }

    /* now we believe all declaration are ok */
    table->table_type.elem_type = declare_elem_type;
    table->table_type.init_size = declare_init_size;
    table->table_type.flags = table_flag;
    table->table_type.max_size = declare_max_size;

#if WASM_ENABLE_WAMR_COMPILER != 0
    if (table->table_type.elem_type == VALUE_TYPE_EXTERNREF)
        parent_module->is_ref_types_used = true;
#endif
    (void)parent_module;
    return true;
fail:
    return false;
}

static bool
check_memory_init_size(bool is_memory64, uint32 init_size, char *error_buf,
                       uint32 error_buf_size)
{
    uint32 default_max_size =
        is_memory64 ? DEFAULT_MEM64_MAX_PAGES : DEFAULT_MAX_PAGES;

    if (!is_memory64 && init_size > default_max_size) {
        set_error_buf(error_buf, error_buf_size,
                      "memory size must be at most 65536 pages (4GiB)");
        return false;
    }
#if WASM_ENABLE_MEMORY64 != 0
    else if (is_memory64 && init_size > default_max_size) {
        set_error_buf(
            error_buf, error_buf_size,
            "memory size must be at most 4,294,967,295 pages (274 Terabyte)");
        return false;
    }
#endif
    return true;
}

static bool
check_memory_max_size(bool is_memory64, uint32 init_size, uint32 max_size,
                      char *error_buf, uint32 error_buf_size)
{
    uint32 default_max_size =
        is_memory64 ? DEFAULT_MEM64_MAX_PAGES : DEFAULT_MAX_PAGES;

    if (max_size < init_size) {
        set_error_buf(error_buf, error_buf_size,
                      "size minimum must not be greater than maximum");
        return false;
    }

    if (!is_memory64 && max_size > default_max_size) {
        set_error_buf(error_buf, error_buf_size,
                      "memory size must be at most 65536 pages (4GiB)");
        return false;
    }
#if WASM_ENABLE_MEMORY64 != 0
    else if (is_memory64 && max_size > default_max_size) {
        set_error_buf(
            error_buf, error_buf_size,
            "memory size must be at most 4,294,967,295 pages (274 Terabyte)");
        return false;
    }
#endif

    return true;
}

static bool
load_memory_import(const uint8 **p_buf, const uint8 *buf_end,
                   WASMModule *parent_module, const char *sub_module_name,
                   const char *memory_name, WASMMemoryImport *memory,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
#if WASM_ENABLE_APP_FRAMEWORK != 0
    uint32 pool_size = wasm_runtime_memory_pool_size();
    uint32 max_page_count = pool_size * APP_MEMORY_MAX_GLOBAL_HEAP_PERCENT
                            / DEFAULT_NUM_BYTES_PER_PAGE;
#else
    uint32 max_page_count;
#endif /* WASM_ENABLE_APP_FRAMEWORK */
    uint32 mem_flag = 0;
    bool is_memory64 = false;
    uint32 declare_init_page_count = 0;
    uint32 declare_max_page_count = 0;
#if WASM_ENABLE_MULTI_MODULE != 0
    WASMModule *sub_module = NULL;
    WASMMemory *linked_memory = NULL;
#endif

    p_org = p;
    read_leb_uint32(p, p_end, mem_flag);
    is_memory64 = mem_flag & MEMORY64_FLAG;
    if (p - p_org > 1) {
        LOG_VERBOSE("integer representation too long(import memory)");
        set_error_buf(error_buf, error_buf_size, "invalid limits flags");
        return false;
    }

    if (!wasm_memory_check_flags(mem_flag, error_buf, error_buf_size, false)) {
        return false;
    }

    read_leb_uint32(p, p_end, declare_init_page_count);
    if (!check_memory_init_size(is_memory64, declare_init_page_count, error_buf,
                                error_buf_size)) {
        return false;
    }

#if WASM_ENABLE_APP_FRAMEWORK == 0
    max_page_count = is_memory64 ? DEFAULT_MEM64_MAX_PAGES : DEFAULT_MAX_PAGES;
#endif
    if (mem_flag & MAX_PAGE_COUNT_FLAG) {
        read_leb_uint32(p, p_end, declare_max_page_count);
        if (!check_memory_max_size(is_memory64, declare_init_page_count,
                                   declare_max_page_count, error_buf,
                                   error_buf_size)) {
            return false;
        }
        if (declare_max_page_count > max_page_count) {
            declare_max_page_count = max_page_count;
        }
    }
    else {
        /* Limit the maximum memory size to max_page_count */
        declare_max_page_count = max_page_count;
    }

#if WASM_ENABLE_MULTI_MODULE != 0
    if (!wasm_runtime_is_built_in_module(sub_module_name)) {
        sub_module = (WASMModule *)wasm_runtime_load_depended_module(
            (WASMModuleCommon *)parent_module, sub_module_name, error_buf,
            error_buf_size);
        if (sub_module) {
            linked_memory = wasm_loader_resolve_memory(
                sub_module_name, memory_name, declare_init_page_count,
                declare_max_page_count, error_buf, error_buf_size);
            if (linked_memory) {
                /**
                 * reset with linked memory limit
                 */
                memory->import_module = sub_module;
                memory->import_memory_linked = linked_memory;
                declare_init_page_count = linked_memory->init_page_count;
                declare_max_page_count = linked_memory->max_page_count;
            }
        }
    }
#endif

    /* (memory (export "memory") 1 2) */
    if (!strcmp("spectest", sub_module_name)) {
        uint32 spectest_memory_init_page = 1;
        uint32 spectest_memory_max_page = 2;

        if (strcmp("memory", memory_name)) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type or unknown import");
            return false;
        }

        if (declare_init_page_count > spectest_memory_init_page
            || declare_max_page_count < spectest_memory_max_page) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type");
            return false;
        }

        declare_init_page_count = spectest_memory_init_page;
        declare_max_page_count = spectest_memory_max_page;
    }
#if WASM_ENABLE_WASI_TEST != 0
    /* a case in wasi-testsuite which imports ("foo" "bar") */
    else if (!strcmp("foo", sub_module_name)) {
        uint32 spectest_memory_init_page = 1;
        uint32 spectest_memory_max_page = 1;

        if (strcmp("bar", memory_name)) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type or unknown import");
            return false;
        }

        if (declare_init_page_count > spectest_memory_init_page
            || declare_max_page_count < spectest_memory_max_page) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type");
            return false;
        }

        declare_init_page_count = spectest_memory_init_page;
        declare_max_page_count = spectest_memory_max_page;
    }
#endif

    /* now we believe all declaration are ok */
    memory->mem_type.flags = mem_flag;
    memory->mem_type.init_page_count = declare_init_page_count;
    memory->mem_type.max_page_count = declare_max_page_count;
    memory->mem_type.num_bytes_per_page = DEFAULT_NUM_BYTES_PER_PAGE;

    *p_buf = p;

    (void)parent_module;
    return true;
fail:
    return false;
}

#if WASM_ENABLE_TAGS != 0
static bool
load_tag_import(const uint8 **p_buf, const uint8 *buf_end,
                const WASMModule *parent_module, /* this module ! */
                const char *sub_module_name, const char *tag_name,
                WASMTagImport *tag, /* structure to fill */
                char *error_buf, uint32 error_buf_size)
{
    /* attribute and type of the import statement */
    uint8 declare_tag_attribute;
    uint32 declare_type_index;
    const uint8 *p = *p_buf, *p_end = buf_end;
#if WASM_ENABLE_MULTI_MODULE != 0
    WASMModule *sub_module = NULL;
#endif

    /* get the one byte attribute */
    CHECK_BUF(p, p_end, 1);
    declare_tag_attribute = read_uint8(p);
    if (declare_tag_attribute != 0) {
        set_error_buf(error_buf, error_buf_size, "unknown tag attribute");
        goto fail;
    }

    /* get type */
    read_leb_uint32(p, p_end, declare_type_index);
    /* compare against module->types */
    if (declare_type_index >= parent_module->type_count) {
        set_error_buf(error_buf, error_buf_size, "unknown tag type");
        goto fail;
    }

    WASMFuncType *declare_tag_type =
        (WASMFuncType *)parent_module->types[declare_type_index];

    /* check, that the type of the declared tag returns void */
    if (declare_tag_type->result_count != 0) {
        set_error_buf(error_buf, error_buf_size,
                      "tag type signature does not return void");

        goto fail;
    }

#if WASM_ENABLE_MULTI_MODULE != 0
    if (!wasm_runtime_is_built_in_module(sub_module_name)) {
        sub_module = (WASMModule *)wasm_runtime_load_depended_module(
            (WASMModuleCommon *)parent_module, sub_module_name, error_buf,
            error_buf_size);
        if (sub_module) {
            /* wasm_loader_resolve_tag checks, that the imported tag
             * and the declared tag have the same type
             */
            uint32 linked_tag_index = 0;
            WASMTag *linked_tag = wasm_loader_resolve_tag(
                sub_module_name, tag_name, declare_tag_type,
                &linked_tag_index /* out */, error_buf, error_buf_size);
            if (linked_tag) {
                tag->import_module = sub_module;
                tag->import_tag_linked = linked_tag;
                tag->import_tag_index_linked = linked_tag_index;
            }
        }
    }
#endif
    /* store to module tag declarations */
    tag->attribute = declare_tag_attribute;
    tag->type = declare_type_index;

    tag->module_name = (char *)sub_module_name;
    tag->field_name = (char *)tag_name;
    tag->tag_type = declare_tag_type;

    *p_buf = p;
    (void)parent_module;

    LOG_VERBOSE("Load tag import success\n");

    return true;
fail:
    return false;
}
#endif /* end of WASM_ENABLE_TAGS != 0 */

static bool
load_global_import(const uint8 **p_buf, const uint8 *buf_end,
                   WASMModule *parent_module, char *sub_module_name,
                   char *global_name, WASMGlobalImport *global, char *error_buf,
                   uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 declare_type = 0;
    uint8 declare_mutable = 0;
#if WASM_ENABLE_MULTI_MODULE != 0
    WASMModule *sub_module = NULL;
    WASMGlobal *linked_global = NULL;
#endif
#if WASM_ENABLE_GC != 0
    WASMRefType ref_type;
    bool need_ref_type_map;
#endif
    bool ret = false;

#if WASM_ENABLE_GC == 0
    CHECK_BUF(p, p_end, 2);
    /* global type */
    declare_type = read_uint8(p);
    if (!is_valid_value_type_for_interpreter(declare_type)) {
        set_error_buf(error_buf, error_buf_size, "type mismatch");
        return false;
    }
    declare_mutable = read_uint8(p);
#else
    if (!resolve_value_type(&p, p_end, parent_module, parent_module->type_count,
                            &need_ref_type_map, &ref_type, false, error_buf,
                            error_buf_size)) {
        return false;
    }
    declare_type = ref_type.ref_type;
    if (need_ref_type_map) {
        if (!(global->ref_type =
                  reftype_set_insert(parent_module->ref_type_set, &ref_type,
                                     error_buf, error_buf_size))) {
            return false;
        }
    }
#if TRACE_WASM_LOADER != 0
    os_printf("import global type: ");
    wasm_dump_value_type(declare_type, global->ref_type);
    os_printf("\n");
#endif
    CHECK_BUF(p, p_end, 1);
    declare_mutable = read_uint8(p);
#endif /* end of WASM_ENABLE_GC == 0 */

    *p_buf = p;

    if (!check_mutability(declare_mutable, error_buf, error_buf_size)) {
        return false;
    }

#if WASM_ENABLE_LIBC_BUILTIN != 0
    ret = wasm_native_lookup_libc_builtin_global(sub_module_name, global_name,
                                                 global);
    if (ret) {
        if (global->type.val_type != declare_type
            || global->type.is_mutable != declare_mutable) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type");
            return false;
        }
        global->is_linked = true;
    }
#endif
#if WASM_ENABLE_MULTI_MODULE != 0
    if (!global->is_linked
        && !wasm_runtime_is_built_in_module(sub_module_name)) {
        sub_module = (WASMModule *)wasm_runtime_load_depended_module(
            (WASMModuleCommon *)parent_module, sub_module_name, error_buf,
            error_buf_size);
        if (sub_module) {
            /* check sub modules */
            linked_global = wasm_loader_resolve_global(
                sub_module_name, global_name, declare_type, declare_mutable,
                error_buf, error_buf_size);
            if (linked_global) {
                global->import_module = sub_module;
                global->import_global_linked = linked_global;
                global->is_linked = true;
            }
        }
    }
#endif

    global->module_name = sub_module_name;
    global->field_name = global_name;
    global->type.val_type = declare_type;
    global->type.is_mutable = (declare_mutable == 1);

#if WASM_ENABLE_WAMR_COMPILER != 0
    if (global->type.val_type == VALUE_TYPE_V128)
        parent_module->is_simd_used = true;
    else if (global->type.val_type == VALUE_TYPE_EXTERNREF)
        parent_module->is_ref_types_used = true;
#endif
    (void)parent_module;
    (void)ret;
    return true;
fail:
    return false;
}

static bool
load_table(const uint8 **p_buf, const uint8 *buf_end, WASMModule *module,
           WASMTable *table, char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
#if WASM_ENABLE_GC != 0
    WASMRefType ref_type;
    bool need_ref_type_map;
#endif
    bool is_table64 = false;

#if WASM_ENABLE_GC == 0
    CHECK_BUF(p, p_end, 1);
    /* 0x70 or 0x6F */
    table->table_type.elem_type = read_uint8(p);
    if (VALUE_TYPE_FUNCREF != table->table_type.elem_type
#if WASM_ENABLE_REF_TYPES != 0
        && VALUE_TYPE_EXTERNREF != table->table_type.elem_type
#endif
    ) {
        set_error_buf(error_buf, error_buf_size, "incompatible import type");
        return false;
    }
#else /* else of WASM_ENABLE_GC == 0 */
    if (!resolve_value_type(&p, p_end, module, module->type_count,
                            &need_ref_type_map, &ref_type, false, error_buf,
                            error_buf_size)) {
        return false;
    }
    table->table_type.elem_type = ref_type.ref_type;
    if (need_ref_type_map) {
        if (!(table->table_type.elem_ref_type =
                  reftype_set_insert(module->ref_type_set, &ref_type, error_buf,
                                     error_buf_size))) {
            return false;
        }
    }
#if TRACE_WASM_LOADER != 0
    os_printf("table type: ");
    wasm_dump_value_type(table->table_type.elem_type,
                         table->table_type.elem_ref_type);
    os_printf("\n");
#endif
#endif /* end of WASM_ENABLE_GC == 0 */

    p_org = p;
    read_leb_uint32(p, p_end, table->table_type.flags);
    is_table64 = table->table_type.flags & TABLE64_FLAG;
    if (p - p_org > 1) {
        LOG_VERBOSE("integer representation too long(table)");
        set_error_buf(error_buf, error_buf_size, "invalid limits flags");
        return false;
    }

    if (!wasm_table_check_flags(table->table_type.flags, error_buf,
                                error_buf_size, false)) {
        return false;
    }

    read_leb_uint32(p, p_end, table->table_type.init_size);
    if (table->table_type.flags & MAX_TABLE_SIZE_FLAG) {
        read_leb_uint32(p, p_end, table->table_type.max_size);
        if (!check_table_max_size(table->table_type.init_size,
                                  table->table_type.max_size, error_buf,
                                  error_buf_size))
            return false;
    }

    adjust_table_max_size(is_table64, table->table_type.init_size,
                          table->table_type.flags & MAX_TABLE_SIZE_FLAG,
                          &table->table_type.max_size);

#if WASM_ENABLE_WAMR_COMPILER != 0
    if (table->table_type.elem_type == VALUE_TYPE_EXTERNREF)
        module->is_ref_types_used = true;
#endif

    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
load_memory(const uint8 **p_buf, const uint8 *buf_end, WASMMemory *memory,
            char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
#if WASM_ENABLE_APP_FRAMEWORK != 0
    uint32 pool_size = wasm_runtime_memory_pool_size();
    uint32 max_page_count = pool_size * APP_MEMORY_MAX_GLOBAL_HEAP_PERCENT
                            / DEFAULT_NUM_BYTES_PER_PAGE;
#else
    uint32 max_page_count;
#endif
    bool is_memory64 = false;

    p_org = p;
    read_leb_uint32(p, p_end, memory->flags);
    is_memory64 = memory->flags & MEMORY64_FLAG;
    if (p - p_org > 1) {
        LOG_VERBOSE("integer representation too long(memory)");
        set_error_buf(error_buf, error_buf_size, "invalid limits flags");
        return false;
    }

    if (!wasm_memory_check_flags(memory->flags, error_buf, error_buf_size,
                                 false)) {
        return false;
    }

    read_leb_uint32(p, p_end, memory->init_page_count);
    if (!check_memory_init_size(is_memory64, memory->init_page_count, error_buf,
                                error_buf_size))
        return false;

#if WASM_ENABLE_APP_FRAMEWORK == 0
    max_page_count = is_memory64 ? DEFAULT_MEM64_MAX_PAGES : DEFAULT_MAX_PAGES;
#endif
    if (memory->flags & 1) {
        read_leb_uint32(p, p_end, memory->max_page_count);
        if (!check_memory_max_size(is_memory64, memory->init_page_count,
                                   memory->max_page_count, error_buf,
                                   error_buf_size))
            return false;
        if (memory->max_page_count > max_page_count)
            memory->max_page_count = max_page_count;
    }
    else {
        /* Limit the maximum memory size to max_page_count */
        memory->max_page_count = max_page_count;
    }

    memory->num_bytes_per_page = DEFAULT_NUM_BYTES_PER_PAGE;

    *p_buf = p;
    return true;
fail:
    return false;
}

static int
cmp_export_name(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

static bool
load_import_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    bool is_load_from_file_buf, bool no_resolve,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end, *p_old;
    uint32 import_count, name_len, type_index, i, u32, flags;
    uint64 total_size;
    WASMImport *import;
    WASMImport *import_functions = NULL, *import_tables = NULL;
    WASMImport *import_memories = NULL, *import_globals = NULL;
#if WASM_ENABLE_TAGS != 0
    WASMImport *import_tags = NULL;
#endif
    char *sub_module_name, *field_name;
    uint8 u8, kind, global_type;

    read_leb_uint32(p, p_end, import_count);

    if (import_count) {
        module->import_count = import_count;
        total_size = sizeof(WASMImport) * (uint64)import_count;
        if (!(module->imports =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        p_old = p;

        /* Scan firstly to get import count of each type */
        for (i = 0; i < import_count; i++) {
            /* module name */
            read_leb_uint32(p, p_end, name_len);
            CHECK_BUF(p, p_end, name_len);
            p += name_len;

            /* field name */
            read_leb_uint32(p, p_end, name_len);
            CHECK_BUF(p, p_end, name_len);
            p += name_len;

            CHECK_BUF(p, p_end, 1);
            /* 0x00/0x01/0x02/0x03/0x04 */
            kind = read_uint8(p);

            switch (kind) {
                case IMPORT_KIND_FUNC: /* import function */
                    read_leb_uint32(p, p_end, type_index);
                    module->import_function_count++;
                    break;

                case IMPORT_KIND_TABLE: /* import table */
                    CHECK_BUF(p, p_end, 1);
                    /* 0x70 */
                    u8 = read_uint8(p);
                    read_leb_uint32(p, p_end, flags);
                    read_leb_uint32(p, p_end, u32);
                    if (flags & 1)
                        read_leb_uint32(p, p_end, u32);
                    module->import_table_count++;

                    if (module->import_table_count > 1) {
#if WASM_ENABLE_REF_TYPES == 0 && WASM_ENABLE_GC == 0
                        set_error_buf(error_buf, error_buf_size,
                                      "multiple tables");
                        return false;
#elif WASM_ENABLE_WAMR_COMPILER != 0
                        module->is_ref_types_used = true;
#endif
                    }
                    break;

                case IMPORT_KIND_MEMORY: /* import memory */
                    read_leb_uint32(p, p_end, flags);
                    read_leb_uint32(p, p_end, u32);
                    if (flags & 1)
                        read_leb_uint32(p, p_end, u32);
                    module->import_memory_count++;
#if WASM_ENABLE_MULTI_MEMORY == 0
                    if (module->import_memory_count > 1) {
                        set_error_buf(error_buf, error_buf_size,
                                      "multiple memories");
                        return false;
                    }
#endif
                    break;

#if WASM_ENABLE_TAGS != 0
                case IMPORT_KIND_TAG: /* import tags */
                    /* it only counts the number of tags to import */
                    module->import_tag_count++;
                    CHECK_BUF(p, p_end, 1);
                    u8 = read_uint8(p);
                    read_leb_uint32(p, p_end, type_index);
                    break;
#endif

                case IMPORT_KIND_GLOBAL: /* import global */
#if WASM_ENABLE_GC != 0
                    /* valtype */
                    CHECK_BUF(p, p_end, 1);
                    global_type = read_uint8(p);
                    if (wasm_is_type_multi_byte_type(global_type)) {
                        int32 heap_type;
                        read_leb_int32(p, p_end, heap_type);
                        (void)heap_type;
                    }

                    /* mutability */
                    CHECK_BUF(p, p_end, 1);
                    p += 1;
#else
                    CHECK_BUF(p, p_end, 2);
                    p += 2;
#endif

                    (void)global_type;
                    module->import_global_count++;
                    break;

                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid import kind");
                    return false;
            }
        }

        if (module->import_function_count)
            import_functions = module->import_functions = module->imports;
        if (module->import_table_count)
            import_tables = module->import_tables =
                module->imports + module->import_function_count;
        if (module->import_memory_count)
            import_memories = module->import_memories =
                module->imports + module->import_function_count
                + module->import_table_count;

#if WASM_ENABLE_TAGS != 0
        if (module->import_tag_count)
            import_tags = module->import_tags =
                module->imports + module->import_function_count
                + module->import_table_count + module->import_memory_count;
        if (module->import_global_count)
            import_globals = module->import_globals =
                module->imports + module->import_function_count
                + module->import_table_count + module->import_memory_count
                + module->import_tag_count;
#else
        if (module->import_global_count)
            import_globals = module->import_globals =
                module->imports + module->import_function_count
                + module->import_table_count + module->import_memory_count;
#endif

        p = p_old;

        /* Scan again to resolve the data */
        for (i = 0; i < import_count; i++) {
            /* load module name */
            read_leb_uint32(p, p_end, name_len);
            CHECK_BUF(p, p_end, name_len);
            if (!(sub_module_name = wasm_const_str_list_insert(
                      p, name_len, module, is_load_from_file_buf, error_buf,
                      error_buf_size))) {
                return false;
            }
            p += name_len;

            /* load field name */
            read_leb_uint32(p, p_end, name_len);
            CHECK_BUF(p, p_end, name_len);
            if (!(field_name = wasm_const_str_list_insert(
                      p, name_len, module, is_load_from_file_buf, error_buf,
                      error_buf_size))) {
                return false;
            }
            p += name_len;

            CHECK_BUF(p, p_end, 1);
            /* 0x00/0x01/0x02/0x03/0x4 */
            kind = read_uint8(p);

            switch (kind) {
                case IMPORT_KIND_FUNC: /* import function */
                    bh_assert(import_functions);
                    import = import_functions++;
                    if (!load_function_import(&p, p_end, module,
                                              sub_module_name, field_name,
                                              &import->u.function, no_resolve,
                                              error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

                case IMPORT_KIND_TABLE: /* import table */
                    bh_assert(import_tables);
                    import = import_tables++;
                    if (!load_table_import(&p, p_end, module, sub_module_name,
                                           field_name, &import->u.table,
                                           error_buf, error_buf_size)) {
                        LOG_DEBUG("can not import such a table (%s,%s)",
                                  sub_module_name, field_name);
                        return false;
                    }
                    break;

                case IMPORT_KIND_MEMORY: /* import memory */
                    bh_assert(import_memories);
                    import = import_memories++;
                    if (!load_memory_import(&p, p_end, module, sub_module_name,
                                            field_name, &import->u.memory,
                                            error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

#if WASM_ENABLE_TAGS != 0
                case IMPORT_KIND_TAG:
                    bh_assert(import_tags);
                    import = import_tags++;
                    if (!load_tag_import(&p, p_end, module, sub_module_name,
                                         field_name, &import->u.tag, error_buf,
                                         error_buf_size)) {
                        return false;
                    }
                    break;
#endif

                case IMPORT_KIND_GLOBAL: /* import global */
                    bh_assert(import_globals);
                    import = import_globals++;
                    if (!load_global_import(&p, p_end, module, sub_module_name,
                                            field_name, &import->u.global,
                                            error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid import kind");
                    return false;
            }
            import->kind = kind;
            import->u.names.module_name = sub_module_name;
            import->u.names.field_name = field_name;
        }

#if WASM_ENABLE_LIBC_WASI != 0
        import = module->import_functions;
        for (i = 0; i < module->import_function_count; i++, import++) {
            if (!strcmp(import->u.names.module_name, "wasi_unstable")
                || !strcmp(import->u.names.module_name,
                           "wasi_snapshot_preview1")) {
                module->import_wasi_api = true;
                break;
            }
        }
#endif
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load import section success.\n");
    (void)u8;
    (void)u32;
    (void)type_index;
    return true;
fail:
    return false;
}

static bool
init_function_local_offsets(WASMFunction *func, char *error_buf,
                            uint32 error_buf_size)
{
    WASMFuncType *param_type = func->func_type;
    uint32 param_count = param_type->param_count;
    uint8 *param_types = param_type->types;
    uint32 local_count = func->local_count;
    uint8 *local_types = func->local_types;
    uint32 i, local_offset = 0;
    uint64 total_size = sizeof(uint16) * ((uint64)param_count + local_count);

    /*
     * Only allocate memory when total_size is not 0,
     * or the return value of malloc(0) might be NULL on some platforms,
     * which causes wasm loader return false.
     */
    if (total_size > 0
        && !(func->local_offsets =
                 loader_malloc(total_size, error_buf, error_buf_size))) {
        return false;
    }

    for (i = 0; i < param_count; i++) {
        func->local_offsets[i] = (uint16)local_offset;
        local_offset += wasm_value_type_cell_num(param_types[i]);
    }

    for (i = 0; i < local_count; i++) {
        func->local_offsets[param_count + i] = (uint16)local_offset;
        local_offset += wasm_value_type_cell_num(local_types[i]);
    }

    bh_assert(local_offset == func->param_cell_num + func->local_cell_num);
    return true;
}

static bool
load_function_section(const uint8 *buf, const uint8 *buf_end,
                      const uint8 *buf_code, const uint8 *buf_code_end,
                      WASMModule *module, char *error_buf,
                      uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    const uint8 *p_code = buf_code, *p_code_end, *p_code_save;
    uint32 func_count;
    uint64 total_size;
    uint32 code_count = 0, code_size, type_index, i, j, k, local_type_index;
    uint32 local_count, local_set_count, sub_local_count, local_cell_num;
    uint8 type;
    WASMFunction *func;
#if WASM_ENABLE_GC != 0
    bool need_ref_type_map;
    WASMRefType ref_type;
    uint32 ref_type_map_count = 0, t = 0, type_index_org;
#endif

    read_leb_uint32(p, p_end, func_count);

    if (buf_code)
        read_leb_uint32(p_code, buf_code_end, code_count);

    if (func_count != code_count) {
        set_error_buf(error_buf, error_buf_size,
                      "function and code section have inconsistent lengths or "
                      "unexpected end");
        return false;
    }

    if (is_indices_overflow(module->import_function_count, func_count,
                            error_buf, error_buf_size))
        return false;

    if (func_count) {
        module->function_count = func_count;
        total_size = sizeof(WASMFunction *) * (uint64)func_count;
        if (!(module->functions =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        for (i = 0; i < func_count; i++) {
            /* Resolve function type */
            read_leb_uint32(p, p_end, type_index);
            if (type_index >= module->type_count) {
                set_error_buf(error_buf, error_buf_size, "unknown type");
                return false;
            }

#if WASM_ENABLE_GC != 0
            type_index_org = type_index;
#endif

#if (WASM_ENABLE_WAMR_COMPILER != 0 || WASM_ENABLE_JIT != 0) \
    && WASM_ENABLE_GC == 0
            type_index = wasm_get_smallest_type_idx(
                module->types, module->type_count, type_index);
#endif

            read_leb_uint32(p_code, buf_code_end, code_size);
            if (code_size == 0 || p_code + code_size > buf_code_end) {
                set_error_buf(error_buf, error_buf_size,
                              "invalid function code size");
                return false;
            }

            /* Resolve local set count */
            p_code_end = p_code + code_size;
            local_count = 0;
            read_leb_uint32(p_code, buf_code_end, local_set_count);
            p_code_save = p_code;

#if WASM_ENABLE_GC != 0
            ref_type_map_count = 0;
#endif

            /* Calculate total local count */
            for (j = 0; j < local_set_count; j++) {
                read_leb_uint32(p_code, buf_code_end, sub_local_count);
                if (sub_local_count > UINT32_MAX - local_count) {
                    set_error_buf(error_buf, error_buf_size, "too many locals");
                    return false;
                }
#if WASM_ENABLE_GC == 0
                CHECK_BUF(p_code, buf_code_end, 1);
                /* 0x7F/0x7E/0x7D/0x7C */
                type = read_uint8(p_code);
                local_count += sub_local_count;
#if WASM_ENABLE_WAMR_COMPILER != 0
                /* If any value's type is v128, mark the module as SIMD used */
                if (type == VALUE_TYPE_V128)
                    module->is_simd_used = true;
#endif
#else
                if (!resolve_value_type(&p_code, buf_code_end, module,
                                        module->type_count, &need_ref_type_map,
                                        &ref_type, false, error_buf,
                                        error_buf_size)) {
                    return false;
                }
                local_count += sub_local_count;
                if (need_ref_type_map)
                    ref_type_map_count += sub_local_count;
#endif
            }

            /* Code size in code entry can't be smaller than size of vec(locals)
             * + expr(at least 1 for opcode end). And expressions are encoded by
             * their instruction sequence terminated with an explicit 0x0B
             * opcode for end. */
            if (p_code_end <= p_code || *(p_code_end - 1) != WASM_OP_END) {
                set_error_buf(
                    error_buf, error_buf_size,
                    "section size mismatch: function body END opcode expected");
                return false;
            }

            /* Alloc memory, layout: function structure + local types */
            code_size = (uint32)(p_code_end - p_code);

            total_size = sizeof(WASMFunction) + (uint64)local_count;
            if (!(func = module->functions[i] =
                      loader_malloc(total_size, error_buf, error_buf_size))) {
                return false;
            }
#if WASM_ENABLE_GC != 0
            if (ref_type_map_count > 0) {
                total_size =
                    sizeof(WASMRefTypeMap) * (uint64)ref_type_map_count;
                if (!(func->local_ref_type_maps = loader_malloc(
                          total_size, error_buf, error_buf_size))) {
                    return false;
                }
                func->local_ref_type_map_count = ref_type_map_count;
            }
#endif

            /* Set function type, local count, code size and code body */
            func->func_type = (WASMFuncType *)module->types[type_index];
            func->local_count = local_count;
            if (local_count > 0)
                func->local_types = (uint8 *)func + sizeof(WASMFunction);
            func->code_size = code_size;
            /*
             * we shall make a copy of code body [p_code, p_code + code_size]
             * when we are worrying about inappropriate releasing behaviour.
             * all code bodies are actually in a buffer which user allocates in
             * his embedding environment and we don't have power on them.
             * it will be like:
             * code_body_cp = malloc(code_size);
             * memcpy(code_body_cp, p_code, code_size);
             * func->code = code_body_cp;
             */
            func->code = (uint8 *)p_code;
#if WASM_ENABLE_GC != 0
            func->type_idx = type_index_org;
#endif

#if WASM_ENABLE_GC != 0
            t = 0;
#endif

            /* Load each local type */
            p_code = p_code_save;
            local_type_index = 0;
            for (j = 0; j < local_set_count; j++) {
                read_leb_uint32(p_code, buf_code_end, sub_local_count);
                /* Note: sub_local_count is allowed to be 0 */
                if (local_type_index > UINT32_MAX - sub_local_count
                    || local_type_index + sub_local_count > local_count) {
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid local count");
                    return false;
                }
#if WASM_ENABLE_GC == 0
                CHECK_BUF(p_code, buf_code_end, 1);
                /* 0x7F/0x7E/0x7D/0x7C */
                type = read_uint8(p_code);
                if (!is_valid_value_type_for_interpreter(type)) {
                    if (type == VALUE_TYPE_V128)
                        set_error_buf(error_buf, error_buf_size,
                                      "v128 value type requires simd feature");
                    else if (type == VALUE_TYPE_FUNCREF
                             || type == VALUE_TYPE_EXTERNREF)
                        set_error_buf(error_buf, error_buf_size,
                                      "ref value type requires "
                                      "reference types feature");
                    else
                        set_error_buf_v(error_buf, error_buf_size,
                                        "invalid local type 0x%02X", type);
                    return false;
                }
#else
                if (!resolve_value_type(&p_code, buf_code_end, module,
                                        module->type_count, &need_ref_type_map,
                                        &ref_type, false, error_buf,
                                        error_buf_size)) {
                    return false;
                }
                if (need_ref_type_map) {
                    WASMRefType *ref_type_tmp;
                    if (!(ref_type_tmp = reftype_set_insert(
                              module->ref_type_set, &ref_type, error_buf,
                              error_buf_size))) {
                        return false;
                    }
                    for (k = 0; k < sub_local_count; k++) {
                        func->local_ref_type_maps[t + k].ref_type =
                            ref_type_tmp;
                        func->local_ref_type_maps[t + k].index =
                            local_type_index + k;
                    }
                    t += sub_local_count;
                }
                type = ref_type.ref_type;
#endif
                for (k = 0; k < sub_local_count; k++) {
                    func->local_types[local_type_index++] = type;
                }
#if WASM_ENABLE_WAMR_COMPILER != 0
                if (type == VALUE_TYPE_V128)
                    module->is_simd_used = true;
                else if (type == VALUE_TYPE_FUNCREF
                         || type == VALUE_TYPE_EXTERNREF)
                    module->is_ref_types_used = true;
#endif
            }

            bh_assert(local_type_index == func->local_count);
#if WASM_ENABLE_GC != 0
            bh_assert(t == func->local_ref_type_map_count);
#if TRACE_WASM_LOADER != 0
            os_printf("func %u, local types: [", i);
            k = 0;
            for (j = 0; j < func->local_count; j++) {
                WASMRefType *ref_type_tmp = NULL;
                if (wasm_is_type_multi_byte_type(func->local_types[j])) {
                    bh_assert(j == func->local_ref_type_maps[k].index);
                    ref_type_tmp = func->local_ref_type_maps[k++].ref_type;
                }
                wasm_dump_value_type(func->local_types[j], ref_type_tmp);
                if (j < func->local_count - 1)
                    os_printf(" ");
            }
            os_printf("]\n");
#endif
#endif

            func->param_cell_num = func->func_type->param_cell_num;
            func->ret_cell_num = func->func_type->ret_cell_num;
            local_cell_num =
                wasm_get_cell_num(func->local_types, func->local_count);

            if (local_cell_num > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "local count too large");
                return false;
            }

            func->local_cell_num = (uint16)local_cell_num;

            if (!init_function_local_offsets(func, error_buf, error_buf_size))
                return false;

            p_code = p_code_end;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load function section success.\n");
    return true;
fail:
    return false;
}

static bool
load_table_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 table_count, i;
    uint64 total_size;
    WASMTable *table;

    read_leb_uint32(p, p_end, table_count);
    if (module->import_table_count + table_count > 1) {
#if WASM_ENABLE_REF_TYPES == 0 && WASM_ENABLE_GC == 0
        /* a total of one table is allowed */
        set_error_buf(error_buf, error_buf_size, "multiple tables");
        return false;
#elif WASM_ENABLE_WAMR_COMPILER != 0
        module->is_ref_types_used = true;
#endif
    }

    if (table_count) {
        module->table_count = table_count;
        total_size = sizeof(WASMTable) * (uint64)table_count;
        if (!(module->tables =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        /* load each table */
        table = module->tables;
        for (i = 0; i < table_count; i++, table++) {
#if WASM_ENABLE_GC != 0
            uint8 flag;
            bool has_init = false;

            CHECK_BUF(buf, buf_end, 1);
            flag = read_uint8(p);

            if (flag == TABLE_INIT_EXPR_FLAG) {
                CHECK_BUF(buf, buf_end, 1);
                flag = read_uint8(p);

                if (flag != 0x00) {
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid leading bytes for table");
                    return false;
                }
                has_init = true;
            }
            else {
                p--;
            }
#endif /* end of WASM_ENABLE_GC != 0 */

            if (!load_table(&p, p_end, module, table, error_buf,
                            error_buf_size))
                return false;

#if WASM_ENABLE_GC != 0
            if (has_init) {
                if (!load_init_expr(module, &p, p_end, &table->init_expr,
                                    table->table_type.elem_type,
                                    table->table_type.elem_ref_type, error_buf,
                                    error_buf_size))
                    return false;
                if (table->init_expr.init_expr_type >= INIT_EXPR_TYPE_STRUCT_NEW
                    && table->init_expr.init_expr_type
                           <= INIT_EXPR_TYPE_ARRAY_NEW_FIXED) {
                    set_error_buf(
                        error_buf, error_buf_size,
                        "unsupported initializer expression for table");
                    return false;
                }
            }
            else {
                if (wasm_is_reftype_htref_non_nullable(
                        table->table_type.elem_type)) {
                    set_error_buf(
                        error_buf, error_buf_size,
                        "type mismatch: non-nullable table without init expr");
                    return false;
                }
            }
#endif /* end of WASM_ENABLE_GC != 0 */

#if WASM_ENABLE_WAMR_COMPILER != 0
            if (table->table_type.elem_type == VALUE_TYPE_EXTERNREF)
                module->is_ref_types_used = true;
#endif
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load table section success.\n");
    return true;
fail:
    return false;
}

static bool
load_memory_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 memory_count, i;
    uint64 total_size;
    WASMMemory *memory;

    read_leb_uint32(p, p_end, memory_count);

#if WASM_ENABLE_MULTI_MEMORY == 0
    /* a total of one memory is allowed */
    if (module->import_memory_count + memory_count > 1) {
        set_error_buf(error_buf, error_buf_size, "multiple memories");
        return false;
    }
#endif

    if (memory_count) {
        module->memory_count = memory_count;
        total_size = sizeof(WASMMemory) * (uint64)memory_count;
        if (!(module->memories =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        /* load each memory */
        memory = module->memories;
        for (i = 0; i < memory_count; i++, memory++)
            if (!load_memory(&p, p_end, memory, error_buf, error_buf_size))
                return false;
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load memory section success.\n");
    return true;
fail:
    return false;
}

static bool
load_global_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 global_count, i;
    uint64 total_size;
    WASMGlobal *global;
    uint8 mutable;
#if WASM_ENABLE_GC != 0
    bool need_ref_type_map;
    WASMRefType ref_type;
#endif

    read_leb_uint32(p, p_end, global_count);
    if (is_indices_overflow(module->import_global_count, global_count,
                            error_buf, error_buf_size))
        return false;

    module->global_count = 0;
    if (global_count) {
        total_size = sizeof(WASMGlobal) * (uint64)global_count;
        if (!(module->globals =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        global = module->globals;

        for (i = 0; i < global_count; i++, global++) {
#if WASM_ENABLE_GC == 0
            CHECK_BUF(p, p_end, 2);
            /* global type */
            global->type.val_type = read_uint8(p);
            if (!is_valid_value_type_for_interpreter(global->type.val_type)) {
                set_error_buf(error_buf, error_buf_size, "type mismatch");
                return false;
            }
            mutable = read_uint8(p);
#else
            if (!resolve_value_type(&p, p_end, module, module->type_count,
                                    &need_ref_type_map, &ref_type, false,
                                    error_buf, error_buf_size)) {
                return false;
            }
            global->type.val_type = ref_type.ref_type;
            CHECK_BUF(p, p_end, 1);
            mutable = read_uint8(p);
#endif /* end of WASM_ENABLE_GC */

#if WASM_ENABLE_WAMR_COMPILER != 0
            if (global->type.val_type == VALUE_TYPE_V128)
                module->is_simd_used = true;
            else if (global->type.val_type == VALUE_TYPE_FUNCREF
                     || global->type.val_type == VALUE_TYPE_EXTERNREF)
                module->is_ref_types_used = true;
#endif

            if (!check_mutability(mutable, error_buf, error_buf_size)) {
                return false;
            }
            global->type.is_mutable = mutable ? true : false;

            /* initialize expression */
            if (!load_init_expr(module, &p, p_end, &(global->init_expr),
                                global->type.val_type,
#if WASM_ENABLE_GC == 0
                                NULL,
#else
                                &ref_type,
#endif
                                error_buf, error_buf_size))
                return false;

#if WASM_ENABLE_GC != 0
            if (global->init_expr.init_expr_type == INIT_EXPR_TYPE_GET_GLOBAL) {
                uint8 global_type;
                WASMRefType *global_ref_type;
                uint32 global_idx = global->init_expr.u.global_index;

                if (global->init_expr.u.global_index
                    >= module->import_global_count + i) {
                    set_error_buf(error_buf, error_buf_size, "unknown global");
                    return false;
                }

                if (global_idx < module->import_global_count) {
                    global_type = module->import_globals[global_idx]
                                      .u.global.type.val_type;
                    global_ref_type =
                        module->import_globals[global_idx].u.global.ref_type;
                }
                else {
                    global_type =
                        module
                            ->globals[global_idx - module->import_global_count]
                            .type.val_type;
                    global_ref_type =
                        module
                            ->globals[global_idx - module->import_global_count]
                            .ref_type;
                }
                if (!wasm_reftype_is_subtype_of(
                        global_type, global_ref_type, global->type.val_type,
                        global->ref_type, module->types, module->type_count)) {
                    set_error_buf(error_buf, error_buf_size, "type mismatch");
                    return false;
                }
            }

            if (need_ref_type_map) {
                if (!(global->ref_type =
                          reftype_set_insert(module->ref_type_set, &ref_type,
                                             error_buf, error_buf_size))) {
                    return false;
                }
            }
#if TRACE_WASM_LOADER != 0
            os_printf("global type: ");
            wasm_dump_value_type(global->type, global->ref_type);
            os_printf("\n");
#endif
#endif
            module->global_count++;
        }
        bh_assert(module->global_count == global_count);
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load global section success.\n");
    return true;
fail:
    return false;
}

static bool
check_duplicate_exports(WASMModule *module, char *error_buf,
                        uint32 error_buf_size)
{
    uint32 i;
    bool result = false;
    char *names_buf[32], **names = names_buf;

    if (module->export_count > 32) {
        names = loader_malloc(module->export_count * sizeof(char *), error_buf,
                              error_buf_size);
        if (!names) {
            return result;
        }
    }

    for (i = 0; i < module->export_count; i++) {
        names[i] = module->exports[i].name;
    }

    qsort(names, module->export_count, sizeof(char *), cmp_export_name);

    for (i = 1; i < module->export_count; i++) {
        if (!strcmp(names[i], names[i - 1])) {
            set_error_buf(error_buf, error_buf_size, "duplicate export name");
            goto cleanup;
        }
    }

    result = true;
cleanup:
    if (module->export_count > 32) {
        wasm_runtime_free(names);
    }
    return result;
}

static bool
load_export_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    bool is_load_from_file_buf, char *error_buf,
                    uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 export_count, i, index;
    uint64 total_size;
    uint32 str_len;
    WASMExport *export;

    read_leb_uint32(p, p_end, export_count);

    if (export_count) {
        module->export_count = export_count;
        total_size = sizeof(WASMExport) * (uint64)export_count;
        if (!(module->exports =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        export = module->exports;
        for (i = 0; i < export_count; i++, export ++) {
#if WASM_ENABLE_THREAD_MGR == 0
            if (p == p_end) {
                /* export section with inconsistent count:
                   n export declared, but less than n given */
                set_error_buf(error_buf, error_buf_size,
                              "length out of bounds");
                return false;
            }
#endif
            read_leb_uint32(p, p_end, str_len);
            CHECK_BUF(p, p_end, str_len);

            if (!(export->name = wasm_const_str_list_insert(
                      p, str_len, module, is_load_from_file_buf, error_buf,
                      error_buf_size))) {
                return false;
            }

            p += str_len;
            CHECK_BUF(p, p_end, 1);
            export->kind = read_uint8(p);
            read_leb_uint32(p, p_end, index);
            export->index = index;

            switch (export->kind) {
                /* function index */
                case EXPORT_KIND_FUNC:
                    if (index >= module->function_count
                                     + module->import_function_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown function");
                        return false;
                    }
#if WASM_ENABLE_SIMD != 0
#if (WASM_ENABLE_WAMR_COMPILER != 0) || (WASM_ENABLE_JIT != 0)
                    /* TODO: check func type, if it has v128 param or result,
                             report error */
#endif
#endif
                    break;
                /* table index */
                case EXPORT_KIND_TABLE:
                    if (index
                        >= module->table_count + module->import_table_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown table");
                        return false;
                    }
                    break;
                /* memory index */
                case EXPORT_KIND_MEMORY:
                    if (index
                        >= module->memory_count + module->import_memory_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown memory");
                        return false;
                    }
                    break;
#if WASM_ENABLE_TAGS != 0
                /* export tag */
                case EXPORT_KIND_TAG:
                    if (index >= module->tag_count + module->import_tag_count) {
                        set_error_buf(error_buf, error_buf_size, "unknown tag");
                        return false;
                    }
                    break;
#endif

                /* global index */
                case EXPORT_KIND_GLOBAL:
                    if (index
                        >= module->global_count + module->import_global_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown global");
                        return false;
                    }
                    break;

                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid export kind");
                    return false;
            }
        }

        if (!check_duplicate_exports(module, error_buf, error_buf_size)) {
            return false;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load export section success.\n");
    return true;
fail:
    return false;
}

static bool
check_table_index(const WASMModule *module, uint32 table_index, char *error_buf,
                  uint32 error_buf_size)
{
#if WASM_ENABLE_REF_TYPES == 0 && WASM_ENABLE_GC == 0
    if (table_index != 0) {
        set_error_buf(error_buf, error_buf_size, "zero byte expected");
        return false;
    }
#endif

    if (table_index >= module->import_table_count + module->table_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown table %d",
                        table_index);
        return false;
    }
    return true;
}

static bool
load_table_index(const uint8 **p_buf, const uint8 *buf_end, WASMModule *module,
                 uint32 *p_table_index, char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 table_index;

    read_leb_uint32(p, p_end, table_index);
    if (!check_table_index(module, table_index, error_buf, error_buf_size)) {
        return false;
    }

    *p_table_index = table_index;
    *p_buf = p;
    return true;
fail:
    return false;
}

/* Element segments must match element type of table */
static bool
check_table_elem_type(WASMModule *module, uint32 table_index,
                      uint32 type_from_elem_seg, char *error_buf,
                      uint32 error_buf_size)
{
    uint32 table_declared_elem_type;

    if (table_index < module->import_table_count)
        table_declared_elem_type =
            module->import_tables[table_index].u.table.table_type.elem_type;
    else
        table_declared_elem_type =
            (module->tables + table_index)->table_type.elem_type;

    if (table_declared_elem_type == type_from_elem_seg)
        return true;

#if WASM_ENABLE_GC != 0
    /*
     * balance in: anyref, funcref, (ref.null func) and (ref.func)
     */
    if (table_declared_elem_type == REF_TYPE_ANYREF)
        return true;

    if (table_declared_elem_type == VALUE_TYPE_FUNCREF
        && type_from_elem_seg == REF_TYPE_HT_NON_NULLABLE)
        return true;

    if (table_declared_elem_type == REF_TYPE_HT_NULLABLE
        && type_from_elem_seg == REF_TYPE_HT_NON_NULLABLE)
        return true;
#endif

    set_error_buf(error_buf, error_buf_size, "type mismatch");
    return false;
}

#if WASM_ENABLE_REF_TYPES != 0 || WASM_ENABLE_GC != 0
static bool
load_elem_type(WASMModule *module, const uint8 **p_buf, const uint8 *buf_end,
               uint32 *p_elem_type,
#if WASM_ENABLE_GC != 0
               WASMRefType **p_elem_ref_type,
#endif
               bool elemkind_zero, char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 elem_type;
#if WASM_ENABLE_GC != 0
    WASMRefType elem_ref_type;
    bool need_ref_type_map;
#endif

    CHECK_BUF(p, p_end, 1);
    elem_type = read_uint8(p);
    if (elemkind_zero) {
        if (elem_type != 0) {
            set_error_buf(error_buf, error_buf_size,
                          "invalid reference type or unknown type");
            return false;
        }
        else {
            *p_elem_type = VALUE_TYPE_FUNCREF;
            *p_buf = p;
            return true;
        }
    }

#if WASM_ENABLE_GC == 0
    if (elem_type != VALUE_TYPE_FUNCREF && elem_type != VALUE_TYPE_EXTERNREF) {
        set_error_buf(error_buf, error_buf_size,
                      "invalid reference type or unknown type");
        return false;
    }
    *p_elem_type = elem_type;
#else
    p--;
    if (!resolve_value_type((const uint8 **)&p, p_end, module,
                            module->type_count, &need_ref_type_map,
                            &elem_ref_type, false, error_buf, error_buf_size)) {
        return false;
    }
    if (!wasm_is_type_reftype(elem_ref_type.ref_type)) {
        set_error_buf(error_buf, error_buf_size,
                      "invalid reference type or unknown type");
        return false;
    }
    *p_elem_type = elem_ref_type.ref_type;
    if (need_ref_type_map) {
        if (!(*p_elem_ref_type =
                  reftype_set_insert(module->ref_type_set, &elem_ref_type,
                                     error_buf, error_buf_size))) {
            return false;
        }
    }
#endif

    *p_buf = p;
    return true;
fail:
    return false;
}
#endif /* end of WASM_ENABLE_REF_TYPES != 0 || WASM_ENABLE_GC != 0 */

static bool
load_func_index_vec(const uint8 **p_buf, const uint8 *buf_end,
                    WASMModule *module, WASMTableSeg *table_segment,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 function_count, function_index = 0, i;
    uint64 total_size;

    read_leb_uint32(p, p_end, function_count);
    table_segment->value_count = function_count;
    total_size = sizeof(InitializerExpression) * (uint64)function_count;
    if (total_size > 0
        && !(table_segment->init_values =
                 (InitializerExpression *)loader_malloc(total_size, error_buf,
                                                        error_buf_size))) {
        return false;
    }

    for (i = 0; i < function_count; i++) {
        InitializerExpression *init_expr = &table_segment->init_values[i];

        read_leb_uint32(p, p_end, function_index);
        if (!check_function_index(module, function_index, error_buf,
                                  error_buf_size)) {
            return false;
        }

        init_expr->init_expr_type = INIT_EXPR_TYPE_FUNCREF_CONST;
        init_expr->u.ref_index = function_index;
    }

    *p_buf = p;
    return true;
fail:
    return false;
}

#if (WASM_ENABLE_GC != 0) || (WASM_ENABLE_REF_TYPES != 0)
static bool
load_init_expr_vec(const uint8 **p_buf, const uint8 *buf_end,
                   WASMModule *module, WASMTableSeg *table_segment,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 ref_count, i;
    uint64 total_size;

    read_leb_uint32(p, p_end, ref_count);
    table_segment->value_count = ref_count;
    total_size = sizeof(InitializerExpression) * (uint64)ref_count;
    if (total_size > 0
        && !(table_segment->init_values =
                 (InitializerExpression *)loader_malloc(total_size, error_buf,
                                                        error_buf_size))) {
        return false;
    }

    for (i = 0; i < ref_count; i++) {
        InitializerExpression *init_expr = &table_segment->init_values[i];

        if (!load_init_expr(module, &p, p_end, init_expr,
                            table_segment->elem_type,
#if WASM_ENABLE_GC == 0
                            NULL,
#else
                            table_segment->elem_ref_type,
#endif
                            error_buf, error_buf_size))
            return false;

        bh_assert((init_expr->init_expr_type == INIT_EXPR_TYPE_GET_GLOBAL)
                  || (init_expr->init_expr_type == INIT_EXPR_TYPE_REFNULL_CONST)
                  || (init_expr->init_expr_type >= INIT_EXPR_TYPE_FUNCREF_CONST
                      && init_expr->init_expr_type
                             <= INIT_EXPR_TYPE_ARRAY_NEW_FIXED));
    }

    *p_buf = p;
    return true;
fail:
    return false;
}
#endif /* end of (WASM_ENABLE_GC != 0) || (WASM_ENABLE_REF_TYPES != 0) */

static bool
load_table_segment_section(const uint8 *buf, const uint8 *buf_end,
                           WASMModule *module, char *error_buf,
                           uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint8 table_elem_idx_type;
    uint32 table_segment_count, i;
    uint64 total_size;
    WASMTableSeg *table_segment;

    read_leb_uint32(p, p_end, table_segment_count);

    if (table_segment_count) {
        module->table_seg_count = table_segment_count;
        total_size = sizeof(WASMTableSeg) * (uint64)table_segment_count;
        if (!(module->table_segments =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        table_segment = module->table_segments;
        for (i = 0; i < table_segment_count; i++, table_segment++) {
            if (p >= p_end) {
                set_error_buf(error_buf, error_buf_size,
                              "invalid value type or "
                              "invalid elements segment kind");
                return false;
            }
            table_elem_idx_type = VALUE_TYPE_I32;

#if WASM_ENABLE_REF_TYPES != 0 || WASM_ENABLE_GC != 0
            read_leb_uint32(p, p_end, table_segment->mode);
            /* last three bits */
            table_segment->mode = table_segment->mode & 0x07;
            switch (table_segment->mode) {
                /* elemkind/elemtype + active */
                case 0:
                case 4:
                {
#if WASM_ENABLE_GC != 0
                    if (table_segment->mode == 0) {
                        /* vec(funcidx), set elem type to (ref func) */
                        WASMRefType elem_ref_type = { 0 };
                        table_segment->elem_type = REF_TYPE_HT_NON_NULLABLE;
                        wasm_set_refheaptype_common(
                            &elem_ref_type.ref_ht_common, false,
                            HEAP_TYPE_FUNC);
                        if (!(table_segment->elem_ref_type = reftype_set_insert(
                                  module->ref_type_set, &elem_ref_type,
                                  error_buf, error_buf_size)))
                            return false;
                    }
                    else {
                        /* vec(expr), set elem type to funcref */
                        table_segment->elem_type = VALUE_TYPE_FUNCREF;
                    }
#else
                    table_segment->elem_type = VALUE_TYPE_FUNCREF;
#endif
                    table_segment->table_index = 0;

                    if (!check_table_index(module, table_segment->table_index,
                                           error_buf, error_buf_size))
                        return false;

#if WASM_ENABLE_MEMORY64 != 0
                    table_elem_idx_type =
                        is_table_64bit(module, table_segment->table_index)
                            ? VALUE_TYPE_I64
                            : VALUE_TYPE_I32;
#endif
                    if (!load_init_expr(module, &p, p_end,
                                        &table_segment->base_offset,
                                        table_elem_idx_type, NULL, error_buf,
                                        error_buf_size))
                        return false;

                    if (table_segment->mode == 0) {
                        /* vec(funcidx) */
                        if (!load_func_index_vec(&p, p_end, module,
                                                 table_segment, error_buf,
                                                 error_buf_size))
                            return false;
                    }
                    else {
                        /* vec(expr) */
                        if (!load_init_expr_vec(&p, p_end, module,
                                                table_segment, error_buf,
                                                error_buf_size))
                            return false;
                    }

                    if (!check_table_elem_type(module,
                                               table_segment->table_index,
                                               table_segment->elem_type,
                                               error_buf, error_buf_size))
                        return false;

                    break;
                }
                /* elemkind + passive/declarative */
                case 1:
                case 3:
                    if (!load_elem_type(module, &p, p_end,
                                        &table_segment->elem_type,
#if WASM_ENABLE_GC != 0
                                        &table_segment->elem_ref_type,
#endif
                                        true, error_buf, error_buf_size))
                        return false;
                    /* vec(funcidx) */
                    if (!load_func_index_vec(&p, p_end, module, table_segment,
                                             error_buf, error_buf_size))
                        return false;
                    break;
                /* elemkind/elemtype + table_idx + active */
                case 2:
                case 6:
                    if (!load_table_index(&p, p_end, module,
                                          &table_segment->table_index,
                                          error_buf, error_buf_size))
                        return false;
#if WASM_ENABLE_MEMORY64 != 0
                    table_elem_idx_type =
                        is_table_64bit(module, table_segment->table_index)
                            ? VALUE_TYPE_I64
                            : VALUE_TYPE_I32;
#endif
                    if (!load_init_expr(module, &p, p_end,
                                        &table_segment->base_offset,
                                        table_elem_idx_type, NULL, error_buf,
                                        error_buf_size))
                        return false;
                    if (!load_elem_type(module, &p, p_end,
                                        &table_segment->elem_type,
#if WASM_ENABLE_GC != 0
                                        &table_segment->elem_ref_type,
#endif
                                        table_segment->mode == 2 ? true : false,
                                        error_buf, error_buf_size))
                        return false;

                    if (table_segment->mode == 2) {
                        /* vec(funcidx) */
                        if (!load_func_index_vec(&p, p_end, module,
                                                 table_segment, error_buf,
                                                 error_buf_size))
                            return false;
                    }
                    else {
                        /* vec(expr) */
                        if (!load_init_expr_vec(&p, p_end, module,
                                                table_segment, error_buf,
                                                error_buf_size))
                            return false;
                    }

                    if (!check_table_elem_type(module,
                                               table_segment->table_index,
                                               table_segment->elem_type,
                                               error_buf, error_buf_size))
                        return false;

                    break;
                case 5:
                case 7:
                    if (!load_elem_type(module, &p, p_end,
                                        &table_segment->elem_type,
#if WASM_ENABLE_GC != 0
                                        &table_segment->elem_ref_type,
#endif
                                        false, error_buf, error_buf_size))
                        return false;
                    /* vec(expr) */
                    if (!load_init_expr_vec(&p, p_end, module, table_segment,
                                            error_buf, error_buf_size))
                        return false;
                    break;
                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "unknown element segment kind");
                    return false;
            }
#else /* else of WASM_ENABLE_REF_TYPES != 0 || WASM_ENABLE_GC != 0 */
            /*
             * like:      00  41 05 0b               04 00 01 00 01
             * for: (elem 0   (offset (i32.const 5)) $f1 $f2 $f1 $f2)
             */
            if (!load_table_index(&p, p_end, module,
                                  &table_segment->table_index, error_buf,
                                  error_buf_size))
                return false;
#if WASM_ENABLE_MEMORY64 != 0
            table_elem_idx_type =
                is_table_64bit(module, table_segment->table_index)
                    ? VALUE_TYPE_I64
                    : VALUE_TYPE_I32;
#endif
            if (!load_init_expr(module, &p, p_end, &table_segment->base_offset,
                                table_elem_idx_type, NULL, error_buf,
                                error_buf_size))
                return false;
            if (!load_func_index_vec(&p, p_end, module, table_segment,
                                     error_buf, error_buf_size))
                return false;

            table_segment->elem_type = VALUE_TYPE_FUNCREF;

            if (!check_table_elem_type(module, table_segment->table_index,
                                       table_segment->elem_type, error_buf,
                                       error_buf_size))
                return false;
#endif /* end of WASM_ENABLE_REF_TYPES != 0 || WASM_ENABLE_GC != 0 */

#if WASM_ENABLE_MEMORY64 != 0
            if (table_elem_idx_type == VALUE_TYPE_I64
                && table_segment->base_offset.u.u64 > UINT32_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "In table64, table base offset can't be "
                              "larger than UINT32_MAX");
                return false;
            }
#endif

#if WASM_ENABLE_WAMR_COMPILER != 0
            if (table_segment->elem_type == VALUE_TYPE_EXTERNREF)
                module->is_ref_types_used = true;
#endif
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load table segment section success.\n");
    return true;
fail:
    return false;
}

static bool
load_data_segment_section(const uint8 *buf, const uint8 *buf_end,
                          WASMModule *module,
#if WASM_ENABLE_BULK_MEMORY != 0
                          bool has_datacount_section,
#endif
                          bool clone_data_seg, char *error_buf,
                          uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 data_seg_count, i, mem_index, data_seg_len;
    uint64 total_size;
    WASMDataSeg *dataseg;
    InitializerExpression init_expr;
#if WASM_ENABLE_BULK_MEMORY != 0
    bool is_passive = false;
    uint32 mem_flag;
#endif
    uint8 mem_offset_type = VALUE_TYPE_I32;

    read_leb_uint32(p, p_end, data_seg_count);

#if WASM_ENABLE_BULK_MEMORY != 0
    if (has_datacount_section && data_seg_count != module->data_seg_count1) {
        set_error_buf(error_buf, error_buf_size,
                      "data count and data section have inconsistent lengths");
        return false;
    }
#endif

    if (data_seg_count) {
        module->data_seg_count = data_seg_count;
        total_size = sizeof(WASMDataSeg *) * (uint64)data_seg_count;
        if (!(module->data_segments =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        for (i = 0; i < data_seg_count; i++) {
            read_leb_uint32(p, p_end, mem_index);
#if WASM_ENABLE_BULK_MEMORY != 0
            is_passive = false;
            mem_flag = mem_index & 0x03;
            switch (mem_flag) {
                case 0x01:
                    is_passive = true;
#if WASM_ENABLE_WAMR_COMPILER != 0
                    module->is_bulk_memory_used = true;
#endif
                    break;
                case 0x00:
                    /* no memory index, treat index as 0 */
                    mem_index = 0;
                    goto check_mem_index;
                case 0x02:
                    /* read following memory index */
                    read_leb_uint32(p, p_end, mem_index);
#if WASM_ENABLE_WAMR_COMPILER != 0
                    module->is_bulk_memory_used = true;
#endif
                check_mem_index:
                    if (mem_index
                        >= module->import_memory_count + module->memory_count) {
                        set_error_buf_v(error_buf, error_buf_size,
                                        "unknown memory %d", mem_index);
                        return false;
                    }
                    break;
                case 0x03:
                default:
                    set_error_buf(error_buf, error_buf_size, "unknown memory");
                    return false;
                    break;
            }
#else
            if (mem_index
                >= module->import_memory_count + module->memory_count) {
                set_error_buf_v(error_buf, error_buf_size, "unknown memory %d",
                                mem_index);
                return false;
            }
#endif /* WASM_ENABLE_BULK_MEMORY */

#if WASM_ENABLE_BULK_MEMORY != 0
            if (!is_passive)
#endif
            {
#if WASM_ENABLE_MEMORY64 != 0
                /* This memory_flag is from memory instead of data segment */
                uint8 memory_flag;
                if (module->import_memory_count > 0) {
                    memory_flag = module->import_memories[mem_index]
                                      .u.memory.mem_type.flags;
                }
                else {
                    memory_flag =
                        module
                            ->memories[mem_index - module->import_memory_count]
                            .flags;
                }
                mem_offset_type = memory_flag & MEMORY64_FLAG ? VALUE_TYPE_I64
                                                              : VALUE_TYPE_I32;
#else
                mem_offset_type = VALUE_TYPE_I32;
#endif
            }

#if WASM_ENABLE_BULK_MEMORY != 0
            if (!is_passive)
#endif
                if (!load_init_expr(module, &p, p_end, &init_expr,
                                    mem_offset_type, NULL, error_buf,
                                    error_buf_size))
                    return false;

            read_leb_uint32(p, p_end, data_seg_len);

            if (!(dataseg = module->data_segments[i] = loader_malloc(
                      sizeof(WASMDataSeg), error_buf, error_buf_size))) {
                return false;
            }

#if WASM_ENABLE_BULK_MEMORY != 0
            dataseg->is_passive = is_passive;
            if (!is_passive)
#endif
            {
                bh_memcpy_s(&dataseg->base_offset,
                            sizeof(InitializerExpression), &init_expr,
                            sizeof(InitializerExpression));

                dataseg->memory_index = mem_index;
            }

            dataseg->data_length = data_seg_len;
            CHECK_BUF(p, p_end, data_seg_len);
            if (clone_data_seg) {
                if (!(dataseg->data = loader_malloc(
                          dataseg->data_length, error_buf, error_buf_size))) {
                    return false;
                }

                bh_memcpy_s(dataseg->data, dataseg->data_length, p,
                            data_seg_len);
            }
            else {
                dataseg->data = (uint8 *)p;
            }
            dataseg->is_data_cloned = clone_data_seg;
            p += data_seg_len;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load data segment section success.\n");
    return true;
fail:
    return false;
}

#if WASM_ENABLE_BULK_MEMORY != 0
static bool
load_datacount_section(const uint8 *buf, const uint8 *buf_end,
                       WASMModule *module, char *error_buf,
                       uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 data_seg_count1 = 0;

    read_leb_uint32(p, p_end, data_seg_count1);
    module->data_seg_count1 = data_seg_count1;

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

#if WASM_ENABLE_WAMR_COMPILER != 0
    module->is_bulk_memory_used = true;
#endif
    LOG_VERBOSE("Load datacount section success.\n");
    return true;
fail:
    return false;
}
#endif

#if WASM_ENABLE_TAGS != 0
static bool
load_tag_section(const uint8 *buf, const uint8 *buf_end, const uint8 *buf_code,
                 const uint8 *buf_code_end, WASMModule *module, char *error_buf,
                 uint32 error_buf_size)
{
    (void)buf_code;
    (void)buf_code_end;

    const uint8 *p = buf, *p_end = buf_end;
    size_t total_size = 0;
    /* number of tags defined in the section */
    uint32 section_tag_count = 0;
    uint8 tag_attribute;
    uint32 tag_type;
    WASMTag *tag = NULL;

    /* get tag count */
    read_leb_uint32(p, p_end, section_tag_count);
    if (is_indices_overflow(module->import_tag_count, section_tag_count,
                            error_buf, error_buf_size))
        return false;

    module->tag_count = section_tag_count;

    if (section_tag_count) {
        total_size = sizeof(WASMTag *) * module->tag_count;
        if (!(module->tags =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }
        /* load each tag, imported tags precede the tags */
        uint32 tag_index;
        for (tag_index = 0; tag_index < section_tag_count; tag_index++) {

            /* get the one byte attribute */
            CHECK_BUF(p, p_end, 1);
            tag_attribute = read_uint8(p);

            /* get type */
            read_leb_uint32(p, p_end, tag_type);
            /* compare against module->types */
            if (tag_type >= module->type_count) {
                set_error_buf(error_buf, error_buf_size, "unknown type");
                return false;
            }

            /* get return type (must be 0) */
            /* check, that the type of the referred tag returns void */
            WASMFuncType *func_type = (WASMFuncType *)module->types[tag_type];
            if (func_type->result_count != 0) {
                set_error_buf(error_buf, error_buf_size,
                              "non-empty tag result type");

                goto fail;
            }

            if (!(tag = module->tags[tag_index] = loader_malloc(
                      sizeof(WASMTag), error_buf, error_buf_size))) {
                return false;
            }

            /* store to module tag declarations */
            tag->attribute = tag_attribute;
            tag->type = tag_type;
            tag->tag_type = func_type;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load tag section success.\n");
    return true;
fail:
    return false;
}
#endif /* end of WASM_ENABLE_TAGS != 0 */

static bool
load_code_section(const uint8 *buf, const uint8 *buf_end, const uint8 *buf_func,
                  const uint8 *buf_func_end, WASMModule *module,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    const uint8 *p_func = buf_func;
    uint32 func_count = 0, code_count;

    /* code has been loaded in function section, so pass it here, just check
     * whether function and code section have inconsistent lengths */
    read_leb_uint32(p, p_end, code_count);

    if (buf_func)
        read_leb_uint32(p_func, buf_func_end, func_count);

    if (func_count != code_count) {
        set_error_buf(error_buf, error_buf_size,
                      "function and code section have inconsistent lengths");
        return false;
    }

    LOG_VERBOSE("Load code segment section success.\n");
    (void)module;
    return true;
fail:
    return false;
}

static bool
load_start_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    WASMFuncType *type;
    uint32 start_function;

    read_leb_uint32(p, p_end, start_function);

    if (start_function
        >= module->function_count + module->import_function_count) {
        set_error_buf(error_buf, error_buf_size, "unknown function");
        return false;
    }

    if (start_function < module->import_function_count)
        type = module->import_functions[start_function].u.function.func_type;
    else
        type = module->functions[start_function - module->import_function_count]
                   ->func_type;
    if (type->param_count != 0 || type->result_count != 0) {
        set_error_buf(error_buf, error_buf_size, "invalid start function");
        return false;
    }

    module->start_function = start_function;

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load start section success.\n");
    return true;
fail:
    return false;
}

#if WASM_ENABLE_STRINGREF != 0
static bool
load_stringref_section(const uint8 *buf, const uint8 *buf_end,
                       WASMModule *module, bool is_load_from_file_buf,
                       char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    int32 deferred_count, immediate_count, string_length, i;
    uint64 total_size;

    read_leb_uint32(p, p_end, deferred_count);
    read_leb_uint32(p, p_end, immediate_count);

    /* proposal set deferred_count for future extension */
    if (deferred_count != 0) {
        goto fail;
    }

    if (immediate_count > 0) {
        total_size = sizeof(char *) * (uint64)immediate_count;
        if (!(module->string_literal_ptrs =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            goto fail;
        }
        module->string_literal_count = immediate_count;

        total_size = sizeof(uint32) * (uint64)immediate_count;
        if (!(module->string_literal_lengths =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            goto fail;
        }

        for (i = 0; i < immediate_count; i++) {
            read_leb_uint32(p, p_end, string_length);

            CHECK_BUF(p, p_end, string_length);
            module->string_literal_ptrs[i] = p;
            module->string_literal_lengths[i] = string_length;
            p += string_length;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        goto fail;
    }

    LOG_VERBOSE("Load stringref section success.\n");
    return true;

fail:
    return false;
}
#endif /* end of WASM_ENABLE_STRINGREF != 0 */

#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
static bool
handle_name_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    bool is_load_from_file_buf, char *error_buf,
                    uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 name_type, subsection_size;
    uint32 previous_name_type = 0;
    uint32 num_func_name;
    uint32 func_index;
    uint32 previous_func_index = ~0U;
    uint32 func_name_len;
    uint32 name_index;
    int i = 0;

    if (p >= p_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }

    while (p < p_end) {
        read_leb_uint32(p, p_end, name_type);
        if (i != 0) {
            if (name_type == previous_name_type) {
                set_error_buf(error_buf, error_buf_size,
                              "duplicate sub-section");
                return false;
            }
            if (name_type < previous_name_type) {
                set_error_buf(error_buf, error_buf_size,
                              "out-of-order sub-section");
                return false;
            }
        }
        previous_name_type = name_type;
        read_leb_uint32(p, p_end, subsection_size);
        CHECK_BUF(p, p_end, subsection_size);
        switch (name_type) {
            case SUB_SECTION_TYPE_FUNC:
                if (subsection_size) {
                    read_leb_uint32(p, p_end, num_func_name);
                    for (name_index = 0; name_index < num_func_name;
                         name_index++) {
                        read_leb_uint32(p, p_end, func_index);
                        if (func_index == previous_func_index) {
                            set_error_buf(error_buf, error_buf_size,
                                          "duplicate function name");
                            return false;
                        }
                        if (func_index < previous_func_index
                            && previous_func_index != ~0U) {
                            set_error_buf(error_buf, error_buf_size,
                                          "out-of-order function index ");
                            return false;
                        }
                        previous_func_index = func_index;
                        read_leb_uint32(p, p_end, func_name_len);
                        CHECK_BUF(p, p_end, func_name_len);
                        /* Skip the import functions */
                        if (func_index >= module->import_function_count) {
                            func_index -= module->import_function_count;
                            if (func_index >= module->function_count) {
                                set_error_buf(error_buf, error_buf_size,
                                              "out-of-range function index");
                                return false;
                            }
                            if (!(module->functions[func_index]->field_name =
                                      wasm_const_str_list_insert(
                                          p, func_name_len, module,
#if WASM_ENABLE_WAMR_COMPILER != 0
                                          false,
#else
                                          is_load_from_file_buf,
#endif
                                          error_buf, error_buf_size))) {
                                return false;
                            }
                        }
                        p += func_name_len;
                    }
                }
                break;
            case SUB_SECTION_TYPE_MODULE: /* TODO: Parse for module subsection
                                           */
            case SUB_SECTION_TYPE_LOCAL:  /* TODO: Parse for local subsection */
            default:
                p = p + subsection_size;
                break;
        }
        i++;
    }

    return true;
fail:
    return false;
}
#endif

static bool
load_user_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                  bool is_load_from_file_buf, char *error_buf,
                  uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    char section_name[32];
    uint32 name_len, buffer_len;

    if (p >= p_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }

    read_leb_uint32(p, p_end, name_len);

    if (p + name_len > p_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }

    if (!wasm_check_utf8_str(p, name_len)) {
        set_error_buf(error_buf, error_buf_size, "invalid UTF-8 encoding");
        return false;
    }

    buffer_len = sizeof(section_name);
    memset(section_name, 0, buffer_len);
    if (name_len < buffer_len) {
        bh_memcpy_s(section_name, buffer_len, p, name_len);
    }
    else {
        bh_memcpy_s(section_name, buffer_len, p, buffer_len - 4);
        memset(section_name + buffer_len - 4, '.', 3);
    }

#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
    if (name_len == 4 && memcmp(p, "name", 4) == 0) {
        module->name_section_buf = buf;
        module->name_section_buf_end = buf_end;
        p += name_len;
        if (!handle_name_section(p, p_end, module, is_load_from_file_buf,
                                 error_buf, error_buf_size)) {
            return false;
        }
        LOG_VERBOSE("Load custom name section success.");
    }
#endif

#if WASM_ENABLE_LOAD_CUSTOM_SECTION != 0
    {
        WASMCustomSection *section =
            loader_malloc(sizeof(WASMCustomSection), error_buf, error_buf_size);

        if (!section) {
            return false;
        }

        section->name_addr = (char *)p;
        section->name_len = name_len;
        section->content_addr = (uint8 *)(p + name_len);
        section->content_len = (uint32)(p_end - p - name_len);

        section->next = module->custom_section_list;
        module->custom_section_list = section;
        LOG_VERBOSE("Load custom section [%s] success.", section_name);
        return true;
    }
#endif

    LOG_VERBOSE("Ignore custom section [%s].", section_name);

    (void)is_load_from_file_buf;
    (void)module;
    return true;
fail:
    return false;
}

static void
calculate_global_data_offset(WASMModule *module)
{
    uint32 i, data_offset;

    data_offset = 0;
    for (i = 0; i < module->import_global_count; i++) {
        WASMGlobalImport *import_global =
            &((module->import_globals + i)->u.global);
#if WASM_ENABLE_FAST_JIT != 0
        import_global->data_offset = data_offset;
#endif
        data_offset += wasm_value_type_size(import_global->type.val_type);
    }

    for (i = 0; i < module->global_count; i++) {
        WASMGlobal *global = module->globals + i;
#if WASM_ENABLE_FAST_JIT != 0
        global->data_offset = data_offset;
#endif
        data_offset += wasm_value_type_size(global->type.val_type);
    }

    module->global_data_size = data_offset;
}

#if WASM_ENABLE_FAST_JIT != 0
static bool
init_fast_jit_functions(WASMModule *module, char *error_buf,
                        uint32 error_buf_size)
{
#if WASM_ENABLE_LAZY_JIT != 0
    JitGlobals *jit_globals = jit_compiler_get_jit_globals();
#endif
    uint32 i;

    if (!module->function_count)
        return true;

    if (!(module->fast_jit_func_ptrs =
              loader_malloc(sizeof(void *) * module->function_count, error_buf,
                            error_buf_size))) {
        return false;
    }

#if WASM_ENABLE_LAZY_JIT != 0
    for (i = 0; i < module->function_count; i++) {
        module->fast_jit_func_ptrs[i] =
            jit_globals->compile_fast_jit_and_then_call;
    }
#endif

    for (i = 0; i < WASM_ORC_JIT_BACKEND_THREAD_NUM; i++) {
        if (os_mutex_init(&module->fast_jit_thread_locks[i]) != 0) {
            set_error_buf(error_buf, error_buf_size,
                          "init fast jit thread lock failed");
            return false;
        }
        module->fast_jit_thread_locks_inited[i] = true;
    }

    return true;
}
#endif /* end of WASM_ENABLE_FAST_JIT != 0 */

#if WASM_ENABLE_JIT != 0
static bool
init_llvm_jit_functions_stage1(WASMModule *module, char *error_buf,
                               uint32 error_buf_size)
{
    LLVMJITOptions *llvm_jit_options = wasm_runtime_get_llvm_jit_options();
    AOTCompOption option = { 0 };
    char *aot_last_error;
    uint64 size;
#if WASM_ENABLE_GC != 0
    bool gc_enabled = true;
#else
    bool gc_enabled = false;
#endif

    if (module->function_count == 0)
        return true;

#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_LAZY_JIT != 0
    if (os_mutex_init(&module->tierup_wait_lock) != 0) {
        set_error_buf(error_buf, error_buf_size, "init jit tierup lock failed");
        return false;
    }
    if (os_cond_init(&module->tierup_wait_cond) != 0) {
        set_error_buf(error_buf, error_buf_size, "init jit tierup cond failed");
        os_mutex_destroy(&module->tierup_wait_lock);
        return false;
    }
    module->tierup_wait_lock_inited = true;
#endif

    size = sizeof(void *) * (uint64)module->function_count
           + sizeof(bool) * (uint64)module->function_count;
    if (!(module->func_ptrs = loader_malloc(size, error_buf, error_buf_size))) {
        return false;
    }
    module->func_ptrs_compiled =
        (bool *)((uint8 *)module->func_ptrs
                 + sizeof(void *) * module->function_count);

    module->comp_data = aot_create_comp_data(module, NULL, gc_enabled);
    if (!module->comp_data) {
        aot_last_error = aot_get_last_error();
        bh_assert(aot_last_error != NULL);
        set_error_buf(error_buf, error_buf_size, aot_last_error);
        return false;
    }

    option.is_jit_mode = true;

    option.opt_level = llvm_jit_options->opt_level;
    option.size_level = llvm_jit_options->size_level;
    option.segue_flags = llvm_jit_options->segue_flags;
    option.quick_invoke_c_api_import =
        llvm_jit_options->quick_invoke_c_api_import;

#if WASM_ENABLE_BULK_MEMORY != 0
    option.enable_bulk_memory = true;
#endif
#if WASM_ENABLE_THREAD_MGR != 0
    option.enable_thread_mgr = true;
#endif
#if WASM_ENABLE_TAIL_CALL != 0
    option.enable_tail_call = true;
#endif
#if WASM_ENABLE_SIMD != 0
    option.enable_simd = true;
#endif
#if WASM_ENABLE_GC == 0 && WASM_ENABLE_REF_TYPES != 0
    option.enable_ref_types = true;
#elif WASM_ENABLE_GC != 0
    option.enable_gc = true;
#endif
    option.enable_aux_stack_check = true;
#if WASM_ENABLE_PERF_PROFILING != 0 || WASM_ENABLE_DUMP_CALL_STACK != 0 \
    || WASM_ENABLE_AOT_STACK_FRAME != 0
    option.aux_stack_frame_type = AOT_STACK_FRAME_TYPE_STANDARD;
    aot_call_stack_features_init_default(&option.call_stack_features);
#endif
#if WASM_ENABLE_PERF_PROFILING != 0
    option.enable_perf_profiling = true;
#endif
#if WASM_ENABLE_MEMORY_PROFILING != 0
    option.enable_memory_profiling = true;
    option.enable_stack_estimation = true;
#endif
#if WASM_ENABLE_SHARED_HEAP != 0
    option.enable_shared_heap = true;
#endif

    module->comp_ctx = aot_create_comp_context(module->comp_data, &option);
    if (!module->comp_ctx) {
        aot_last_error = aot_get_last_error();
        bh_assert(aot_last_error != NULL);
        set_error_buf(error_buf, error_buf_size, aot_last_error);
        return false;
    }

    return true;
}

static bool
init_llvm_jit_functions_stage2(WASMModule *module, char *error_buf,
                               uint32 error_buf_size)
{
    char *aot_last_error;
    uint32 i;

    if (module->function_count == 0)
        return true;

    if (!aot_compile_wasm(module->comp_ctx)) {
        aot_last_error = aot_get_last_error();
        bh_assert(aot_last_error != NULL);
        set_error_buf(error_buf, error_buf_size, aot_last_error);
        return false;
    }

#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_LAZY_JIT != 0
    if (module->orcjit_stop_compiling)
        return false;
#endif

    bh_print_time("Begin to lookup llvm jit functions");

    for (i = 0; i < module->function_count; i++) {
        LLVMOrcJITTargetAddress func_addr = 0;
        LLVMErrorRef error;
        char func_name[48];

        snprintf(func_name, sizeof(func_name), "%s%d", AOT_FUNC_PREFIX, i);
        error = LLVMOrcLLLazyJITLookup(module->comp_ctx->orc_jit, &func_addr,
                                       func_name);
        if (error != LLVMErrorSuccess) {
            char *err_msg = LLVMGetErrorMessage(error);
            set_error_buf_v(error_buf, error_buf_size,
                            "failed to compile llvm jit function: %s", err_msg);
            LLVMDisposeErrorMessage(err_msg);
            return false;
        }

        /**
         * No need to lock the func_ptr[func_idx] here as it is basic
         * data type, the load/store for it can be finished by one cpu
         * instruction, and there can be only one cpu instruction
         * loading/storing at the same time.
         */
        module->func_ptrs[i] = (void *)func_addr;

#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_LAZY_JIT != 0
        module->functions[i]->llvm_jit_func_ptr = (void *)func_addr;

        if (module->orcjit_stop_compiling)
            return false;
#endif
    }

    bh_print_time("End lookup llvm jit functions");

    return true;
}
#endif /* end of WASM_ENABLE_JIT != 0 */

#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT != 0 \
    && WASM_ENABLE_LAZY_JIT != 0
static void *
init_llvm_jit_functions_stage2_callback(void *arg)
{
    WASMModule *module = (WASMModule *)arg;
    char error_buf[128];
    uint32 error_buf_size = (uint32)sizeof(error_buf);

    if (!init_llvm_jit_functions_stage2(module, error_buf, error_buf_size)) {
        module->orcjit_stop_compiling = true;
        return NULL;
    }

    os_mutex_lock(&module->tierup_wait_lock);
    module->llvm_jit_inited = true;
    os_cond_broadcast(&module->tierup_wait_cond);
    os_mutex_unlock(&module->tierup_wait_lock);

    return NULL;
}
#endif

#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0
/* The callback function to compile jit functions */
static void *
orcjit_thread_callback(void *arg)
{
    OrcJitThreadArg *thread_arg = (OrcJitThreadArg *)arg;
#if WASM_ENABLE_JIT != 0
    AOTCompContext *comp_ctx = thread_arg->comp_ctx;
#endif
    WASMModule *module = thread_arg->module;
    uint32 group_idx = thread_arg->group_idx;
    uint32 group_stride = WASM_ORC_JIT_BACKEND_THREAD_NUM;
    uint32 func_count = module->function_count;
    uint32 i;

#if WASM_ENABLE_FAST_JIT != 0
    /* Compile fast jit functions of this group */
    for (i = group_idx; i < func_count; i += group_stride) {
        if (!jit_compiler_compile(module, i + module->import_function_count)) {
            LOG_ERROR("failed to compile fast jit function %u\n", i);
            break;
        }

        if (module->orcjit_stop_compiling) {
            return NULL;
        }
    }
#if WASM_ENABLE_JIT != 0 && WASM_ENABLE_LAZY_JIT != 0
    os_mutex_lock(&module->tierup_wait_lock);
    module->fast_jit_ready_groups++;
    os_mutex_unlock(&module->tierup_wait_lock);
#endif
#endif

#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT != 0 \
    && WASM_ENABLE_LAZY_JIT != 0
    /* For JIT tier-up, set each llvm jit func to call_to_fast_jit */
    for (i = group_idx; i < func_count;
         i += group_stride * WASM_ORC_JIT_COMPILE_THREAD_NUM) {
        uint32 j;

        for (j = 0; j < WASM_ORC_JIT_COMPILE_THREAD_NUM; j++) {
            if (i + j * group_stride < func_count) {
                if (!jit_compiler_set_call_to_fast_jit(
                        module,
                        i + j * group_stride + module->import_function_count)) {
                    LOG_ERROR(
                        "failed to compile call_to_fast_jit for func %u\n",
                        i + j * group_stride + module->import_function_count);
                    module->orcjit_stop_compiling = true;
                    return NULL;
                }
            }
            if (module->orcjit_stop_compiling) {
                return NULL;
            }
        }
    }

    /* Wait until init_llvm_jit_functions_stage2 finishes and all
       fast jit functions are compiled */
    os_mutex_lock(&module->tierup_wait_lock);
    while (!(module->llvm_jit_inited && module->enable_llvm_jit_compilation
             && module->fast_jit_ready_groups >= group_stride)) {
        os_cond_reltimedwait(&module->tierup_wait_cond,
                             &module->tierup_wait_lock, 10000);
        if (module->orcjit_stop_compiling) {
            /* init_llvm_jit_functions_stage2 failed */
            os_mutex_unlock(&module->tierup_wait_lock);
            return NULL;
        }
    }
    os_mutex_unlock(&module->tierup_wait_lock);
#endif

#if WASM_ENABLE_JIT != 0
    /* Compile llvm jit functions of this group */
    for (i = group_idx; i < func_count;
         i += group_stride * WASM_ORC_JIT_COMPILE_THREAD_NUM) {
        LLVMOrcJITTargetAddress func_addr = 0;
        LLVMErrorRef error;
        char func_name[48];
        typedef void (*F)(void);
        union {
            F f;
            void *v;
        } u;
        uint32 j;

        snprintf(func_name, sizeof(func_name), "%s%d%s", AOT_FUNC_PREFIX, i,
                 "_wrapper");
        LOG_DEBUG("compile llvm jit func %s", func_name);
        error =
            LLVMOrcLLLazyJITLookup(comp_ctx->orc_jit, &func_addr, func_name);
        if (error != LLVMErrorSuccess) {
            char *err_msg = LLVMGetErrorMessage(error);
            LOG_ERROR("failed to compile llvm jit function %u: %s", i, err_msg);
            LLVMDisposeErrorMessage(err_msg);
            break;
        }

        /* Call the jit wrapper function to trigger its compilation, so as
           to compile the actual jit functions, since we add the latter to
           function list in the PartitionFunction callback */
        u.v = (void *)func_addr;
        u.f();

        for (j = 0; j < WASM_ORC_JIT_COMPILE_THREAD_NUM; j++) {
            if (i + j * group_stride < func_count) {
                module->func_ptrs_compiled[i + j * group_stride] = true;
#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_LAZY_JIT != 0
                snprintf(func_name, sizeof(func_name), "%s%d", AOT_FUNC_PREFIX,
                         i + j * group_stride);
                error = LLVMOrcLLLazyJITLookup(comp_ctx->orc_jit, &func_addr,
                                               func_name);
                if (error != LLVMErrorSuccess) {
                    char *err_msg = LLVMGetErrorMessage(error);
                    LOG_ERROR("failed to compile llvm jit function %u: %s", i,
                              err_msg);
                    LLVMDisposeErrorMessage(err_msg);
                    /* Ignore current llvm jit func, as its func ptr is
                       previous set to call_to_fast_jit, which also works */
                    continue;
                }

                jit_compiler_set_llvm_jit_func_ptr(
                    module,
                    i + j * group_stride + module->import_function_count,
                    (void *)func_addr);

                /* Try to switch to call this llvm jit function instead of
                   fast jit function from fast jit jitted code */
                jit_compiler_set_call_to_llvm_jit(
                    module,
                    i + j * group_stride + module->import_function_count);
#endif
            }
        }

        if (module->orcjit_stop_compiling) {
            break;
        }
    }
#endif

    return NULL;
}

static void
orcjit_stop_compile_threads(WASMModule *module)
{
    uint32 i, thread_num = (uint32)(sizeof(module->orcjit_thread_args)
                                    / sizeof(OrcJitThreadArg));

    module->orcjit_stop_compiling = true;
    for (i = 0; i < thread_num; i++) {
        if (module->orcjit_threads[i])
            os_thread_join(module->orcjit_threads[i], NULL);
    }
}

static bool
compile_jit_functions(WASMModule *module, char *error_buf,
                      uint32 error_buf_size)
{
    uint32 thread_num =
        (uint32)(sizeof(module->orcjit_thread_args) / sizeof(OrcJitThreadArg));
    uint32 i, j;

    bh_print_time("Begin to compile jit functions");

    /* Create threads to compile the jit functions */
    for (i = 0; i < thread_num && i < module->function_count; i++) {
#if WASM_ENABLE_JIT != 0
        module->orcjit_thread_args[i].comp_ctx = module->comp_ctx;
#endif
        module->orcjit_thread_args[i].module = module;
        module->orcjit_thread_args[i].group_idx = i;

        if (os_thread_create(&module->orcjit_threads[i], orcjit_thread_callback,
                             (void *)&module->orcjit_thread_args[i],
                             APP_THREAD_STACK_SIZE_DEFAULT)
            != 0) {
            set_error_buf(error_buf, error_buf_size,
                          "create orcjit compile thread failed");
            /* Terminate the threads created */
            module->orcjit_stop_compiling = true;
            for (j = 0; j < i; j++) {
                os_thread_join(module->orcjit_threads[j], NULL);
            }
            return false;
        }
    }

#if WASM_ENABLE_LAZY_JIT == 0
    /* Wait until all jit functions are compiled for eager mode */
    for (i = 0; i < thread_num; i++) {
        if (module->orcjit_threads[i])
            os_thread_join(module->orcjit_threads[i], NULL);
    }

#if WASM_ENABLE_FAST_JIT != 0
    /* Ensure all the fast-jit functions are compiled */
    for (i = 0; i < module->function_count; i++) {
        if (!jit_compiler_is_compiled(module,
                                      i + module->import_function_count)) {
            set_error_buf(error_buf, error_buf_size,
                          "failed to compile fast jit function");
            return false;
        }
    }
#endif

#if WASM_ENABLE_JIT != 0
    /* Ensure all the llvm-jit functions are compiled */
    for (i = 0; i < module->function_count; i++) {
        if (!module->func_ptrs_compiled[i]) {
            set_error_buf(error_buf, error_buf_size,
                          "failed to compile llvm jit function");
            return false;
        }
    }
#endif
#endif /* end of WASM_ENABLE_LAZY_JIT == 0 */

    bh_print_time("End compile jit functions");

    return true;
}
#endif /* end of WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0 */

static bool
wasm_loader_prepare_bytecode(WASMModule *module, WASMFunction *func,
                             uint32 cur_func_idx, char *error_buf,
                             uint32 error_buf_size);

#if WASM_ENABLE_FAST_INTERP != 0 && WASM_ENABLE_LABELS_AS_VALUES != 0
void **
wasm_interp_get_handle_table(void);

static void **handle_table;
#endif

static bool
load_from_sections(WASMModule *module, WASMSection *sections,
                   bool is_load_from_file_buf, bool wasm_binary_freeable,
                   bool no_resolve, char *error_buf, uint32 error_buf_size)
{
    WASMExport *export;
    WASMSection *section = sections;
    const uint8 *buf, *buf_end, *buf_code = NULL, *buf_code_end = NULL,
                                *buf_func = NULL, *buf_func_end = NULL;
    WASMGlobal *aux_data_end_global = NULL, *aux_heap_base_global = NULL;
    WASMGlobal *aux_stack_top_global = NULL, *global;
    uint64 aux_data_end = (uint64)-1LL, aux_heap_base = (uint64)-1LL,
           aux_stack_top = (uint64)-1LL;
    uint32 global_index, func_index, i;
    uint32 aux_data_end_global_index = (uint32)-1;
    uint32 aux_heap_base_global_index = (uint32)-1;
    WASMFuncType *func_type;
    uint8 malloc_free_io_type = VALUE_TYPE_I32;
    bool reuse_const_strings = is_load_from_file_buf && !wasm_binary_freeable;
    bool clone_data_seg = is_load_from_file_buf && wasm_binary_freeable;
#if WASM_ENABLE_BULK_MEMORY != 0
    bool has_datacount_section = false;
#endif

    /* Find code and function sections if have */
    while (section) {
        if (section->section_type == SECTION_TYPE_CODE) {
            buf_code = section->section_body;
            buf_code_end = buf_code + section->section_body_size;
#if WASM_ENABLE_DEBUG_INTERP != 0 || WASM_ENABLE_DEBUG_AOT != 0
            module->buf_code = (uint8 *)buf_code;
            module->buf_code_size = section->section_body_size;
#endif
        }
        else if (section->section_type == SECTION_TYPE_FUNC) {
            buf_func = section->section_body;
            buf_func_end = buf_func + section->section_body_size;
        }
        section = section->next;
    }

    section = sections;
    while (section) {
        buf = section->section_body;
        buf_end = buf + section->section_body_size;
        switch (section->section_type) {
            case SECTION_TYPE_USER:
                /* unsupported user section, ignore it. */
                if (!load_user_section(buf, buf_end, module,
                                       reuse_const_strings, error_buf,
                                       error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_TYPE:
                if (!load_type_section(buf, buf_end, module, error_buf,
                                       error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_IMPORT:
                if (!load_import_section(buf, buf_end, module,
                                         reuse_const_strings, no_resolve,
                                         error_buf, error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_FUNC:
                if (!load_function_section(buf, buf_end, buf_code, buf_code_end,
                                           module, error_buf, error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_TABLE:
                if (!load_table_section(buf, buf_end, module, error_buf,
                                        error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_MEMORY:
                if (!load_memory_section(buf, buf_end, module, error_buf,
                                         error_buf_size))
                    return false;
                break;
#if WASM_ENABLE_TAGS != 0
            case SECTION_TYPE_TAG:
                /* load tag declaration section */
                if (!load_tag_section(buf, buf_end, buf_code, buf_code_end,
                                      module, error_buf, error_buf_size))
                    return false;
                break;
#endif
            case SECTION_TYPE_GLOBAL:
                if (!load_global_section(buf, buf_end, module, error_buf,
                                         error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_EXPORT:
                if (!load_export_section(buf, buf_end, module,
                                         reuse_const_strings, error_buf,
                                         error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_START:
                if (!load_start_section(buf, buf_end, module, error_buf,
                                        error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_ELEM:
                if (!load_table_segment_section(buf, buf_end, module, error_buf,
                                                error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_CODE:
                if (!load_code_section(buf, buf_end, buf_func, buf_func_end,
                                       module, error_buf, error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_DATA:
                if (!load_data_segment_section(buf, buf_end, module,
#if WASM_ENABLE_BULK_MEMORY != 0
                                               has_datacount_section,
#endif
                                               clone_data_seg, error_buf,
                                               error_buf_size))
                    return false;
                break;
#if WASM_ENABLE_BULK_MEMORY != 0
            case SECTION_TYPE_DATACOUNT:
                if (!load_datacount_section(buf, buf_end, module, error_buf,
                                            error_buf_size))
                    return false;
                has_datacount_section = true;
                break;
#endif
#if WASM_ENABLE_STRINGREF != 0
            case SECTION_TYPE_STRINGREF:
                if (!load_stringref_section(buf, buf_end, module,
                                            reuse_const_strings, error_buf,
                                            error_buf_size))
                    return false;
                break;
#endif
            default:
                set_error_buf(error_buf, error_buf_size, "invalid section id");
                return false;
        }

        section = section->next;
    }

    module->aux_data_end_global_index = (uint32)-1;
    module->aux_heap_base_global_index = (uint32)-1;
    module->aux_stack_top_global_index = (uint32)-1;

    /* Resolve auxiliary data/stack/heap info and reset memory info */
    export = module->exports;
    for (i = 0; i < module->export_count; i++, export ++) {
        if (export->kind == EXPORT_KIND_GLOBAL) {
            if (!strcmp(export->name, "__heap_base")) {
                global_index = export->index - module->import_global_count;
                global = module->globals + global_index;
                if (global->type.val_type == VALUE_TYPE_I32
                    && !global->type.is_mutable
                    && global->init_expr.init_expr_type
                           == INIT_EXPR_TYPE_I32_CONST) {
                    aux_heap_base_global = global;
                    aux_heap_base = (uint64)(uint32)global->init_expr.u.i32;
                    aux_heap_base_global_index = export->index;
                    LOG_VERBOSE("Found aux __heap_base global, value: %" PRIu64,
                                aux_heap_base);
                }
            }
            else if (!strcmp(export->name, "__data_end")) {
                global_index = export->index - module->import_global_count;
                global = module->globals + global_index;
                if (global->type.val_type == VALUE_TYPE_I32
                    && !global->type.is_mutable
                    && global->init_expr.init_expr_type
                           == INIT_EXPR_TYPE_I32_CONST) {
                    aux_data_end_global = global;
                    aux_data_end = (uint64)(uint32)global->init_expr.u.i32;
                    aux_data_end_global_index = export->index;
                    LOG_VERBOSE("Found aux __data_end global, value: %" PRIu64,
                                aux_data_end);

                    aux_data_end = align_uint64(aux_data_end, 16);
                }
            }

            /* For module compiled with -pthread option, the global is:
                [0] stack_top       <-- 0
                [1] tls_pointer
                [2] tls_size
                [3] data_end        <-- 3
                [4] global_base
                [5] heap_base       <-- 5
                [6] dso_handle

                For module compiled without -pthread option:
                [0] stack_top       <-- 0
                [1] data_end        <-- 1
                [2] global_base
                [3] heap_base       <-- 3
                [4] dso_handle
            */
            if (aux_data_end_global && aux_heap_base_global
                && aux_data_end <= aux_heap_base) {
                module->aux_data_end_global_index = aux_data_end_global_index;
                module->aux_data_end = aux_data_end;
                module->aux_heap_base_global_index = aux_heap_base_global_index;
                module->aux_heap_base = aux_heap_base;

                /* Resolve aux stack top global */
                for (global_index = 0; global_index < module->global_count;
                     global_index++) {
                    global = module->globals + global_index;
                    if (global->type.is_mutable /* heap_base and data_end is
                                              not mutable */
                        && global->type.val_type == VALUE_TYPE_I32
                        && global->init_expr.init_expr_type
                               == INIT_EXPR_TYPE_I32_CONST
                        && (uint64)(uint32)global->init_expr.u.i32
                               <= aux_heap_base) {
                        aux_stack_top_global = global;
                        aux_stack_top = (uint64)(uint32)global->init_expr.u.i32;
                        module->aux_stack_top_global_index =
                            module->import_global_count + global_index;
                        module->aux_stack_bottom = aux_stack_top;
                        module->aux_stack_size =
                            aux_stack_top > aux_data_end
                                ? (uint32)(aux_stack_top - aux_data_end)
                                : (uint32)aux_stack_top;
                        LOG_VERBOSE(
                            "Found aux stack top global, value: %" PRIu64 ", "
                            "global index: %d, stack size: %d",
                            aux_stack_top, global_index,
                            module->aux_stack_size);
                        break;
                    }
                }
                if (!aux_stack_top_global) {
                    /* Auxiliary stack global isn't found, it must be unused
                       in the wasm app, as if it is used, the global must be
                       defined. Here we set it to __heap_base global and set
                       its size to 0. */
                    aux_stack_top_global = aux_heap_base_global;
                    aux_stack_top = aux_heap_base;
                    module->aux_stack_top_global_index =
                        module->aux_heap_base_global_index;
                    module->aux_stack_bottom = aux_stack_top;
                    module->aux_stack_size = 0;
                }
                break;
            }
        }
    }

    module->malloc_function = (uint32)-1;
    module->free_function = (uint32)-1;
    module->retain_function = (uint32)-1;

    /* Resolve malloc/free function exported by wasm module */
#if WASM_ENABLE_MEMORY64 != 0
    if (has_module_memory64(module))
        malloc_free_io_type = VALUE_TYPE_I64;
#endif
    export = module->exports;
    for (i = 0; i < module->export_count; i++, export ++) {
        if (export->kind == EXPORT_KIND_FUNC) {
            if (!strcmp(export->name, "malloc")
                && export->index >= module->import_function_count) {
                func_index = export->index - module->import_function_count;
                func_type = module->functions[func_index]->func_type;
                if (func_type->param_count == 1 && func_type->result_count == 1
                    && func_type->types[0] == malloc_free_io_type
                    && func_type->types[1] == malloc_free_io_type) {
                    bh_assert(module->malloc_function == (uint32)-1);
                    module->malloc_function = export->index;
                    LOG_VERBOSE("Found malloc function, name: %s, index: %u",
                                export->name, export->index);
                }
            }
            else if (!strcmp(export->name, "__new")
                     && export->index >= module->import_function_count) {
                /* __new && __pin for AssemblyScript */
                func_index = export->index - module->import_function_count;
                func_type = module->functions[func_index]->func_type;
                if (func_type->param_count == 2 && func_type->result_count == 1
                    && func_type->types[0] == malloc_free_io_type
                    && func_type->types[1] == VALUE_TYPE_I32
                    && func_type->types[2] == malloc_free_io_type) {
                    uint32 j;
                    WASMExport *export_tmp;

                    bh_assert(module->malloc_function == (uint32)-1);
                    module->malloc_function = export->index;
                    LOG_VERBOSE("Found malloc function, name: %s, index: %u",
                                export->name, export->index);

                    /* resolve retain function.
                       If not found, reset malloc function index */
                    export_tmp = module->exports;
                    for (j = 0; j < module->export_count; j++, export_tmp++) {
                        if ((export_tmp->kind == EXPORT_KIND_FUNC)
                            && (!strcmp(export_tmp->name, "__retain")
                                || (!strcmp(export_tmp->name, "__pin")))
                            && (export_tmp->index
                                >= module->import_function_count)) {
                            func_index = export_tmp->index
                                         - module->import_function_count;
                            func_type =
                                module->functions[func_index]->func_type;
                            if (func_type->param_count == 1
                                && func_type->result_count == 1
                                && func_type->types[0] == malloc_free_io_type
                                && func_type->types[1] == malloc_free_io_type) {
                                bh_assert(module->retain_function
                                          == (uint32)-1);
                                module->retain_function = export_tmp->index;
                                LOG_VERBOSE("Found retain function, name: %s, "
                                            "index: %u",
                                            export_tmp->name,
                                            export_tmp->index);
                                break;
                            }
                        }
                    }
                    if (j == module->export_count) {
                        module->malloc_function = (uint32)-1;
                        LOG_VERBOSE("Can't find retain function,"
                                    "reset malloc function index to -1");
                    }
                }
            }
            else if (((!strcmp(export->name, "free"))
                      || (!strcmp(export->name, "__release"))
                      || (!strcmp(export->name, "__unpin")))
                     && export->index >= module->import_function_count) {
                func_index = export->index - module->import_function_count;
                func_type = module->functions[func_index]->func_type;
                if (func_type->param_count == 1 && func_type->result_count == 0
                    && func_type->types[0] == malloc_free_io_type) {
                    bh_assert(module->free_function == (uint32)-1);
                    module->free_function = export->index;
                    LOG_VERBOSE("Found free function, name: %s, index: %u",
                                export->name, export->index);
                }
            }
        }
    }

#if WASM_ENABLE_FAST_INTERP != 0 && WASM_ENABLE_LABELS_AS_VALUES != 0
    handle_table = wasm_interp_get_handle_table();
#endif

    for (i = 0; i < module->function_count; i++) {
        WASMFunction *func = module->functions[i];
        if (!wasm_loader_prepare_bytecode(module, func, i, error_buf,
                                          error_buf_size)) {
            return false;
        }

        if (i == module->function_count - 1
            && func->code + func->code_size != buf_code_end) {
            set_error_buf(error_buf, error_buf_size,
                          "code section size mismatch");
            return false;
        }
    }

    if (!module->possible_memory_grow) {
        WASMMemoryImport *memory_import;
        WASMMemory *memory;

        if (aux_data_end_global && aux_heap_base_global
            && aux_stack_top_global) {
            uint64 init_memory_size;
            uint64 shrunk_memory_size = align_uint64(aux_heap_base, 8);

            /* Only resize(shrunk) the memory size if num_bytes_per_page is in
             * valid range of uint32 */
            if (shrunk_memory_size <= UINT32_MAX) {
                if (module->import_memory_count) {
                    memory_import = &module->import_memories[0].u.memory;
                    init_memory_size =
                        (uint64)memory_import->mem_type.num_bytes_per_page
                        * memory_import->mem_type.init_page_count;
                    if (shrunk_memory_size <= init_memory_size) {
                        /* Reset memory info to decrease memory usage */
                        memory_import->mem_type.num_bytes_per_page =
                            (uint32)shrunk_memory_size;
                        memory_import->mem_type.init_page_count = 1;
                        LOG_VERBOSE("Shrink import memory size to %" PRIu64,
                                    shrunk_memory_size);
                    }
                }

                if (module->memory_count) {
                    memory = &module->memories[0];
                    init_memory_size = (uint64)memory->num_bytes_per_page
                                       * memory->init_page_count;
                    if (shrunk_memory_size <= init_memory_size) {
                        /* Reset memory info to decrease memory usage */
                        memory->num_bytes_per_page = (uint32)shrunk_memory_size;
                        memory->init_page_count = 1;
                        LOG_VERBOSE("Shrink memory size to %" PRIu64,
                                    shrunk_memory_size);
                    }
                }
            }
        }

#if WASM_ENABLE_MULTI_MODULE == 0
        if (module->import_memory_count) {
            memory_import = &module->import_memories[0].u.memory;
            /* Only resize the memory to one big page if num_bytes_per_page is
             * in valid range of uint32 */
            if (memory_import->mem_type.init_page_count < DEFAULT_MAX_PAGES) {
                memory_import->mem_type.num_bytes_per_page *=
                    memory_import->mem_type.init_page_count;

                if (memory_import->mem_type.init_page_count > 0)
                    memory_import->mem_type.init_page_count =
                        memory_import->mem_type.max_page_count = 1;
                else
                    memory_import->mem_type.init_page_count =
                        memory_import->mem_type.max_page_count = 0;
            }
        }
        if (module->memory_count) {
            memory = &module->memories[0];
            /* Only resize(shrunk) the memory size if num_bytes_per_page is in
             * valid range of uint32 */
            if (memory->init_page_count < DEFAULT_MAX_PAGES) {
                memory->num_bytes_per_page *= memory->init_page_count;
                if (memory->init_page_count > 0)
                    memory->init_page_count = memory->max_page_count = 1;
                else
                    memory->init_page_count = memory->max_page_count = 0;
            }
        }
#endif
    }

#if WASM_ENABLE_MEMORY64 != 0
    if (!check_memory64_flags_consistency(module, error_buf, error_buf_size,
                                          false))
        return false;
#endif

    calculate_global_data_offset(module);

#if WASM_ENABLE_FAST_JIT != 0
    if (!init_fast_jit_functions(module, error_buf, error_buf_size)) {
        return false;
    }
#endif

#if WASM_ENABLE_JIT != 0
    if (!init_llvm_jit_functions_stage1(module, error_buf, error_buf_size)) {
        return false;
    }
#if !(WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_LAZY_JIT != 0)
    if (!init_llvm_jit_functions_stage2(module, error_buf, error_buf_size)) {
        return false;
    }
#else
    /* Run aot_compile_wasm in a backend thread, so as not to block the main
       thread fast jit execution, since applying llvm optimizations in
       aot_compile_wasm may cost a lot of time.
       Create thread with enough native stack to apply llvm optimizations */
    if (os_thread_create(&module->llvm_jit_init_thread,
                         init_llvm_jit_functions_stage2_callback,
                         (void *)module, APP_THREAD_STACK_SIZE_DEFAULT * 8)
        != 0) {
        set_error_buf(error_buf, error_buf_size,
                      "create orcjit compile thread failed");
        return false;
    }
#endif
#endif

#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0
    /* Create threads to compile the jit functions */
    if (!compile_jit_functions(module, error_buf, error_buf_size)) {
        return false;
    }
#endif

#if WASM_ENABLE_MEMORY_TRACING != 0
    wasm_runtime_dump_module_mem_consumption((WASMModuleCommon *)module);
#endif
    return true;
}

static WASMModule *
create_module(char *name, char *error_buf, uint32 error_buf_size)
{
    WASMModule *module =
        loader_malloc(sizeof(WASMModule), error_buf, error_buf_size);
    bh_list_status ret;

    if (!module) {
        return NULL;
    }

    module->module_type = Wasm_Module_Bytecode;

    /* Set start_function to -1, means no start function */
    module->start_function = (uint32)-1;

    module->name = name;
    module->is_binary_freeable = false;

#if WASM_ENABLE_FAST_INTERP == 0
    module->br_table_cache_list = &module->br_table_cache_list_head;
    ret = bh_list_init(module->br_table_cache_list);
    bh_assert(ret == BH_LIST_SUCCESS);
#endif

#if WASM_ENABLE_MULTI_MODULE != 0
    module->import_module_list = &module->import_module_list_head;
    ret = bh_list_init(module->import_module_list);
    bh_assert(ret == BH_LIST_SUCCESS);
#endif

#if WASM_ENABLE_DEBUG_INTERP != 0
    ret = bh_list_init(&module->fast_opcode_list);
    bh_assert(ret == BH_LIST_SUCCESS);
#endif

#if WASM_ENABLE_GC != 0
    if (!(module->ref_type_set =
              wasm_reftype_set_create(GC_REFTYPE_MAP_SIZE_DEFAULT))) {
        set_error_buf(error_buf, error_buf_size, "create reftype map failed");
        goto fail1;
    }

    if (os_mutex_init(&module->rtt_type_lock)) {
        set_error_buf(error_buf, error_buf_size, "init rtt type lock failed");
        goto fail2;
    }
#endif

#if WASM_ENABLE_DEBUG_INTERP != 0                         \
    || (WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT != 0 \
        && WASM_ENABLE_LAZY_JIT != 0)
    if (os_mutex_init(&module->instance_list_lock) != 0) {
        set_error_buf(error_buf, error_buf_size,
                      "init instance list lock failed");
        goto fail3;
    }
#endif

    (void)ret;
    return module;

#if WASM_ENABLE_DEBUG_INTERP != 0                    \
    || (WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT \
        && WASM_ENABLE_LAZY_JIT != 0)
fail3:
#endif
#if WASM_ENABLE_GC != 0
    os_mutex_destroy(&module->rtt_type_lock);
fail2:
    bh_hash_map_destroy(module->ref_type_set);
fail1:
#endif
    wasm_runtime_free(module);
    return NULL;
}

#if WASM_ENABLE_DEBUG_INTERP != 0
static bool
record_fast_op(WASMModule *module, uint8 *pos, uint8 orig_op, char *error_buf,
               uint32 error_buf_size)
{
    WASMFastOPCodeNode *fast_op =
        loader_malloc(sizeof(WASMFastOPCodeNode), error_buf, error_buf_size);
    if (fast_op) {
        fast_op->offset = pos - module->load_addr;
        fast_op->orig_op = orig_op;
        bh_list_insert(&module->fast_opcode_list, fast_op);
    }
    return fast_op ? true : false;
}
#endif

WASMModule *
wasm_loader_load_from_sections(WASMSection *section_list, char *error_buf,
                               uint32 error_buf_size)
{
    WASMModule *module = create_module("", error_buf, error_buf_size);
    if (!module)
        return NULL;

    if (!load_from_sections(module, section_list, false, true, false, error_buf,
                            error_buf_size)) {
        wasm_loader_unload(module);
        return NULL;
    }

    LOG_VERBOSE("Load module from sections success.\n");
    return module;
}

static void
destroy_sections(WASMSection *section_list)
{
    WASMSection *section = section_list, *next;
    while (section) {
        next = section->next;
        wasm_runtime_free(section);
        section = next;
    }
}

/* clang-format off */
static uint8 section_ids[] = {
    SECTION_TYPE_USER,
    SECTION_TYPE_TYPE,
    SECTION_TYPE_IMPORT,
    SECTION_TYPE_FUNC,
    SECTION_TYPE_TABLE,
    SECTION_TYPE_MEMORY,
#if WASM_ENABLE_TAGS != 0
    SECTION_TYPE_TAG,
#endif
#if WASM_ENABLE_STRINGREF != 0
    /* must immediately precede the global section,
       or where the global section would be */
    SECTION_TYPE_STRINGREF,
#endif
    SECTION_TYPE_GLOBAL,
    SECTION_TYPE_EXPORT,
    SECTION_TYPE_START,
    SECTION_TYPE_ELEM,
#if WASM_ENABLE_BULK_MEMORY != 0
    SECTION_TYPE_DATACOUNT,
#endif
    SECTION_TYPE_CODE,
    SECTION_TYPE_DATA
};
/* clang-format on */

static uint8
get_section_index(uint8 section_type)
{
    uint8 max_id = sizeof(section_ids) / sizeof(uint8);

    for (uint8 i = 0; i < max_id; i++) {
        if (section_type == section_ids[i])
            return i;
    }

    return (uint8)-1;
}

static bool
create_sections(const uint8 *buf, uint32 size, WASMSection **p_section_list,
                char *error_buf, uint32 error_buf_size)
{
    WASMSection *section_list_end = NULL, *section;
    const uint8 *p = buf, *p_end = buf + size;
    uint8 section_type, section_index, last_section_index = (uint8)-1;
    uint32 section_size;

    bh_assert(!*p_section_list);

    p += 8;
    while (p < p_end) {
        CHECK_BUF(p, p_end, 1);
        section_type = read_uint8(p);
        section_index = get_section_index(section_type);
        if (section_index != (uint8)-1) {
            if (section_type != SECTION_TYPE_USER) {
                /* Custom sections may be inserted at any place,
                   while other sections must occur at most once
                   and in prescribed order. */
                if (last_section_index != (uint8)-1
                    && (section_index <= last_section_index)) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unexpected content after last section or "
                                  "junk after last section");
                    return false;
                }
                last_section_index = section_index;
            }
            read_leb_uint32(p, p_end, section_size);
            CHECK_BUF1(p, p_end, section_size);

            if (!(section = loader_malloc(sizeof(WASMSection), error_buf,
                                          error_buf_size))) {
                return false;
            }

            section->section_type = section_type;
            section->section_body = (uint8 *)p;
            section->section_body_size = section_size;

            if (!section_list_end)
                *p_section_list = section_list_end = section;
            else {
                section_list_end->next = section;
                section_list_end = section;
            }

            p += section_size;
        }
        else {
            set_error_buf(error_buf, error_buf_size, "invalid section id");
            return false;
        }
    }

    return true;
fail:
    return false;
}

static void
exchange32(uint8 *p_data)
{
    uint8 value = *p_data;
    *p_data = *(p_data + 3);
    *(p_data + 3) = value;

    value = *(p_data + 1);
    *(p_data + 1) = *(p_data + 2);
    *(p_data + 2) = value;
}

static union {
    int a;
    char b;
} __ue = { .a = 1 };

#define is_little_endian() (__ue.b == 1)

static bool
load(const uint8 *buf, uint32 size, WASMModule *module,
     bool wasm_binary_freeable, bool no_resolve, char *error_buf,
     uint32 error_buf_size)
{
    const uint8 *buf_end = buf + size;
    const uint8 *p = buf, *p_end = buf_end;
    uint32 magic_number, version;
    WASMSection *section_list = NULL;

    CHECK_BUF1(p, p_end, sizeof(uint32));
    magic_number = read_uint32(p);
    if (!is_little_endian())
        exchange32((uint8 *)&magic_number);

    if (magic_number != WASM_MAGIC_NUMBER) {
        set_error_buf(error_buf, error_buf_size, "magic header not detected");
        return false;
    }

    CHECK_BUF1(p, p_end, sizeof(uint32));
    version = read_uint32(p);
    if (!is_little_endian())
        exchange32((uint8 *)&version);

    if (version != WASM_CURRENT_VERSION) {
        set_error_buf(error_buf, error_buf_size, "unknown binary version");
        return false;
    }

    module->package_version = version;

    if (!create_sections(buf, size, &section_list, error_buf, error_buf_size)
        || !load_from_sections(module, section_list, true, wasm_binary_freeable,
                               no_resolve, error_buf, error_buf_size)) {
        destroy_sections(section_list);
        return false;
    }

    destroy_sections(section_list);
    return true;
fail:
    return false;
}

#if WASM_ENABLE_LIBC_WASI != 0
/**
 * refer to
 * https://github.com/WebAssembly/WASI/blob/main/design/application-abi.md
 */
static bool
check_wasi_abi_compatibility(const WASMModule *module,
#if WASM_ENABLE_MULTI_MODULE != 0