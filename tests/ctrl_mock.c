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

int
ctrl_init(const char *user, const char *domain, const char *host)
{
  return mock(user, domain, host);
}

RD_BOOL
ctrl_is_slave()
{
  return mock();
}

int
ctrl_send_command(const char *cmd, const char *args)
{
  return mock(cmd, args);
}
