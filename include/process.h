#pragma once
#include "opts.h"

bool IsFolder(char *path);
bool ProcessFile(char *inPath, char *outPath, Opts *opts);
bool ProcessFolder(char *inDir, Opts *opts);

