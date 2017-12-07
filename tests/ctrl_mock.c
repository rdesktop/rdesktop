#include <cgreen/mocks.h>
#include "../rdesktop.h"

void
ctrl_add_fds(int *n, fd_set * rfds)
{
  mock(n, rfds);
}

void
ctrl_check_fds(fd_set * rfds, fd_set * wfds)
{
  mock(rfds, wfds);
}
