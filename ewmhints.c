/*
   rdesktop: A Remote Desktop Protocol client.

   Support functions for Extended Window Manager Hints,
   http://www.freedesktop.org/standards/wm-spec.html

   Copyright (C) Peter Astrand <peter@cendio.se> 2003
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <X11/Xlib.h>
#include "rdesktop.h"

extern Display *g_display;

/* 
   Get window property value (32 bit format) 
   Returns zero on success, -1 on error
*/
static int
get_property_value(char *propname, long max_length,
		   unsigned long *nitems_return, unsigned char **prop_return)
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

	result = XGetWindowProperty(g_display, DefaultRootWindow(g_display), property, 0,	/* long_offset */
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
		fprintf(stderr, "Root window is missing property %s\n", propname);
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

	if (get_property_value("_NET_CURRENT_DESKTOP", 1, &nitems_return, &prop_return) < 0)
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
	uint32 *return_words;
	const uint32 net_workarea_x_offset = 0;
	const uint32 net_workarea_y_offset = 1;
	const uint32 net_workarea_width_offset = 2;
	const uint32 net_workarea_height_offset = 3;
	const uint32 max_prop_length = 32 * 4;	/* Max 32 desktops */

	if (get_property_value("_NET_WORKAREA", max_prop_length, &nitems_return, &prop_return) < 0)
		return (-1);

	if (nitems_return % 4)
	{
		fprintf(stderr, "_NET_WORKAREA has odd length\n");
		return (-1);
	}

	current_desktop = get_current_desktop();

	if (current_desktop < 0)
		return -1;

	return_words = (uint32 *) prop_return;

	*x = return_words[current_desktop * 4 + net_workarea_x_offset];
	*y = return_words[current_desktop * 4 + net_workarea_y_offset];
	*width = return_words[current_desktop * 4 + net_workarea_width_offset];
	*height = return_words[current_desktop * 4 + net_workarea_height_offset];

	XFree(prop_return);

	return (0);

}
