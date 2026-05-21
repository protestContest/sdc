#include "sdc.h"
#include "sdc/correct.h"
#include "sdc/tables.h"

static void CopyInBlock(SDCContext *sdc, u32 index, u32 numBlocks, SDCBlock *block)
{
  u32 x, y;
  u32 offset = 4*sdc->offsetStrip;
  i32 blockX = 8*index - offset; // x-coord in the image of the block left pixel
  u32 leftMargin, rightMargin;
  u32 edgeDefects[4] = {0};

  // fill left edge
  leftMargin = offset;
  if (index == 0) {
    // copy leftmost pixel
    for (y = 0; y < 10; y++) {
      for (x = 0; x < leftMargin+1; x++) {
        block->values[rChan][y][x] = Log(GetSample(&sdc->inBuf, 0, y, rChan));
        block->values[gChan][y][x] = Log(GetSample(&sdc->inBuf, 0, y, gChan));
        block->values[bChan][y][x] = Log(GetSample(&sdc->inBuf, 0, y, bChan));
        block->values[irChan][y][x] = Log(GetSample(&sdc->inBuf, 0, y, irChan));
      }
    }
  } else {
    // fetch from previous block
    for (y = 0; y < 10; y++) {
      block->values[rChan][y][0] = Log(GetSample(&sdc->inBuf, blockX-1, y, rChan));
      block->values[gChan][y][0] = Log(GetSample(&sdc->inBuf, blockX-1, y, gChan));
      block->values[bChan][y][0] = Log(GetSample(&sdc->inBuf, blockX-1, y, bChan));
      block->values[irChan][y][0] = Log(GetSample(&sdc->inBuf, blockX-1, y, irChan));
    }
  }

  // last block needs filling on the right
  if (index == numBlocks - 1) {
    rightMargin = (sdc->imageWidth+offset) % 8; // x pos in 8x8 block
    if (rightMargin == 0) rightMargin = 8;

    // fill right margin
    for (y = 0; y < 10; y++) {
      float rSample = Log(GetSample(&sdc->inBuf, blockX + rightMargin - 1, y, rChan));
      float gSample = Log(GetSample(&sdc->inBuf, blockX + rightMargin - 1, y, gChan));
      float bSample = Log(GetSample(&sdc->inBuf, blockX + rightMargin - 1, y, bChan));
      float irSample = Log(GetSample(&sdc->inBuf, blockX + rightMargin - 1, y, irChan));

      for (x = rightMargin+1; x < 10; x++) { // x tracks pos in 10x10 buffer
        block->values[rChan][y][x] = rSample;
        block->values[gChan][y][x] = gSample;
        block->values[bChan][y][x] = bSample;
        block->values[irChan][y][x] = irSample;
      }
    }

    // fill rest
    for (y = 0; y < 10; y++) {
      for (x = 0; x < rightMargin+1; x++) {
        block->values[rChan][y][x] = Log(GetSample(&sdc->inBuf, blockX + x - 1, y, rChan));
        block->values[gChan][y][x] = Log(GetSample(&sdc->inBuf, blockX + x - 1, y, gChan));
        block->values[bChan][y][x] = Log(GetSample(&sdc->inBuf, blockX + x - 1, y, bChan));
        block->values[irChan][y][x] = Log(GetSample(&sdc->inBuf, blockX + x - 1, y, irChan));
      }
    }
  } else {
    // normal fill
    for (y = 0; y < 10; y++) {
      u32 startX = (index == 0) ? leftMargin+1 : 0; // skip left margin for first block
      for (x = startX; x < 10; x++) {
        block->values[rChan][y][x] = Log(GetSample(&sdc->inBuf, blockX + x - 1, y, rChan));
        block->values[gChan][y][x] = Log(GetSample(&sdc->inBuf, blockX + x - 1, y, gChan));
        block->values[bChan][y][x] = Log(GetSample(&sdc->inBuf, blockX + x - 1, y, bChan));
        block->values[irChan][y][x] = Log(GetSample(&sdc->inBuf, blockX + x - 1, y, irChan));
      }
    }
  }

  for (x = 0; x < 10; x++) {
    if (block->values[irChan][0][x] < sdc->irThreshold) edgeDefects[0]++;
    if (block->values[irChan][9][x] < sdc->irThreshold) edgeDefects[1]++;
  }
  for (y = 0; y < 10; y++) {
    if (block->values[irChan][y][0] < sdc->irThreshold) edgeDefects[2]++;
    if (block->values[irChan][y][9] < sdc->irThreshold) edgeDefects[3]++;
  }
  if (edgeDefects[0] == 10 || edgeDefects[1] == 10 || edgeDefects[2] == 10 || edgeDefects[3] == 10) {
    block->edgesDefective = true;
  }
}

static void Blend(SDCContext *sdc, SDCBlock *block, u32 x, u32 y, u32 blockX, u8 *outbuf)
{
  static const u16 blendOld[4][8] = {
    {4, 3, 3, 2, 2, 3, 3, 4},
    {3, 3, 2, 1, 1, 2, 3, 3},
    {3, 2, 1, 0, 0, 1, 2, 3},
    {2, 1, 0, 0, 0, 0, 1, 2}
  };

  u32 c;
  u8 *pixel = outbuf + y*sdc->blendBuf.rowSize + (blockX+x)*sdc->blendBuf.pixelSize;

  for (c = 0; c < 3; c++) {
    u16 old = GetSample(&sdc->blendBuf, blockX + x, y, c);
    u16 new = InvLog(block->values[c][y+1][x+1]);
    u16 wOld = blendOld[y][x];
    u16 wNew = 4 - wOld;
    u16 sample = ((u32)new*wNew + (u32)old*wOld + 2) / 4;

    if (sdc->bitDepth > 8) {
      sample <<= 16 - sdc->bitDepth;
      ((u16*)pixel)[c] = sample;
    } else {
      pixel[c] = sample;
    }
  }
}

static void NoBlend(SDCContext *sdc, SDCBlock *block, u32 x, u32 y, u32 blockX, u8 *outbuf)
{
  u32 c;
  u8 *pixel = outbuf + y*sdc->blendBuf.rowSize + (blockX+x)*sdc->blendBuf.pixelSize;

  for (c = 0; c < 3; c++) {
    u16 sample = InvLog(block->values[c][y+1][x+1]);
    if (sdc->bitDepth > 8) {
      sample <<= 16 - sdc->bitDepth;
      ((u16*)pixel)[c] = sample;
    } else {
      pixel[c] = sample;
    }
  }
}

static void Store(SDCContext *sdc, SDCBlock *block, u32 x, u32 y, u32 blockX)
{
  u32 c;
  for (c = 0; c < 3; c++) {
    u16 new = InvLog(block->values[c][y+1][x+1]);
    SetSample(&sdc->blendBuf, blockX + x, y-4, c, new);
  }
}

static void OutputBlock(SDCContext *sdc, u32 strip, u32 index, u32 numBlocks, SDCBlock *block, u8 *outbuf)
{
  u32 x, y;
  u32 offset = 4*sdc->offsetStrip;
  i32 blockX = 8*index - offset; // x-coord in the image of the block left pixel
  u32 startX = (index == 0) ? offset : 0;
  u32 endX = 8;
  u32 stripY = strip*4;
  bool lastStrip = stripY + 8 >= sdc->imageHeight;

  if (index == numBlocks - 1) {
    endX = (sdc->imageWidth+offset) % 8;
    if (endX == 0) endX = 8;
  }

  if (strip == 0) {
    // no previous rows to blend
    for (y = 0; y < 4; y++) {
      for (x = startX; x < endX; x++) {
        NoBlend(sdc, block, x, y, blockX, outbuf);
      }
    }
  } else {
    // blend first 4 rows
    for (y = 0; y < 4; y++) {
      for (x = startX; x < endX; x++) {
        Blend(sdc, block, x, y, blockX, outbuf);
      }
    }
  }

  if (lastStrip) {
    // don't store last rows, just output
    u32 extraRows = stripY + 8 - sdc->imageHeight;
    u32 lastRows = 4 - extraRows;
    for (y = 4; y < 4+lastRows; y++) {
      for (x = startX; x < endX; x++) {
        NoBlend(sdc, block, x, y, blockX, outbuf);
      }
    }
  } else {
    // store last 4 rows
    for (y = 4; y < 8; y++) {
      for (x = startX; x < endX; x++) {
        Store(sdc, block, x, y, blockX);
      }
    }
  }
}

static u32 ProcessBlocks(SDCContext *sdc, u32 strip, u8 *outbuf, u32 numRows)
{
  u32 numBlocks = Align(sdc->imageWidth + 4*sdc->offsetStrip, 8);
  u32 i;

  for (i = 0; i < numBlocks; i++) {
    SDCBlock block = {0};
    CopyInBlock(sdc, i, numBlocks, &block);
    Correct(sdc, &block);
    OutputBlock(sdc, strip, i, numBlocks, &block, outbuf);
  }

  return 4;
}

u32 SDCProcess(SDCContext *sdc, u8 *inbuf, u32 numRows, u8 *outbuf)
{
  u32 numProcessed = 0;
  u32 leftover;
  u32 strip = sdc->rowsProcessed / 4;

  if (sdc->inBuf.count == 0 && sdc->rowsProcessed == 0) {
    CopyFirstRow(&sdc->inBuf, inbuf);
  }

  CopyInRows(&sdc->inBuf, &inbuf, &numRows, 10);
  while (RowBufFull(&sdc->inBuf, 10)) {
    ProcessBlocks(sdc, strip, outbuf, 8);
    numProcessed += 4;
    sdc->rowsProcessed += 4;
    strip++;
    outbuf += 4*sdc->blendBuf.rowSize;
    RotateRows(&sdc->inBuf, 4);
    sdc->offsetStrip = !sdc->offsetStrip;
    CopyInRows(&sdc->inBuf, &inbuf, &numRows, 10);
  }

  leftover = sdc->inBuf.count - 1;

  // last, partial row
  if (leftover && sdc->rowsProcessed + leftover == sdc->imageHeight) {
    DuplicateLastRow(&sdc->inBuf, 10);
    ProcessBlocks(sdc, strip, outbuf, 8);
    numProcessed += leftover;
    outbuf += leftover*sdc->blendBuf.rowSize;
  }

  return numProcessed;
}
