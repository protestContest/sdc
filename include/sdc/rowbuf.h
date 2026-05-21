#pragma once

typedef struct {
  u32 count;
  u32 rowSize;
  u32 pixelSize;
  u32 chanSize;
  u8 *data;
} RowBuf;

void InitRowBuf(RowBuf *buf,
                u32 bitsPerSample, u32 pixelSize, u32 rowSize, u32 cap);
void DestroyRowBuf(RowBuf *buf);
void CopyFirstRow(RowBuf *buf, u8 *src);
void RotateRows(RowBuf *buf, u32 numRows);
void DuplicateLastRow(RowBuf *buf, u32 maxRows);
void CopyInRows(RowBuf *buf, u8 **src, u32 *numRows, u32 maxRows);
u8 *GetPixel(RowBuf *buf, u32 x, u32 y);
u16 GetSample(RowBuf *buf, u32 x, u32 y, u32 channel);
void SetSample(RowBuf *buf, u32 x, u32 y, u32 channel, u16 value);

#define RowBufFull(buf, max)  ((buf)->count == (max))

