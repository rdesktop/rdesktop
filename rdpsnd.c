#include "rdesktop.h"

static VCHANNEL *rdpsnd_channel;

static void
rdpsnd_process(STREAM s)
{
	printf("rdpsnd_process\n");
	hexdump(s->p, s->end - s->p);
}

BOOL
rdpsnd_init(void)
{
	rdpsnd_channel =
		channel_register("rdpsnd", CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP,
				 rdpsnd_process);
	return (rdpsnd_channel != NULL);
}
