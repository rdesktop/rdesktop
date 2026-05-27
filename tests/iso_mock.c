#include <cgreen/mocks.h>
#include "../rdesktop.h"

RD_BOOL
iso_connect(char *server, char *username, char *domain, char *password,
	    RD_BOOL reconnect, uint32 * selected_protocol)
{
  return (RD_BOOL) mock(server, username, domain, password, reconnect, selected_protocol);
}


void
iso_disconnect(void)
{
  mock();
}

void iso_send(STREAM stream)
{
  mock(stream->data);
}

STREAM iso_recv(RD_BOOL * is_fastpath, uint8 * fastpath_hdr)
{
  return (STREAM)mock(is_fastpath, fastpath_hdr);
}

void
iso_reset_state(void)
{
  mock();
}

STREAM iso_init(int length)
{
  return (STREAM)mock(length);
}
