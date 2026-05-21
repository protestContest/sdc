#pragma once
#include "sdc.h"

void DCT2D(float *values, u32 stride);
void DCTBlock(SDCBlock *block);
void IDCTBlock(SDCBlock *block);
