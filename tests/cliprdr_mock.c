#include <cgreen/mocks.h>
#include "../rdesktop.h"

void
cliprdr_set_mode(const char *optarg)
{
  mock(optarg);
}
