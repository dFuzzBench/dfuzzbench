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
 * \file    decoder.c
 *
 * \brief   Interfaces implementation introduced in decoder system architecture
 *
 * \date    03/10/2009 Created
 *
 *************************************************************************************
 */
#include "codec_def.h"
#include "decoder.h"
#include "cpu.h"
#include "au_parser.h"
#include "get_intra_predictor.h"
#include "rec_mb.h"
#include "mc.h"
#include "decode_mb_aux.h"
#include "manage_dec_ref.h"
#include "decoder_core.h"
#include "deblocking.h"
#include "expand_pic.h"
#include "decode_slice.h"
#include "error_concealment.h"
#include "memory_align.h"
#include "wels_decoder_thread.h"

namespace WelsDec {

extern PPicture AllocPicture (PWelsDecoderContext pCtx, const int32_t kiPicWidth, const int32_t kiPicHeight);

extern void FreePicture (PPicture pPic, CMemoryAlign* pMa);

static int32_t CreatePicBuff (PWelsDecoderContext pCtx, PPicBuff* ppPicBuf, const int32_t kiSize,
                              const int32_t kiPicWidth, const int32_t kiPicHeight) {
  PPicBuff pPicBuf = NULL;
  int32_t iPicIdx = 0;
  if (kiSize <= 0 || kiPicWidth <= 0 || kiPicHeight <= 0) {
    return ERR_INFO_INVALID_PARAM;
  }

  CMemoryAlign* pMa = pCtx->pMemAlign;

  pPicBuf = (PPicBuff)pMa->WelsMallocz (sizeof (SPicBuff), "PPicBuff");

  if (NULL == pPicBuf) {
    return ERR_INFO_OUT_OF_MEMORY;
  }

  pPicBuf->ppPic = (PPicture*)pMa->WelsMallocz (kiSize * sizeof (PPicture), "PPicture*");

  if (NULL == pPicBuf->ppPic) {
    pPicBuf->iCapacity = 0;
    DestroyPicBuff (&pPicBuf, pMa);
    return ERR_INFO_OUT_OF_MEMORY;
  }

  for (iPicIdx = 0; iPicIdx < kiSize; ++ iPicIdx) {
    PPicture pPic = AllocPicture (pCtx, kiPicWidth, kiPicHeight);
    if (NULL == pPic) {
      // init capacity first for free memory
      pPicBuf->iCapacity = iPicIdx;
      DestroyPicBuff (&pPicBuf, pMa);
      return ERR_INFO_OUT_OF_MEMORY;
    }
    pPicBuf->ppPic[iPicIdx] = pPic;
  }

// initialize context in queue
  pPicBuf->iCapacity   = kiSize;
  pPicBuf->iCurrentIdx = 0;
  * ppPicBuf           = pPicBuf;

  return ERR_NONE;
}

static int32_t IncreasePicBuff (PWelsDecoderContext pCtx, PPicBuff* ppPicBuf, const int32_t kiOldSize,
                                const int32_t kiPicWidth, const int32_t kiPicHeight, const int32_t kiNewSize) {
  PPicBuff pPicOldBuf = *ppPicBuf;
  PPicBuff pPicNewBuf = NULL;
  int32_t iPicIdx = 0;
  if (kiOldSize <= 0 || kiNewSize <= 0 || kiPicWidth <= 0 || kiPicHeight <= 0) {
    return ERR_INFO_INVALID_PARAM;
  }

  CMemoryAlign* pMa = pCtx->pMemAlign;
  pPicNewBuf = (PPicBuff)pMa->WelsMallocz (sizeof (SPicBuff), "PPicBuff");

  if (NULL == pPicNewBuf) {
    return ERR_INFO_OUT_OF_MEMORY;
  }

  pPicNewBuf->ppPic = (PPicture*)pMa->WelsMallocz (kiNewSize * sizeof (PPicture), "PPicture*");

  if (NULL == pPicNewBuf->ppPic) {
    pPicNewBuf->iCapacity = 0;
    DestroyPicBuff (&pPicNewBuf, pMa);
    return ERR_INFO_OUT_OF_MEMORY;
  }

  // increase new PicBuf
  for (iPicIdx = kiOldSize; iPicIdx < kiNewSize; ++ iPicIdx) {
    PPicture pPic = AllocPicture (pCtx, kiPicWidth, kiPicHeight);
    if (NULL == pPic) {
      // Set maximum capacity as the new malloc memory at the tail
      pPicNewBuf->iCapacity = iPicIdx;
      DestroyPicBuff (&pPicNewBuf, pMa);
      return ERR_INFO_OUT_OF_MEMORY;
    }
    pPicNewBuf->ppPic[iPicIdx] = pPic;
  }

  // copy old PicBuf to new PicBuf
  memcpy (pPicNewBuf->ppPic, pPicOldBuf->ppPic, kiOldSize * sizeof (PPicture));

// initialize context in queue
  pPicNewBuf->iCapacity   = kiNewSize;
  pPicNewBuf->iCurrentIdx = pPicOldBuf->iCurrentIdx;
  * ppPicBuf              = pPicNewBuf;

  for (int32_t i = 0; i < pPicNewBuf->iCapacity; i++) {
    pPicNewBuf->ppPic[i]->bUsedAsRef = false;
    pPicNewBuf->ppPic[i]->bIsLongRef = false;
    pPicNewBuf->ppPic[i]->uiRefCount = 0;
    pPicNewBuf->ppPic[i]->bAvailableFlag = true;
    pPicNewBuf->ppPic[i]->bIsComplete = false;
  }
// remove old PicBuf
  if (pPicOldBuf->ppPic != NULL) {
    pMa->WelsFree (pPicOldBuf->ppPic, "pPicOldBuf->queue");
    pPicOldBuf->ppPic = NULL;
  }
  pPicOldBuf->iCapacity = 0;
  pPicOldBuf->iCurrentIdx = 0;
  pMa->WelsFree (pPicOldBuf, "pPicOldBuf");
  pPicOldBuf = NULL;
  return ERR_NONE;
}

static int32_t DecreasePicBuff (PWelsDecoderContext pCtx, PPicBuff* ppPicBuf, const int32_t kiOldSize,
                                const int32_t kiPicWidth, const int32_t kiPicHeight, const int32_t kiNewSize) {
  PPicBuff pPicOldBuf = *ppPicBuf;
  PPicBuff pPicNewBuf = NULL;
  int32_t iPicIdx = 0;
  if (kiOldSize <= 0 || kiNewSize <= 0 || kiPicWidth <= 0 || kiPicHeight <= 0) {
    return ERR_INFO_INVALID_PARAM;
  }

  CMemoryAlign* pMa = pCtx->pMemAlign;

  pPicNewBuf = (PPicBuff)pMa->WelsMallocz (sizeof (SPicBuff), "PPicBuff");

  if (NULL == pPicNewBuf) {
    return ERR_INFO_OUT_OF_MEMORY;
  }

  pPicNewBuf->ppPic = (PPicture*)pMa->WelsMallocz (kiNewSize * sizeof (PPicture), "PPicture*");

  if (NULL == pPicNewBuf->ppPic) {
    pPicNewBuf->iCapacity = 0;
    DestroyPicBuff (&pPicNewBuf, pMa);
    return ERR_INFO_OUT_OF_MEMORY;
  }

  int32_t iPrevPicIdx = -1;
  for (iPrevPicIdx = 0; iPrevPicIdx < kiOldSize; ++iPrevPicIdx) {
    if (pCtx->pLastDecPicInfo->pPreviousDecodedPictureInDpb == pPicOldBuf->ppPic[iPrevPicIdx]) {
      break;
    }
  }
  int32_t iDelIdx;
  if (iPrevPicIdx < kiOldSize && iPrevPicIdx >= kiNewSize) {
    // found pPreviousDecodedPictureInDpb,
    pPicNewBuf->ppPic[0] = pPicOldBuf->ppPic[iPrevPicIdx];
    pPicNewBuf->iCurrentIdx = 0;
    memcpy (pPicNewBuf->ppPic + 1, pPicOldBuf->ppPic, (kiNewSize - 1) * sizeof (PPicture));
    iDelIdx = kiNewSize - 1;
  } else {
    memcpy (pPicNewBuf->ppPic, pPicOldBuf->ppPic, kiNewSize * sizeof (PPicture));
    pPicNewBuf->iCurrentIdx = iPrevPicIdx < kiNewSize ? iPrevPicIdx : 0;
    iDelIdx = kiNewSize;
  }

  //update references due to allocation changes
  //all references' references have to be reset oss-buzz 14423
  for (int32_t i = 0; i < kiNewSize; i++) {
    for (int32_t listIdx = LIST_0; listIdx < LIST_A; ++listIdx) {
      int32_t j = -1;
      while (++j < MAX_DPB_COUNT && pPicNewBuf->ppPic[i]->pRefPic[listIdx][j] != NULL) {
        pPicNewBuf->ppPic[i]->pRefPic[listIdx][j] = NULL;
      }
    }
  }

  for (iPicIdx = iDelIdx; iPicIdx < kiOldSize; iPicIdx++) {
    if (iPrevPicIdx != iPicIdx) {
      if (pPicOldBuf->ppPic[iPicIdx] != NULL) {
        FreePicture (pPicOldBuf->ppPic[iPicIdx], pMa);
        pPicOldBuf->ppPic[iPicIdx] = NULL;
      }
    }
  }

  // initialize context in queue
  pPicNewBuf->iCapacity = kiNewSize;
  * ppPicBuf             = pPicNewBuf;

  for (int32_t i = 0; i < pPicNewBuf->iCapacity; i++) {
    pPicNewBuf->ppPic[i]->bUsedAsRef = false;
    pPicNewBuf->ppPic[i]->bIsLongRef = false;
    pPicNewBuf->ppPic[i]->uiRefCount = 0;
    pPicNewBuf->ppPic[i]->bAvailableFlag = true;
    pPicNewBuf->ppPic[i]->bIsComplete = false;
  }
  // remove old PicBuf
  if (pPicOldBuf->ppPic != NULL) {
    pMa->WelsFree (pPicOldBuf->ppPic, "pPicOldBuf->queue");
    pPicOldBuf->ppPic = NULL;
  }
  pPicOldBuf->iCapacity = 0;
  pPicOldBuf->iCurrentIdx = 0;
  pMa->WelsFree (pPicOldBuf, "pPicOldBuf");
  pPicOldBuf = NULL;

  return ERR_NONE;
}

void DestroyPicBuff (PPicBuff* ppPicBuf, CMemoryAlign* pMa) {
  PPicBuff pPicBuf = NULL;

  if (NULL == ppPicBuf || NULL == *ppPicBuf)
    return;

  pPicBuf = *ppPicBuf;
  while (pPicBuf->ppPic != NULL) {
    int32_t iPicIdx = 0;
    while (iPicIdx < pPicBuf->iCapacity) {
      PPicture pPic = pPicBuf->ppPic[iPicIdx];
      if (pPic != NULL) {
        FreePicture (pPic, pMa);
      }
      pPic = NULL;
      ++ iPicIdx;
    }

    pMa->WelsFree (pPicBuf->ppPic, "pPicBuf->queue");

    pPicBuf->ppPic = NULL;
  }
  pPicBuf->iCapacity = 0;
  pPicBuf->iCurrentIdx = 0;

  pMa->WelsFree (pPicBuf, "pPicBuf");

  pPicBuf = NULL;
  *ppPicBuf = NULL;
}

/*
 * fill data fields in default for decoder context
 */
void WelsDecoderDefaults (PWelsDecoderContext pCtx, SLogContext* pLogCtx) {
  int32_t iCpuCores               = 1;
  pCtx->sLogCtx = *pLogCtx;

  pCtx->pArgDec                   = NULL;

  pCtx->bHaveGotMemory            = false;              // not ever request memory blocks for decoder context related
  pCtx->uiCpuFlag                 = 0;

  pCtx->bAuReadyFlag              = 0;                  // au data is not ready
  pCtx->bCabacInited = false;

  pCtx->uiCpuFlag = WelsCPUFeatureDetect (&iCpuCores);

  pCtx->iImgWidthInPixel          = 0;
  pCtx->iImgHeightInPixel         = 0;                  // alloc picture data when picture size is available
  pCtx->iLastImgWidthInPixel      = 0;
  pCtx->iLastImgHeightInPixel     = 0;
  pCtx->bFreezeOutput = true;

  pCtx->iFrameNum                 = -1;
  pCtx->pLastDecPicInfo->iPrevFrameNum             = -1;
