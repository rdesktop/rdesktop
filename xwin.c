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
#include <X11/Xutil.h>
#include <time.h>
#include "rdesktop.h"

extern int width;
extern int height;
extern BOOL motion;
extern BOOL grab_keyboard;
extern BOOL fullscreen;
extern int private_colormap;

static int bpp;
static int depth;
static Display *display;
static Window wnd;
static GC gc;
static Visual *visual;
static uint32 *colmap;

#define Ctrans(col) ( private_colormap ? col : colmap[col])

#define L_ENDIAN
int screen_msbfirst = 0;

static uint8 *translate(int width, int height, uint8 *data);

static int rop2_map[] = {
	GXclear,		/* 0 */
	GXnor,			/* DPon */
	GXandInverted,		/* DPna */
	GXcopyInverted,		/* Pn */
	GXandReverse,		/* PDna */
	GXinvert,		/* Dn */
	GXxor,			/* DPx */
	GXnand,			/* DPan */
	GXand,			/* DPa */
	GXequiv,		/* DPxn */
	GXnoop,			/* D */
	GXorInverted,		/* DPno */
	GXcopy,			/* P */
	GXorReverse,		/* PDno */
	GXor,			/* DPo */
	GXset			/* 1 */
};

static void
xwin_set_function(uint8 rop2)
{
	static uint8 last_rop2 = ROP2_COPY;

	if (last_rop2 != rop2)
	{
		XSetFunction(display, gc, rop2_map[rop2]);
		last_rop2 = rop2;
	}
}

static void
xwin_grab_keyboard()
{
	XGrabKeyboard(display, wnd, True, GrabModeAsync, GrabModeAsync,
		      CurrentTime);
}

static void
xwin_ungrab_keyboard()
{
	XUngrabKeyboard(display, CurrentTime);
}

BOOL
ui_create_window(char *title)
{
	XSetWindowAttributes attribs;
	XClassHint *classhints;
	XSizeHints *sizehints;
	unsigned long input_mask;
	XPixmapFormatValues *pfm;
	int count;

	display = XOpenDisplay(NULL);
	if (display == NULL)
	{
		ERROR("Failed to open display\n");
		return False;
	}

	visual = DefaultVisual(display, DefaultScreen(display));
	depth = DefaultDepth(display, DefaultScreen(display));
	pfm = XListPixmapFormats(display, &count);
	if (pfm != NULL)
	{
		while (count--)
		{
			if ((pfm + count)->depth == depth
			    && (pfm + count)->bits_per_pixel > bpp)
			{
				bpp = (pfm + count)->bits_per_pixel;
			}
		}
		XFree(pfm);
	}

	if (bpp < 8)
	{
		ERROR("Less than 8 bpp not currently supported.\n");
		XCloseDisplay(display);
		return False;
	}

	width &= ~3; /* make width nicely divisible */

	attribs.background_pixel = BlackPixel(display, DefaultScreen(display));
	attribs.backing_store = Always;

	if (fullscreen)
	{
		attribs.override_redirect = True;
		width = WidthOfScreen(DefaultScreenOfDisplay(display));
		height = HeightOfScreen(DefaultScreenOfDisplay(display));
		XSetInputFocus(display, PointerRoot, RevertToPointerRoot,
			       CurrentTime);
	}
	else
	{
		attribs.override_redirect = False;
	}

	wnd = XCreateWindow(display, DefaultRootWindow(display),
			    0, 0, width, height, 0, CopyFromParent,
			    InputOutput, CopyFromParent,
			    CWBackingStore | CWBackPixel | CWOverrideRedirect,
			    &attribs);

	XStoreName(display, wnd, title);

	classhints = XAllocClassHint();
	if (classhints != NULL)

	{
		classhints->res_name = "rdesktop";
		classhints->res_class = "rdesktop";
		XSetClassHint(display, wnd, classhints);
		XFree(classhints);
	}

	sizehints = XAllocSizeHints();
	if (sizehints)
	{
		sizehints->flags =
			PPosition | PSize | PMinSize | PMaxSize | PBaseSize;
		sizehints->min_width = width;
		sizehints->max_width = width;
		sizehints->min_height = height;
		sizehints->max_height = height;
		sizehints->base_width = width;
		sizehints->base_height = height;
		XSetWMNormalHints(display, wnd, sizehints);
		XFree(sizehints);
	}

	input_mask = KeyPressMask | KeyReleaseMask;
	input_mask |= ButtonPressMask | ButtonReleaseMask;
	if (motion)
		input_mask |= PointerMotionMask;
	if (grab_keyboard)
		input_mask |= EnterWindowMask | LeaveWindowMask;

	XSelectInput(display, wnd, input_mask);
	gc = XCreateGC(display, wnd, 0, NULL);

	XMapWindow(display, wnd);
	return True;
}

void
ui_destroy_window()
{
	XFreeGC(display, gc);
	XDestroyWindow(display, wnd);
	XCloseDisplay(display);
	display = NULL;
}

static uint8
xwin_translate_key(unsigned long key)
{
	DEBUG("KEY(code=0x%lx)\n", key);

	if ((key > 8) && (key <= 0x60))
		return (key - 8);

	switch (key)
	{
		case 0x61:	/* home */
			return 0x47 | 0x80;
		case 0x62:	/* up arrow */
			return 0x48 | 0x80;
		case 0x63:	/* page up */
			return 0x49 | 0x80;
		case 0x64:	/* left arrow */
			return 0x4b | 0x80;
		case 0x66:	/* right arrow */
			return 0x4d | 0x80;
		case 0x67:	/* end */
			return 0x4f | 0x80;
		case 0x68:	/* down arrow */
			return 0x50 | 0x80;
		case 0x69:	/* page down */
			return 0x51 | 0x80;
		case 0x6a:	/* insert */
			return 0x52 | 0x80;
		case 0x6b:	/* delete */
			return 0x53 | 0x80;
		case 0x6c:	/* keypad enter */
			return 0x1c | 0x80;
		case 0x6d:	/* right ctrl */
			return 0x1d | 0x80;
		case 0x6f:	/* ctrl - print screen */
			return 0x37 | 0x80;
		case 0x70:	/* keypad '/' */
			return 0x35 | 0x80;
		case 0x71:	/* right alt */
			return 0x38 | 0x80;
		case 0x72:	/* ctrl break */
			return 0x46 | 0x80;
		case 0x73:	/* left window key */
			return 0xff;	/* real scancode is 5b */
		case 0x74:	/* right window key */
			return 0xff;	/* real scancode is 5c */
		case 0x75:	/* menu key */
			return 0x5d | 0x80;
	}

	return 0;
}

static uint16
xwin_translate_mouse(unsigned long button)
{
	switch (button)
	{
		case Button1:	/* left */
			return MOUSE_FLAG_BUTTON1;
		case Button2:	/* middle */
			return MOUSE_FLAG_BUTTON3;
		case Button3:	/* right */
			return MOUSE_FLAG_BUTTON2;
	}

	return 0;
}

void
ui_process_events()
{
	XEvent event;
	uint8 scancode;
	uint16 button;
	uint32 ev_time;

	if (display == NULL)
		return;

	while (XCheckWindowEvent(display, wnd, ~0, &event))
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
				break;

			case EnterNotify:
				if (grab_keyboard)
					xwin_grab_keyboard();
				break;

			case LeaveNotify:
				if (grab_keyboard)
					xwin_ungrab_keyboard();
				break;
		}
	}
}

void
ui_move_pointer(int x, int y)
{
	XWarpPointer(display, wnd, wnd, 0, 0, 0, 0, x, y);
}

HBITMAP
ui_create_bitmap(int width, int height, uint8 *data)
{
	XImage *image;
	Pixmap bitmap;
	uint8 *tdata;
	tdata = (private_colormap ? data : translate(width, height, data));
	bitmap = XCreatePixmap(display, wnd, width, height, depth);
	image =
		XCreateImage(display, visual,
			     depth, ZPixmap,
			     0, tdata, width, height, BitmapPad(display), 0);

	xwin_set_function(ROP2_COPY);
	XPutImage(display, bitmap, gc, image, 0, 0, 0, 0, width, height);

	XFree(image);
	if (!private_colormap)
		xfree(tdata);
	return (HBITMAP) bitmap;
}

void
ui_paint_bitmap(int x, int y, int cx, int cy,
		int width, int height, uint8 *data)
{
	XImage *image;
	uint8 *tdata =
		(private_colormap ? data : translate(width, height, data));
	image =
		XCreateImage(display, visual, depth, ZPixmap, 0, tdata, width,
			     height, BitmapPad(display), 0);

	xwin_set_function(ROP2_COPY);

	/* Window */
	XPutImage(display, wnd, gc, image, 0, 0, x, y, cx, cy);
	XFree(image);
	if (!private_colormap)
		xfree(tdata);
}

void
ui_destroy_bitmap(HBITMAP bmp)
{
	XFreePixmap(display, (Pixmap) bmp);
}

HCURSOR
ui_create_cursor(unsigned int x, unsigned int y, int width,
		 int height, uint8 *mask, uint8 *data)
{
	XImage *imagecursor;
	XImage *imagemask;
	Pixmap maskbitmap, cursorbitmap;
	Cursor cursor;
	XColor bg, fg;
	GC lgc;
	int i, x1, y1, scanlinelen;
	uint8 *cdata, *cmask;
	uint8 c;
	cdata = (uint8 *) malloc(sizeof(uint8) * width * height);
	if (!cdata)
		return NULL;
	scanlinelen = (width + 7) >> 3;
	cmask = (uint8 *) malloc(sizeof(uint8) * scanlinelen * height);
	if (!cmask)
	{
		free(cdata);
		return NULL;
	}
	i = (height - 1) * scanlinelen;

	if (!screen_msbfirst)
	{
		while (i >= 0)
		{
			for (x1 = 0; x1 < scanlinelen; x1++)
			{
				c = *(mask++);
				cmask[i + x1] =
					((c & 0x1) << 7) | ((c & 0x2) << 5) |
					((c & 0x4) << 3) | ((c & 0x8) << 1) |
					((c & 0x10) >> 1) | ((c & 0x20) >> 3)
					| ((c & 0x40) >> 5) | ((c & 0x80) >>
							       7);
			}
			i -= scanlinelen;
		}
	}
	else
	{
		while (i >= 0)
		{
			for (x1 = 0; x1 < scanlinelen; x1++)
			{
				cmask[i + x1] = *(mask++);
			}
			i -= scanlinelen;
		}
	}


	fg.red = 0;
	fg.blue = 0;
	fg.green = 0;
	fg.flags = DoRed | DoBlue | DoGreen;
	bg.red = 65535;
	bg.blue = 65535;
	bg.green = 65535;
	bg.flags = DoRed | DoBlue | DoGreen;
	maskbitmap = XCreatePixmap(display, wnd, width, height, 1);
	cursorbitmap = XCreatePixmap(display, wnd, width, height, 1);
	lgc = XCreateGC(display, maskbitmap, 0, NULL);
	XSetFunction(display, lgc, GXcopy);
	imagemask =
		XCreateImage(display, visual, 1, XYBitmap, 0, cmask, width,
			     height, 8, 0);
	imagecursor =
		XCreateImage(display, visual, 1, XYBitmap, 0, cdata, width,
			     height, 8, 0);
	for (y1 = height - 1; y1 >= 0; y1--)
		for (x1 = 0; x1 < width; x1++)
		{
			if (data[0] >= 0x80 || data[1] >= 0x80
			    || data[2] >= 0x80)
				if (XGetPixel(imagemask, x1, y1))

				{
					XPutPixel(imagecursor, x1, y1, 0);
					XPutPixel(imagemask, x1, y1, 0);	/* mask is blank for text cursor! */
				}

				else
					XPutPixel(imagecursor, x1, y1, 1);

			else
				XPutPixel(imagecursor, x1, y1,
					  XGetPixel(imagemask, x1, y1));
			data += 3;
		}
	XPutImage(display, maskbitmap, lgc, imagemask, 0, 0, 0, 0, width,
		  height);
	XPutImage(display, cursorbitmap, lgc, imagecursor, 0, 0, 0, 0, width,
		  height); XFree(imagemask);
	XFree(imagecursor);
	free(cmask);
	free(cdata);
	XFreeGC(display, lgc);
	cursor =
		XCreatePixmapCursor(display, cursorbitmap, maskbitmap, &fg,
				    &bg, x, y);
	XFreePixmap(display, maskbitmap);
	XFreePixmap(display, cursorbitmap);
	return (HCURSOR) cursor;
}

void
ui_set_cursor(HCURSOR cursor)
{
	XDefineCursor(display, wnd, (Cursor) cursor);
}

void
ui_destroy_cursor(HCURSOR cursor)
{
	XFreeCursor(display, (Cursor) cursor);
}

HGLYPH
ui_create_glyph(int width, int height, uint8 *data)
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
	image->byte_order = MSBFirst;
	image->bitmap_bit_order = MSBFirst;
	XInitImage(image);

	XSetFunction(display, gc, GXcopy);
	XPutImage(display, bitmap, gc, image, 0, 0, 0, 0, width, height);
	XFree(image);
	XFreeGC(display, gc);

	return (HGLYPH) bitmap;
}

void
ui_destroy_glyph(HGLYPH glyph)
{
	XFreePixmap(display, (Pixmap) glyph);
}

HCOLOURMAP
ui_create_colourmap(COLOURMAP *colours)
{
	if (!private_colormap)
	{
		COLOURENTRY *entry;
		int i, ncolours = colours->ncolours;
		uint32 *nc = xmalloc(sizeof(*colmap) * ncolours);
		for (i = 0; i < ncolours; i++)
		{
			XColor xc;
			entry = &colours->colours[i];
			xc.red = entry->red << 8;
			xc.green = entry->green << 8;
			xc.blue = entry->blue << 8;
			XAllocColor(display,
				    DefaultColormap(display,
						    DefaultScreen(display)),
				    &xc);
			/* XXX Check return value */
			nc[i] = xc.pixel;
		}
		return nc;
	}
	else
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
		return (HCOLOURMAP) map;
	}
}

void
ui_destroy_colourmap(HCOLOURMAP map)
{
	XFreeColormap(display, (Colormap) map);
}

void
ui_set_colourmap(HCOLOURMAP map)
{

	/* XXX, change values of all pixels on the screen if the new colmap
	 * doesn't have the same values as the old one? */
	if (!private_colormap)
		colmap = map;
	else
	{
		XSetWindowColormap(display, wnd, (Colormap) map);
		if (fullscreen)
			XInstallColormap(display, (Colormap) map);
	}
}

void
ui_set_clip(int x, int y, int cx, int cy)
{
	XRectangle rect;

	rect.x = x;
	rect.y = y;
	rect.width = cx;
	rect.height = cy;
	XSetClipRectangles(display, gc, 0, 0, &rect, 1, YXBanded);
}

void
ui_reset_clip()
{
	XRectangle rect;

	rect.x = 0;
	rect.y = 0;
	rect.width = width;
	rect.height = height;
	XSetClipRectangles(display, gc, 0, 0, &rect, 1, YXBanded);
}

void
ui_bell()
{
	XBell(display, 0);
}

void
ui_destblt(uint8 opcode,
	   /* dest */ int x, int y, int cx, int cy)
{
	xwin_set_function(opcode);

	XFillRectangle(display, wnd, gc, x, y, cx, cy);
}

void
ui_patblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* brush */ BRUSH *brush, int bgcolour, int fgcolour)
{
	Display *dpy = display;
	Pixmap fill;
	uint8 i, ipattern[8];

	xwin_set_function(opcode);

	switch (brush->style)
	{
		case 0:	/* Solid */
			XSetForeground(dpy, gc, Ctrans(fgcolour));
			XFillRectangle(dpy, wnd, gc, x, y, cx, cy);
			break;

		case 3:	/* Pattern */
			for (i = 0; i != 8; i++)
				ipattern[i] = ~brush->pattern[i];
			fill = (Pixmap) ui_create_glyph(8, 8, ipattern);

			XSetForeground(dpy, gc, Ctrans(fgcolour));
			XSetBackground(dpy, gc, Ctrans(bgcolour));
			XSetFillStyle(dpy, gc, FillOpaqueStippled);
			XSetStipple(dpy, gc, fill);

			XFillRectangle(dpy, wnd, gc, x, y, cx, cy);

			XSetFillStyle(dpy, gc, FillSolid);
			ui_destroy_glyph((HGLYPH) fill);
			break;

		default:
			NOTIMP("brush %d\n", brush->style);
	}
}

void
ui_screenblt(uint8 opcode,
	     /* dest */ int x, int y, int cx, int cy,
	     /* src */ int srcx, int srcy)
{
	xwin_set_function(opcode);

	XCopyArea(display, wnd, wnd, gc, srcx, srcy, cx, cy, x, y);
}

void
ui_memblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* src */ HBITMAP src, int srcx, int srcy)
{
	xwin_set_function(opcode);

	XCopyArea(display, (Pixmap) src, wnd, gc, srcx, srcy, cx, cy, x, y);
}

void
ui_triblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* src */ HBITMAP src, int srcx, int srcy,
	  /* brush */ BRUSH *brush, int bgcolour, int fgcolour)
{
	/* This is potentially difficult to do in general. Until someone
	   comes up with a more efficient way of doing it I am using cases. */

	switch (opcode)
	{
		case 0x69:	/* PDSxxn */
			ui_memblt(ROP2_XOR, x, y, cx, cy, src, srcx, srcy);
			ui_patblt(ROP2_NXOR, x, y, cx, cy,
				  brush, bgcolour, fgcolour);
			break;

		case 0xb8:	/* PSDPxax */
			ui_patblt(ROP2_XOR, x, y, cx, cy,
				  brush, bgcolour, fgcolour);
			ui_memblt(ROP2_AND, x, y, cx, cy, src, srcx, srcy);
			ui_patblt(ROP2_XOR, x, y, cx, cy,
				  brush, bgcolour, fgcolour);
			break;

		case 0xc0:
			ui_memblt(ROP2_COPY, x, y, cx, cy, src, srcx, srcy);
			ui_patblt(ROP2_AND, x, y, cx, cy, brush, bgcolour,
				  fgcolour);
			break;

		default:
			NOTIMP("triblt 0x%x\n", opcode);
			ui_memblt(ROP2_COPY, x, y, cx, cy, src, srcx, srcy);
	}
}

void
ui_line(uint8 opcode,
	/* dest */ int startx, int starty, int endx, int endy,
	/* pen */ PEN *pen)
{
	xwin_set_function(opcode);

	XSetForeground(display, gc, Ctrans(pen->colour));
	XDrawLine(display, wnd, gc, startx, starty, endx, endy);
}

void
ui_rect(
	       /* dest */ int x, int y, int cx, int cy,
	       /* brush */ int colour)
{
	xwin_set_function(ROP2_COPY);

	XSetForeground(display, gc, Ctrans(colour));
	XFillRectangle(display, wnd, gc, x, y, cx, cy);
}

void
ui_draw_glyph(int mixmode,
	      /* dest */ int x, int y, int cx, int cy,
	      /* src */ HGLYPH glyph, int srcx, int srcy, int bgcolour,
	      int fgcolour)
{
	Pixmap pixmap = (Pixmap) glyph;

	xwin_set_function(ROP2_COPY);


	XSetForeground(display, gc, Ctrans(fgcolour));
	switch (mixmode)
	{
		case MIX_TRANSPARENT:
			XSetStipple(display, gc, pixmap);
			XSetFillStyle(display, gc, FillStippled);
			XSetTSOrigin(display, gc, x, y);
			XFillRectangle(display, wnd, gc, x, y, cx, cy);
			XSetFillStyle(display, gc, FillSolid);
			break;

		case MIX_OPAQUE:
			XSetBackground(display, gc, Ctrans(bgcolour));
/*      XCopyPlane (display, pixmap, back_pixmap, back_gc, srcx, srcy, cx, cy, x, y, 1); */
			XSetStipple(display, gc, pixmap);
			XSetFillStyle(display, gc, FillOpaqueStippled);
			XSetTSOrigin(display, gc, x, y);
			XFillRectangle(display, wnd, gc, x, y, cx, cy);
			XSetFillStyle(display, gc, FillSolid);
			break;

		default:
			NOTIMP("mix %d\n", mixmode);
	}
}

void
ui_draw_text(uint8 font, uint8 flags, int mixmode, int x, int y,
	     int clipx, int clipy, int clipcx, int clipcy,
	     int boxx, int boxy, int boxcx, int boxcy,
	     int bgcolour, int fgcolour, uint8 *text, uint8 length)
{
	FONTGLYPH *glyph;
	int i, xyoffset;

	xwin_set_function(ROP2_COPY);
	XSetForeground(display, gc, Ctrans(bgcolour));

	if (boxcx > 1)
		XFillRectangle(display, wnd, gc, boxx, boxy, boxcx, boxcy);
	else if (mixmode == MIX_OPAQUE)
		XFillRectangle(display, wnd, gc, clipx, clipy, clipcx, clipcy);

	/* Paint text, character by character */
	for (i = 0; i < length; i++)
	{
		glyph = cache_get_font(font, text[i]);

		if (!(flags & TEXT2_IMPLICIT_X))

		{
			xyoffset = text[++i];
			if ((xyoffset & 0x80))
			{
				if (flags & 0x04)	/* vertical text */
					y += text[++i] | (text[++i] << 8);
				else
					x += text[++i] | (text[++i] << 8);
			}
			else
			{
				if (flags & 0x04)	/* vertical text */
					y += xyoffset;
				else
					x += xyoffset;
			}

		}
		if (glyph != NULL)
		{
			ui_draw_glyph(mixmode, x + (short) glyph->offset,
				      y + (short) glyph->baseline,
				      glyph->width, glyph->height,
				      glyph->pixmap, 0, 0,
				      bgcolour, fgcolour);

			if (flags & TEXT2_IMPLICIT_X)
				x += glyph->width;
		}
	}
}

void
ui_desktop_save(uint32 offset, int x, int y, int cx, int cy)
{
	Pixmap pix;
	XImage *image;

	pix = XCreatePixmap(display, wnd, cx, cy, depth);
	xwin_set_function(ROP2_COPY);

	XCopyArea(display, wnd, pix, gc, x, y, cx, cy, 0, 0);
	image = XGetImage(display, pix, 0, 0, cx, cy, AllPlanes, ZPixmap);

	offset *= bpp/8;
	cache_put_desktop(offset, cx, cy, image->bytes_per_line,
			  bpp/8, image->data);

	XDestroyImage(image);
	XFreePixmap(display, pix);
}

void
ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy)
{
	XImage *image;
	uint8 *data;

	offset *= bpp/8;
	data = cache_get_desktop(offset, cx, cy, bpp/8);
	if (data == NULL)
		return;
	image =
		XCreateImage(display, visual,
			     depth, ZPixmap,
			     0, data, cx, cy, BitmapPad(display),
			     cx * bpp/8);
	xwin_set_function(ROP2_COPY);
	XPutImage(display, wnd, gc, image, 0, 0, x, y, cx, cy);
	XFree(image);
}

/* unroll defines, used to make the loops a bit more readable... */
#define unroll8Expr(uexp) uexp uexp uexp uexp uexp uexp uexp uexp
#define unroll8Lefts(uexp) case 7: uexp \
	case 6: uexp \
	case 5: uexp \
	case 4: uexp \
	case 3: uexp \
	case 2: uexp \
	case 1: uexp

static uint8 *
translate(int width, int height, uint8 *data)
{
	uint32 i;
	uint32 size = width * height;
	uint8 *d2 = xmalloc(size * bpp/8);
	uint8 *d3 = d2;
	uint32 pix;
	i = (size & ~0x7);

	/* XXX: where are the bits swapped??? */
#ifdef L_ENDIAN			/* little-endian */
	/* big-endian screen */
	if (screen_msbfirst)
	{
		switch (bpp)
		{
			case 32:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix >> 24;
						    *d3++ = pix >> 16;
						    *d3++ = pix >> 8;
						    *d3++ = pix;) i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix >>
								     24;
								     *d3++ =
								     pix >>
								     16;
								     *d3++ =
								     pix >> 8;
								     *d3++ =
								     pix;)}
				break;
			case 24:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix >> 16;
						    *d3++ = pix >> 8;
						    *d3++ = pix;) i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix >>
								     16;
								     *d3++ =
								     pix >> 8;
								     *d3++ =
								     pix;)}
				break;
			case 16:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix >> 8;
						    *d3++ = pix;) i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix >> 8;
								     *d3++ =
								     pix;)}
				break;
			case 8:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;)}
				break;
		}
	}
	else
	{			/* little-endian screen */
		switch (bpp)
		{
			case 32:
				while (i)
				{
					unroll8Expr(*((uint32 *) d3) =
						    colmap[*data++];
						    d3 += sizeof(uint32);
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(*
								     ((uint32
								       *) d3)
= colmap[*data++];
d3 += sizeof(uint32);
					)}
				break;
			case 24:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						    *d3++ = pix >> 8;
						    *d3++ = pix >> 16;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;
								     *d3++ =
								     pix >> 8;
								     *d3++ =
								     pix >>
								     16;)}
				break;
			case 16:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						    *d3++ = pix >> 8;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;
								     *d3++ =
								     pix >> 8;
					)}
				break;
			case 8:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;)}
		}
	}

#else /* bigendian-compiled */
	if (screen_msbfirst)
	{
		/* big-endian screen */
		switch (bpp)
		{
			case 32:
				while (i)
				{
					unroll8Expr(*((uint32 *) d3) =
						    colmap[*data++];
						    d3 += sizeof(uint32);
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(*
								     ((uint32
								       *) d3)
= colmap[*data++];
d3 += sizeof(uint32);
					)}
				break;
			case 24:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						    *d3++ = pix >> 8;
						    *d3++ = pix >> 16;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;
								     *d3++ =
								     pix >> 8;
								     *d3++ =
								     pix >>
								     16;)}
				break;
			case 16:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						    *d3++ = pix >> 8;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;
								     *d3++ =
								     pix >> 8;
					)}
				break;
			case 8:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;)}
		}
	}
	else
	{
		/* little-endian screen */
		switch (bpp)
		{
			case 32:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						    *d3++ = pix >> 8;
						    *d3++ = pix >> 16;
						    *d3++ = pix >> 24;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;
								     *d3++ =
								     pix >> 8;
								     *d3++ =
								     pix >>
								     16;
								     *d3++ =
								     pix >>
								     24;)}
				break;
			case 24:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						    *d3++ = pix >> 8;
						    *d3++ = pix >> 16;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;
								     *d3++ =
								     pix >> 8;
								     *d3++ =
								     pix >>
								     16;)}
				break;
			case 16:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						    *d3++ = pix >> 8;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;
								     *d3++ =
								     pix >> 8;
					)}
				break;
			case 8:
				while (i)
				{
					unroll8Expr(pix = colmap[*data++];
						    *d3++ = pix;
						)i -= 8;
				}
				i = (size & 0x7);
				if (i != 0)
					switch (i)
					{
							unroll8Lefts(pix =
								     colmap
								     [*data++];
								     *d3++ =
								     pix;)}
		}
	}
#endif

	return d2;
}
