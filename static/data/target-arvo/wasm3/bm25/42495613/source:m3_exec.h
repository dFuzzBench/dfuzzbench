//
//  m3_exec.h
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.


#ifndef m3_exec_h
#define m3_exec_h

// TODO: all these functions could move over to the .c at some point. normally, I'd say screw it,
// but it might prove useful to be able to compile m3_exec alone w/ optimizations while the remaining
// code is at debug O0


// About the naming convention of these operations/macros (_rs, _sr_, _ss, _srs, etc.)
//------------------------------------------------------------------------------------------------------
//   - 'r' means register and 's' means slot
//   - the first letter is the top of the stack
//
//  so, for example, _rs means the first operand (the first thing pushed to the stack) is in a slot
//  and the second operand (the top of the stack) is in a register
//------------------------------------------------------------------------------------------------------


#include "m3_math_utils.h"
#include "m3_compile.h"
#include "m3_env.h"
#include "m3_info.h"
#include "m3_exec_defs.h"

#include <limits.h>

d_m3BeginExternC

# define rewrite_op(OP)             * ((void **) (_pc-1)) = (void*)(OP)

# define immediate(TYPE)            * ((TYPE *) _pc++)
# define skip_immediate(TYPE)       (_pc++)

# define slot(TYPE)                 * (TYPE *) (_sp + immediate (i32))
# define slot_ptr(TYPE)             (TYPE *) (_sp + immediate (i32))


# if d_m3EnableOpProfiling
                                    d_m3RetSig  profileOp   (d_m3OpSig, cstr_t i_operationName);
#   define nextOp()                 return profileOp (d_m3OpAllArgs, __FUNCTION__)
# elif d_m3EnableOpTracing
                                    d_m3RetSig  debugOp     (d_m3OpSig, cstr_t i_operationName);
#   define nextOp()                 return debugOp (d_m3OpAllArgs, __FUNCTION__)
# else
#   define nextOp()                 nextOpDirect()
# endif

#define jumpOp(PC)                  jumpOpDirect(PC)

#if d_m3RecordBacktraces
    #define pushBacktraceFrame()            (PushBacktraceFrame (_mem->runtime, _pc - 1))
    #define fillBacktraceFrame(FUNCTION)    (FillBacktraceFunctionInfo (_mem->runtime, function))

    #define newTrap(err)                    return (pushBacktraceFrame (), err)
    #define forwardTrap(err)                return err
#else
    #define pushBacktraceFrame()            do {} while (0)
    #define fillBacktraceFrame(FUNCTION)    do {} while (0)

    #define newTrap(err)                    return err
    #define forwardTrap(err)                return err
#endif


#if d_m3EnableStrace == 1
    // Flat trace
    #define d_m3TracePrepare
    #define d_m3TracePrint(fmt, ...)            fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#elif d_m3EnableStrace >= 2
    // Structured trace
    #define d_m3TracePrepare                    const IM3Runtime trace_rt = m3MemRuntime(_mem);
    #define d_m3TracePrint(fmt, ...)            fprintf(stderr, "%*s" fmt "\n", (trace_rt->callDepth)*2, "", ##__VA_ARGS__)
#else
    #define d_m3TracePrepare
    #define d_m3TracePrint(fmt, ...)
#endif

#if d_m3EnableStrace >= 3
    #define d_m3TraceLoad(TYPE,offset,val)      d_m3TracePrint("load." #TYPE "  0x%x = %" PRI##TYPE, offset, val)
    #define d_m3TraceStore(TYPE,offset,val)     d_m3TracePrint("store." #TYPE " 0x%x , %" PRI##TYPE, offset, val)
#else
    #define d_m3TraceLoad(TYPE,offset,val)
    #define d_m3TraceStore(TYPE,offset,val)
#endif

d_m3RetSig  Call  (d_m3OpSig)
{
    m3ret_t possible_trap = m3_Yield ();
    if (UNLIKELY(possible_trap)) return possible_trap;

    nextOpDirect();
}

// TODO: OK, this needs some explanation here ;0

#define d_m3CommutativeOpMacro(RES, REG, TYPE, NAME, OP, ...) \
d_m3Op(TYPE##_##NAME##_rs)                              \
{                                                       \
    TYPE operand = slot (TYPE);                         \
    OP((RES), operand, ((TYPE) REG), ##__VA_ARGS__);    \
    nextOp ();                                          \
}                                                       \
d_m3Op(TYPE##_##NAME##_ss)                              \
{                                                       \
    TYPE operand2 = slot (TYPE);                        \
    TYPE operand1 = slot (TYPE);                        \
    OP((RES), operand1, operand2, ##__VA_ARGS__);       \
    nextOp ();                                          \
}

#define d_m3OpMacro(RES, REG, TYPE, NAME, OP, ...)      \
d_m3Op(TYPE##_##NAME##_sr)                              \
{                                                       \
    TYPE operand = slot (TYPE);                         \
    OP((RES), ((TYPE) REG), operand, ##__VA_ARGS__);    \
    nextOp ();                                          \
}                                                       \
d_m3CommutativeOpMacro(RES, REG, TYPE,NAME, OP, ##__VA_ARGS__)

// Accept macros
#define d_m3CommutativeOpMacro_i(TYPE, NAME, MACRO, ...)    d_m3CommutativeOpMacro  ( _r0,  _r0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3OpMacro_i(TYPE, NAME, MACRO, ...)               d_m3OpMacro             ( _r0,  _r0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3CommutativeOpMacro_f(TYPE, NAME, MACRO, ...)    d_m3CommutativeOpMacro  (_fp0, _fp0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3OpMacro_f(TYPE, NAME, MACRO, ...)               d_m3OpMacro             (_fp0, _fp0, TYPE, NAME, MACRO, ##__VA_ARGS__)

#define M3_FUNC(RES, A, B, OP)  (RES) = OP((A), (B))    // Accept functions: res = OP(a,b)
#define M3_OPER(RES, A, B, OP)  (RES) = ((A) OP (B))    // Accept operators: res = a OP b

#define d_m3CommutativeOpFunc_i(TYPE, NAME, OP)     d_m3CommutativeOpMacro_i    (TYPE, NAME, M3_FUNC, OP)
#define d_m3OpFunc_i(TYPE, NAME, OP)                d_m3OpMacro_i               (TYPE, NAME, M3_FUNC, OP)
#define d_m3CommutativeOpFunc_f(TYPE, NAME, OP)     d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_FUNC, OP)
#define d_m3OpFunc_f(TYPE, NAME, OP)                d_m3OpMacro_f               (TYPE, NAME, M3_FUNC, OP)

#define d_m3CommutativeOp_i(TYPE, NAME, OP)         d_m3CommutativeOpMacro_i    (TYPE, NAME, M3_OPER, OP)
#define d_m3Op_i(TYPE, NAME, OP)                    d_m3OpMacro_i               (TYPE, NAME, M3_OPER, OP)
#define d_m3CommutativeOp_f(TYPE, NAME, OP)         d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_OPER, OP)
#define d_m3Op_f(TYPE, NAME, OP)                    d_m3OpMacro_f               (TYPE, NAME, M3_OPER, OP)

// compare needs to be distinct for fp 'cause the result must be _r0
#define d_m3CompareOp_f(TYPE, NAME, OP)             d_m3OpMacro                 (_r0, _fp0, TYPE, NAME, M3_OPER, OP)
#define d_m3CommutativeCmpOp_f(TYPE, NAME, OP)      d_m3CommutativeOpMacro      (_r0, _fp0, TYPE, NAME, M3_OPER, OP)


//-----------------------

// signed
d_m3CommutativeOp_i (i32, Equal,            ==)     d_m3CommutativeOp_i (i64, Equal,            ==)
d_m3CommutativeOp_i (i32, NotEqual,         !=)     d_m3CommutativeOp_i (i64, NotEqual,         !=)

d_m3Op_i (i32, LessThan,                    < )     d_m3Op_i (i64, LessThan,                    < )
d_m3Op_i (i32, GreaterThan,                 > )     d_m3Op_i (i64, GreaterThan,                 > )
d_m3Op_i (i32, LessThanOrEqual,             <=)     d_m3Op_i (i64, LessThanOrEqual,             <=)
d_m3Op_i (i32, GreaterThanOrEqual,          >=)     d_m3Op_i (i64, GreaterThanOrEqual,          >=)

// unsigned
d_m3Op_i (u32, LessThan,                    < )     d_m3Op_i (u64, LessThan,                    < )
d_m3Op_i (u32, GreaterThan,                 > )     d_m3Op_i (u64, GreaterThan,                 > )
d_m3Op_i (u32, LessThanOrEqual,             <=)     d_m3Op_i (u64, LessThanOrEqual,             <=)
d_m3Op_i (u32, GreaterThanOrEqual,          >=)     d_m3Op_i (u64, GreaterThanOrEqual,          >=)

#if d_m3HasFloat
d_m3CommutativeCmpOp_f (f32, Equal,         ==)     d_m3CommutativeCmpOp_f (f64, Equal,         ==)
d_m3CommutativeCmpOp_f (f32, NotEqual,      !=)     d_m3CommutativeCmpOp_f (f64, NotEqual,      !=)
d_m3CompareOp_f (f32, LessThan,             < )     d_m3CompareOp_f (f64, LessThan,             < )
d_m3CompareOp_f (f32, GreaterThan,          > )     d_m3CompareOp_f (f64, GreaterThan,          > )
d_m3CompareOp_f (f32, LessThanOrEqual,      <=)     d_m3CompareOp_f (f64, LessThanOrEqual,      <=)
d_m3CompareOp_f (f32, GreaterThanOrEqual,   >=)     d_m3CompareOp_f (f64, GreaterThanOrEqual,   >=)
#endif

d_m3CommutativeOp_i (i32, Add,              +)      d_m3CommutativeOp_i (i64, Add,              +)
d_m3CommutativeOp_i (i32, Multiply,         *)      d_m3CommutativeOp_i (i64, Multiply,         *)

d_m3Op_i (i32, Subtract,                    -)      d_m3Op_i (i64, Subtract,                    -)

#define OP_SHL_32(X,N) ((X) << ((u32)(N) % 32))
#define OP_SHL_64(X,N) ((X) << ((u64)(N) % 64))
#define OP_SHR_32(X,N) ((X) >> ((u32)(N) % 32))
#define OP_SHR_64(X,N) ((X) >> ((u64)(N) % 64))

d_m3OpFunc_i (u32, ShiftLeft,       OP_SHL_32)      d_m3OpFunc_i (u64, ShiftLeft,       OP_SHL_64)
d_m3OpFunc_i (i32, ShiftRight,      OP_SHR_32)      d_m3OpFunc_i (i64, ShiftRight,      OP_SHR_64)
d_m3OpFunc_i (u32, ShiftRight,      OP_SHR_32)      d_m3OpFunc_i (u64, ShiftRight,      OP_SHR_64)

d_m3CommutativeOp_i (u32, And,              &)
d_m3CommutativeOp_i (u32, Or,               |)
d_m3CommutativeOp_i (u32, Xor,              ^)

d_m3CommutativeOp_i (u64, And,              &)
d_m3CommutativeOp_i (u64, Or,               |)
d_m3CommutativeOp_i (u64, Xor,              ^)

#if d_m3HasFloat
d_m3CommutativeOp_f (f32, Add,              +)      d_m3CommutativeOp_f (f64, Add,              +)
d_m3CommutativeOp_f (f32, Multiply,         *)      d_m3CommutativeOp_f (f64, Multiply,         *)
d_m3Op_f (f32, Subtract,                    -)      d_m3Op_f (f64, Subtract,                    -)
d_m3Op_f (f32, Divide,                      /)      d_m3Op_f (f64, Divide,                      /)
#endif

d_m3OpFunc_i(u32, Rotl, rotl32)
d_m3OpFunc_i(u32, Rotr, rotr32)
d_m3OpFunc_i(u64, Rotl, rotl64)
d_m3OpFunc_i(u64, Rotr, rotr64)

d_m3OpMacro_i(u32, Divide, OP_DIV_U);
d_m3OpMacro_i(i32, Divide, OP_DIV_S, INT32_MIN);
d_m3OpMacro_i(u64, Divide, OP_DIV_U);
d_m3OpMacro_i(i64, Divide, OP_DIV_S, INT64_MIN);

d_m3OpMacro_i(u32, Remainder, OP_REM_U);
d_m3OpMacro_i(i32, Remainder, OP_REM_S, INT32_MIN);
d_m3OpMacro_i(u64, Remainder, OP_REM_U);
d_m3OpMacro_i(i64, Remainder, OP_REM_S, INT64_MIN);

#if d_m3HasFloat
d_m3OpFunc_f(f32, Min, min_f32);
d_m3OpFunc_f(f32, Max, max_f32);
d_m3OpFunc_f(f64, Min, min_f64);
d_m3OpFunc_f(f64, Max, max_f64);

d_m3OpFunc_f(f32, CopySign, copysignf);
d_m3OpFunc_f(f64, CopySign, copysign);
#endif

// Unary operations
// Note: This macro follows the principle of d_m3OpMacro

#define d_m3UnaryMacro(RES, REG, TYPE, NAME, OP, ...)   \
d_m3Op(TYPE##_##NAME##_r)                           \
{                                                   \
    OP((RES), (TYPE) REG, ##__VA_ARGS__);           \
    nextOp ();                                      \
}                                                   \
d_m3Op(TYPE##_##NAME##_s)                           \
{                                                   \
    TYPE operand = slot (TYPE);                     \
    OP((RES), operand, ##__VA_ARGS__);              \
    nextOp ();                                      \
}

#define M3_UNARY(RES, X, OP) (RES) = OP(X)
#define d_m3UnaryOp_i(TYPE, NAME, OPERATION)        d_m3UnaryMacro( _r0,  _r0, TYPE, NAME, M3_UNARY, OPERATION)
#define d_m3UnaryOp_f(TYPE, NAME, OPERATION)        d_m3UnaryMacro(_fp0, _fp0, TYPE, NAME, M3_UNARY, OPERATION)

#if d_m3HasFloat
d_m3UnaryOp_f (f32, Abs,        fabsf);         d_m3UnaryOp_f (f64, Abs,        fabs);
d_m3UnaryOp_f (f32, Ceil,       ceilf);         d_m3UnaryOp_f (f64, Ceil,       ceil);
d_m3UnaryOp_f (f32, Floor,      floorf);        d_m3UnaryOp_f (f64, Floor,      floor);
d_m3UnaryOp_f (f32, Trunc,      truncf);        d_m3UnaryOp_f (f64, Trunc,      trunc);
d_m3UnaryOp_f (f32, Sqrt,       sqrtf);         d_m3UnaryOp_f (f64, Sqrt,       sqrt);
d_m3UnaryOp_f (f32, Nearest,    rintf);         d_m3UnaryOp_f (f64, Nearest,    rint);
d_m3UnaryOp_f (f32, Negate,     -);             d_m3UnaryOp_f (f64, Negate,     -);
#endif

#define OP_EQZ(x) ((x) == 0)

d_m3UnaryOp_i (i32, EqualToZero, OP_EQZ)
d_m3UnaryOp_i (i64, EqualToZero, OP_EQZ)

// clz(0), ctz(0) results are undefined for rest platforms, fix it
#if (defined(__i386__) || defined(__x86_64__)) && !(defined(__AVX2__) || (defined(__ABM__) && defined(__BMI__)))
    #define OP_CLZ_32(x) (UNLIKELY((x) == 0) ? 32 : __builtin_clz(x))
    #define OP_CTZ_32(x) (UNLIKELY((x) == 0) ? 32 : __builtin_ctz(x))
    // for 64-bit instructions branchless approach more preferable
    #define OP_CLZ_64(x) (__builtin_clzll((x) | (1LL <<  0)) + OP_EQZ(x))
    #define OP_CTZ_64(x) (__builtin_ctzll((x) | (1LL << 63)) + OP_EQZ(x))
#elif defined(__ppc__) || defined(__ppc64__)
// PowerPC is defined for __builtin_clz(0) and __builtin_ctz(0).
// See (https://github.com/aquynh/capstone/blob/master/MathExtras.h#L99)
    #define OP_CLZ_32(x) __builtin_clz(x)
    #define OP_CTZ_32(x) __builtin_ctz(x)
    #define OP_CLZ_64(x) __builtin_clzll(x)
    #define OP_CTZ_64(x) __builtin_ctzll(x)
#else
    #define OP_CLZ_32(x) (UNLIKELY((x) == 0) ? 32 : __builtin_clz(x))
    #define OP_CTZ_32(x) (UNLIKELY((x) == 0) ? 32 : __builtin_ctz(x))
    #define OP_CLZ_64(x) (UNLIKELY((x) == 0) ? 64 : __builtin_clzll(x))
    #define OP_CTZ_64(x) (UNLIKELY((x) == 0) ? 64 : __builtin_ctzll(x))
#endif

d_m3UnaryOp_i (u32, Clz, OP_CLZ_32)
d_m3UnaryOp_i (u64, Clz, OP_CLZ_64)

d_m3UnaryOp_i (u32, Ctz, OP_CTZ_32)
d_m3UnaryOp_i (u64, Ctz, OP_CTZ_64)

d_m3UnaryOp_i (u32, Popcnt, __builtin_popcount)
d_m3UnaryOp_i (u64, Popcnt, __builtin_popcountll)

#define OP_WRAP_I64(X) ((X) & 0x00000000ffffffff)

d_m3Op(i32_Wrap_i64_r)
{
    _r0 = OP_WRAP_I64((i64) _r0);
    nextOp ();
}

d_m3Op(i32_Wrap_i64_s)
{
    i64 operand = slot (i64);
    _r0 = OP_WRAP_I64(operand);
    nextOp ();
}

// Integer sign extension operations
#define OP_EXTEND8_S_I32(X)  ((int32_t)(int8_t)(X))
#define OP_EXTEND16_S_I32(X) ((int32_t)(int16_t)(X))
#define OP_EXTEND8_S_I64(X)  ((int64_t)(int8_t)(X))
#define OP_EXTEND16_S_I64(X) ((int64_t)(int16_t)(X))
#define OP_EXTEND32_S_I64(X) ((int64_t)(int32_t)(X))

d_m3UnaryOp_i (i32, Extend8_s,  OP_EXTEND8_S_I32)
d_m3UnaryOp_i (i32, Extend16_s, OP_EXTEND16_S_I32)
d_m3UnaryOp_i (i64, Extend8_s,  OP_EXTEND8_S_I64)
d_m3UnaryOp_i (i64, Extend16_s, OP_EXTEND16_S_I64)
d_m3UnaryOp_i (i64, Extend32_s, OP_EXTEND32_S_I64)

#define d_m3TruncMacro(DEST, SRC, TYPE, NAME, FROM, OP, ...)   \
d_m3Op(TYPE##_##NAME##_##FROM##_r_r)                \
{                                                   \
    OP((DEST), (FROM) SRC, ##__VA_ARGS__);          \
    nextOp ();                                      \
}                                                   \
d_m3Op(TYPE##_##NAME##_##FROM##_r_s)                \
{                                                   \
    FROM * stack = slot_ptr (FROM);                 \
    OP((DEST), (* stack), ##__VA_ARGS__);           \
    nextOp ();                                      \
}                                                   \
d_m3Op(TYPE##_##NAME##_##FROM##_s_r)                \
{                                                   \
    TYPE * dest = slot_ptr (TYPE);                  \
    OP((* dest), (FROM) SRC, ##__VA_ARGS__);        \
    nextOp ();                                      \
}                                                   \
d_m3Op(TYPE##_##NAME##_##FROM##_s_s)                \
{                                                   \
    FROM * stack = slot_ptr (FROM);                 \
    TYPE * dest = slot_ptr (TYPE);                  \
    OP((* dest), (* stack), ##__VA_ARGS__);         \
    nextOp ();                                      \
}

#if d_m3HasFloat
d_m3TruncMacro(_r0, _fp0, i32, Trunc, f32, OP_I32_TRUNC_F32)
d_m3TruncMacro(_r0, _fp0, u32, Trunc, f32, OP_U32_TRUNC_F32)
d_m3TruncMacro(_r0, _fp0, i32, Trunc, f64, OP_I32_TRUNC_F64)
d_m3TruncMacro(_r0, _fp0, u32, Trunc, f64, OP_U32_TRUNC_F64)

d_m3TruncMacro(_r0, _fp0, i64, Trunc, f32, OP_I64_TRUNC_F32)
d_m3TruncMacro(_r0, _fp0, u64, Trunc, f32, OP_U64_TRUNC_F32)
d_m3TruncMacro(_r0, _fp0, i64, Trunc, f64, OP_I64_TRUNC_F64)
d_m3TruncMacro(_r0, _fp0, u64, Trunc, f64, OP_U64_TRUNC_F64)

d_m3TruncMacro(_r0, _fp0, i32, TruncSat, f32, OP_I32_TRUNC_SAT_F32)
d_m3TruncMacro(_r0, _fp0, u32, TruncSat, f32, OP_U32_TRUNC_SAT_F32)
d_m3TruncMacro(_r0, _fp0, i32, TruncSat, f64, OP_I32_TRUNC_SAT_F64)
d_m3TruncMacro(_r0, _fp0, u32, TruncSat, f64, OP_U32_TRUNC_SAT_F64)

d_m3TruncMacro(_r0, _fp0, i64, TruncSat, f32, OP_I64_TRUNC_SAT_F32)
d_m3TruncMacro(_r0, _fp0, u64, TruncSat, f32, OP_U64_TRUNC_SAT_F32)
d_m3TruncMacro(_r0, _fp0, i64, TruncSat, f64, OP_I64_TRUNC_SAT_F64)
d_m3TruncMacro(_r0, _fp0, u64, TruncSat, f64, OP_U64_TRUNC_SAT_F64)
#endif

#define d_m3TypeModifyOp(REG_TO, REG_FROM, TO, NAME, FROM)  \
d_m3Op(TO##_##NAME##_##FROM##_r)                            \
{                                                           \
    REG_TO = (TO) ((FROM) REG_FROM);                        \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_s)                            \
{                                                           \
    FROM from = slot (FROM);                                \
    REG_TO = (TO) (from);                                   \
    nextOp ();                                              \
}

// Int to int
d_m3TypeModifyOp (_r0, _r0, i64, Extend, i32);
d_m3TypeModifyOp (_r0, _r0, i64, Extend, u32);

// Float to float
#if d_m3HasFloat
d_m3TypeModifyOp (_fp0, _fp0, f32, Demote, f64);
d_m3TypeModifyOp (_fp0, _fp0, f64, Promote, f32);
#endif

#define d_m3TypeConvertOp(REG_TO, REG_FROM, TO, NAME, FROM) \
d_m3Op(TO##_##NAME##_##FROM##_r_r)                          \
{                                                           \
    REG_TO = (TO) ((FROM) REG_FROM);                        \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_s_r)                          \
{                                                           \
    slot (TO) = (TO) ((FROM) REG_FROM);                     \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_r_s)                          \
{                                                           \
    FROM from = slot (FROM);                                \
    REG_TO = (TO) (from);                                   \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_s_s)                          \
{                                                           \
    FROM from = slot (FROM);                                \
    slot (TO) = (TO) (from);                                \
    nextOp ();                                              \
}

// Int to float
#if d_m3HasFloat
d_m3TypeConvertOp (_fp0, _r0, f64, Convert, i32);
d_m3TypeConvertOp (_fp0, _r0, f64, Convert, u32);
d_m3TypeConvertOp (_fp0, _r0, f64, Convert, i64);
d_m3TypeConvertOp (_fp0, _r0, f64, Convert, u64);

d_m3TypeConvertOp (_fp0, _r0, f32, Convert, i32);
d_m3TypeConvertOp (_fp0, _r0, f32, Convert, u32);
d_m3TypeConvertOp (_fp0, _r0, f32, Convert, i64);
d_m3TypeConvertOp (_fp0, _r0, f32, Convert, u64);
#endif

#define d_m3ReinterpretOp(REG, TO, SRC, FROM)               \
d_m3Op(TO##_Reinterpret_##FROM##_r_r)                       \
{                                                           \
    union { FROM c; TO t; } u;                              \
    u.c = (FROM) SRC;                                       \
    REG = u.t;                                              \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_Reinterpret_##FROM##_r_s)                       \
{                                                           \
    union { FROM c; TO t; } u;                              \
    u.c = slot (FROM);                                      \
    REG = u.t;                                              \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_Reinterpret_##FROM##_s_r)                       \
{                                                           \
    union { FROM c; TO t; } u;                              \
    u.c = (FROM) SRC;                                       \
    slot (TO) = u.t;                                        \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_Reinterpret_##FROM##_s_s)                       \
{                                                           \
    union { FROM c; TO t; } u;                              \
    u.c = slot (FROM);                                      \
    slot (TO) = u.t;                                        \
    nextOp ();                                              \
}

#if d_m3HasFloat
d_m3ReinterpretOp (_r0, i32, _fp0, f32)
d_m3ReinterpretOp (_r0, i64, _fp0, f64)
d_m3ReinterpretOp (_fp0, f32, _r0, i32)
d_m3ReinterpretOp (_fp0, f64, _r0, i64)
#endif


d_m3Op  (GetGlobal_s32)
{
    u32 * global = immediate (u32 *);
    slot (u32) = * global;                        //  printf ("get global: %p %" PRIi64 "\n", global, *global);

    nextOp ();
}


d_m3Op  (GetGlobal_s64)
{
    u64 * global = immediate (u64 *);
    slot (u64) = * global;                        // printf ("get global: %p %" PRIi64 "\n", global, *global);

    nextOp ();
}


d_m3Op  (SetGlobal_i32)
{
    u32 * global = immediate (u32 *);
    * global = (u32) _r0;                         //  printf ("set global: %p %" PRIi64 "\n", global, _r0);

    nextOp ();
}


d_m3Op  (SetGlobal_i64)
{
    u64 * global = immediate (u64 *);
    * global = (u64) _r0;                         //  printf ("set global: %p %" PRIi64 "\n", global, _r0);

    nextOp ();
}


d_m3Op  (Call)
{
    pc_t callPC                 = immediate (pc_t);
    i32 stackOffset             = immediate (i32);
    IM3Memory memory            = m3MemInfo (_mem);

    m3stack_t sp = _sp + stackOffset;

    m3ret_t r = Call (callPC, sp, _mem, d_m3OpDefaultArgs);
    _mem = memory->mallocated;

    if (LIKELY(not r))
        nextOp ();
    else
    {
        pushBacktraceFrame ();
        forwardTrap (r);
    }
}


d_m3Op  (CallIndirect)
{
    u32 tableIndex              = slot (u32);
    IM3Module module            = immediate (IM3Module);
    IM3FuncType type            = immediate (IM3FuncType);
    i32 stackOffset             = immediate (i32);
    IM3Memory memory            = m3MemInfo (_mem);

    m3stack_t sp = _sp + stackOffset;

    m3ret_t r = m3Err_none;

    if (LIKELY(tableIndex < module->table0Size))
    {
        IM3Function function = module->table0 [tableIndex];

        if (LIKELY(function))
        {
            if (LIKELY(type == function->funcType))
            {
                if (UNLIKELY(not function->compiled))
                    r = Compile_Function (function);

                if (LIKELY(not r))
                {
                    r = Call (function->compiled, sp, _mem, d_m3OpDefaultArgs);
                    _mem = memory->mallocated;

                    if (LIKELY(not r))
                        nextOpDirect ();
                    else
                    {
                        pushBacktraceFrame ();
                        forwardTrap (r);
                    }
                }
            }
            else r = m3Err_trapIndirectCallTypeMismatch;
        }
        else r = m3Err_trapTableElementIsNull;
    }
    else r = m3Err_trapTableIndexOutOfRange;

    if (UNLIKELY(r))
        newTrap (r);
    else forwardTrap (r);
}


d_m3Op  (CallRawFunction)
{
    d_m3TracePrepare

    M3ImportContext ctx;

    M3RawCall call = (M3RawCall) (* _pc++);
    ctx.function = immediate (IM3Function);
    ctx.userdata = immediate (void *);
    u64* const sp = ((u64*)_sp);
    IM3Memory memory = m3MemInfo (_mem);

    IM3Runtime runtime = m3MemRuntime(_mem);

#if d_m3EnableStrace
    IM3FuncType ftype = ctx.function->funcType;

    FILE* out = stderr;
    char outbuff[1024];
    char* outp = outbuff;
    char* oute = outbuff+1024;

    outp += snprintf(outp, oute-outp, "%s!%s(", ctx.function->import.moduleUtf8, ctx.function->import.fieldUtf8);

    const int nArgs = ftype->numArgs;
    const int nRets = ftype->numRets;
    for (int i=0; i<nArgs; i++) {
        const int type = ftype->types[nRets + i];
        switch (type) {
        case c_m3Type_i32:  outp += snprintf(outp, oute-outp, "%" PRIi32, *(i32*)(sp+i)); break;
        case c_m3Type_i64:  outp += snprintf(outp, oute-outp, "%" PRIi64, *(i64*)(sp+i)); break;
        case c_m3Type_f32:  outp += snprintf(outp, oute-outp, "%" PRIf32, *(f32*)(sp+i)); break;
        case c_m3Type_f64:  outp += snprintf(outp, oute-outp, "%" PRIf64, *(f64*)(sp+i)); break;
        default:            outp += snprintf(outp, oute-outp, "<type %d>", type);         break;
        }
