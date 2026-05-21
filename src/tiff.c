#include "tiff.h"
#include <stdlib.h>
#include <string.h>

#define TAG_SIZE 12

static void SwapBytes(u16 *buf, u32 size)
{
  u32 i;
  for (i = 0; i < size/2; i++) {
    buf[i] = ((buf[i] & 0xFF) << 8) | ((buf[i] & 0xFF00) >> 8);
  }
}

static u32 ParseLong(void *data, bool bigEndian)
{
  u8 *bytes = (u8*)data;
  if (bigEndian) {
    return
      ((u32)bytes[0] << 24) |
      ((u32)bytes[1] << 16) |
      ((u32)bytes[2] <<  8) |
      ((u32)bytes[3]);
  } else {
    return
      ((u32)bytes[3] << 24) |
      ((u32)bytes[2] << 16) |
      ((u32)bytes[1] <<  8) |
      ((u32)bytes[0]);
  }
}

static u16 ParseShort(void *data, bool bigEndian)
{
  u8 *bytes = (u8*)data;
  if (bigEndian) {
    return
      ((u16)bytes[0] <<  8) |
      ((u16)bytes[1]);
  } else {
    return
      ((u16)bytes[1] <<  8) |
      ((u16)bytes[0]);
  }
}

static void ReadBytes(TIFFFile *file, u32 *offset, u32 length, void *dst)
{
  fseek(file->f, *offset, SEEK_SET);
  fread(dst, length, 1, file->f);
  *offset += length;
}

static u16 ReadShort(TIFFFile *file, u32 *offset)
{
  u8 value[2];
  ReadBytes(file, offset, sizeof(value), &value);
  return ParseShort(value, file->bigEndian);
}

static u32 ReadLong(TIFFFile *file, u32 *offset)
{
  u8 value[4];
  ReadBytes(file, offset, sizeof(value), &value);
  return ParseLong(value, file->bigEndian);
}

static void WriteBytes(TIFFFile *file, u32 *offset, void *data, u32 length)
{
  fseek(file->f, *offset, SEEK_SET);
  fwrite(data, length, 1, file->f);
  *offset += length;
  if (*offset > file->size) file->size = *offset;
}

static void WriteLong(TIFFFile *file, u32 *offset, u32 num)
{
  u8 data[4];
  if (file->bigEndian) {
    data[0] = (num >> 24) & 0xFF;
    data[1] = (num >> 16) & 0xFF;
    data[2] = (num >>  8) & 0xFF;
    data[3] = num & 0xFF;
  } else {
    data[3] = (num >> 24) & 0xFF;
    data[2] = (num >> 16) & 0xFF;
    data[1] = (num >>  8) & 0xFF;
    data[0] = num & 0xFF;
  }
  WriteBytes(file, offset, &data, sizeof(data));
}

static void WriteShort(TIFFFile *file, u32 *offset, u16 num)
{
  u8 data[2];
  if (file->bigEndian) {
    data[0] = (num >>  8) & 0xFF;
    data[1] = num & 0xFF;
  } else {
    data[1] = (num >>  8) & 0xFF;
    data[0] = num & 0xFF;
  }
  WriteBytes(file, offset, &data, sizeof(data));
}

static void AppendBytes(TIFFFile *file, void *data, u32 length)
{
  fseek(file->f, 0, SEEK_END);
  fwrite(data, length, 1, file->f);
  file->size += length;
}

static void AppendShort(TIFFFile *file, u16 num)
{
  u8 data[2];
  if (file->bigEndian) {
    data[0] = (num >>  8) & 0xFF;
    data[1] = num & 0xFF;
  } else {
    data[1] = (num >>  8) & 0xFF;
    data[0] = num & 0xFF;
  }
  AppendBytes(file, data, sizeof(data));
}

static void AppendLong(TIFFFile *file, u32 num)
{
  u8 data[4];
  if (file->bigEndian) {
    data[0] = (num >> 24) & 0xFF;
    data[1] = (num >> 16) & 0xFF;
    data[2] = (num >>  8) & 0xFF;
    data[3] = num & 0xFF;
  } else {
    data[3] = (num >> 24) & 0xFF;
    data[2] = (num >> 16) & 0xFF;
    data[1] = (num >>  8) & 0xFF;
    data[0] = num & 0xFF;
  }
  AppendBytes(file, data, sizeof(data));
}

static u32 AppendZero(TIFFFile *file, u32 length)
{
  static u8 zeroes[4096] = {0};
  u32 offset = file->size;
  file->size += length;

  fseek(file->f, 0, SEEK_END);

  while (length > sizeof(zeroes)) {
    fwrite(zeroes, sizeof(zeroes), 1, file->f);
    length -= sizeof(zeroes);
  }
  if (length) {
    fwrite(zeroes, length, 1, file->f);
  }

  return offset;
}

char *TIFFTypeName(u32 type)
{
  switch (type) {
  case tiffByte: return "Byte";
  case tiffAscii: return "Ascii";
  case tiffShort: return "Short";
  case tiffLong: return "Long";
  case tiffRational: return "Rational";
  default: return "<Unknown>";
  }
}

u32 TIFFTypeSize(u32 type)
{
  switch (type) {
  case tiffByte:      return 1;
  case tiffAscii:     return 1;
  case tiffShort:     return 2;
  case tiffLong:      return 4;
  case tiffRational:  return 8;
  default:            return 0;
  }
}

char *TIFFTagName(u16 tagID)
{
  switch (tagID) {
  case tagNewSubFileType: return "NewSubFileType";
  case tagSubFileType:  return "SubFileType";
  case tagImageWidth: return "ImageWidth";
  case tagImageHeight:  return "ImageHeight";
  case tagBitsPerSample:  return "BitsPerSample";
  case tagCompression:  return "Compression";
  case tagPhotometricInterpretation:  return "PhotometricInterpretation";
  case tagDocumentName: return "DocumentName";
  case tagImageDescription: return "ImageDescription";
  case tagMake: return "Make";
  case tagModel:  return "Model";
  case tagStripOffsets: return "StripOffsets";
  case tagOrientation:  return "Orientation";
  case tagSamplesPerPixel:  return "SamplesPerPixel";
  case tagRowsPerStrip: return "RowsPerStrip";
  case tagStripByteCounts:  return "StripByteCounts";
  case tagMinSampleValue:  return "MinSampleValue";
  case tagMaxSampleValue: return "MaxSampleValue";
  case tagXResolution:  return "XResolution";
  case tagYResolution:  return "YResolution";
  case tagPlanarConfig: return "PlanarConfig";
  case tagResolutionUnit: return "ResolutionUnit";
  case tagSoftware: return "Software";
  case tagDateTime: return "DateTime";
  case tagArtist: return "Artist";
  case tagExtraSamples: return "ExtraSamples";
  default: return "<Unknown>";
  }
}

static u32 TagDataSize(TIFFTag *tag)
{
  return tag->count*TIFFTypeSize(tag->type);
}

static bool TagDataInline(TIFFTag *tag)
{
  return TagDataSize(tag) <= 4;
}

static TIFFFile *NewTIFFFile(void)
{
  TIFFFile *file = malloc(sizeof(TIFFFile));
  file->f = 0;
  file->size = 0 ;
  file->firstImage = 0;
  return file;
}

static void WriteTIFFHeader(TIFFFile *file)
{
  char beSig[4] = {0x4D, 0x4D, 0x00, 0x2A};
  char leSig[4] = {0x49, 0x49, 0x2A, 0x00};
  if (file->bigEndian) {
    AppendBytes(file, beSig, sizeof(beSig));
  } else {
    AppendBytes(file, leSig, sizeof(leSig));
  }
  AppendLong(file, 0);
}

TIFFFile *CreateTIFFFile(char *path)
{
  TIFFFile *file;
  FILE *f = fopen(path, "w+");
  if (!f) return 0;

  file = NewTIFFFile();
  file->f = f;
  file->bigEndian = true;

  WriteTIFFHeader(file);

  return file;
}

static TIFFFile *OpenTIFFFile(char *path)
{
  FILE *f;
  TIFFFile *file;

  f = fopen(path, "r");
  if (!f) return 0;

  file = NewTIFFFile();
  file->f = f;

  fseek(file->f, 0, SEEK_END);
  file->size = ftell(file->f);
  if ((i32)file->size < 0) {
    FreeTIFFFile(file);
    return 0;
  }
  fseek(file->f, 0, SEEK_SET);

  return file;
}

static u32 ParseTIFFHeader(TIFFFile *file)
{
  char beSig[4] = {0x4D, 0x4D, 0x00, 0x2A};
  char leSig[4] = {0x49, 0x49, 0x2A, 0x00};
  u8 data[8];
  u32 offset = 0;

  if (file->size < 8) return 0;

  ReadBytes(file, &offset, sizeof(data), data);

  if (memcmp(data, beSig, sizeof(beSig)) == 0) {
    file->bigEndian = true;
    offset = ParseLong(data + sizeof(beSig), file->bigEndian);
  } else if (memcmp(data, leSig, sizeof(leSig)) == 0) {
    file->bigEndian = false;
    offset = ParseLong(data + sizeof(leSig), file->bigEndian);
  } else {
    return 0;
  }

  if (offset >= file->size) return 0;
  return offset;
}

static TIFFTag *AppendTag(TIFFImage *img, u16 tagID, u16 type, u32 count)
{
  TIFFTag *tag;
  img->numTags++;
  img->tags = realloc(img->tags, img->numTags*sizeof(TIFFTag));
  tag = img->tags + img->numTags - 1;
  tag->id = tagID;
  tag->type = type;
  tag->count = count;
  tag->values = malloc(TagDataSize(tag));
  return tag;
}

static bool ParseTIFFTag(TIFFImage *img, u32 *offset)
{
  u32 dataSize, count;
  u16 tagID, type;
  TIFFTag *tag;

  tagID = ReadShort(img->file, offset);
  type = ReadShort(img->file, offset);
  count = ReadLong(img->file, offset);

  if (TIFFTypeSize(type) == 0) return false;

  tag = AppendTag(img, tagID, type, count);
  dataSize = TagDataSize(tag);

  if (TagDataInline(tag)) {
    u32 dataOffset = *offset;
    ReadBytes(img->file, &dataOffset, dataSize, tag->values);
    *offset += 4;
  } else {
    u32 dataOffset = ReadLong(img->file, offset);
    ReadBytes(img->file, &dataOffset, dataSize, tag->values);
  }

  if (tag->type == tiffShort) {
    u32 i;
    u16 *values = (u16*)tag->values;
    for (i = 0; i < tag->count; i++) {
      values[i] = ParseShort(values + i, img->file->bigEndian);
    }
  } else if (tag->type == tiffLong) {
    u32 i;
    u32 *values = (u32*)tag->values;
    for (i = 0; i < tag->count; i++) {
      values[i] = ParseLong(values + i, img->file->bigEndian);
    }
  } else if (tag->type == tiffRational) {
    u32 i;
    u32 *values = (u32*)tag->values;
    for (i = 0; i < tag->count*2; i++) {
      values[i] = ParseLong(values + i, img->file->bigEndian);
    }
  }

  return true;
}

static void FreeTIFFImages(TIFFImage *img)
{
  TIFFImage *next = img->nextImage;
  u32 i;
  for (i = 0; i < img->numTags; i++) {
    free(img->tags[i].values);
  }
  free(img->tags);
  if (img->outbuf.data) {
    free(img->outbuf.data);
  }
  free(img);
  if (next) FreeTIFFImages(next);
}

static int CompareTags(const void *t1, const void *t2)
{
  TIFFTag *tag1 = (TIFFTag*)t1;
  TIFFTag *tag2 = (TIFFTag*)t2;
  if (tag1->id < tag2->id) return -1;
  if (tag1->id > tag2->id) return 1;
  return 0;
}

static void SortTags(TIFFImage *img)
{
  qsort(img->tags, img->numTags, sizeof(TIFFTag), CompareTags);
}

static bool ParseTIFFImages(TIFFFile *file, u32 offset)
{
  u16 numTags;
  u32 i, imgOffset, nextOffset;
  TIFFImage *img;

  if (offset + 2 > file->size) return false;

  imgOffset = offset;
  numTags = ReadShort(file, &offset);
  if (offset + numTags*TAG_SIZE + sizeof(nextOffset) > file->size) return false;

  img = NewTIFFImage(file);
  img->offset = imgOffset;
  for (i = 0; i < numTags; i++) {
    if (!ParseTIFFTag(img, &offset)) return false;
  }

  SortTags(img);

  nextOffset = ReadLong(file, &offset);
  if (nextOffset == 0) return true;

  if (nextOffset >= file->size) return false;

  return ParseTIFFImages(file, nextOffset);
}

TIFFFile *ReadTIFF(char *filename)
{
  TIFFFile *file;
  u32 offset;

  file = OpenTIFFFile(filename);
  if (!file) return 0;

  offset = ParseTIFFHeader(file);
  if (offset == 0) {
    FreeTIFFFile(file);
    return 0;
  }

  if (!ParseTIFFImages(file, offset)) {
    FreeTIFFFile(file);
    return 0;
  }

  return file;
}

void FreeTIFFFile(TIFFFile *file)
{
  if (!file) return;
  if (file->f) fclose(file->f);
  if (file->firstImage) {
    FreeTIFFImages(file->firstImage);
  }
  free(file);
}

TIFFImage *NewTIFFImage(TIFFFile *file)
{
  TIFFImage *img = malloc(sizeof(TIFFImage));
  img->file = file;
  img->offset = 0;
  img->numTags = 0;
  img->tags = 0;
  img->nextImage = 0;
  img->outbuf.capacity = 0;
  img->outbuf.count = 0;
  img->outbuf.data = 0;

  if (file->firstImage == 0) {
    file->firstImage = img;
  } else {
    TIFFImage *lastImg = file->firstImage;
    while (lastImg->nextImage) lastImg = lastImg->nextImage;
    lastImg->nextImage = img;
  }

  return img;
}

TIFFImage *GetTIFFImage(TIFFImage *tiff, u32 index)
{
  while (tiff && index > 0) {
    tiff = tiff->nextImage;
    index--;
  }
  return tiff;
}

static void WriteIFD(TIFFImage *img)
{
  TIFFFile *file = img->file;
  u32 i, j;
  u32 tagDataOffset;
  u32 nextImageOffset;

  img->offset = file->size;
  nextImageOffset = img->offset + sizeof(img->numTags) + img->numTags*TAG_SIZE;
  tagDataOffset = nextImageOffset + sizeof(u32);

  AppendShort(file, img->numTags);
  for (i = 0; i < img->numTags; i++) {
    TIFFTag *tag = &img->tags[i];
    AppendShort(file, tag->id);
    AppendShort(file, tag->type);
    AppendLong(file, tag->count);
    if (TagDataInline(tag)) {
      u8 data[4] = {0};
      if (tag->type == tiffByte || tag->type == tiffAscii) {
        AppendBytes(file, tag->values, tag->count);
        AppendBytes(file, data, 4 - tag->count);
      } else if (tag->type == tiffShort) {
        if (tag->count > 0) {
          AppendShort(file, ((u16*)tag->values)[0]);
          if (tag->count > 1) {
            AppendShort(file, ((u16*)tag->values)[1]);
          } else {
            AppendBytes(file, data, 2);
          }
        } else {
          AppendBytes(file, data, 4);
        }
      } else if (tag->type == tiffLong) {
        AppendLong(file, ((u32*)tag->values)[0]);
      } else {
        AppendBytes(file, data, 4);
      }
    } else {
      AppendLong(file, tagDataOffset);
      tagDataOffset += TagDataSize(tag);
    }
  }

  // offset placeholder
  AppendLong(file, 0);

  for (i = 0; i < img->numTags; i++) {
    TIFFTag *tag = &img->tags[i];
    if (TagDataInline(tag)) continue;

    if (tag->type == tiffShort) {
      for (j = 0; j < tag->count; j++) {
        AppendShort(file, ((u16*)tag->values)[j]);
      }
    } else if (tag->type == tiffLong) {
      for (j = 0; j < tag->count; j++) {
        AppendLong(file, ((u32*)tag->values)[j]);
      }
    } else if (tag->type == tiffRational) {
      for (j = 0; j < tag->count; j++) {
        TIFFRational rat = ((TIFFRational*)tag->values)[j];
        AppendLong(file, rat.numer);
        AppendLong(file, rat.denom);
      }
    } else {
      AppendBytes(file, tag->values, TagDataSize(tag));
    }
  }

  if (img->nextImage) {
    WriteLong(file, &nextImageOffset, file->size);
  } else {
    WriteLong(file, &nextImageOffset, 0);
  }
}

void WriteTIFFImages(TIFFFile *file)
{
  TIFFImage *img;
  u32 offset;
  if (!file->firstImage) return;

  img = file->firstImage;
  offset = 4;
  WriteLong(file, &offset, file->size);
  WriteIFD(img);

  while (img->nextImage) {
    img = img->nextImage;
    WriteIFD(img);
  }
}

static TIFFTag *FindTag(TIFFImage *img, u16 tagID)
{
  u32 left = 0;
  u32 right = img->numTags;
  i32 index;
  u16 id;

  while (left < right) {
    index = left + (right-left)/2;
    id = img->tags[index].id;

    if (id == tagID) {
      return &img->tags[index];
    }
    if (tagID < id) {
      right = index;
    } else if (tagID > id) {
      left = index + 1;
    }
  }
  return 0;
}

void AddTIFFTag(TIFFImage *img, u16 tagID, u16 type, u32 count)
{
  TIFFTag *tag = FindTag(img, tagID);
  if (tag) return;
  AppendTag(img, tagID, type, count);
  SortTags(img);
}

bool TIFFHasTag(TIFFImage *img, u16 tagID)
{
  TIFFTag *tag = FindTag(img, tagID);
  return tag != 0;
}

u16 TIFFTagCount(TIFFImage *img, u16 tagID)
{
  TIFFTag *tag = FindTag(img, tagID);
  if (!tag) return 0;
  return tag->count;
}

u32 TIFFTagDataSize(TIFFImage *img, u16 tagID)
{
  TIFFTag *tag = FindTag(img, tagID);
  if (!tag) return 0;
  return TagDataSize(tag);
}

void SetTIFFTagData(TIFFImage *img, u16 tagID, void *data)
{
  TIFFTag *tag = FindTag(img, tagID);
  if (!tag) return;
  free(tag->values);
  tag->values = data;
}

u32 TIFFTagValue(TIFFImage *img, u16 tagID, u32 index)
{
  TIFFTag *tag = FindTag(img, tagID);
  if (!tag) return 0;
  switch (tag->type) {
  case tiffByte:
  case tiffAscii:
    return ((u8*)tag->values)[index];
  case tiffShort:
    return ((u16*)tag->values)[index];
  default:
    return ((u32*)tag->values)[index];
  }
}

TIFFRational TIFFTagRational(TIFFImage *img, u16 tagID, u32 index)
{
  TIFFTag *tag = FindTag(img, tagID);
  TIFFRational rat = {0};
  if (!tag) return rat;
  rat = ((TIFFRational*)tag->values)[index];
  return rat;
}

static void SetTagValue(TIFFTag *tag, u32 index, u32 value)
{
  if (index >= tag->count) {
    index = tag->count;
    tag->count++;
    tag->values = realloc(tag->values, TagDataSize(tag));
  }
  switch (tag->type) {
  case tiffByte:
  case tiffAscii:
    ((u8*)tag->values)[index] = (u8)value;
    break;
  case tiffShort:
    ((u16*)tag->values)[index] = (u16)value;
    break;
  default:
    ((u32*)tag->values)[index] = value;
  }
}

void SetTIFFTag(TIFFImage *img, u16 tagID, u32 index, u32 value)
{
  TIFFTag *tag = FindTag(img, tagID);
  if (!tag) return;
  SetTagValue(tag, index, value);
}

void SetTIFFTagRational(TIFFImage *img, u16 tagID, u32 index, TIFFRational rat)
{
  TIFFTag *tag = FindTag(img, tagID);
  if (!tag) return;
  if (index >= tag->count) {
    index = tag->count;
    tag->count++;
    tag->values = realloc(tag->values, TagDataSize(tag));
  }
  ((TIFFRational*)tag->values)[index] = rat;
}

void TIFFCopyTag(TIFFImage *from, TIFFImage *to, u16 tagID)
{
  TIFFTag *fromTag = FindTag(from, tagID);
  TIFFTag *toTag;
  u32 size;

  if (!fromTag) return;
  toTag = FindTag(to, tagID);
  if (toTag) return;

  toTag = AppendTag(to, tagID, fromTag->type, fromTag->count);

  size = TagDataSize(fromTag);
  toTag->values = realloc(toTag->values, size);
  BlockMove(fromTag->values, toTag->values, size);

  SortTags(to);
}

u32 TIFFImageNumStrips(TIFFImage *img)
{
  TIFFTag *tag = FindTag(img, tagStripOffsets);
  if (!tag) return 0;
  return tag->count;
}

static void WriteImageStrip(TIFFImage *img, void *data, u32 size)
{
  TIFFTag *offsets, *byteCounts;
  bool sort = false;

  offsets = FindTag(img, tagStripOffsets);
  if (offsets) {
    SetTagValue(offsets, offsets->count, img->file->size);
  } else {
    sort = true;
    offsets = AppendTag(img, tagStripOffsets, tiffLong, 1);
    SetTagValue(offsets, 0, img->file->size);
  }

  byteCounts = FindTag(img, tagStripByteCounts);
  if (byteCounts) {
    SetTagValue(byteCounts, byteCounts->count, size);
  } else {
    sort = true;
    byteCounts = AppendTag(img, tagStripByteCounts, tiffLong, 1);
    SetTagValue(byteCounts, 0, size);
  }

  if (sort) SortTags(img);

  if (TIFFTagValue(img, tagBitsPerSample, 0) > 8) {
    SwapBytes(data, size);
  }

  AppendBytes(img->file, data, size);
}

static void CopyDataIn(TIFFBuf *buf, void **data, u32 *size)
{
  u32 bytesLeft = buf->capacity - buf->count;
  u32 toCopy = Min(bytesLeft, *size);
  if (toCopy == 0) return;
  memcpy(buf->data + buf->count, *data, toCopy);
  *data += toCopy;
  *size -= toCopy;
  buf->count += toCopy;
}

#define BufFull(buf)  ((buf)->count == (buf)->capacity)

void BufferImageData(TIFFImage *img)
{
  u32 numRows = TIFFTagValue(img, tagRowsPerStrip, 0);
  u32 width = TIFFTagValue(img, tagImageWidth, 0);
  u32 numChannels = TIFFTagValue(img, tagSamplesPerPixel, 0);
  u32 sampleSize = TIFFTagValue(img, tagBitsPerSample, 0) > 8 ? 2 : 1;
  u32 bufSize = numRows*width*numChannels*sampleSize;
  if (!bufSize) return;

  img->outbuf.capacity = bufSize;
  img->outbuf.count = 0;
  img->outbuf.data = malloc(bufSize);
}

void WriteImageData(TIFFImage *img, void *data, u32 size)
{
  if (img->outbuf.data == 0) {
    WriteImageStrip(img, data, size);
    return;
  }

  CopyDataIn(&img->outbuf, &data, &size);
  while (BufFull(&img->outbuf)) {
    WriteImageStrip(img, img->outbuf.data, img->outbuf.count);
    img->outbuf.count = 0;
    CopyDataIn(&img->outbuf, &data, &size);
  }
}

void FlushImageData(TIFFImage *img)
{
  if (img->outbuf.count == 0) return;
  WriteImageStrip(img, img->outbuf.data, img->outbuf.count);
}

u32 ReadImageStrip(TIFFImage *img, u32 index, void *dst)
{
  u32 offset = TIFFTagValue(img, tagStripOffsets, index);
  u32 size = TIFFTagValue(img, tagStripByteCounts, index);
  if (!offset) return 0;
  if (offset >= img->file->size) return 0;
  if (size == 0) return 0;

  ReadBytes(img->file, &offset, size, dst);

  if (TIFFTagValue(img, tagBitsPerSample, 0) > 8) {
    SwapBytes(dst, size);
  }

  return size;
}

u32 TIFFImageRowSize(TIFFImage *img)
{
  u32 numChannels = TIFFTagValue(img, tagSamplesPerPixel, 0);
  u32 channelSize = TIFFTagValue(img, tagBitsPerSample, 0) > 8 ? 2 : 1;
  return channelSize * numChannels * TIFFImageWidth(img);
}

u32 TIFFImageLargestStrip(TIFFImage *img)
{
  u32 i;
  TIFFTag *tag = FindTag(img, tagStripByteCounts);
  u16 *counts;
  u32 max = 0;
  if (!tag) return 0;

  counts = (u16*)tag->values;
  for (i = 0; i < tag->count; i++) {
    if (counts[i] > max) max = counts[i];
  }

  return max;
}
