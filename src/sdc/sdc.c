#include "sdc.h"
#include "sdc/tables.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SampleBytes(bitDepth) (((bitDepth) > 8) + 1)

/* Envelope-widening factors, positive and negative */
#define baseA 1.1
#define maxA  2.0
#define baseB 0.8
#define maxB  0.3

i32 SDCInit(SDCContext *sdc, u32 imageWidth, u32 imageHeight,
            u16 bitDepth, u16 dpi, bool sharpen)
{
  u32 inPixelSize = SampleBytes(bitDepth)*4;
  u32 outPixelSize = SampleBytes(bitDepth)*3;
  u32 inRowSize = imageWidth*inPixelSize;
  u32 outRowSize = imageWidth*outPixelSize;
  u32 x, y;

  if (imageWidth < 10 || imageHeight < 10) return sdcImageTooSmall;
  if (bitDepth != 8 && bitDepth != 12 && bitDepth != 16) return sdcUnsupportedBitDepth;
  if (sharpen && !GetSharpenTable(dpi)) return sdcUnsupportedSharpenDPI;

  InitTables(bitDepth);

  sdc->imageWidth = imageWidth;
  sdc->imageHeight = imageHeight;
  sdc->bitDepth = bitDepth;
  sdc->dpi = dpi;
  sdc->enableSharpen = sharpen;
  sdc->offsetStrip = false;

  InitRowBuf(&sdc->inBuf, bitDepth, inPixelSize, inRowSize, 10);
  InitRowBuf(&sdc->blendBuf, bitDepth, outPixelSize, outRowSize, 4);

  sdc->rowsAnalyzed = 0;
  sdc->lsqRatioNumer = 0.0;
  sdc->lsqRatioDenom = 0.0;
  sdc->meanVisNumer = 0.0;
  sdc->meanIRNumer = 0.0;
  sdc->meanDenom = 0.0;
  sdc->leakBias = 0.0;
  sdc->leakRatio = 0.0;
  sdc->pct30 = Log(30) - Log(100);

  sdc->rowsProcessed = 0;
  sdc->brightness90pct = Log((u32)(0.9*(float)((1 << bitDepth)-1) + 0.5));
  sdc->irThreshold = Log(15) - Log(100) + sdc->brightness90pct;
  sdc->noiseRef = 0.0;

  sdc->defectSum = 0.0;
  sdc->blocksCorrected = 0;
  sdc->defectPct = 0;

  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      float dx, dy;

      dx = ((float)x / 7.0) * (maxA - baseA);
      dy = ((float)y / 7.0) * (maxA - baseA);
      sdc->gainOuter[y][x] = baseA + sqrt(dx*dx + dy*dy);

      dx = ((float)x / 7.0) * (maxB - baseB);
      dy = ((float)y / 7.0) * (maxB - baseB);
      sdc->gainInner[y][x] = Max(0.0, baseB - sqrt(dx*dx + dy*dy));
    }
  }

  return sdcOk;
}

void SDCComplete(SDCContext *sdc)
{
  if (sdc->inBuf.data) free(sdc->inBuf.data);
  sdc->inBuf.data = 0;
  if (sdc->blendBuf.data) free(sdc->blendBuf.data);
  sdc->blendBuf.data = 0;

  if (sdc->blocksCorrected > 0) {
    double val;
    val = sdc->defectSum / (double)(sdc->blocksCorrected * 64);
    val = (val - 20.0) / 350.0 * 100.0;
    val = Max(0.0, Min(100.0, val));
    sdc->defectPct = (u16)(val + 0.5);
  }
}

u16 SDCDefectPercent(SDCContext *sdc)
{
  return sdc->defectPct;
}
