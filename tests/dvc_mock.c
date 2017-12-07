#include <cgreen/mocks.h>
#include "../rdesktop.h"

RD_BOOL
dvc_init(void)
{
  return mock();
}

RD_BOOL
dvc_channels_register(const char *name, dvc_channel_process_fn handler)
{
  return mock(name, handler);
}

RD_BOOL
dvc_channels_is_available(const char *name)
{
  return mock(name);
}

void
dvc_send(const char *name, STREAM s)
{
  mock(name, s);
}
