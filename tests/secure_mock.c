#include <cgreen/mocks.h>
#include "../rdesktop.h"

STREAM sec_recv(uint8 * rdpver)
{
  return (STREAM)mock(rdpver);
}

void sec_disconnect()
{
  mock();
}

STREAM sec_init(uint32 flags, int maxlen)
{
  return (STREAM) mock(flags, maxlen);
}

RD_BOOL sec_connect(char *server, char *username, char *domain, char *password, RD_BOOL reconnect)
{
  return mock(server, username, domain, password, reconnect);
}

void sec_reset_state()
{
  mock();
}

void sec_send(STREAM s, uint32 flags)
{
  mock(s, flags);
}

void
sec_hash_sha1_16(uint8 * out, uint8 * in, uint8 * salt1)
{
  mock(out, in, salt1);
}

void
sec_hash_to_string(char *out, int out_size, uint8 * in, int in_size)
{
  mock(out, out_size, in, in_size);
}


void
sec_process_mcs_data(STREAM s)
{
  mock(s);
}
