#include <cgreen/mocks.h>
#include "../rdesktop.h"

void
rdp_send_input(uint32 time, uint16 message_type, uint16 device_flags, uint16 param1, uint16 param2)
{
  mock(time, message_type, device_flags, param1, param2);
}

void
rdp_send_suppress_output_pdu(enum RDP_SUPPRESS_STATUS allowupdates)
{
  mock(allowupdates);
}

