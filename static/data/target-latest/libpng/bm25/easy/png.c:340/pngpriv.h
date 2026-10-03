/* pngpriv.h - private declarations for use inside libpng
 *
 * Copyright (c) 2018-2024 Cosmin Truta
 * Copyright (c) 1998-2002,2004,2006-2018 Glenn Randers-Pehrson
 * Copyright (c) 1996-1997 Andreas Dilger
 * Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 */

/* The symbols declared in this file (including the functions declared
 * as extern) are PRIVATE.  They are not part of the libpng public
 * interface, and are not recommended for use by regular applications.
 * Some of them may become public in the future; others may stay private,
 * change in an incompatible way, or even disappear.
 * Although the libpng users are not forbidden to include this header,
 * they should be well aware of the issues that may arise from doing so.
 */

#ifndef PNGPRIV_H
#define PNGPRIV_H

/* Feature Test Macros.  The following are defined here to ensure that correctly
 * implemented libraries reveal the APIs libpng needs to build and hide those
 * that are not needed and potentially damaging to the compilation.
 *
 * Feature Test Macros must be defined before any system header is included (see
 * POSIX 1003.1 2.8.2 "POSIX Symbols."
 *
 * These macros only have an effect if the operating system supports either
 * POSIX 1003.1 or C99, or both.  On other operating systems (particularly
 * Windows/Visual Studio) there is no effect; the OS specific tests below are
 * still required (as of 2011-05-02.)
 */
#ifndef _POSIX_SOURCE
#  define _POSIX_SOURCE 1 /* Just the POSIX 1003.1 and C89 APIs */
#endif

#ifndef PNG_VERSION_INFO_ONLY
/* Standard library headers not required by png.h: */
#  include <stdlib.h>
#  include <string.h>
#endif

#define PNGLIB_BUILD /*libpng is being built, not used*/

/* If HAVE_CONFIG_H is defined during the build then the build system must
 * provide an appropriate "config.h" file on the include path.  The header file
 * must provide definitions as required below (search for "HAVE_CONFIG_H");
 * see configure.ac for more details of the requirements.  The macro
 * "PNG_NO_CONFIG_H" is provided for maintainers to test for dependencies on
 * 'configure'; define this macro to prevent the configure build including the
 * configure generated config.h.  Libpng is expected to compile without *any*
 * special build system support on a reasonably ANSI-C compliant system.
 */
#if defined(HAVE_CONFIG_H) && !defined(PNG_NO_CONFIG_H)
#  include <config.h>

   /* Pick up the definition of 'restrict' from config.h if it was read: */
#  define PNG_RESTRICT restrict
#endif

/* To support symbol prefixing it is necessary to know *before* including png.h
 * whether the fixed point (and maybe other) APIs are exported, because if they
 * are not internal definitions may be required.  This is handled below just
 * before png.h is included, but load the configuration now if it is available.
 */
#ifndef PNGLCONF_H
#  include "pnglibconf.h"
#endif

/* Local renames may change non-exported API functions from png.h */
#if defined(PNG_PREFIX) && !defined(PNGPREFIX_H)
#  include "pngprefix.h"
#endif

#ifdef PNG_USER_CONFIG
#  include "pngusr.h"
   /* These should have been defined in pngusr.h */
#  ifndef PNG_USER_PRIVATEBUILD
#    define PNG_USER_PRIVATEBUILD "Custom libpng build"
#  endif
#  ifndef PNG_USER_DLLFNAME_POSTFIX
#    define PNG_USER_DLLFNAME_POSTFIX "Cb"
#  endif
#endif

/* Compile time options.
 * =====================
 * In a multi-arch build the compiler may compile the code several times for the
 * same object module, producing different binaries for different architectures.
 * When this happens configure-time setting of the target host options cannot be
 * done and this interferes with the handling of the ARM NEON optimizations, and
 * possibly other similar optimizations.  Put additional tests here; in general
 * this is needed when the same option can be changed at both compile time and
 * run time depending on the target OS (i.e. iOS vs Android.)
 *
 * NOTE: symbol prefixing does not pass $(CFLAGS) to the preprocessor, because
 * this is not possible with certain compilers (Oracle SUN OS CC), as a result
 * it is necessary to ensure that all extern functions that *might* be used
 * regardless of $(CFLAGS) get declared in this file.  The test on __ARM_NEON__
 * below is one example of this behavior because it is controlled by the
 * presence or not of -mfpu=neon on the GCC command line, it is possible to do
 * this in $(CC), e.g. "CC=gcc -mfpu=neon", but people who build libpng rarely
 * do this.
 */
#ifndef PNG_ARM_NEON_OPT
   /* ARM NEON optimizations are being controlled by the compiler settings,
    * typically the target FPU.  If the FPU has been set to NEON (-mfpu=neon
    * with GCC) then the compiler will define __ARM_NEON__ and we can rely
    * unconditionally on NEON instructions not crashing, otherwise we must
    * disable use of NEON instructions.
    *
    * NOTE: at present these optimizations depend on 'ALIGNED_MEMORY', so they
    * can only be turned on automatically if that is supported too.  If
    * PNG_ARM_NEON_OPT is set in CPPFLAGS (to >0) then arm/arm_init.c will fail
    * to compile with an appropriate #error if ALIGNED_MEMORY has been turned
    * off.
    *
    * Note that gcc-4.9 defines __ARM_NEON instead of the deprecated
    * __ARM_NEON__, so we check both variants.
    *
    * To disable ARM_NEON optimizations entirely, and skip compiling the
    * associated assembler code, pass --enable-arm-neon=no to configure
    * or put -DPNG_ARM_NEON_OPT=0 in CPPFLAGS.
    */
#  if (defined(__ARM_NEON__) || defined(__ARM_NEON)) && \
   defined(PNG_ALIGNED_MEMORY_SUPPORTED)
#     define PNG_ARM_NEON_OPT 2
#  else
#     define PNG_ARM_NEON_OPT 0
#  endif
#endif

#if PNG_ARM_NEON_OPT > 0
   /* NEON optimizations are to be at least considered by libpng, so enable the
    * callbacks to do this.
    */
#  define PNG_FILTER_OPTIMIZATIONS png_init_filter_functions_neon
#  ifndef PNG_ARM_NEON_IMPLEMENTATION
      /* Use the intrinsics code by default. */
#     define PNG_ARM_NEON_IMPLEMENTATION 1
#  endif
#else /* PNG_ARM_NEON_OPT == 0 */
#     define PNG_ARM_NEON_IMPLEMENTATION 0
#endif /* PNG_ARM_NEON_OPT > 0 */

#ifndef PNG_MIPS_MSA_OPT
#  if defined(__mips_msa) && (__mips_isa_rev >= 5) && \
   defined(PNG_ALIGNED_MEMORY_SUPPORTED)
#     define PNG_MIPS_MSA_OPT 2
#  else
#     define PNG_MIPS_MSA_OPT 0
#  endif
#endif

#ifndef PNG_MIPS_MMI_OPT
#  ifdef PNG_MIPS_MMI
#    if defined(__mips_loongson_mmi) && (_MIPS_SIM == _ABI64) && \
     defined(PNG_ALIGNED_MEMORY_SUPPORTED)
#       define PNG_MIPS_MMI_OPT 1
#    else
#       define PNG_MIPS_MMI_OPT 0
#    endif
#  else
#    define PNG_MIPS_MMI_OPT 0
#  endif
#endif

#ifndef PNG_POWERPC_VSX_OPT
#  if defined(__PPC64__) && defined(__ALTIVEC__) && defined(__VSX__)
#     define PNG_POWERPC_VSX_OPT 2
#  else
#     define PNG_POWERPC_VSX_OPT 0
#  endif
#endif

#ifndef PNG_LOONGARCH_LSX_OPT
#  if defined(__loongarch_sx)
#     define PNG_LOONGARCH_LSX_OPT 1
#  else
#     define PNG_LOONGARCH_LSX_OPT 0
#  endif
#endif

#ifndef PNG_INTEL_SSE_OPT
#   ifdef PNG_INTEL_SSE
      /* Only check for SSE if the build configuration has been modified to
       * enable SSE optimizations.  This means that these optimizations will
       * be off by default.  See contrib/intel for more details.
       */
#      if defined(__SSE4_1__) || defined(__AVX__) || defined(__SSSE3__) || \
       defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || \
       (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#         define PNG_INTEL_SSE_OPT 1
#      else
#         define PNG_INTEL_SSE_OPT 0
#      endif
#   else
#      define PNG_INTEL_SSE_OPT 0
#   endif
#endif

#if PNG_INTEL_SSE_OPT > 0
#   ifndef PNG_INTEL_SSE_IMPLEMENTATION
#      if defined(__SSE4_1__) || defined(__AVX__)
          /* We are not actually using AVX, but checking for AVX is the best
             way we can detect SSE4.1 and SSSE3 on MSVC.
          */
#         define PNG_INTEL_SSE_IMPLEMENTATION 3
#      elif defined(__SSSE3__)
#         define PNG_INTEL_SSE_IMPLEMENTATION 2
#      elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || \
       (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#         define PNG_INTEL_SSE_IMPLEMENTATION 1
#      else
#         define PNG_INTEL_SSE_IMPLEMENTATION 0
#      endif
#   endif

#   if PNG_INTEL_SSE_IMPLEMENTATION > 0
#      define PNG_FILTER_OPTIMIZATIONS png_init_filter_functions_sse2
#   endif
#else
#   define PNG_INTEL_SSE_IMPLEMENTATION 0
#endif

#if PNG_MIPS_MSA_OPT > 0
#  ifndef PNG_MIPS_MSA_IMPLEMENTATION
#     if defined(__mips_msa)
#        if defined(__clang__)
#        elif defined(__GNUC__)
#           if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
#              define PNG_MIPS_MSA_IMPLEMENTATION 2
#           endif /* no GNUC support */
#        endif /* __GNUC__ */
#     else /* !defined __mips_msa */
#        define PNG_MIPS_MSA_IMPLEMENTATION 2
#     endif /* __mips_msa */
#  endif /* !PNG_MIPS_MSA_IMPLEMENTATION */

#  ifndef PNG_MIPS_MSA_IMPLEMENTATION
#     define PNG_MIPS_MSA_IMPLEMENTATION 1
#     define PNG_FILTER_OPTIMIZATIONS png_init_filter_functions_mips
#  endif
#else
#  define PNG_MIPS_MSA_IMPLEMENTATION 0
#endif /* PNG_MIPS_MSA_OPT > 0 */

#if PNG_MIPS_MMI_OPT > 0
#  ifndef PNG_MIPS_MMI_IMPLEMENTATION
#     if defined(__mips_loongson_mmi) && (_MIPS_SIM == _ABI64)
#        define PNG_MIPS_MMI_IMPLEMENTATION 2
#     else /* !defined __mips_loongson_mmi  || _MIPS_SIM != _ABI64 */
#        define PNG_MIPS_MMI_IMPLEMENTATION 0
#     endif /* __mips_loongson_mmi  && _MIPS_SIM == _ABI64 */
#  endif /* !PNG_MIPS_MMI_IMPLEMENTATION */

#   if PNG_MIPS_MMI_IMPLEMENTATION > 0
#      define PNG_FILTER_OPTIMIZATIONS png_init_filter_functions_mips
#   endif
#else
#   define PNG_MIPS_MMI_IMPLEMENTATION 0
#endif /* PNG_MIPS_MMI_OPT > 0 */

#if PNG_POWERPC_VSX_OPT > 0
#  define PNG_FILTER_OPTIMIZATIONS png_init_filter_functions_vsx
#  define PNG_POWERPC_VSX_IMPLEMENTATION 1
#else
#  define PNG_POWERPC_VSX_IMPLEMENTATION 0
#endif

#if PNG_LOONGARCH_LSX_OPT > 0
#   define PNG_FILTER_OPTIMIZATIONS png_init_filter_functions_lsx
#   define PNG_LOONGARCH_LSX_IMPLEMENTATION 1
#else
#   define PNG_LOONGARCH_LSX_IMPLEMENTATION 0
#endif

/* Is this a build of a DLL where compilation of the object modules requires
 * different preprocessor settings to those required for a simple library?  If
 * so PNG_BUILD_DLL must be set.
 *
 * If libpng is used inside a DLL but that DLL does not export the libpng APIs
 * PNG_BUILD_DLL must not be set.  To avoid the code below kicking in build a
 * static library of libpng then link the DLL against that.
 */
#ifndef PNG_BUILD_DLL
#  ifdef DLL_EXPORT
      /* This is set by libtool when files are compiled for a DLL; libtool
       * always compiles twice, even on systems where it isn't necessary.  Set
       * PNG_BUILD_DLL in case it is necessary:
       */
#     define PNG_BUILD_DLL
#  else
#     ifdef _WINDLL
         /* This is set by the Microsoft Visual Studio IDE in projects that
          * build a DLL.  It can't easily be removed from those projects (it
          * isn't visible in the Visual Studio UI) so it is a fairly reliable
          * indication that PNG_IMPEXP needs to be set to the DLL export
          * attributes.
          */
#        define PNG_BUILD_DLL
#     else
#        ifdef __DLL__
            /* This is set by the Borland C system when compiling for a DLL
             * (as above.)
             */
#           define PNG_BUILD_DLL
#        else
            /* Add additional compiler cases here. */
#        endif
#     endif
#  endif
#endif /* Setting PNG_BUILD_DLL if required */

/* See pngconf.h for more details: the builder of the library may set this on
 * the command line to the right thing for the specific compilation system or it
 * may be automagically set above (at present we know of no system where it does
 * need to be set on the command line.)
 *
 * PNG_IMPEXP must be set here when building the library to prevent pngconf.h
 * setting it to the "import" setting for a DLL build.
 */
#ifndef PNG_IMPEXP
#  ifdef PNG_BUILD_DLL
#     define PNG_IMPEXP PNG_DLL_EXPORT
#  else
      /* Not building a DLL, or the DLL doesn't require specific export
       * definitions.
       */
#     define PNG_IMPEXP
#  endif
#endif

/* No warnings for private or deprecated functions in the build: */
#ifndef PNG_DEPRECATED
#  define PNG_DEPRECATED
#endif
#ifndef PNG_PRIVATE
#  define PNG_PRIVATE
#endif

/* Symbol preprocessing support.
 *
 * To enable listing global, but internal, symbols the following macros should
 * always be used to declare an extern data or function object in this file.
 */
#ifndef PNG_INTERNAL_DATA
#  define PNG_INTERNAL_DATA(type, name, array) PNG_LINKAGE_DATA type name array
#endif

#ifndef PNG_INTERNAL_FUNCTION
#  define PNG_INTERNAL_FUNCTION(type, name, args, attributes)\
      PNG_LINKAGE_FUNCTION PNG_FUNCTION(type, name, args, PNG_EMPTY attributes)
#endif

#ifndef PNG_INTERNAL_CALLBACK
#  define PNG_INTERNAL_CALLBACK(type, name, args, attributes)\
      PNG_LINKAGE_CALLBACK PNG_FUNCTION(type, (PNGCBAPI name), args,\
         PNG_EMPTY attributes)
#endif

/* If floating or fixed point APIs are disabled they may still be compiled
 * internally.  To handle this make sure they are declared as the appropriate
 * internal extern function (otherwise the symbol prefixing stuff won't work and
 * the functions will be used without definitions.)
 *
 * NOTE: although all the API functions are declared here they are not all
 * actually built!  Because the declarations are still made it is necessary to
 * fake out types that they depend on.
 */
#ifndef PNG_FP_EXPORT
#  ifndef PNG_FLOATING_POINT_SUPPORTED
#     define PNG_FP_EXPORT(ordinal, type, name, args)\
         PNG_INTERNAL_FUNCTION(type, name, args, PNG_EMPTY);
#     ifndef PNG_VERSION_INFO_ONLY
         typedef struct png_incomplete png_double;
         typedef png_double*           png_doublep;
         typedef const png_double*     png_const_doublep;
         typedef png_double**          png_doublepp;
#     endif
#  endif
#endif
#ifndef PNG_FIXED_EXPORT
#  ifndef PNG_FIXED_POINT_SUPPORTED
#     define PNG_FIXED_EXPORT(ordinal, type, name, args)\
         PNG_INTERNAL_FUNCTION(type, name, args, PNG_EMPTY);
#  endif
#endif

#include "png.h"

/* pngconf.h does not set PNG_DLL_EXPORT unless it is required, so: */
#ifndef PNG_DLL_EXPORT
#  define PNG_DLL_EXPORT
#endif

/* This is a global switch to set the compilation for an installed system
 * (a release build).  It can be set for testing debug builds to ensure that
 * they will compile when the build type is switched to RC or STABLE, the
 * default is just to use PNG_LIBPNG_BUILD_BASE_TYPE.  Set this in CPPFLAGS
 * with either:
 *
 *   -DPNG_RELEASE_BUILD Turns on the release compile path
 *   -DPNG_RELEASE_BUILD=0 Turns it off
 * or in your pngusr.h with
 *   #define PNG_RELEASE_BUILD=1 Turns on the release compile path
 *   #define PNG_RELEASE_BUILD=0 Turns it off
 */
#ifndef PNG_RELEASE_BUILD
#  define PNG_RELEASE_BUILD (PNG_LIBPNG_BUILD_BASE_TYPE >= PNG_LIBPNG_BUILD_RC)
#endif

/* SECURITY and SAFETY:
 *
 * libpng is built with support for internal limits on image dimensions and
 * memory usage.  These are documented in scripts/pnglibconf.dfa of the
 * source and recorded in the machine generated header file pnglibconf.h.
 */

/* If you are running on a machine where you cannot allocate more
 * than 64K of memory at once, uncomment this.  While libpng will not
 * normally need that much memory in a chunk (unless you load up a very
 * large file), zlib needs to know how big of a chunk it can use, and
 * libpng thus makes sure to check any memory allocation to verify it
 * will fit into memory.
 *
 * zlib provides 'MAXSEG_64K' which, if defined, indicates the
 * same limit and pngconf.h (already included) sets the limit
 * if certain operating systems are detected.
 */
#if defined(MAXSEG_64K) && !defined(PNG_MAX_MALLOC_64K)
#  define PNG_MAX_MALLOC_64K
#endif

#ifndef PNG_UNUSED
/* Unused formal parameter warnings are silenced using the following macro
 * which is expected to have no bad effects on performance (optimizing
 * compilers will probably remove it entirely).  Note that if you replace
 * it with something other than whitespace, you must include the terminating
 * semicolon.
 */
#  define PNG_UNUSED(param) (void)param;
#endif

/* Just a little check that someone hasn't tried to define something
 * contradictory.
 */
#if (PNG_ZBUF_SIZE > 65536L) && defined(PNG_MAX_MALLOC_64K)
#  undef PNG_ZBUF_SIZE
#  define PNG_ZBUF_SIZE 65536L
#endif

/* If warnings or errors are turned off the code is disabled or redirected here.
 * From 1.5.4 functions have been added to allow very limited formatting of
 * error and warning messages - this code will also be disabled here.
 */
#ifdef PNG_WARNINGS_SUPPORTED
#  define PNG_WARNING_PARAMETERS(p) png_warning_parameters p;
#else
#  define png_warning_parameter(p,number,string) ((void)0)
#  define png_warning_parameter_unsigned(p,number,format,value) ((void)0)
#  define png_warning_parameter_signed(p,number,format,value) ((void)0)
#  define png_formatted_warning(pp,p,message) ((void)(pp))
#  define PNG_WARNING_PARAMETERS(p)
#endif
#ifndef PNG_ERROR_TEXT_SUPPORTED
#  define png_fixed_error(s1,s2) png_err(s1)
#endif

/* Some fixed point APIs are still required even if not exported because
 * they get used by the corresponding floating point APIs.  This magic
 * deals with this:
 */
#ifdef PNG_FIXED_POINT_SUPPORTED
#  define PNGFAPI PNGAPI
#else
#  define PNGFAPI /* PRIVATE */
#endif

#ifndef PNG_VERSION_INFO_ONLY
/* Other defines specific to compilers can go here.  Try to keep
 * them inside an appropriate ifdef/endif pair for portability.
 */

/* C allows up-casts from (void*) to any pointer and (const void*) to any
 * pointer to a const object.  C++ regards this as a type error and requires an
 * explicit, static, cast and provides the static_cast<> rune to ensure that
 * const is not cast away.
 */
#ifdef __cplusplus
#  define png_voidcast(type, value) static_cast<type>(value)
#  define png_constcast(type, value) const_cast<type>(value)
#  define png_aligncast(type, value) \
   static_cast<type>(static_cast<void*>(value))
#  define png_aligncastconst(type, value) \
   static_cast<type>(static_cast<const void*>(value))
#else
#  define png_voidcast(type, value) (value)
#  define png_constcast(type, value) ((type)(void*)(const void*)(value))
#  define png_aligncast(type, value) ((void*)(value))
#  define png_aligncastconst(type, value) ((const void*)(value))
#endif /* __cplusplus */

#if defined(PNG_FLOATING_POINT_SUPPORTED) ||\
    defined(PNG_FLOATING_ARITHMETIC_SUPPORTED)
   /* png.c requires the following ANSI-C constants if the conversion of
    * floating point to ASCII is implemented therein:
    *
    *  DBL_DIG  Maximum number of decimal digits (can be set to any constant)
    *  DBL_MIN  Smallest normalized fp number (can be set to an arbitrary value)
    *  DBL_MAX  Maximum floating point number (can be set to an arbitrary value)
    */
#  include <float.h>

#  include <math.h>

#  if defined(_AMIGA) && defined(__SASC) && defined(_M68881)
   /* Amiga SAS/C: We must include builtin FPU functions when compiling using
    * MATH=68881
    */
#    include <m68881.h>
#  endif
#endif

/* This provides the non-ANSI (far) memory allocation routines. */
#if defined(__TURBOC__) && defined(__MSDOS__)
#  include <mem.h>
#  include <alloc.h>
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#  include <windows.h>
#endif
#endif /* PNG_VERSION_INFO_ONLY */

/* Moved here around 1.5.0beta36 from pngconf.h */
/* Users may want to use these so they are not private.  Any library
 * functions that are passed far data must be model-independent.
 */

/* Platform-independent functions */
#ifndef PNG_ABORT
#  define PNG_ABORT() abort()
#endif

/* These macros may need to be architecture dependent. */
#define PNG_ALIGN_NONE      0 /* do not use data alignment */
#define PNG_ALIGN_ALWAYS    1 /* assume unaligned accesses are OK */
#ifdef offsetof
#  define PNG_ALIGN_OFFSET  2 /* use offsetof to determine alignment */
#else
#  define PNG_ALIGN_OFFSET -1 /* prevent the use of this */
#endif
#define PNG_ALIGN_SIZE      3 /* use sizeof to determine alignment */

#ifndef PNG_ALIGN_TYPE
   /* Default to using aligned access optimizations and requiring alignment to a
    * multiple of the data type size.  Override in a compiler specific fashion
    * if necessary by inserting tests here:
    */
#  define PNG_ALIGN_TYPE PNG_ALIGN_SIZE
#endif

#if PNG_ALIGN_TYPE == PNG_ALIGN_SIZE
   /* This is used because in some compiler implementations non-aligned
    * structure members are supported, so the offsetof approach below fails.
    * Set PNG_ALIGN_SIZE=0 for compiler combinations where unaligned access
    * is good for performance.  Do not do this unless you have tested the
    * result and understand it.
    */
#  define png_alignof(type) (sizeof(type))
#else
#  if PNG_ALIGN_TYPE == PNG_ALIGN_OFFSET
#    define png_alignof(type) offsetof(struct{char c; type t;}, t)
#  else
#    if PNG_ALIGN_TYPE == PNG_ALIGN_ALWAYS
#      define png_alignof(type) 1
#    endif
     /* Else leave png_alignof undefined to prevent use thereof */
#  endif
#endif

/* This implicitly assumes alignment is always a multiple of 2. */
#ifdef png_alignof
#  define png_isaligned(ptr, type) \
   (((type)(size_t)((const void*)(ptr)) & (type)(png_alignof(type)-1)) == 0)
#else
#  define png_isaligned(ptr, type) 0
#endif

/* End of memory model/platform independent support */
/* End of 1.5.0beta36 move from pngconf.h */

/* CONSTANTS and UTILITY MACROS
 * These are used internally by libpng and not exposed in the API
 */

/* Various modes of operation.  Note that after an init, mode is set to
 * zero automatically when the structure is created.  Three of these
 * are defined in png.h because they need to be visible to applications
 * that call png_set_unknown_chunk().
 */
/* #define PNG_HAVE_IHDR            0x01U (defined in png.h) */
/* #define PNG_HAVE_PLTE            0x02U (defined in png.h) */
#define PNG_HAVE_IDAT               0x04U
/* #define PNG_AFTER_IDAT           0x08U (defined in png.h) */
#define PNG_HAVE_IEND               0x10U
                   /*               0x20U (unused) */
                   /*               0x40U (unused) */
                   /*               0x80U (unused) */
#define PNG_HAVE_CHUNK_HEADER      0x100U
#define PNG_WROTE_tIME             0x200U
#define PNG_WROTE_INFO_BEFORE_PLTE 0x400U
#define PNG_BACKGROUND_IS_GRAY     0x800U
#define PNG_HAVE_PNG_SIGNATURE    0x1000U
#define PNG_HAVE_CHUNK_AFTER_IDAT 0x2000U /* Have another chunk after IDAT */
#define PNG_WROTE_eXIf            0x4000U
#define PNG_IS_READ_STRUCT        0x8000U /* Else is a write struct */

/* Flags for the transformations the PNG library does on the image data */
#define PNG_BGR                 0x0001U
#define PNG_INTERLACE           0x0002U
#define PNG_PACK                0x0004U
#define PNG_SHIFT               0x0008U
#define PNG_SWAP_BYTES          0x0010U
#define PNG_INVERT_MONO         0x0020U
#define PNG_QUANTIZE            0x0040U
#define PNG_COMPOSE             0x0080U    /* Was PNG_BACKGROUND */
#define PNG_BACKGROUND_EXPAND   0x0100U
#define PNG_EXPAND_16           0x0200U    /* Added to libpng 1.5.2 */
#define PNG_16_TO_8             0x0400U    /* Becomes 'chop' in 1.5.4 */
#define PNG_RGBA                0x0800U
#define PNG_EXPAND              0x1000U
#define PNG_GAMMA               0x2000U
#define PNG_GRAY_TO_RGB         0x4000U
#define PNG_FILLER              0x8000U
#define PNG_PACKSWAP           0x10000U
#define PNG_SWAP_ALPHA         0x20000U
#define PNG_STRIP_ALPHA        0x40000U
#define PNG_INVERT_ALPHA       0x80000U
#define PNG_USER_TRANSFORM    0x100000U
#define PNG_RGB_TO_GRAY_ERR   0x200000U
#define PNG_RGB_TO_GRAY_WARN  0x400000U
#define PNG_RGB_TO_GRAY       0x600000U /* two bits, RGB_TO_GRAY_ERR|WARN */
#define PNG_ENCODE_ALPHA      0x800000U /* Added to libpng-1.5.4 */
#define PNG_ADD_ALPHA        0x1000000U /* Added to libpng-1.2.7 */
#define PNG_EXPAND_tRNS      0x2000000U /* Added to libpng-1.2.9 */
#define PNG_SCALE_16_TO_8    0x4000000U /* Added to libpng-1.5.4 */
                       /*    0x8000000U unused */
                       /*   0x10000000U unused */
                       /*   0x20000000U unused */
                       /*   0x40000000U unused */
/* Flags for png_create_struct */
#define PNG_STRUCT_PNG   0x0001U
#define PNG_STRUCT_INFO  0x0002U

/* Flags for the png_ptr->flags rather than declaring a byte for each one */
#define PNG_FLAG_ZLIB_CUSTOM_STRATEGY     0x0001U
#define PNG_FLAG_ZSTREAM_INITIALIZED      0x0002U /* Added to libpng-1.6.0 */
                                  /*      0x0004U    unused */
#define PNG_FLAG_ZSTREAM_ENDED            0x0008U /* Added to libpng-1.6.0 */
                                  /*      0x0010U    unused */
                                  /*      0x0020U    unused */
#define PNG_FLAG_ROW_INIT                 0x0040U
#define PNG_FLAG_FILLER_AFTER             0x0080U
#define PNG_FLAG_CRC_ANCILLARY_USE        0x0100U
#define PNG_FLAG_CRC_ANCILLARY_NOWARN     0x0200U
#define PNG_FLAG_CRC_CRITICAL_USE         0x0400U
#define PNG_FLAG_CRC_CRITICAL_IGNORE      0x0800U
#define PNG_FLAG_ASSUME_sRGB              0x1000U /* Added to libpng-1.5.4 */
#define PNG_FLAG_OPTIMIZE_ALPHA           0x2000U /* Added to libpng-1.5.4 */
#define PNG_FLAG_DETECT_UNINITIALIZED     0x4000U /* Added to libpng-1.5.4 */
/* #define PNG_FLAG_KEEP_UNKNOWN_CHUNKS      0x8000U */
/* #define PNG_FLAG_KEEP_UNSAFE_CHUNKS      0x10000U */
#define PNG_FLAG_LIBRARY_MISMATCH        0x20000U
#define PNG_FLAG_STRIP_ERROR_NUMBERS     0x40000U
#define PNG_FLAG_STRIP_ERROR_TEXT        0x80000U
#define PNG_FLAG_BENIGN_ERRORS_WARN     0x100000U /* Added to libpng-1.4.0 */
#define PNG_FLAG_APP_WARNINGS_WARN      0x200000U /* Added to libpng-1.6.0 */
#define PNG_FLAG_APP_ERRORS_WARN        0x400000U /* Added to libpng-1.6.0 */
                                  /*    0x800000U    unused */
                                  /*   0x1000000U    unused */
                                  /*   0x2000000U    unused */
                                  /*   0x4000000U    unused */
                                  /*   0x8000000U    unused */
                                  /*  0x10000000U    unused */
                                  /*  0x20000000U    unused */
                                  /*  0x40000000U    unused */

#define PNG_FLAG_CRC_ANCILLARY_MASK (PNG_FLAG_CRC_ANCILLARY_USE | \
                                     PNG_FLAG_CRC_ANCILLARY_NOWARN)

#define PNG_FLAG_CRC_CRITICAL_MASK  (PNG_FLAG_CRC_CRITICAL_USE | \
                                     PNG_FLAG_CRC_CRITICAL_IGNORE)

#define PNG_FLAG_CRC_MASK           (PNG_FLAG_CRC_ANCILLARY_MASK | \
                                     PNG_FLAG_CRC_CRITICAL_MASK)

/* Save typing and make code easier to understand */

#define PNG_COLOR_DIST(c1, c2) (abs((int)((c1).red) - (int)((c2).red)) + \
   abs((int)((c1).green) - (int)((c2).green)) + \
   abs((int)((c1).blue) - (int)((c2).blue)))

/* Added to libpng-1.6.0: scale a 16-bit value in the range 0..65535 to 0..255
 * by dividing by 257 *with rounding*.  This macro is exact for the given range.
 * See the discourse in pngrtran.c png_do_scale_16_to_8.  The values in the
 * macro were established by experiment (modifying the added value).  The macro
 * has a second variant that takes a value already scaled by 255 and divides by
 * 65535 - this has a maximum error of .502.  Over the range 0..65535*65535 it
 * only gives off-by-one errors and only for 0.5% (1 in 200) of the values.
 */
#define PNG_DIV65535(v24) (((v24) + 32895) >> 16)
#define PNG_DIV257(v16) PNG_DIV65535((png_uint_32)(v16) * 255)

/* Added to libpng-1.2.6 JB */
#define PNG_ROWBYTES(pixel_bits, width) \
    ((pixel_bits) >= 8 ? \
    ((size_t)(width) * (((size_t)(pixel_bits)) >> 3)) : \
    (( ((size_t)(width) * ((size_t)(pixel_bits))) + 7) >> 3) )

