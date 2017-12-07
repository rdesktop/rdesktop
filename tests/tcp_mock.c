#include <cgreen/mocks.h>
#include "../rdesktop.h"

char *tcp_get_address()
{
  return (char *) mock();
}
