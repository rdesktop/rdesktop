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
extern BOOL sendmotion;
extern BOOL fullscreen;

static Display *display;
static Window wnd;
static GC gc;
static Visual *visual;
static int depth;
static int bpp;
static BOOL backpixmap;

static BOOL owncolmap;
static Colormap xcolmap;
static uint32 white;
static uint32 *colmap;

#define TRANSLATE(col)		( owncolmap ? col : colmap[col] )
#define SET_FOREGROUND(col)	XSetForeground(display, gc, TRANSLATE(col));
#define SET_BACKGROUND(col)	XSetBackground(display, gc, TRANSLATE(col));

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

#define SET_FUNCTION(rop2)	{ if (rop2 != ROP2_COPY) XSetFunction(display, gc, rop2_map[rop2]); }
#define RESET_FUNCTION(rop2)	{ if (rop2 != ROP2_COPY) XSetFunction(display, gc, GXcopy); }

static void
translate8(uint8 *data, uint8 *out, uint8 *end)
{
	while (out < end)
		*(out++) = (uint8)colmap[*(data++)];
}

static void
translate16(uint8 *data, uint16 *out, uint16 *end)
{
	while (out < end)
		*(out++) = (uint16)colmap[*(data++)];
}

/* XXX endianness */
static void
translate24(uint8 *data, uint8 *out, uint8 *end)
{
	uint32 value;

	while (out < end)
	{
		value = colmap[*(data++)];
		*(out++) = value;
		*(out++) = value >> 8;
		*(out++) = value >> 16;
	}
}

static void
translate32(uint8 *data, uint32 *out, uint32 *end)
{
	while (out < end)
		*(out++) = colmap[*(data++)];
}

static uint8 *
translate(int width, int height, uint8 *data)
{
	int size = width * height * bpp/8;
	uint8 *out = xmalloc(size);
	uint8 *end = out + size;

	switch (bpp)
	{
		case 8:
			translate8(data, out, end);
			break;

		case 16:
			translate16(data, (uint16 *)out, (uint16 *)end);
			break;

		case 24:
			translate24(data, out, end);
			break;

		case 32:
			translate32(data, (uint32 *)out, (uint32 *)end);
			break;
	}

	return out;
}

#define L_ENDIAN
int screen_msbfirst = 0;


BOOL
ui_create_window(char *title)
{
	XSetWindowAttributes attribs;
	XClassHint *classhints;
	XSizeHints *sizehints;
	unsigned long input_mask;
	XPixmapFormatValues *pfm;
	Screen *screen;
	int i;


	display = XOpenDisplay(NULL);
	if (display == NULL)
	{
		ERROR("Failed to open display\n");
		return False;
	}

	screen = DefaultScreenOfDisplay(display);
	visual = DefaultVisualOfScreen(screen);
	depth = DefaultDepthOfScreen(screen);

	pfm = XListPixmapFormats(display, &i);
	if (pfm != NULL)
	{
		/* Use maximum bpp for this depth - this is generally
		   desirable, e.g. 24 bits->32 bits. */
		while (i--)
		{
			if ((pfm[i].depth == depth)
			    && (pfm[i].bits_per_pixel > bpp))
			{
				bpp = pfm[i].bits_per_pixel;
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

	if (depth <= 8)
		owncolmap = True;
	else
		xcolmap = DefaultColormapOfScreen(screen);

	white = WhitePixelOfScreen(screen);
	attribs.background_pixel = BlackPixelOfScreen(screen);
	attribs.backing_store = DoesBackingStore(screen);

	if (attribs.backing_store == NotUseful)
		backpixmap = True;

	if (fullscreen)
	{
		attribs.override_redirect = True;
		width = WidthOfScreen(screen);
		height = HeightOfScreen(screen);
	}
	else
	{
		attribs.override_redirect = False;
	}

	width &= ~3; /* make width a multiple of 32 bits */

	wnd = XCreateWindow(display, RootWindowOfScreen(screen),
			    0, 0, width, height, 0, CopyFromParent,
			    InputOutput, CopyFromParent,
			    CWBackingStore | CWBackPixel | CWOverrideRedirect,
			    &attribs);

	XStoreName(display, wnd, title);

	classhints = XAllocClassHint();
	if (classhints != NULL)
	{
		classhints->res_name = classhints->res_class = "rdesktop";
		XSetClassHint(display, wnd, classhints);
		XFree(classhints);
	}

	sizehints = XAllocSizeHints();
	if (sizehints)
	{
		sizehints->flags = PMinSize | PMaxSize;
		sizehints->min_width = sizehints->max_width = width;
		sizehints->min_height = sizehints->max_height = height;
		XSetWMNormalHints(display, wnd, sizehints);
		XFree(sizehints);
	}

	input_mask = KeyPressMask | KeyReleaseMask
			| ButtonPressMask | ButtonReleaseMask
			| EnterWindowMask | LeaveWindowMask;

	if (sendmotion)
		input_mask |= PointerMotionMask;

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
				XGrabKeyboard(display, wnd, True, GrabModeAsync,
					      GrabModeAsync, CurrentTime);
				break;

			case LeaveNotify:
				XUngrabKeyboard(display, CurrentTime);
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

	tdata = (owncolmap ? data : translate(width, height, data));
	bitmap = XCreatePixmap(display, wnd, width, height, depth);
	image = XCreateImage(display, visual, depth, ZPixmap,
			     0, tdata, width, height, 8, 0);

	XPutImage(display, bitmap, gc, image, 0, 0, 0, 0, width, height);

	XFree(image);
	if (!owncolmap)
		xfree(tdata);
	return (HBITMAP) bitmap;
}

void
ui_paint_bitmap(int x, int y, int cx, int cy,
		int width, int height, uint8 *data)
{
	XImage *image;
	uint8 *tdata;

	tdata = (owncolmap ? data : translate(width, height, data));
	image = XCreateImage(display, visual, depth, ZPixmap,
			     0, tdata, width, height, 8, 0);

	XPutImage(display, wnd, gc, image, 0, 0, x, y, cx, cy);

	XFree(image);
	if (!owncolmap)
		xfree(tdata);
}

void
ui_destroy_bitmap(HBITMAP bmp)
{
	XFreePixmap(display, (Pixmap)bmp);
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

	XPutImage(display, bitmap, gc, image, 0, 0, 0, 0, width, height);

	XFree(image);
	XFreeGC(display, gc);
	return (HGLYPH)bitmap;
}

void
ui_destroy_glyph(HGLYPH glyph)
{
	XFreePixmap(display, (Pixmap)glyph);
}

HCURSOR
ui_create_cursor(unsigned int x, unsigned int y, int width,
		 int height, uint8 *andmask, uint8 *xormask)
{
	HGLYPH maskglyph, cursorglyph;
	XColor bg, fg;
	Cursor xcursor;
	uint8 *cursor, *pcursor;
	uint8 *mask, *pmask;
	uint8 nextbit;
	int scanline, offset;
	int i, j;

	scanline = (width + 7) / 8;
	offset = scanline * height;

	cursor = xmalloc(offset);
	memset(cursor, 0, offset);

	mask = xmalloc(offset);
	memset(mask, 0, offset);

	/* approximate AND and XOR masks with a monochrome X pointer */
	for (i = 0; i < height; i++)
	{
		offset -= scanline;
		pcursor = &cursor[offset];
		pmask = &mask[offset];

		for (j = 0; j < scanline; j++)
		{
			for (nextbit = 0x80; nextbit != 0; nextbit >>= 1)
			{
				if (xormask[0] || xormask[1] || xormask[2])
				{
					*pcursor |= (~(*andmask) & nextbit);
					*pmask |= nextbit;
				}
				else
				{
					*pcursor |= ((*andmask) & nextbit);
					*pmask |= (~(*andmask) & nextbit);
				}

				xormask += 3;
			}

			andmask++;
			pcursor++;
			pmask++;
		}
	}

	fg.red = fg.blue = fg.green = 0xffff;
	bg.red = bg.blue = bg.green = 0x0000;
	fg.flags = bg.flags = DoRed | DoBlue | DoGreen;

	cursorglyph = ui_create_glyph(width, height, cursor);
	maskglyph = ui_create_glyph(width, height, mask);
	
	xcursor = XCreatePixmapCursor(display, (Pixmap)cursorglyph,
				(Pixmap)maskglyph, &fg, &bg, x, y);

	ui_destroy_glyph(maskglyph);
	ui_destroy_glyph(cursorglyph);
	xfree(mask);
	xfree(cursor);
	return (HCURSOR)xcursor;
}

void
ui_set_cursor(HCURSOR cursor)
{
	XDefineCursor(display, wnd, (Cursor)cursor);
}

void
ui_destroy_cursor(HCURSOR cursor)
{
	XFreeCursor(display, (Cursor)cursor);
}

#define MAKE_XCOLOR(xc,c) \
		(xc)->red   = ((c)->red   << 8) | (c)->red; \
		(xc)->green = ((c)->green << 8) | (c)->green; \
		(xc)->blue  = ((c)->blue  << 8) | (c)->blue; \
		(xc)->flags = DoRed | DoGreen | DoBlue;

HCOLOURMAP
ui_create_colourmap(COLOURMAP *colours)
{
	COLOURENTRY *entry;
	int i, ncolours = colours->ncolours;

	if (owncolmap)
	{
		XColor *xcolours, *xentry;
		Colormap map;

		xcolours = xmalloc(sizeof(XColor) * ncolours);
		for (i = 0; i < ncolours; i++)
		{
			entry = &colours->colours[i];
			xentry = &xcolours[i];
			xentry->pixel = i;
			MAKE_XCOLOR(xentry, entry);
		}

		map = XCreateColormap(display, wnd, visual, AllocAll);
		XStoreColors(display, map, xcolours, ncolours);

		xfree(xcolours);
		return (HCOLOURMAP)map;
	}
	else
	{
		uint32 *map = xmalloc(sizeof(*colmap) * ncolours);
		XColor xentry;

		for (i = 0; i < ncolours; i++)
		{
			entry = &colours->colours[i];
			MAKE_XCOLOR(&xentry, entry);

			if (XAllocColor(display, xcolmap, &xentry) != 0)
				map[i] = xentry.pixel;
			else
				map[i] = white;
		}

		return map;
	}
}

void
ui_destroy_colourmap(HCOLOURMAP map)
{
	if (owncolmap)
		XFreeColormap(display, (Colormap)map);
	else
		xfree(map);
}

void
ui_set_colourmap(HCOLOURMAP map)
{
	if (owncolmap)
		XSetWindowColormap(display, wnd, (Colormap)map);
	else
		colmap = map;
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
	SET_FUNCTION(opcode);
	XFillRectangle(display, wnd, gc, x, y, cx, cy);
	RESET_FUNCTION(opcode);
}

void
ui_patblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* brush */ BRUSH *brush, int bgcolour, int fgcolour)
{
	Pixmap fill;

	SET_FUNCTION(opcode);

	switch (brush->style)
	{
		case 0:	/* Solid */
			SET_FOREGROUND(fgcolour);
			XFillRectangle(display, wnd, gc, x, y, cx, cy);
			break;

		case 3:	/* Pattern */
			fill = (Pixmap)ui_create_glyph(8, 8, brush->pattern);

			SET_FOREGROUND(bgcolour);
			SET_BACKGROUND(fgcolour);
			XSetFillStyle(display, gc, FillOpaqueStippled);
			XSetStipple(display, gc, fill);
			XSetTSOrigin(display, gc, brush->xorigin, brush->yorigin);

			XFillRectangle(display, wnd, gc, x, y, cx, cy);

			XSetFillStyle(display, gc, FillSolid);
			ui_destroy_glyph((HGLYPH)fill);
			break;

		default:
			NOTIMP("brush %d\n", brush->style);
	}

	RESET_FUNCTION(opcode);
}

void
ui_screenblt(uint8 opcode,
	     /* dest */ int x, int y, int cx, int cy,
	     /* src */ int srcx, int srcy)
{
	SET_FUNCTION(opcode);
	XCopyArea(display, wnd, wnd, gc, srcx, srcy, cx, cy, x, y);
	RESET_FUNCTION(opcode);
}

void
ui_memblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* src */ HBITMAP src, int srcx, int srcy)
{
	SET_FUNCTION(opcode);
	XCopyArea(display, (Pixmap)src, wnd, gc, srcx, srcy, cx, cy, x, y);
	RESET_FUNCTION(opcode);
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

		case 0xc0:	/* PSa */
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
	SET_FUNCTION(opcode);
	SET_FOREGROUND(pen->colour);
	XDrawLine(display, wnd, gc, startx, starty, endx, endy);
	RESET_FUNCTION(opcode);
}

void
ui_rect(
	       /* dest */ int x, int y, int cx, int cy,
	       /* brush */ int colour)
{
	SET_FOREGROUND(colour);
	XFillRectangle(display, wnd, gc, x, y, cx, cy);
}

void
ui_draw_glyph(int mixmode,
	      /* dest */ int x, int y, int cx, int cy,
	      /* src */ HGLYPH glyph, int srcx, int srcy, int bgcolour,
	      int fgcolour)
{
	SET_FOREGROUND(fgcolour);
	SET_BACKGROUND(bgcolour);

	XSetFillStyle(display, gc, (mixmode == MIX_TRANSPARENT)
		      ? FillStippled : FillOpaqueStippled);
	XSetStipple(display, gc, (Pixmap)glyph);
	XSetTSOrigin(display, gc, x, y);

	XFillRectangle(display, wnd, gc, x, y, cx, cy);

	XSetFillStyle(display, gc, FillSolid);
}

void
ui_draw_text(uint8 font, uint8 flags, int mixmode, int x, int y,
	     int clipx, int clipy, int clipcx, int clipcy,
	     int boxx, int boxy, int boxcx, int boxcy,
	     int bgcolour, int fgcolour, uint8 *text, uint8 length)
{
	FONTGLYPH *glyph;
	int i, offset;

	SET_FOREGROUND(bgcolour);

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
			offset = text[++i];
			if (offset & 0x80)
				offset = ((offset & 0x7f) << 8) | text[++i];

			if (flags & TEXT2_VERTICAL)
				y += offset;
			else
				x += offset;
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

	image = XCreateImage(display, visual, depth, ZPixmap,
			     0, data, cx, cy, BitmapPad(display),
			     cx * bpp/8);

	XPutImage(display, wnd, gc, image, 0, 0, x, y, cx, cy);
	XFree(image);
}

