#pragma once
#include <stdio.h>

enum {
  tiffByte = 1,
  tiffAscii = 2,
  tiffShort = 3,
  tiffLong = 4,
  tiffRational = 5
};

enum {
  tagNewSubFileType = 254,
  tagSubFileType = 255,
  tagImageWidth = 256, /* required */
  tagImageHeight = 257, /* required */
  tagBitsPerSample = 258, /* required */
  tagCompression = 259, /* required */
  tagPhotometricInterpretation = 262, /* required */
  tagDocumentName = 269,
  tagImageDescription = 270,
  tagMake = 271,
  tagModel = 272,
  tagStripOffsets = 273, /* required */
  tagOrientation = 274,
  tagSamplesPerPixel = 277, /* required */
  tagRowsPerStrip = 278, /* required */
  tagStripByteCounts = 279, /* required */
  tagMinSampleValue = 280,
  tagMaxSampleValue = 281,
  tagXResolution = 282, /* required */
  tagYResolution = 283, /* required */
  tagPlanarConfig = 284, /* required */
  tagResolutionUnit = 296, /* required */
  tagSoftware = 305,
  tagDateTime = 306,
  tagArtist = 315,
  tagExtraSamples = 338
};

typedef struct {
  u32 numer;
  u32 denom;
} TIFFRational;

typedef struct {
  u16 id;
  u16 type;
  u32 count;
  void *values;
} TIFFTag;

typedef struct {
  u32 capacity;
  u32 count;
  void *data;
} TIFFBuf;

typedef struct TIFFImage {
  struct TIFFFile *file;
  u32 offset;
  u16 numTags;
  TIFFTag *tags;
  struct TIFFImage *nextImage;
  TIFFBuf outbuf;
} TIFFImage;

typedef struct TIFFFile {
  FILE *f;
  u32 size;
  TIFFImage *firstImage;
  bool bigEndian;
} TIFFFile;

char *TIFFTypeName(u32 type);
u32 TIFFTypeSize(u32 type);
char *TIFFTagName(u16 tagID);

TIFFFile *CreateTIFFFile(char *path);
TIFFFile *ReadTIFF(char *path);
void FreeTIFFFile(TIFFFile *file);

TIFFImage *NewTIFFImage(TIFFFile *file);
TIFFImage *GetTIFFImage(TIFFImage *tiff, u32 index);
void WriteTIFFImages(TIFFFile *file);

void AddTIFFTag(TIFFImage *img, u16 tagID, u16 type, u32 count);
bool TIFFHasTag(TIFFImage *img, u16 tagID);
u16 TIFFTagCount(TIFFImage *img, u16 tagID);
u32 TIFFTagDataSize(TIFFImage *img, u16 tagID);
void SetTIFFTagData(TIFFImage *img, u16 tagID, void *data);
u32 TIFFTagValue(TIFFImage *img, u16 tagID, u32 index);
TIFFRational TIFFTagRational(TIFFImage *img, u16 tagID, u32 index);
void SetTIFFTag(TIFFImage *img, u16 tagID, u32 index, u32 value);
void SetTIFFTagRational(TIFFImage *img, u16 tagID, u32 index, TIFFRational rat);
void TIFFCopyTag(TIFFImage *from, TIFFImage *to, u16 tagID);

#define TIFFImageWidth(img) TIFFTagValue(img, tagImageWidth, 0)
#define TIFFImageHeight(img) TIFFTagValue(img, tagImageHeight, 0)
#define TIFFImageDepth(img) TIFFTagValue(img, tagBitsPerSample, 0)
#define TIFFImageRowsPerStrip(img) TIFFTagValue(img, tagRowsPerStrip, 0)
u32 TIFFImageNumStrips(TIFFImage *img);

void BufferImageData(TIFFImage *img);
void WriteImageData(TIFFImage *img, void *data, u32 size);
void FlushImageData(TIFFImage *img);
u32 ReadImageStrip(TIFFImage *img, u32 index, void *dst);
#define TIFFStripSize(img, index)  TIFFTagValue(img, tagStripByteCounts, index)
u32 TIFFImageRowSize(TIFFImage *img);
u32 TIFFImageLargestStrip(TIFFImage *img);
