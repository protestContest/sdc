#pragma once
#include "sdc.h"

typedef struct {
  u8 count;
  u8 values[8];
} DefectMap;

void SetDefect(DefectMap *map, u32 x, u32 y);
bool HasDefect(DefectMap *map, u32 x, u32 y);
void Inpaint(SDCContext *sdc, SDCBlock *block, DefectMap *defects);
