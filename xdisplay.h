#ifndef RDESKTOP_XDISPLAY_H
#define RDESKTOP_XDISPLAY_H

#include <X11/Xlib.h>

int xdisplay_is_wayland_session(void);
Display *xdisplay_open(int *used_xwayland);

#endif
