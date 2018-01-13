#include <cgreen/mocks.h>
#include "../rdesktop.h"

int
printer_enum_devices(uint32 * id, char *optarg)
{
  return mock(id, optarg);
}
