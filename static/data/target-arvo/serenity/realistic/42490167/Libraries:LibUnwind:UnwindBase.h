/*
 * Copyright (c) 2020, Andrew Kaster <andrewdkaster@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>

// The following definitions and comments are largely pulled from the Itanium Exception handling ABI
// Reference: https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.htm, version 1.22
// This header defines the methods, types, and constants outlined in Level I. Base ABI
// Also, from the Intel386 psABI version 1.1
// Reference: https://github.com/hjl-tools/x86-psABI/wiki/X86-psABI

// FIXME: Configure gcc and libgcc with --with-system-libunwind :^)

extern "C" {

/// Return code used by _Unwind family of methods
enum _Unwind_Reason_Code {
    _URC_NO_REASON = 0,                /// Forced unwind stop routine determined this isn't the correct frame
    _URC_FOREIGN_EXCEPTION_CAUGHT = 1, /// This indicates that a different runtime caught this exception.
                                       ///     Nested foreign exceptions, or rethrowing a foreign exception,
                                       ///     result in undefined behaviour.
    _URC_FATAL_PHASE2_ERROR = 2,       /// The personality routine encountered an error during phase 2,
                                       ///     for instance a stack corruption (call std::terminate())
    _URC_FATAL_PHASE1_ERROR = 3,       /// The personality routine encountered an error during phase 1,
                                       ///     other than the specific error codes defined.
    _URC_NORMAL_STOP = 4,              /// Exception handled(?) Spec doesn't say...
    _URC_END_OF_STACK = 5,             /// Reached the top of the stack without a handler :(
    _URC_HANDLER_FOUND = 6,            /// Success! End phase 1 lookup, continue to phase 2
    _URC_INSTALL_CONTEXT = 7,          /// Personality routine wants the context created
    _URC_CONTINUE_UNWIND = 8           /// Destructors etc called, keep walking up stack until we reach handler
};

// Forward declaration for cleanup method below
struct _Unwind_Exception;

/// Method that knows how to destroy a particualar exception. Called when handling a foreign exception
typedef void (*_Unwind_Exception_Cleanup_Fn)(_Unwind_Reason_Code reason, struct _Unwind_Exception* exc);

/// Exception object, language agnostic
/// @note: _Unwind_Exception must be double word aligned
/// @note: the private vars are different sizes on 64-bit...
struct _Unwind_Exception {
    uint64_t exception_class;                       ///< For identification. For c++, the four low bytes are "C++\0"
    _Unwind_Exception_Cleanup_Fn exception_cleanup; ///< Used if caught by a different runtime
    uint32_t private_1;                             ///< Private for system implementation
    uint32_t private_2;                             ///< Private for system implementation
    // Note that for C++, the language specific exception object will be located directly following this header
};

/// Opaque typedef to system unwinder implementation class
struct _Unwind_Context;

/// Raise an exception, nominally noreturn (i.e. if we return, we've got trouble)
/// @param exception_object Exception that was allocated by language specific runtime
///     It must have its exception_class and cleanup fields set
/// @returns One of the following reason codes:
///     * _URC_END_OF_STACK : No handler found during phase 1 lookup. (uncaught_exception())
///     * _URC_FATAL_PHASE1_ERROR: Stack corruption during phase 1 lookup. (terminate())
///
/// @note If the unwinder can't do phase 2 cleanup, it should return _URC_FATAL_PHASE2_ERROR
/// @note The caller of _Unwind_RaiseException can make no assumptions about the state of its stack or registers.
///
_Unwind_Reason_Code _Unwind_RaiseException(struct _Unwind_Exception* exception_object);

/// Resume propagation of an existing exception e.g. after executing cleanup code in a partially unwound stack.
/// A call to this routine is inserted at the end of a landing pad that performed cleanup, but did not resume normal
/// execution. It causes unwinding to proceed further.
///
/// @note  _Unwind_Resume should not be used to implement rethrowing. To the unwinding runtime,/
///     the catch code that rethrows was a handler, and the previous unwinding session was terminated before entering.
///     Rethrowing is implemented by calling _Unwind_RaiseException again with the same exception object.
///     This means that re-throwing an exception causes the exception handing process to begin again at phase 1.
/// @note This is the only routine in the unwind library which is expected to be called directly by generated code:
///     it will be called at the end of a landing pad in a "landing-pad" model.
///
void _Unwind_Resume(struct _Unwind_Exception* exception_object);

/// Deletes the given exception object.
/// If a given runtime resumes normal execution after catching a foreign exception, it will not know how to delete
/// that exception. Such an exception will be deleted by calling _Unwind_DeleteException.
/// This is a convenience function that calls the function pointed to by the exception_cleanup field of the exception
/// header.
///
void _Unwind_DeleteException(struct _Unwind_Exception* exception_object);

//-------------
// Context management routines
//
// Used for communicating information about the unwind context between the unwind library, personality routine,
// and compiler-generated landing pad.
// They include routines to read or set the context record images of registers in the stack frame corresponding
// to a given unwind context, and to identify the location of the current unwind descriptors and unwind frame.
//-------------

/// Get the 32-bit value of the given general register
/// The register is identified by its index in the DWARF register mapping
///
/// During the two phases of unwinding, no registers have a guaranteed value.
///
uint32_t _Unwind_GetGR(struct _Unwind_Context* context, int index);

/// Set the 32-bit value of the given register
/// The register identified by its index as for _Unwind_GetGR.
///
/// The behaviour is guaranteed only if the function is called during phase 2 of unwinding,
/// and applied to an unwind context representing a handler frame, for which the personality routine will return
/// _URC_INSTALL_CONTEXT. In that case, only registers %eax and %edx should be used.
/// These scratch registers are reserved for passing arguments between the personality routine and the landing pads.
///
void _Unwind_SetGR(struct _Unwind_Context* context, int index, uint32_t new_value);

/// Get the 32-bit value of the instruction pointer (IP).
///
/// During unwinding, the value is guaranteed to be the address of the instruction immediately following the call site
/// in the function identified by the unwind context. This value may be outside of the procedure fragment for
/// a function call that is known to not return (such as _Unwind_Resume).
///
uint32_t _Unwind_GetIP(struct _Unwind_Context* context);

/// Set the value of the instruction pointer (IP) for the routine identified by the unwind context.
///
/// The behaviour is guaranteed only when this function is called for an unwind context representing a handler frame,
/// for which the personality routine will return _URC_INSTALL_CONTEXT.
/// In this case, control will be transferred to the given address, which should be the address of a landing pad.
///
void _Unwind_SetIP(struct _Unwind_Context* context, uint32_t new_value);

/// Get language specific data area for the current stack frame.
/// Useful for retrieving information that was cached after finding the personality routine.
///
uint32_t _Unwind_GetLanguageSpecificData(struct _Unwind_Context* context);

/// Get the address of the beginning of the procedure or code fragment described by the current unwind descriptor
/// block.
///
/// This information is required to access any data stored relative to the beginning
/// of the procedure fragment. For instance, a call site table might be stored relative
/// to the beginning of the procedure fragment that contains the calls.
/// During unwinding, the function returns the start of the procedure fragment containing the call site in the current
/// stack frame.
///
uint32_t _Unwind_GetRegionStart(struct _Unwind_Context* context);

/// This function returns the 32-bit Canonical Frame Address which is defined as
/// the value of %esp at the call site in the previous frame. This value is guaranteed
/// to be correct any time the context has been passed to a personality routine or a
/// stop function.
uint32_t _Unwind_GetCFA(struct _Unwind_Context* context);

/// Personality routine
/// The personality routine is the function in the C++ (or other language) runtime library which serves
/// as an interface between the system unwind library and
/// language-specific exception handling semantics. It is specific to the code fragment
/// described by an unwind info block, and it is always referenced via the pointer in
/// the unwind info block, and hence it has no psABI-specified name.
/// @note Both GCC and Clang will generate .cfi_personality directives for __gxx_personality_v0
///     Unless we tell the compiler we're using setjump/longjump exceptions (ew, how 90's)
///
/// @param version Better be 1
/// @param actions See below
/// @param exceptionClass [ vendor ][ language ] e.g. "CLNGC++\0" "GNUCC++\0"
/// @param exceptionObject language-specific exception
/// @param context Opaque handle for personality routine + unwinder lib state
///
typedef _Unwind_Reason_Code (*__personality_routine)(int version,
    _Unwind_Action actions,
    uint64_t exceptionClass,
    struct _Unwind_Exception* exceptionObject,
    struct _Unwind_Context* context);

/// Action parameter type for personality routine during unwind. Will be a bit-wise OR of the __UnwindActions below
typedef int _Unwind_Action;

/// Which action(s) the personality routine should perform duing unwind
enum __UnwindActions {
    _UA_SEARCH_PHASE = 1,  /// The personality routine should check if the current frame contains a handler,
                           ///    and if so return _URC_HANDLER_FOUND, or otherwise return _URC_CONTINUE_UNWIND.
                           ///    _UA_SEARCH_PHASE cannot be set at the same time as _UA_CLEANUP_PHASE.
    _UA_CLEANUP_PHASE = 2, /// The personality routine should perform cleanup for the current frame.
