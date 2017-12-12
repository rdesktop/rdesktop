#include <cgreen/mocks.h>
#include "../rdesktop.h"

#include <X11/Xlib.h>

void xclip_init()
{
  mock();
}

void xclip_deinit()
{
  mock();
}

void
xclip_handle_SelectionNotify(XSelectionEvent * event)
{
  mock(event);
}

void
xclip_handle_SelectionRequest(XSelectionRequestEvent * xevent)
{
  mock(xevent);
}
void
xclip_handle_SelectionClear(void)
{
  mock();
}

void
xclip_handle_PropertyNotify(XPropertyEvent * xev)
{
  mock(xev);
}
