#pragma once
#include "sdc/rowbuf.h"

/* Input buffers must contain whole rows with interleaved channels: R, G, B, IR */
/* Output buffers contain whole rows of R, G, B */

#define rChan   0
#define gChan   1
#define bChan   2
#define irChan  3

#define sqrt2     1.41421354f
#define halfSqrt2 0.70710677f

enum {
  sdcOk = 0,
  sdcImageTooSmall = 1,
  sdcUnsupportedBitDepth = 2,
  sdcUnsupportedSharpenDPI = 3
};

typedef struct {
  u32 imageWidth;
  u32 imageHeight;
  u16 bitDepth;
  u16 dpi;
  bool enableSharpen;

  /* row processing */
  RowBuf inBuf;
  RowBuf blendBuf;
  bool offsetStrip;

  /* analyze state */
  u32 rowsAnalyzed;
  double lsqRatioNumer;
  double lsqRatioDenom;
  double meanVisNumer;
  double meanIRNumer;
  double meanDenom;
  double leakBias;
  double leakRatio;

  /* correct state */
  u32 rowsProcessed;
  double brightness90pct;
  double noiseRef;
  double irThreshold;
  double pct30;
  double defectSum;
  u16 defectPct;
  u32 blocksCorrected;
  float gainOuter[8][8];
  float gainInner[8][8];
} SDCContext;

typedef struct {
  float values[4][10][10];
  bool edgesDefective;
} SDCBlock;

/* Initialize a context before processing an image. */
i32 SDCInit(SDCContext *sdc, u32 imageWidth, u32 imageHeight,
            u16 bitDepth, u16 dpi, bool sharpen);

/* Destroys a context when processing is done */
void SDCComplete(SDCContext *sdc);

/* Analyze a buffer of rows, incorporating stats into a context */
void SDCAnalyze(SDCContext *sdc, u8 *buf, u32 numRows);

/* Process a buffer of rows. Returns the number of output rows, which can be
 * between zero and numRows + 16. outBuf must be big enough to hold the maximum
 * number of rows. */
u32 SDCProcess(SDCContext *sdc, u8 *inbuf, u32 numRows, u8 *outbuf);

/* Returns a metric of image defectiveness before cleaning */
u16 SDCDefectPercent(SDCContext *sdc);
