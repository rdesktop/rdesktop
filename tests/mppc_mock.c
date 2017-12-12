#include <cgreen/mocks.h>
#include "../rdesktop.h"

int
mppc_expand(uint8 * data, uint32 clen, uint8 ctype, uint32 * roff, uint32 * rlen)
{
  return mock(data, clen, ctype, roff, rlen);
}
