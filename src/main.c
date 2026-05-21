#include "opts.h"
#include "process.h"

int main(int argc, char *argv[])
{
  Opts opts;

  if (!ParseOpts(argc, argv, &opts)) {
    return 1;
  }

  if (IsFolder(opts.inPath)) {
    if (!ProcessFolder(opts.inPath, &opts)) return 1;
  } else {
    if (!ProcessFile(opts.inPath, NULL, &opts)) return 1;
  }

  return 0;
}
