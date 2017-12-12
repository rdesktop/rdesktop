#include <cgreen/mocks.h>
#include "../rdesktop.h"

RD_BOOL seamless_init()
{
  return mock();
}

void seamless_reset_state()
{
  mock();
}

unsigned int seamless_send_sync(void)
{
  return mock();
}

unsigned int seamless_send_state(unsigned long id, unsigned int state, unsigned long flags)
{
  return mock(id, state, flags);
}

unsigned int seamless_send_position(unsigned long id, int x, int y,
				    int width, int height, unsigned long flags)
{
  return mock(id, x, y, width, height, flags);
}

void seamless_select_timeout(struct timeval *tv)
{
  mock(tv);
}

unsigned int seamless_send_zchange(unsigned long id, unsigned long below, unsigned long flags)
{
  return mock(id, below, flags);
}

unsigned int seamless_send_focus(unsigned long id, unsigned long flags)
{
  return mock(id, flags);
}

unsigned int seamless_send_destroy(unsigned long id)
{
  return mock(id);
}

unsigned int seamless_send_spawn(char *cmd)
{
  return mock(cmd);
}

unsigned int seamless_send_persistent(RD_BOOL enable)
{
  return mock(enable);
}
