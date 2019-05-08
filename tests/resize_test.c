#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>
#include <X11/Xlib.h>
#include "../rdesktop.h"

/* Boilerplate */
Describe(Resize);
BeforeEach(Resize) {};
AfterEach(Resize) {};

/* globals driven by xwin.c */
RD_BOOL g_user_quit;
RD_BOOL g_exit_mainloop;
uint32 g_requested_session_width;
uint32 g_requested_session_height;
window_size_type_t g_window_size_type;
uint16 g_session_width;
uint16 g_session_height;
int g_xpos;
int g_ypos;
int g_pos;
RD_BOOL g_sendmotion;
RD_BOOL g_fullscreen;
RD_BOOL g_grab_keyboard;
RD_BOOL g_hide_decorations;
RD_BOOL g_pending_resize;
char g_title[] = "MyTitle";
char g_seamless_spawn_cmd[] = "";
int g_server_depth;
int g_win_button_size;
RD_BOOL g_seamless_persistent_mode;
RD_BOOL g_seamless_rdp;
RD_BOOL g_seamless_persistent_mode;
uint32 g_embed_wnd;
Atom g_net_wm_state_atom;
Atom g_net_wm_desktop_atom;
Atom g_net_wm_ping_atom;
RD_BOOL g_ownbackstore;
RD_BOOL g_rdpsnd;
RD_BOOL g_owncolmap;
RD_BOOL g_local_cursor;

/* globals driven by utils.c */
char g_codepage[16] = "";

/* global driven by rdp.c */
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
uint32 g_requested_session_width;
uint32 g_requested_session_height;
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

/* globals from secure.c */
char g_hostname[16];
uint32 g_requested_session_width;
uint32 g_requested_session_height;
int g_dpi;
unsigned int g_keylayout;
int g_keyboard_type;
int g_keyboard_subtype;
int g_keyboard_functionkeys;
RD_BOOL g_encryption;
RD_BOOL g_licence_issued;
RD_BOOL g_licence_error_result;
RDP_VERSION g_rdp_version;
RD_BOOL g_console_session;
uint32 g_redirect_session_id;
int g_server_depth;
VCHANNEL g_channels[1];
unsigned int g_num_channels;
uint8 g_client_random[SEC_RANDOM_SIZE];

/* Xlib macros to mock functions */
#undef DefaultRootWindow
Window DefaultRootWindow(Display *display) { return (Window) mock(display); }

#undef WidthOfScreen
int WidthOfScreen(Screen* x) { return mock(x); }

#undef HeightOfScreen
int HeightOfScreen(Screen *x) { return mock(x); }


#include "../xwin.c"
#include "../utils.c"
#include "../rdp.c"
#include "../stream.c"
#include "../secure.c"

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

/* X11 mocks */

Status
XGetWindowAttributes(Display *display, Window wnd, XWindowAttributes *attr)
{
	return mock(display, wnd, attr);
}

int
XResizeWindow(Display *display, Window wnd, unsigned width, unsigned height)
{
	return mock(display, wnd, width, height);
}

XSizeHints *
XAllocSizeHints(void)
{
	return (XSizeHints *) mock();
}


int
XSetClipRectangles(Display *display, GC gc, int clip_x_origin, int clip_y_origin,
		   XRectangle rectangles[], int n, int ordering)
{
	return mock(display, gc, clip_x_origin, clip_y_origin, rectangles, n, ordering);
}

/* Test helpers */

struct stream
bitmap_caps_packet(int width, int height)
{
	struct stream s;
	memset(&s, 0, sizeof(s));
	s_realloc(&s, 32);
	s_reset(&s);

	out_uint16_le(&s, 32); /* depth */
	out_uint8s(&s, 6); /* pad? dunno */
	out_uint16_le(&s, width);
	out_uint16_le(&s, height);
	s_mark_end(&s);
	s_reset(&s);

	return s;
}

#define RDPEDISP True
#define RECONNECT False

#define FULLSCREEN True
#define WINDOW False

void setup_user_initiated_resize(int width, int height, RD_BOOL use_rdpedisp, RD_BOOL fullscreen)
{
	/* g_resize_timer = A second ago */
	struct timeval tv;
	gettimeofday(&tv, NULL);
	tv.tv_sec -= 1;
	g_resize_timer = tv;

	g_fullscreen = fullscreen;

	g_window_width = width;
	g_window_height = height;

	expect(rdpedisp_is_available,
	       will_return(use_rdpedisp));
}

struct stream setup_server_resize_response(int width, int height) {
	expect(XGetWindowAttributes);
	expect(XSetClipRectangles);
	expect(XAllocSizeHints,
	       will_return(0));

	return bitmap_caps_packet(width, height);
}

Ensure(Resize, UsingRDPEDISP)
{
	struct stream s;
	int user_wanted_width = 1280;
	int user_wanted_height = 1024;

	/* Step 1 : Act on UI side initiated resize and tell server about it using RDPEDISP */
	setup_user_initiated_resize(user_wanted_width, user_wanted_height, RDPEDISP, WINDOW);

	expect(rdpedisp_set_session_size,
	       when(width, is_equal_to(user_wanted_width)),
	       when(height, is_equal_to(user_wanted_height)));

	/* FIXME: Move process_pending_resize out of X11 UI implementation */
	assert_that(process_pending_resize(), is_equal_to(False));

	/* Step 2 : Handle a BITMAP_CAPS containing session size from server
	   so set window size accordingly */
	s = setup_server_resize_response(user_wanted_width, user_wanted_height);

	expect(XResizeWindow,
	       when(width, is_equal_to(user_wanted_width)),
	       when(height, is_equal_to(user_wanted_height)));

	rdp_process_bitmap_caps(&s);
	free(s.data);
}

Ensure(Resize, UsingRDPEDISPHonoursServerMaximumSessionSizeLimit)
{
	/* User has changed window size, X has resized, RDP needs to tell server.
	   RDPEDISP is available and a Windows 2012 server will limit session size
	   to maximum 8192x8192. We will set window size to whatever server says.
	*/
	struct stream s;
	int user_wanted_width = 9000;
	int user_wanted_height = 9000;

	/* Step 1 : Act on UI side initiated resize and tell server about it
	   using RDPEDISP */
	setup_user_initiated_resize(user_wanted_width, user_wanted_height, RDPEDISP, WINDOW);

	expect(rdpedisp_set_session_size,
	       when(width, is_equal_to(user_wanted_width)),
	       when(height, is_equal_to(user_wanted_height)));

	/* FIXME: Move process_pending_resize out of X11 UI implementation */
	assert_that(process_pending_resize(), is_equal_to(False));

	/* Step 2 : Handle a BITMAP_CAPS containing session size from server
	   so set window size accordingly */

	int resulting_server_width = 8192;
	int resulting_server_height = 8192;

	s = setup_server_resize_response(resulting_server_width, resulting_server_height);

	expect(XResizeWindow,
	       when(width, is_equal_to(resulting_server_width)),
	       when(height, is_equal_to(resulting_server_height)));

	/* simulate response from server */
	rdp_process_bitmap_caps(&s);
	free(s.data);
}


Ensure(Resize, UsingRDPEDISPHonoursServerMinimumSessionSizeLimit)
{
	/* User has changed window size, X has resized, RDP needs to tell server.
	   RDPEDISP is available and a Windows 2012 server will limit session size
	   to minimum 200x200. We will set window size to whatever server says.
	*/

	struct stream s;

	/* Step 1 : Act on UI side initiated resize and tell server about it
	   using RDPEDISP */

	int user_wanted_width = 100;
	int user_wanted_height = 100;

	setup_user_initiated_resize(user_wanted_width, user_wanted_height, RDPEDISP, WINDOW);

	expect(rdpedisp_set_session_size,
	       when(width, is_equal_to(user_wanted_width)),
	       when(height, is_equal_to(user_wanted_height)));

	/* FIXME: Move process_pending_resize out of X11 UI implementation */
	assert_that(process_pending_resize(), is_equal_to(False));

	/* Step 2 : Handle a BITMAP_CAPS containing session size from server
	   so set window size accordingly */

	int resulting_server_width = 200;
	int resulting_server_height = 200;

	s = setup_server_resize_response(resulting_server_width, resulting_server_height);

	expect(XResizeWindow,
	       when(width, is_equal_to(resulting_server_width)),
	       when(height, is_equal_to(resulting_server_height)));

	/* simulate response from server */
	rdp_process_bitmap_caps(&s);
	free(s.data);
}

Ensure(Resize, UsingRDPEDISPHonoursServerSessionWidthConstraintMustBeEven)
{
	/* User has changed window size, X has resized, RDP needs to tell server.
	   RDPEDISP is available and a Windows 2012 server will limit session size
	   to minimum 200x200. We will set window size to whatever server says.
	*/
	struct stream s;

	/* Step 1 : Act on UI side initiated resize and tell server about it
	   using RDPEDISP */

	int user_wanted_width = 999;
	int user_wanted_height = 900;

	setup_user_initiated_resize(user_wanted_width, user_wanted_height, RDPEDISP, WINDOW);

	expect(rdpedisp_set_session_size,
	       when(width, is_equal_to(user_wanted_width)),
	       when(height, is_equal_to(user_wanted_height)));

	/* FIXME: Move process_pending_resize out of X11 UI implementation */
	assert_that(process_pending_resize(), is_equal_to(False));

	/* Step 2 : Handle a BITMAP_CAPS containing session size from server
	   so set window size accordingly */

	/* FIXME: Does the server round up or down? */
	int resulting_server_width = 998;
	int resulting_server_height = 900;

	s = setup_server_resize_response(resulting_server_width, resulting_server_height);

	expect(XResizeWindow,
	       when(width, is_equal_to(resulting_server_width)),
	       when(height, is_equal_to(resulting_server_height)));

	/* simulate response from server */
	rdp_process_bitmap_caps(&s);
	free(s.data);
}

void get_width_and_height_from_mcs_connect_initial(int *width, int *height)
{
	STREAM s;

	/* Allocate stream and write mcs_connect_initial PDU to it */
	s = s_alloc(4096);
	sec_out_mcs_connect_initial_pdu(s, 0);

	/* Rewind and extract the requested session size */
	s_reset(s);
	in_uint8s(s, 31);
	in_uint16_le(s, *width);	/* desktopWidth */
	in_uint16_le(s, *height);	/* desktopHeight */

	s_free(s);
}


Ensure(Resize, UsingReconnect)
{
	struct stream s;

	int user_wanted_width = 1280;
	int user_wanted_height = 1024;

	/* Step 1 : Act on UI side initiated resize */
	setup_user_initiated_resize(user_wanted_width, user_wanted_height, RECONNECT, WINDOW);

	assert_that(process_pending_resize(),
		    is_equal_to(True));

	/* we assume that process_pending_resize returning True will exit the main loop and initiate a
	   reconnect */


	/* Step 2 : Simulate parts of the connection sequence where we send
	   width & height through a MCS Connect Initial packet to the server */

	int sent_width, sent_height;

	get_width_and_height_from_mcs_connect_initial(&sent_width, &sent_height);

	assert_that(sent_width, is_equal_to(user_wanted_width));
	assert_that(sent_height, is_equal_to(user_wanted_height));


	/* Step 3 : Handle a BITMAP_CAPS containing session size from server
	   so set window size accordingly */

	s = setup_server_resize_response(user_wanted_width,
					 user_wanted_height);

	expect(XResizeWindow,
	       when(width, is_equal_to(user_wanted_width)),
	       when(height, is_equal_to(user_wanted_height)));

	rdp_process_bitmap_caps(&s);
	free(s.data);
}


Ensure(Resize, UsingReconnectHonoursServerMaximumSessionSizeLimit)
{
	struct stream s;

	int user_wanted_width = 9000;
	int user_wanted_height = 9000;

	/* Step 1 : Act on UI side initiated resize */
	setup_user_initiated_resize(user_wanted_width, user_wanted_height, RECONNECT, WINDOW);

	/* FIXME: Move process_pending_resize out of X11 UI implementation */
	assert_that(process_pending_resize(),
		    is_equal_to(True));

	/* We assume that process_pending_resize returning True exits the main
	   loop and initiates a reconnect */


	/* Step 2 : Simulate parts of the connection sequence where we send
	   width & height through a MCS Connect Initial packet to the server */

	int sent_width, sent_height;

	get_width_and_height_from_mcs_connect_initial(&sent_width, &sent_height);

	assert_that(sent_width, is_equal_to(user_wanted_width));
	assert_that(sent_height, is_equal_to(user_wanted_height));

	/* Step 3 : Handle a BITMAP_CAPS containing session size from server
	   so set window size accordingly */

	int resulting_server_width = 4096;
	int resulting_server_height = 2048;

	s = setup_server_resize_response(resulting_server_width,
					 resulting_server_height);

	expect(XResizeWindow,
	       when(width, is_equal_to(resulting_server_width)),
	       when(height, is_equal_to(resulting_server_height)));

	rdp_process_bitmap_caps(&s);
	free(s.data);
}

int
XNextEvent(Display *display, XEvent *event)
{
	return mock(display, event);
}

int
XPending(Display *display)
{
	return mock(display);
}

void
setup_user_initiated_root_window_resize(int width, int height,
					RD_BOOL use_rdpedisp, RD_BOOL fullscreen)
{
	XEvent rootWindowResizeEvent;
	memset(&rootWindowResizeEvent, 0, sizeof(XEvent));

	rootWindowResizeEvent.xconfigure.type = ConfigureNotify;
	rootWindowResizeEvent.xconfigure.window = DefaultRootWindow(g_display);
	rootWindowResizeEvent.xconfigure.width = width;
	rootWindowResizeEvent.xconfigure.height = height;

	/* one event to process */
	expect(XPending, will_return(1));

	/* event is a ConfigureNotify event on root window */
	expect(XNextEvent,
	       will_set_contents_of_parameter(event,
					      &rootWindowResizeEvent,
					      sizeof(XConfigureEvent *)));
	/* no more events to process */
	expect(XPending,
	       will_return(0));


	expect(rdpedisp_is_available, will_return(use_rdpedisp));
	g_fullscreen = fullscreen;
}
