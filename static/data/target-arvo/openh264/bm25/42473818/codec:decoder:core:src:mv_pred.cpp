/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * \file    mv_pred.c
 *
 * \brief   Get MV predictor and update motion vector of mb cache
 *
 * \date    05/22/2009 Created
 *
 *************************************************************************************
 */

#include "mv_pred.h"
#include "ls_defines.h"
#include "mb_cache.h"
#include "parse_mb_syn_cabac.h"

namespace WelsDec {

static inline  void SetRectBlock (void* vp, int32_t w, const int32_t h, int32_t stride, const uint32_t val,
                                  const int32_t size) {
  uint8_t* p = (uint8_t*)vp;
  w *= size;
  if (w == 1 && h == 4) {
    * (uint8_t*) (p + 0 * stride) =
      * (uint8_t*) (p + 1 * stride) =
        * (uint8_t*) (p + 2 * stride) =
          * (uint8_t*) (p + 3 * stride) = (uint8_t)val;
  } else if (w == 2 && h == 2) {
    * (uint16_t*) (p + 0 * stride) =
      * (uint16_t*) (p + 1 * stride) = size == 4 ? (uint16_t)val : (uint16_t) (val * 0x0101U);
  } else if (w == 2 && h == 4) {
    * (uint16_t*) (p + 0 * stride) =
      * (uint16_t*) (p + 1 * stride) =
        * (uint16_t*) (p + 2 * stride) =
          * (uint16_t*) (p + 3 * stride) = size == 4 ? (uint16_t)val : (uint16_t) (val * 0x0101U);
  } else if (w == 4 && h == 2) {
    * (uint32_t*) (p + 0 * stride) =
      * (uint32_t*) (p + 1 * stride) = size == 4 ? val : (uint32_t) (val * 0x01010101UL);
  } else if (w == 4 && h == 4) {
    * (uint32_t*) (p + 0 * stride) =
      * (uint32_t*) (p + 1 * stride) =
        * (uint32_t*) (p + 2 * stride) =
          * (uint32_t*) (p + 3 * stride) = size == 4 ? val : (uint32_t) (val * 0x01010101UL);
  } else if (w == 8 && h == 1) {
    * (uint32_t*) (p + 0 * stride) =
      * (uint32_t*) (p + 0 * stride + 4) = size == 4 ? val : (uint32_t) (val * 0x01010101UL);
  } else if (w == 8 && h == 2) {
    * (uint32_t*) (p + 0 * stride) =
      * (uint32_t*) (p + 0 * stride + 4) =
        * (uint32_t*) (p + 1 * stride) =
          * (uint32_t*) (p + 1 * stride + 4) = size == 4 ? val : (uint32_t) (val * 0x01010101UL);
  } else if (w == 8 && h == 4) {
    * (uint32_t*) (p + 0 * stride) =
      * (uint32_t*) (p + 0 * stride + 4) =
        * (uint32_t*) (p + 1 * stride) =
          * (uint32_t*) (p + 1 * stride + 4) =
            * (uint32_t*) (p + 2 * stride) =
              * (uint32_t*) (p + 2 * stride + 4) =
                * (uint32_t*) (p + 3 * stride) =
                  * (uint32_t*) (p + 3 * stride + 4) = size == 4 ? val : (uint32_t) (val * 0x01010101UL);
  } else if (w == 16 && h == 2) {
    * (uint32_t*) (p + 0 * stride + 0) =
      * (uint32_t*) (p + 0 * stride + 4) =
        * (uint32_t*) (p + 0 * stride + 8) =
          * (uint32_t*) (p + 0 * stride + 12) =
            * (uint32_t*) (p + 1 * stride + 0) =
              * (uint32_t*) (p + 1 * stride + 4) =
                * (uint32_t*) (p + 1 * stride + 8) =
                  * (uint32_t*) (p + 1 * stride + 12) = size == 4 ? val : (uint32_t) (val * 0x01010101UL);
  } else if (w == 16 && h == 3) {
    * (uint32_t*) (p + 0 * stride + 0) =
      * (uint32_t*) (p + 0 * stride + 4) =
        * (uint32_t*) (p + 0 * stride + 8) =
          * (uint32_t*) (p + 0 * stride + 12) =
            * (uint32_t*) (p + 1 * stride + 0) =
              * (uint32_t*) (p + 1 * stride + 4) =
                * (uint32_t*) (p + 1 * stride + 8) =
                  * (uint32_t*) (p + 1 * stride + 12) =
                    * (uint32_t*) (p + 2 * stride + 0) =
                      * (uint32_t*) (p + 2 * stride + 4) =
                        * (uint32_t*) (p + 2 * stride + 8) =
                          * (uint32_t*) (p + 2 * stride + 12) = size == 4 ? val : (uint32_t) (val * 0x01010101UL);
  } else if (w == 16 && h == 4) {
    * (uint32_t*) (p + 0 * stride + 0) =
      * (uint32_t*) (p + 0 * stride + 4) =
        * (uint32_t*) (p + 0 * stride + 8) =
          * (uint32_t*) (p + 0 * stride + 12) =
            * (uint32_t*) (p + 1 * stride + 0) =
              * (uint32_t*) (p + 1 * stride + 4) =
                * (uint32_t*) (p + 1 * stride + 8) =
                  * (uint32_t*) (p + 1 * stride + 12) =
                    * (uint32_t*) (p + 2 * stride + 0) =
                      * (uint32_t*) (p + 2 * stride + 4) =
                        * (uint32_t*) (p + 2 * stride + 8) =
                          * (uint32_t*) (p + 2 * stride + 12) =
                            * (uint32_t*) (p + 3 * stride + 0) =
                              * (uint32_t*) (p + 3 * stride + 4) =
                                * (uint32_t*) (p + 3 * stride + 8) =
                                  * (uint32_t*) (p + 3 * stride + 12) = size == 4 ? val : (uint32_t) (val * 0x01010101UL);
  }
}
void CopyRectBlock4Cols (void* vdst, void* vsrc, const int32_t stride_dst, const int32_t stride_src, int32_t w,
                         const int32_t size) {
  uint8_t* dst = (uint8_t*)vdst;
  uint8_t* src = (uint8_t*)vsrc;
  w *= size;
  if (w == 1) {
    dst[stride_dst * 0] = src[stride_src * 0];
    dst[stride_dst * 1] = src[stride_src * 1];
    dst[stride_dst * 2] = src[stride_src * 2];
    dst[stride_dst * 3] = src[stride_src * 3];
  } else if (w == 2) {
    * (uint16_t*) (&dst[stride_dst * 0]) = * (uint16_t*) (&src[stride_src * 0]);
    * (uint16_t*) (&dst[stride_dst * 1]) = * (uint16_t*) (&src[stride_src * 1]);
    * (uint16_t*) (&dst[stride_dst * 2]) = * (uint16_t*) (&src[stride_src * 2]);
    * (uint16_t*) (&dst[stride_dst * 3]) = * (uint16_t*) (&src[stride_src * 3]);
  } else if (w == 4) {
    * (uint32_t*) (&dst[stride_dst * 0]) = * (uint32_t*) (&src[stride_src * 0]);
    * (uint32_t*) (&dst[stride_dst * 1]) = * (uint32_t*) (&src[stride_src * 1]);
    * (uint32_t*) (&dst[stride_dst * 2]) = * (uint32_t*) (&src[stride_src * 2]);
    * (uint32_t*) (&dst[stride_dst * 3]) = * (uint32_t*) (&src[stride_src * 3]);
  } else if (w == 16) {
    memcpy (&dst[stride_dst * 0], &src[stride_src * 0], 16);
    memcpy (&dst[stride_dst * 1], &src[stride_src * 1], 16);
    memcpy (&dst[stride_dst * 2], &src[stride_src * 2], 16);
    memcpy (&dst[stride_dst * 3], &src[stride_src * 3], 16);
  }
}
void PredPSkipMvFromNeighbor (PDqLayer pCurLayer, int16_t iMvp[2]) {
