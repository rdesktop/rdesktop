#include <cgreen/mocks.h>
#include "../rdesktop.h"

void process_orders(STREAM s, uint16 num_orders)
{
  mock(s, num_orders);
}

void reset_order_state()
{
  mock();
}
