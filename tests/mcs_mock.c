#include <cgreen/mocks.h>
#include "../rdesktop.h"

STREAM
mcs_init(int length)
{
  return (STREAM)mock(length);
}

void
mcs_send_to_channel(STREAM s, uint16 channel)
{
  mock(s, channel);
}

void
mcs_send(STREAM s)
{
  mock(s);
}

STREAM
mcs_recv(uint16 * channel, RD_BOOL * is_fastpath, uint8 * fastpath_hdr)
{
  return (STREAM)mock(channel, is_fastpath, fastpath_hdr);
}

RD_BOOL
mcs_connect_start(char *server, char *username, char *domain, char *password,
		  RD_BOOL reconnect, uint32 * selected_protocol)
{
  return mock(server,username,domain,password,reconnect,selected_protocol);
}

RD_BOOL
mcs_connect_finalize(STREAM s)
{
  return mock(s);
}

void
mcs_disconnect(int reason)
{
  mock(reason);
}

void
mcs_reset_state(void)
{
  mock();
}
