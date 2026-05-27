#include <cgreen/mocks.h>
#include "../rdesktop.h"

RD_BOOL
addin_parse_option(const char *optarg)
{
	return mock(optarg);
}

RD_BOOL
addin_init(void)
{
	return mock();
}

void
addin_add_fds(int *n, fd_set *rfds, fd_set *wfds)
{
	mock(n, rfds, wfds);
}

void
addin_check_fds(fd_set *rfds, fd_set *wfds)
{
	mock(rfds, wfds);
}

void
addin_deinit(void)
{
	mock();
}
