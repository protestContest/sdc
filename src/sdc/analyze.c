#include "sdc.h"
#include "sdc/tables.h"

#define bufRows 8

static float BlockThreshold(u32 bitDepth)
{
  switch (bitDepth) {
  case 8:  return    34.425;
  case 12: return   552.825;
  default: return   8847.22;
  }
}

static void AnalyzeRows(SDCContext *sdc)
{
  float visMid[4];
  float irMid[4];
  float visAvg, irAvg;
  float blockThreshold = BlockThreshold(sdc->bitDepth);
  u32 i, j;
  u32 sampleBytes = sdc->bitDepth > 8 ? 2 : 1;
  u32 numBlocks = sdc->imageWidth / 8; // discards right margin
  for (i = 0; i < numBlocks; i++) {
    bool blockClean = true;
    float blockIRSum = 0;
    u32 x, y;

    visMid[0] = 0;
    visMid[1] = 0;
    visMid[2] = 0;
    visMid[3] = 0;
    irMid[0] = 0;
    irMid[1] = 0;
    irMid[2] = 0;
    irMid[3] = 0;

    // per-pixel accumulation
    for (y = 0; y < 8; y++) {
      for (x = 0; x < 8; x++) {
        u32 rowX = 8*i + x;
        u32 quad = (y>>2)*2 + (x>>2); // quadrant number, i.e. index into mid-band array
        u16 vis, irVal;
        float logVis, logIR;
        float w;
        u8 *pixel = sdc->inBuf.data + sdc->inBuf.rowSize*y + 4*sampleBytes*rowX;

        if (sdc->bitDepth > 8) {
          vis = ((u16*)pixel)[0];
          irVal = ((u16*)pixel)[3];
        } else {
          vis = pixel[0];
          irVal = pixel[3];
        }

        if (irVal <= blockThreshold) {
          blockClean = false;
          continue;
        }

        logVis = Log(vis);
        logIR = Log(irVal);
        w = irVal;
        blockIRSum += irVal;
        sdc->meanVisNumer += w*w*logVis;
        sdc->meanIRNumer += w*w*logIR;
        sdc->meanDenom += w*w;

        visMid[quad] += logVis;
        irMid[quad] += logIR;
      }
    }

    if (!blockClean) continue;

    // laplacian pyramid: average each quadrant, diff the total average
    visMid[0] /= 16;
    visMid[1] /= 16;
    visMid[2] /= 16;
    visMid[3] /= 16;
    visAvg = (visMid[0] + visMid[1] + visMid[2] + visMid[3]) / 4;
    visMid[0] -= visAvg;
    visMid[1] -= visAvg;
    visMid[2] -= visAvg;
    visMid[3] -= visAvg;

    irMid[0] /= 16;
    irMid[1] /= 16;
    irMid[2] /= 16;
    irMid[3] /= 16;
    irAvg = (irMid[0] + irMid[1] + irMid[2] + irMid[3]) / 4;
    irMid[0] -= irAvg;
    irMid[1] -= irAvg;
    irMid[2] -= irAvg;
    irMid[3] -= irAvg;

    for (j = 0; j < 4; j++) {
      float visRes = visMid[j];
      float irRes = irMid[j];
      float ratio, factor, term;

      if (visRes == 0) continue;

      ratio = irRes / visRes;

      if (ratio < -0.125 || ratio > 0.425) continue;

      if (ratio >= 0 && ratio <= 0.3) {
        factor = visRes;
      } else {
        factor = visRes*halfSqrt2;
      }

      term = blockIRSum*blockIRSum * factor*factor;
      sdc->lsqRatioNumer += ratio * term;
      sdc->lsqRatioDenom += term;
    }
  }

  sdc->rowsAnalyzed += 8;
}

void SDCAnalyze(SDCContext *sdc, u8 *buf, u32 numRows)
{
  u32 totalAnalyzeRows = sdc->imageHeight & 0xFFFFFFF8; // height divisible by 8

  CopyInRows(&sdc->inBuf, &buf, &numRows, 8);
  while (RowBufFull(&sdc->inBuf, 8)) {
    AnalyzeRows(sdc);
    sdc->inBuf.count = 0;
    CopyInRows(&sdc->inBuf, &buf, &numRows, 8);
  }

  if (sdc->rowsAnalyzed == totalAnalyzeRows) {
    float meanVis = 0.0;
    float meanIR = 0.0;
    if (sdc->meanDenom != 0.0) meanVis = sdc->meanVisNumer / sdc->meanDenom;
    if (sdc->meanDenom != 0.0) meanIR = sdc->meanIRNumer / sdc->meanDenom;
    if (sdc->lsqRatioDenom) {
      float k;
      switch (sdc->bitDepth) {
          case 8:  k = 30.4f;   break;
          case 12: k = 325.0f;  break;
          default: k = 3901.0f;
      }

      sdc->leakRatio = sdc->lsqRatioNumer / sdc->lsqRatioDenom;
      sdc->leakBias = (meanIR - sdc->leakRatio * meanVis) / (1 - sdc->leakRatio);
      sdc->noiseRef = k / sdc->leakRatio;
    }
    sdc->inBuf.count = 0;
  }
}
