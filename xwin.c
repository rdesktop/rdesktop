/*
   rdesktop: A Remote Desktop Protocol client.
   User interface services - X-Windows
   Copyright (C) Matthew Chapman 1999-2000
   
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

#include "includes.h"

HWINDOW ui_create_window(int width, int height)
{
	struct window *wnd;
	Display *display;
	Window window;
	int black;
	GC gc;

	display = XOpenDisplay(NULL);
	if (display == NULL)
		return NULL;

	black = BlackPixel(display, DefaultScreen(display));
	window = XCreateSimpleWindow(display, DefaultRootWindow(display),
				0, 0, width, height, 0, black, black);

	XMapWindow(display, window);
	XSync(display, True);

	gc = XCreateGC(display, window, 0, NULL);

	wnd = xmalloc(sizeof(struct window));
	wnd->display = display;
	wnd->wnd = window;
	wnd->gc = gc;
	return wnd;
}

void ui_destroy_window(HWINDOW wnd)
{
	XFreeGC(wnd->display, wnd->gc);
	XDestroyWindow(wnd->display, wnd->wnd);
	XCloseDisplay(wnd->display);
}

HBITMAP ui_create_bitmap(HWINDOW wnd, int width, int height, uint8 *data)
{
	XImage *image;
	Visual *visual;

	visual = DefaultVisual(wnd->display, DefaultScreen(wnd->display));
	image = XCreateImage(wnd->display, visual, 8, ZPixmap, 0,
				data, width, height, 32, width);

	return (HBITMAP)image;
}

void ui_destroy_bitmap(HBITMAP bmp)
{
	XDestroyImage((XImage *)bmp);
}

void ui_paint_bitmap(HWINDOW wnd, HBITMAP bmp, int x, int y)
{
	XImage *image = (XImage *)bmp;

	XPutImage(wnd->display, wnd->wnd, wnd->gc, image,
			0, 0, x, y, image->width, image->height);

	XSync(wnd->display, True);
}
