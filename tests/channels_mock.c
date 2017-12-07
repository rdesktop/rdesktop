#include <cgreen/mocks.h>
#include "../rdesktop.h"

void channel_process(STREAM s, uint16 mcs_channel)
{
  mock(s, mcs_channel);
}
