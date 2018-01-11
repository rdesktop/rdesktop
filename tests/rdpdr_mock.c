#include <cgreen/mocks.h>
#include "../rdesktop.h"

void
rdpdr_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv, RD_BOOL * timeout)
{
  mock(n, rfds, wfds, tv, timeout);
}

void
rdpdr_check_fds(fd_set * rfds, fd_set * wfds, RD_BOOL timed_out)
{
  mock(rfds, wfds, timed_out);
}

RD_BOOL
rdpdr_init()
{
  return mock();
}
