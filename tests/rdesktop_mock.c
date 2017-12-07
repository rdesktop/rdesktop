#include <cgreen/mocks.h>
#include "../rdesktop.h"

void rd_create_ui(void)
{
  mock();
}


void generate_random(uint8 * random)
{
  mock(random);
}
