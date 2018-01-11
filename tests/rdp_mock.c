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

RD_BOOL
rdp_connect(char *server, uint32 flags, char *domain, char *password, char *command,
	    char *directory, RD_BOOL reconnect)
{
  return mock(server, flags, domain, password, command, directory, reconnect);
}

void
rdp_disconnect()
{
  mock();
}

void
rdp_main_loop(RD_BOOL * deactivated, uint32 * ext_disc_reason)
{
  mock(deactivated, ext_disc_reason);
}

void
rdp_reset_state()
{
  mock();
}
