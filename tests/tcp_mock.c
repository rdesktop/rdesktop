#include <cgreen/mocks.h>
#include "../rdesktop.h"

char *tcp_get_address()
{
  return (char *) mock();
}

void
tcp_run_ui(RD_BOOL run)
{
  mock(run);
}
