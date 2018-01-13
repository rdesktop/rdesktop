#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>
#include "../rdesktop.h"
#include "../proto.h"

/* Boilerplate */
Describe(RDP);
BeforeEach(RDP) {};
AfterEach(RDP) {};

/* Global Variables.. :( */
uint16 g_mcs_userid;
char *g_username;
char g_password[64];
char g_codepage[16];
RD_BOOL g_orders;
RD_BOOL g_encryption;
RD_BOOL g_desktop_save;
RD_BOOL g_polygon_ellipse_orders;
RDP_VERSION g_rdp_version;
uint16 g_server_rdp_version;
uint32 g_rdp5_performanceflags;
int g_server_depth;
RD_BOOL g_bitmap_cache;
RD_BOOL g_bitmap_cache_persist_enable;
RD_BOOL g_numlock_sync;
RD_BOOL g_pending_resize;
RD_BOOL g_network_error;
time_t g_wait_for_deactivate_ts;
RDPCOMP g_mppc_dict;
RD_BOOL g_redirect;
char *g_redirect_server;
uint32 g_redirect_server_len;
char *g_redirect_domain;
uint32 g_redirect_domain_len;
char *g_redirect_username;
uint32 g_redirect_username_len;
uint8 *g_redirect_lb_info;
uint32 g_redirect_lb_info_len;
uint8 *g_redirect_cookie;
uint32 g_redirect_cookie_len;
uint32 g_redirect_flags;
uint32 g_redirect_session_id;
uint32 g_reconnect_logonid;
char g_reconnect_random[16];
time_t g_reconnect_random_ts;
RD_BOOL g_has_reconnect_random;
uint8 g_client_random[SEC_RANDOM_SIZE];
RD_BOOL g_local_cursor;

#include "../rdp.c"
#include "../utils.c"
#include "../stream.c"

/* malloc; exit if out of memory */
void *
xmalloc(int size)
{
	void *mem = malloc(size);
	if (mem == NULL)
	{
		logger(Core, Error, "xmalloc, failed to allocate %d bytes", size);
		exit(EX_UNAVAILABLE);
	}
	return mem;
}

/* Exit on NULL pointer. Use to verify result from XGetImage etc */
void
exit_if_null(void *ptr)
{
	if (ptr == NULL)
	{
		logger(Core, Error, "unexpected null pointer. Out of memory?");
		exit(EX_UNAVAILABLE);
	}
}

/* strdup */
char *
xstrdup(const char *s)
{
	char *mem = strdup(s);
	if (mem == NULL)
	{
		logger(Core, Error, "xstrdup(), strdup() failed: %s", strerror(errno));
		exit(EX_UNAVAILABLE);
	}
	return mem;
}

/* realloc; exit if out of memory */
void *
xrealloc(void *oldmem, size_t size)
{
	void *mem;

	if (size == 0)
		size = 1;
	mem = realloc(oldmem, size);
	if (mem == NULL)
	{
		logger(Core, Error, "xrealloc, failed to reallocate %ld bytes", size);
		exit(EX_UNAVAILABLE);
	}
	return mem;
}

/* free */
void
xfree(void *mem)
{
	free(mem);
}


/* Test function */
Ensure(RDP, ProcessBitmapCapsCallsUiResizeWindow) {
  struct stream s;
  memset(&s, 0, sizeof(struct stream));

  expect(ui_resize_window,
	 when(width, is_equal_to(1024)),
	 when(height, is_equal_to(768)));

  s_realloc(&s, 32);
  s_reset(&s);

  out_uint16_le(&s, 32); /* depth */
  out_uint8s(&s, 6); /* pad? dunno */
  out_uint16_le(&s, 1024);
  out_uint16_le(&s, 768);
  s_mark_end(&s);
  s_reset(&s);

  rdp_process_bitmap_caps(&s);

  free(s.data);
}
