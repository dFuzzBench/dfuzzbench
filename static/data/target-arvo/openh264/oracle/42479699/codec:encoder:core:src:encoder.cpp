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
 * \file    encoder.c
 *
 * \brief   core encoder
 *
 * \date    5/14/2009 Created
 *
 *************************************************************************************
 */
#include "encoder.h"
#include "cpu_core.h"

#include "decode_mb_aux.h"
#include "get_intra_predictor.h"

#include "deblocking.h"
#include "ref_list_mgr_svc.h"
#include "mc.h"
#include "paraset_strategy.h"
#include "sample.h"

#include "svc_enc_golomb.h"
#include "svc_base_layer_md.h"
#include "svc_mode_decision.h"
#include "set_mb_syn_cavlc.h"
#include "crt_util_safe_x.h" // Safe CRT routines like utils for cross_platforms
#include "slice_multi_threading.h"

//  global   function  pointers  definition
namespace WelsEnc {
/* Motion compensation */


/*!
 * \brief   initialize source picture body
 * \param   pSrc        SSourcePicture*
 * \param   csp         internal csp format
 * \param   iWidth      widht of picture in pixels
 * \param   iHeight     iHeight of picture in pixels
 * \return  successful - 0; otherwise none 0 for failed
 */
int32_t InitPic (const void* kpSrc, const int32_t kiColorspace, const int32_t kiWidth, const int32_t kiHeight) {
  SSourcePicture* pSrcPic = (SSourcePicture*)kpSrc;

  if (NULL == pSrcPic || kiWidth == 0 || kiHeight == 0)
    return 1;

  pSrcPic->iColorFormat = kiColorspace;
  pSrcPic->iPicWidth    = kiWidth;
  pSrcPic->iPicHeight   = kiHeight;

  //currently encoder only supports videoFormatI420.
  if ((kiColorspace & (~videoFormatVFlip)) != videoFormatI420)
    return 2;
  switch (kiColorspace & (~videoFormatVFlip)) {
  case videoFormatI420:
  case videoFormatYV12:
    pSrcPic->pData[0]   = NULL;
    pSrcPic->pData[1]   = NULL;
    pSrcPic->pData[2]   = NULL;
    pSrcPic->pData[3]   = NULL;
    pSrcPic->iStride[0] = kiWidth;
    pSrcPic->iStride[2] = pSrcPic->iStride[1] = kiWidth >> 1;
    pSrcPic->iStride[3] = 0;
    break;
  case videoFormatYUY2:
  case videoFormatYVYU:
  case videoFormatUYVY:
    pSrcPic->pData[0]   = NULL;
    pSrcPic->pData[1]   = NULL;
    pSrcPic->pData[2]   = NULL;
    pSrcPic->pData[3]   = NULL;
    pSrcPic->iStride[0] = CALC_BI_STRIDE (kiWidth,  16);
    pSrcPic->iStride[3] = pSrcPic->iStride[2] = pSrcPic->iStride[1] = 0;
    break;
  case videoFormatRGB:
  case videoFormatBGR:
    pSrcPic->pData[0]   = NULL;
    pSrcPic->pData[1]   = NULL;
    pSrcPic->pData[2]   = NULL;
    pSrcPic->pData[3]   = NULL;
    pSrcPic->iStride[0] = CALC_BI_STRIDE (kiWidth, 24);
    pSrcPic->iStride[3] = pSrcPic->iStride[2] = pSrcPic->iStride[1] = 0;
    if (kiColorspace & videoFormatVFlip)
      pSrcPic->iColorFormat = kiColorspace & (~videoFormatVFlip);
    else
      pSrcPic->iColorFormat = kiColorspace | videoFormatVFlip;
    break;
  case videoFormatBGRA:
  case videoFormatRGBA:
  case videoFormatARGB:
  case videoFormatABGR:
    pSrcPic->pData[0]   = NULL;
    pSrcPic->pData[1]   = NULL;
    pSrcPic->pData[2]   = NULL;
    pSrcPic->pData[3]   = NULL;
    pSrcPic->iStride[0] = kiWidth << 2;
    pSrcPic->iStride[3] = pSrcPic->iStride[2] = pSrcPic->iStride[1] = 0;
    if (kiColorspace & videoFormatVFlip)
      pSrcPic->iColorFormat = kiColorspace & (~videoFormatVFlip);
    else
      pSrcPic->iColorFormat = kiColorspace | videoFormatVFlip;
    break;
  default:
    return 2; // any else?
  }

  return 0;
}


void WelsInitBGDFunc (SWelsFuncPtrList* pFuncList, const bool kbEnableBackgroundDetection) {
  if (kbEnableBackgroundDetection) {
    pFuncList->pfInterMdBackgroundDecision = WelsMdInterJudgeBGDPskip;
    pFuncList->pfMdBackgroundInfoUpdate = WelsMdUpdateBGDInfo;
  } else {
    pFuncList->pfInterMdBackgroundDecision = WelsMdInterJudgeBGDPskipFalse;
    pFuncList->pfMdBackgroundInfoUpdate = WelsMdUpdateBGDInfoNULL;
  }
}

/*!
 * \brief   initialize function pointers that potentially used in Wels encoding
 * \param   pEncCtx     sWelsEncCtx*
 * \return  successful - 0; otherwise none 0 for failed
 */
int32_t InitFunctionPointers (sWelsEncCtx* pEncCtx, SWelsSvcCodingParam* pParam, uint32_t uiCpuFlag) {
  int32_t iReturn = ENC_RETURN_SUCCESS;
  SWelsFuncPtrList* pFuncList = pEncCtx->pFuncList;
  bool bScreenContent = (SCREEN_CONTENT_REAL_TIME == pParam->iUsageType);

  /* Functionality utilization of CPU instructions dependency */
  pFuncList->pfSetMemZeroSize8              = WelsSetMemZero_c;             // confirmed_safe_unsafe_usage
  pFuncList->pfSetMemZeroSize64Aligned16    = WelsSetMemZero_c;     // confirmed_safe_unsafe_usage
  pFuncList->pfSetMemZeroSize64             = WelsSetMemZero_c;     // confirmed_safe_unsafe_usage
#if defined(X86_ASM)
  if (uiCpuFlag & WELS_CPU_MMXEXT) {
    pFuncList->pfSetMemZeroSize8            = WelsSetMemZeroSize8_mmx;              // confirmed_safe_unsafe_usage
    pFuncList->pfSetMemZeroSize64Aligned16  = WelsSetMemZeroSize64_mmx;     // confirmed_safe_unsafe_usage
    pFuncList->pfSetMemZeroSize64           = WelsSetMemZeroSize64_mmx;     // confirmed_safe_unsafe_usage
  }
  if (uiCpuFlag & WELS_CPU_SSE2) {
    pFuncList->pfSetMemZeroSize64Aligned16  = WelsSetMemZeroAligned64_sse2; // confirmed_safe_unsafe_usage
  }
#endif//X86_ASM

#if defined(HAVE_NEON)
  if (uiCpuFlag & WELS_CPU_NEON) {
    pFuncList->pfSetMemZeroSize8            = WelsSetMemZero_neon;
