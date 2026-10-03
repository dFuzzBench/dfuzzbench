/* inflate.c -- zlib decompression
 * Copyright (C) 1995-2022 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/*
 * Change history:
 *
 * 1.2.beta0    24 Nov 2002
 * - First version -- complete rewrite of inflate to simplify code, avoid
 *   creation of window when not needed, minimize use of window when it is
 *   needed, make inffast.c even faster, implement gzip decoding, and to
 *   improve code readability and style over the previous zlib inflate code
 *
 * 1.2.beta1    25 Nov 2002
 * - Use pointers for available input and output checking in inffast.c
 * - Remove input and output counters in inffast.c
 * - Change inffast.c entry and loop from avail_in >= 7 to >= 6
 * - Remove unnecessary second byte pull from length extra in inffast.c
 * - Unroll direct copy to three copies per loop in inffast.c
 *
 * 1.2.beta2    4 Dec 2002
 * - Change external routine names to reduce potential conflicts
 * - Correct filename to inffixed.h for fixed tables in inflate.c
 * - Make hbuf[] unsigned char to match parameter type in inflate.c
 * - Change strm->next_out[-state->offset] to *(strm->next_out - state->offset)
 *   to avoid negation problem on Alphas (64 bit) in inflate.c
 *
 * 1.2.beta3    22 Dec 2002
 * - Add comments on state->bits assertion in inffast.c
 * - Add comments on op field in inftrees.h
 * - Fix bug in reuse of allocated window after inflateReset()
 * - Remove bit fields--back to byte structure for speed
 * - Remove distance extra == 0 check in inflate_fast()--only helps for lengths
 * - Change post-increments to pre-increments in inflate_fast(), PPC biased?
 * - Add compile time option, POSTINC, to use post-increments instead (Intel?)
 * - Make MATCH copy in inflate() much faster for when inflate_fast() not used
 * - Use local copies of stream next and avail values, as well as local bit
 *   buffer and bit count in inflate()--for speed when inflate_fast() not used
 *
 * 1.2.beta4    1 Jan 2003
 * - Split ptr - 257 statements in inflate_table() to avoid compiler warnings
 * - Move a comment on output buffer sizes from inffast.c to inflate.c
 * - Add comments in inffast.c to introduce the inflate_fast() routine
 * - Rearrange window copies in inflate_fast() for speed and simplification
 * - Unroll last copy for window match in inflate_fast()
 * - Use local copies of window variables in inflate_fast() for speed
 * - Pull out common wnext == 0 case for speed in inflate_fast()
 * - Make op and len in inflate_fast() unsigned for consistency
 * - Add FAR to lcode and dcode declarations in inflate_fast()
 * - Simplified bad distance check in inflate_fast()
 * - Added inflateBackInit(), inflateBack(), and inflateBackEnd() in new
 *   source file infback.c to provide a call-back interface to inflate for
 *   programs like gzip and unzip -- uses window as output buffer to avoid
 *   window copying
 *
 * 1.2.beta5    1 Jan 2003
 * - Improved inflateBack() interface to allow the caller to provide initial
 *   input in strm.
 * - Fixed stored blocks bug in inflateBack()
 *
 * 1.2.beta6    4 Jan 2003
 * - Added comments in inffast.c on effectiveness of POSTINC
 * - Typecasting all around to reduce compiler warnings
 * - Changed loops from while (1) or do {} while (1) to for (;;), again to
 *   make compilers happy
 * - Changed type of window in inflateBackInit() to unsigned char *
 *
 * 1.2.beta7    27 Jan 2003
 * - Changed many types to unsigned or unsigned short to avoid warnings
 * - Added inflateCopy() function
 *
 * 1.2.0        9 Mar 2003
 * - Changed inflateBack() interface to provide separate opaque descriptors
 *   for the in() and out() functions
 * - Changed inflateBack() argument and in_func typedef to swap the length
 *   and buffer address return values for the input function
 * - Check next_in and next_out for Z_NULL on entry to inflate()
 *
 * The history for versions after 1.2.0 are in ChangeLog in zlib distribution.
 */

#include "zutil.h"
#include "inftrees.h"
#include "inflate.h"
#include "inffast.h"

#ifdef MAKEFIXED
#  ifndef BUILDFIXED
#    define BUILDFIXED
#  endif
#endif

local int inflateStateCheck(z_streamp strm) {
    struct inflate_state FAR *state;
    if (strm == Z_NULL ||
        strm->zalloc == (alloc_func)0 || strm->zfree == (free_func)0)
        return 1;
    state = (struct inflate_state FAR *)strm->state;
    if (state == Z_NULL || state->strm != strm ||
        state->mode < HEAD || state->mode > SYNC)
        return 1;
    return 0;
}

int ZEXPORT inflateResetKeep(z_streamp strm) {
    struct inflate_state FAR *state;

    if (inflateStateCheck(strm)) return Z_STREAM_ERROR;
    state = (struct inflate_state FAR *)strm->state;
    strm->total_in = strm->total_out = state->total = 0;
    strm->start_bit = strm->stop_bit = strm->last_bit = 0;
    strm->msg = Z_NULL;
    if (state->wrap)        /* to support ill-conceived Java test suite */
        strm->adler = state->wrap & 1;
    state->mode = HEAD;
    state->last = 0;
    state->havedict = 0;
    state->flags = -1;
    state->dmax = 32768U;
    state->head = Z_NULL;
    state->hold = 0;
    state->bits = 0;
    state->lencode = state->distcode = state->next = state->codes;
    state->sane = 1;
    state->back = -1;
    Tracev((stderr, "inflate: reset\n"));
    return Z_OK;
}

int ZEXPORT inflateReset(z_streamp strm) {
    struct inflate_state FAR *state;

    if (inflateStateCheck(strm)) return Z_STREAM_ERROR;
    state = (struct inflate_state FAR *)strm->state;
    state->wsize = 0;
    state->whave = 0;
    state->wnext = 0;
    return inflateResetKeep(strm);
}

int ZEXPORT inflateReset2(z_streamp strm, int windowBits) {
    int wrap;
    struct inflate_state FAR *state;

    /* get the state */
    if (inflateStateCheck(strm)) return Z_STREAM_ERROR;
    state = (struct inflate_state FAR *)strm->state;

    /* extract wrap request from windowBits parameter */
    if (windowBits < 0) {
        if (windowBits < -15)
            return Z_STREAM_ERROR;
        wrap = 0;
        windowBits = -windowBits;
    }
    else {
        wrap = (windowBits >> 4) + 5;
#ifdef GUNZIP
        if (windowBits < 48)
            windowBits &= 15;
#endif
    }

    /* set number of window bits, free window if different */
    if (windowBits && (windowBits < 8 || windowBits > 15))
        return Z_STREAM_ERROR;
    if (state->window != Z_NULL && state->wbits != (unsigned)windowBits) {
        ZFREE(strm, state->window);
        state->window = Z_NULL;
    }

    /* update state and reset the rest of it */
    state->wrap = wrap;
    state->wbits = (unsigned)windowBits;
    return inflateReset(strm);
}

int ZEXPORT inflateInit2_(z_streamp strm, int windowBits,
                          const char *version, int stream_size) {
    int ret;
    struct inflate_state FAR *state;

    if (version == Z_NULL || version[0] != ZLIB_VERSION[0] ||
        stream_size != (int)(sizeof(z_stream)))
        return Z_VERSION_ERROR;
    if (strm == Z_NULL) return Z_STREAM_ERROR;
    strm->msg = Z_NULL;                 /* in case we return an error */
    if (strm->zalloc == (alloc_func)0) {
#ifdef Z_SOLO
        return Z_STREAM_ERROR;
#else
        strm->zalloc = zcalloc;
        strm->opaque = (voidpf)0;
#endif
    }
    if (strm->zfree == (free_func)0)
#ifdef Z_SOLO
        return Z_STREAM_ERROR;
#else
        strm->zfree = zcfree;
#endif
    state = (struct inflate_state FAR *)
            ZALLOC(strm, 1, sizeof(struct inflate_state));
    if (state == Z_NULL) return Z_MEM_ERROR;
    Tracev((stderr, "inflate: allocated\n"));
    strm->state = (struct internal_state FAR *)state;
    state->strm = strm;
    state->window = Z_NULL;
    state->mode = HEAD;     /* to pass state test in inflateReset2() */
    ret = inflateReset2(strm, windowBits);
    if (ret != Z_OK) {
        ZFREE(strm, state);
        strm->state = Z_NULL;
    }
    return ret;
}

#ifdef NOVGZ

int ZEXPORT inflateInit_(z_streamp strm, const char *version,
                         int stream_size) {
    return inflateInit2_(strm, DEF_WBITS, version, stream_size);
}

int ZEXPORT inflatePrime(z_streamp strm, int bits, int value) {
    struct inflate_state FAR *state;

    if (inflateStateCheck(strm)) return Z_STREAM_ERROR;
    if (bits == 0)
        return Z_OK;
    state = (struct inflate_state FAR *)strm->state;
    if (bits < 0) {
        state->hold = 0;
        state->bits = 0;
        return Z_OK;
    }
    if (bits > 16 || state->bits + (uInt)bits > 32) return Z_STREAM_ERROR;
    value &= (1L << bits) - 1;
    state->hold += (unsigned)value << state->bits;
    state->bits += (uInt)bits;
    return Z_OK;
}

#endif /* NOVGZ */

/*
   Return state with length and distance decoding tables and index sizes set to
   fixed code decoding.  Normally this returns fixed tables from inffixed.h.
   If BUILDFIXED is defined, then instead this routine builds the tables the
   first time it's called, and returns those tables the first time and
   thereafter.  This reduces the size of the code by about 2K bytes, in
   exchange for a little execution time.  However, BUILDFIXED should not be
   used for threaded applications, since the rewriting of the tables and virgin
   may not be thread-safe.
 */
local void fixedtables(struct inflate_state FAR *state) {
#ifdef BUILDFIXED
    static int virgin = 1;
    static code *lenfix, *distfix;
    static code fixed[544];

    /* build fixed huffman tables if first call (may not be thread safe) */
    if (virgin) {
        unsigned sym, bits;
        static code *next;

        /* literal/length table */
        sym = 0;
        while (sym < 144) state->lens[sym++] = 8;
        while (sym < 256) state->lens[sym++] = 9;
        while (sym < 280) state->lens[sym++] = 7;
        while (sym < 288) state->lens[sym++] = 8;
        next = fixed;
        lenfix = next;
        bits = 9;
        inflate_table(LENS, state->lens, 288, &(next), &(bits), state->work);

        /* distance table */
        sym = 0;
        while (sym < 32) state->lens[sym++] = 5;
        distfix = next;
        bits = 5;
        inflate_table(DISTS, state->lens, 32, &(next), &(bits), state->work);

        /* do this just once */
        virgin = 0;
    }
#else /* !BUILDFIXED */
#   include "inffixed.h"
#endif /* BUILDFIXED */
    state->lencode = lenfix;
    state->lenbits = 9;
    state->distcode = distfix;
    state->distbits = 5;
}

#ifdef MAKEFIXED
#include <stdio.h>

/*
   Write out the inffixed.h that is #include'd above.  Defining MAKEFIXED also
   defines BUILDFIXED, so the tables are built on the fly.  makefixed() writes
   those tables to stdout, which would be piped to inffixed.h.  A small program
   can simply call makefixed to do this:

    void makefixed(void);

    int main(void)
    {
        makefixed();
        return 0;
    }

   Then that can be linked with zlib built with MAKEFIXED defined and run:

    a.out > inffixed.h
 */
void makefixed(void)
{
    unsigned low, size;
    struct inflate_state state;

    fixedtables(&state);
    puts("    /* inffixed.h -- table for decoding fixed codes");
    puts("     * Generated automatically by makefixed().");
    puts("     */");
    puts("");
    puts("    /* WARNING: this file should *not* be used by applications.");
    puts("       It is part of the implementation of this library and is");
    puts("       subject to change. Applications should only use zlib.h.");
    puts("     */");
    puts("");
    size = 1U << 9;
    printf("    static const code lenfix[%u] = {", size);
    low = 0;
    for (;;) {
        if ((low % 7) == 0) printf("\n        ");
        printf("{%u,%u,%d}", (low & 127) == 99 ? 64 : state.lencode[low].op,
               state.lencode[low].bits, state.lencode[low].val);
        if (++low == size) break;
        putchar(',');
    }
    puts("\n    };");
    size = 1U << 5;
    printf("\n    static const code distfix[%u] = {", size);
    low = 0;
    for (;;) {
        if ((low % 6) == 0) printf("\n        ");
        printf("{%u,%u,%d}", state.distcode[low].op, state.distcode[low].bits,
               state.distcode[low].val);
        if (++low == size) break;
        putchar(',');
    }
    puts("\n    };");
}
#endif /* MAKEFIXED */

/*
   Update the window with the last wsize (normally 32K) bytes written before
   returning.  If window does not exist yet, create it.  This is only called
   when a window is already in use, or when output has been written during this
   inflate call, but the end of the deflate stream has not been reached yet.
   It is also called to create a window for dictionary data when a dictionary
   is loaded.

   Providing output buffers larger than 32K to inflate() should provide a speed
   advantage, since only the last 32K of output is copied to the sliding window
   upon return from inflate(), and since all distances after the first 32K of
   output will fall in the output data, making match copies simpler and faster.
   The advantage may be dependent on the size of the processor's data caches.
 */
local int updatewindow(z_streamp strm, const Bytef *end, unsigned copy) {
    struct inflate_state FAR *state;
    unsigned dist;

    state = (struct inflate_state FAR *)strm->state;

    /* if it hasn't been done already, allocate space for the window */
    if (state->window == Z_NULL) {
        state->window = (unsigned char FAR *)
                        ZALLOC(strm, 1U << state->wbits,
                               sizeof(unsigned char));
        if (state->window == Z_NULL) return 1;
    }

    /* if window not in use yet, initialize */
    if (state->wsize == 0) {
        state->wsize = 1U << state->wbits;
        state->wnext = 0;
        state->whave = 0;
    }

    /* copy state->wsize or less output bytes into the circular window */
    if (copy >= state->wsize) {
        zmemcpy(state->window, end - state->wsize, state->wsize);
        state->wnext = 0;
        state->whave = state->wsize;
    }
    else {
        dist = state->wsize - state->wnext;
        if (dist > copy) dist = copy;
        zmemcpy(state->window + state->wnext, end - copy, dist);
        copy -= dist;
        if (copy) {
            zmemcpy(state->window, end - copy, copy);
            state->wnext = copy;
            state->whave = state->wsize;
        }
        else {
            state->wnext += dist;
            if (state->wnext == state->wsize) state->wnext = 0;
            if (state->whave < state->wsize) state->whave += dist;
        }
    }
    return 0;
}

/* Macros for inflate(): */

/* check function to use adler32() for zlib or crc32() for gzip */
#ifdef GUNZIP
#  define UPDATE_CHECK(check, buf, len) \
    (state->flags ? crc32(check, buf, len) : adler32(check, buf, len))
#else
#  define UPDATE_CHECK(check, buf, len) adler32(check, buf, len)
#endif

/* check macros for header crc */
#ifdef GUNZIP
#  define CRC2(check, word) \
    do { \
        hbuf[0] = (unsigned char)(word); \
        hbuf[1] = (unsigned char)((word) >> 8); \
        check = crc32(check, hbuf, 2); \
    } while (0)

#  define CRC4(check, word) \
    do { \
        hbuf[0] = (unsigned char)(word); \
        hbuf[1] = (unsigned char)((word) >> 8); \
        hbuf[2] = (unsigned char)((word) >> 16); \
        hbuf[3] = (unsigned char)((word) >> 24); \
        check = crc32(check, hbuf, 4); \
    } while (0)
#endif

/* Load registers with state in inflate() for speed */
#define LOAD() \
    do { \
        put = strm->next_out; \
        left = strm->avail_out; \
        next = strm->next_in; \
        have = strm->avail_in; \
        hold = state->hold; \
        bits = state->bits; \
    } while (0)

/* Restore state from registers in inflate() */
#define RESTORE() \
    do { \
        strm->next_out = put; \
        strm->avail_out = left; \
        strm->next_in = next; \
        strm->avail_in = have; \
        state->hold = hold; \
        state->bits = bits; \
    } while (0)

/* Clear the input bit accumulator */
#define INITBITS() \
    do { \
        hold = 0; \
        bits = 0; \
    } while (0)

/* Get a byte of input into the bit accumulator, or return from inflate()
   if there is no input available. */
#define PULLBYTE() \
    do { \
        if (have == 0) goto inf_leave; \
        have--; \
        hold += (unsigned long)(*next++) << bits; \
        bits += 8; \
    } while (0)

/* Assure that there are at least n bits in the bit accumulator.  If there is
   not enough available input to do that, then return from inflate(). */
#define NEEDBITS(n) \
    do { \
        while (bits < (unsigned)(n)) \
            PULLBYTE(); \
    } while (0)

/* Return the low n bits of the bit accumulator (n < 16) */
#define BITS(n) \
    ((unsigned)hold & ((1U << (n)) - 1))

/* Remove n bits from the bit accumulator */
#define DROPBITS(n) \
    do { \
        hold >>= (n); \
        bits -= (unsigned)(n); \
    } while (0)

/* Remove zero to seven bits as needed to go to a byte boundary */
#define BYTEBITS() \
    do { \
        hold >>= bits & 7; \
        bits -= bits & 7; \
    } while (0)

/*
   inflate() uses a state machine to process as much input data and generate as
   much output data as possible before returning.  The state machine is
   structured roughly as follows:

    for (;;) switch (state) {
    ...
    case STATEn:
