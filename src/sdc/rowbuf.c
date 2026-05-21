#include "sdc/rowbuf.h"
#include <stdlib.h>

void InitRowBuf(RowBuf *buf,
                u32 bitsPerSample, u32 pixelSize, u32 rowSize, u32 cap)
{
  buf->count = 0;
  buf->rowSize = rowSize;
  buf->pixelSize = pixelSize;
  buf->chanSize = bitsPerSample;
  buf->data = malloc(rowSize*cap);
}

void DestroyRowBuf(RowBuf *buf)
{
  if (buf->data) free(buf->data);
  buf->data = 0;
}

static void ShiftRowIn(u8 *data, u16 depth, u32 rowSize)
{
  u32 i;
  if (depth > 8) {
    u32 shift = 16 - depth;
    u16 *words = (u16*)data;
    for (i = 0; i < rowSize/2; i++) {
      words[i] >>= shift;
    }
  }
}

void CopyFirstRow(RowBuf *buf, u8 *src)
{
  BlockMove(src, buf->data, buf->rowSize);
  ShiftRowIn(buf->data, buf->chanSize, buf->rowSize);
  buf->count = 1;
}

void RotateRows(RowBuf *buf, u32 numRows)
{
  u32 rowsLeft, size;
  u8 *src;
  numRows = Min(numRows, buf->count);
  rowsLeft = buf->count - numRows;
  src = buf->data + numRows*buf->rowSize;
  size = rowsLeft*buf->rowSize;
  BlockMove(src, buf->data, size);
  buf->count -= numRows;
}

void DuplicateLastRow(RowBuf *buf, u32 maxRows)
{
  u32 i;
  u8 *src = buf->data + (buf->count-1)*buf->rowSize;
  u8 *dst = buf->data + buf->count*buf->rowSize;
  for (i = buf->count; i < maxRows; i++) {
    BlockMove(src, dst, buf->rowSize);
    ShiftRowIn(dst, buf->chanSize, buf->rowSize);
    dst += buf->rowSize;
  }
  buf->count = maxRows;
}

void CopyInRows(RowBuf *buf, u8 **src, u32 *numRows, u32 maxRows)
{
  u32 rowsLeft, copyNum, copySize;
  u8 *dst;

  if (*numRows == 0) return;
  if (buf->count == maxRows) buf->count = 0;

  rowsLeft = maxRows - buf->count;
  copyNum = Min(*numRows, rowsLeft);
  copySize = copyNum * buf->rowSize;
  dst = buf->data + buf->count*buf->rowSize;
  BlockMove(*src, dst, copySize);
  ShiftRowIn(dst, buf->chanSize, copySize);
  buf->count += copyNum;
  *src += copySize;
  *numRows -= copyNum;
}

u8 *GetPixel(RowBuf *buf, u32 x, u32 y)
{
  return buf->data + buf->rowSize*y + buf->pixelSize*x;
}

u16 GetSample(RowBuf *buf, u32 x, u32 y, u32 channel)
{
  if (buf->chanSize <= 8) {
    u8 *pixel = GetPixel(buf, x, y);
    return pixel[channel];
  } else {
    u16 *pixel = (u16*)GetPixel(buf, x, y);
    return pixel[channel];
  }
}

void SetSample(RowBuf *buf, u32 x, u32 y, u32 channel, u16 value)
{
  if (buf->chanSize <= 8) {
    u8 *pixel = GetPixel(buf, x, y);
    pixel[channel] = value;
  } else {
    u16 *pixel = (u16*)GetPixel(buf, x, y);
    pixel[channel] = value;
  }
}
