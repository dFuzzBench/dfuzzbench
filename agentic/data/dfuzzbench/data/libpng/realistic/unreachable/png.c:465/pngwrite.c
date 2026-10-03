/* pngwrite.c - general routines to write a PNG file
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

#include "pngpriv.h"
#ifdef PNG_SIMPLIFIED_WRITE_STDIO_SUPPORTED
#  include <errno.h>
#endif /* SIMPLIFIED_WRITE_STDIO */

#ifdef PNG_WRITE_SUPPORTED

#ifdef PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED
/* Write out all the unknown chunks for the current given location */
static void
write_unknown_chunks(png_structrp png_ptr, png_const_inforp info_ptr,
    unsigned int where)
{
   if (info_ptr->unknown_chunks_num != 0)
   {
      png_const_unknown_chunkp up;

      png_debug(5, "writing extra chunks");

      for (up = info_ptr->unknown_chunks;
           up < info_ptr->unknown_chunks + info_ptr->unknown_chunks_num;
           ++up)
         if ((up->location & where) != 0)
      {
         /* If per-chunk unknown chunk handling is enabled use it, otherwise
          * just write the chunks the application has set.
          */
#ifdef PNG_SET_UNKNOWN_CHUNKS_SUPPORTED
         int keep = png_handle_as_unknown(png_ptr, up->name);

         /* NOTE: this code is radically different from the read side in the
          * matter of handling an ancillary unknown chunk.  In the read side
          * the default behavior is to discard it, in the code below the default
          * behavior is to write it.  Critical chunks are, however, only
          * written if explicitly listed or if the default is set to write all
          * unknown chunks.
          *
          * The default handling is also slightly weird - it is not possible to
          * stop the writing of all unsafe-to-copy chunks!
          *
          * TODO: REVIEW: this would seem to be a bug.
          */
         if (keep != PNG_HANDLE_CHUNK_NEVER &&
             ((up->name[3] & 0x20) /* safe-to-copy overrides everything */ ||
              keep == PNG_HANDLE_CHUNK_ALWAYS ||
              (keep == PNG_HANDLE_CHUNK_AS_DEFAULT &&
               png_ptr->unknown_default == PNG_HANDLE_CHUNK_ALWAYS)))
#endif
         {
            /* TODO: review, what is wrong with a zero length unknown chunk? */
            if (up->size == 0)
               png_warning(png_ptr, "Writing zero-length unknown chunk");

            png_write_chunk(png_ptr, up->name, up->data, up->size);
         }
      }
   }
}
#endif /* WRITE_UNKNOWN_CHUNKS */

/* Writes all the PNG information.  This is the suggested way to use the
 * library.  If you have a new chunk to add, make a function to write it,
 * and put it in the correct location here.  If you want the chunk written
 * after the image data, put it in png_write_end().  I strongly encourage
 * you to supply a PNG_INFO_<chunk> flag, and check info_ptr->valid before
 * writing the chunk, as that will keep the code from breaking if you want
 * to just write a plain PNG file.  If you have long comments, I suggest
 * writing them in png_write_end(), and compressing them.
 */
void PNGAPI
png_write_info_before_PLTE(png_structrp png_ptr, png_const_inforp info_ptr)
{
   png_debug(1, "in png_write_info_before_PLTE");

   if (png_ptr == NULL || info_ptr == NULL)
      return;

   if ((png_ptr->mode & PNG_WROTE_INFO_BEFORE_PLTE) == 0)
   {
      /* Write PNG signature */
      png_write_sig(png_ptr);

#ifdef PNG_MNG_FEATURES_SUPPORTED
      if ((png_ptr->mode & PNG_HAVE_PNG_SIGNATURE) != 0 && \
          png_ptr->mng_features_permitted != 0)
      {
         png_warning(png_ptr,
             "MNG features are not allowed in a PNG datastream");
         png_ptr->mng_features_permitted = 0;
      }
#endif

      /* Write IHDR information. */
      png_write_IHDR(png_ptr, info_ptr->width, info_ptr->height,
          info_ptr->bit_depth, info_ptr->color_type, info_ptr->compression_type,
          info_ptr->filter_type,
#ifdef PNG_WRITE_INTERLACING_SUPPORTED
          info_ptr->interlace_type
#else
          0
#endif
         );

      /* The rest of these check to see if the valid field has the appropriate
       * flag set, and if it does, writes the chunk.
       *
       * 1.6.0: COLORSPACE support controls the writing of these chunks too, and
       * the chunks will be written if the WRITE routine is there and
       * information * is available in the COLORSPACE. (See
       * png_colorspace_sync_info in png.c for where the valid flags get set.)
       *
       * Under certain circumstances the colorspace can be invalidated without
       * syncing the info_struct 'valid' flags; this happens if libpng detects
       * an error and calls png_error while the color space is being set, yet
       * the application continues writing the PNG.  So check the 'invalid'
       * flag here too.
       */
#ifdef PNG_GAMMA_SUPPORTED
#  ifdef PNG_WRITE_gAMA_SUPPORTED
      if ((info_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) == 0 &&
          (info_ptr->colorspace.flags & PNG_COLORSPACE_FROM_gAMA) != 0 &&
          (info_ptr->valid & PNG_INFO_gAMA) != 0)
         png_write_gAMA_fixed(png_ptr, info_ptr->colorspace.gamma);
#  endif
#endif

#ifdef PNG_COLORSPACE_SUPPORTED
      /* Write only one of sRGB or an ICC profile.  If a profile was supplied
       * and it matches one of the known sRGB ones issue a warning.
       */
#  ifdef PNG_WRITE_iCCP_SUPPORTED
         if ((info_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) == 0 &&
             (info_ptr->valid & PNG_INFO_iCCP) != 0)
         {
#    ifdef PNG_WRITE_sRGB_SUPPORTED
               if ((info_ptr->valid & PNG_INFO_sRGB) != 0)
                  png_app_warning(png_ptr,
                      "profile matches sRGB but writing iCCP instead");
#     endif

            png_write_iCCP(png_ptr, info_ptr->iccp_name,
                info_ptr->iccp_profile);
         }
#     ifdef PNG_WRITE_sRGB_SUPPORTED
         else
#     endif
#  endif

#  ifdef PNG_WRITE_sRGB_SUPPORTED
         if ((info_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) == 0 &&
             (info_ptr->valid & PNG_INFO_sRGB) != 0)
            png_write_sRGB(png_ptr, info_ptr->colorspace.rendering_intent);
#  endif /* WRITE_sRGB */
#endif /* COLORSPACE */

#ifdef PNG_WRITE_sBIT_SUPPORTED
         if ((info_ptr->valid & PNG_INFO_sBIT) != 0)
            png_write_sBIT(png_ptr, &(info_ptr->sig_bit), info_ptr->color_type);
#endif

#ifdef PNG_COLORSPACE_SUPPORTED
#  ifdef PNG_WRITE_cHRM_SUPPORTED
         if ((info_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) == 0 &&
             (info_ptr->colorspace.flags & PNG_COLORSPACE_FROM_cHRM) != 0 &&
             (info_ptr->valid & PNG_INFO_cHRM) != 0)
            png_write_cHRM_fixed(png_ptr, &info_ptr->colorspace.end_points_xy);
#  endif
#endif

#ifdef PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED
         write_unknown_chunks(png_ptr, info_ptr, PNG_HAVE_IHDR);
#endif

      png_ptr->mode |= PNG_WROTE_INFO_BEFORE_PLTE;
   }
}

void PNGAPI
png_write_info(png_structrp png_ptr, png_const_inforp info_ptr)
{
#if defined(PNG_WRITE_TEXT_SUPPORTED) || defined(PNG_WRITE_sPLT_SUPPORTED)
   int i;
#endif

   png_debug(1, "in png_write_info");

   if (png_ptr == NULL || info_ptr == NULL)
      return;

   png_write_info_before_PLTE(png_ptr, info_ptr);

   if ((info_ptr->valid & PNG_INFO_PLTE) != 0)
      png_write_PLTE(png_ptr, info_ptr->palette,
          (png_uint_32)info_ptr->num_palette);

   else if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      png_error(png_ptr, "Valid palette required for paletted images");

#ifdef PNG_WRITE_tRNS_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_tRNS) !=0)
   {
#ifdef PNG_WRITE_INVERT_ALPHA_SUPPORTED
      /* Invert the alpha channel (in tRNS) */
      if ((png_ptr->transformations & PNG_INVERT_ALPHA) != 0 &&
          info_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      {
         int j, jend;

         jend = info_ptr->num_trans;
         if (jend > PNG_MAX_PALETTE_LENGTH)
            jend = PNG_MAX_PALETTE_LENGTH;

         for (j = 0; j<jend; ++j)
            info_ptr->trans_alpha[j] =
               (png_byte)(255 - info_ptr->trans_alpha[j]);
      }
#endif
      png_write_tRNS(png_ptr, info_ptr->trans_alpha, &(info_ptr->trans_color),
          info_ptr->num_trans, info_ptr->color_type);
   }
#endif
#ifdef PNG_WRITE_bKGD_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_bKGD) != 0)
      png_write_bKGD(png_ptr, &(info_ptr->background), info_ptr->color_type);
#endif

#ifdef PNG_WRITE_eXIf_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_eXIf) != 0)
   {
      png_write_eXIf(png_ptr, info_ptr->exif, info_ptr->num_exif);
      png_ptr->mode |= PNG_WROTE_eXIf;
   }
#endif

#ifdef PNG_WRITE_hIST_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_hIST) != 0)
      png_write_hIST(png_ptr, info_ptr->hist, info_ptr->num_palette);
#endif

#ifdef PNG_WRITE_oFFs_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_oFFs) != 0)
      png_write_oFFs(png_ptr, info_ptr->x_offset, info_ptr->y_offset,
          info_ptr->offset_unit_type);
#endif

#ifdef PNG_WRITE_pCAL_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_pCAL) != 0)
      png_write_pCAL(png_ptr, info_ptr->pcal_purpose, info_ptr->pcal_X0,
          info_ptr->pcal_X1, info_ptr->pcal_type, info_ptr->pcal_nparams,
          info_ptr->pcal_units, info_ptr->pcal_params);
#endif

#ifdef PNG_WRITE_sCAL_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_sCAL) != 0)
      png_write_sCAL_s(png_ptr, (int)info_ptr->scal_unit,
          info_ptr->scal_s_width, info_ptr->scal_s