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
        wnd->visual = DefaultVisual(wnd->display, DefaultScreen(wnd->display));

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

	image = XCreateImage(wnd->display, wnd->visual, 8, ZPixmap, 0,
				data, width, height, 32, width);

	return (HBITMAP)image;
}

void ui_destroy_bitmap(HWINDOW wnd, HBITMAP bmp)
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

HCOLORMAP ui_create_colormap(HWINDOW wnd, COLORMAP *colors)
{
	COLORENTRY *entry;
	XColor *xcolors, *xentry;
	Colormap map;
	int i, ncolors = colors->ncolors;

	xcolors = malloc(sizeof(XColor) * ncolors);
	for (i = 0; i < ncolors; i++)
	{
		entry = &colors->colors[i];
		xentry = &xcolors[i];

		xentry->pixel = i;
		xentry->red = entry->red << 8;
		xentry->blue = entry->blue << 8;
		xentry->green = entry->green << 8;
		xentry->flags = DoRed | DoBlue | DoGreen;
	}

	map = XCreateColormap(wnd->display, wnd->wnd, wnd->visual, AllocAll);
	XStoreColors(wnd->display, map, xcolors, ncolors);

	free(xcolors);
	return (HCOLORMAP)map;
}

void ui_destroy_colormap(HWINDOW wnd, HCOLORMAP map)
{
	XFreeColormap(wnd->display, (Colormap)map);
}

void ui_set_colormap(HWINDOW wnd, HCOLORMAP map)
{
	XSetWindowColormap(wnd->display, wnd->wnd, (Colormap)map);
}

void ui_draw_rectangle(HWINDOW wnd, int x, int y, int width, int height)
{
	static int white = 0;

	XSetForeground(wnd->display, wnd->gc, white);
	XFillRectangle(wnd->display, wnd->wnd, wnd->gc, x, y, width, height);

	white++;
}

void ui_move_pointer(HWINDOW wnd, int x, int y)
{
	XWarpPointer(wnd->display, wnd->wnd, wnd->wnd, 0, 0, 0, 0, x, y);
}
