/* pngrutil.c - utilities to read a PNG file
 *
 * Copyright (c) 2018-2024 Cosmin Truta
 * Copyright (c) 1998-2002,2004,2006-2018 Glenn Randers-Pehrson
 * Copyright (c) 1996-1997 Andreas Dilger
 * Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 *
 * This file contains routines that are only called from within
 * libpng itself during the course of reading an image.
 */

#include "pngpriv.h"

#ifdef PNG_READ_SUPPORTED

#ifdef PNG_READ_INTERLACING_SUPPORTED
/* Arrays to facilitate interlacing - use pass (0 - 6) as index. */

/* Start of interlace block */
static const png_byte png_pass_start[7] = {0, 4, 0, 2, 0, 1, 0};
/* Offset to next interlace block */
static const png_byte png_pass_inc[7] = {8, 8, 4, 4, 2, 2, 1};
/* Start of interlace block in the y direction */
static const png_byte png_pass_ystart[7] = {0, 0, 4, 0, 2, 0, 1};
/* Offset to next interlace block in the y direction */
static const png_byte png_pass_yinc[7] = {8, 8, 8, 4, 4, 2, 2};

/* TODO: Move these arrays to a common utility module to avoid duplication. */
#endif

png_uint_32 PNGAPI
png_get_uint_31(png_const_structrp png_ptr, png_const_bytep buf)
{
   png_uint_32 uval = png_get_uint_32(buf);

   if (uval > PNG_UINT_31_MAX)
      png_error(png_ptr, "PNG unsigned integer out of range");

   return uval;
}

#if defined(PNG_READ_gAMA_SUPPORTED) || defined(PNG_READ_cHRM_SUPPORTED)
/* The following is a variation on the above for use with the fixed
 * point values used for gAMA and cHRM.  Instead of png_error it
 * issues a warning and returns (-1) - an invalid value because both
 * gAMA and cHRM use *unsigned* integers for fixed point values.
 */
#define PNG_FIXED_ERROR (-1)

static png_fixed_point /* PRIVATE */
png_get_fixed_point(png_structrp png_ptr, png_const_bytep buf)
{
   png_uint_32 uval = png_get_uint_32(buf);

   if (uval <= PNG_UINT_31_MAX)
      return (png_fixed_point)uval; /* known to be in range */

   /* The caller can turn off the warning by passing NULL. */
   if (png_ptr != NULL)
      png_warning(png_ptr, "PNG fixed point integer out of range");

   return PNG_FIXED_ERROR;
}
#endif

#ifdef PNG_READ_INT_FUNCTIONS_SUPPORTED
/* NOTE: the read macros will obscure these definitions, so that if
 * PNG_USE_READ_MACROS is set the library will not use them internally,
 * but the APIs will still be available externally.
 *
 * The parentheses around "PNGAPI function_name" in the following three
 * functions are necessary because they allow the macros to co-exist with
 * these (unused but exported) functions.
 */

/* Grab an unsigned 32-bit integer from a buffer in big-endian format. */
png_uint_32 (PNGAPI
png_get_uint_32)(png_const_bytep buf)
{
   png_uint_32 uval =
       ((png_uint_32)(*(buf    )) << 24) +
       ((png_uint_32)(*(buf + 1)) << 16) +
       ((png_uint_32)(*(buf + 2)) <<  8) +
       ((png_uint_32)(*(buf + 3))      ) ;

   return uval;
}

/* Grab a signed 32-bit integer from a buffer in big-endian format.  The
 * data is stored in the PNG file in two's complement format and there
 * is no guarantee that a 'png_int_32' is exactly 32 bits, therefore
 * the following code does a two's complement to native conversion.
 */
png_int_32 (PNGAPI
png_get_int_32)(png_const_bytep buf)
{
   png_uint_32 uval = png_get_uint_32(buf);
   if ((uval & 0x80000000) == 0) /* non-negative */
      return (png_int_32)uval;

   uval = (uval ^ 0xffffffff) + 1;  /* 2's complement: -x = ~x+1 */
   if ((uval & 0x80000000) == 0) /* no overflow */
      return -(png_int_32)uval;
   /* The following has to be safe; this function only gets called on PNG data
    * and if we get here that data is invalid.  0 is the most safe value and
    * if not then an attacker would surely just generate a PNG with 0 instead.
    */
   return 0;
}

/* Grab an unsigned 16-bit integer from a buffer in big-endian format. */
png_uint_16 (PNGAPI
png_get_uint_16)(png_const_bytep buf)
{
   /* ANSI-C requires an int value to accommodate at least 16 bits so this
    * works and allows the compiler not to worry about possible narrowing
    * on 32-bit systems.  (Pre-ANSI systems did not make integers smaller
    * than 16 bits either.)
    */
   unsigned int val =
       ((unsigned int)(*buf) << 8) +
       ((unsigned int)(*(buf + 1)));

   return (png_uint_16)val;
}

#endif /* READ_INT_FUNCTIONS */

/* Read and check the PNG file signature */
void /* PRIVATE */
png_read_sig(png_structrp png_ptr, png_inforp info_ptr)
{
   size_t num_checked, num_to_check;

   /* Exit if the user application does not expect a signature. */
   if (png_ptr->sig_bytes >= 8)
      return;

   num_checked = png_ptr->sig_bytes;
   num_to_check = 8 - num_checked;

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_SIGNATURE;
#endif

   /* The signature must be serialized in a single I/O call. */
   png_read_data(png_ptr, &(info_ptr->signature[num_checked]), num_to_check);
   png_ptr->sig_bytes = 8;

   if (png_sig_cmp(info_ptr->signature, num_checked, num_to_check) != 0)
   {
      if (num_checked < 4 &&
          png_sig_cmp(info_ptr->signature, num_checked, num_to_check - 4) != 0)
         png_error(png_ptr, "Not a PNG file");
      else
         png_error(png_ptr, "PNG file corrupted by ASCII conversion");
   }
   if (num_checked < 3)
      png_ptr->mode |= PNG_HAVE_PNG_SIGNATURE;
}

/* Read the chunk header (length + type name).
 * Put the type name into png_ptr->chunk_name, and return the length.
 */
png_uint_32 /* PRIVATE */
png_read_chunk_header(png_structrp png_ptr)
{
   png_byte buf[8];
   png_uint_32 length;

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_HDR;
#endif

   /* Read the length and the chunk name.
    * This must be performed in a single I/O call.
    */
   png_read_data(png_ptr, buf, 8);
   length = png_get_uint_31(png_ptr, buf);

   /* Put the chunk name into png_ptr->chunk_name. */
   png_ptr->chunk_name = PNG_CHUNK_FROM_STRING(buf+4);

   png_debug2(0, "Reading chunk typeid = 0x%lx, length = %lu",
       (unsigned long)png_ptr->chunk_name, (unsigned long)length);

   /* Reset the crc and run it over the chunk name. */
   png_reset_crc(png_ptr);
   png_calculate_crc(png_ptr, buf + 4, 4);

   /* Check to see if chunk name is valid. */
   png_check_chunk_name(png_ptr, png_ptr->chunk_name);

   /* Check for too-large chunk length */
   png_check_chunk_length(png_ptr, length);

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_DATA;
#endif

   return length;
}

/* Read data, and (optionally) run it through the CRC. */
void /* PRIVATE */
png_crc_read(png_structrp png_ptr, png_bytep buf, png_uint_32 length)
{
   if (png_ptr == NULL)
      return;

   png_read_data(png_ptr, buf, length);
   png_calculate_crc(png_ptr, buf, length);
}

/* Optionally skip data and then check the CRC.  Depending on whether we
 * are reading an ancillary or critical chunk, and how the program has set
 * things up, we may calculate the CRC on the data and print a message.
 * Returns '1' if there was a CRC error, '0' otherwise.
 */
int /* PRIVATE */
png_crc_finish(png_structrp png_ptr, png_uint_32 skip)
{
   /* The size of the local buffer for inflate is a good guess as to a
    * reasonable size to use for buffering reads from the application.
    */
   while (skip > 0)
   {
      png_uint_32 len;
      png_byte tmpbuf[PNG_INFLATE_BUF_SIZE];

      len = (sizeof tmpbuf);
      if (len > skip)
         len = skip;
      skip -= len;

      png_crc_read(png_ptr, tmpbuf, len);
   }

   if (png_crc_error(png_ptr) != 0)
   {
      if (PNG_CHUNK_ANCILLARY(png_ptr->chunk_name) != 0 ?
          (png_ptr->flags & PNG_FLAG_CRC_ANCILLARY_NOWARN) == 0 :
          (png_ptr->flags & PNG_FLAG_CRC_CRITICAL_USE) != 0)
      {
         png_chunk_warning(png_ptr, "CRC error");
      }

      else
         png_chunk_error(png_ptr, "CRC error");

      return 1;
   }

   return 0;
}

/* Compare the CRC stored in the PNG file with that calculated by libpng from
 * the data it has read thus far.
 */
int /* PRIVATE */
png_crc_error(png_structrp png_ptr)
{
   png_byte crc_bytes[4];
   png_uint_32 crc;
   int need_crc = 1;

   if (PNG_CHUNK_ANCILLARY(png_ptr->chunk_name) != 0)
   {
      if ((png_ptr->flags & PNG_FLAG_CRC_ANCILLARY_MASK) ==
          (PNG_FLAG_CRC_ANCILLARY_USE | PNG_FLAG_CRC_ANCILLARY_NOWARN))
         need_crc = 0;
   }

   else /* critical */
   {
      if ((png_ptr->flags & PNG_FLAG_CRC_CRITICAL_IGNORE) != 0)
         need_crc = 0;
   }

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_CRC;
#endif

   /* The chunk CRC must be serialized in a single I/O call. */
   png_read_data(png_ptr, crc_bytes, 4);

   if (need_crc != 0)
   {
      crc = png_get_uint_32(crc_bytes);
      return crc != png_ptr->crc;
   }

   else
      return 0;
}

#if defined(PNG_READ_iCCP_SUPPORTED) || defined(PNG_READ_iTXt_SUPPORTED) ||\
    defined(PNG_READ_pCAL_SUPPORTED) || defined(PNG_READ_sCAL_SUPPORTED) ||\
    defined(PNG_READ_sPLT_SUPPORTED) || defined(PNG_READ_tEXt_SUPPORTED) ||\
    defined(PNG_READ_zTXt_SUPPORTED) || defined(PNG_SEQUENTIAL_READ_SUPPORTED)
/* Manage the read buffer; this simply reallocates the buffer if it is not small
 * enough (or if it is not allocated).  The routine returns a pointer to the
 * buffer; if an error occurs and 'warn' is set the routine returns NULL, else
 * it will call png_error (via png_malloc) on failure.  (warn == 2 means
 * 'silent').
 */
static png_bytep
png_read_buffer(png_structrp png_ptr, png_alloc_size_t new_size, int warn)
{
   png_bytep buffer = png_ptr->read_buffer;

   if (buffer != NULL && new_size > png_ptr->read_buffer_size)
   {
      png_ptr->read_buffer = NULL;
      png_ptr->read_buffer_size = 0;
      png_free(png_ptr, buffer);
      buffer = NULL;
   }

   if (buffer == NULL)
   {
      buffer = png_voidcast(png_bytep, png_malloc_base(png_ptr, new_size));

      if (buffer != NULL)
      {
         memset(buffer, 0, new_size); /* just in case */
         png_ptr->read_buffer = buffer;
         png_ptr->read_buffer_size = new_size;
      }

      else if (warn < 2) /* else silent */
      {
         if (warn != 0)
             png_chunk_warning(png_ptr, "insufficient memory to read chunk");

         else
             png_chunk_error(png_ptr, "insufficient memory to read chunk");
      }
   }

   return buffer;
}
#endif /* READ_iCCP|iTXt|pCAL|sCAL|sPLT|tEXt|zTXt|SEQUENTIAL_READ */

/* png_inflate_claim: claim the zstream for some nefarious purpose that involves
 * decompression.  Returns Z_OK on success, else a zlib error code.  It checks
 * the owner but, in final release builds, just issues a warning if some other
 * chunk apparently owns the stream.  Prior to release it does a png_error.
 */
static int
png_inflate_claim(png_structrp png_ptr, png_uint_32 owner)
{
   if (png_ptr->zowner != 0)
   {
      char msg[64];

      PNG_STRING_FROM_CHUNK(msg, png_ptr->zowner);
      /* So the message that results is "<chunk> using zstream"; this is an
       * internal error, but is very useful for debugging.  i18n requirements
       * are minimal.
       */
      (void)png_safecat(msg, (sizeof msg), 4, " using zstream");
#if PNG_RELEASE_BUILD
      png_chunk_warning(png_ptr, msg);
      png_ptr->zowner = 0;
#else
      png_chunk_error(png_ptr, msg);
#endif
   }

   /* Implementation note: unlike 'png_deflate_claim' this internal function
    * does not take the size of the data as an argument.  Some efficiency could
    * be gained by using this when it is known *if* the zlib stream itself does
    * not record the number; however, this is an illusion: the original writer
    * of the PNG may have selected a lower window size, and we really must
    * follow that because, for systems with with limited capabilities, we
    * would otherwise reject the application's attempts to use a smaller window
    * size (zlib doesn't have an interface to say "this or lower"!).
    *
    * inflateReset2 was added to zlib 1.2.4; before this the window could not be
    * reset, therefore it is necessary to always allocate the maximum window
    * size with earlier zlibs just in case later compressed chunks need it.
    */
   {
      int ret; /* zlib return code */
#if ZLIB_VERNUM >= 0x1240
      int window_bits = 0;

# if defined(PNG_SET_OPTION_SUPPORTED) && defined(PNG_MAXIMUM_INFLATE_WINDOW)
      if (((png_ptr->options >> PNG_MAXIMUM_INFLATE_WINDOW) & 3) ==
          PNG_OPTION_ON)
      {
         window_bits = 15;
         png_ptr->zstream_start = 0; /* fixed window size */
      }

      else
      {
         png_ptr->zstream_start = 1;
      }
# endif

#endif /* ZLIB_VERNUM >= 0x1240 */

      /* Set this for safety, just in case the previous owner left pointers to
       * memory allocations.
       */
      png_ptr->zstream.next_in = NULL;
      png_ptr->zstream.avail_in = 0;
      png_ptr->zstream.next_out = NULL;
      png_ptr->zstream.avail_out = 0;

      if ((png_ptr->flags & PNG_FLAG_ZSTREAM_INITIALIZED) != 0)
      {
#if ZLIB_VERNUM >= 0x1240
         ret = inflateReset2(&png_ptr->zstream, window_bits);
#else
         ret = inflateReset(&png_ptr->zstream);
#endif
      }

      else
      {
#if ZLIB_VERNUM >= 0x1240
         ret = inflateInit2(&png_ptr->zstream, window_bits);
#else
         ret = inflateInit(&png_ptr->zstream);
#endif

         if (ret == Z_OK)
            png_ptr->flags |= PNG_FLAG_ZSTREAM_INITIALIZED;
      }

#ifdef PNG_DISABLE_ADLER32_CHECK_SUPPORTED
      if (((png_ptr->options >> PNG_IGNORE_ADLER32) & 3) == PNG_OPTION_ON)
         /* Turn off validation of the ADLER32 checksum in IDAT chunks */
         ret = inflateValidate(&png_ptr->zstream, 0);
#endif

      if (ret == Z_OK)
         png_ptr->zowner = owner;

      else
         png_zstream_error(png_ptr, ret);

      return ret;
   }

#ifdef window_bits
# undef window_bits
#endif
}

#if ZLIB_VERNUM >= 0x1240
/* Handle the start of the inflate stream if we called inflateInit2(strm,0);
 * in this case some zlib versions skip validation of the CINFO field and, in
 * certain circumstances, libpng may end up displaying an invalid image, in
 * contrast to implementations that call zlib in the normal way (e.g. libpng
 * 1.5).
 */
int /* PRIVATE */
png_zlib_inflate(png_structrp png_ptr, int flush)
{
   if (png_ptr->zstream_start && png_ptr->zstream.avail_in > 0)
   {
      if ((*png_ptr->zstream.next_in >> 4) > 7)
      {
         png_ptr->zstream.msg = "invalid window size (libpng)";
         return Z_DATA_ERROR;
      }

      png_ptr->zstream_start = 0;
   }

   return inflate(&png_ptr->zstream, flush);
}
#endif /* Zlib >= 1.2.4 */

#ifdef PNG_READ_COMPRESSED_TEXT_SUPPORTED
#if defined(PNG_READ_zTXt_SUPPORTED) || defined (PNG_READ_iTXt_SUPPORTED)
/* png_inflate now returns zlib error codes including Z_OK and Z_STREAM_END to
 * allow the caller to do multiple calls if required.  If the 'finish' flag is
 * set Z_FINISH will be passed to the final inflate() call and Z_STREAM_END must
 * be returned or there has been a problem, otherwise Z_SYNC_FLUSH is used and
 * Z_OK or Z_STREAM_END will be returned on success.
 *
 * The input and output sizes are updated to the actual amounts of data consumed
 * or written, not the amount available (as in a z_stream).  The data pointers
 * are not changed, so the next input is (data+input_size) and the next
 * available output is (output+output_size).
 */
static int
png_inflate(png_structrp png_ptr, png_uint_32 owner, int finish,
    /* INPUT: */ png_const_bytep input, png_uint_32p input_size_ptr,
    /* OUTPUT: */ png_bytep output, png_alloc_size_t *output_size_ptr)
{
   if (png_ptr->zowner == owner) /* Else not claimed */
   {
      int ret;
      png_alloc_size_t avail_out = *output_size_ptr;
      png_uint_32 avail_in = *input_size_ptr;

      /* zlib can't necessarily handle more than 65535 bytes at once (i.e. it
       * can't even necessarily handle 65536 bytes) because the type uInt is
       * "16 bits or more".  Consequently it is necessary to chunk the input to
       * zlib.  This code uses ZLIB_IO_MAX, from pngpriv.h, as the maximum (the
       * maximum value that can be stored in a uInt.)  It is possible to set
       * ZLIB_IO_MAX to a lower value in pngpriv.h and this may sometimes have
       * a performance advantage, because it reduces the amount of data accessed
       * at each step and that may give the OS more time to page it in.
       */
      png_ptr->zstream.next_in = PNGZ_INPUT_CAST(input);
      /* avail_in and avail_out are set below from 'size' */
      png_ptr->zstream.avail_in = 0;
      png_ptr->zstream.avail_out = 0;

      /* Read directly into the output if it is available (this is set to
       * a local buffer below if output is NULL).
       */
      if (output != NULL)
         png_ptr->zstream.next_out = output;

      do
      {
         uInt avail;
         Byte local_buffer[PNG_INFLATE_BUF_SIZE];

         /* zlib INPUT BUFFER */
         /* The setting of 'avail_in' used to be outside the loop; by setting it
          * inside it is possible to chunk the input to zlib and simply rely on
          * zlib to advance the 'next_in' pointer.  This allows arbitrary
          * amounts of data to be passed through zlib at the unavoidable cost of
          * requiring a window save (memcpy of up to 32768 output bytes)
          * every ZLIB_IO_MAX input bytes.
          */
         avail_in += png_ptr->zstream.avail_in; /* not consumed last time */

         avail = ZLIB_IO_MAX;

         if (avail_in < avail)
            avail = (uInt)avail_in; /* safe: < than ZLIB_IO_MAX */

         avail_in -= avail;
         png_ptr->zstream.avail_in = avail;

         /* zlib OUTPUT BUFFER */
         avail_out += png_ptr->zstream.avail_out; /* not written last time */

         avail = ZLIB_IO_MAX; /* maximum zlib can process */

         if (output == NULL)
         {
            /* Reset the output buffer each time round if output is NULL and
             * make available the full buffer, up to 'remaining_space'
             */
            png_ptr->zstream.next_out = local_buffer;
            if ((sizeof local_buffer) < avail)
               avail = (sizeof local_buffer);
         }

         if (avail_out < avail)
            avail = (uInt)avail_out; /* safe: < ZLIB_IO_MAX */

         png_ptr->zstream.avail_out = avail;
         avail_out -= avail;

         /* zlib inflate call */
         /* In fact 'avail_out' may be 0 at this point, that happens at the end
          * of the read when the final LZ end code was not passed at the end of
          * the previous chunk of input data.  Tell zlib if we have reached the
          * end of the output buffer.
          */
         ret = PNG_INFLATE(png_ptr, avail_out > 0 ? Z_NO_FLUSH :
             (finish ? Z_FINISH : Z_SYNC_FLUSH));
      } while (ret == Z_OK);

      /* For safety kill the local buffer pointer now */
      if (output == NULL)
         png_ptr->zstream.next_out = NULL;

      /* Claw back the 'size' and 'remaining_space' byte counts. */
      avail_in += png_ptr->zstream.avail_in;
      avail_out += png_ptr->zstream.avail_out;

      /* Update the input and output sizes; the updated values are the amount
       * consumed or written, effectively the inverse of what zlib uses.
       */
      if (avail_out > 0)
         *output_size_ptr -= avail_out;

      if (avail_in > 0)
         *input_size_ptr -= avail_in;

      /* Ensure png_ptr->zstream.msg is set (even in the success case!) */
      png_zstream_error(png_ptr, ret);
      return ret;
   }

   else
   {
      /* This is a bad internal error.  The recovery assigns to the zstream msg
       * pointer, which is not owned by the caller, but this is safe; it's only
       * used on errors!
       */
      png_ptr->zstream.msg = PNGZ_MSG_CAST("zstream unclaimed");
      return Z_STREAM_ERROR;
   }
}

/*
 * Decompress trailing data in a chunk.  The assumption is that read_buffer
 * points at an allocated area holding the contents of a chunk with a
 * trailing compressed part.  What we get back is an allocated area
 * holding the original prefix part and an uncompressed version of the
 * trailing part (the malloc area passed in is freed).
 */
static int
png_decompress_chunk(png_structrp png_ptr,
    png_uint_32 chunklength, png_uint_32 prefix_size,
    png_alloc_size_t *newlength /* must be initialized to the maximum! */,
    int terminate /*add a '\0' to the end of the uncompressed data*/)
{
   /* TODO: implement different limits for different types of chunk.
    *
    * The caller supplies *newlength set to the maximum length of the
    * uncompressed data, but this routine allocates space for the prefix and
    * maybe a '\0' terminator too.  We have to assume that 'prefix_size' is
    * limited only by the maximum chunk size.
    */
   png_alloc_size_t limit = PNG_SIZE_MAX;

# ifdef PNG_SET_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_malloc_max > 0 &&
       png_ptr->user_chunk_malloc_max < limit)
      limit = png_ptr->user_chunk_malloc_max;
# elif PNG_USER_CHUNK_MALLOC_MAX > 0
   if (PNG_USER_CHUNK_MALLOC_MAX < limit)
      limit = PNG_USER_CHUNK_MALLOC_MAX;
# endif

   if (limit >= prefix_size + (terminate != 0))
   {
      int ret;

      limit -= prefix_size + (terminate != 0);

      if (limit < *newlength)
         *newlength = limit;

      /* Now try to claim the stream. */
      ret = png_inflate_claim(png_ptr, png_ptr->chunk_name);

      if (ret == Z_OK)
      {
         png_uint_32 lzsize = chunklength - prefix_size;

         ret = png_inflate(png_ptr, png_ptr->chunk_name, 1/*finish*/,
             /* input: */ png_ptr->read_buffer + prefix_size, &lzsize,
             /* output: */ NULL, newlength);

         if (ret == Z_STREAM_END)
         {
            /* Use 'inflateReset' here, not 'inflateReset2' because this
             * preserves the previously decided window size (otherwise it would
             * be necessary to store the previous window size.)  In practice
             * this doesn't matter anyway, because png_inflate will call inflate
             * with Z_FINISH in almost all cases, so the window will not be
             * maintained.
             */
            if (inflateReset(&png_ptr->zstream) == Z_OK)
            {
               /* Because of the limit checks above we know that the new,
                * expanded, size will fit in a size_t (let alone an
                * png_alloc_size_t).  Use png_malloc_base here to avoid an
                * extra OOM message.
                */
               png_alloc_size_t new_size = *newlength;
               png_alloc_size_t buffer_size = prefix_size + new_size +
                   (terminate != 0);
               png_bytep text = png_voidcast(png_bytep, png_malloc_base(png_ptr,
                   buffer_size));

               if (text != NULL)
               {
                  memset(text, 0, buffer_size);

                  ret = png_inflate(png_ptr, png_ptr->chunk_name, 1/*finish*/,
                      png_ptr->read_buffer + prefix_size, &lzsize,
                      text + prefix_size, newlength);

                  if (ret == Z_STREAM_END)
                  {
                     if (new_size == *newlength)
                     {
                        if (terminate != 0)
                           text[prefix_size + *newlength] = 0;

                        if (prefix_size > 0)
                           memcpy(text, png_ptr->read_buffer, prefix_size);

                        {
                           png_bytep old_ptr = png_ptr->read_buffer;

                           png_ptr->read_buffer = text;
                           png_ptr->read_buffer_size = buffer_size;
                           text = old_ptr; /* freed below */
                        }
                     }

                     else
                     {
                        /* The size changed on the second read, there can be no
                         * guarantee that anything is correct at this point.
                         * The 'msg' pointer has been set to "unexpected end of
                         * LZ stream", which is fine, but return an error code
                         * that the caller won't accept.
                         */
                        ret = PNG_UNEXPECTED_ZLIB_RETURN;
                     }
                  }

                  else if (ret == Z_OK)
                     ret = PNG_UNEXPECTED_ZLIB_RETURN; /* for safety */

                  /* Free the text pointer (this is the old read_buffer on
                   * success)
                   */
                  png_free(png_ptr, text);

                  /* This really is very benign, but it's still an error because
                   * the extra space may otherwise be used as a Trojan Horse.
                   */
                  if (ret == Z_STREAM_END &&
                      chunklength - prefix_size != lzsize)
                     png_chunk_benign_error(png_ptr, "extra compressed data");
               }

               else
               {
                  /* Out of memory allocating the buffer */
                  ret = Z_MEM_ERROR;
                  png_zstream_error(png_ptr, Z_MEM_ERROR);
               }
            }

            else
            {
               /* inflateReset failed, store the error message */
               png_zstream_error(png_ptr, ret);
               ret = PNG_UNEXPECTED_ZLIB_RETURN;
            }
         }

         else if (ret == Z_OK)
            ret = PNG_UNEXPECTED_ZLIB_RETURN;

         /* Release the claimed stream */
         png_ptr->zowner = 0;
      }

      else /* the claim failed */ if (ret == Z_STREAM_END) /* impossible! */
         ret = PNG_UNEXPECTED_ZLIB_RETURN;

      return ret;
   }

   else
   {
      /* Application/configuration limits exceeded */
      png_zstream_error(png_ptr, Z_MEM_ERROR);
      return Z_MEM_ERROR;
   }
}
#endif /* READ_zTXt || READ_iTXt */
#endif /* READ_COMPRESSED_TEXT */

#ifdef PNG_READ_iCCP_SUPPORTED
/* Perform a partial read and decompress, producing 'avail_out' bytes and
 * reading from the current chunk as required.
 */
static int
png_inflate_read(png_structrp png_ptr, png_bytep read_buffer, uInt read_size,
    png_uint_32p chunk_bytes, png_bytep next_out, png_alloc_size_t *out_size,
    int finish)
{
   if (png_ptr->zowner == png_ptr->chunk_name)
   {
      int ret;

      /* next_in and avail_in must have been initialized by the caller. */
      png_ptr->zstream.next_out = next_out;
      png_ptr->zstream.avail_out = 0; /* set in the loop */

      do
      {
         if (png_ptr->zstream.avail_in == 0)
         {
            if (read_size > *chunk_bytes)
               read_size = (uInt)*chunk_bytes;
            *chunk_bytes -= read_size;

            if (read_size > 0)
               png_crc_read(png_ptr, read_buffer, read_size);

            png_ptr->zstream.next_in = read_buffer;
            png_ptr->zstream.avail_in = read_size;
         }

         if (png_ptr->zstream.avail_out == 0)
         {
            uInt avail = ZLIB_IO_MAX;
            if (avail > *out_size)
               avail = (uInt)*out_size;
            *out_size -= avail;

            png_ptr->zstream.avail_out = avail;
         }

         /* Use Z_SYNC_FLUSH when there is no more chunk data to ensure that all
          * the available output is produced; this allows reading of truncated
          * streams.
          */
         ret = PNG_INFLATE(png_ptr, *chunk_bytes > 0 ?
             Z_NO_FLUSH : (finish ? Z_FINISH : Z_SYNC_FLUSH));
      }
      while (ret == Z_OK && (*out_size > 0 || png_ptr->zstream.avail_out > 0));

      *out_size += png_ptr->zstream.avail_out;
      png_ptr->zstream.avail_out = 0; /* Should not be required, but is safe */

      /* Ensure the error message pointer is always set: */
      png_zstream_error(png_ptr, ret);
      return ret;
   }

   else
   {
      png_ptr->zstream.msg = PNGZ_MSG_CAST("zstream unclaimed");
      return Z_STREAM_ERROR;
   }
}
#endif /* READ_iCCP */

/* Read and check the IDHR chunk */

void /* PRIVATE */
png_handle_IHDR(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[13];
   png_uint_32 width, height;
   int bit_depth, color_type, compression_type, filter_type;
   int interlace_type;

   png_debug(1, "in png_handle_IHDR");

   if ((png_ptr->mode & PNG_HAVE_IHDR) != 0)
      png_chunk_error(png_ptr, "out of place");

   /* Check the length */
   if (length != 13)
      png_chunk_error(png_ptr, "invalid");

   png_ptr->mode |= PNG_HAVE_IHDR;

   png_crc_read(png_ptr, buf, 13);
   png_crc_finish(png_ptr, 0);

   width = png_get_uint_31(png_ptr, buf);
   height = png_get_uint_31(png_ptr, buf + 4);
   bit_depth = buf[8];
   color_type = buf[9];
   compression_type = buf[10];
   filter_type = buf[11];
   interlace_type = buf[12];

   /* Set internal variables */
   png_ptr->width = width;
   png_ptr->height = height;
   png_ptr->bit_depth = (png_byte)bit_depth;
   png_ptr->interlaced = (png_byte)interlace_type;
   png_ptr->color_type = (png_byte)color_type;
#ifdef PNG_MNG_FEATURES_SUPPORTED
   png_ptr->filter_type = (png_byte)filter_type;
#endif
   png_ptr->compression_type = (png_byte)compression_type;

   /* Find number of channels */
   switch (png_ptr->color_type)
   {
      default: /* invalid, png_set_IHDR calls png_error */
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_PALETTE:
         png_ptr->channels = 1;
         break;

      case PNG_COLOR_TYPE_RGB:
         png_ptr->channels = 3;
         break;

      case PNG_COLOR_TYPE_GRAY_ALPHA:
         png_ptr->channels = 2;
         break;

      case PNG_COLOR_TYPE_RGB_ALPHA:
         png_ptr->channels = 4;
         break;
   }

   /* Set up other useful info */
   png_ptr->pixel_depth = (png_byte)(png_ptr->bit_depth * png_ptr->channels);
   png_ptr->rowbytes = PNG_ROWBYTES(png_ptr->pixel_depth, png_ptr->width);
   png_debug1(3, "bit_depth = %d", png_ptr->bit_depth);
   png_debug1(3, "channels = %d", png_ptr->channels);
   png_debug1(3, "rowbytes = %lu", (unsigned long)png_ptr->rowbytes);
   png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth,
       color_type, interlace_type, compression_type, filter_type);
}

/* Read and check the palette */
void /* PRIVATE */
png_handle_PLTE(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_color palette[PNG_MAX_PALETTE_LENGTH];
   int max_palette_length, num, i;
#ifdef PNG_POINTER_INDEXING_SUPPORTED
   png_colorp pal_ptr;
#endif

   png_debug(1, "in png_handle_PLTE");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   /* Moved to before the 'after IDAT' check below because otherwise duplicate
    * PLTE chunks are potentially ignored (the spec says there shall not be more
    * than one PLTE, the error is not treated as benign, so this check trumps
    * the requirement that PLTE appears before IDAT.)
    */
   else if ((png_ptr->mode & PNG_HAVE_PLTE) != 0)
      png_chunk_error(png_ptr, "duplicate");

   else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0)
   {
      /* This is benign because the non-benign error happened before, when an
       * IDAT was encountered in a color-mapped image with no PLTE.
       */
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   png_ptr->mode |= PNG_HAVE_PLTE;

   if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) == 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "ignored in grayscale PNG");
      return;
   }

#ifndef PNG_READ_OPT_PLTE_SUPPORTED
   if (png_ptr->color_type != PNG_COLOR_TYPE_PALETTE)
   {
      png_crc_finish(png_ptr, length);
      return;
   }
#endif

   if (length > 3*PNG_MAX_PALETTE_LENGTH || length % 3)
   {
      png_crc_finish(png_ptr, length);

      if (png_ptr->color_type != PNG_COLOR_TYPE_PALETTE)
         png_chunk_benign_error(png_ptr, "invalid");

      else
         png_chunk_error(png_ptr, "invalid");

      return;
   }

   /* The cast is safe because 'length' is less than 3*PNG_MAX_PALETTE_LENGTH */
   num = (int)length / 3;

   /* If the palette has 256 or fewer entries but is too large for the bit
    * depth, we don't issue an error, to preserve the behavior of previous
    * libpng versions. We silently truncate the unused extra palette entries
    * here.
    */
   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      max_palette_length = (1 << png_ptr->bit_depth);
   else
      max_palette_length = PNG_MAX_PALETTE_LENGTH;

   if (num > max_palette_length)
      num = max_palette_length;

#ifdef PNG_POINTER_INDEXING_SUPPORTED
   for (i = 0, pal_ptr = palette; i < num; i++, pal_ptr++)
   {
      png_byte buf[3];

      png_crc_read(png_ptr, buf, 3);
      pal_ptr->red = buf[0];
      pal_ptr->green = buf[1];
      pal_ptr->blue = buf[2];
   }
#else
   for (i = 0; i < num; i++)
   {
      png_byte buf[3];

      png_crc_read(png_ptr, buf, 3);
      /* Don't depend upon png_color being any order */
      palette[i].red = buf[0];
      palette[i].green = buf[1];
      palette[i].blue = buf[2];
   }
#endif

   /* If we actually need the PLTE chunk (ie for a paletted image), we do
    * whatever the normal CRC configuration tells us.  However, if we
    * have an RGB image, the PLTE can be considered ancillary, so
    * we will act as though it is.
    */
#ifndef PNG_READ_OPT_PLTE_SUPPORTED
   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
#endif
   {
      png_crc_finish(png_ptr, (png_uint_32) (length - (unsigned int)num * 3));
   }

#ifndef PNG_READ_OPT_PLTE_SUPPORTED
   else if (png_crc_error(png_ptr) != 0)  /* Only if we have a CRC error */
   {
      /* If we don't want to use the data from an ancillary chunk,
       * we have two options: an error abort, or a warning and we
       * ignore the data in this chunk (which should be OK, since
       * it's considered ancillary for a RGB or RGBA image).
       *
       * IMPLEMENTATION NOTE: this is only here because png_crc_finish uses the
       * chunk type to determine whether to check the ancillary or the critical
       * flags.
       */
      if ((png_ptr->flags & PNG_FLAG_CRC_ANCILLARY_USE) == 0)
      {
         if ((png_ptr->flags & PNG_FLAG_CRC_ANCILLARY_NOWARN) != 0)
            return;

         else
            png_chunk_error(png_ptr, "CRC error");
      }

      /* Otherwise, we (optionally) emit a warning and use the chunk. */
      else if ((png_ptr->flags & PNG_FLAG_CRC_ANCILLARY_NOWARN) == 0)
         png_chunk_warning(png_ptr, "CRC error");
   }
#endif

   /* TODO: png_set_PLTE has the side effect of setting png_ptr->palette to its
    * own copy of the palette.  This has the side effect that when png_start_row
    * is called (this happens after any call to png_read_update_info) the
    * info_ptr palette gets changed.  This is extremely unexpected and
    * confusing.
    *
    * Fix this by not sharing the palette in this way.
    */
   png_set_PLTE(png_ptr, info_ptr, palette, num);

   /* The three chunks, bKGD, hIST and tRNS *must* appear after PLTE and before
    * IDAT.  Prior to 1.6.0 this was not checked; instead the code merely
    * checked the apparent validity of a tRNS chunk inserted before PLTE on a
    * palette PNG.  1.6.0 attempts to rigorously follow the standard and
    * therefore does a benign error if the erroneous condition is detected *and*
    * cancels the tRNS if the benign error returns.  The alternative is to
    * amend the standard since it would be rather hypocritical of the standards
    * maintainers to ignore it.
    */
#ifdef PNG_READ_tRNS_SUPPORTED
   if (png_ptr->num_trans > 0 ||
       (info_ptr != NULL && (info_ptr->valid & PNG_INFO_tRNS) != 0))
   {
      /* Cancel this because otherwise it would be used if the transforms
       * require it.  Don't cancel the 'valid' flag because this would prevent
       * detection of duplicate chunks.
       */
      png_ptr->num_trans = 0;

      if (info_ptr != NULL)
         info_ptr->num_trans = 0;

      png_chunk_benign_error(png_ptr, "tRNS must be after");
   }
#endif

#ifdef PNG_READ_hIST_SUPPORTED
   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_hIST) != 0)
      png_chunk_benign_error(png_ptr, "hIST must be after");
#endif

#ifdef PNG_READ_bKGD_SUPPORTED
   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_bKGD) != 0)
      png_chunk_benign_error(png_ptr, "bKGD must be after");
#endif
}

void /* PRIVATE */
png_handle_IEND(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_debug(1, "in png_handle_IEND");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0 ||
       (png_ptr->mode & PNG_HAVE_IDAT) == 0)
      png_chunk_error(png_ptr, "out of place");

   png_ptr->mode |= (PNG_AFTER_IDAT | PNG_HAVE_IEND);

   png_crc_finish(png_ptr, length);

   if (length != 0)
      png_chunk_benign_error(png_ptr, "invalid");

   PNG_UNUSED(info_ptr)
}

#ifdef PNG_READ_gAMA_SUPPORTED
void /* PRIVATE */
png_handle_gAMA(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_fixed_point igamma;
   png_byte buf[4];

   png_debug(1, "in png_handle_gAMA");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & (PNG_HAVE_IDAT|PNG_HAVE_PLTE)) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   if (length != 4)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid");
      return;
   }

   png_crc_read(png_ptr, buf, 4);

   if (png_crc_finish(png_ptr, 0) != 0)
      return;

   igamma = png_get_fixed_point(NULL, buf);

   png_colorspace_set_gamma(png_ptr, &png_ptr->colorspace, igamma);
   png_colorspace_sync(png_ptr, info_ptr);
}
#endif

#ifdef PNG_READ_sBIT_SUPPORTED
void /* PRIVATE */
png_handle_sBIT(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   unsigned int truelen, i;
   png_byte sample_depth;
   png_byte buf[4];

   png_debug(1, "in png_handle_sBIT");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & (PNG_HAVE_IDAT|PNG_HAVE_PLTE)) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_sBIT) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      truelen = 3;
      sample_depth = 8;
   }

   else
   {
      truelen = png_ptr->channels;
      sample_depth = png_ptr->bit_depth;
   }

   if (length != truelen || length > 4)
   {
      png_chunk_benign_error(png_ptr, "invalid");
      png_crc_finish(png_ptr, length);
      return;
   }

   buf[0] = buf[1] = buf[2] = buf[3] = sample_depth;
   png_crc_read(png_ptr, buf, truelen);

   if (png_crc_finish(png_ptr, 0) != 0)
      return;

   for (i=0; i<truelen; ++i)
   {
      if (buf[i] == 0 || buf[i] > sample_depth)
      {
         png_chunk_benign_error(png_ptr, "invalid");
         return;
      }
   }

   if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0)
   {
      png_ptr->sig_bit.red = buf[0];
      png_ptr->sig_bit.green = buf[1];
      png_ptr->sig_bit.blue = buf[2];
      png_ptr->sig_bit.alpha = buf[3];
   }

   else
   {
      png_ptr->sig_bit.gray = buf[0];
      png_ptr->sig_bit.red = buf[0];
      png_ptr->sig_bit.green = buf[0];
      png_ptr->sig_bit.blue = buf[0];
      png_ptr->sig_bit.alpha = buf[1];
   }

   png_set_sBIT(png_ptr, info_ptr, &(png_ptr->sig_bit));
}
#endif

#ifdef PNG_READ_cHRM_SUPPORTED
void /* PRIVATE */
png_handle_cHRM(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[32];
   png_xy xy;

   png_debug(1, "in png_handle_cHRM");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & (PNG_HAVE_IDAT|PNG_HAVE_PLTE)) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   if (length != 32)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid");
      return;
   }

   png_crc_read(png_ptr, buf, 32);

   if (png_crc_finish(png_ptr, 0) != 0)
      return;

   xy.whitex = png_get_fixed_point(NULL, buf);
   xy.whitey = png_get_fixed_point(NULL, buf + 4);
   xy.redx   = png_get_fixed_point(NULL, buf + 8);
   xy.redy   = png_get_fixed_point(NULL, buf + 12);
   xy.greenx = png_get_fixed_point(NULL, buf + 16);
   xy.greeny = png_get_fixed_point(NULL, buf + 20);
   xy.bluex  = png_get_fixed_point(NULL, buf + 24);
   xy.bluey  = png_get_fixed_point(NULL, buf + 28);

   if (xy.whitex == PNG_FIXED_ERROR ||
       xy.whitey == PNG_FIXED_ERROR ||
       xy.redx   == PNG_FIXED_ERROR ||
       xy.redy   == PNG_FIXED_ERROR ||
       xy.greenx == PNG_FIXED_ERROR ||
       xy.greeny == PNG_FIXED_ERROR ||
       xy.bluex  == PNG_FIXED_ERROR ||
       xy.bluey  == PNG_FIXED_ERROR)
   {
      png_chunk_benign_error(png_ptr, "invalid values");
      return;
   }

   /* If a colorspace error has already been output skip this chunk */
   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) != 0)
      return;

   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_FROM_cHRM) != 0)
   {
      png_ptr->colorspace.flags |= PNG_COLORSPACE_INVALID;
      png_colorspace_sync(png_ptr, info_ptr);
      png_chunk_benign_error(png_ptr, "duplicate");
      return;
   }

   png_ptr->colorspace.flags |= PNG_COLORSPACE_FROM_cHRM;
   (void)png_colorspace_set_chromaticities(png_ptr, &png_ptr->colorspace, &xy,
       1/*prefer cHRM values*/);
   png_colorspace_sync(png_ptr, info_ptr);
}
#endif

#ifdef PNG_READ_sRGB_SUPPORTED
void /* PRIVATE */
png_handle_sRGB(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte intent;

   png_debug(1, "in png_handle_sRGB");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & (PNG_HAVE_IDAT|PNG_HAVE_PLTE)) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   if (length != 1)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid");
      return;
   }

   png_crc_read(png_ptr, &intent, 1);

   if (png_crc_finish(png_ptr, 0) != 0)
      return;

   /* If a colorspace error has already been output skip this chunk */
   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) != 0)
      return;

   /* Only one sRGB or iCCP chunk is allowed, use the HAVE_INTENT flag to detect
    * this.
    */
   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_HAVE_INTENT) != 0)
   {
      png_ptr->colorspace.flags |= PNG_COLORSPACE_INVALID;
      png_colorspace_sync(png_ptr, info_ptr);
      png_chunk_benign_error(png_ptr, "too many profiles");
      return;
   }

   (void)png_colorspace_set_sRGB(png_ptr, &png_ptr->colorspace, intent);
   png_colorspace_sync(png_ptr, info_ptr);
}
#endif /* READ_sRGB */

#ifdef PNG_READ_iCCP_SUPPORTED
void /* PRIVATE */
png_handle_iCCP(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
/* Note: this does not properly handle profiles that are > 64K under DOS */
{
   png_const_charp errmsg = NULL; /* error message output, or no error */
   int finished = 0; /* crc checked */

   png_debug(1, "in png_handle_iCCP");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & (PNG_HAVE_IDAT|PNG_HAVE_PLTE)) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   /* Consistent with all the above colorspace handling an obviously *invalid*
    * chunk is just ignored, so does not invalidate the color space.  An
    * alternative is to set the 'invalid' flags at the start of this routine
    * and only clear them in they were not set before and all the tests pass.
    */

   /* The keyword must be at least one character and there is a
    * terminator (0) byte and the compression method byte, and the
    * 'zlib' datastream is at least 11 bytes.
    */
   if (length < 14)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "too short");
      return;
   }

   /* If a colorspace error has already been output skip this chunk */
   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) != 0)
   {
      png_crc_finish(png_ptr, length);
      return;
   }

   /* Only one sRGB or iCCP chunk is allowed, use the HAVE_INTENT flag to detect
    * this.
    */
   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_HAVE_INTENT) == 0)
   {
      uInt read_length, keyword_length;
      char keyword[81];

      /* Find the keyword; the keyword plus separator and compression method
       * bytes can be at most 81 characters long.
       */
      read_length = 81; /* maximum */
      if (read_length > length)
         read_length = (uInt)length;

      png_crc_read(png_ptr, (png_bytep)keyword, read_length);
      length -= read_length;

      /* The minimum 'zlib' stream is assumed to be just the 2 byte header,
       * 5 bytes minimum 'deflate' stream, and the 4 byte checksum.
       */
      if (length < 11)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "too short");
         return;
      }

      keyword_length = 0;
      while (keyword_length < 80 && keyword_length < read_length &&
         keyword[keyword_length] != 0)
         ++keyword_length;

      /* TODO: make the keyword checking common */
      if (keyword_length >= 1 && keyword_length <= 79)
      {
         /* We only understand '0' compression - deflate - so if we get a
          * different value we can't safely decode the chunk.
          */
         if (keyword_length+1 < read_length &&
            keyword[keyword_length+1] == PNG_COMPRESSION_TYPE_BASE)
         {
            read_length -= keyword_length+2;

            if (png_inflate_claim(png_ptr, png_iCCP) == Z_OK)
            {
               Byte profile_header[132]={0};
               Byte local_buffer[PNG_INFLATE_BUF_SIZE];
               png_alloc_size_t size = (sizeof profile_header);

               png_ptr->zstream.next_in = (Bytef*)keyword + (keyword_length+2);
               png_ptr->zstream.avail_in = read_length;
               (void)png_inflate_read(png_ptr, local_buffer,
                   (sizeof local_buffer), &length, profile_header, &size,
                   0/*finish: don't, because the output is too small*/);

               if (size == 0)
               {
                  /* We have the ICC profile header; do the basic header checks.
                   */
                  png_uint_32 profile_length = png_get_uint_32(profile_header);

                  if (png_icc_check_length(png_ptr, &png_ptr->colorspace,
                      keyword, profile_length) != 0)
                  {
                     /* The length is apparently ok, so we can check the 132
                      * byte header.
                      */
                     if (png_icc_check_header(png_ptr, &png_ptr->colorspace,
                         keyword, profile_length, profile_header,
                         png_ptr->color_type) != 0)
                     {
                        /* Now read the tag table; a variable size buffer is
                         * needed at this point, allocate one for the whole
                         * profile.  The header check has already validated
                         * that none of this stuff will overflow.
                         */
                        png_uint_32 tag_count =
                           png_get_uint_32(profile_header + 128);
                        png_bytep profile = png_read_buffer(png_ptr,
                            profile_length, 2/*silent*/);

                        if (profile != NULL)
                        {
                           memcpy(profile, profile_header,
                               (sizeof profile_header));

                           size = 12 * tag_count;

                           (void)png_inflate_read(png_ptr, local_buffer,
                               (sizeof local_buffer), &length,
                               profile + (sizeof profile_header), &size, 0);

                           /* Still expect a buffer error because we expect
                            * there to be some tag data!
                            */
                           if (size == 0)
                           {
                              if (png_icc_check_tag_table(png_ptr,
                                  &png_ptr->colorspace, keyword, profile_length,
                                  profile) != 0)
                              {
                                 /* The profile has been validated for basic
                                  * security issues, so read the whole thing in.
                                  */
                                 size = profile_length - (sizeof profile_header)
                                     - 12 * tag_count;

                                 (void)png_inflate_read(png_ptr, local_buffer,
                                     (sizeof local_buffer), &length,
                                     profile + (sizeof profile_header) +
                                     12 * tag_count, &size, 1/*finish*/);

                                 if (length > 0 && !(png_ptr->flags &
                                     PNG_FLAG_BENIGN_ERRORS_WARN))
                                    errmsg = "extra compressed data";

                                 /* But otherwise allow extra data: */
                                 else if (size == 0)
                                 {
                                    if (length > 0)
                                    {
                                       /* This can be handled completely, so
                                        * keep going.
                                        */
                                       png_chunk_warning(png_ptr,
                                           "extra compressed data");
                                    }

                                    png_crc_finish(png_ptr, length);
                                    finished = 1;

# if defined(PNG_sRGB_SUPPORTED) && PNG_sRGB_PROFILE_CHECKS >= 0
                                    /* Check for a match against sRGB */
                                    png_icc_set_sRGB(png_ptr,
                                        &png_ptr->colorspace, profile,
                                        png_ptr->zstream.adler);
# endif

                                    /* Steal the profile for info_ptr. */
                                    if (info_ptr != NULL)
                                    {
                                       png_free_data(png_ptr, info_ptr,
                                           PNG_FREE_ICCP, 0);

                                       info_ptr->iccp_name = png_voidcast(char*,
                                           png_malloc_base(png_ptr,
                                           keyword_length+1));
                                       if (info_ptr->iccp_name != NULL)
                                       {
                                          memcpy(info_ptr->iccp_name, keyword,
                                              keyword_length+1);
                                          info_ptr->iccp_proflen =
                                              profile_length;
                                          info_ptr->iccp_profile = profile;
                                          png_ptr->read_buffer = NULL; /*steal*/
                                          info_ptr->free_me |= PNG_FREE_ICCP;
                                          info_ptr->valid |= PNG_INFO_iCCP;
                                       }

                                       else
                                       {
                                          png_ptr->colorspace.flags |=
                                             PNG_COLORSPACE_INVALID;
                                          errmsg = "out of memory";
                                       }
                                    }

                                    /* else the profile remains in the read
                                     * buffer which gets reused for subsequent
                                     * chunks.
                                     */

                                    if (info_ptr != NULL)
                                       png_colorspace_sync(png_ptr, info_ptr);

                                    if (errmsg == NULL)
                                    {
                                       png_ptr->zowner = 0;
                                       return;
                                    }
                                 }
                                 if (errmsg == NULL)
                                    errmsg = png_ptr->zstream.msg;
                              }
                              /* else png_icc_check_tag_table output an error */
                           }
                           else /* profile truncated */
                              errmsg = png_ptr->zstream.msg;
                        }

                        else
                           errmsg = "out of memory";
                     }

                     /* else png_icc_check_header output an error */
                  }

                  /* else png_icc_check_length output an error */
               }

               else /* profile truncated */
                  errmsg = png_ptr->zstream.msg;

               /* Release the stream */
               png_ptr->zowner = 0;
            }

            else /* png_inflate_claim failed */
               errmsg = png_ptr->zstream.msg;
         }

         else
            errmsg = "bad compression method"; /* or missing */
      }

      else
         errmsg = "bad keyword";
   }

   else
      errmsg = "too many profiles";

   /* Failure: the reason is in 'errmsg' */
   if (finished == 0)
      png_crc_finish(png_ptr, length);

   png_ptr->colorspace.flags |= PNG_COLORSPACE_INVALID;
   png_colorspace_sync(png_ptr, info_ptr);
   if (errmsg != NULL) /* else already output */
      png_chunk_benign_error(png_ptr, errmsg);
}
#endif /* READ_iCCP */

#ifdef PNG_READ_sPLT_SUPPORTED
void /* PRIVATE */
png_handle_sPLT(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
/* Note: this does not properly handle chunks that are > 64K under DOS */
{
   png_bytep entry_start, buffer;
   png_sPLT_t new_palette;
   png_sPLT_entryp pp;
   png_uint_32 data_length;
   int entry_size, i;
   png_uint_32 skip = 0;
   png_uint_32 dl;
   size_t max_dl;

   png_debug(1, "in png_handle_sPLT");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         png_warning(png_ptr, "No space in chunk cache for sPLT");
         png_crc_finish(png_ptr, length);
         return;
      }
   }
#endif

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

#ifdef PNG_MAX_MALLOC_64K
   if (length > 65535U)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "too large to fit in memory");
      return;
   }
#endif

   buffer = png_read_buffer(png_ptr, length+1, 2/*silent*/);
   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return;
   }


   /* WARNING: this may break if size_t is less than 32 bits; it is assumed
    * that the PNG_MAX_MALLOC_64K test is enabled in this case, but this is a
    * potential breakage point if the types in pngconf.h aren't exactly right.
    */
   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, skip) != 0)
      return;

   buffer[length] = 0;

   for (entry_start = buffer; *entry_start; entry_start++)
      /* Empty loop to find end of name */ ;

   ++entry_start;

   /* A sample depth should follow the separator, and we should be on it  */
   if (length < 2U || entry_start > buffer + (length - 2U))
   {
      png_warning(png_ptr, "malformed sPLT chunk");
      return;
   }

   new_palette.depth = *entry_start++;
   entry_size = (new_palette.depth == 8 ? 6 : 10);
   /* This must fit in a png_uint_32 because it is derived from the original
    * chunk data length.
    */
   data_length = length - (png_uint_32)(entry_start - buffer);

   /* Integrity-check the data length */
   if ((data_length % (unsigned int)entry_size) != 0)
   {
      png_warning(png_ptr, "sPLT chunk has bad length");
      return;
   }

   dl = (png_uint_32)(data_length / (unsigned int)entry_size);
   max_dl = PNG_SIZE_MAX / (sizeof (png_sPLT_entry));

   if (dl > max_dl)
   {
      png_warning(png_ptr, "sPLT chunk too long");
      return;
   }

   new_palette.nentries = (png_int_32)(data_length / (unsigned int)entry_size);

   new_palette.entries = (png_sPLT_entryp)png_malloc_warn(png_ptr,
       (png_alloc_size_t) new_palette.nentries * (sizeof (png_sPLT_entry)));

   if (new_palette.entries == NULL)
   {
      png_warning(png_ptr, "sPLT chunk requires too much memory");
      return;
   }

#ifdef PNG_POINTER_INDEXING_SUPPORTED
   for (i = 0; i < new_palette.nentries; i++)
   {
      pp = new_palette.entries + i;

      if (new_palette.depth == 8)
      {
         pp->red = *entry_start++;
         pp->green = *entry_start++;
         pp->blue = *entry_start++;
         pp->alpha = *entry_start++;
      }

      else
      {
         pp->red   = png_get_uint_16(entry_start); entry_start += 2;
         pp->green = png_get_uint_16(entry_start); entry_start += 2;
         pp->blue  = png_get_uint_16(entry_start); entry_start += 2;
         pp->alpha = png_get_uint_16(entry_start); entry_start += 2;
      }

      pp->frequency = png_get_uint_16(entry_start); entry_start += 2;
   }
#else
   pp = new_palette.entries;

   for (i = 0; i < new_palette.nentries; i++)
   {

      if (new_palette.depth == 8)
      {
         pp[i].red   = *entry_start++;
         pp[i].green = *entry_start++;
         pp[i].blue  = *entry_start++;
         pp[i].alpha = *entry_start++;
      }

      else
      {
         pp[i].red   = png_get_uint_16(entry_start); entry_start += 2;
         pp[i].green = png_get_uint_16(entry_start); entry_start += 2;
         pp[i].blue  = png_get_uint_16(entry_start); entry_start += 2;
         pp[i].alpha = png_get_uint_16(entry_start); entry_start += 2;
      }

      pp[i].frequency = png_get_uint_16(entry_start); entry_start += 2;
   }
#endif

   /* Discard all chunk data except the name and stash that */
   new_palette.name = (png_charp)buffer;

   png_set_sPLT(png_ptr, info_ptr, &new_palette, 1);

   png_free(png_ptr, new_palette.entries);
}
#endif /* READ_sPLT */

#ifdef PNG_READ_tRNS_SUPPORTED
void /* PRIVATE */
png_handle_tRNS(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte readbuf[PNG_MAX_PALETTE_LENGTH];

   png_debug(1, "in png_handle_tRNS");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   else if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_tRNS) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->color_type == PNG_COLOR_TYPE_GRAY)
   {
      png_byte buf[2];

      if (length != 2)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "invalid");
         return;
      }

      png_crc_read(png_ptr, buf, 2);
      png_ptr->num_trans = 1;
      png_ptr->trans_color.gray = png_get_uint_16(buf);
   }

   else if (png_ptr->color_type == PNG_COLOR_TYPE_RGB)
   {
      png_byte buf[6];

      if (length != 6)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "invalid");
         return;
      }

      png_crc_read(png_ptr, buf, length);
      png_ptr->num_trans = 1;
      png_ptr->trans_color.red = png_get_uint_16(buf);
      png_ptr->trans_color.green = png_get_uint_16(buf + 2);
      png_ptr->trans_color.blue = png_get_uint_16(buf + 4);
   }

   else if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      if ((png_ptr->mode & PNG_HAVE_PLTE) == 0)
      {
         /* TODO: is this actually an error in the ISO spec? */
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "out of place");
         return;
      }

      if (length > (unsigned int) png_ptr->num_palette ||
         length > (unsigned int) PNG_MAX_PALETTE_LENGTH ||
         length == 0)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "invalid");
         return;
      }

      png_crc_read(png_ptr, readbuf, length);
      png_ptr->num_trans = (png_uint_16)length;
   }

   else
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid with alpha channel");
      return;
   }

   if (png_crc_finish(png_ptr, 0) != 0)
   {
      png_ptr->num_trans = 0;
      return;
   }

   /* TODO: this is a horrible side effect in the palette case because the
    * png_struct ends up with a pointer to the tRNS buffer owned by the
    * png_info.  Fix this.
    */
   png_set_tRNS(png_ptr, info_ptr, readbuf, png_ptr->num_trans,
       &(png_ptr->trans_color));
}
#endif

#ifdef PNG_READ_bKGD_SUPPORTED
void /* PRIVATE */
png_handle_bKGD(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   unsigned int truelen;
   png_byte buf[6];
   png_color_16 background;

   png_debug(1, "in png_handle_bKGD");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0 ||
       (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE &&
       (png_ptr->mode & PNG_HAVE_PLTE) == 0))
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   else if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_bKGD) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      truelen = 1;

   else if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0)
      truelen = 6;

   else
      truelen = 2;

   if (length != truelen)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid");
      return;
   }

   png_crc_read(png_ptr, buf, truelen);

   if (png_crc_finish(png_ptr, 0) != 0)
      return;

   /* We convert the index value into RGB components so that we can allow
    * arbitrary RGB values for background when we have transparency, and
    * so it is easy to determine the RGB values of the background color
    * from the info_ptr struct.
    */
   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      background.index = buf[0];

      if (info_ptr != NULL && info_ptr->num_palette != 0)
      {
         if (buf[0] >= info_ptr->num_palette)
         {
            png_chunk_benign_error(png_ptr, "invalid index");
            return;
         }

         background.red = (png_uint_16)png_ptr->palette[buf[0]].red;
         background.green = (png_uint_16)png_ptr->palette[buf[0]].green;
         background.blue = (png_uint_16)png_ptr->palette[buf[0]].blue;
      }

      else
         background.red = background.green = background.blue = 0;

      background.gray = 0;
   }

   else if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) == 0) /* GRAY */
   {
      if (png_ptr->bit_depth <= 8)
      {
         if (buf[0] != 0 || buf[1] >= (unsigned int)(1 << png_ptr->bit_depth))
         {
            png_chunk_benign_error(png_ptr, "invalid gray level");
            return;
         }
      }

      background.index = 0;
      background.red =
      background.green =
      background.blue =
      background.gray = png_get_uint_16(buf);
   }

   else
   {
      if (png_ptr->bit_depth <= 8)
      {
         if (buf[0] != 0 || buf[2] != 0 || buf[4] != 0)
         {
            png_chunk_benign_error(png_ptr, "invalid color");
            return;
         }
      }

      background.index = 0;
      background.red = png_get_uint_16(buf);
      background.green = png_get_uint_16(buf + 2);
      background.blue = png_get_uint_16(buf + 4);
      background.gray = 0;
   }

   png_set_bKGD(png_ptr, info_ptr, &background);
}
#endif

#ifdef PNG_READ_eXIf_SUPPORTED
void /* PRIVATE */
png_handle_eXIf(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   unsigned int i;

   png_debug(1, "in png_handle_eXIf");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   if (length < 2)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "too short");
      return;
   }

   else if (info_ptr == NULL || (info_ptr->valid & PNG_INFO_eXIf) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "duplicate");
      return;
   }

   info_ptr->free_me |= PNG_FREE_EXIF;

   info_ptr->eXIf_buf = png_voidcast(png_bytep,
             png_malloc_warn(png_ptr, length));

   if (info_ptr->eXIf_buf == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return;
   }

   for (i = 0; i < length; i++)
   {
      png_byte buf[1];
      png_crc_read(png_ptr, buf, 1);
      info_ptr->eXIf_buf[i] = buf[0];
      if (i == 1)
      {
         if ((buf[0] != 'M' && buf[0] != 'I') ||
             (info_ptr->eXIf_buf[0] != buf[0]))
         {
            png_crc_finish(png_ptr, length - 2);
            png_chunk_benign_error(png_ptr, "incorrect byte-order specifier");
            png_free(png_ptr, info_ptr->eXIf_buf);
            info_ptr->eXIf_buf = NULL;
            return;
         }
      }
   }

   if (png_crc_finish(png_ptr, 0) == 0)
      png_set_eXIf_1(png_ptr, info_ptr, length, info_ptr->eXIf_buf);

   png_free(png_ptr, info_ptr->eXIf_buf);
   info_ptr->eXIf_buf = NULL;
}
#endif

#ifdef PNG_READ_hIST_SUPPORTED
void /* PRIVATE */
png_handle_hIST(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   unsigned int num, i;
   png_uint_16 readbuf[PNG_MAX_PALETTE_LENGTH];

   png_debug(1, "in png_handle_hIST");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0 ||
       (png_ptr->mode & PNG_HAVE_PLTE) == 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   else if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_hIST) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "duplicate");
      return;
   }

   num = length / 2 ;

   if (length != num * 2 ||
       num != (unsigned int)png_ptr->num_palette ||
       num > (unsigned int)PNG_MAX_PALETTE_LENGTH)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid");
      return;
   }

   for (i = 0; i < num; i++)
   {
      png_byte buf[2];

      png_crc_read(png_ptr, buf, 2);
      readbuf[i] = png_get_uint_16(buf);
   }

   if (png_crc_finish(png_ptr, 0) != 0)
      return;

   png_set_hIST(png_ptr, info_ptr, readbuf);
}
#endif

#ifdef PNG_READ_pHYs_SUPPORTED
void /* PRIVATE */
png_handle_pHYs(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[9];
   png_uint_32 res_x, res_y;
   int unit_type;

   png_debug(1, "in png_handle_pHYs");

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR");

   else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of place");
      return;
   }

   else if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_pHYs) != 0)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "duplicate");
      return;
   }

   if (length != 9)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid");
      return;
   }

   png_crc_read(png_ptr, buf, 9);

   if (png_crc_finish(png_ptr, 0) != 0)
      return;

   res_x = png_get_uint_32(buf);
