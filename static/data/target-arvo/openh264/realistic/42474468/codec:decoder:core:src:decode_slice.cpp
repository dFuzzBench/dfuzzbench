/*!
 * \copy
 *     Copyright (c)  2008-2013, Cisco Systems
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
 *  Abstract
 *      current slice decoding
 *
 *  History
 *      07/10/2008 Created
 *      08/09/2013 Modified
 *
 *****************************************************************************/


#include "deblocking.h"

#include "decode_slice.h"

#include "parse_mb_syn_cavlc.h"
#include "parse_mb_syn_cabac.h"
#include "rec_mb.h"
#include "mv_pred.h"

#include "cpu_core.h"

namespace WelsDec {

extern void FreePicture (PPicture pPic, CMemoryAlign* pMa);

extern PPicture AllocPicture (PWelsDecoderContext pCtx, const int32_t kiPicWidth, const int32_t kiPicHeight);

static bool CheckRefPics (const PWelsDecoderContext& pCtx) {
  int32_t listCount = 1;
  if (pCtx->eSliceType == B_SLICE) {
    ++listCount;
  }
  for (int32_t list = LIST_0; list < listCount; ++list) {
    int32_t shortRefCount = pCtx->sRefPic.uiShortRefCount[list];
    for (int32_t refIdx = 0; refIdx < shortRefCount; ++refIdx) {
      if (!pCtx->sRefPic.pShortRefList[list][refIdx]) {
        return false;
      }
    }
    int32_t longRefCount = pCtx->sRefPic.uiLongRefCount[list];
    for (int32_t refIdx = 0; refIdx < longRefCount; ++refIdx) {
      if (!pCtx->sRefPic.pLongRefList[list][refIdx]) {
        return false;
      }
    }
  }
  return true;
}

int32_t WelsTargetSliceConstruction (PWelsDecoderContext pCtx) {
  PDqLayer pCurLayer = pCtx->pCurDqLayer;
  PSlice pCurSlice = &pCurLayer->sLayerInfo.sSliceInLayer;
  PSliceHeader pSliceHeader = &pCurSlice->sSliceHeaderExt.sSliceHeader;

  int32_t iTotalMbTargetLayer = pSliceHeader->pSps->uiTotalMbCount;

  int32_t iCurLayerWidth  = pCurLayer->iMbWidth << 4;
  int32_t iCurLayerHeight = pCurLayer->iMbHeight << 4;

  int32_t iNextMbXyIndex = 0;
  PFmo pFmo = pCtx->pFmo;

  int32_t iTotalNumMb = pCurSlice->iTotalMbInCurSlice;
  int32_t iCountNumMb = 0;
  PDeblockingFilterMbFunc pDeblockMb;

  if (!pCtx->bAvcBasedFlag && iCurLayerWidth != pCtx->iCurSeqIntervalMaxPicWidth) {
    return ERR_INFO_WIDTH_MISMATCH;
  }

  iNextMbXyIndex   = pSliceHeader->iFirstMbInSlice;
  pCurLayer->iMbX  = iNextMbXyIndex % pCurLayer->iMbWidth;
  pCurLayer->iMbY  = iNextMbXyIndex / pCurLayer->iMbWidth;
  pCurLayer->iMbXyIndex = iNextMbXyIndex;

  if (0 == iNextMbXyIndex) {
    pCurLayer->pDec->iSpsId = pCtx->pSps->iSpsId;
    pCurLayer->pDec->iPpsId = pCtx->pPps->iPpsId;

    pCurLayer->pDec->uiQualityId = pCurLayer->sLayerInfo.sNalHeaderExt.uiQualityId;
  }

  do {
    if (iCountNumMb >= iTotalNumMb) {
      break;
    }

    if (!pCtx->pParam->bParseOnly) { //for parse only, actual recon MB unnecessary
      if (WelsTargetMbConstruction (pCtx)) {
        WelsLog (& (pCtx->sLogCtx), WELS_LOG_WARNING,
                 "WelsTargetSliceConstruction():::MB(%d, %d) construction error. pCurSlice_type:%d",
                 pCurLayer->iMbX, pCurLayer->iMbY, pCurSlice->eSliceType);

        return ERR_INFO_MB_RECON_FAIL;
      }
    }

    ++iCountNumMb;
    if (!pCurLayer->pMbCorrectlyDecodedFlag[iNextMbXyIndex]) { //already con-ed, overwrite
      pCurLayer->pMbCorrectlyDecodedFlag[iNextMbXyIndex] = true;
      pCtx->pDec->iMbEcedPropNum += (pCurLayer->pMbRefConcealedFlag[iNextMbXyIndex] ? 1 : 0);
      ++pCtx->iTotalNumMbRec;
    }

    if (pCtx->iTotalNumMbRec > iTotalMbTargetLayer) {
      WelsLog (& (pCtx->sLogCtx), WELS_LOG_WARNING,
               "WelsTargetSliceConstruction():::pCtx->iTotalNumMbRec:%d, iTotalMbTargetLayer:%d",
               pCtx->iTotalNumMbRec, iTotalMbTargetLayer);

      return ERR_INFO_MB_NUM_EXCEED_FAIL;
    }

    if (pSliceHeader->pPps->uiNumSliceGroups > 1) {
      iNextMbXyIndex = FmoNextMb (pFmo, iNextMbXyIndex);
    } else {
      ++iNextMbXyIndex;
    }
    if (-1 == iNextMbXyIndex || iNextMbXyIndex >= iTotalMbTargetLayer) { // slice group boundary or end of a frame
      break;
    }
    pCurLayer->iMbX  = iNextMbXyIndex % pCurLayer->iMbWidth;
    pCurLayer->iMbY  = iNextMbXyIndex / pCurLayer->iMbWidth;
    pCurLayer->iMbXyIndex = iNextMbXyIndex;
  } while (1);

  pCtx->pDec->iWidthInPixel  = iCurLayerWidth;
  pCtx->pDec->iHeightInPixel = iCurLayerHeight;

  if ((pCurSlice->eSliceType != I_SLICE) && (pCurSlice->eSliceType != P_SLICE) && (pCurSlice->eSliceType != B_SLICE))
    return ERR_NONE; //no error but just ignore the type unsupported

  if (pCtx->pParam->bParseOnly) //for parse only, deblocking should not go on
    return ERR_NONE;

  pDeblockMb = WelsDeblockingMb;

  if (1 == pSliceHeader->uiDisableDeblockingFilterIdc
      || pCtx->pCurDqLayer->sLayerInfo.sSliceInLayer.iTotalMbInCurSlice <= 0) {
    return ERR_NONE;//NO_SUPPORTED_FILTER_IDX
  } else {
    WelsDeblockingFilterSlice (pCtx, pDeblockMb);
  }
  // any other filter_idc not supported here, 7/22/2010

  return ERR_NONE;
}

int32_t WelsMbInterSampleConstruction (PWelsDecoderContext pCtx, PDqLayer pCurLayer,
                                       uint8_t* pDstY, uint8_t* pDstU, uint8_t* pDstV, int32_t iStrideL, int32_t iStrideC) {
  int32_t iMbXy = pCurLayer->iMbXyIndex;
  int32_t i, iIndex, iOffset;

  if (pCurLayer->pTransformSize8x8Flag[iMbXy]) {
    for (i = 0; i < 4; i++) {
      iIndex = g_kuiMbCountScan4Idx[i << 2];
      if (pCurLayer->pNzc[iMbXy][iIndex] || pCurLayer->pNzc[iMbXy][iIndex + 1] || pCurLayer->pNzc[iMbXy][iIndex + 4]
          || pCurLayer->pNzc[iMbXy][iIndex + 5]) {
        iOffset = ((iIndex >> 2) << 2) * iStrideL + ((iIndex % 4) << 2);
        pCtx->pIdctResAddPredFunc8x8 (pDstY + iOffset, iStrideL, pCurLayer->pScaledTCoeff[iMbXy] + (i << 6));
      }
    }
  } else {
    // luma.
    const int8_t* pNzc = pCurLayer->pNzc[iMbXy];
    int16_t* pScaledTCoeff = pCurLayer->pScaledTCoeff[iMbXy];
    pCtx->pIdctFourResAddPredFunc (pDstY + 0 * iStrideL + 0, iStrideL, pScaledTCoeff + 0 * 64, pNzc +  0);
    pCtx->pIdctFourResAddPredFunc (pDstY + 0 * iStrideL + 8, iStrideL, pScaledTCoeff + 1 * 64, pNzc +  2);
    pCtx->pIdctFourResAddPredFunc (pDstY + 8 * iStrideL + 0, iStrideL, pScaledTCoeff + 2 * 64, pNzc +  8);
    pCtx->pIdctFourResAddPredFunc (pDstY + 8 * iStrideL + 8, iStrideL, pScaledTCoeff + 3 * 64, pNzc + 10);
  }

  const int8_t* pNzc = pCurLayer->pNzc[iMbXy];
  int16_t* pScaledTCoeff = pCurLayer->pScaledTCoeff[iMbXy];
  // Cb.
  pCtx->pIdctFourResAddPredFunc (pDstU, iStrideC, pScaledTCoeff + 4 * 64, pNzc + 16);
  // Cr.
  pCtx->pIdctFourResAddPredFunc (pDstV, iStrideC, pScaledTCoeff + 5 * 64, pNzc + 18);

  return ERR_NONE;
}
int32_t WelsMbInterConstruction (PWelsDecoderContext pCtx, PDqLayer pCurLayer) {
  int32_t iMbX = pCurLayer->iMbX;
  int32_t iMbY = pCurLayer->iMbY;
  uint8_t*  pDstY, *pDstCb, *pDstCr;

  int32_t iLumaStride   = pCtx->pDec->iLinesize[0];
  int32_t iChromaStride = pCtx->pDec->iLinesize[1];

  pDstY  = pCurLayer->pDec->pData[0] + ((iMbY * iLumaStride + iMbX) << 4);
  pDstCb = pCurLayer->pDec->pData[1] + ((iMbY * iChromaStride + iMbX) << 3);
  pDstCr = pCurLayer->pDec->pData[2] + ((iMbY * iChromaStride + iMbX) << 3);

  if (pCtx->eSliceType == P_SLICE) {
    WELS_B_MB_REC_VERIFY (GetInterPred (pDstY, pDstCb, pDstCr, pCtx));
  } else {
    if (pCtx->pTempDec == NULL)
      pCtx->pTempDec = AllocPicture (pCtx, pCtx->pSps->iMbWidth << 4, pCtx->pSps->iMbHeight << 4);
    uint8_t*   pTempDstYCbCr[3];
    uint8_t*   pDstYCbCr[3];
    pTempDstYCbCr[0] = pCtx->pTempDec->pData[0] + ((iMbY * iLumaStride + iMbX) << 4);
    pTempDstYCbCr[1] = pCtx->pTempDec->pData[1] + ((iMbY * iChromaStride + iMbX) << 3);
    pTempDstYCbCr[2] = pCtx->pTempDec->pData[2] + ((iMbY * iChromaStride + iMbX) << 3);
    pDstYCbCr[0] = pDstY;
    pDstYCbCr[1] = pDstCb;
    pDstYCbCr[2] = pDstCr;
    WELS_B_MB_REC_VERIFY (GetInterBPred (pDstYCbCr, pTempDstYCbCr, pCtx));
  }
  WelsMbInterSampleConstruction (pCtx, pCurLayer, pDstY, pDstCb, pDstCr, iLumaStride, iChromaStride);

  pCtx->sBlockFunc.pWelsSetNonZeroCountFunc (
    pCurLayer->pNzc[pCurLayer->iMbXyIndex]); // set all none-zero nzc to 1; dbk can be opti!
  return ERR_NONE;
}

void WelsLumaDcDequantIdct (int16_t* pBlock, int32_t iQp, PWelsDecoderContext pCtx) {
  const int32_t kiQMul = pCtx->bUseScalingList ? pCtx->pDequant_coeff4x4[0][iQp][0] : (g_kuiDequantCoeff[iQp][0] << 4);
#define STRIDE 16
  int32_t i;
  int32_t iTemp[16]; //FIXME check if this is a good idea
  int16_t* pBlk = pBlock;
  static const int32_t kiXOffset[4] = {0, STRIDE, STRIDE << 2,  5 * STRIDE};
  static const int32_t kiYOffset[4] = {0, STRIDE << 1, STRIDE << 3, 10 * STRIDE};

  for (i = 0; i < 4; i++) {
    const int32_t kiOffset = kiYOffset[i];
    const int32_t kiX1 = kiOffset + kiXOffset[2];
    const int32_t kiX2 = STRIDE + kiOffset;
    const int32_t kiX3 = kiOffset + kiXOffset[3];
    const int32_t kiI4 = i << 2; // 4*i
    const int32_t kiZ0 = pBlk[kiOffset] + pBlk[kiX1];
    const int32_t kiZ1 = pBlk[kiOffset] - pBlk[kiX1];
    const int32_t kiZ2 = pBlk[kiX2] - pBlk[kiX3];
    const int32_t kiZ3 = pBlk[kiX2] + pBlk[kiX3];

    iTemp[kiI4]  = kiZ0 + kiZ3;
    iTemp[1 + kiI4] = kiZ1 + kiZ2;
    iTemp[2 + kiI4] = kiZ1 - kiZ2;
    iTemp[3 + kiI4] = kiZ0 - kiZ3;
  }

  for (i = 0; i < 4; i++) {
    const int32_t kiOffset = kiXOffset[i];
    const int32_t kiI4 = 4 + i;
    const int32_t kiZ0 = iTemp[i] + iTemp[4 + kiI4];
    const int32_t kiZ1 = iTemp[i] - iTemp[4 + kiI4];
    const int32_t kiZ2 = iTemp[kiI4] - iTemp[8 + kiI4];
    const int32_t kiZ3 = iTemp[kiI4] + iTemp[8 + kiI4];

    pBlk[kiOffset] = ((kiZ0 + kiZ3) * kiQMul + (1 << 5)) >> 6; //FIXME think about merging this into decode_resdual
    pBlk[kiYOffset[1] + kiOffset] = ((kiZ1 + kiZ2) * kiQMul + (1 << 5)) >> 6;
    pBlk[kiYOffset[2] + kiOffset] = ((kiZ1 - kiZ2) * kiQMul + (1 << 5)) >> 6;
    pBlk[kiYOffset[3] + kiOffset] = ((kiZ0 - kiZ3) * kiQMul + (1 << 5)) >> 6;
  }
#undef STRIDE
}

int32_t WelsMbIntraPredictionConstruction (PWelsDecoderContext pCtx, PDqLayer pCurLayer, bool bOutput) {
//seems IPCM should not enter this path
  int32_t iMbXy = pCurLayer->iMbXyIndex;

  WelsFillRecNeededMbInfo (pCtx, bOutput, pCurLayer);

  if (IS_INTRA16x16 (pCurLayer->pMbType[iMbXy])) {
    RecI16x16Mb (iMbXy, pCtx, pCurLayer->pScaledTCoeff[iMbXy], pCurLayer);
  } else if (IS_INTRA8x8 (pCurLayer->pMbType[iMbXy])) {
    RecI8x8Mb (iMbXy, pCtx, pCurLayer->pScaledTCoeff[iMbXy], pCurLayer);
  } else if (IS_INTRA4x4 (pCurLayer->pMbType[iMbXy])) {
    RecI4x4Mb (iMbXy, pCtx, pCurLayer->pScaledTCoeff[iMbXy], pCurLayer);
  }
  return ERR_NONE;
}

int32_t WelsMbInterPrediction (PWelsDecoderContext pCtx, PDqLayer pCurLayer) {
  int32_t iMbX = pCurLayer->iMbX;
  int32_t iMbY = pCurLayer->iMbY;
  uint8_t*  pDstY, *pDstCb, *pDstCr;

  int32_t iLumaStride   = pCtx->pDec->iLinesize[0];
  int32_t iChromaStride = pCtx->pDec->iLinesize[1];

  pDstY  = pCurLayer->pDec->pData[0] + ((iMbY * iLumaStride + iMbX) << 4);
  pDstCb = pCurLayer->pDec->pData[1] + ((iMbY * iChromaStride + iMbX) << 3);
  pDstCr = pCurLayer->pDec->pData[2] + ((iMbY * iChromaStride + iMbX) << 3);

  if (pCtx->eSliceType == P_SLICE) {
    WELS_B_MB_REC_VERIFY (GetInterPred (pDstY, pDstCb, pDstCr, pCtx));
  } else {
    if (pCtx->pTempDec == NULL)
      pCtx->pTempDec = AllocPicture (pCtx, pCtx->pSps->iMbWidth << 4, pCtx->pSps->iMbHeight << 4);
    uint8_t*   pTempDstYCbCr[3];
    uint8_t*   pDstYCbCr[3];
    pTempDstYCbCr[0] = pCtx->pTempDec->pData[0] + ((iMbY * iLumaStride + iMbX) << 4);
    pTempDstYCbCr[1] = pCtx->pTempDec->pData[1] + ((iMbY * iChromaStride + iMbX) << 3);
    pTempDstYCbCr[2] = pCtx->pTempDec->pData[2] + ((iMbY * iChromaStride + iMbX) << 3);
    pDstYCbCr[0] = pDstY;
    pDstYCbCr[1] = pDstCb;
    pDstYCbCr[2] = pDstCr;
    WELS_B_MB_REC_VERIFY (GetInterBPred (pDstYCbCr, pTempDstYCbCr, pCtx));
  }
  return ERR_NONE;
}

int32_t WelsTargetMbConstruction (PWelsDecoderContext pCtx) {
  PDqLayer pCurLayer = pCtx->pCurDqLayer;
  if (MB_TYPE_INTRA_PCM == pCurLayer->pMbType[pCurLayer->iMbXyIndex]) {
    //already decoded and reconstructed when parsing
    return ERR_NONE;
  } else if (IS_INTRA (pCurLayer->pMbType[pCurLayer->iMbXyIndex])) {
    WelsMbIntraPredictionConstruction (pCtx, pCurLayer, 1);
  } else if (IS_INTER (pCurLayer->pMbType[pCurLayer->iMbXyIndex])) { //InterMB
    if (0 == pCurLayer->pCbp[pCurLayer->iMbXyIndex]) { //uiCbp==0 include SKIP
      if (!CheckRefPics (pCtx)) {
        return ERR_INFO_MB_RECON_FAIL;
      }
      return WelsMbInterPrediction (pCtx, pCurLayer);
    } else {
      WelsMbInterConstruction (pCtx, pCurLayer);
    }
  } else {
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_WARNING, "WelsTargetMbConstruction():::::Unknown MB type: %d",
             pCurLayer->pMbType[pCurLayer->iMbXyIndex]);
    return ERR_INFO_MB_RECON_FAIL;
  }

  return ERR_NONE;
}

void WelsChromaDcIdct (int16_t* pBlock) {
  int32_t iStride = 32;
  int32_t iXStride = 16;
  int32_t iStride1 = iXStride + iStride;
  int16_t* pBlk = pBlock;
  int32_t iA, iB, iC, iD, iE;

  iA = pBlk[0];
  iB = pBlk[iXStride];
  iC = pBlk[iStride];
  iD = pBlk[iStride1];

  iE = iA - iB;
  iA += iB;
  iB = iC - iD;
  iC += iD;

  pBlk[0] = (iA + iC);
  pBlk[iXStride] = (iE + iB);
  pBlk[iStride] = (iA - iC);
  pBlk[iStride1] = (iE - iB);
}

void WelsMapNxNNeighToSampleNormal (PWelsNeighAvail pNeighAvail, int32_t* pSampleAvail) {
  if (pNeighAvail->iLeftAvail) {  //left
    pSampleAvail[ 6] =
      pSampleAvail[12] =
        pSampleAvail[18] =
          pSampleAvail[24] = 1;
  }
  if (pNeighAvail->iLeftTopAvail) { //top_left
    pSampleAvail[0] = 1;
  }
  if (pNeighAvail->iTopAvail) { //top
    pSampleAvail[1] =
      pSampleAvail[2] =
        pSampleAvail[3] =
          pSampleAvail[4] = 1;
  }
  if (pNeighAvail->iRightTopAvail) { //top_right
    pSampleAvail[5] = 1;
  }
}

void WelsMapNxNNeighToSampleConstrain1 (PWelsNeighAvail pNeighAvail, int32_t* pSampleAvail) {
  if (pNeighAvail->iLeftAvail && IS_INTRA (pNeighAvail->iLeftType)) {   //left
    pSampleAvail[ 6] =
      pSampleAvail[12] =
        pSampleAvail[18] =
          pSampleAvail[24] = 1;
  }
  if (pNeighAvail->iLeftTopAvail && IS_INTRA (pNeighAvail->iLeftTopType)) {  //top_left
    pSampleAvail[0] = 1;
  }
  if (pNeighAvail->iTopAvail && IS_INTRA (pNeighAvail->iTopType)) {  //top
    pSampleAvail[1] =
      pSampleAvail[2] =
        pSampleAvail[3] =
          pSampleAvail[4] = 1;
  }
  if (pNeighAvail->iRightTopAvail && IS_INTRA (pNeighAvail->iRightTopType)) {  //top_right
    pSampleAvail[5] = 1;
  }
}
void WelsMap16x16NeighToSampleNormal (PWelsNeighAvail pNeighAvail, uint8_t* pSampleAvail) {
  if (pNeighAvail->iLeftAvail) {
    *pSampleAvail = (1 << 2);
  }
  if (pNeighAvail->iLeftTopAvail) {
    *pSampleAvail |= (1 << 1);
  }
  if (pNeighAvail->iTopAvail) {
    *pSampleAvail |= 1;
  }
}

void WelsMap16x16NeighToSampleConstrain1 (PWelsNeighAvail pNeighAvail, uint8_t* pSampleAvail) {
  if (pNeighAvail->iLeftAvail && IS_INTRA (pNeighAvail->iLeftType)) {
    *pSampleAvail = (1 << 2);
  }
  if (pNeighAvail->iLeftTopAvail && IS_INTRA (pNeighAvail->iLeftTopType)) {
    *pSampleAvail |= (1 << 1);
  }
  if (pNeighAvail->iTopAvail && IS_INTRA (pNeighAvail->iTopType)) {
    *pSampleAvail |= 1;
  }
}

int32_t ParseIntra4x4Mode (PWelsDecoderContext pCtx, PWelsNeighAvail pNeighAvail, int8_t* pIntraPredMode,
                           PBitStringAux pBs,
                           PDqLayer pCurDqLayer) {
  int32_t iSampleAvail[5 * 6] = { 0 }; //initialize as 0
  int32_t iMbXy = pCurDqLayer->iMbXyIndex;
  int32_t iFinalMode, i;

  uint8_t uiNeighAvail = 0;
  uint32_t uiCode;
  int32_t iCode;
  pCtx->pMapNxNNeighToSampleFunc (pNeighAvail, iSampleAvail);
  uiNeighAvail = (iSampleAvail[6] << 2) | (iSampleAvail[0] << 1) | (iSampleAvail[1]);
  for (i = 0; i < 16; i++) {
    int32_t iPrevIntra4x4PredMode = 0;
    if (pCurDqLayer->sLayerInfo.pPps->bEntropyCodingModeFlag) {
      WELS_READ_VERIFY (ParseIntraPredModeLumaCabac (pCtx, iCode));
      iPrevIntra4x4PredMode = iCode;
    } else {
      WELS_READ_VERIFY (BsGetOneBit (pBs, &uiCode));
      iPrevIntra4x4PredMode = uiCode;
    }
    const int32_t kiPredMode = PredIntra4x4Mode (pIntraPredMode, i);

    int8_t iBestMode;
    if (pCurDqLayer->sLayerInfo.pPps->bEntropyCodingModeFlag) {
      if (iPrevIntra4x4PredMode == -1)
        iBestMode = kiPredMode;
      else
        iBestMode = iPrevIntra4x4PredMode + (iPrevIntra4x4PredMode >= kiPredMode);
    } else {
      if (iPrevIntra4x4PredMode) {
        iBestMode = kiPredMode;
      } else {
        WELS_READ_VERIFY (BsGetBits (pBs, 3, &uiCode));
        iBestMode = uiCode + ((int32_t) uiCode >= kiPredMode);
      }
    }

    iFinalMode = CheckIntraNxNPredMode (&iSampleAvail[0], &iBestMode, i, false);
    if (iFinalMode == GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INVALID_INTRA4X4_MODE)) {
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I4x4_PRED_MODE);
    }

    pCurDqLayer->pIntra4x4FinalMode[iMbXy][g_kuiScan4[i]] = iFinalMode;

    pIntraPredMode[g_kuiScan8[i]] = iBestMode;

    iSampleAvail[g_kuiCache30ScanIdx[i]] = 1;
  }
  ST32 (&pCurDqLayer->pIntraPredMode[iMbXy][0], LD32 (&pIntraPredMode[1 + 8 * 4]));
  pCurDqLayer->pIntraPredMode[iMbXy][4] = pIntraPredMode[4 + 8 * 1];
  pCurDqLayer->pIntraPredMode[iMbXy][5] = pIntraPredMode[4 + 8 * 2];
  pCurDqLayer->pIntraPredMode[iMbXy][6] = pIntraPredMode[4 + 8 * 3];

  if (pCtx->pSps->uiChromaFormatIdc == 0)//no need parse chroma
    return ERR_NONE;

  if (pCurDqLayer->sLayerInfo.pPps->bEntropyCodingModeFlag) {
    WELS_READ_VERIFY (ParseIntraPredModeChromaCabac (pCtx, uiNeighAvail, iCode));
    if (iCode > MAX_PRED_MODE_ID_CHROMA) {
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
    }
    pCurDqLayer->pChromaPredMode[iMbXy] = iCode;
  } else {
    WELS_READ_VERIFY (BsGetUe (pBs, &uiCode)); //intra_chroma_pred_mode
    if (uiCode > MAX_PRED_MODE_ID_CHROMA) {
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
    }
    pCurDqLayer->pChromaPredMode[iMbXy] = uiCode;
  }

  if (-1 == pCurDqLayer->pChromaPredMode[iMbXy]
      || CheckIntraChromaPredMode (uiNeighAvail, &pCurDqLayer->pChromaPredMode[iMbXy])) {
    return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
  }
  return ERR_NONE;
}

int32_t ParseIntra8x8Mode (PWelsDecoderContext pCtx, PWelsNeighAvail pNeighAvail, int8_t* pIntraPredMode,
                           PBitStringAux pBs,
                           PDqLayer pCurDqLayer) {
  // Similar with Intra_4x4, can put them together when needed
  int32_t iSampleAvail[5 * 6] = { 0 }; //initialize as 0
  int32_t iMbXy = pCurDqLayer->iMbXyIndex;
  int32_t iFinalMode, i;

  uint8_t uiNeighAvail = 0;
  uint32_t uiCode;
  int32_t iCode;
  pCtx->pMapNxNNeighToSampleFunc (pNeighAvail, iSampleAvail);
  // Top-Right : Left : Top-Left : Top
  uiNeighAvail = (iSampleAvail[5] << 3) | (iSampleAvail[6] << 2) | (iSampleAvail[0] << 1) | (iSampleAvail[1]);

  pCurDqLayer->pIntraNxNAvailFlag[iMbXy] = uiNeighAvail;

  for (i = 0; i < 4; i++) {
    int32_t iPrevIntra4x4PredMode = 0;
    if (pCurDqLayer->sLayerInfo.pPps->bEntropyCodingModeFlag) {
      WELS_READ_VERIFY (ParseIntraPredModeLumaCabac (pCtx, iCode));
      iPrevIntra4x4PredMode = iCode;
    } else {
      WELS_READ_VERIFY (BsGetOneBit (pBs, &uiCode));
      iPrevIntra4x4PredMode = uiCode;
    }
    const int32_t kiPredMode = PredIntra4x4Mode (pIntraPredMode, i << 2);

    int8_t iBestMode;
    if (pCurDqLayer->sLayerInfo.pPps->bEntropyCodingModeFlag) {
      if (iPrevIntra4x4PredMode == -1)
        iBestMode = kiPredMode;
      else
        iBestMode = iPrevIntra4x4PredMode + (iPrevIntra4x4PredMode >= kiPredMode);
    } else {
      if (iPrevIntra4x4PredMode) {
        iBestMode = kiPredMode;
      } else {
        WELS_READ_VERIFY (BsGetBits (pBs, 3, &uiCode));
        iBestMode = uiCode + ((int32_t) uiCode >= kiPredMode);
      }
    }

    iFinalMode = CheckIntraNxNPredMode (&iSampleAvail[0], &iBestMode, i << 2, true);

    if (iFinalMode == GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INVALID_INTRA4X4_MODE)) {
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I4x4_PRED_MODE);
    }

    for (int j = 0; j < 4; j++) {
      pCurDqLayer->pIntra4x4FinalMode[iMbXy][g_kuiScan4[ (i << 2) + j]] = iFinalMode;
      pIntraPredMode[g_kuiScan8[ (i << 2) + j]] = iBestMode;
      iSampleAvail[g_kuiCache30ScanIdx[ (i << 2) + j]] = 1;
    }
  }
  ST32 (&pCurDqLayer->pIntraPredMode[iMbXy][0], LD32 (&pIntraPredMode[1 + 8 * 4]));
  pCurDqLayer->pIntraPredMode[iMbXy][4] = pIntraPredMode[4 + 8 * 1];
  pCurDqLayer->pIntraPredMode[iMbXy][5] = pIntraPredMode[4 + 8 * 2];
  pCurDqLayer->pIntraPredMode[iMbXy][6] = pIntraPredMode[4 + 8 * 3];

  if (pCtx->pSps->uiChromaFormatIdc == 0)
    return ERR_NONE;

  if (pCurDqLayer->sLayerInfo.pPps->bEntropyCodingModeFlag) {
    WELS_READ_VERIFY (ParseIntraPredModeChromaCabac (pCtx, uiNeighAvail, iCode));
    if (iCode > MAX_PRED_MODE_ID_CHROMA) {
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
    }
    pCurDqLayer->pChromaPredMode[iMbXy] = iCode;
  } else {
    WELS_READ_VERIFY (BsGetUe (pBs, &uiCode)); //intra_chroma_pred_mode
    if (uiCode > MAX_PRED_MODE_ID_CHROMA) {
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
    }
    pCurDqLayer->pChromaPredMode[iMbXy] = uiCode;
  }

  if (-1 == pCurDqLayer->pChromaPredMode[iMbXy]
      || CheckIntraChromaPredMode (uiNeighAvail, &pCurDqLayer->pChromaPredMode[iMbXy])) {
    return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
  }

  return ERR_NONE;
}

int32_t ParseIntra16x16Mode (PWelsDecoderContext pCtx, PWelsNeighAvail pNeighAvail, PBitStringAux pBs,
                             PDqLayer pCurDqLayer) {
  int32_t iMbXy = pCurDqLayer->iMbXyIndex;
  uint8_t uiNeighAvail = 0; //0x07 = 0 1 1 1, means left, top-left, top avail or not. (1: avail, 0: unavail)
  uint32_t uiCode;
  int32_t iCode;
  pCtx->pMap16x16NeighToSampleFunc (pNeighAvail, &uiNeighAvail);

  if (CheckIntra16x16PredMode (uiNeighAvail,
                               &pCurDqLayer->pIntraPredMode[iMbXy][7])) { //invalid iPredMode, must stop decoding
    return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I16x16_PRED_MODE);
  }
  if (pCtx->pSps->uiChromaFormatIdc == 0)
    return ERR_NONE;

  if (pCurDqLayer->sLayerInfo.pPps->bEntropyCodingModeFlag) {
    WELS_READ_VERIFY (ParseIntraPredModeChromaCabac (pCtx, uiNeighAvail, iCode));
    if (iCode > MAX_PRED_MODE_ID_CHROMA) {
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
    }
    pCurDqLayer->pChromaPredMode[iMbXy] = iCode;
  } else {
    WELS_READ_VERIFY (BsGetUe (pBs, &uiCode)); //intra_chroma_pred_mode
    if (uiCode > MAX_PRED_MODE_ID_CHROMA) {
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
    }
    pCurDqLayer->pChromaPredMode[iMbXy] = uiCode;
  }
  if (-1 == pCurDqLayer->pChromaPredMode[iMbXy]
      || CheckIntraChromaPredMode (uiNeighAvail, &pCurDqLayer->pChromaPredMode[iMbXy])) {
    return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_I_CHROMA_PRED_MODE);
  }

  return ERR_NONE;
}

int32_t WelsDecodeMbCabacISliceBaseMode0 (PWelsDecoderContext pCtx, uint32_t& uiEosFlag) {
  PDqLayer pCurLayer             = pCtx->pCurDqLayer;
  PBitStringAux pBsAux           = pCurLayer->pBitStringAux;
  PSlice pSlice                  = &pCurLayer->sLayerInfo.sSliceInLayer;
  PSliceHeader pSliceHeader      = &pSlice->sSliceHeaderExt.sSliceHeader;
  SWelsNeighAvail sNeighAvail;
  int32_t iScanIdxStart = pSlice->sSliceHeaderExt.uiScanIdxStart;
  int32_t iScanIdxEnd   = pSlice->sSliceHeaderExt.uiScanIdxEnd;
  int32_t iMbXy = pCurLayer->iMbXyIndex;
  int32_t i;
  uint32_t uiMbType = 0, uiCbp = 0, uiCbpLuma = 0, uiCbpChroma = 0;

  ENFORCE_STACK_ALIGN_1D (uint8_t, pNonZeroCount, 48, 16);

  pCurLayer->pNoSubMbPartSizeLessThan8x8Flag[iMbXy] = true;
  pCurLayer->pTransformSize8x8Flag[iMbXy] = false;

  pCurLayer->pInterPredictionDoneFlag[iMbXy] = 0;
  pCurLayer->pResidualPredFlag[iMbXy] = pSlice->sSliceHeaderExt.bDefaultResidualPredFlag;
  GetNeighborAvailMbType (&sNeighAvail, pCurLayer);
  WELS_READ_VERIFY (ParseMBTypeISliceCabac (pCtx, &sNeighAvail, uiMbType));
  if (uiMbType > 25) {
    return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_MB_TYPE);
  } else if (!pCtx->pSps->uiChromaFormatIdc && ((uiMbType >= 5 && uiMbType <= 12) || (uiMbType >= 17
             && uiMbType <= 24))) {
    return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_MB_TYPE);
  } else if (25 == uiMbType) {   //I_PCM
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_DEBUG, "I_PCM mode exists in I slice!");
    WELS_READ_VERIFY (ParseIPCMInfoCabac (pCtx));
    pSlice->iLastDeltaQp = 0;
    WELS_READ_VERIFY (ParseEndOfSliceCabac (pCtx, uiEosFlag));
    if (uiEosFlag) {
      RestoreCabacDecEngineToBS (pCtx->pCabacDecEngine, pCtx->pCurDqLayer->pBitStringAux);
    }
    return ERR_NONE;
  } else if (0 == uiMbType) { //I4x4
    ENFORCE_STACK_ALIGN_1D (int8_t, pIntraPredMode, 48, 16);
    pCurLayer->pMbType[iMbXy] = MB_TYPE_INTRA4x4;
    if (pCtx->pPps->bTransform8x8ModeFlag) {
      // Transform 8x8 cabac will be added soon
      WELS_READ_VERIFY (ParseTransformSize8x8FlagCabac (pCtx, &sNeighAvail, pCtx->pCurDqLayer->pTransformSize8x8Flag[iMbXy]));
    }
    if (pCtx->pCurDqLayer->pTransformSize8x8Flag[iMbXy]) {
      uiMbType = pCurLayer->pMbType[iMbXy] = MB_TYPE_INTRA8x8;
      pCtx->pFillInfoCacheIntraNxNFunc (&sNeighAvail, pNonZeroCount, pIntraPredMode, pCurLayer);
      WELS_READ_VERIFY (ParseIntra8x8Mode (pCtx, &sNeighAvail, pIntraPredMode, pBsAux, pCurLayer));
    } else {
      pCtx->pFillInfoCacheIntraNxNFunc (&sNeighAvail, pNonZeroCount, pIntraPredMode, pCurLayer);
      WELS_READ_VERIFY (ParseIntra4x4Mode (pCtx, &sNeighAvail, pIntraPredMode, pBsAux, pCurLayer));
    }
    //get uiCbp for I4x4
    WELS_READ_VERIFY (ParseCbpInfoCabac (pCtx, &sNeighAvail, uiCbp));
    pCurLayer->pCbp[iMbXy] = uiCbp;
    pSlice->iLastDeltaQp = uiCbp == 0 ? 0 : pSlice->iLastDeltaQp;
    uiCbpChroma = pCtx->pSps->uiChromaFormatIdc ? uiCbp >> 4 : 0;
    uiCbpLuma = uiCbp & 15;
  } else { //I16x16;
    pCurLayer->pMbType[iMbXy] = MB_TYPE_INTRA16x16;
    pCurLayer->pTransformSize8x8Flag[iMbXy] = false;
    pCurLayer->pNoSubMbPartSizeLessThan8x8Flag[iMbXy] = true;
    pCurLayer->pIntraPredMode[iMbXy][7] = (uiMbType - 1) & 3;
    pCurLayer->pCbp[iMbXy] = g_kuiI16CbpTable[ (uiMbType - 1) >> 2];
    uiCbpChroma = pCtx->pSps->uiChromaFormatIdc ? pCurLayer->pCbp[iMbXy] >> 4 : 0 ;
    uiCbpLuma = pCurLayer->pCbp[iMbXy] & 15;
    WelsFillCacheNonZeroCount (&sNeighAvail, pNonZeroCount, pCurLayer);
    WELS_READ_VERIFY (ParseIntra16x16Mode (pCtx, &sNeighAvail, pBsAux, pCurLayer));
  }

  ST32 (&pCurLayer->pNzc[iMbXy][0], 0);
  ST32 (&pCurLayer->pNzc[iMbXy][4], 0);
  ST32 (&pCurLayer->pNzc[iMbXy][8], 0);
  ST32 (&pCurLayer->pNzc[iMbXy][12], 0);
  ST32 (&pCurLayer->pNzc[iMbXy][16], 0);
  ST32 (&pCurLayer->pNzc[iMbXy][20], 0);
  pCurLayer->pCbfDc[iMbXy] = 0;

  if (pCurLayer->pCbp[iMbXy] == 0 && IS_INTRANxN (pCurLayer->pMbType[iMbXy])) {
    pCurLayer->pLumaQp[iMbXy] = pSlice->iLastMbQp;
    for (i = 0; i < 2; i++) {
      pCurLayer->pChromaQp[iMbXy][i] = g_kuiChromaQpTable[WELS_CLIP3 ((pCurLayer->pLumaQp[iMbXy] +
                                       pSliceHeader->pPps->iChromaQpIndexOffset[i]), 0, 51)];
    }
  }

  if (pCurLayer->pCbp[iMbXy] || MB_TYPE_INTRA16x16 == pCurLayer->pMbType[iMbXy]) {
    memset (pCurLayer->pScaledTCoeff[iMbXy], 0, 384 * sizeof (pCurLayer->pScaledTCoeff[iMbXy][0]));
    int32_t iQpDelta, iId8x8, iId4x4;
    WELS_READ_VERIFY (ParseDeltaQpCabac (pCtx, iQpDelta));
    if (iQpDelta > 25 || iQpDelta < -26) {//out of iQpDelta range
      return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_INFO_INVALID_QP);
    }
    pCurLayer->pLumaQp[iMbXy] = (pSlice->iLastMbQp + iQpDelta + 52) % 52; //update last_mb_qp
    pSlice->iLastMbQp = pCurLayer->pLumaQp[iMbXy];
    for (i = 0; i < 2; i++) {
      pCurLayer->pChromaQp[iMbXy][i] = g_kuiChromaQpTable[WELS_CLIP3 ((pSlice->iLastMbQp +
                                       pSliceHeader->pPps->iChromaQpIndexOffset[i]), 0, 51)];
    }
    if (MB_TYPE_INTRA16x16 == pCurLayer->pMbType[iMbXy]) {
      //step1: Luma DC
      WELS_READ_VERIFY (ParseResidualBlockCabac (&sNeighAvail, pNonZeroCount, pBsAux, 0, 16, g_kuiLumaDcZigzagScan,
                        I16_LUMA_DC, pCurLayer->pScaledTCoeff[iMbXy], pCurLayer->pLumaQp[iMbXy], pCtx));
      //step2: Luma AC
      if (uiCbpLuma) {
        for (i = 0; i < 16; i++) {
          WELS_READ_VERIFY (ParseResidualBlockCabac (&sNeighAvail, pNonZeroCount, pBsAux, i,
                            iScanIdxEnd - WELS_MAX (iScanIdxStart, 1) + 1, g_kuiZigzagScan + WELS_MAX (iScanIdxStart, 1), I16_LUMA_AC,
                            pCurLayer->pScaledTCoeff[iMbXy] + (i << 4), pCurLayer->pLumaQp[iMbXy], pCtx));
        }
        ST32 (&pCurLayer->pNzc[iMbXy][0], LD32 (&pNonZeroCount[1 + 8 * 1]));
        ST32 (&pCurLayer->pNzc[iMbXy][4], LD32 (&pNonZeroCount[1 + 8 * 2]));
        ST32 (&pCurLayer->pNzc[iMbXy][8], LD32 (&pNonZeroCount[1 + 8 * 3]));
        ST32 (&pCurLayer->pNzc[iMbXy][12], LD32 (&pNonZeroCount[1 + 8 * 4]));
      } else { //pNonZeroCount = 0
        ST32 (&pCurLayer->pNzc[iMbXy][0], 0);
        ST32 (&pCurLayer->pNzc[iMbXy][4], 0);
        ST32 (&pCurLayer->pNzc[iMbXy][8], 0);
        ST32 (&pCurLayer->pNzc[iMbXy][12], 0);
      }
    } else { //non-MB_TYPE_INTRA16x16
      if (pCurLayer->pTransformSize8x8Flag[iMbXy]) {
        // Transform 8x8 support for CABAC
        for (iId8x8 = 0; iId8x8 < 4; iId8x8++) {
          if (uiCbpLuma & (1 << iId8x8)) {
            WELS_READ_VERIFY (ParseResidualBlockCabac8x8 (&sNeighAvail, pNonZeroCount, pBsAux, (iId8x8 << 2),
                              iScanIdxEnd - iScanIdxStart + 1, g_kuiZigzagScan8x8 + iScanIdxStart, LUMA_DC_AC_INTRA_8,
                              pCurLayer->pScaledTCoeff[iMbXy] + (iId8x8 << 6), pCurLayer->pLumaQp[iMbXy], pCtx));
          } else {
            ST16 (&pNonZeroCount[g_kCacheNzcScanIdx[ (iId8x8 << 2)]], 0);
            ST16 (&pNonZeroCount[g_kCacheNzcScanIdx[ (iId8x8 << 2) + 2]], 0);
          }
        }
        ST32 (&pCurLayer->pNzc[iMbXy][0], LD32 (&pNonZeroCount[1 + 8 * 1]));
        ST32 (&pCurLayer->pNzc[iMbXy][4], LD32 (&pNonZeroCount[1 + 8 * 2]));
        ST32 (&pCurLayer->pNzc[iMbXy][8], LD32 (&pNonZeroCount[1 + 8 * 3]));
        ST32 (&pCurLayer->pNzc[iMbXy][12], LD32 (&pNonZeroCount[1 + 8 * 4]));
      } else {
