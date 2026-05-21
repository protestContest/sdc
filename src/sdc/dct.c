#include "sdc/dct.h"

#define K1  1.30656302f
#define K2  0.54119610f
#define K3  0.38268343f

static void DCT1D(float *values, u32 stride)
{
  float s0, s1, s2, s3, d0, d1, d2, d3, es0, es1, ed0, ed1, z;
  float u, A, B, t01, t23, R, q;

  s0 = values[0*stride] + values[7*stride];
  s1 = values[1*stride] + values[6*stride];
  s2 = values[2*stride] + values[5*stride];
  s3 = values[3*stride] + values[4*stride];
  d0 = values[0*stride] - values[7*stride];
  d1 = values[1*stride] - values[6*stride];
  d2 = values[2*stride] - values[5*stride];
  d3 = values[3*stride] - values[4*stride];
  es0 = s0 + s3;
  es1 = s1 + s2;
  ed0 = s0 - s3;
  ed1 = s1 - s2;
  z = halfSqrt2 * (ed0 + ed1);

  values[0*stride] = es0 + es1;
  values[2*stride] = ed0 + z;
  values[4*stride] = es0 - es1;
  values[6*stride] = ed0 - z;

  u = halfSqrt2 * (d1 + d2);
  A = d0 + u;
  B = d0 - u;
  t01 = d0 + d1;
  t23 = d2 + d3;
  z = K3 * (t23 - t01);
  R = K1 * t01 + z;
  q = K2 * t23 + z;

  values[1*stride] = A + R;
  values[7*stride] = A - R;
  values[5*stride] = B + q;
  values[3*stride] = B - q;
}

void DCT2D(float *values, u32 stride)
{
  u32 x, y;
  for (y = 0; y < 8; y++) {
    DCT1D(values + y*stride, 1);
  }
  for (x = 0; x < 8; x++) {
    DCT1D(values + x, stride);
  }
}

void DCTBlock(SDCBlock *block)
{
  u32 c;
  for (c = 0; c < 3; c++) {
    float *values = (float*)block->values[c];
    DCT2D(values + 11, 10);
  }
}

#define L1   -2.61312604f
#define L2    1.08239222f
#define L3    1.84775906f
#define IDCT_SCALE 0.015625f

static void IDCT1D(float *values, u32 stride, float scale)
{
  float x[8];
  float esSum, esDiff, edSum, edDiffRot;
  float evInHi, evInLo, evOutHi, evOutLo;
  float s17, d17, s35, d35;
  float shared, oddSum, t1, t2, t3;

  x[0] = values[0*stride];
  x[1] = values[1*stride];
  x[2] = values[2*stride];
  x[3] = values[3*stride];
  x[4] = values[4*stride];
  x[5] = values[5*stride];
  x[6] = values[6*stride];
  x[7] = values[7*stride];

  esSum = x[0] + x[4];
  esDiff = x[0] - x[4];
  edSum = x[2] + x[6];
  edDiffRot = sqrt2 * (x[2] - x[6]) - edSum;

  evInLo = esSum + edSum;
  evInHi = esSum - edSum;
  evOutLo = esDiff + edDiffRot;
  evOutHi = esDiff - edDiffRot;

  s17 = x[1] + x[7];
  d17 = x[1] - x[7];
  s35 = x[5] + x[3];
  d35 = x[5] - x[3];

  shared = L3 * (d35 + d17);
  oddSum = s17 + s35;

  t1 = L1 * d35 + shared - oddSum;
  t2 = sqrt2 * (s17 - s35) - t1;
  t3 = L2 * d17 - shared + t2;

  values[0*stride] = scale * (evInLo + oddSum);
  values[7*stride] = scale * (evInLo - oddSum);
  values[1*stride] = scale * (evOutLo + t1);
  values[6*stride] = scale * (evOutLo - t1);
  values[2*stride] = scale * (evOutHi + t2);
  values[5*stride] = scale * (evOutHi - t2);
  values[4*stride] = scale * (evInHi + t3);
  values[3*stride] = scale * (evInHi - t3);
}

void IDCTBlock(SDCBlock *block)
{
  u32 c, x, y;

  for (c = 0; c < 3; c++) {
    float *values = (float*)block->values[c];
    for (x = 1; x < 9; x++) {
      IDCT1D(values + 10 + x, 10, 1.0);
    }
    for (y = 1; y < 9; y++) {
      IDCT1D(values + y*10 + 1, 1, IDCT_SCALE);
    }
  }
}
