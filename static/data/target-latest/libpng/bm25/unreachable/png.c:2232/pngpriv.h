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

/* This returns the number of trailing bits in the last byte of a row, 0 if the
 * last byte is completely full of pixels.  It is, in principle, (pixel_bits x
 * width) % 8, but that would overflow for large 'width'.  The second macro is
 * the same except that it returns the number of unused bits in the last byte;
 * (8-TRAILBITS), but 0 when TRAILBITS is 0.
 *
 * NOTE: these macros are intended to be self-evidently correct and never
 * overflow on the assumption that pixel_bits is in the range 0..255.  The
 * arguments are evaluated only once and they can be signed (e.g. as a result of
 * the integral promotions).  The result of the expression always has type
 * (png_uint_32), however the compiler always knows it is in the range 0..7.
 */
#define PNG_TRAILBITS(pixel_bits, width) \
    (((pixel_bits) * ((width) % (png_uint_32)8)) % 8)

#define PNG_PADBITS(pixel_bits, width) \
    ((8 - PNG_TRAILBITS(pixel_bits, width)) % 8)

/* PNG_OUT_OF_RANGE returns true if value is outside the range
 * ideal-delta..ideal+delta.  Each argument is evaluated twice.
 * "ideal" and "delta" should be constants, normally simple
 * integers, "value" a variable. Added to libpng-1.2.6 JB
 */
#define PNG_OUT_OF_RANGE(value, ideal, delta) \
   ( (value) < (ideal)-(delta) || (value) > (ideal)+(delta) )

/* Conversions between fixed and floating point, only defined if
 * required (to make sure the code doesn't accidentally use float
 * when it is supposedly disabled.)
 */
#ifdef PNG_FLOATING_POINT_SUPPORTED
/* The floating point conversion can't overflow, though it can and
 * does lose accuracy relative to the original fixed point value.
 * In practice this doesn't matter because png_fixed_point only
 * stores numbers with very low precision.  The png_ptr and s
 * arguments are unused by default but are there in case error
 * checking becomes a requirement.
 */
#define png_float(png_ptr, fixed, s) (.00001 * (fixed))

/* The fixed point conversion performs range checking and evaluates
 * its argument multiple times, so must be used with care.  The
 * range checking uses the PNG specification values for a signed
 * 32-bit fixed point value except that the values are deliberately
 * rounded-to-zero to an integral value - 21474 (21474.83 is roughly
 * (2^31-1) * 100000). 's' is a string that describes the value being
 * converted.
 *
 * NOTE: this macro will raise a png_error if the range check fails,
 * therefore it is normally only appropriate to use this on values
 * that come from API calls or other sources where an out of range
 * error indicates a programming error, not a data error!
 *
 * NOTE: by default this is off - the macro is not used - because the
 * function call saves a lot of code.
 */
#ifdef PNG_FIXED_POINT_MACRO_SUPPORTED
#define png_fixed(png_ptr, fp, s) ((fp) <= 21474 && (fp) >= -21474 ?\
    ((png_fixed_point)(100000 * (fp))) : (png_fixed_error(png_ptr, s),0))
#endif
/* else the corresponding function is defined below, inside the scope of the
 * cplusplus test.
 */
#endif

/* Constants for known chunk types.  If you need to add a chunk, define the name
 * here.  For historical reasons these constants have the form png_<name>; i.e.
 * the prefix is lower case.  Please use decimal values as the parameters to
 * match the ISO PNG specification and to avoid relying on the C locale
 * interpretation of character values.
 *
 * Prior to 1.5.6 these constants were strings, as of 1.5.6 png_uint_32 values
 * are computed and a new macro (PNG_STRING_FROM_CHUNK) added to allow a string
 * to be generated if required.
 *
 * PNG_32b correctly produces a value shifted by up to 24 bits, even on
 * architectures where (int) is only 16 bits.
 */
#define PNG_32b(b,s) ((png_uint_32)(b) << (s))
#define PNG_U32(b1,b2,b3,b4) \
   (PNG_32b(b1,24) | PNG_32b(b2,16) | PNG_32b(b3,8) | PNG_32b(b4,0))

/* Constants for known chunk types.
 *
 * MAINTAINERS: If you need to add a chunk, define the name here.
 * For historical reasons these constants have the form png_<name>; i.e.
 * the prefix is lower case.  Please use decimal values as the parameters to
 * match the ISO PNG specification and to avoid relying on the C locale
 * interpretation of character values.  Please keep the list sorted.
 *
 * Notice that PNG_U32 is used to define a 32-bit value for the 4 byte chunk
 * type.  In fact the specification does not express chunk types this way,
 * however using a 32-bit value means that the chunk type can be read from the
 * stream using exactly the same code as used for a 32-bit unsigned value and
 * can be examined far more efficiently (using one arithmetic compare).
 *
 * Prior to 1.5.6 the chunk type constants were expressed as C strings.  The
 * libpng API still uses strings for 'unknown' chunks and a macro,
 * PNG_STRING_FROM_CHUNK, allows a string to be generated if required.  Notice
 * that for portable code numeric values must still be used; the string "IHDR"
 * is not portable and neither is PNG_U32('I', 'H', 'D', 'R').
 *
 * In 1.7.0 the definitions will be made public in png.h to avoid having to
 * duplicate the same definitions in application code.
 */
#define png_IDAT PNG_U32( 73,  68,  65,  84)
#define png_IEND PNG_U32( 73,  69,  78,  68)
#define png_IHDR PNG_U32( 73,  72,  68,  82)
#define png_PLTE PNG_U32( 80,  76,  84,  69)
#define png_bKGD PNG_U32( 98,  75,  71,  68)
#define png_cHRM PNG_U32( 99,  72,  82,  77)
#define png_eXIf PNG_U32(101,  88,  73, 102) /* registered July 2017 */
#define png_fRAc PNG_U32(102,  82,  65,  99) /* registered, not defined */
#define png_gAMA PNG_U32(103,  65,  77,  65)
#define png_gIFg PNG_U32(103,  73,  70, 103)
#define png_gIFt PNG_U32(103,  73,  70, 116) /* deprecated */
#define png_gIFx PNG_U32(103,  73,  70, 120)
#define png_hIST PNG_U32(104,  73,  83,  84)
#define png_iCCP PNG_U32(105,  67,  67,  80)
#define png_iTXt PNG_U32(105,  84,  88, 116)
#define png_oFFs PNG_U32(111,  70,  70, 115)
#define png_pCAL PNG_U32(112,  67,  65,  76)
#define png_pHYs PNG_U32(112,  72,  89, 115)
#define png_sBIT PNG_U32(115,  66,  73,  84)
#define png_sCAL PNG_U32(115,  67,  65,  76)
#define png_sPLT PNG_U32(115,  80,  76,  84)
#define png_sRGB PNG_U32(115,  82,  71,  66)
#define png_sTER PNG_U32(115,  84,  69,  82)
#define png_tEXt PNG_U32(116,  69,  88, 116)
#define png_tIME PNG_U32(116,  73,  77,  69)
#define png_tRNS PNG_U32(116,  82,  78,  83)
#define png_zTXt PNG_U32(122,  84,  88, 116)

/* The following will work on (signed char*) strings, whereas the get_uint_32
 * macro will fail on top-bit-set values because of the sign extension.
 */
#define PNG_CHUNK_FROM_STRING(s)\
   PNG_U32(0xff & (s)[0], 0xff & (s)[1], 0xff & (s)[2], 0xff & (s)[3])

/* This uses (char), not (png_byte) to avoid warnings on systems where (char) is
 * signed and the argument is a (char[])  This macro will fail miserably on
 * systems where (char) is more than 8 bits.
 */
#define PNG_STRING_FROM_CHUNK(s,c)\
   (void)(((char*)(s))[0]=(char)(((c)>>24) & 0xff), \
   ((char*)(s))[1]=(char)(((c)>>16) & 0xff),\
   ((char*)(s))[2]=(char)(((c)>>8) & 0xff), \
   ((char*)(s))[3]=(char)((c & 0xff)))

/* Do the same but terminate with a null character. */
#define PNG_CSTRING_FROM_CHUNK(s,c)\
   (void)(PNG_STRING_FROM_CHUNK(s,c), ((char*)(s))[4] = 0)

/* Test on flag values as defined in the spec (section 5.4): */
#define PNG_CHUNK_ANCILLARY(c)   (1 & ((c) >> 29))
#define PNG_CHUNK_CRITICAL(c)     (!PNG_CHUNK_ANCILLARY(c))
#define PNG_CHUNK_PRIVATE(c)      (1 & ((c) >> 21))
#define PNG_CHUNK_RESERVED(c)     (1 & ((c) >> 13))
#define PNG_CHUNK_SAFE_TO_COPY(c) (1 & ((c) >>  5))

/* Gamma values (new at libpng-1.5.4): */
#define PNG_GAMMA_MAC_OLD 151724  /* Assume '1.8' is really 2.2/1.45! */
#define PNG_GAMMA_MAC_INVERSE 65909
#define PNG_GAMMA_sRGB_INVERSE 45455

/* Almost everything below is C specific; the #defines above can be used in
 * non-C code (so long as it is C-preprocessed) the rest of this stuff cannot.
 */
#ifndef PNG_VERSION_INFO_ONLY

#include "pngstruct.h"
#include "pnginfo.h"

/* Validate the include paths - the include path used to generate pnglibconf.h
 * must match that used in the build, or we must be using pnglibconf.h.prebuilt:
 */
#if PNG_ZLIB_VERNUM != 0 && PNG_ZLIB_VERNUM != ZLIB_VERNUM
#  error ZLIB_VERNUM != PNG_ZLIB_VERNUM \
      "-I (include path) error: see the notes in pngpriv.h"
   /* This means that when pnglibconf.h was built the copy of zlib.h that it
    * used is not the same as the one being used here.  Because the build of
    * libpng makes decisions to use inflateInit2 and inflateReset2 based on the
    * zlib version number and because this affects handling of certain broken
    * PNG files the -I directives must match.
    *
    * The most likely explanation is that you passed a -I in CFLAGS. This will
    * not work; all the preprocessor directives and in particular all the -I
    * directives must be in CPPFLAGS.
    */
#endif

/* This is used for 16-bit gamma tables -- only the top level pointers are
 * const; this could be changed:
 */
typedef const png_uint_16p * png_const_uint_16pp;

/* Added to libpng-1.5.7: sRGB conversion tables */
#if defined(PNG_SIMPLIFIED_READ_SUPPORTED) ||\
   defined(PNG_SIMPLIFIED_WRITE_SUPPORTED)
#ifdef PNG_SIMPLIFIED_READ_SUPPORTED
PNG_INTERNAL_DATA(const png_uint_16, png_sRGB_table, [256]);
   /* Convert from an sRGB encoded value 0..255 to a 16-bit linear value,
    * 0..65535.  This table gives the closest 16-bit answers (no errors).
    */
#endif

PNG_INTERNAL_DATA(const png_uint_16, png_sRGB_base, [512]);
PNG_INTERNAL_DATA(const png_byte, png_sRGB_delta, [512]);

#define PNG_sRGB_FROM_LINEAR(linear) \
  ((png_byte)(0xff & ((png_sRGB_base[(linear)>>15] \
   + ((((linear) & 0x7fff)*png_sRGB_delta[(linear)>>15])>>12)) >> 8)))
   /* Given a value 'linear' in the range 0..255*65535 calculate the 8-bit sRGB
    * encoded value with maximum error 0.646365.  Note that the input is not a
    * 16-bit value; it has been multiplied by 255! */
#endif /* SIMPLIFIED_READ/WRITE */


/* Inhibit C++ name-mangling for libpng functions but not for system calls. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Internal functions; these are not exported from a DLL however because they
 * are used within several of the C source files they have to be C extern.
 *
 * All of these functions must be declared with PNG_INTERNAL_FUNCTION.
 */

/* Zlib support */
#define PNG_UNEXPECTED_ZLIB_RETURN (-7)
PNG_INTERNAL_FUNCTION(void, png_zstream_error,(png_structrp png_ptr, int ret),
   PNG_EMPTY);
   /* Used by the zlib handling functions to ensure that z_stream::msg is always
    * set before they return.
    */

#ifdef PNG_WRITE_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_free_buffer_list,(png_structrp png_ptr,
   png_compression_bufferp *list),PNG_EMPTY);
   /* Free the buffer list used by the compressed write code. */
#endif

#if defined(PNG_FLOATING_POINT_SUPPORTED) && \
   !defined(PNG_FIXED_POINT_MACRO_SUPPORTED) && \
   (defined(PNG_gAMA_SUPPORTED) || defined(PNG_cHRM_SUPPORTED) || \
   defined(PNG_sCAL_SUPPORTED) || defined(PNG_READ_BACKGROUND_SUPPORTED) || \
   defined(PNG_READ_RGB_TO_GRAY_SUPPORTED)) || \
   (defined(PNG_sCAL_SUPPORTED) && \
   defined(PNG_FLOATING_ARITHMETIC_SUPPORTED))
PNG_INTERNAL_FUNCTION(png_fixed_point,png_fixed,(png_const_structrp png_ptr,
   double fp, png_const_charp text),PNG_EMPTY);
#endif

/* Check the user version string for compatibility, returns false if the version
 * numbers aren't compatible.
 */
PNG_INTERNAL_FUNCTION(int,png_user_version_check,(png_structrp png_ptr,
   png_const_charp user_png_ver),PNG_EMPTY);

/* Internal base allocator - no messages, NULL on failure to allocate.  This
 * does, however, call the application provided allocator and that could call
 * png_error (although that would be a bug in the application implementation.)
 */
PNG_INTERNAL_FUNCTION(png_voidp,png_malloc_base,(png_const_structrp png_ptr,
   png_alloc_size_t size),PNG_ALLOCATED);

#if defined(PNG_TEXT_SUPPORTED) || defined(PNG_sPLT_SUPPORTED) ||\
   defined(PNG_STORE_UNKNOWN_CHUNKS_SUPPORTED)
/* Internal array allocator, outputs no error or warning messages on failure,
 * just returns NULL.
 */
PNG_INTERNAL_FUNCTION(png_voidp,png_malloc_array,(png_const_structrp png_ptr,
   int nelements, size_t element_size),PNG_ALLOCATED);

/* The same but an existing array is extended by add_elements.  This function
 * also memsets the new elements to 0 and copies the old elements.  The old
 * array is not freed or altered.
 */
PNG_INTERNAL_FUNCTION(png_voidp,png_realloc_array,(png_const_structrp png_ptr,
   png_const_voidp array, int old_elements, int add_elements,
   size_t element_size),PNG_ALLOCATED);
#endif /* text, sPLT or unknown chunks */

/* Magic to create a struct when there is no struct to call the user supplied
 * memory allocators.  Because error handling has not been set up the memory
 * handlers can't safely call png_error, but this is an obscure and undocumented
 * restriction so libpng has to assume that the 'free' handler, at least, might
 * call png_error.
 */
PNG_INTERNAL_FUNCTION(png_structp,png_create_png_struct,
   (png_const_charp user_png_ver, png_voidp error_ptr, png_error_ptr error_fn,
    png_error_ptr warn_fn, png_voidp mem_ptr, png_malloc_ptr malloc_fn,
    png_free_ptr free_fn),PNG_ALLOCATED);

/* Free memory from internal libpng struct */
PNG_INTERNAL_FUNCTION(void,png_destroy_png_struct,(png_structrp png_ptr),
   PNG_EMPTY);

/* Free an allocated jmp_buf (always succeeds) */
PNG_INTERNAL_FUNCTION(void,png_free_jmpbuf,(png_structrp png_ptr),PNG_EMPTY);

/* Function to allocate memory for zlib.  PNGAPI is disallowed. */
PNG_INTERNAL_FUNCTION(voidpf,png_zalloc,(voidpf png_ptr, uInt items, uInt size),
   PNG_ALLOCATED);

/* Function to free memory for zlib.  PNGAPI is disallowed. */
PNG_INTERNAL_FUNCTION(void,png_zfree,(voidpf png_ptr, voidpf ptr),PNG_EMPTY);

/* Next four functions are used internally as callbacks.  PNGCBAPI is required
 * but not PNG_EXPORT.  PNGAPI added at libpng version 1.2.3, changed to
 * PNGCBAPI at 1.5.0
 */

PNG_INTERNAL_FUNCTION(void PNGCBAPI,png_default_read_data,(png_structp png_ptr,
    png_bytep data, size_t length),PNG_EMPTY);

#ifdef PNG_PROGRESSIVE_READ_SUPPORTED
PNG_INTERNAL_FUNCTION(void PNGCBAPI,png_push_fill_buffer,(png_structp png_ptr,
    png_bytep buffer, size_t length),PNG_EMPTY);
#endif

PNG_INTERNAL_FUNCTION(void PNGCBAPI,png_default_write_data,(png_structp png_ptr,
    png_bytep data, size_t length),PNG_EMPTY);

#ifdef PNG_WRITE_FLUSH_SUPPORTED
#  ifdef PNG_STDIO_SUPPORTED
PNG_INTERNAL_FUNCTION(void PNGCBAPI,png_default_flush,(png_structp png_ptr),
   PNG_EMPTY);
#  endif
#endif

/* Reset the CRC variable */
PNG_INTERNAL_FUNCTION(void,png_reset_crc,(png_structrp png_ptr),PNG_EMPTY);

/* Write the "data" buffer to whatever output you are using */
PNG_INTERNAL_FUNCTION(void,png_write_data,(png_structrp png_ptr,
    png_const_bytep data, size_t length),PNG_EMPTY);

/* Read and check the PNG file signature */
PNG_INTERNAL_FUNCTION(void,png_read_sig,(png_structrp png_ptr,
   png_inforp info_ptr),PNG_EMPTY);

/* Read the chunk header (length + type name) */
PNG_INTERNAL_FUNCTION(png_uint_32,png_read_chunk_header,(png_structrp png_ptr),
   PNG_EMPTY);

/* Read data from whatever input you are using into the "data" buffer */
PNG_INTERNAL_FUNCTION(void,png_read_data,(png_structrp png_ptr, png_bytep data,
    size_t length),PNG_EMPTY);

/* Read bytes into buf, and update png_ptr->crc */
PNG_INTERNAL_FUNCTION(void,png_crc_read,(png_structrp png_ptr, png_bytep buf,
    png_uint_32 length),PNG_EMPTY);

/* Read "skip" bytes, read the file crc, and (optionally) verify png_ptr->crc */
PNG_INTERNAL_FUNCTION(int,png_crc_finish,(png_structrp png_ptr,
   png_uint_32 skip),PNG_EMPTY);

/* Read the CRC from the file and compare it to the libpng calculated CRC */
PNG_INTERNAL_FUNCTION(int,png_crc_error,(png_structrp png_ptr),PNG_EMPTY);

/* Calculate the CRC over a section of data.  Note that we are only
 * passing a maximum of 64K on systems that have this as a memory limit,
 * since this is the maximum buffer size we can specify.
 */
PNG_INTERNAL_FUNCTION(void,png_calculate_crc,(png_structrp png_ptr,
   png_const_bytep ptr, size_t length),PNG_EMPTY);

#ifdef PNG_WRITE_FLUSH_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_flush,(png_structrp png_ptr),PNG_EMPTY);
#endif

/* Write various chunks */

/* Write the IHDR chunk, and update the png_struct with the necessary
 * information.
 */
PNG_INTERNAL_FUNCTION(void,png_write_IHDR,(png_structrp png_ptr,
   png_uint_32 width, png_uint_32 height, int bit_depth, int color_type,
   int compression_method, int filter_method, int interlace_method),PNG_EMPTY);

PNG_INTERNAL_FUNCTION(void,png_write_PLTE,(png_structrp png_ptr,
   png_const_colorp palette, png_uint_32 num_pal),PNG_EMPTY);

PNG_INTERNAL_FUNCTION(void,png_compress_IDAT,(png_structrp png_ptr,
   png_const_bytep row_data, png_alloc_size_t row_data_length, int flush),
   PNG_EMPTY);

PNG_INTERNAL_FUNCTION(void,png_write_IEND,(png_structrp png_ptr),PNG_EMPTY);

#ifdef PNG_WRITE_gAMA_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_gAMA_fixed,(png_structrp png_ptr,
    png_fixed_point file_gamma),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_sBIT_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_sBIT,(png_structrp png_ptr,
    png_const_color_8p sbit, int color_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_cHRM_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_cHRM_fixed,(png_structrp png_ptr,
    const png_xy *xy), PNG_EMPTY);
   /* The xy value must have been previously validated */
#endif

#ifdef PNG_WRITE_sRGB_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_sRGB,(png_structrp png_ptr,
    int intent),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_eXIf_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_eXIf,(png_structrp png_ptr,
    png_bytep exif, int num_exif),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_iCCP_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_iCCP,(png_structrp png_ptr,
   png_const_charp name, png_const_bytep profile), PNG_EMPTY);
   /* The profile must have been previously validated for correctness, the
    * length comes from the first four bytes.  Only the base, deflate,
    * compression is supported.
    */
#endif

#ifdef PNG_WRITE_sPLT_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_sPLT,(png_structrp png_ptr,
    png_const_sPLT_tp palette),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_tRNS_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_tRNS,(png_structrp png_ptr,
    png_const_bytep trans, png_const_color_16p values, int number,
    int color_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_bKGD_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_bKGD,(png_structrp png_ptr,
    png_const_color_16p values, int color_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_hIST_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_hIST,(png_structrp png_ptr,
    png_const_uint_16p hist, int num_hist),PNG_EMPTY);
#endif

/* Chunks that have keywords */
#ifdef PNG_WRITE_tEXt_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_tEXt,(png_structrp png_ptr,
   png_const_charp key, png_const_charp text, size_t text_len),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_zTXt_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_zTXt,(png_structrp png_ptr, png_const_charp
    key, png_const_charp text, int compression),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_iTXt_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_iTXt,(png_structrp png_ptr,
    int compression, png_const_charp key, png_const_charp lang,
    png_const_charp lang_key, png_const_charp text),PNG_EMPTY);
#endif

#ifdef PNG_TEXT_SUPPORTED  /* Added at version 1.0.14 and 1.2.4 */
PNG_INTERNAL_FUNCTION(int,png_set_text_2,(png_const_structrp png_ptr,
    png_inforp info_ptr, png_const_textp text_ptr, int num_text),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_oFFs_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_oFFs,(png_structrp png_ptr,
    png_int_32 x_offset, png_int_32 y_offset, int unit_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_pCAL_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_pCAL,(png_structrp png_ptr,
    png_charp purpose, png_int_32 X0, png_int_32 X1, int type, int nparams,
    png_const_charp units, png_charpp params),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_pHYs_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_pHYs,(png_structrp png_ptr,
    png_uint_32 x_pixels_per_unit, png_uint_32 y_pixels_per_unit,
    int unit_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_tIME_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_tIME,(png_structrp png_ptr,
    png_const_timep mod_time),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_sCAL_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_sCAL_s,(png_structrp png_ptr,
    int unit, png_const_charp width, png_const_charp height),PNG_EMPTY);
#endif

/* Called when finished processing a row of data */
PNG_INTERNAL_FUNCTION(void,png_write_finish_row,(png_structrp png_ptr),
    PNG_EMPTY);

/* Internal use only.   Called before first row of data */
PNG_INTERNAL_FUNCTION(void,png_write_start_row,(png_structrp png_ptr),
    PNG_EMPTY);

/* Combine a row of data, dealing with alpha, etc. if requested.  'row' is an
 * array of png_ptr->width pixels.  If the image is not interlaced or this
 * is the final pass this just does a memcpy, otherwise the "display" flag
 * is used to determine whether to copy pixels that are not in the current pass.
 *
 * Because 'png_do_read_interlace' (below) replicates pixels this allows this
 * function to achieve the documented 'blocky' appearance during interlaced read
 * if display is 1 and the 'sparkle' appearance, where existing pixels in 'row'
 * are not changed if they are not in the current pass, when display is 0.
 *
 * 'display' must be 0 or 1, otherwise the memcpy will be done regardless.
 *
 * The API always reads from the png_struct row buffer and always assumes that
 * it is full width (png_do_read_interlace has already been called.)
 *
 * This function is only ever used to write to row buffers provided by the
 * caller of the relevant libpng API and the row must have already been
 * transformed by the read transformations.
 *
 * The PNG_USE_COMPILE_TIME_MASKS option causes generation of pre-computed
 * bitmasks for use within the code, otherwise runtime generated masks are used.
 * The default is compile time masks.
 */
#ifndef PNG_USE_COMPILE_TIME_MASKS
#  define PNG_USE_COMPILE_TIME_MASKS 1
#endif
PNG_INTERNAL_FUNCTION(void,png_combine_row,(png_const_structrp png_ptr,
    png_bytep row, int display),PNG_EMPTY);

#ifdef PNG_READ_INTERLACING_SUPPORTED
/* Expand an interlaced row: the 'row_info' describes the pass data that has
 * been read in and must correspond to the pixels in 'row', the pixels are
 * expanded (moved apart) in 'row' to match the final layout, when doing this
 * the pixels are *replicated* to the intervening space.  This is essential for
 * the correct operation of png_combine_row, above.
 */
PNG_INTERNAL_FUNCTION(void,png_do_read_interlace,(png_row_infop row_info,
    png_bytep row, int pass, png_uint_32 transformations),PNG_EMPTY);
#endif

/* GRR TO DO (2.0 or whenever):  simplify other internal calling interfaces */

#ifdef PNG_WRITE_INTERLACING_SUPPORTED
/* Grab pixels out of a row for an interlaced pass */
PNG_INTERNAL_FUNCTION(void,png_do_write_interlace,(png_row_infop row_info,
    png_bytep row, int pass),PNG_EMPTY);
#endif

/* Unfilter a row: check the filter value before calling this, there is no point
 * calling it for PNG_FILTER_VALUE_NONE.
 */
PNG_INTERNAL_FUNCTION(void,png_read_filter_row,(png_structrp pp, png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row, int filter),PNG_EMPTY);

#if PNG_ARM_NEON_OPT > 0
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_up_neon,(png_row_infop row_info,
    png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub3_neon,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub4_neon,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg3_neon,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg4_neon,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth3_neon,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth4_neon,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
#endif

#if PNG_MIPS_MSA_IMPLEMENTATION == 1
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_up_msa,(png_row_infop row_info,
    png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub3_msa,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub4_msa,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg3_msa,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg4_msa,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth3_msa,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth4_msa,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
#endif

#if PNG_MIPS_MMI_IMPLEMENTATION > 0
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_up_mmi,(png_row_infop row_info,
    png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub3_mmi,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub4_mmi,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg3_mmi,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg4_mmi,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth3_mmi,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth4_mmi,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
#endif

#if PNG_POWERPC_VSX_OPT > 0
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_up_vsx,(png_row_infop row_info,
    png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub3_vsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub4_vsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg3_vsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg4_vsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth3_vsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth4_vsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
#endif

#if PNG_INTEL_SSE_IMPLEMENTATION > 0
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub3_sse2,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub4_sse2,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg3_sse2,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg4_sse2,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth3_sse2,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth4_sse2,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
#endif

#if PNG_LOONGARCH_LSX_IMPLEMENTATION == 1
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_up_lsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub3_lsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub4_lsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg3_lsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg4_lsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth3_lsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth4_lsx,(png_row_infop
    row_info, png_bytep row, png_const_bytep prev_row),PNG_EMPTY);
#endif

/* Choose the best filter to use and filter the row data */
PNG_INTERNAL_FUNCTION(void,png_write_find_filter,(png_structrp png_ptr,
    png_row_infop row_info),PNG_EMPTY);

#ifdef PNG_SEQUENTIAL_READ_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_read_IDAT_data,(png_structrp png_ptr,
   png_bytep output, png_alloc_size_t avail_out),PNG_EMPTY);
   /* Read 'avail_out' bytes of data from the IDAT stream.  If the output buffer
    * is NULL the function checks, instead, for the end of the stream.  In this
    * case a benign error will be issued if the stream end is not found or if
    * extra data has to be consumed.
    */
PNG_INTERNAL_FUNCTION(void,png_read_finish_IDAT,(png_structrp png_ptr),
   PNG_EMPTY);
   /* This cleans up when the IDAT LZ stream does not end when the last image
    * byte is read; there is still some pending input.
    */

PNG_INTERNAL_FUNCTION(void,png_read_finish_row,(png_structrp png_ptr),
   PNG_EMPTY);
   /* Finish a row while reading, dealing with interlacing passes, etc. */
#endif /* SEQUENTIAL_READ */

/* Initialize the row buffers, etc. */
PNG_INTERNAL_FUNCTION(void,png_read_start_row,(png_structrp png_ptr),PNG_EMPTY);

#if ZLIB_VERNUM >= 0x1240
PNG_INTERNAL_FUNCTION(int,png_zlib_inflate,(png_structrp png_ptr, int flush),
      PNG_EMPTY);
#  define PNG_INFLATE(pp, flush) png_zlib_inflate(pp, flush)
#else /* Zlib < 1.2.4 */
#  define PNG_INFLATE(pp, flush) inflate(&(pp)->zstream, flush)
#endif /* Zlib < 1.2.4 */

#ifdef PNG_READ_TRANSFORMS_SUPPORTED
/* Optional call to update the users info structure */
PNG_INTERNAL_FUNCTION(void,png_read_transform_info,(png_structrp png_ptr,
    png_inforp info_ptr),PNG_EMPTY);
#endif

/* Shared transform functions, defined in pngtran.c */
#if defined(PNG_WRITE_FILLER_SUPPORTED) || \
    defined(PNG_READ_STRIP_ALPHA_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_do_strip_channel,(png_row_infop row_info,
    png_bytep row, int at_start),PNG_EMPTY);
#endif

#ifdef PNG_16BIT_SUPPORTED
#if defined(PNG_READ_SWAP_SUPPORTED) || defined(PNG_WRITE_SWAP_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_do_swap,(png_row_infop row_info,
    png_bytep row),PNG_EMPTY);
#endif
#endif

#if defined(PNG_READ_PACKSWAP_SUPPORTED) || \
    defined(PNG_WRITE_PACKSWAP_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_do_packswap,(png_row_infop row_info,
    png_bytep row),PNG_EMPTY);
#endif

#if defined(PNG_READ_INVERT_SUPPORTED) || defined(PNG_WRITE_INVERT_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_do_invert,(png_row_infop row_info,
    png_bytep row),PNG_EMPTY);
#endif

#if defined(PNG_READ_BGR_SUPPORTED) || defined(PNG_WRITE_BGR_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_do_bgr,(png_row_infop row_info,
    png_bytep row),PNG_EMPTY);
#endif

/* The following decodes the appropriate chunks, and does error correction,
 * then calls the appropriate callback for the chunk if it is valid.
 */

/* Decode the IHDR chunk */
PNG_INTERNAL_FUNCTION(void,png_handle_IHDR,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_handle_PLTE,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_handle_IEND,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);

#ifdef PNG_READ_bKGD_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_bKGD,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_cHRM_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_cHRM,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_eXIf_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_eXIf,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_gAMA_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_gAMA,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_hIST_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_hIST,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_iCCP_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_iCCP,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif /* READ_iCCP */

#ifdef PNG_READ_iTXt_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_iTXt,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_oFFs_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_oFFs,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_pCAL_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_pCAL,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_pHYs_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_pHYs,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG_EMPTY);
#endif

#ifdef PNG_READ_sBIT_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_sBIT,(png_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 length),PNG