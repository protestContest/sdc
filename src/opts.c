#include "opts.h"
#include <unistd.h>

static Opts defaultOpts = {0, false, false, false};

void Usage(void)
{
  printf("Usage: sdc [-bsi] image.tiff|folder\n");
}

bool ParseOpts(int argc, char *argv[], Opts *opts)
{
  *opts = defaultOpts;
  int ch;

  while ((ch = getopt(argc, argv, "bsi")) >= 0) {
    switch (ch) {
    case 'b':
      opts->backup = true;
      break;
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

  opts->inPath = argv[optind];

  return true;
}
