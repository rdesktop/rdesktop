#include <cgreen/mocks.h>
#include "../rdesktop.h"

int
serial_enum_devices(uint32 * id, char *optarg)
{
  return mock(id, optarg);
}
