/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.

   Support functions for Extended Window Manager Hints,
   http://www.freedesktop.org/wiki/Standards_2fwm_2dspec

   Copyright 2005-2011 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright 2007 Pierre Ossman <ossman@cendio.se> for Cendio AB
   Copyright 2015 Henrik Andersson <hean01@cendio.se> for Cendio AB

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include "rdesktop.h"

#define _NET_WM_STATE_REMOVE        0	/* remove/unset property */
#define _NET_WM_STATE_ADD           1	/* add/set property */
#define _NET_WM_STATE_TOGGLE        2	/* toggle property  */

extern Display *g_display;

static Atom g_net_wm_state_maximized_vert_atom, g_net_wm_state_maximized_horz_atom,
	g_net_wm_state_hidden_atom, g_net_wm_name_atom, g_utf8_string_atom,
	g_net_wm_state_skip_taskbar_atom, g_net_wm_state_skip_pager_atom,
	g_net_wm_state_modal_atom, g_net_wm_icon_atom, g_net_wm_state_above_atom, g_net_wm_pid_atom;

Atom g_net_wm_state_atom, g_net_wm_desktop_atom, g_net_wm_ping_atom;

/* 
   Get window property value (32 bit format) 
   Returns zero on success, -1 on error
*/
static int
get_property_value(Window wnd, char *propname, long max_length,
		   unsigned long *nitems_return, unsigned char **prop_return, int nowarn)
{
	int result;
	Atom property;
	Atom actual_type_return;
	int actual_format_return;
	unsigned long bytes_after_return;

	property = XInternAtom(g_display, propname, True);
	if (property == None)
	{
		fprintf(stderr, "Atom %s does not exist\n", propname);
		return (-1);
	}

	result = XGetWindowProperty(g_display, wnd, property, 0,	/* long_offset */
				    max_length,	/* long_length */
				    False,	/* delete */
				    AnyPropertyType,	/* req_type */
				    &actual_type_return,
				    &actual_format_return,
				    nitems_return, &bytes_after_return, prop_return);

	if (result != Success)
	{
		fprintf(stderr, "XGetWindowProperty failed\n");
		return (-1);
	}

	if (actual_type_return == None || actual_format_return == 0)
	{
		if (!nowarn)
			fprintf(stderr, "Window is missing property %s\n", propname);
		return (-1);
	}

	if (bytes_after_return)
	{
		fprintf(stderr, "%s is too big for me\n", propname);
		return (-1);
	}

	if (actual_format_return != 32)
	{
		fprintf(stderr, "%s has bad format\n", propname);
		return (-1);
	}

	return (0);
}

/* 
   Get current desktop number
   Returns -1 on error
*/
static int
get_current_desktop(void)
{
	unsigned long nitems_return;
	unsigned char *prop_return;
	int current_desktop;

	if (get_property_value
	    (DefaultRootWindow(g_display), "_NET_CURRENT_DESKTOP", 1, &nitems_return,
	     &prop_return, 0) < 0)
		return (-1);

	if (nitems_return != 1)
	{
		fprintf(stderr, "_NET_CURRENT_DESKTOP has bad length\n");
		return (-1);
	}

	current_desktop = *prop_return;
	XFree(prop_return);
	return current_desktop;
}

/*
  Get workarea geometry
  Returns zero on success, -1 on error
 */

int
get_current_workarea(uint32 * x, uint32 * y, uint32 * width, uint32 * height)
{
	int current_desktop;
	unsigned long nitems_return;
	unsigned char *prop_return;
	long *return_words;
	const uint32 net_workarea_x_offset = 0;
	const uint32 net_workarea_y_offset = 1;
	const uint32 net_workarea_width_offset = 2;
	const uint32 net_workarea_height_offset = 3;
	const uint32 max_prop_length = 32 * 4;	/* Max 32 desktops */

	if (get_property_value
	    (DefaultRootWindow(g_display), "_NET_WORKAREA", max_prop_length, &nitems_return,
	     &prop_return, 0) < 0)
		return (-1);

	if (nitems_return % 4)
	{
		fprintf(stderr, "_NET_WORKAREA has odd length\n");
		return (-1);
	}

	current_desktop = get_current_desktop();

	if (current_desktop < 0)
		return -1;

	return_words = (long *) prop_return;

	*x = return_words[current_desktop * 4 + net_workarea_x_offset];
	*y = return_words[current_desktop * 4 + net_workarea_y_offset];
	*width = return_words[current_desktop * 4 + net_workarea_width_offset];
	*height = return_words[current_desktop * 4 + net_workarea_height_offset];

	XFree(prop_return);

	return (0);

}



void
ewmh_init()
{
	/* FIXME: Use XInternAtoms */
	g_net_wm_state_maximized_vert_atom =
		XInternAtom(g_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
	g_net_wm_state_maximized_horz_atom =
		XInternAtom(g_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	g_net_wm_state_hidden_atom = XInternAtom(g_display, "_NET_WM_STATE_HIDDEN", False);
	g_net_wm_state_skip_taskbar_atom =
		XInternAtom(g_display, "_NET_WM_STATE_SKIP_TASKBAR", False);
	g_net_wm_state_skip_pager_atom = XInternAtom(g_display, "_NET_WM_STATE_SKIP_PAGER", False);
	g_net_wm_state_modal_atom = XInternAtom(g_display, "_NET_WM_STATE_MODAL", False);
	g_net_wm_state_above_atom = XInternAtom(g_display, "_NET_WM_STATE_ABOVE", False);
	g_net_wm_state_atom = XInternAtom(g_display, "_NET_WM_STATE", False);
	g_net_wm_desktop_atom = XInternAtom(g_display, "_NET_WM_DESKTOP", False);
	g_net_wm_name_atom = XInternAtom(g_display, "_NET_WM_NAME", False);
	g_net_wm_icon_atom = XInternAtom(g_display, "_NET_WM_ICON", False);
	g_net_wm_pid_atom = XInternAtom(g_display, "_NET_WM_PID", False);
	g_net_wm_ping_atom = XInternAtom(g_display, "_NET_WM_PING", False);
	g_utf8_string_atom = XInternAtom(g_display, "UTF8_STRING", False);
}


/* 
   Get the window state: normal/minimized/maximized. 
*/
#ifndef MAKE_PROTO
int
ewmh_get_window_state(Window w)
{
	unsigned long nitems_return;
	unsigned char *prop_return;
	unsigned long *return_words;
	unsigned long item;
	RD_BOOL maximized_vert, maximized_horz, hidden;

	maximized_vert = maximized_horz = hidden = False;

	if (get_property_value(w, "_NET_WM_STATE", 64, &nitems_return, &prop_return, 0) < 0)
		return SEAMLESSRDP_NORMAL;

	return_words = (unsigned long *) prop_return;

	for (item = 0; item < nitems_return; item++)
	{
		if (return_words[item] == g_net_wm_state_maximized_vert_atom)
			maximized_vert = True;
		if (return_words[item] == g_net_wm_state_maximized_horz_atom)
			maximized_horz = True;
		if (return_words[item] == g_net_wm_state_hidden_atom)
			hidden = True;
	}

	XFree(prop_return);

	/* In EWMH, HIDDEN overrides MAXIMIZED_VERT/MAXIMIZED_HORZ */
	if (hidden)
		return SEAMLESSRDP_MINIMIZED;
	else if (maximized_vert && maximized_horz)
		return SEAMLESSRDP_MAXIMIZED;
	else
		return SEAMLESSRDP_NORMAL;
}

static int
ewmh_modify_state(Window wnd, int add, Atom atom1, Atom atom2)
{
	Status status;
	XEvent xevent;

	int result;
	unsigned long nitems;
	unsigned char *props;
	uint32 state = WithdrawnState;

	/* The spec states that the window manager must respect any
	   _NET_WM_STATE attributes on a withdrawn window. In order words, we
	   modify the attributes directly for withdrawn windows and ask the WM
	   to do it for active windows. */
	result = get_property_value(wnd, "WM_STATE", 64, &nitems, &props, 1);
	if ((result >= 0) && nitems)
	{
		state = *(uint32 *) props;
		XFree(props);
	}

	if (state == WithdrawnState)
	{
		if (add)
		{
			Atom atoms[2];

			atoms[0] = atom1;
			nitems = 1;
			if (atom2)
			{
				atoms[1] = atom2;
				nitems = 2;
			}

			XChangeProperty(g_display, wnd, g_net_wm_state_atom, XA_ATOM,
					32, PropModeAppend, (unsigned char *) atoms, nitems);
		}
		else
		{
			Atom *atoms;
			int i;

			if (get_property_value(wnd, "_NET_WM_STATE", 64, &nitems, &props, 1) < 0)
				return 0;

			atoms = (Atom *) props;

			for (i = 0; i < nitems; i++)
			{
				if ((atoms[i] == atom1) || (atom2 && (atoms[i] == atom2)))
				{
					if (i != (nitems - 1))
						memmove(&atoms[i], &atoms[i + 1],
							sizeof(Atom) * (nitems - i - 1));
					nitems--;
					i--;
				}
			}

			XChangeProperty(g_display, wnd, g_net_wm_state_atom, XA_ATOM,
					32, PropModeReplace, (unsigned char *) atoms, nitems);

			XFree(props);
		}

		return 0;
	}

	xevent.type = ClientMessage;
	xevent.xclient.window = wnd;
	xevent.xclient.message_type = g_net_wm_state_atom;
	xevent.xclient.format = 32;
	if (add)
		xevent.xclient.data.l[0] = _NET_WM_STATE_ADD;
	else
		xevent.xclient.data.l[0] = _NET_WM_STATE_REMOVE;
	xevent.xclient.data.l[1] = atom1;
	xevent.xclient.data.l[2] = atom2;
	xevent.xclient.data.l[3] = 0;
	xevent.xclient.data.l[4] = 0;
	status = XSendEvent(g_display, DefaultRootWindow(g_display), False,
			    SubstructureNotifyMask | SubstructureRedirectMask, &xevent);
	if (!status)
		return -1;

	return 0;
}

/* 
   Set the window state: normal/minimized/maximized. 
   Returns -1 on failure. 
*/
int
ewmh_change_state(Window wnd, int state)
{
	/*
	 * Deal with the max atoms
	 */
	if (state == SEAMLESSRDP_MAXIMIZED)
	{
		if (ewmh_modify_state
		    (wnd, 1, g_net_wm_state_maximized_vert_atom,
		     g_net_wm_state_maximized_horz_atom) < 0)
			return -1;
	}
	else
	{
		if (ewmh_modify_state
		    (wnd, 0, g_net_wm_state_maximized_vert_atom,
		     g_net_wm_state_maximized_horz_atom) < 0)
			return -1;
	}

	return 0;
}


int
ewmh_get_window_desktop(Window wnd)
{
	unsigned long nitems_return;
	unsigned char *prop_return;
	int desktop;

	if (get_property_value(wnd, "_NET_WM_DESKTOP", 1, &nitems_return, &prop_return, 0) < 0)
		return (-1);

	if (nitems_return != 1)
	{
		fprintf(stderr, "_NET_WM_DESKTOP has bad length\n");
		return (-1);
	}

	desktop = *prop_return;
	XFree(prop_return);
	return desktop;
}


int
ewmh_move_to_desktop(Window wnd, unsigned int desktop)
{
	Status status;
	XEvent xevent;

	xevent.type = ClientMessage;
	xevent.xclient.window = wnd;
	xevent.xclient.message_type = g_net_wm_desktop_atom;
	xevent.xclient.format = 32;
	xevent.xclient.data.l[0] = desktop;
	xevent.xclient.data.l[1] = 0;
	xevent.xclient.data.l[2] = 0;
	xevent.xclient.data.l[3] = 0;
	xevent.xclient.data.l[4] = 0;
	status = XSendEvent(g_display, DefaultRootWindow(g_display), False,
			    SubstructureNotifyMask | SubstructureRedirectMask, &xevent);
	if (!status)
		return -1;

	return 0;
}

void
ewmh_set_wm_name(Window wnd, const char *title)
{
	int len;

	len = strlen(title);
	XChangeProperty(g_display, wnd, g_net_wm_name_atom, g_utf8_string_atom,
			8, PropModeReplace, (unsigned char *) title, len);
}

void
ewmh_set_wm_pid(Window wnd, pid_t pid)
{
	XChangeProperty(g_display, wnd, g_net_wm_pid_atom,
			XA_CARDINAL, sizeof(pid_t) * 8, PropModeReplace, (unsigned char *) &pid, 1);
}

int
ewmh_set_window_popup(Window wnd)
{
	if (ewmh_modify_state
	    (wnd, 1, g_net_wm_state_skip_taskbar_atom, g_net_wm_state_skip_pager_atom) < 0)
		return -1;
	return 0;
}

int
ewmh_set_window_modal(Window wnd)
{
	if (ewmh_modify_state(wnd, 1, g_net_wm_state_modal_atom, 0) < 0)
		return -1;
	return 0;
}

void
ewmh_set_icon(Window wnd, int width, int height, const char *rgba_data)
{
	unsigned long nitems, i;
	unsigned char *props;
	unsigned long *cur_set, *new_set;
	unsigned long *icon;

	cur_set = NULL;
	new_set = NULL;

	if (get_property_value(wnd, "_NET_WM_ICON", 10000, &nitems, &props, 1) >= 0)
	{
		cur_set = (unsigned long *) props;

		for (i = 0; i < nitems;)
		{
			if (cur_set[i] == width && cur_set[i + 1] == height)
				break;

			i += 2 + cur_set[i] * cur_set[i + 1];
		}

		if (i != nitems)
			icon = cur_set + i;
		else
		{
			new_set = xmalloc((nitems + width * height + 2) * sizeof(unsigned long));
			memcpy(new_set, cur_set, nitems * sizeof(unsigned long));
			icon = new_set + nitems;
			nitems += width * height + 2;
		}
	}
	else
	{
		new_set = xmalloc((width * height + 2) * sizeof(unsigned long));
		icon = new_set;
		nitems = width * height + 2;
	}

	icon[0] = width;
	icon[1] = height;

	/* Convert RGBA -> ARGB */
	for (i = 0; i < width * height; i++)
	{
		icon[i + 2] =
			rgba_data[i * 4 + 3] << 24 |
			((rgba_data[i * 4 + 0] << 16) & 0x00FF0000) |
			((rgba_data[i * 4 + 1] << 8) & 0x0000FF00) |
			((rgba_data[i * 4 + 2] << 0) & 0x000000FF);
	}

	XChangeProperty(g_display, wnd, g_net_wm_icon_atom, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *) (new_set ? new_set : cur_set), nitems);

	if (cur_set)
		XFree(cur_set);
	if (new_set)
		xfree(new_set);
}

void
ewmh_del_icon(Window wnd, int width, int height)
{
	unsigned long nitems, i, icon_size;
	unsigned char *props;
	unsigned long *cur_set, *new_set;

	cur_set = NULL;
	new_set = NULL;

	if (get_property_value(wnd, "_NET_WM_ICON", 10000, &nitems, &props, 1) < 0)
		return;

	cur_set = (unsigned long *) props;

	for (i = 0; i < nitems;)
	{
		if (cur_set[i] == width && cur_set[i + 1] == height)
			break;

		i += 2 + cur_set[i] * cur_set[i + 1];
	}

	if (i == nitems)
		goto out;

	icon_size = width * height + 2;
	new_set = xmalloc((nitems - icon_size) * sizeof(unsigned long));

	if (i != 0)
		memcpy(new_set, cur_set, i * sizeof(unsigned long));
	if (i != nitems - icon_size)
		memcpy(new_set + i, cur_set + i + icon_size,
		       (nitems - (i + icon_size)) * sizeof(unsigned long));

	nitems -= icon_size;

	XChangeProperty(g_display, wnd, g_net_wm_icon_atom, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *) new_set, nitems);

	xfree(new_set);

      out:
	XFree(cur_set);
}

int
ewmh_set_window_above(Window wnd)
{
	if (ewmh_modify_state(wnd, 1, g_net_wm_state_above_atom, 0) < 0)
		return -1;
	return 0;
}

RD_BOOL
ewmh_is_window_above(Window w)
{
	unsigned long nitems_return;
	unsigned char *prop_return;
	unsigned long *return_words;
	unsigned long item;
	RD_BOOL above;

	above = False;

	if (get_property_value(w, "_NET_WM_STATE", 64, &nitems_return, &prop_return, 0) < 0)
		return False;

	return_words = (unsigned long *) prop_return;

	for (item = 0; item < nitems_return; item++)
	{
		if (return_words[item] == g_net_wm_state_above_atom)
			above = True;
	}

	XFree(prop_return);

	return above;
}

#endif /* MAKE_PROTO */


#if 0

/* FIXME: _NET_MOVERESIZE_WINDOW is for pagers, not for
   applications. We should implement _NET_WM_MOVERESIZE instead */

int
ewmh_net_moveresize_window(Window wnd, int x, int y, int width, int height)
{
	Status status;
	XEvent xevent;
	Atom moveresize;

	moveresize = XInternAtom(g_display, "_NET_MOVERESIZE_WINDOW", False);
	if (!moveresize)
	{
		return -1;
	}

	xevent.type = ClientMessage;
	xevent.xclient.window = wnd;
	xevent.xclient.message_type = moveresize;
	xevent.xclient.format = 32;
	xevent.xclient.data.l[0] = StaticGravity | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11);
	xevent.xclient.data.l[1] = x;
	xevent.xclient.data.l[2] = y;
	xevent.xclient.data.l[3] = width;
	xevent.xclient.data.l[4] = height;

	status = XSendEvent(g_display, DefaultRootWindow(g_display), False,
			    SubstructureNotifyMask | SubstructureRedirectMask, &xevent);
	if (!status)
		return -1;
	return 0;
}

#endif
