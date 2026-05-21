#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiff.h"
#include "sdc.h"
#include <unistd.h>

typedef struct {
  char *inFilename;
  char *outFilename;
  bool sharpen;
  bool invert;
} Opts;
static Opts defaultOpts = {0, 0, false, false};

static void Usage(void)
{
  printf("Usage: sdcl [-s] image.tiff [output.tiff]\n");
}

#define cleanExt ".clean"
static char *OutFilename(char *inFilename)
{
  u32 i;
  u32 inLen = strlen(inFilename);
  u32 cleanExtLen = strlen(cleanExt);
  u32 inExtLen = 0;
  u32 dotPos = inLen;
  char *outFilename = malloc(inLen + cleanExtLen + 1);
  outFilename[inLen + cleanExtLen] = 0;

  if (inLen > 0) {
    for (i = 0; i < inLen - 1; i++) {
      if (inFilename[inLen - 1 - i] == '.') {
        dotPos = inLen - 1 - i;
        inExtLen = inLen - dotPos;
        break;
      }
    }
  }

  memcpy(outFilename, inFilename, dotPos);
  memcpy(outFilename+dotPos, cleanExt, cleanExtLen);
  memcpy(outFilename+dotPos+cleanExtLen, inFilename + dotPos, inExtLen);
  return outFilename;
}

static bool ParseOpts(int argc, char *argv[], Opts *opts)
{
  *opts = defaultOpts;
  int ch;

  while ((ch = getopt(argc, argv, "si")) >= 0) {
    switch (ch) {
    case 's':
      opts->sharpen = true;
      break;
    case 'i':
      opts->invert = true;
      break;
    default:
      Usage();
      return false;
    }
  }

  if (optind == argc) {
    Usage();
    return false;
  }

  opts->inFilename = argv[optind];
  if (optind + 1 < argc) {
    opts->outFilename = argv[optind+1];
  } else {
    opts->outFilename = OutFilename(opts->inFilename);
  }

  return true;
}

static char *ValidInputImage(TIFFImage *img)
{
  u32 depth;
  if (!TIFFHasTag(img, tagImageWidth)) return "Missing width tag";
  if (!TIFFHasTag(img, tagImageHeight)) return "Missing height tag";
  if (!TIFFHasTag(img, tagSamplesPerPixel)) return "Missing samples per pixel tag";
  if (!TIFFHasTag(img, tagBitsPerSample)) return "Missing bits per sample tag";
  if (!TIFFHasTag(img, tagPlanarConfig)) return "Missing planar configuration tag";

  if (TIFFTagValue(img, tagSamplesPerPixel, 0) != 4) return "Requires 4 channels: RGB + IR";
  if (TIFFTagCount(img, tagBitsPerSample) != 4) return "Wrong number of bits per sample count";
  depth = TIFFTagValue(img, tagBitsPerSample, 0);
  if (depth != 8 && depth != 12 && depth != 16) return "Requires bit depth of 8, 12, or 16";
  if (TIFFTagValue(img, tagBitsPerSample, 1) != depth ||
      TIFFTagValue(img, tagBitsPerSample, 2) != depth ||
      TIFFTagValue(img, tagBitsPerSample, 3) != depth) return "Channel bit depths must match";

  if (TIFFTagValue(img, tagPlanarConfig, 0) != 1) return "Only channel-interleaved images supported";

  return 0;
}

static TIFFFile *CreateOutFile(TIFFImage *inImg, char *outFilename)
{
  TIFFFile *outFile = CreateTIFFFile(outFilename);
  TIFFImage *outImg;
  u32 i;
  if (!outFile) return 0;

  outImg = NewTIFFImage(outFile);

  for (i = 0; i < inImg->numTags; i++) {
    TIFFTag *tag = &inImg->tags[i];

    // outfile will only be RGB
    if (tag->id == tagSamplesPerPixel) {
      AddTIFFTag(outImg, tagSamplesPerPixel, tiffShort, 1);
      SetTIFFTag(outImg, tagSamplesPerPixel, 0, 3);
      continue;
    }

    // outfile will only be RGB
    if (tag->id == tagBitsPerSample) {
      u32 depth = TIFFTagValue(inImg, tagBitsPerSample, 0);
      AddTIFFTag(outImg, tagBitsPerSample, tiffShort, 3);
      SetTIFFTag(outImg, tagBitsPerSample, 0, depth);
      SetTIFFTag(outImg, tagBitsPerSample, 1, depth);
      SetTIFFTag(outImg, tagBitsPerSample, 2, depth);
      continue;
    }

    // don't copy image data - will happen as output of SDC
    if (tag->id == tagStripOffsets ||
        tag->id == tagStripByteCounts ||
        tag->id == tagExtraSamples) {
      continue;
    }

    TIFFCopyTag(inImg, outImg, tag->id);
  }

  BufferImageData(outImg);

  return outFile;
}

static void InvertBuf(void *buf, u32 size, u32 depth)
{
  u32 i;
  if (depth == 8) {
    u8 *data = (u8*)buf;
    for (i = 0; i < size; i++) {
      data[i] = 255 - data[i];
    }
  } else {
    u16 *data = (u16*)buf;
    u32 maxVal = (((u32)1 << depth) - 1) << (16 - depth);
    for (i = 0; i < size/2; i++) {
      data[i] = maxVal - data[i];
    }
  }
}

static void CorrectFile(TIFFImage *inImg, TIFFImage *outImg, Opts *opts)
{
  SDCContext sdc;
  u32 largestStripSize = TIFFImageLargestStrip(inImg);
  u32 inRowSize = TIFFImageRowSize(inImg);
  u32 outRowSize = TIFFImageRowSize(outImg);
  u32 maxNumRows = largestStripSize / inRowSize;
  u32 inBufSize = largestStripSize;
  u32 outBufSize = (maxNumRows + 16)*outRowSize;
  u32 numStrips = TIFFImageNumStrips(inImg);
  u32 i;
  u8 *inBuf = malloc(inBufSize);
  u8 *outBuf = malloc(outBufSize);
  u32 depth = TIFFImageDepth(inImg);
  i32 err;

  // detect 12-bits formatted in 16-bit image
  if (depth > 8) {
    u32 maxVal = TIFFTagValue(inImg, tagMaxSampleValue, 0);
    if (maxVal == 0xFFF0) {
      depth = 12;
    }
  }

  err = SDCInit(&sdc,
          TIFFImageWidth(inImg),
          TIFFImageHeight(inImg),
          depth,
          TIFFTagValue(inImg, tagXResolution, 0),
          opts->sharpen);

  if (err) {
    printf("Error initializing SDC: %d\n", err);
    exit(1);
  }

  for (i = 0; i < numStrips; i++) {
    u32 size = ReadImageStrip(inImg, i, inBuf);
    u32 numRows = size / inRowSize;
    SDCAnalyze(&sdc, inBuf, numRows);
  }

  for (i = 0; i < numStrips; i++) {
    u32 size = ReadImageStrip(inImg, i, inBuf);
    u32 numRows = size / inRowSize;
    numRows = SDCProcess(&sdc, inBuf, numRows, outBuf);
    if (numRows) {
      if (opts->invert) {
        InvertBuf(outBuf, numRows*outRowSize, depth);
      }
      WriteImageData(outImg, outBuf, numRows*outRowSize);
    }
  }

  SDCComplete(&sdc);
  FlushImageData(outImg);

  free(inBuf);
  free(outBuf);
}

int main(int argc, char *argv[])
{
  TIFFFile *inFile, *outFile;
  char *error;
  Opts opts;

  if (!ParseOpts(argc, argv, &opts)) {
    return 1;
  }

  inFile = ReadTIFF(opts.inFilename);
  if (!inFile) {
    printf("Could not read %s\n", opts.inFilename);
    return 1;
  }

  error = ValidInputImage(inFile->firstImage);
  if (error) {
    printf("%s\n", error);
    return 1;
  }

  outFile = CreateOutFile(inFile->firstImage, opts.outFilename);
  if (!outFile) {
    printf("Could not create %s\n", opts.outFilename);
    return 1;
  }

  CorrectFile(inFile->firstImage, outFile->firstImage, &opts);

  WriteTIFFImages(outFile);

  FreeTIFFFile(inFile);
  FreeTIFFFile(outFile);
  return 0;
}
