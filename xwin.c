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

HWINDOW ui_create_window(HCONN conn, int width, int height)
{
	struct window *wnd;
	XSetWindowAttributes attribs;
	Display *display;
	Visual *visual;
	Window window;
	int black;
	GC gc;

	display = XOpenDisplay(NULL);
	if (display == NULL)
		return NULL;

	visual = DefaultVisual(display, DefaultScreen(display));
	black = BlackPixel(display, DefaultScreen(display));

	attribs.background_pixel = black;
	attribs.backing_store = Always;
	window = XCreateWindow(display, DefaultRootWindow(display), 0, 0,
			width, height, 0, 8, InputOutput, visual,
			CWBackingStore | CWBackPixel, &attribs);

	XStoreName(display, window, "rdesktop");
	XMapWindow(display, window);
	XSelectInput(display, window, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
	XSync(display, True);

	gc = XCreateGC(display, window, 0, NULL);

	wnd = xmalloc(sizeof(struct window));
	wnd->conn = conn;
	wnd->width = width;
	wnd->height = height;
	wnd->display = display;
	wnd->wnd = window;
	wnd->gc = gc;
        wnd->visual = visual;

	return wnd;
}

void ui_destroy_window(HWINDOW wnd)
{
	XFreeGC(wnd->display, wnd->gc);
	XDestroyWindow(wnd->display, wnd->wnd);
	XCloseDisplay(wnd->display);
}

static uint8 xwin_translate_key(unsigned long key)
{
	DEBUG("KEY(code=0x%lx)\n", key);

	if ((key > 8) && (key <= 0x60))
		return (key - 8);

	switch (key)
	{
		case 0x62: /* left arrow */
			return 0x48;
		case 0x64: /* up arrow */
			return 0x4b;
		case 0x66: /* down arrow */
			return 0x4d;
		case 0x68: /* right arrow */
			return 0x50;
		case 0x73: /* Windows key */
			DEBUG("CHECKPOINT\n");
	}

	return 0;
}

static uint16 xwin_translate_mouse(unsigned long button)
{
	switch (button)
	{
		case Button1: /* left */
			return MOUSE_FLAG_BUTTON1;
		case Button2: /* middle */
			return MOUSE_FLAG_BUTTON3;
		case Button3: /* right */
			return MOUSE_FLAG_BUTTON2;
	}

	return 0;
}

void ui_process_events(HWINDOW wnd, HCONN conn)
{
	XEvent event;
	uint8 scancode;
	uint16 button;

	if (wnd == NULL)
		return;

	while (XCheckWindowEvent(wnd->display, wnd->wnd, 0xffffffff, &event))
	{
		switch (event.type)
		{
			case KeyPress:
				scancode = xwin_translate_key(event.xkey.keycode);
				if (scancode == 0)
					break;

				rdp_send_input(conn, RDP_INPUT_SCANCODE, 0,
						scancode, 0);
				break;

			case KeyRelease:
				scancode = xwin_translate_key(event.xkey.keycode);
				if (scancode == 0)
					break;

				rdp_send_input(conn, RDP_INPUT_SCANCODE,
						KBD_FLAG_DOWN | KBD_FLAG_UP,
						scancode, 0);
				break;

			case ButtonPress:
				button = xwin_translate_mouse(event.xbutton.button);

				if (button == 0)
					break;

				rdp_send_input(conn, RDP_INPUT_MOUSE,
						button | MOUSE_FLAG_DOWN,
						event.xbutton.x,
						event.xbutton.y);
				break;

			case ButtonRelease:
				button = xwin_translate_mouse(event.xbutton.button);
				if (button == 0)
					break;

				rdp_send_input(conn, RDP_INPUT_MOUSE,
						button,
						event.xbutton.x,
						event.xbutton.y);
		}
	}
}

void ui_move_pointer(HWINDOW wnd, int x, int y)
{
	XWarpPointer(wnd->display, wnd->wnd, wnd->wnd, 0, 0, 0, 0, x, y);
}

HBITMAP ui_create_bitmap(HWINDOW wnd, int width, int height, uint8 *data)
{
	XImage *image;
	Pixmap bitmap;

	bitmap = XCreatePixmap(wnd->display, wnd->wnd, width, height, 8);

	image = XCreateImage(wnd->display, wnd->visual, 8, ZPixmap, 0,
				data, width, height, 8, width);
	XSetFunction(wnd->display, wnd->gc, GXcopy);
	XPutImage(wnd->display, bitmap, wnd->gc, image, 0, 0, 0, 0,
			width, height);
	XFree(image);
	
	return (HBITMAP)bitmap;
}

void ui_destroy_bitmap(HWINDOW wnd, HBITMAP bmp)
{
	XFreePixmap(wnd->display, (Pixmap)bmp);
}

HGLYPH ui_create_glyph(HWINDOW wnd, int width, int height, uint8 *data)
{
	XImage *image;
	Pixmap bitmap;
	int scanline;
	GC gc;

	scanline = (width + 7) / 8;

	bitmap = XCreatePixmap(wnd->display, wnd->wnd, width, height, 1);
	gc = XCreateGC(wnd->display, bitmap, 0, NULL);

	image = XCreateImage(wnd->display, wnd->visual, 1, ZPixmap, 0,
				data, width, height, 8, scanline);
	XSetFunction(wnd->display, wnd->gc, GXcopy);
	XPutImage(wnd->display, bitmap, gc, image, 0, 0, 0, 0, width, height);
	XFree(image);
	XFreeGC(wnd->display, gc);
	
	return (HGLYPH)bitmap;
}

void ui_destroy_glyph(HWINDOW wnd, HGLYPH glyph)
{
	XFreePixmap(wnd->display, (Pixmap)glyph);
}

HCOLOURMAP ui_create_colourmap(HWINDOW wnd, COLOURMAP *colours)
{
	COLOURENTRY *entry;
	XColor *xcolours, *xentry;
	Colormap map;
	int i, ncolours = colours->ncolours;

	xcolours = malloc(sizeof(XColor) * ncolours);
	for (i = 0; i < ncolours; i++)
	{
		entry = &colours->colours[i];
		xentry = &xcolours[i];

		xentry->pixel = i;
		xentry->red = entry->red << 8;
		xentry->blue = entry->blue << 8;
		xentry->green = entry->green << 8;
		xentry->flags = DoRed | DoBlue | DoGreen;
	}

	map = XCreateColormap(wnd->display, wnd->wnd, wnd->visual, AllocAll);
	XStoreColors(wnd->display, map, xcolours, ncolours);

	free(xcolours);
	return (HCOLOURMAP)map;
}

void ui_destroy_colourmap(HWINDOW wnd, HCOLOURMAP map)
{
	XFreeColormap(wnd->display, (Colormap)map);
}

void ui_set_colourmap(HWINDOW wnd, HCOLOURMAP map)
{
	XSetWindowColormap(wnd->display, wnd->wnd, (Colormap)map);
}

void ui_set_clip(HWINDOW wnd, int x, int y, int cx, int cy)
{
	XRectangle rect;

	rect.x = x;
	rect.y = y;
	rect.width = cx;
	rect.height = cy;
	XSetClipRectangles(wnd->display, wnd->gc, 0, 0, &rect, 1, YXBanded);
}

void ui_reset_clip(HWINDOW wnd)
{
	XRectangle rect;

	rect.x = 0;
	rect.y = 0;
	rect.width = wnd->width;
	rect.height = wnd->height;
	XSetClipRectangles(wnd->display, wnd->gc, 0, 0, &rect, 1, YXBanded);
}

static int rop2_map[] = {
	GXclear,	/* 0 */
	GXnor,		/* DPon */
	GXandInverted,	/* DPna */
	GXcopyInverted,	/* Pn */
	GXandReverse,	/* PDna */
	GXinvert,	/* Dn */
	GXxor,		/* DPx */
	GXnand,		/* DPan */
	GXand,		/* DPa */
	GXequiv,	/* DPxn */
	GXnoop,		/* D */
	GXorInverted,	/* DPno */
	GXcopy,		/* P */
	GXorReverse,	/* PDno */
	GXor,		/* DPo */
	GXset		/* 1 */
};

static void xwin_set_function(HWINDOW wnd, uint8 rop2)
{
	XSetFunction(wnd->display, wnd->gc, rop2_map[rop2]);
}

void ui_destblt(HWINDOW wnd, uint8 opcode,
	/* dest */  int x, int y, int cx, int cy)
{
	xwin_set_function(wnd, opcode);

	XFillRectangle(wnd->display, wnd->wnd, wnd->gc, x, y, cx, cy);
}

void ui_patblt(HWINDOW wnd, uint8 opcode,
	/* dest */  int x, int y, int cx, int cy,
	/* brush */ BRUSH *brush, int bgcolour, int fgcolour)
{
	Display *dpy = wnd->display;
	GC gc = wnd->gc;
	Pixmap fill;

	xwin_set_function(wnd, opcode);

	switch (brush->style)
	{
		case 0: /* Solid */
			XSetForeground(dpy, gc, fgcolour);
			XFillRectangle(dpy, wnd->wnd, gc, x, y, cx, cy);
			break;

		case 3: /* Pattern */
			fill = (Pixmap)ui_create_glyph(wnd, 8, 8, brush->pattern);

			XSetForeground(dpy, gc, fgcolour);
			XSetBackground(dpy, gc, bgcolour);
			XSetFillStyle(dpy, gc, FillOpaqueStippled);
			XSetStipple(dpy, gc, fill);

			XFillRectangle(dpy, wnd->wnd, gc, x, y, cx, cy);

			XSetFillStyle(dpy, gc, FillSolid);
			ui_destroy_glyph(wnd, (HGLYPH)fill);
			break;

		default:
			NOTIMP("brush style %d\n", brush->style);
	}
}

void ui_screenblt(HWINDOW wnd, uint8 opcode,
		/* dest */ int x, int y, int cx, int cy,
		/* src */  int srcx, int srcy)
{
	xwin_set_function(wnd, opcode);

	XCopyArea(wnd->display, wnd->wnd, wnd->wnd, wnd->gc, srcx, srcy,
			cx, cy, x, y);
}

void ui_memblt(HWINDOW wnd, uint8 opcode,
	/* dest */  int x, int y, int cx, int cy,
	/* src */   HBITMAP src, int srcx, int srcy)
{
	xwin_set_function(wnd, opcode);

	XCopyArea(wnd->display, (Pixmap)src, wnd->wnd, wnd->gc, srcx, srcy,
			cx, cy, x, y);
}

void ui_triblt(HWINDOW wnd, uint8 opcode,
	/* dest */  int x, int y, int cx, int cy,
	/* src */   HBITMAP src, int srcx, int srcy,
	/* brush */ BRUSH *brush, int bgcolour, int fgcolour)
{
	/* This is potentially difficult to do in general. Until someone
	   comes up with an efficient way of doing that I am using cases. */

	switch (opcode)
	{
		case 0xb8: /* PSDPxax */
			ui_patblt(wnd, ROP2_XOR, x, y, cx, cy,
					brush, bgcolour, fgcolour);
			ui_memblt(wnd, ROP2_AND, x, y, cx, cy,
					src, srcx, srcy);
			ui_patblt(wnd, ROP2_XOR, x, y, cx, cy,
					brush, bgcolour, fgcolour);
			break;

		default:
			NOTIMP("triblt opcode 0x%x\n", opcode);
			ui_memblt(wnd, ROP2_COPY, x, y, cx, cy,
					brush, bgcolour, fgcolour);
	}
}

void ui_line(HWINDOW wnd, uint8 opcode,
	/* dest */  int startx, int starty, int endx, int endy,
	/* pen */   PEN *pen)
{
	xwin_set_function(wnd, opcode);

	XSetForeground(wnd->display, wnd->gc, pen->colour);
	XDrawLine(wnd->display, wnd->wnd, wnd->gc, startx, starty, endx, endy);
}

void ui_rect(HWINDOW wnd,
	/* dest */  int x, int y, int cx, int cy,
	/* brush */ int colour)
{
	xwin_set_function(wnd, ROP2_COPY);

	XSetForeground(wnd->display, wnd->gc, colour);
	XFillRectangle(wnd->display, wnd->wnd, wnd->gc, x, y, cx, cy);
}

void ui_draw_glyph(HWINDOW wnd, int mixmode,
	/* dest */ int x, int y, int cx, int cy,
	/* src */  HGLYPH glyph, int srcx, int srcy, int bgcolour, int fgcolour)
{
	Pixmap pixmap = (Pixmap)glyph;

	xwin_set_function(wnd, ROP2_COPY);

	XSetForeground(wnd->display, wnd->gc, fgcolour);

	switch (mixmode)
	{
		case MIX_TRANSPARENT:
			XSetStipple(wnd->display, wnd->gc, pixmap);
			XSetFillStyle(wnd->display, wnd->gc, FillStippled);
			XSetTSOrigin(wnd->display, wnd->gc, x, y);
			XFillRectangle(wnd->display, wnd->wnd, wnd->gc,
					x, y, cx, cy);
			XSetFillStyle(wnd->display, wnd->gc, FillSolid);
			break;

		case MIX_OPAQUE:
			XSetBackground(wnd->display, wnd->gc, bgcolour);
			XCopyPlane(wnd->display, pixmap, wnd->wnd, wnd->gc,
					srcx, srcy, cx, cy, x, y, 1);
			break;

		default:
			NOTIMP("mix mode %d\n", mixmode);
	}
}

void ui_draw_text(HWINDOW wnd, uint8 font, uint8 flags, int mixmode, int x,
			int y, int boxx, int boxy, int boxcx, int boxcy,
			int bgcolour, int fgcolour, uint8 *text, uint8 length)
{
	FONT_GLYPH *glyph;
	int i;

	if (boxcx > 1)
	{
		ui_rect(wnd, boxx, boxy, boxcx, boxcy, bgcolour);
	}

	/* Paint text, character by character */
	for (i = 0; i < length; i++)
	{
		glyph = cache_get_font(wnd->conn, font, text[i]);

		if (glyph != NULL)
		{
			ui_draw_glyph(wnd, mixmode, x,
					y + (short)glyph->baseline,
					glyph->width, glyph->height,
					glyph->pixmap, 0, 0,
					bgcolour, fgcolour);

			if (flags & TEXT2_IMPLICIT_X)
				x += glyph->width;
			else
				x += text[++i];
		}
	}
}

void ui_desktop_save(HWINDOW wnd, uint8 *data, int x, int y, int cx, int cy)
{
	XImage *image;
	int scanline;

	scanline = (cx + 3) & ~3;
	image = XGetImage(wnd->display, wnd->wnd, x, y, cx, cy,
				0xffffffff, ZPixmap);
	memcpy(data, image->data, scanline*cy);
	XDestroyImage(image);
}

void ui_desktop_restore(HWINDOW wnd, uint8 *data, int x, int y, int cx, int cy)
{
	XImage *image;
	int scanline;

	scanline = (cx + 3) & ~3;
	image = XCreateImage(wnd->display, wnd->visual, 8, ZPixmap, 0,
				data, cx, cy, 32, scanline);
	XSetFunction(wnd->display, wnd->gc, GXcopy);
	XPutImage(wnd->display, wnd->wnd, wnd->gc, image, 0, 0, x, y, cx, cy);
	XFree(image);
}
