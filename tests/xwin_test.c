#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>
#include "../rdesktop.h"
#include "../proto.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>

/* Boilerplate */
Describe(XWIN);
BeforeEach(XWIN) {};
AfterEach(XWIN) {};

/* Global Variables.. :( */
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
char g_title[64];
char g_seamless_spawn_cmd[512];
/* Color depth of the RDP session.
   As of RDP 5.1, it may be 8, 15, 16 or 24. */
int g_server_depth;
int g_win_button_size;
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
char g_codepage[16];

#include "../xwin.c"
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

/* Special mocks */

int XResizeWindow(Display *display, Window wnd, unsigned int width, unsigned int height)
{
  return mock(display, wnd, width, height);
}

Status
XGetWindowAttributes(Display *display, Window wnd, XWindowAttributes *attr)
{
  return mock(display, wnd, attr);
}

void
XSetWMSizeHints(Display *display, Window wnd, XSizeHints *hints, Atom property)
{
  mock(display, wnd, hints, property);
}

int
XSetClipRectangles(Display *display, GC gc, int clip_x_origin,
		   int clip_y_origin, XRectangle rectangles[],
		   int n, int ordering)
{
  return mock(display, gc, clip_x_origin, clip_y_origin,
	      rectangles, n, ordering);
}

int XPending(Display *display)
{
  return mock(display);
}

/* Test functions */

Ensure(XWIN, UiResizeWindowCallsXResizeWindow) {
  int width = 1024;
  int height = 768;

  /* stubs */
  expect(XGetWindowAttributes, will_return(True));
  expect(XSetWMSizeHints);
  expect(XSetClipRectangles);

  /* expects */
  expect(XResizeWindow,
	 when(width, is_equal_to(width)),
	 when(height, is_equal_to(height)));

  ui_resize_window(width, height);
}

/* FIXME: This test is broken */
#if 0
Ensure(XWIN, UiSelectCallsProcessPendingResizeIfGPendingResizeIsTrue)
{
  g_pending_resize = True;

  expect(rdpdr_add_fds);
  expect(rdpdr_check_fds);

  expect(ctrl_add_fds);
  expect(ctrl_check_fds);

  expect(seamless_select_timeout);

  expect(XPending, will_return(0));

  expect(rdpedisp_is_available, will_return(False));

  /* HELP! How do we mock functions within the same unit? We're
     drawing blanks with plenty of "redefined function" or "multiple
     definition" errors from the compiler and linker.

     The two attempts below does not work as intended.
  */

  /* Letting process_pending_resize return true will break the
     ui_select loop. */
  expect(process_pending_resize, will_return(True));

  /* process_fds returning True indicates there is data on rdp socket,
     assumed this will break out of ui_select loop */
  //expect(process_fds, will_return(True));

  ui_select(0);
}
#endif /* broken */


#if 0 /* hackety-hackety-hack /k */
Ensure(XWIN, UiSelectCallsRDPEDISPSetSessionSizeOnResize)
{
  g_pending_resize = True;
  g_resize_timer = (struct timeval) {0, 0};
  expect(rdpedisp_is_available, will_return(True));
  expect(rdpedisp_set_session_size);

  ui_select(0);
}
#endif
