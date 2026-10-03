/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_emit_aot_file.h"
#include "../aot/aot_runtime.h"

#define PUT_U64_TO_ADDR(addr, value)        \
    do {                                    \
        union {                             \
            uint64 val;                     \
            uint32 parts[2];                \
        } u;                                \
        u.val = (value);                    \
        ((uint32 *)(addr))[0] = u.parts[0]; \
        ((uint32 *)(addr))[1] = u.parts[1]; \
    } while (0)

#define CHECK_SIZE(size)                                   \
    do {                                                   \
        if (size == (uint32)-1) {                          \
            aot_set_last_error("get symbol size failed."); \
            return (uint32)-1;                             \
        }                                                  \
    } while (0)

/* Internal function in object file */
typedef struct AOTObjectFunc {
    char *func_name;
    /* text offset of aot_func#n */
    uint64 text_offset;
    /* text offset of aot_func_internal#n */
    uint64 text_offset_of_aot_func_internal;
} AOTObjectFunc;

/* Symbol table list node */
typedef struct AOTSymbolNode {
    struct AOTSymbolNode *next;
    uint32 str_len;
    char *symbol;
} AOTSymbolNode;

typedef struct AOTSymbolList {
    AOTSymbolNode *head;
    AOTSymbolNode *end;
    uint32 len;
} AOTSymbolList;

/* AOT object data */
typedef struct AOTObjectData {
    AOTCompContext *comp_ctx;

    LLVMMemoryBufferRef mem_buf;
    LLVMBinaryRef binary;

    AOTTargetInfo target_info;

    void *text;
    uint32 text_size;

    void *text_unlikely;
    uint32 text_unlikely_size;

    void *text_hot;
    uint32 text_hot_size;

    /* literal data and size */
    void *literal;
    uint32 literal_size;

    AOTObjectDataSection *data_sections;
    uint32 data_sections_count;

    AOTObjectFunc *funcs;
    uint32 func_count;

    AOTSymbolList symbol_list;
    AOTRelocationGroup *relocation_groups;
    uint32 relocation_group_count;

    const char *stack_sizes_section_name;
    uint32 stack_sizes_offset;
    uint32 *stack_sizes;
} AOTObjectData;

#if 0
static void dump_buf(uint8 *buf, uint32 size, char *title)
{
    int i;
    printf("------ %s -------", title);
    for (i = 0; i < size; i++) {
        if ((i % 16) == 0)
            printf("\n");
        printf("%02x ", (unsigned char)buf[i]);
    }
    printf("\n\n");
}
#endif

static bool
is_32bit_binary(const AOTObjectData *obj_data)
{
    /* bit 1: 0 is 32-bit, 1 is 64-bit */
    return obj_data->target_info.bin_type & 2 ? false : true;
}

static bool
is_little_endian_binary(const AOTObjectData *obj_data)
{
    /* bit 0: 0 is little-endian, 1 is big-endian */
    return obj_data->target_info.bin_type & 1 ? false : true;
}

static bool
str_starts_with(const char *str, const char *prefix)
{
    size_t len_pre = strlen(prefix), len_str = strlen(str);
    return (len_str >= len_pre) && !memcmp(str, prefix, len_pre);
}

static uint32
get_file_header_size()
{
    /* magic number (4 bytes) + version (4 bytes) */
    return sizeof(uint32) + sizeof(uint32);
}

static uint32
get_string_size(AOTCompContext *comp_ctx, const char *s)
{
    /* string size (2 bytes) + string content + '\0' */
    return (uint32)sizeof(uint16) + (uint32)strlen(s) + 1;
}

static uint32
get_target_info_section_size()
{
    return sizeof(AOTTargetInfo);
}

static uint32
get_init_expr_size(const AOTCompContext *comp_ctx, const AOTCompData *comp_data,
                   InitializerExpression *expr);

static uint32
get_mem_init_data_size(AOTCompContext *comp_ctx, AOTMemInitData *mem_init_data)
{
    /* init expr type (4 bytes)
     * + init expr value (4 bytes, valid value can only be i32/get_global)
     * + byte count (4 bytes) + bytes */
    uint32 total_size =
        (uint32)(get_init_expr_size(comp_ctx, comp_ctx->comp_data,
                                    &mem_init_data->offset)
                 + sizeof(uint32) + mem_init_data->byte_count);

    /* bulk_memory enabled:
        is_passive (4 bytes) + memory_index (4 bytes)
       bulk memory disabled:
        placeholder (4 bytes) + placeholder (4 bytes)
    */
    total_size += (sizeof(uint32) + sizeof(uint32));

    return total_size;
}

static uint32
get_mem_init_data_list_size(AOTCompContext *comp_ctx,
                            AOTMemInitData **mem_init_data_list,
                            uint32 mem_init_data_count)
{
    AOTMemInitData **mem_init_data = mem_init_data_list;
    uint32 size = 0, i;

    for (i = 0; i < mem_init_data_count; i++, mem_init_data++) {
        size = align_uint(size, 4);
        size += get_mem_init_data_size(comp_ctx, *mem_init_data);
    }
    return size;
}

static uint32
get_import_memory_size(AOTCompData *comp_data)
{
    /* currently we only emit import_memory_count = 0 */
    return sizeof(uint32);
}

static uint32
get_memory_size(AOTCompData *comp_data)
{
    /* memory_count + count * (flags + num_bytes_per_page +
                               init_page_count + max_page_count) */
    return (uint32)(sizeof(uint32)
                    + comp_data->memory_count * sizeof(uint32) * 4);
}

static uint32
get_mem_info_size(AOTCompContext *comp_ctx, AOTCompData *comp_data)
{
    /* import_memory_size + memory_size
       + init_data_count + init_data_list */
    return get_import_memory_size(comp_data) + get_memory_size(comp_data)
           + (uint32)sizeof(uint32)
           + get_mem_init_data_list_size(comp_ctx,
                                         comp_data->mem_init_data_list,
                                         comp_data->mem_init_data_count);
}

static uint32
get_init_expr_size(const AOTCompContext *comp_ctx, const AOTCompData *comp_data,
                   InitializerExpression *expr)
{
    /* init_expr_type */
    uint32 size = sizeof(uint32);
#if WASM_ENABLE_GC != 0
    WASMModule *module = comp_data->wasm_module;
#endif

    /* + init value size */
    switch (expr->init_expr_type) {
        case INIT_EXPR_NONE:
            /* no init value, used in table initializer */
            break;
        case INIT_EXPR_TYPE_I32_CONST:
        case INIT_EXPR_TYPE_F32_CONST:
        case INIT_EXPR_TYPE_GET_GLOBAL:
            size += sizeof(uint32);
            break;
        case INIT_EXPR_TYPE_I64_CONST:
        case INIT_EXPR_TYPE_F64_CONST:
            size += sizeof(uint64);
            break;
        case INIT_EXPR_TYPE_V128_CONST:
            size += sizeof(uint64) * 2;
            break;
        case INIT_EXPR_TYPE_FUNCREF_CONST:
        case INIT_EXPR_TYPE_REFNULL_CONST:
            /* ref_index */
            size += sizeof(uint32);
            break;
#if WASM_ENABLE_GC != 0
        case INIT_EXPR_TYPE_I31_NEW:
            /* i32 */
            size += sizeof(uint32);
            break;
        case INIT_EXPR_TYPE_STRUCT_NEW:
        {
            uint32 i;
            WASMStructNewInitValues *struct_new_init_values =
                (WASMStructNewInitValues *)expr->u.data;

            /* type_index + field_count + fields */
            size += sizeof(uint32) + sizeof(uint32);

            bh_assert(struct_new_init_values->type_idx < module->type_count);

            for (i = 0; i < struct_new_init_values->count; i++) {
                WASMStructType *struct_type =
                    (WASMStructType *)
                        module->types[struct_new_init_values->type_idx];
                uint32 field_size;

                bh_assert(struct_type);
                bh_assert(struct_type->field_count
                          == struct_new_init_values->count);

                field_size = wasm_value_type_size_internal(
                    struct_type->fields[i].field_type, comp_ctx->pointer_size);
                if (field_size < sizeof(uint32))
                    field_size = sizeof(uint32);
                size += field_size;
            }
            break;
        }
        case INIT_EXPR_TYPE_STRUCT_NEW_DEFAULT:
            /* type_index */
            size += sizeof(uint32);
            break;
        case INIT_EXPR_TYPE_ARRAY_NEW_DEFAULT:
            /* array_elem_type + type_index + len */
            size += sizeof(uint32) * 3;
            break;
        case INIT_EXPR_TYPE_ARRAY_NEW:
        case INIT_EXPR_TYPE_ARRAY_NEW_FIXED:
        {
            WASMArrayNewInitValues *array_new_init_values =
                (WASMArrayNewInitValues *)expr->u.data;
            WASMArrayType *array_type = NULL;
            uint32 value_count;

            array_type =
                (WASMArrayType *)module->types[array_new_init_values->type_idx];

            bh_assert(array_type);
            bh_assert(array_new_init_values->type_idx < module->type_count);

            value_count =
                (expr->init_expr_type == INIT_EXPR_TYPE_ARRAY_NEW_FIXED)
                    ? array_new_init_values->length
                    : 1;

            /* array_elem_type + type_index + len + elems */
            size += sizeof(uint32) * 3
                    + wasm_value_type_size_internal(array_type->elem_type,
                                                    comp_ctx->pointer_size)
                          * value_count;
            break;
        }
#endif /* end of WASM_ENABLE_GC != 0 */
        default:
            bh_assert(0);
    }

    return size;
}

static uint32
get_table_init_data_size(AOTCompContext *comp_ctx,
                         AOTTableInitData *table_init_data)
{
    uint32 size, i;

    /*
     * mode (4 bytes), elem_type (4 bytes)
     *
     * table_index(4 bytes) + init expr type (4 bytes) + init expr value (8
     * bytes)
     */
    size = (uint32)(sizeof(uint32) * 2 + sizeof(uint32) + sizeof(uint32)
                    + sizeof(uint64))
           /* Size of WasmRefType - inner padding (ref type + nullable +
              heap_type) */
           + 8;

    /* + value count/func index count (4 bytes) + init_values */
    size += sizeof(uint32);
    for (i = 0; i < table_init_data->value_count; i++) {
        size += get_init_expr_size(comp_ctx, comp_ctx->comp_data,
                                   &table_init_data->init_values[i]);
    }

    return size;
}

static uint32
get_table_init_data_list_size(AOTCompContext *comp_ctx,
                              AOTTableInitData **table_init_data_list,
                              uint32 table_init_data_count)
{
    /*
     * ------------------------------
     * | table_init_data_count
     * ------------------------------
     * |                     | U32 mode
     * | AOTTableInitData[N] | U32 elem_type
     * |                     | U32 table_index
     * |                     | U32 offset.init_expr_type
     * |                     | U64 offset.u.i64
     * |                     | U32 func_index_count / elem_count
     * |                     | UINTPTR [func_index_count] / [elem_count]
     * ------------------------------
     */
    AOTTableInitData **table_init_data = table_init_data_list;
    uint32 size = 0, i;

    /* table_init_data_count(4 bytes) */
    size = (uint32)sizeof(uint32);

    for (i = 0; i < table_init_data_count; i++, table_init_data++) {
        size = align_uint(size, 4);
        size += get_table_init_data_size(comp_ctx, *table_init_data);
    }
    return size;
}

static uint32
get_import_table_size(const AOTCompContext *comp_ctx,
                      const AOTCompData *comp_data)
{
    /*
     * ------------------------------
     * | import_table_count
     * ------------------------------
     * |                   | U8 elem_type
     * |                   | U8 flags
     * |                   | U8 possible_grow
     * | AOTImportTable[N] | U8 elem_ref_type.nullable (for GC only)
     * |                   | U32 init_size
     * |                   | U32 max_size
     * |                   | U32 elem_ref_type.heap_type (for GC only)
     * ------------------------------
     */
    uint32 size = 0, i;

    size = (uint32)sizeof(uint32);
    for (i = 0; i < comp_data->import_table_count; i++) {
        size += sizeof(uint32) * 3;
#if WASM_ENABLE_GC != 0
        if (comp_ctx->enable_gc
            && comp_data->import_tables[i].table_type.elem_ref_type)
            size += sizeof(uint32);
#endif
    }
    return size;
}

static uint32
get_table_size(const AOTCompContext *comp_ctx, const AOTCompData *comp_data)
{
    /*
     * ------------------------------
     * | table_count
     * ------------------------------
     * |             | U8 elem_type
     * |             | U8 flags
     * |             | U8 possible_grow
     * | AOTTable[N] | U8 elem_ref_type.nullable (for GC only)
     * |             | U32 init_size
     * |             | U32 max_size
     * |             | U32 elem_ref_type.heap_type (for GC only)
     * |             | N   init_expr (for GC only)
     * ------------------------------
     */
    uint32 size = 0, i;

    size = (uint32)sizeof(uint32);
    for (i = 0; i < comp_data->table_count; i++) {
        size += sizeof(uint32) * 3;
#if WASM_ENABLE_GC != 0
        if (comp_ctx->enable_gc) {
            if (comp_data->tables[i].table_type.elem_ref_type) {
                size += sizeof(uint32);
            }
            size += get_init_expr_size(comp_ctx, comp_data,
                                       &comp_data->tables[i].init_expr);
        }
#endif
    }
    return size;
}

static uint32
get_table_info_size(AOTCompContext *comp_ctx, AOTCompData *comp_data)
{
    /*
     * ------------------------------
     * | import_table_count
     * ------------------------------
     * |
     * | AOTImportTable[import_table_count]
     * |
     * ------------------------------
     * | table_count
     * ------------------------------
     * |
     * | AOTTable[table_count]
     * |
     * ------------------------------
     * | table_init_data_count
     * ------------------------------
     * |
     * | AOTTableInitData*[table_init_data_count]
     * |
     * ------------------------------
     */
    return get_import_table_size(comp_ctx, comp_data)
           + get_table_size(comp_ctx, comp_data)
           + get_table_init_data_list_size(comp_ctx,
                                           comp_data->table_init_data_list,
                                           comp_data->table_init_data_count);
}

static uint32
get_func_type_size(AOTCompContext *comp_ctx, AOTFuncType *func_type)
{
#if WASM_ENABLE_GC != 0
    /* type flag + equivalence type flag + is_sub_final + parent_type_idx
       + rec_count + rec_idx + param count + result count
       + ref_type_map_count + types + context of ref_type_map */
    if (comp_ctx->enable_gc) {
        uint32 size = 0;

        /* type flag */
        size += sizeof(func_type->base_type.type_flag);
        /* equivalence type flag + is_sub_final */
        size += sizeof(uint16);
        /* parent_type_idx */
        size += sizeof(func_type->base_type.parent_type_idx);
        /* rec_count */
        size += sizeof(func_type->base_type.rec_count);
        /* rec_idx */
        size += sizeof(func_type->base_type.rec_idx);
        /* param count */
        size += sizeof(func_type->param_count);
        /* result count */
        size += sizeof(func_type->result_count);
        /* ref_type_map_count */
        size += sizeof(func_type->ref_type_map_count);
        /* param and result types */
        size += func_type->param_count + func_type->result_count;
        /* align size */
        size = align_uint(size, 4);
        /* ref_type_map */
        size += func_type->ref_type_map_count * 8;

        return size;
    }
    else
#endif
    {
        /* type flag + param count + result count + types */
        return (uint32)sizeof(uint16) * 3 + func_type->param_count
               + func_type->result_count;
    }
}

#if WASM_ENABLE_GC != 0
static uint32
get_struct_type_size(AOTCompContext *comp_ctx, AOTStructType *struct_type)
{
    uint32 size = 0;
    /* type flag + equivalence type flag + is_sub_final + parent_type_idx
       + rec_count + rec_idx + field count + fields */

    /* type flag */
    size += sizeof(struct_type->base_type.type_flag);
    /* equivalence type flag + is_sub_final */
    size += sizeof(uint16);
    /* parent_type_idx */
    size += sizeof(struct_type->base_type.parent_type_idx);
    /* rec_count */
    size += sizeof(struct_type->base_type.rec_count);
    /* rec_idx */
    size += sizeof(struct_type->base_type.rec_idx);
    /* field count */
    size += sizeof(struct_type->field_count);
    /* field types */
    size += struct_type->field_count * 2;
    /* ref_type_map_count */
    size += sizeof(struct_type->ref_type_map_count);
    size = align_uint(size, 4);
    /* ref_type_map */
    size += struct_type->ref_type_map_count * 8;
    return size;
}

static uint32
get_array_type_size(AOTCompContext *comp_ctx, AOTArrayType *array_type)
{
    uint32 size = 0;
    /* type flag + equivalence type flag + is_sub_final + parent_type_idx
       + rec_count + rec_idx + elem_flags + elem_type + elem_ref_type */

    /* type flag */
    size += sizeof(array_type->base_type.type_flag);
    /* equivalence type flag + is_sub_final */
    size += sizeof(uint16);
    /* parent_type_idx (u32) */
    size += sizeof(array_type->base_type.parent_type_idx);
    /* rec_count */
    size += sizeof(array_type->base_type.rec_count);
    /* rec_idx */
    size += sizeof(array_type->base_type.rec_idx);
    /* elem_flags (u16) */
    size += sizeof(array_type->elem_flags);
    /* elem_type (u8) */
    size += sizeof(array_type->elem_type);
    /* elem_ref_type */
    if (array_type->elem_ref_type) {
        /* nullable (u8) */
        size += sizeof(uint8);
        /* heap type (u32) */
        size += sizeof(uint32);
    }

    return size;
}
#endif

static uint32
get_type_info_size(AOTCompContext *comp_ctx, AOTCompData *comp_data)
{
    /* Initial size with size of type count */
    uint32 size = 4;
    uint32 i;

#if WASM_ENABLE_GC != 0
    if (comp_ctx->enable_gc) {
        for (i = 0; i < comp_data->type_count; i++) {
            uint32 j;

            size = align_uint(size, 4);

            /* Emit simple info if there is an equivalence type */
            for (j = 0; j < i; j++) {
                if (comp_data->types[j] == comp_data->types[i]) {
                    /* type_flag (2 bytes) + equivalence type flag (1 byte)
                       + padding (1 byte) + equivalence type index */
                    size += 8;
                    break;
                }
            }
            if (j < i)
                continue;

            if (comp_data->types[i]->type_flag == WASM_TYPE_FUNC)
                size += get_func_type_size(comp_ctx,
                                           (AOTFuncType *)comp_data->types[i]);
            else if (comp_data->types[i]->type_flag == WASM_TYPE_STRUCT)
                size += get_struct_type_size(
                    comp_ctx, (AOTStructType *)comp_data->types[i]);
            else if (comp_data->types[i]->type_flag == WASM_TYPE_ARRAY)
                size += get_array_type_size(
                    comp_ctx, (AOTArrayType *)comp_data->types[i]);
            else
                bh_assert(0);
        }
    }
    else
#endif
    {
        for (i = 0; i < comp_data->type_count; i++) {
            size = align_uint(size, 4);
            size += get_func_type_size(comp_ctx,
                                       (AOTFuncType *)comp_data->types[i]);
        }
    }

    return size;
}

static uint32
get_import_global_size(AOTCompContext *comp_ctx, AOTImportGlobal *import_global)
{
    /* type (1 byte) + is_mutable (1 byte) + module_name + global_name */
    uint32 size = (uint32)sizeof(uint8) * 2
                  + get_string_size(comp_ctx, import_global->module_name);
    size = align_uint(size, 2);
    size += get_string_size(comp_ctx, import_global->global_name);
    return size;
}

static uint32
get_import_globals_size(AOTCompContext *comp_ctx,
                        AOTImportGlobal *import_globals,
                        uint32 import_global_count)
{
    AOTImportGlobal *import_global = import_globals;
    uint32 size = 0, i;

    for (i = 0; i < import_global_count; i++, import_global++) {
        size = align_uint(size, 2);
        size += get_import_global_size(comp_ctx, import_global);
    }
    return size;
}

static uint32
get_import_global_info_size(AOTCompContext *comp_ctx, AOTCompData *comp_data)
{
    /* import global count + import globals */
    return (uint32)sizeof(uint32)
           + get_import_globals_size(comp_ctx, comp_data->import_globals,
                                     comp_data->import_global_count);
}

static uint32
get_global_size(AOTCompContext *comp_ctx, AOTGlobal *global)
{
    /* type (1 byte) + is_mutable (1 byte) + padding (2 bytes)
            + init expr value (include init expr type) */
    return sizeof(uint8) * 2 + sizeof(uint8) * 2
           + get_init_expr_size(comp_ctx, comp_ctx->comp_data,
                                &global->init_expr);
}

static uint32
get_globals_size(AOTCompContext *comp_ctx, AOTGlobal *globals,
                 uint32 global_count)
{
    AOTGlobal *global = globals;
    uint32 size = 0, i;

    for (i = 0; i < global_count; i++, global++) {
        size = align_uint(size, 4);
        size += get_global_size(comp_ctx, global);
    }
    return size;
}

static uint32
get_global_info_size(AOTCompContext *comp_ctx, AOTCompData *comp_data)
{
    /* global count + globals */
    return (uint32)sizeof(uint32)
           + get_globals_size(comp_ctx, comp_data->globals,
                              comp_data->global_count);
}

static uint32
get_import_func_size(AOTCompContext *comp_ctx, AOTImportFunc *import_func)
{
    /* type index (2 bytes) + module_name + func_name */
    uint32 size = (uint32)sizeof(uint16)
                  + get_string_size(comp_ctx, import_func->module_name);
    size = align_uint(size, 2);
    size += get_string_size(comp_ctx, import_func->func_name);
    return size;
}

static uint32
get_import_funcs_size(AOTCompContext *comp_ctx, AOTImportFunc *import_funcs,
                      uint32 import_func_count)
{
    AOTImportFunc *import_func = import_funcs;
    uint32 size = 0, i;

    for (i = 0; i < import_func_count; i++, import_func++) {
        size = align_uint(size, 2);
        size += get_import_func_size(comp_ctx, import_func);
    }
    return size;
}

static uint32
get_import_func_info_size(AOTCompContext *comp_ctx, AOTCompData *comp_data)
{
    /* import func count + import funcs */
    return (uint32)sizeof(uint32)
           + get_import_funcs_size(comp_ctx, comp_data->import_funcs,
                                   comp_data->import_func_count);
}

static uint32
get_object_data_sections_size(AOTCompContext *comp_ctx,
                              AOTObjectDataSection *data_sections,
                              uint32 data_sections_count)
{
    AOTObjectDataSection *data_section = data_sections;
    uint32 size = 0, i;

    for (i = 0; i < data_sections_count; i++, data_section++) {
        /* name + size + data */
        size = align_uint(size, 2);
        size += get_string_size(comp_ctx, data_section->name);
        size = align_uint(size, 4);
        size += (uint32)sizeof(uint32);
        size += data_section->size;
    }
    return size;
}

static uint32
get_object_data_section_info_size(AOTCompContext *comp_ctx,
                                  AOTObjectData *obj_data)
{
    /* data sections count + data sections */
    return (uint32)sizeof(uint32)
           + get_object_data_sections_size(comp_ctx, obj_data->data_sections,
                                           obj_data->data_sections_count);
}

static uint32
get_init_data_section_size(AOTCompContext *comp_ctx, AOTCompData *comp_data,
                           AOTObjectData *obj_data)
{
    uint32 size = 0;

    size += get_mem_info_size(comp_ctx, comp_data);

    size = align_uint(size, 4);
    size += get_table_info_size(comp_ctx, comp_data);

    size = align_uint(size, 4);
    size += get_type_info_size(comp_ctx, comp_data);

    size = align_uint(size, 4);
    size += get_import_global_info_size(comp_ctx, comp_data);

    size = align_uint(size, 4);
    size += get_global_info_size(comp_ctx, comp_data);

    size = align_uint(size, 4);
    size += get_import_func_info_size(comp_ctx, comp_data);

    /* func count + start func index */
    size = align_uint(size, 4);
    size += (uint32)sizeof(uint32) * 2;

    /* aux data/heap/stack data */
    size += sizeof(uint32) * 10;

    size += get_object_data_section_info_size(comp_ctx, obj_data);
    return size;
}

static uint32
get_text_section_size(AOTObjectData *obj_data)
{
    return sizeof(uint32) + align_uint(obj_data->literal_size, 4)
           + align_uint(obj_data->text_size, 4)
           + align_uint(obj_data->text_unlikely_size, 4)
           + align_uint(obj_data->text_hot_size, 4);
}

static uint32
get_func_section_size(AOTCompContext *comp_ctx, AOTCompData *comp_data,
                      AOTObjectData *obj_data)
{
    uint32 size = 0;

    /* text offsets */
    if (is_32bit_binary(obj_data))
        size = (uint32)sizeof(uint32) * comp_data->func_count;
    else
        size = (uint32)sizeof(uint64) * comp_data->func_count;

    /* function type indexes */
    size += (uint32)sizeof(uint32) * comp_data->func_count;

    /* max_local_cell_nums */
    size += (uint32)sizeof(uint32) * comp_data->func_count;

    /* max_stack_cell_nums */
    size += (uint32)sizeof(uint32) * comp_data->func_count;

#if WASM_ENABLE_GC != 0
    /* func_local_ref_flags */
    if (comp_ctx->enable_gc) {
        AOTFuncType *func_type;
        uint32 i, j, local_ref_flags_cell_num;

        for (i = 0; i < comp_data->import_func_count; i++) {
            func_type = comp_data->import_funcs[i].func_type;
            /* recalculate cell_num based on target pointer size */
            local_ref_flags_cell_num = 0;
            for (j = 0; j < func_type->param_count; j++) {
                local_ref_flags_cell_num += wasm_value_type_cell_num_internal(
                    func_type->types[j], comp_ctx->pointer_size);
            }
            local_ref_flags_cell_num =
                local_ref_flags_cell_num > 2 ? local_ref_flags_cell_num : 2;

            size = align_uint(size, 4);
            size += (uint32)sizeof(uint32);
            size += (uint32)sizeof(uint8) * local_ref_flags_cell_num;
        }

        for (i = 0; i < comp_data->func_count; i++) {
            func_type = comp_data->funcs[i]->func_type;
            local_ref_flags_cell_num = comp_data->funcs[i]->param_cell_num
                                       + comp_data->funcs[i]->local_cell_num;

            size = align_uint(size, 4);
            size += (uint32)sizeof(uint32);
            size += (uint32)sizeof(uint8) * local_ref_flags_cell_num;
        }
    }
#endif

    return size;
}

static uint32
get_export_size(AOTCompContext *comp_ctx, AOTExport *export)
{
    /* export index + export kind + 1 byte padding + export name */
    return (uint32)sizeof(uint32) + sizeof(uint8) + 1
           + get_string_size(comp_ctx, export->name);
}

static uint32
get_exports_size(AOTCompContext *comp_ctx, AOTExport *exports,
                 uint32 export_count)
{
    AOTExport *export = exports;
    uint32 size = 0, i;

    for (i = 0; i < export_count; i++, export ++) {
        size = align_uint(size, 4);
        size += get_export_size(comp_ctx, export);
    }
    return size;
}

static uint32
get_export_section_size(AOTCompContext *comp_ctx, AOTCompData *comp_data)
{
    /* export count + exports */
    return (uint32)sizeof(uint32)
           + get_exports_size(comp_ctx, comp_data->wasm_module->exports,
                              comp_data->wasm_module->export_count);
}

static uint32
get_relocation_size(AOTRelocation *relocation, bool is_32bin)
{
    /* offset + addend + relocation type + symbol name */
    uint32 size = 0;
    if (is_32bin)
        size = sizeof(uint32) * 2; /* offset and addend */
    else
        size = sizeof(uint64) * 2;  /* offset and addend */
    size += (uint32)sizeof(uint32); /* relocation type */
    size += (uint32)sizeof(uint32); /* symbol name index */
    return size;
}

static uint32
get_relocations_size(AOTObjectData *obj_data,
                     AOTRelocationGroup *relocation_group,
                     AOTRelocation *relocations, uint32 relocation_count,
                     bool is_32bin)
{
    AOTRelocation *relocation = relocations;
    uint32 size = 0, i;

    for (i = 0; i < relocation_count; i++, relocation++) {
        /* ignore the relocations to aot_func_internal#n in text section
           for windows platform since they will be applied in
           aot_emit_text_section */
        if ((!strcmp(relocation_group->section_name, ".text")
             || !strcmp(relocation_group->section_name, ".ltext"))
            && !strncmp(relocation->symbol_name, AOT_FUNC_INTERNAL_PREFIX,
                        strlen(AOT_FUNC_INTERNAL_PREFIX))
            && ((!strncmp(obj_data->comp_ctx->target_arch, "x86_64", 6)
                 /* Windows AOT_COFF64_BIN_TYPE */
                 && obj_data->target_info.bin_type == 6
                 /* IMAGE_REL_AMD64_REL32 in windows x86_64 */
                 && relocation->relocation_type == 4)
                || (!strncmp(obj_data->comp_ctx->target_arch, "i386", 4)
                    /* Windows AOT_COFF32_BIN_TYPE */
                    && obj_data->target_info.bin_type == 4
                    /* IMAGE_REL_I386_REL32 in windows x86_32 */
                    && relocation->relocation_type == 20))) {
            continue;
        }
        size = align_uint(size, 4);
        size += get_relocation_size(relocation, is_32bin);
    }
    return size;
}

static uint32
get_relocation_group_size(AOTObjectData *obj_data,
                          AOTRelocationGroup *relocation_group, bool is_32bin)
{
    uint32 size = 0;
    /* section name index + relocation count + relocations */
    size += (uint32)sizeof(uint32);
    size += (uint32)sizeof(uint32);
    size += get_relocations_size(obj_data, relocation_group,
                                 relocation_group->relocations,
                                 relocation_group->relocation_count, is_32bin);
    return size;
}

static uint32
get_relocation_groups_size(AOTObjectData *obj_data,
                           AOTRelocationGroup *relocation_groups,
                           uint32 relocation_group_count, bool is_32bin)
{
    AOTRelocationGroup *relocation_group = relocation_groups;
    uint32 size = 0, i;

    for (i = 0; i < relocation_group_count; i++, relocation_group++) {
        size = align_uint(size, 4);
        size += get_relocation_group_size(obj_data, relocation_group, is_32bin);
    }
    return size;
}

/* return the index (in order of insertion) of the symbol,
   create if not exits, -1 if failed */
static uint32
get_relocation_symbol_index(const char *symbol_name, bool *is_new,
                            AOTSymbolList *symbol_list)
{
    AOTSymbolNode *sym;
    uint32 index = 0;

    sym = symbol_list->head;
    while (sym) {
        if (!strcmp(sym->symbol, symbol_name)) {
            if (is_new)
                *is_new = false;
            return index;
        }

        sym = sym->next;
        index++;
    }

    /* Not found in symbol_list, add it */
    sym = wasm_runtime_malloc(sizeof(AOTSymbolNode));
    if (!sym) {
        return (uint32)-1;
    }

    memset(sym, 0, sizeof(AOTSymbolNode));
    sym->symbol = (char *)symbol_name;
    sym->str_len = (uint32)strlen(symbol_name);

    if (!symbol_list->head) {
        symbol_list->head = symbol_list->end = sym;
    }
    else {
        symbol_list->end->next = sym;
        symbol_list->end = sym;
    }
    symbol_list->len++;

    if (is_new)
        *is_new = true;
    return index;
}

static uint32
get_relocation_symbol_size(AOTCompContext *comp_ctx, AOTRelocation *relocation,
                           AOTSymbolList *symbol_list)
{
    uint32 size = 0, index = 0;
    bool is_new = false;

    index = get_relocation_symbol_index(relocation->symbol_name, &is_new,
                                        symbol_list);
    CHECK_SIZE(index);

    if (is_new) {
        size += get_string_size(comp_ctx, relocation->symbol_name);
        size = align_uint(size, 2);
    }

    relocation->symbol_index = index;
    return size;
}

static uint32
get_relocations_symbol_size(AOTCompContext *comp_ctx,
                            AOTRelocation *relocations, uint32 relocation_count,
                            AOTSymbolList *symbol_list)
{
    AOTRelocation *relocation = relocations;
    uint32 size = 0, curr_size, i;

    for (i = 0; i < relocation_count; i++, relocation++) {
        curr_size =
            get_relocation_symbol_size(comp_ctx, relocation, symbol_list);
        CHECK_SIZE(curr_size);

        size += curr_size;
    }
    return size;
}

static uint32
get_relocation_group_symbol_size(AOTCompContext *comp_ctx,
                                 AOTRelocationGroup *relocation_group,
                                 AOTSymbolList *symbol_list)
{
    uint32 size = 0, index = 0, curr_size;
    bool is_new = false;

    index = get_relocation_symbol_index(relocation_group->section_name, &is_new,
                                        symbol_list);
    CHECK_SIZE(index);

    if (is_new) {
        size += get_string_size(comp_ctx, relocation_group->section_name);
        size = align_uint(size, 2);
    }

    relocation_group->name_index = index;

    curr_size = get_relocations_symbol_size(
        comp_ctx, relocation_group->relocations,
        relocation_group->relocation_count, symbol_list);
    CHECK_SIZE(curr_size);
    size += curr_size;

    return size;
}

static uint32
get_relocation_groups_symbol_size(AOTCompContext *comp_ctx,
                                  AOTRelocationGroup *relocation_groups,
                                  uint32 relocation_group_count,
                                  AOTSymbolList *symbol_list)
{
    AOTRelocationGroup *relocation_group = relocation_groups;
    uint32 size = 0, curr_size, i;

    for (i = 0; i < relocation_group_count; i++, relocation_group++) {
        curr_size = get_relocation_group_symbol_size(comp_ctx, relocation_group,
                                                     symbol_list);
        CHECK_SIZE(curr_size);
        size += curr_size;
    }
    return size;
}

static uint32
get_symbol_size_from_symbol_list(AOTCompContext *comp_ctx,
                                 AOTSymbolList *symbol_list)
{
    AOTSymbolNode *sym;
    uint32 size = 0;

    sym = symbol_list->head;
    while (sym) {
        /* (uint16)str_len + str */
        size += get_string_size(comp_ctx, sym->symbol);
        size = align_uint(size, 2);
        sym = sym->next;
    }

    return size;
}

static uint32
get_relocation_section_symbol_size(AOTCompContext *comp_ctx,
                                   AOTObjectData *obj_data)
{
    AOTRelocationGroup *relocation_groups = obj_data->relocation_groups;
    uint32 relocation_group_count = obj_data->relocation_group_count;
    uint32 string_count = 0, symbol_table_size = 0;

    /* section size will be calculated twice,
       get symbol size from symbol list directly in the second calculation */
    if (obj_data->symbol_list.len > 0) {
        symbol_table_size =
            get_symbol_size_from_symbol_list(comp_ctx, &obj_data->symbol_list);
    }
    else {
        symbol_table_size = get_relocation_groups_symbol_size(
            comp_ctx, relocation_groups, relocation_group_count,
            &obj_data->symbol_list);
    }
    CHECK_SIZE(symbol_table_size);
    string_count = obj_data->symbol_list.len;

    /* string_count + string_offsets + total_string_len
       + [str (string_len + str)] */
    return (uint32)(sizeof(uint32) + sizeof(uint32) * string_count
                    + sizeof(uint32) + symbol_table_size);
}

static uint32
get_relocation_section_size(AOTCompContext *comp_ctx, AOTObjectData *obj_data)
{
    AOTRelocationGroup *relocation_groups = obj_data->relocation_groups;
    uint32 relocation_group_count = obj_data->relocation_group_count;
    uint32 symbol_table_size = 0;

    symbol_table_size = get_relocation_section_symbol_size(comp_ctx, obj_data);
    CHECK_SIZE(symbol_table_size);
    symbol_table_size = align_uint(symbol_table_size, 4);

    /* relocation group count + symbol_table + relocation groups */
    return (uint32)sizeof(uint32) + symbol_table_size
           + get_relocation_groups_size(obj_data, relocation_groups,
                                        relocation_group_count,
                                        is_32bit_binary(obj_data));
}

static uint32
get_native_symbol_list_size(AOTCompContext *comp_ctx)
{
    uint32 len = 0;
    AOTNativeSymbol *sym = NULL;

    sym = bh_list_first_elem(&comp_ctx->native_symbols);

    while (sym) {
        len = align_uint(len, 2);
        len += get_string_size(comp_ctx, sym->symbol);
        sym = bh_list_elem_next(sym);
    }

    return len;
}

#if WASM_ENABLE_STRINGREF != 0
static uint32
get_string_literal_section_size(AOTCompContext *comp_ctx,
                                AOTCompData *comp_data);
#endif

static uint32
get_custom_sections_size(AOTCompContext *comp_ctx, AOTCompData *comp_data);

uint32
aot_get_aot_file_size(AOTCompContext *comp_ctx, AOTCompData *comp_data,
                      AOTObjectData *obj_data)
{
    uint32 size = 0;
    uint32 size_custom_section = 0;
#if WASM_ENABLE_STRINGREF != 0
    uint32 size_string_literal_section = 0;
#endif

    /* aot file header */
    size += get_file_header_size();

    /* target info section */
    size = align_uint(size, 4);
    /* section id + section size */
    size += (uint32)sizeof(uint32) * 2;
    size += get_target_info_section_size();

    /* init data section */
    size = align_uint(size, 4);
    /* section id + section size */
    size += (uint32)sizeof(uint32) * 2;
    size += get_init_data_section_size(comp_ctx, comp_data, obj_data);

    /* text section */
    size = align_uint(size, 4);
    /* section id + section size */
    size += (uint32)sizeof(uint32) * 2;
    size += get_text_section_size(obj_data);

    /* function section */
    size = align_uint(size, 4);
    /* section id + section size */
    size += (uint32)sizeof(uint32) * 2;
    size += get_func_section_size(comp_ctx, comp_data, obj_data);

    /* export section */
    size = align_uint(size, 4);
    /* section id + section size */
    size += (uint32)sizeof(uint32) * 2;
    size += get_export_section_size(comp_ctx, comp_data);

    /* relocation section */
    size = align_uint(size, 4);
    /* section id + section size */
    size += (uint32)sizeof(uint32) * 2;
    size += get_relocation_section_size(comp_ctx, obj_data);

    if (get_native_symbol_list_size(comp_ctx) > 0) {
        /* emit only when there are native symbols */
        size = align_uint(size, 4);
        /* section id + section size + sub section id + symbol count */
        size += (uint32)sizeof(uint32) * 4;
        size += get_native_symbol_list_size(comp_ctx);
    }

    size_custom_section = get_custom_sections_size(comp_ctx, comp_data);
    if (size_custom_section > 0) {
        size = align_uint(size, 4);
        size += size_custom_section;
    }

#if WASM_ENABLE_STRINGREF != 0
    /* string literal section */
    size_string_literal_section =
        get_string_literal_section_size(comp_ctx, comp_data);
    if (size_string_literal_section > 0) {
        size = align_uint(size, 4);
        /* section id + section size + sub section id */
        size += (uint32)sizeof(uint32) * 3;
        size += size_string_literal_section;
    }
#endif

    return size;
}

#define exchange_uint8(p_data) (void)0

static void
exchange_uint16(uint8 *p_data)
{
    uint8 value = *p_data;
    *p_data = *(p_data + 1);
    *(p_data + 1) = value;
}

static void
exchange_uint32(uint8 *p_data)
{
    uint8 value = *p_data;
    *p_data = *(p_data + 3);
    *(p_data + 3) = value;

    value = *(p_data + 1);
    *(p_data + 1) = *(p_data + 2);
    *(p_data + 2) = value;
}

static void
exchange_uint64(uint8 *p_data)
{
    uint32 value;

    value = *(uint32 *)p_data;
    *(uint32 *)p_data = *(uint32 *)(p_data + 4);
    *(uint32 *)(p_data + 4) = value;
    exchange_uint32(p_data);
    exchange_uint32(p_data + 4);
}

static void
exchange_uint128(uint8 *p_data)
{
    /* swap high 64bit and low 64bit */
    uint64 value = *(uint64 *)p_data;
    *(uint64 *)p_data = *(uint64 *)(p_data + 8);
    *(uint64 *)(p_data + 8) = value;
    /* exchange high 64bit */
    exchange_uint64(p_data);
    /* exchange low 64bit */
    exchange_uint64(p_data + 8);
}

static union {
    int a;
    char b;
} __ue = { .a = 1 };

#define is_little_endian() (__ue.b == 1)

#define CHECK_BUF(length)                       \
    do {                                        \
        if (buf + offset + length > buf_end) {  \
            aot_set_last_error("buf overflow"); \
            return false;                       \
        }                                       \
    } while (0)

#define EMIT_U8(v)                           \
    do {                                     \
        CHECK_BUF(1);                        \
        *(uint8 *)(buf + offset) = (uint8)v; \
        offset++;                            \
    } while (0)

#define EMIT_U16(v)                       \
    do {                                  \
        uint16 t = (uint16)v;             \
        CHECK_BUF(2);                     \
        if (!is_little_endian())          \
            exchange_uint16((uint8 *)&t); \
        *(uint16 *)(buf + offset) = t;    \
        offset += (uint32)sizeof(uint16); \
    } while (0)

#define EMIT_U32(v)                       \
    do {                                  \
        uint32 t = (uint32)v;             \
        CHECK_BUF(4);                     \
        if (!is_little_endian())          \
            exchange_uint32((uint8 *)&t); \
        *(uint32 *)(buf + offset) = t;    \
        offset += (uint32)sizeof(uint32); \
    } while (0)

#define EMIT_U64(v)                       \
    do {                                  \
        uint64 t = (uint64)v;             \
        CHECK_BUF(8);                     \
        if (!is_little_endian())          \
            exchange_uint64((uint8 *)&t); \
        PUT_U64_TO_ADDR(buf + offset, t); \
        offset += (uint32)sizeof(uint64); \
    } while (0)

#define EMIT_V128(v)                         \
    do {                                     \
        uint64 *t = (uint64 *)v.i64x2;       \
        CHECK_BUF(16);                       \
        if (!is_little_endian())             \
            exchange_uint128((uint8 *)t);    \
        PUT_U64_TO_ADDR(buf + offset, t[0]); \
        offset += (uint32)sizeof(uint64);    \
        PUT_U64_TO_ADDR(buf + offset, t[1]); \
        offset += (uint32)sizeof(uint64);    \
    } while (0)

#define EMIT_BUF(v, len)              \
    do {                              \
        CHECK_BUF(len);               \
        memcpy(buf + offset, v, len); \
        offset += len;                \
    } while (0)

/* Emit string with '\0'
 */
#define EMIT_STR(s)                                   \
    do {                                              \
        uint32 str_len = (uint32)strlen(s) + 1;       \
        if (str_len > INT16_MAX) {                    \
            aot_set_last_error("emit string failed: " \
                               "string too long");    \
            return false;                             \
        }                                             \
        EMIT_U16(str_len);                            \
        EMIT_BUF(s, str_len);                         \
    } while (0)

#if WASM_ENABLE_LOAD_CUSTOM_SECTION != 0
static bool
read_leb(uint8 **p_buf, const uint8 *buf_end, uint32 maxbits, bool sign,
         uint64 *p_result)
{
    const uint8 *buf = *p_buf;
    uint64 result = 0;
    uint32 shift = 0;
    uint32 offset = 0, bcnt = 0;
    uint64 byte;

    while (true) {
        /* uN or SN must not exceed ceil(N/7) bytes */
        if (bcnt + 1 > (maxbits + 6) / 7) {
            aot_set_last_error("integer representation too long");
            return false;
        }

        if (buf + offset + 1 > buf_end) {
            aot_set_last_error("unexpected end of section or function");
            return false;
        }
        byte = buf[offset];
        offset += 1;
        result |= ((byte & 0x7f) << shift);
        shift += 7;
        bcnt += 1;
        if ((byte & 0x80) == 0) {
            break;
        }
    }

    if (!sign && maxbits == 32 && shift >= maxbits) {
        /* The top bits set represent values > 32 bits */
        if (((uint8)byte) & 0xf0)
            goto fail_integer_too_large;
    }
    else if (sign && maxbits == 32) {
        if (shift < maxbits) {
            /* Sign extend, second highest bit is the sign bit */
            if ((uint8)byte & 0x40)
                result |= (~((uint64)0)) << shift;
        }
        else {
            /* The top bits should be a sign-extension of the sign bit */
            bool sign_bit_set = ((uint8)byte) & 0x8;
            int top_bits = ((uint8)byte) & 0xf0;
            if ((sign_bit_set && top_bits != 0x70)
                || (!sign_bit_set && top_bits != 0))
                goto fail_integer_too_large;
        }
    }
    else if (sign && maxbits == 64) {
        if (shift < maxbits) {
            /* Sign extend, second highest bit is the sign bit */
            if ((uint8)byte & 0x40)
                result |= (~((uint64)0)) << shift;
        }
        else {
            /* The top bits should be a sign-extension of the sign bit */
            bool sign_bit_set = ((uint8)byte) & 0x1;
            int top_bits = ((uint8)byte) & 0xfe;

            if ((sign_bit_set && top_bits != 0x7e)
                || (!sign_bit_set && top_bits != 0))
                goto fail_integer_too_large;
        }
    }

    *p_buf += offset;
    *p_result = result;
    return true;

fail_integer_too_large:
    aot_set_last_error("integer too large");
    return false;
}

/* NOLINTNEXTLINE */
#define read_leb_uint32(p, p_end, res)                         \
    do {                                                       \
        uint64 res64;                                          \
        if (!read_leb((uint8 **)&p, p_end, 32, false, &res64)) \
            goto fail;                                         \
        res = (uint32)res64;                                   \
    } while (0)

/*
 * - transfer .name section in .wasm (comp_data->name_section_buf) to
 *   aot buf (comp_data->aot_name_section_buf)
 * - leb128 to u32
 * - add `\0` at the end of every name, and adjust length(+1)
 */
static uint32
get_name_section_size(AOTCompData *comp_data)
{
    /* original name section content in .wasm */
    const uint8 *p = comp_data->name_section_buf,
                *p_end = comp_data->name_section_buf_end;
    uint8 *buf, *buf_end;
    uint32 name_type, subsection_size;
    uint32 previous_name_type = 0;
    uint32 num_func_name;
    uint32 func_index;
    uint32 previous_func_index = ~0U;
    uint32 func_name_len;
    uint32 name_index;
    int i = 0;
    uint32 name_len;
    uint32 offset = 0;
    uint32 max_aot_buf_size = 0;

    if (p >= p_end) {
        aot_set_last_error("unexpected end");
        return 0;
    }

    max_aot_buf_size = 4 * (uint32)(p_end - p);
    if (!(buf = comp_data->aot_name_section_buf =
              wasm_runtime_malloc(max_aot_buf_size))) {
        aot_set_last_error("allocate memory for custom name section failed.");
        return 0;
    }
    memset(buf, 0, (uint32)max_aot_buf_size);
    buf_end = buf + max_aot_buf_size;

    /* the size of "name". it should be 4 */
    read_leb_uint32(p, p_end, name_len);
    offset = align_uint(offset, 4);
    EMIT_U32(name_len);

    if (name_len != 4 || p + name_len > p_end) {
        aot_set_last_error("unexpected end");
        return 0;
    }

    /* "name" */
    if (memcmp(p, "name", 4) != 0) {
        aot_set_last_error("invalid custom name section");
        return 0;
    }
    EMIT_BUF(p, name_len);
    p += name_len;

    while (p < p_end) {
        read_leb_uint32(p, p_end, name_type);
        if (i != 0) {
            if (name_type == previous_name_type) {
                aot_set_last_error("duplicate sub-section");
                return 0;
            }
            if (name_type < previous_name_type) {
                aot_set_last_error("out-of-order sub-section");
                return 0;
            }
        }
        previous_name_type = name_type;
        read_leb_uint32(p, p_end, subsection_size);
        switch (name_type) {
            case SUB_SECTION_TYPE_FUNC:
                if (subsection_size) {
                    offset = align_uint(offset, 4);
                    EMIT_U32(name_type);
                    EMIT_U32(subsection_size);

                    read_leb_uint32(p, p_end, num_func_name);
                    EMIT_U32(num_func_name);

                    for (name_index = 0; name_index < num_func_name;
                         name_index++) {
                        read_leb_uint32(p, p_end, func_index);
                        offset = align_uint(offset, 4);
                        EMIT_U32(func_index);
                        if (func_index == previous_func_index) {
                            aot_set_last_error("duplicate function name");
                            return 0;
                        }
                        if (func_index < previous_func_index
                            && previous_func_index != ~0U) {
                            aot_set_last_error("out-of-order function index ");
                            return 0;
                        }
                        previous_func_index = func_index;
                        read_leb_uint32(p, p_end, func_name_len);
                        offset = align_uint(offset, 2);

                        /* emit a string ends with `\0` */
                        if (func_name_len + 1 > UINT16_MAX) {
                            aot_set_last_error(
                                "emit string failed: string too long");
                            goto fail;
                        }
                        /* extra 1 byte for \0 */
                        EMIT_U16(func_name_len + 1);
                        EMIT_BUF(p, func_name_len);
                        p += func_name_len;
                        EMIT_U8(0);
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

    return offset;
fail:
    return 0;
}
#endif /* end of WASM_ENABLE_LOAD_CUSTOM_SECTION != 0 */

#if WASM_ENABLE_STRINGREF != 0
static uint32
get_string_literal_section_size(AOTCompContext *comp_ctx,
                                AOTCompData *comp_data)
{
    uint32 i;
    uint32 size = 0;
    uint32 string_count = comp_data->string_literal_count;

    if (string_count == 0) {
        return 0;
    }

    /* reserved slot + string count + string_lengths */
    size += sizeof(uint32) * (2 + string_count);

    for (i = 0; i < string_count; i++) {
        size += comp_data->string_literal_lengths_wp[i];
    }

    return size;
}
#endif /* end of WASM_ENABLE_STRINGREF != 0 */

static uint32
get_custom_sections_size(AOTCompContext *comp_ctx, AOTCompData *comp_data)
{
#if WASM_ENABLE_LOAD_CUSTOM_SECTION != 0
    uint32 size = 0, i;

    for (i = 0; i < comp_ctx->custom_sections_count; i++) {
        const char *section_name = comp_ctx->custom_sections_wp[i];
        const uint8 *content = NULL;
        uint32 length = 0;

        if (strcmp(section_name, "name") == 0) {
            /* custom name section */
            comp_data->aot_name_section_size = get_name_section_size(comp_data);
            if (comp_data->aot_name_section_size == 0) {
                LOG_WARNING("Can't find custom section [name], ignore it");
                continue;
            }

            size = align_uint(size, 4);
            /* section id + section size + sub section id */
            size += (uint32)sizeof(uint32) * 3;
            size += comp_data->aot_name_section_size;
            continue;
        }

        content = wasm_loader_get_custom_section(comp_data->wasm_module,
                                                 section_name, &length);
        if (!content) {
            LOG_WARNING("Can't find custom section [%s], ignore it",
                        section_name);
            continue;
        }

        size = align_uint(size, 4);
        /* section id + section size + sub section id */
        size += (uint32)sizeof(uint32) * 3;
        /* section name and len */
        size += get_string_size(comp_ctx, section_name);
        /* section content */
        size += length;
    }

    return size;
#else
    return 0;
#endif
}

static bool
aot_emit_file_header(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                     AOTCompData *comp_data, AOTObjectData *obj_data)
{
    uint32 offset = *p_offset;
    uint32 aot_curr_version = AOT_CURRENT_VERSION;

    EMIT_U8('\0');
    EMIT_U8('a');
    EMIT_U8('o');
    EMIT_U8('t');

    EMIT_U32(aot_curr_version);

    *p_offset = offset;
    return true;
}

static bool
aot_emit_target_info_section(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                             AOTCompData *comp_data, AOTObjectData *obj_data)
{
    uint32 offset = *p_offset;
    uint32 section_size = get_target_info_section_size();
    AOTTargetInfo *target_info = &obj_data->target_info;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(AOT_SECTION_TYPE_TARGET_INFO);
    EMIT_U32(section_size);

    EMIT_U16(target_info->bin_type);
    EMIT_U16(target_info->abi_type);
    EMIT_U16(target_info->e_type);
    EMIT_U16(target_info->e_machine);
    EMIT_U32(target_info->e_version);
    EMIT_U32(target_info->e_flags);
    EMIT_U64(target_info->feature_flags);
    EMIT_U64(target_info->reserved);
    EMIT_BUF(target_info->arch, sizeof(target_info->arch));

    if (offset - *p_offset != section_size + sizeof(uint32) * 2) {
        aot_set_last_error("emit target info failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_init_expr(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                   AOTCompContext *comp_ctx, InitializerExpression *expr);

static bool
aot_emit_mem_info(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                  AOTCompContext *comp_ctx, AOTCompData *comp_data,
                  AOTObjectData *obj_data)
{
    uint32 offset = *p_offset, i;
    AOTMemInitData **init_datas = comp_data->mem_init_data_list;

    *p_offset = offset = align_uint(offset, 4);

    /* Emit import memory count, only emit 0 currently.
       TODO: emit the actual import memory count and
             the full import memory info. */
    EMIT_U32(0);

    /* Emit memory count */
    EMIT_U32(comp_data->memory_count);
    /* Emit memory items */
    for (i = 0; i < comp_data->memory_count; i++) {
        EMIT_U32(comp_data->memories[i].flags);
        EMIT_U32(comp_data->memories[i].num_bytes_per_page);
        EMIT_U32(comp_data->memories[i].init_page_count);
        EMIT_U32(comp_data->memories[i].max_page_count);
    }

    /* Emit mem init data count */
    EMIT_U32(comp_data->mem_init_data_count);
    /* Emit mem init data items */
    for (i = 0; i < comp_data->mem_init_data_count; i++) {
        offset = align_uint(offset, 4);
#if WASM_ENABLE_BULK_MEMORY != 0
        if (comp_ctx->enable_bulk_memory) {
            EMIT_U32(init_datas[i]->is_passive);
            EMIT_U32(init_datas[i]->memory_index);
        }
        else
#endif
        {
            /* emit two placeholder to keep the same size */
            EMIT_U32(0);
            EMIT_U32(0);
        }
        if (!aot_emit_init_expr(buf, buf_end, &offset, comp_ctx,
                                &init_datas[i]->offset))
            return false;
        EMIT_U32(init_datas[i]->byte_count);
        EMIT_BUF(init_datas[i]->bytes, init_datas[i]->byte_count);
    }

    if (offset - *p_offset != get_mem_info_size(comp_ctx, comp_data)) {
        aot_set_last_error("emit memory info failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_init_expr(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                   AOTCompContext *comp_ctx, InitializerExpression *expr)
{
    uint32 offset = *p_offset;
#if WASM_ENABLE_GC != 0
    WASMModule *module = comp_ctx->comp_data->wasm_module;
#endif

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(expr->init_expr_type);
    switch (expr->init_expr_type) {
        case INIT_EXPR_NONE:
            break;
        case INIT_EXPR_TYPE_I32_CONST:
        case INIT_EXPR_TYPE_F32_CONST:
            EMIT_U32(expr->u.i32);
            break;
        case INIT_EXPR_TYPE_I64_CONST:
        case INIT_EXPR_TYPE_F64_CONST:
            EMIT_U64(expr->u.i64);
            break;
        case INIT_EXPR_TYPE_V128_CONST:
            EMIT_V128(expr->u.v128);
            break;
        case INIT_EXPR_TYPE_GET_GLOBAL:
            EMIT_U32(expr->u.global_index);
            break;
        case INIT_EXPR_TYPE_FUNCREF_CONST:
        case INIT_EXPR_TYPE_REFNULL_CONST:
            EMIT_U32(expr->u.ref_index);
            break;
#if WASM_ENABLE_GC != 0
        case INIT_EXPR_TYPE_I31_NEW:
            EMIT_U32(expr->u.i32);
            break;
        case INIT_EXPR_TYPE_STRUCT_NEW:
        {
            uint32 i;
            WASMStructNewInitValues *init_values =
                (WASMStructNewInitValues *)expr->u.data;
            WASMStructType *struct_type = NULL;

            EMIT_U32(init_values->type_idx);
            EMIT_U32(init_values->count);

            bh_assert(init_values->type_idx < module->type_count);

            struct_type =
                (WASMStructType *)module->types[init_values->type_idx];

            bh_assert(struct_type);
            bh_assert(struct_type->field_count == init_values->count);

            for (i = 0; i < init_values->count; i++) {
                uint32 field_size = wasm_value_type_size_internal(
                    struct_type->fields[i].field_type, comp_ctx->pointer_size);
                if (field_size <= sizeof(uint32))
                    EMIT_U32(init_values->fields[i].u32);
                else if (field_size == sizeof(uint64))
                    EMIT_U64(init_values->fields[i].u64);
                else if (field_size == sizeof(uint64) * 2)
                    EMIT_V128(init_values->fields[i].v128);
                else {
                    bh_assert(0);
                }
            }

            break;
        }
        case INIT_EXPR_TYPE_STRUCT_NEW_DEFAULT:
            EMIT_U32(expr->u.type_index);
            break;
        case INIT_EXPR_TYPE_ARRAY_NEW_DEFAULT:
        {
            WASMArrayType *array_type = NULL;

            bh_assert(expr->u.array_new_default.type_index
                      < module->type_count);
            array_type =
                (WASMArrayType *)
                    module->types[expr->u.array_new_default.type_index];

            EMIT_U32(array_type->elem_type);
            EMIT_U32(expr->u.array_new_default.type_index);
            EMIT_U32(expr->u.array_new_default.length);
            break;
        }
        case INIT_EXPR_TYPE_ARRAY_NEW:
        case INIT_EXPR_TYPE_ARRAY_NEW_FIXED:
        {
            uint32 value_count, i, field_size;
            WASMArrayNewInitValues *init_values =
                (WASMArrayNewInitValues *)expr->u.data;
            WASMArrayType *array_type = NULL;

            bh_assert(init_values->type_idx < module->type_count);
            array_type = (WASMArrayType *)module->types[init_values->type_idx];

            EMIT_U32(array_type->elem_type);
            EMIT_U32(init_values->type_idx);
            EMIT_U32(init_values->length);

            value_count =
                (expr->init_expr_type == INIT_EXPR_TYPE_ARRAY_NEW_FIXED)
                    ? init_values->length
                    : 1;

            field_size = wasm_value_type_size_internal(array_type->elem_type,
                                                       comp_ctx->pointer_size);

            for (i = 0; i < value_count; i++) {
                if (field_size <= sizeof(uint32))
                    EMIT_U32(init_values->elem_data[i].u32);
                else if (field_size == sizeof(uint64))
                    EMIT_U64(init_values->elem_data[i].u64);
                else if (field_size == sizeof(uint64) * 2)
                    EMIT_V128(init_values->elem_data[i].v128);
                else {
                    bh_assert(0);
                }
            }
            break;
        }
#endif /* end of WASM_ENABLE_GC != 0 */
        default:
            aot_set_last_error("invalid init expr type.");
            return false;
    }

    *p_offset = offset;
    return true;
}

static bool
aot_emit_table_info(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                    AOTCompContext *comp_ctx, AOTCompData *comp_data,
                    AOTObjectData *obj_data)
{
    uint32 offset = *p_offset, i, j;
    AOTTableInitData **init_datas = comp_data->table_init_data_list;

    *p_offset = offset = align_uint(offset, 4);

    /* Emit import table count */
    EMIT_U32(comp_data->import_table_count);
    /* Emit table items */
    for (i = 0; i < comp_data->import_table_count; i++) {
        /* TODO:
         * EMIT_STR(comp_data->import_tables[i].module_name );
         * EMIT_STR(comp_data->import_tables[i].table_name);
         */
        EMIT_U8(comp_data->import_tables[i].table_type.elem_type);
        EMIT_U8(comp_data->import_tables[i].table_type.flags);
        EMIT_U8(comp_data->import_tables[i].table_type.possible_grow);
#if WASM_ENABLE_GC != 0
        if (comp_ctx->enable_gc
            && comp_data->import_tables[i].table_type.elem_ref_type) {
            EMIT_U8(comp_data->import_tables[i]
                        .table_type.elem_ref_type->ref_ht_common.nullable);
        }
        else
#endif
        {
            /* emit one placeholder to keep the same size */
            EMIT_U8(0);
        }
        EMIT_U32(comp_data->import_tables[i].table_type.init_size);
        EMIT_U32(comp_data->import_tables[i].table_type.max_size);
#if WASM_ENABLE_GC != 0
        if (comp_ctx->enable_gc
            && comp_data->import_tables[i].table_type.elem_ref_type) {
            bh_assert(wasm_is_type_multi_byte_type(
                comp_data->import_tables[i].table_type.elem_type));
            EMIT_U32(comp_data->import_tables[i]
                         .table_type.elem_ref_type->ref_ht_common.heap_type);
        }
#endif
    }

    /* Emit table count */
    EMIT_U32(comp_data->table_count);
    /* Emit table items */
    for (i = 0; i < comp_data->table_count; i++) {
        EMIT_U8(comp_data->tables[i].table_type.elem_type);
        EMIT_U8(comp_data->tables[i].table_type.flags);
        EMIT_U8(comp_data->tables[i].table_type.possible_grow);
#if WASM_ENABLE_GC != 0
        if (comp_ctx->enable_gc
            && comp_data->tables[i].table_type.elem_ref_type) {
            EMIT_U8(comp_data->tables[i]
                        .table_type.elem_ref_type->ref_ht_common.nullable);
        }
        else
#endif
        {
            /* emit one placeholder to keep the same size */
            EMIT_U8(0);
        }
        EMIT_U32(comp_data->tables[i].table_type.init_size);
        EMIT_U32(comp_data->tables[i].table_type.max_size);
#if WASM_ENABLE_GC != 0
        if (comp_ctx->enable_gc) {
            if (comp_data->tables[i].table_type.elem_ref_type) {
                bh_assert(wasm_is_type_multi_byte_type(
                    comp_data->tables[i].table_type.elem_type));
                EMIT_U32(
                    comp_data->tables[i]
                        .table_type.elem_ref_type->ref_ht_common.heap_type);
            }
            if (!aot_emit_init_expr(buf, buf_end, &offset, comp_ctx,
                                    &comp_data->tables[i].init_expr)) {
                return false;
            }
        }
#endif
    }

    /* Emit table init data count */
    EMIT_U32(comp_data->table_init_data_count);
    /* Emit table init data items */
    for (i = 0; i < comp_data->table_init_data_count; i++) {
        offset = align_uint(offset, 4);
        EMIT_U32(init_datas[i]->mode);
        EMIT_U32(init_datas[i]->elem_type);
        EMIT_U32(init_datas[i]->table_index);
        EMIT_U32(init_datas[i]->offset.init_expr_type);
        EMIT_U64(init_datas[i]->offset.u.i64);
#if WASM_ENABLE_GC != 0
        if (comp_ctx->enable_gc && init_datas[i]->elem_ref_type) {
            EMIT_U16(init_datas[i]->elem_ref_type->ref_ht_common.ref_type);
            EMIT_U16(init_datas[i]->elem_ref_type->ref_ht_common.nullable);
            EMIT_U32(init_datas[i]->elem_ref_type->ref_ht_common.heap_type);
        }
        else
#endif
        {
            EMIT_U16(init_datas[i]->elem_type);
            EMIT_U16(0);
            EMIT_U32(0);
        }
        EMIT_U32(init_datas[i]->value_count);
        for (j = 0; j < init_datas[i]->value_count; j++) {
            if (!aot_emit_init_expr(buf, buf_end, &offset, comp_ctx,
                                    &init_datas[i]->init_values[j]))
                return false;
        }
    }

    if (offset - *p_offset != get_table_info_size(comp_ctx, comp_data)) {
        aot_set_last_error("emit table info failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

#if WASM_ENABLE_GC != 0
static bool
aot_emit_reftype_map(uint8 *buf, uint8 *buf_end, uint32 *p_offset, uint32 count,
                     WASMRefTypeMap *refmap)
{
    uint32 offset = *p_offset, i;

    for (i = 0; i < count; i++) {
        EMIT_U16(refmap->index);
        WASMRefType *ref_type = refmap->ref_type;

        /* Note: WASMRefType is a union type */
        EMIT_U8(ref_type->ref_ht_common.ref_type);
        EMIT_U8(ref_type->ref_ht_common.nullable);
        EMIT_U32(ref_type->ref_ht_common.heap_type);

        refmap++;
    }

    *p_offset = offset;
    return true;
}
#endif

static bool
aot_emit_type_info(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                   AOTCompContext *comp_ctx, AOTCompData *comp_data,
                   AOTObjectData *obj_data)
{
    uint32 offset = *p_offset, i;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(comp_data->type_count);

#if WASM_ENABLE_GC != 0
    if (comp_ctx->enable_gc) {
        AOTType **types = comp_data->types;
        int32 idx;
        uint32 j;

        for (i = 0; i < comp_data->type_count; i++) {
            offset = align_uint(offset, 4);

            /* Emit simple info if there is an equivalence type */
            for (j = 0; j < i; j++) {
                if (types[j] == types[i]) {
                    EMIT_U16(types[i]->type_flag);
                    /* equivalence type flag is true */
                    EMIT_U8(1);
                    EMIT_U8(0);
                    /* equivalence type index */
                    EMIT_U32(j);
                    break;
                }
            }
            if (j < i)
                continue;

            EMIT_U16(types[i]->type_flag);
            /* equivalence type flag is false */
            EMIT_U8(0);
            EMIT_U8(types[i]->is_sub_final);
            EMIT_U32(types[i]->parent_type_idx);

            EMIT_U16(types[i]->rec_count);
            EMIT_U16(types[i]->rec_idx);

            /* Emit WASM_TYPE_FUNC */
            if (types[i]->type_flag == WASM_TYPE_FUNC) {
                AOTFuncType *func_type = (AOTFuncType *)types[i];
                EMIT_U16(func_type->param_count);
                EMIT_U16(func_type->result_count);
                EMIT_U16(func_type->ref_type_map_count);
                EMIT_BUF(func_type->types,
                         func_type->param_count + func_type->result_count);

                offset = align_uint(offset, 4);

                aot_emit_reftype_map(buf, buf_end, &offset,
                                     func_type->ref_type_map_count,
                                     func_type->ref_type_maps);
            }
            /* Emit WASM_TYPE_STRUCT */
            else if (types[i]->type_flag == WASM_TYPE_STRUCT) {
                AOTStructType *struct_type = (AOTStructType *)types[i];
                EMIT_U16(struct_type->field_count);
                EMIT_U16(struct_type->ref_type_map_count);

                for (idx = 0; idx < struct_type->field_count; idx++) {
                    EMIT_U8(struct_type->fields[idx].field_flags);
                    EMIT_U8(struct_type->fields[idx].field_type);
                }

                offset = align_uint(offset, 4);

                aot_emit_reftype_map(buf, buf_end, &offset,
                                     struct_type->ref_type_map_count,
                                     struct_type->ref_type_maps);
            }
            /* Emit WASM_TYPE_ARRAY */
            else if (types[i]->type_flag == WASM_TYPE_ARRAY) {
                AOTArrayType *array_type = (AOTArrayType *)types[i];
                EMIT_U16(array_type->elem_flags);
                EMIT_U8(array_type->elem_type);
                if (array_type->elem_ref_type) {
                    bh_assert(
                        wasm_is_type_multi_byte_type(array_type->elem_type));
                    EMIT_U8(array_type->elem_ref_type->ref_ht_common.nullable);
                    EMIT_U32(
                        array_type->elem_ref_type->ref_ht_common.heap_type);
                }
            }
            else {
                aot_set_last_error("invalid type flag.");
                return false;
            }
        }

        if (offset - *p_offset != get_type_info_size(comp_ctx, comp_data)) {
            aot_set_last_error("emit function type info failed.");
            return false;
        }

        *p_offset = offset;
    }
    else
#endif
    {
        AOTFuncType **func_types = (AOTFuncType **)comp_data->types;

        for (i = 0; i < comp_data->type_count; i++) {
            offset = align_uint(offset, 4);
            /* If GC is disabled, only emit function type info */
            EMIT_U16(WASM_TYPE_FUNC);
            /* Omit to emit dummy padding for is_sub_final,
             * parent_type_index, rec_count, rec_idx, 10 bytes in total */
            EMIT_U16(func_types[i]->param_count);
            EMIT_U16(func_types[i]->result_count);
            /* Omit to emit dummy padding for ref_type_map_count, 2 bytes in
             * total */
            EMIT_BUF(func_types[i]->types,
                     func_types[i]->param_count + func_types[i]->result_count);
        }

        if (offset - *p_offset != get_type_info_size(comp_ctx, comp_data)) {
            aot_set_last_error("emit function type info failed.");
            return false;
        }

        *p_offset = offset;
    }

    return true;
}

static bool
aot_emit_import_global_info(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                            AOTCompContext *comp_ctx, AOTCompData *comp_data,
                            AOTObjectData *obj_data)
{
    uint32 offset = *p_offset, i;
    AOTImportGlobal *import_global = comp_data->import_globals;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(comp_data->import_global_count);

    for (i = 0; i < comp_data->import_global_count; i++, import_global++) {
        offset = align_uint(offset, 2);
        EMIT_U8(import_global->type.val_type);
        EMIT_U8(import_global->type.is_mutable);
        EMIT_STR(import_global->module_name);
        offset = align_uint(offset, 2);
        EMIT_STR(import_global->global_name);
    }

    if (offset - *p_offset
        != get_import_global_info_size(comp_ctx, comp_data)) {
        aot_set_last_error("emit import global info failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_global_info(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                     AOTCompContext *comp_ctx, AOTCompData *comp_data,
                     AOTObjectData *obj_data)
{
    uint32 offset = *p_offset, i;
    AOTGlobal *global = comp_data->globals;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(comp_data->global_count);

    for (i = 0; i < comp_data->global_count; i++, global++) {
        offset = align_uint(offset, 4);
        EMIT_U8(global->type.val_type);
        EMIT_U8(global->type.is_mutable);

        offset = align_uint(offset, 4);
        if (!aot_emit_init_expr(buf, buf_end, &offset, comp_ctx,
                                &global->init_expr))
            return false;
    }

    if (offset - *p_offset != get_global_info_size(comp_ctx, comp_data)) {
        aot_set_last_error("emit global info failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_import_func_info(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                          AOTCompContext *comp_ctx, AOTCompData *comp_data,
                          AOTObjectData *obj_data)
{
    uint32 offset = *p_offset, i;
    AOTImportFunc *import_func = comp_data->import_funcs;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(comp_data->import_func_count);

    for (i = 0; i < comp_data->import_func_count; i++, import_func++) {
        offset = align_uint(offset, 2);
        EMIT_U16(import_func->func_type_index);
        EMIT_STR(import_func->module_name);
        offset = align_uint(offset, 2);
        EMIT_STR(import_func->func_name);
    }

    if (offset - *p_offset != get_import_func_info_size(comp_ctx, comp_data)) {
        aot_set_last_error("emit import function info failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_object_data_section_info(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                                  AOTCompContext *comp_ctx,
                                  AOTObjectData *obj_data)
{
    uint32 offset = *p_offset, i;
    AOTObjectDataSection *data_section = obj_data->data_sections;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(obj_data->data_sections_count);

    for (i = 0; i < obj_data->data_sections_count; i++, data_section++) {
        offset = align_uint(offset, 2);
        EMIT_STR(data_section->name);
        offset = align_uint(offset, 4);
        EMIT_U32(data_section->size);
        if (obj_data->stack_sizes_section_name != NULL
            && !strcmp(obj_data->stack_sizes_section_name,
                       data_section->name)) {
            uint32 ss_offset = obj_data->stack_sizes_offset;
            uint32 ss_size =
                obj_data->func_count * sizeof(*obj_data->stack_sizes);
            LOG_VERBOSE("Replacing stack_sizes in %s section, offset %" PRIu32
                        ", size %" PRIu32,
                        obj_data->stack_sizes_section_name, ss_offset, ss_size);
            bh_assert(ss_offset + ss_size <= data_section->size);
            /* 0 .. ss_offset */
            if (ss_offset > 0) {
                EMIT_BUF(data_section->data, ss_offset);
            }
            /* ss_offset .. ss_offset+ss_size */
            EMIT_BUF(obj_data->stack_sizes, ss_size);
            /* ss_offset+ss_size .. data_section->size */
            if (data_section->size > ss_offset + ss_size) {
                EMIT_BUF(data_section->data + ss_offset + ss_size,
                         data_section->size - (ss_offset + ss_size));
            }
        }
        else {
            EMIT_BUF(data_section->data, data_section->size);
        }
    }

    if (offset - *p_offset
        != get_object_data_section_info_size(comp_ctx, obj_data)) {
        aot_set_last_error("emit object data section info failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_init_data_section(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                           AOTCompContext *comp_ctx, AOTCompData *comp_data,
                           AOTObjectData *obj_data)
{
    uint32 section_size =
        get_init_data_section_size(comp_ctx, comp_data, obj_data);
    uint32 offset = *p_offset;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(AOT_SECTION_TYPE_INIT_DATA);
    EMIT_U32(section_size);

    if (!aot_emit_mem_info(buf, buf_end, &offset, comp_ctx, comp_data, obj_data)
        || !aot_emit_table_info(buf, buf_end, &offset, comp_ctx, comp_data,
                                obj_data)
        || !aot_emit_type_info(buf, buf_end, &offset, comp_ctx, comp_data,
                               obj_data)
        || !aot_emit_import_global_info(buf, buf_end, &offset, comp_ctx,
                                        comp_data, obj_data)
        || !aot_emit_global_info(buf, buf_end, &offset, comp_ctx, comp_data,
                                 obj_data)
        || !aot_emit_import_func_info(buf, buf_end, &offset, comp_ctx,
                                      comp_data, obj_data))
        return false;

    offset = align_uint(offset, 4);
    EMIT_U32(comp_data->func_count);
    EMIT_U32(comp_data->start_func_index);

    EMIT_U32(comp_data->aux_data_end_global_index);
    EMIT_U64(comp_data->aux_data_end);
    EMIT_U32(comp_data->aux_heap_base_global_index);
    EMIT_U64(comp_data->aux_heap_base);
    EMIT_U32(comp_data->aux_stack_top_global_index);
    EMIT_U64(comp_data->aux_stack_bottom);
    EMIT_U32(comp_data->aux_stack_size);

    if (!aot_emit_object_data_section_info(buf, buf_end, &offset, comp_ctx,
                                           obj_data))
        return false;

    if (offset - *p_offset != section_size + sizeof(uint32) * 2) {
        aot_set_last_error("emit init data section failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_text_section(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                      AOTCompData *comp_data, AOTObjectData *obj_data)
{
    uint32 section_size = get_text_section_size(obj_data);
    uint32 offset = *p_offset;
    uint8 placeholder = 0;
    AOTRelocationGroup *relocation_group;
    AOTRelocation *relocation;
    uint32 i, j, relocation_count;
    uint8 *text;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(AOT_SECTION_TYPE_TEXT);
    EMIT_U32(section_size);
    EMIT_U32(obj_data->literal_size);

    if (obj_data->literal_size > 0) {
        EMIT_BUF(obj_data->literal, obj_data->literal_size);
        while (offset & 3)
            EMIT_BUF(&placeholder, 1);
    }

    text = buf + offset;

    if (obj_data->text_size > 0) {
        EMIT_BUF(obj_data->text, obj_data->text_size);
        while (offset & 3)
            EMIT_BUF(&placeholder, 1);
    }
    if (obj_data->text_unlikely_size > 0) {
        EMIT_BUF(obj_data->text_unlikely, obj_data->text_unlikely_size);
        while (offset & 3)
            EMIT_BUF(&placeholder, 1);
    }
    if (obj_data->text_hot_size > 0) {
        EMIT_BUF(obj_data->text_hot, obj_data->text_hot_size);
        while (offset & 3)
            EMIT_BUF(&placeholder, 1);
    }

    if (offset - *p_offset != section_size + sizeof(uint32) * 2) {
        aot_set_last_error("emit text section failed.");
        return false;
    }

    /* apply relocations to aot_func_internal#n in text section for
       windows platform */
    if ((!strncmp(obj_data->comp_ctx->target_arch, "x86_64", 6)
         /* Windows AOT_COFF64_BIN_TYPE */
         && obj_data->target_info.bin_type == 6)
        || (!strncmp(obj_data->comp_ctx->target_arch, "i386", 4)
            /* Windows AOT_COFF32_BIN_TYPE */
            && obj_data->target_info.bin_type == 4)) {
        relocation_group = obj_data->relocation_groups;
        for (i = 0; i < obj_data->relocation_group_count;
             i++, relocation_group++) {
            /* relocation in text section */
            if ((!strcmp(relocation_group->section_name, ".text")
                 || !strcmp(relocation_group->section_name, ".ltext"))) {
                relocation = relocation_group->relocations;
                relocation_count = relocation_group->relocation_count;
                for (j = 0; j < relocation_count; j++) {
                    /* relocation to aot_func_internal#n */
                    if (str_starts_with(relocation->symbol_name,
                                        AOT_FUNC_INTERNAL_PREFIX)
                        && ((obj_data->target_info.bin_type
                                 == 6 /* AOT_COFF64_BIN_TYPE */
                             && relocation->relocation_type
                                    == 4 /* IMAGE_REL_AMD64_REL32 */)
                            || (obj_data->target_info.bin_type
                                    == 4 /* AOT_COFF32_BIN_TYPE */
                                && relocation->relocation_type
                                       == 20 /* IMAGE_REL_I386_REL32 */))) {
                        uint32 func_idx =
                            atoi(relocation->symbol_name
                                 + strlen(AOT_FUNC_INTERNAL_PREFIX));
                        uint64 text_offset, reloc_offset, reloc_addend;

                        bh_assert(func_idx < obj_data->func_count);

                        text_offset = obj_data->funcs[func_idx]
                                          .text_offset_of_aot_func_internal;
                        reloc_offset = relocation->relocation_offset;
                        reloc_addend = relocation->relocation_addend;
                        /* S + A - P */
                        *(uint32 *)(text + reloc_offset) =
                            (uint32)(text_offset + reloc_addend - reloc_offset
                                     - 4);

                        /* remove current relocation as it has been applied */
                        if (j < relocation_count - 1) {
                            uint32 move_size =
                                (uint32)(sizeof(AOTRelocation)
                                         * (relocation_count - 1 - j));
                            bh_memmove_s(relocation, move_size, relocation + 1,
                                         move_size);
                        }
                        relocation_group->relocation_count--;
                    }
                    else {
                        relocation++;
                    }
                }
            }
        }
    }

    *p_offset = offset;

    return true;
}

#if WASM_ENABLE_GC != 0
static bool
aot_emit_ref_flag(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                  uint8 pointer_size, int8 type)
{
    uint32 j, offset = *p_offset;
    uint16 value_type_cell_num;

    if (wasm_is_type_reftype(type) && !wasm_is_reftype_i31ref(type)) {
        EMIT_U8(1);
        if (pointer_size == sizeof(uint64))
            EMIT_U8(1);
    }
    else {
        value_type_cell_num = wasm_value_type_cell_num(type);
        for (j = 0; j < value_type_cell_num; j++)
            EMIT_U8(0);
    }

    *p_offset = offset;
    return true;
}
#endif

static bool
aot_emit_func_section(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                      AOTCompContext *comp_ctx, AOTCompData *comp_data,
                      AOTObjectData *obj_data)
{
    uint32 section_size = get_func_section_size(comp_ctx, comp_data, obj_data);
    uint32 i, offset = *p_offset;
    AOTObjectFunc *func = obj_data->funcs;
    AOTFunc **funcs = comp_data->funcs;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(AOT_SECTION_TYPE_FUNCTION);
    EMIT_U32(section_size);

    for (i = 0; i < obj_data->func_count; i++, func++) {
        if (is_32bit_binary(obj_data))
            EMIT_U32(func->text_offset);
        else
            EMIT_U64(func->text_offset);
    }

    for (i = 0; i < comp_data->func_count; i++)
        EMIT_U32(funcs[i]->func_type_index);

    for (i = 0; i < comp_data->func_count; i++) {
        uint32 max_local_cell_num =
            funcs[i]->param_cell_num + funcs[i]->local_cell_num;
        EMIT_U32(max_local_cell_num);
    }

    for (i = 0; i < comp_data->func_count; i++)
        EMIT_U32(funcs[i]->max_stack_cell_num);

#if WASM_ENABLE_GC != 0
    if (comp_ctx->enable_gc) {
        /* emit func_local_ref_flag arrays for both import and AOTed funcs */
        AOTFuncType *func_type;
        uint32 j, local_ref_flags_cell_num, paddings;

        for (i = 0; i < comp_data->import_func_count; i++) {
            func_type = comp_data->import_funcs[i].func_type;
            /* recalculate cell_num based on target pointer size */
            local_ref_flags_cell_num = 0;
            for (j = 0; j < func_type->param_count; j++) {
                local_ref_flags_cell_num += wasm_value_type_cell_num_internal(
                    func_type->types[j], comp_ctx->pointer_size);
            }
            paddings =
                local_ref_flags_cell_num < 2 ? 2 - local_ref_flags_cell_num : 0;
            local_ref_flags_cell_num =
                local_ref_flags_cell_num > 2 ? local_ref_flags_cell_num : 2;

            offset = align_uint(offset, 4);
            EMIT_U32(local_ref_flags_cell_num);
            for (j = 0; j < func_type->param_count; j++) {
                if (!aot_emit_ref_flag(buf, buf_end, &offset,
                                       comp_ctx->pointer_size,
                                       func_type->types[j]))
                    return false;
            }
            for (j = 0; j < paddings; j++)
                EMIT_U8(0);
        }

        for (i = 0; i < comp_data->func_count; i++) {
            func_type = funcs[i]->func_type;
            local_ref_flags_cell_num =
                funcs[i]->param_cell_num + funcs[i]->local_cell_num;

            offset = align_uint(offset, 4);
            EMIT_U32(local_ref_flags_cell_num);
            /* emit local_ref_flag for param variables */
            for (j = 0; j < func_type->param_count; j++) {
                if (!aot_emit_ref_flag(buf, buf_end, &offset,
                                       comp_ctx->pointer_size,
                                       func_type->types[j]))
                    return false;
            }
            /* emit local_ref_flag for local variables */
            for (j = 0; j < funcs[i]->local_count; j++) {
                if (!aot_emit_ref_flag(buf, buf_end, &offset,
                                       comp_ctx->pointer_size,
                                       funcs[i]->local_types_wp[j]))
                    return false;
            }
        }
    }
#endif /* end of WASM_ENABLE_GC != 0 */

    if (offset - *p_offset != section_size + sizeof(uint32) * 2) {
        aot_set_last_error("emit function section failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_export_section(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                        AOTCompContext *comp_ctx, AOTCompData *comp_data,
                        AOTObjectData *obj_data)
{
    uint32 section_size = get_export_section_size(comp_ctx, comp_data);
    AOTExport *export = comp_data->wasm_module->exports;
    uint32 export_count = comp_data->wasm_module->export_count;
    uint32 i, offset = *p_offset;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(AOT_SECTION_TYPE_EXPORT);
    EMIT_U32(section_size);
    EMIT_U32(export_count);

    for (i = 0; i < export_count; i++, export ++) {
        offset = align_uint(offset, 4);
        EMIT_U32(export->index);
        EMIT_U8(export->kind);
        EMIT_U8(0);
        EMIT_STR(export->name);
    }

    if (offset - *p_offset != section_size + sizeof(uint32) * 2) {
        aot_set_last_error("emit export section failed.");
        return false;
    }

    *p_offset = offset;

    return true;
}

static bool
aot_emit_relocation_symbol_table(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                                 AOTCompContext *comp_ctx,
                                 AOTCompData *comp_data,
                                 AOTObjectData *obj_data)
{
    uint32 symbol_offset = 0, total_string_len = 0;
    uint32 offset = *p_offset;
    AOTSymbolNode *sym;

    EMIT_U32(obj_data->symbol_list.len);

    /* emit symbol offsets */
    sym = (AOTSymbolNode *)(obj_data->symbol_list.head);
    while (sym) {
        EMIT_U32(symbol_offset);
        /* string_len + str[0 .. string_len - 1] */
        symbol_offset += get_string_size(comp_ctx, sym->symbol);
        symbol_offset = align_uint(symbol_offset, 2);
        sym = sym->next;
    }

    /* emit total string len */
    total_string_len = symbol_offset;
    EMIT_U32(total_string_len);

    /* emit symbols */
    sym = (AOTSymbolNode *)(obj_data->symbol_list.head);
    while (sym) {
        EMIT_STR(sym->symbol);
        offset = align_uint(offset, 2);
        sym = sym->next;
    }

    *p_offset = offset;
    return true;
}

static bool
aot_emit_relocation_section(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                            AOTCompContext *comp_ctx, AOTCompData *comp_data,
                            AOTObjectData *obj_data)
{
    uint32 section_size = get_relocation_section_size(comp_ctx, obj_data);
    uint32 i, offset = *p_offset;
    AOTRelocationGroup *relocation_group = obj_data->relocation_groups;

    if (section_size == (uint32)-1)
        return false;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(AOT_SECTION_TYPE_RELOCATION);
    EMIT_U32(section_size);

    aot_emit_relocation_symbol_table(buf, buf_end, &offset, comp_ctx, comp_data,
                                     obj_data);

    offset = align_uint(offset, 4);
    EMIT_U32(obj_data->relocation_group_count);

    /* emit each relocation group */
    for (i = 0; i < obj_data->relocation_group_count; i++, relocation_group++) {
        AOTRelocation *relocation = relocation_group->relocations;
        uint32 j;

        offset = align_uint(offset, 4);
        EMIT_U32(relocation_group->name_index);
        offset = align_uint(offset, 4);
        EMIT_U32(relocation_group->relocation_count);

        /* emit each relocation */
        for (j = 0; j < relocation_group->relocation_count; j++, relocation++) {
            offset = align_uint(offset, 4);
            if (is_32bit_binary(obj_data)) {
                EMIT_U32(relocation->relocation_offset);
                EMIT_U32(relocation->relocation_addend);
            }
            else {
                EMIT_U64(relocation->relocation_offset);
                EMIT_U64(relocation->relocation_addend);
            }
            EMIT_U32(relocation->relocation_type);
            EMIT_U32(relocation->symbol_index);
        }
    }

    if (offset - *p_offset != section_size + sizeof(uint32) * 2) {
        aot_set_last_error("emit relocation section failed.");
        return false;
    }

    *p_offset = offset;
    return true;
}

static bool
aot_emit_native_symbol(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                       AOTCompContext *comp_ctx)
{
    uint32 offset = *p_offset;
    AOTNativeSymbol *sym = NULL;

    if (bh_list_length(&comp_ctx->native_symbols) == 0)
        /* emit only when there are native symbols */
        return true;

    *p_offset = offset = align_uint(offset, 4);

    EMIT_U32(AOT_SECTION_TYPE_CUSTOM);
    /* sub section id + symbol count + symbol list */
    EMIT_U32(sizeof(uint32) * 2 + get_native_symbol_list_size(comp_ctx));
    EMIT_U32(AOT_CUSTOM_SECTION_NATIVE_SYMBOL);
    EMIT_U32(bh_list_length(&comp_ctx->native_symbols));

    sym = bh_list_first_elem(&comp_ctx->native_symbols);

    while (sym) {
        offset = align_uint(offset, 2);
        EMIT_STR(sym->symbol);
        sym = bh_list_elem_next(sym);
    }

    *p_offset = offset;

    return true;
}

#if WASM_ENABLE_LOAD_CUSTOM_SECTION != 0
static bool
aot_emit_name_section(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                      AOTCompData *comp_data, AOTCompContext *comp_ctx)
{
    uint32 offset = *p_offset;

    if (comp_data->aot_name_section_size == 0)
        return true;

    offset = align_uint(offset, 4);

    EMIT_U32(AOT_SECTION_TYPE_CUSTOM);
    /* sub section id + name section size */
    EMIT_U32(sizeof(uint32) * 1 + comp_data->aot_name_section_size);
    EMIT_U32(AOT_CUSTOM_SECTION_NAME);
    bh_memcpy_s((uint8 *)(buf + offset), (uint32)(buf_end - buf),
                comp_data->aot_name_section_buf,
                (uint32)comp_data->aot_name_section_size);
    offset += comp_data->aot_name_section_size;

    *p_offset = offset;

    LOG_DEBUG("emit name section");
    return true;
}
#endif

#if WASM_ENABLE_STRINGREF != 0
static bool
aot_emit_string_literal_section(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                                AOTCompData *comp_data,
                                AOTCompContext *comp_ctx)
{
    uint32 string_count = comp_data->string_literal_count;

    if (string_count > 0) {
        uint32 offset = *p_offset;
        uint32 i;

        *p_offset = offset = align_uint(offset, 4);

        EMIT_U32(AOT_SECTION_TYPE_CUSTOM);
        /* sub section id + string literal section size */
        EMIT_U32(sizeof(uint32) * 1
                 + get_string_literal_section_size(comp_ctx, comp_data));
        EMIT_U32(AOT_CUSTOM_SECTION_STRING_LITERAL);

        /* reserved */
        EMIT_U32(0);

        /* string literal count */
        EMIT_U32(string_count);

        for (i = 0; i < string_count; i++) {
            EMIT_U32(comp_data->string_literal_lengths_wp[i]);
        }

        for (i = 0; i < string_count; i++) {
            uint32 string_length = comp_data->string_literal_lengths_wp[i];
            bh_memcpy_s((uint8 *)(buf + offset), (uint32)(buf_end - buf),
                        comp_data->string_literal_ptrs_wp[i], string_length);
            offset += string_length;
        }

        *p_offset = offset;
    }

    return true;
}
#endif /* end of WASM_ENABLE_STRINGREF != 0 */

static bool
aot_emit_custom_sections(uint8 *buf, uint8 *buf_end, uint32 *p_offset,
                         AOTCompData *comp_data, AOTCompContext *comp_ctx)
{
#if WASM_ENABLE_LOAD_CUSTOM_SECTION != 0
    uint32 offset = *p_offset, i;

    for (i = 0; i < comp_ctx->custom_sections_count; i++) {
        const char *section_name = comp_ctx->custom_sections_wp[i];
        const uint8 *content = NULL;
        uint32 length = 0;

        if (strcmp(section_name, "name") == 0) {
            *p_offset = offset;
            if (!aot_emit_name_section(buf, buf_end, p_offset, comp_data,
                                       comp_ctx))
                return false;

            offset = *p_offset;
            continue;
        }

        content = wasm_loader_get_custom_section(comp_data->wasm_module,
                                                 section_name, &length);
        if (!content) {
            /* Warning has been reported during calculating size */
            continue;
        }

        offset = align_uint(offset, 4);
        EMIT_U32(AOT_SECTION_TYPE_CUSTOM);
        /* sub section id + content */
        EMIT_U32(sizeof(uint32) * 1 + get_string_size(comp_ctx, section_name)
                 + length);
        EMIT_U32(AOT_CUSTOM_SECTION_RAW);
        EMIT_STR(section_name);
        bh_memcpy_s((uint8 *)(buf + offset), (uint32)(buf_end - buf), content,
                    length);
        offset += length;
    }

    *p_offset = offset;
#endif

    return true;
}

typedef uint32 U32;
typedef int32 I32;
typedef uint16 U16;
typedef uint8 U8;

struct coff_hdr {
    U16 u16Machine;
    U16 u16NumSections;
    U32 u32DateTimeStamp;
    U32 u32SymTblPtr;
    U32 u32NumSymbols;
    U16 u16PeHdrSize;
    U16 u16Characs;
};

#define E_TYPE_REL 1
#define E_TYPE_XIP 4

#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_I386 0x014c
#define IMAGE_FILE_MACHINE_IA64 0x0200

#define AOT_COFF32_BIN_TYPE 4 /* 32-bit little endian */
#define AOT_COFF64_BIN_TYPE 6 /* 64-bit little endian */

#define EI_NIDENT 16

typedef uint32 elf32_word;
typedef int32 elf32_sword;
typedef uint16 elf32_half;
typedef uint32 elf32_off;
typedef uint32 elf32_addr;

struct elf32_ehdr {
    unsigned char e_ident[EI_NIDENT]; /* ident bytes */
    elf32_half e_type;                /* file type */
    elf32_half e_machine;             /* target machine */
    elf32_word e_version;             /* file version */
