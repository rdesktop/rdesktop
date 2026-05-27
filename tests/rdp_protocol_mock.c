#include "../rdesktop.h"

void
_rdp_protocol_error(const char *file, int line, const char *func,
		    const char *message, STREAM s)
{
	(void) file;
	(void) line;
	(void) func;
	(void) message;
	(void) s;
	abort();
}
