#include <cgreen/mocks.h>
#include "../rdesktop.h"
#include <X11/Xlib.h>

int
ewmh_get_window_state(Window w)
{
  return mock(w);
}

int
ewmh_change_state(Window wnd, int state)
{
  return mock(wnd, state);
}

int
ewmh_move_to_desktop(Window wnd, unsigned int desktop)
{
  return mock(wnd, desktop);
}

int
ewmh_get_window_desktop(Window wnd)
{
  return mock(wnd);
}

void
ewmh_set_wm_name(Window wnd, const char *title)
{
  mock(wnd, title);
}

void
ewmh_set_wm_pid(Window wnd, pid_t pid)
{
  mock(wnd, pid);
}

int
ewmh_set_window_popup(Window wnd)
{
  return mock(wnd);
}

int
ewmh_set_window_modal(Window wnd)
{
  return mock(wnd);
}

void
ewmh_set_icon(Window wnd, int width, int height, const char *rgba_data)
{
  mock(wnd, width, height, rgba_data);
}

void
ewmh_del_icon(Window wnd, int width, int height)
{
  mock(wnd, width, height);
}

int
ewmh_set_window_above(Window wnd)
{
  return mock(wnd);
}

RD_BOOL
ewmh_is_window_above(Window w)
{
  return mock(w);
}

int
get_current_workarea(uint32 *x, uint32 *y, uint32 *width, uint32 *height)
{
  return mock(x, y, width, height);
}

void
ewmh_init()
{
  mock();
}
