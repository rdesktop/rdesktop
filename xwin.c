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

#include <X11/Xlib.h>
#include <time.h>
#include "rdesktop.h"

extern int width;
extern int height;
extern BOOL motion;

static Display *display;
static Window wnd;
static GC gc;
static Visual *visual;
static XIM IM;

BOOL ui_create_window(char *title)
{
	Screen *screen;
	XSetWindowAttributes attribs;
	unsigned long input_mask;
	int i;

	display = XOpenDisplay(NULL);
	if (display == NULL)
		return False;

	/* Check the screen supports 8-bit depth. */
	screen = DefaultScreenOfDisplay(display);
	for (i = 0; i < screen->ndepths; i++)
		if (screen->depths[i].depth == 8)
			break;

	if (i >= screen->ndepths)
	{
		ERROR("8-bit depth required (in this version).\n");
		XCloseDisplay(display);
		return False;
	}

	visual = DefaultVisual(display, DefaultScreen(display));

	attribs.background_pixel = BlackPixel(display, DefaultScreen(display));
	attribs.backing_store = Always;
	wnd = XCreateWindow(display, DefaultRootWindow(display),
			0, 0, width, height, 0, 8, InputOutput, visual,
			CWBackingStore | CWBackPixel, &attribs);

	XStoreName(display, wnd, title);
	XMapWindow(display, wnd);

	input_mask  = KeyPressMask | KeyReleaseMask;
	input_mask |= ButtonPressMask | ButtonReleaseMask;
	if (motion)
		input_mask |= PointerMotionMask;

	XSelectInput(display, wnd, input_mask);
	gc = XCreateGC(display, wnd, 0, NULL);

	IM = XOpenIM(display, NULL, NULL, NULL);
	return True;
}

void ui_destroy_window()
{
	XCloseIM(IM);
	XFreeGC(display, gc);
	XDestroyWindow(display, wnd);
	XCloseDisplay(display);
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

void ui_process_events()
{
	XEvent event;
	uint8 scancode;
	uint16 button;
	uint32 ev_time;

	if (display == NULL)
		return;

	while (XCheckWindowEvent(display, wnd, 0xffffffff, &event))
	{
		ev_time = time(NULL);

		switch (event.type)
		{
			case KeyPress:
				scancode = xwin_translate_key(event.xkey.keycode);
				if (scancode == 0)
					break;

				rdp_send_input(ev_time, RDP_INPUT_SCANCODE, 0,
						scancode, 0);
				break;

			case KeyRelease:
				scancode = xwin_translate_key(event.xkey.keycode);
				if (scancode == 0)
					break;

				rdp_send_input(ev_time, RDP_INPUT_SCANCODE,
						KBD_FLAG_DOWN | KBD_FLAG_UP,
						scancode, 0);
				break;

			case ButtonPress:
				button = xwin_translate_mouse(event.xbutton.button);

				if (button == 0)
					break;

				rdp_send_input(ev_time, RDP_INPUT_MOUSE,
						button | MOUSE_FLAG_DOWN,
						event.xbutton.x,
						event.xbutton.y);
				break;

			case ButtonRelease:
				button = xwin_translate_mouse(event.xbutton.button);
				if (button == 0)
					break;

				rdp_send_input(ev_time, RDP_INPUT_MOUSE,
						button,
						event.xbutton.x,
						event.xbutton.y);
				break;

			case MotionNotify:
				rdp_send_input(ev_time, RDP_INPUT_MOUSE,
						MOUSE_FLAG_MOVE,
						event.xmotion.x,
						event.xmotion.y);
		}
	}
}

void ui_move_pointer(int x, int y)
{
	XWarpPointer(display, wnd, wnd, 0, 0, 0, 0, x, y);
}

HBITMAP ui_create_bitmap(int width, int height, uint8 *data)
{
	XImage *image;
	Pixmap bitmap;

	bitmap = XCreatePixmap(display, wnd, width, height, 8);

	image = XCreateImage(display, visual, 8, ZPixmap, 0,
				data, width, height, 8, width);
	XSetFunction(display, gc, GXcopy);
	XPutImage(display, bitmap, gc, image, 0, 0, 0, 0, width, height);
	XFree(image);
	
	return (HBITMAP)bitmap;
}

void ui_paint_bitmap(int x, int y, int cx, int cy,
			int width, int height, uint8 *data)
{
	XImage *image;

	image = XCreateImage(display, visual, 8, ZPixmap, 0,
				data, width, height, 8, width);
	XSetFunction(display, gc, GXcopy);
	XPutImage(display, wnd, gc, image, 0, 0, x, y, cx, cy);
	XFree(image); 
}

void ui_destroy_bitmap(HBITMAP bmp)
{
	XFreePixmap(display, (Pixmap)bmp);
}

HGLYPH ui_create_glyph(int width, int height, uint8 *data)
{
	XImage *image;
	Pixmap bitmap;
	int scanline;
	GC gc;

	scanline = (width + 7) / 8;

	bitmap = XCreatePixmap(display, wnd, width, height, 1);
	gc = XCreateGC(display, bitmap, 0, NULL);

	image = XCreateImage(display, visual, 1, ZPixmap, 0,
				data, width, height, 8, scanline);
	XSetFunction(display, gc, GXcopy);
	XPutImage(display, bitmap, gc, image, 0, 0, 0, 0, width, height);
	XFree(image);
	XFreeGC(display, gc);
	
	return (HGLYPH)bitmap;
}

void ui_destroy_glyph(HGLYPH glyph)
{
	XFreePixmap(display, (Pixmap)glyph);
}

HCOLOURMAP ui_create_colourmap(COLOURMAP *colours)
{
	COLOURENTRY *entry;
	XColor *xcolours, *xentry;
	Colormap map;
	int i, ncolours = colours->ncolours;

	xcolours = xmalloc(sizeof(XColor) * ncolours);
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

	map = XCreateColormap(display, wnd, visual, AllocAll);
	XStoreColors(display, map, xcolours, ncolours);

	xfree(xcolours);
	return (HCOLOURMAP)map;
}

void ui_destroy_colourmap(HCOLOURMAP map)
{
	XFreeColormap(display, (Colormap)map);
}

void ui_set_colourmap(HCOLOURMAP map)
{
	XSetWindowColormap(display, wnd, (Colormap)map);
}

void ui_set_clip(int x, int y, int cx, int cy)
{
	XRectangle rect;

	rect.x = x;
	rect.y = y;
	rect.width = cx;
	rect.height = cy;
	XSetClipRectangles(display, gc, 0, 0, &rect, 1, YXBanded);
}

void ui_reset_clip()
{
	XRectangle rect;

	rect.x = 0;
	rect.y = 0;
	rect.width = width;
	rect.height = height;
	XSetClipRectangles(display, gc, 0, 0, &rect, 1, YXBanded);
}

void ui_bell()
{
	XBell(display, 0);
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

static void xwin_set_function(uint8 rop2)
{
	XSetFunction(display, gc, rop2_map[rop2]);
}

void ui_destblt(uint8 opcode,
	/* dest */  int x, int y, int cx, int cy)
{
	xwin_set_function(opcode);

	XFillRectangle(display, wnd, gc, x, y, cx, cy);
}

void ui_patblt(uint8 opcode,
	/* dest */  int x, int y, int cx, int cy,
	/* brush */ BRUSH *brush, int bgcolour, int fgcolour)
{
	Display *dpy = display;
	Pixmap fill;

	xwin_set_function(opcode);

	switch (brush->style)
	{
		case 0: /* Solid */
			XSetForeground(dpy, gc, fgcolour);
			XFillRectangle(dpy, wnd, gc, x, y, cx, cy);
			break;

		case 3: /* Pattern */
			fill = (Pixmap)ui_create_glyph(8, 8, brush->pattern);

			XSetForeground(dpy, gc, fgcolour);
			XSetBackground(dpy, gc, bgcolour);
			XSetFillStyle(dpy, gc, FillOpaqueStippled);
			XSetStipple(dpy, gc, fill);

			XFillRectangle(dpy, wnd, gc, x, y, cx, cy);

			XSetFillStyle(dpy, gc, FillSolid);
			ui_destroy_glyph((HGLYPH)fill);
			break;

		default:
			NOTIMP("brush %d\n", brush->style);
	}
}

void ui_screenblt(uint8 opcode,
		/* dest */ int x, int y, int cx, int cy,
		/* src */  int srcx, int srcy)
{
	xwin_set_function(opcode);

	XCopyArea(display, wnd, wnd, gc, srcx, srcy,
			cx, cy, x, y);
}

void ui_memblt(uint8 opcode,
	/* dest */  int x, int y, int cx, int cy,
	/* src */   HBITMAP src, int srcx, int srcy)
{
	xwin_set_function(opcode);

	XCopyArea(display, (Pixmap)src, wnd, gc, srcx, srcy,
			cx, cy, x, y);
}

void ui_triblt(uint8 opcode,
	/* dest */  int x, int y, int cx, int cy,
	/* src */   HBITMAP src, int srcx, int srcy,
	/* brush */ BRUSH *brush, int bgcolour, int fgcolour)
{
	/* This is potentially difficult to do in general. Until someone
	   comes up with a more efficient way of doing it I am using cases. */

	switch (opcode)
	{
		case 0x69: /* PDSxxn */
			ui_memblt(ROP2_XOR, x, y, cx, cy, src, srcx, srcy);
			ui_patblt(ROP2_NXOR, x, y, cx, cy,
					brush, bgcolour, fgcolour);
			break;

		case 0xb8: /* PSDPxax */
			ui_patblt(ROP2_XOR, x, y, cx, cy,
					brush, bgcolour, fgcolour);
			ui_memblt(ROP2_AND, x, y, cx, cy, src, srcx, srcy);
			ui_patblt(ROP2_XOR, x, y, cx, cy,
					brush, bgcolour, fgcolour);
			break;

		default:
			NOTIMP("triblt 0x%x\n", opcode);
			ui_memblt(ROP2_COPY, x, y, cx, cy, src, srcx, srcy);
	}
}

void ui_line(uint8 opcode,
	/* dest */  int startx, int starty, int endx, int endy,
	/* pen */   PEN *pen)
{
	xwin_set_function(opcode);

	XSetForeground(display, gc, pen->colour);
	XDrawLine(display, wnd, gc, startx, starty, endx, endy);
}

void ui_rect(
	/* dest */  int x, int y, int cx, int cy,
	/* brush */ int colour)
{
	xwin_set_function(ROP2_COPY);

	XSetForeground(display, gc, colour);
	XFillRectangle(display, wnd, gc, x, y, cx, cy);
}

void ui_draw_glyph(int mixmode,
	/* dest */ int x, int y, int cx, int cy,
	/* src */  HGLYPH glyph, int srcx, int srcy, int bgcolour, int fgcolour)
{
	Pixmap pixmap = (Pixmap)glyph;

	xwin_set_function(ROP2_COPY);

	XSetForeground(display, gc, fgcolour);

	switch (mixmode)
	{
		case MIX_TRANSPARENT:
			XSetStipple(display, gc, pixmap);
			XSetFillStyle(display, gc, FillStippled);
			XSetTSOrigin(display, gc, x, y);
			XFillRectangle(display, wnd, gc,
					x, y, cx, cy);
			XSetFillStyle(display, gc, FillSolid);
			break;

		case MIX_OPAQUE:
			XSetBackground(display, gc, bgcolour);
			XCopyPlane(display, pixmap, wnd, gc,
					srcx, srcy, cx, cy, x, y, 1);
			break;

		default:
			NOTIMP("mix %d\n", mixmode);
	}
}

void ui_draw_text(uint8 font, uint8 flags, int mixmode, int x, int y,
			int clipx, int clipy, int clipcx, int clipcy,
			int boxx, int boxy, int boxcx, int boxcy,
			int bgcolour, int fgcolour, uint8 *text, uint8 length)
{
	FONTGLYPH *glyph;
	int i;

	if (boxcx > 1)
	{
		ui_rect(boxx, boxy, boxcx, boxcy, bgcolour);
	}
	else if (mixmode == MIX_OPAQUE)
	{
		ui_rect(clipx, clipy, clipcx, clipcy, bgcolour);
	}

	/* Paint text, character by character */
	for (i = 0; i < length; i++)
	{
		glyph = cache_get_font(font, text[i]);

		if (!(flags & TEXT2_IMPLICIT_X))
			x += text[++i];

		if (glyph != NULL)
		{
			ui_draw_glyph(mixmode, x,
					y + (short)glyph->baseline,
					glyph->width, glyph->height,
					glyph->pixmap, 0, 0,
					bgcolour, fgcolour);

			if (flags & TEXT2_IMPLICIT_X)
				x += glyph->width;
		}
	}
}

void ui_desktop_save(uint32 offset, int x, int y, int cx, int cy)
{
	XImage *image;

	image = XGetImage(display, wnd, x, y, cx, cy, 0xffffffff, ZPixmap);
	cache_put_desktop(offset, cx, cy, image->bytes_per_line, image->data);
	XFree(image->data);
	XFree(image);
}

void ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy)
{
	XImage *image;
	uint8 *data;

	data = cache_get_desktop(offset, cx, cy);
	if (data == NULL)
		return;

	image = XCreateImage(display, visual, 8, ZPixmap, 0,
				data, cx, cy, 32, cx);
	XSetFunction(display, gc, GXcopy);
	XPutImage(display, wnd, gc, image, 0, 0, x, y, cx, cy);
	XFree(image);
}
