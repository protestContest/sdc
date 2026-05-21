#include "process.h"
#include "tiff.h"
#include "sdc.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>

bool IsFolder(char *path)
{
  struct stat st;
  if (stat(path, &st) != 0) return false;
  return S_ISDIR(st.st_mode);
}

#define cleanExt ".clean"
static char *OutFilename(char *inFilename)
{
  u32 i;
  u32 inLen = strlen(inFilename);
  u32 cleanExtLen = strlen(cleanExt);
  u32 inExtLen = 0;
  u32 dotPos = inLen;
  char *outFilename;

  outFilename = malloc(inLen + cleanExtLen + 1);
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

#define lutSize 4096
#define lutMax (lutSize - 1)

/* Curves apply to 12-bit readings held in 16-bit samples. The low four bits
 * carry the fraction left by averaging several scanner samples. */
static bool HasLUT(TIFFImage *img)
{
  if (TIFFTagValue(img, tagBitsPerSample, 0) != 16) return false;
  return TIFFTagDataSize(img, tagChannelLUT) == 3*lutSize*sizeof(u16);
}

static TIFFFile *CreateOutFile(TIFFImage *inImg, char *outFilename, bool applyLUT)
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

    // An applied LUT shouldn't carry over. It rescales the samples to the full
    // range, so the old maximum doesn't hold either.
    if (applyLUT && HasLUT(inImg) &&
        (tag->id == tagChannelLUT || tag->id == tagMaxSampleValue)) {
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

/* the sRGB encoding, which is what readers assume of an untagged image */
static double EncodeGamma(double v)
{
  if (v <= 0.0031308) return 12.92*v;
  return 1.055*pow(v, 1.0/2.4) - 0.055;
}

static u32 RunEnd(u16 *curve, u32 start)
{
  u32 end = start;
  while (end < lutMax && curve[end + 1] == curve[start]) end++;
  return end;
}

/* The scanner's curves map film readings to linear light, in whole counts.
 * Where a curve is nearly flat, a run of readings shares one count, and the
 * gamma encoding would push those runs far apart. So each run is taken as one
 * sample of the smooth curve, at the run's center, and the curve is rebuilt by
 * interpolating between the samples before it's encoded.
 *
 * A run at either end of a curve is a clip, not a rounded slope: the first is
 * the black point. Those stay flat. */
static void EncodeCurve(u16 *curve)
{
  static double smooth[lutSize];
  double x0, y0, x1, y1;
  u32 start, end, i;

  end = RunEnd(curve, 0);
  for (i = 0; i <= end; i++) smooth[i] = curve[0];
  x0 = end;
  y0 = curve[0];

  while (end < lutMax) {
    start = end + 1;
    end = RunEnd(curve, start);
    x1 = (end == lutMax) ? start : (start + end)/2.0;
    y1 = curve[start];
    for (i = (u32)x0 + 1; i <= (u32)x1; i++) {
      smooth[i] = y0 + (y1 - y0)*(i - x0)/(x1 - x0);
    }
    x0 = x1;
    y0 = y1;
  }
  for (i = (u32)x0 + 1; i < lutSize; i++) smooth[i] = y0;

  for (i = 0; i < lutSize; i++) {
    curve[i] = (u16)(EncodeGamma(smooth[i]/lutMax)*65535.0 + 0.5);
  }
}

/* Returns the image's curves with the gamma encoding folded in, so a single
 * lookup takes a film reading to a viewable value. Entries span all 16 bits. */
static u16 *ImageLUT(TIFFImage *img)
{
  u16 *lut;
  u32 i;
  if (!HasLUT(img)) return 0;
  lut = (u16*)TIFFTagData(img, tagChannelLUT);
  for (i = 0; i < 3; i++) EncodeCurve(lut + i*lutSize);
  return lut;
}

/* buf holds whole rows of RGB. The top 12 bits of a sample pick the entry, and
 * the low four interpolate toward the next. */
static void ApplyLUT(u16 *lut, void *buf, u32 size)
{
  u16 *data = (u16*)buf;
  u32 i;
  for (i = 0; i < size/2; i++) {
    u16 *curve = lut + (i % 3)*lutSize;
    u32 index = data[i] >> 4;
    u32 frac = data[i] & 15;
    u32 next = index < lutMax ? index + 1 : index;
    data[i] = (curve[index]*(16 - frac) + curve[next]*frac + 8) >> 4;
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
  u16 *lut;
  i32 err;

  // the LUT inverts, so it's only applied when inverting
  lut = opts->invert ? ImageLUT(inImg) : 0;

  err = SDCInit(&sdc,
          TIFFImageWidth(inImg),
          TIFFImageHeight(inImg),
          depth,
          TIFFTagValue(inImg, tagXResolution, 0),
          opts->sharpen);

  if (err) {
    fprintf(stderr, "Error initializing SDC: %d\n", err);
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
      if (lut) {
        ApplyLUT(lut, outBuf, numRows*outRowSize);
      } else if (opts->invert) {
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

static char *JoinPath(char *dir, char *name)
{
  char *out = malloc(strlen(dir) + 1 + strlen(name) + 1);
  strcpy(out, dir);
  strcat(out, "/");
  strcat(out, name);
  return out;
}

static bool HasSuffix(char *name, char *suffix)
{
  u32 len = strlen(name);
  u32 suffixLen = strlen(suffix);
  return len > suffixLen && strcmp(name + len - suffixLen, suffix) == 0;
}

/* leaves out the outputs of an earlier backup run */
static bool IsInputName(char *name)
{
  return HasSuffix(name, ".tiff") && !HasSuffix(name, cleanExt ".tiff");
}

bool ProcessFile(char *inPath, char *outPath, Opts *opts)
{
  TIFFFile *inFile, *outFile;
  char *error;
  char *ownPath = 0;
  bool replace = (!outPath) && (!opts->backup);
  bool ok;

  inFile = ReadTIFF(inPath);
  if (!inFile) {
    fprintf(stderr, "Could not read %s\n", inPath);
    return false;
  }

  error = ValidInputImage(inFile->firstImage);
  if (error) {
    fprintf(stderr, "%s\n", error);
    FreeTIFFFile(inFile);
    return false;
  }

  if (!outPath) {
    ownPath = OutFilename(inPath);
    outPath = ownPath;
  }

  outFile = CreateOutFile(inFile->firstImage, outPath, opts->invert);
  if (!outFile) {
    fprintf(stderr, "Could not create %s\n", outPath);
    FreeTIFFFile(inFile);
    free(ownPath);
    return false;
  }

  CorrectFile(inFile->firstImage, outFile->firstImage, opts);

  WriteTIFFImages(outFile);
  ok = CloseTIFFFile(outFile);

  FreeTIFFFile(inFile);
  FreeTIFFFile(outFile);

  // a short write (full disk) must never stand in for the original
  if (!ok) {
    fprintf(stderr, "Could not write %s\n", outPath);
    remove(outPath);
  } else if (replace && rename(outPath, inPath) != 0) {
    fprintf(stderr, "Could not replace %s\n", inPath);
    ok = false;
  }

  free(ownPath);
  return ok;
}

/* Same as running on each file in the folder by itself. The names are gathered
 * up front, since processing adds and replaces files in the folder. */
bool ProcessFolder(char *inDir, Opts *opts)
{
  DIR *d;
  struct dirent *ent;
  char **paths = 0;
  u32 numPaths = 0;
  u32 i;
  bool ok = true;

  d = opendir(inDir);
  if (!d) {
    fprintf(stderr, "Could not open %s\n", inDir);
    return false;
  }

  while ((ent = readdir(d)) != NULL) {
    if (!IsInputName(ent->d_name)) continue;
    paths = realloc(paths, (numPaths + 1)*sizeof(char*));
    paths[numPaths++] = JoinPath(inDir, ent->d_name);
  }
  closedir(d);

  for (i = 0; i < numPaths; i++) {
    fprintf(stderr, "Processing %s\n", paths[i]);
    if (!ProcessFile(paths[i], NULL, opts)) ok = false;
    free(paths[i]);
  }

  free(paths);
  return ok;
}
