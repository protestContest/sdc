#pragma once

typedef const float SharpenTable[3][64];

SharpenTable *GetSharpenTable(u16 dpi);

void InitTables(u16 bitDepth);
float Log(u16 n);
u16 InvLog(double density);
