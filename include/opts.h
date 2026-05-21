#pragma once

typedef struct {
  char *inPath;
  bool sharpen;
  bool invert;
  bool backup;
} Opts;

void Usage(void);
bool ParseOpts(int argc, char *argv[], Opts *opts);
