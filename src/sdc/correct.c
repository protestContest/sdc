#include "sdc/correct.h"
#include "sdc/tables.h"
#include "sdc/dct.h"
#include "sdc/inpaint.h"

static float Preprocess(SDCContext *sdc, SDCBlock *block, DefectMap *defects)
{
  u32 x, y;
  float sum = 0.0;

  for (y = 0; y < 10; y++) {
    for (x = 0; x < 10; x++) {
      float diff = block->values[rChan][y][x] - sdc->brightness90pct;
      float scaled = sdc->leakRatio * diff + sdc->pct30;
      float predicted = (float)(sdc->brightness90pct + (double)scaled);

      if (block->values[irChan][y][x] < predicted) {
        if (x > 0 && x < 9 && y > 0 && y < 9) {
          SetDefect(defects, x-1, y-1);
        }
        block->values[irChan][y][x] = predicted;
      }
    }
  }

  for (y = 1; y < 9; y++) {
    for (x = 1; x < 9; x++) {
      sum += block->values[rChan][y][x];
    }
  }

  return sdc->brightness90pct - 0.015625*sum;
}

static void CopyInOffset(float src[10][10], u32 ox, u32 oy, float dst[8][8])
{
  u32 x, y;
  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      dst[y][x] = src[y+oy][x+ox];
    }
  }
}

static void GenerateOffsetDCTs(float ir[10][10], float dcts[9][8][8])
{
  CopyInOffset(ir, 1, 1, dcts[0]); // center
  CopyInOffset(ir, 0, 1, dcts[1]); // left
  CopyInOffset(ir, 2, 1, dcts[2]); // right
  CopyInOffset(ir, 1, 0, dcts[3]); // up
  CopyInOffset(ir, 1, 2, dcts[4]); // down
  CopyInOffset(ir, 0, 0, dcts[5]); // top-left
  CopyInOffset(ir, 0, 2, dcts[6]); // bottom-left
  CopyInOffset(ir, 2, 0, dcts[7]); // top-right
  CopyInOffset(ir, 2, 2, dcts[8]); // bottom-right

  DCT2D((float*)dcts[0], 8);
  DCT2D((float*)dcts[1], 8);
  DCT2D((float*)dcts[2], 8);
  DCT2D((float*)dcts[3], 8);
  DCT2D((float*)dcts[4], 8);
  DCT2D((float*)dcts[5], 8);
  DCT2D((float*)dcts[6], 8);
  DCT2D((float*)dcts[7], 8);
  DCT2D((float*)dcts[8], 8);
}

static float CrossCorrelate(float a, float b, float weight) {
  float min = Min(Abs(a), Abs(b));
  bool sameSign = (a < 0.0f) == (b < 0.0f);
  return weight * (sameSign ? min : -min);
}

static float Correlate(float vis[10][10], float ir[8][8])
{
  u32 x, y;
  float sum = 0.0;
  static float weights[8][8] = {
    {0.0, 0.0, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0},
    {0.0, 0.0, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0},
    {0.5, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
    {1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0},
    {1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0},
    {1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0}
  };

  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      if (weights[y][x] != 0.0) {
        sum += CrossCorrelate(vis[y+1][x+1], ir[y][x], weights[y][x]);
      }
    }
  }
  return sum;
}

static float CorrelateDefectAndRed(float vis[10][10],
                                   float irDCTs[9][8][8], float weights[9])
{
  u32 i;
  float correlations[9];
  float sum, mean;

  sum = 0.0;
  for (i = 0; i < 9; i++) {
    correlations[i] = Correlate(vis, irDCTs[i]);
    sum += correlations[i];
  }
  mean = sum / 9.0;

  sum = 0.0;
  for (i = 0; i < 9; i++) {
    float deviation = correlations[i] - mean + 1.0;
    if (deviation < 0.0) deviation = 0.0;
    weights[i] = deviation*deviation;
    sum += weights[i];
  }

  return sum;
}

static void GenerateMaxAndMinRanges(
    float irDCTs[9][8][8], float envs[8][8][2],
    float offsetWeights[9], float weightSum)
{
  float invSum = 1.0 / weightSum;
  float sum;
  float mean;
  float variance;
  u32 x, y, i;

  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      sum = 0.0;
      for (i = 0; i < 9; i++) {
        sum += offsetWeights[i] * irDCTs[i][y][x];
      }
      mean = sum * invSum;

      if (x > 0 || y > 0) {
        sum = 0.0;
        for (i = 0; i < 9; i++) {
          float diff = Abs(irDCTs[i][y][x] - mean);
          sum += offsetWeights[i]*diff;
        }
        variance = sum * invSum * 1.5;
      } else {
        variance = 0.0;
      }
      envs[y][x][0] = mean - variance;
      envs[y][x][1] = mean + variance;
    }
  }
}

static void WidenMaxAndMinRanges(float envs[8][8][2],
                                 float outer[8][8], float inner[8][8])
{
  u32 x, y;

  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      float min, max;

      if (x == 0 && y == 0) continue;

      min = envs[y][x][0];
      max = envs[y][x][1];

      if (max < 0.0) {
        envs[y][x][0] = min * outer[y][x];
        envs[y][x][1] = max * inner[y][x];
      } else if (min > 0.0) {
        envs[y][x][0] = min * inner[y][x];
        envs[y][x][1] = max * outer[y][x];
      } else {
        envs[y][x][0] = min * outer[y][x];
        envs[y][x][1] = max * outer[y][x];
      }
    }
  }
}

static void NormalizeMaxAndMinRanges(float vis[10][10], float envs[8][8][2],
                                     float leakRatio, float leakBias)
{
  u32 x, y;
  float acScale = 1.5 * leakRatio;
  float invRatio = 1 / (1 - leakRatio);

  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      float visVal = vis[y+1][x+1];
      float min = envs[y][x][0];
      float max = envs[y][x][1];

      if (x == 0 && y == 0) {
        envs[y][x][0] = (min - leakRatio*visVal) * invRatio - 64*leakBias;
        envs[y][x][1] = envs[y][x][0];
      } else if (min <= 0.0 && max >= 0.0) {
        envs[y][x][0] = Min(0.0, (min - acScale*visVal)) * invRatio;
        envs[y][x][1] = Max(0.0, (max - acScale*visVal)) * invRatio;
      } else if (max < 0.0 && visVal < 0.0) {
        envs[y][x][0] = Min(0.0, min - acScale*visVal) * invRatio;
        envs[y][x][1] = Min(0.0, max - acScale*visVal) * invRatio;
      } else if (min > 0.0 && visVal > 0.0) {
        envs[y][x][0] = Max(0.0, min - acScale*visVal) * invRatio;
        envs[y][x][1] = Max(0.0, max - acScale*visVal) * invRatio;
      }
    }
  }
}
static float EnvFloor(float envs[8][8][2], u32 x, u32 y)
{
  float sum = 0.0;
  sum += Abs(envs[y  ][x  ][0]);
  sum += Abs(envs[y  ][x  ][1]);
  sum += Abs(envs[y  ][x+1][0]);
  sum += Abs(envs[y  ][x+1][1]);
  sum += Abs(envs[y+1][x  ][0]);
  sum += Abs(envs[y+1][x  ][1]);
  sum += Abs(envs[y+1][x+1][0]);
  sum += Abs(envs[y+1][x+1][1]);
  return 0.25 * 0.125 * sum;
}

static void ApplyFloor(float envs[8][8][2], u32 x, u32 y, float floor)
{
  float min = envs[y][x][0];
  float max = envs[y][x][1];
  float mid;
  if (max - min >= 2.0 * floor) return;
  mid = (min + max) / 2;
  envs[y][x][0] = mid - floor;
  envs[y][x][1] = mid + floor;
}

static void AdjustMaxAndMinToFloor(float envs[8][8][2])
{
  float floorA = EnvFloor(envs, 2, 0);
  float floorB = EnvFloor(envs, 0, 2);
  float floorC = EnvFloor(envs, 2, 2);
  u32 x, y;

  for (y = 0; y < 4; y++) {
    for (x = 4; x < 8; x++) {
      ApplyFloor(envs, x, y, floorA);
    }
  }

  for (y = 4; y < 8; y++) {
    for (x = 0; x < 4; x++) {
      ApplyFloor(envs, x, y, floorB);
    }
  }

  for (y = 4; y < 8; y++) {
    for (x = 4; x < 8; x++) {
      ApplyFloor(envs, x, y, floorC);
    }
  }
}

static void CalcDefectPercent(SDCContext *sdc, float ir[8][8])
{
  sdc->defectSum +=
      Abs(ir[0][2]) + Abs(ir[0][3]) + Abs(ir[1][2]) + Abs(ir[1][3]) +
      Abs(ir[2][0]) + Abs(ir[2][1]) + Abs(ir[3][0]) + Abs(ir[3][1]);
  sdc->blocksCorrected++;
}

static void Subtractor(SDCBlock *block, float envs[8][8][2], float blockNoise)
{
  u32 x, y, c;
  for (c = 0; c < 3; c++) {
    block->values[c][1][1] -= blockNoise*envs[0][0][1];

    for (y = 0; y < 8; y++) {
      float *row = (float*)block->values[c] + (y+1)*10 + 1;
      for (x = 0; x < 8; x++) {
        float min, max;
        if (x == 0 && y == 0) continue;

        min = envs[y][x][0];
        max = envs[y][x][1];
        if (row[x] > max) {
          row[x] -= max;
        } else if (row[x] >= min) {
          row[x] = 0;
        } else {
          row[x] -= min;
        }
      }
    }
  }
}

static void Sharpen(SDCBlock *block, u16 dpi)
{
  float *table = (float*)GetSharpenTable(dpi);
  u32 x, y, c;
  if (!table) return;
  for (c = 0; c < 3; c++) {
    float *chanTable = table + c*64;
    for (y = 0; y < 8; y++) {
      for (x = 0; x < 8; x++) {
        block->values[c][y+1][x+1] *= chanTable[y*8+x];
      }
    }
  }
}

void Correct(SDCContext *sdc, SDCBlock *block)
{
  DefectMap defects = {0};
  float envs[8][8][2];
  float irDCTs[9][8][8];
  float offsetWeights[9];
  float weightSum;
  float blockDarkness = Preprocess(sdc, block, &defects);
  float blockNoise = Min(0.8125 * (sdc->noiseRef - blockDarkness), 1.0);

  if (block->edgesDefective || blockNoise <= 0.0) {
    return;
  }

  if (defects.count > 0) {
    Inpaint(sdc, block, &defects);
  }

  DCTBlock(block);
  GenerateOffsetDCTs(block->values[irChan], irDCTs);
  weightSum = CorrelateDefectAndRed(block->values[rChan],
                                    irDCTs, offsetWeights);
  GenerateMaxAndMinRanges(irDCTs, envs, offsetWeights, weightSum);
  WidenMaxAndMinRanges(envs, sdc->gainOuter, sdc->gainInner);
  NormalizeMaxAndMinRanges(block->values[rChan], envs,
                           sdc->leakRatio, sdc->leakBias);
  AdjustMaxAndMinToFloor(envs);
  CalcDefectPercent(sdc, irDCTs[0]);
  Subtractor(block, envs, blockNoise);
  if (sdc->enableSharpen) {
    Sharpen(block, sdc->dpi);
  }

  IDCTBlock(block);
}
