#include <cgreen/mocks.h>
#include "../rdesktop.h"

void
rdpedisp_init()
{
  mock();
}

RD_BOOL
rdpedisp_is_available()
{
  return mock();
}

void
rdpedisp_set_session_size(uint32 width, uint32 height)
{
  mock(width, height);
}
