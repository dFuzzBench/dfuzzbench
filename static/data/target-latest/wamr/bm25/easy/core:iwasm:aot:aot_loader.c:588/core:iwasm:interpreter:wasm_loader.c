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
