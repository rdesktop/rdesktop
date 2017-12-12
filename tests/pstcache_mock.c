#include <cgreen/mocks.h>
#include "../rdesktop.h"

int pstcache_enumerate(uint8 id, HASH_KEY * keylist)
{
  return mock(id, keylist);
}

RD_BOOL pstcache_init(uint8 cache_id)
{
  return mock(cache_id);
}
