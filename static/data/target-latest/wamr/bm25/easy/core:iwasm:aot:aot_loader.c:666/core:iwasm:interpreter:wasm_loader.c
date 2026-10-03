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
