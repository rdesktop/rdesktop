/*
   rdesktop: A Remote Desktop Protocol client.
   User interface services - X Window System
   Copyright (C) Matthew Chapman 1999-2002

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
#include <errno.h>
#include "rdesktop.h"

extern int width;
extern int height;
extern BOOL sendmotion;
extern BOOL fullscreen;
extern BOOL grab_keyboard;
extern BOOL hide_decorations;
extern char title[];
BOOL enable_compose = False;
BOOL focused;
BOOL mouse_in_wnd;

Display *display;
static int x_socket;
static Screen *screen;
static Window wnd;
static GC gc;
static Visual *visual;
static int depth;
static int bpp;
static XIM IM;
static XIC IC;
static XModifierKeymap *mod_map;
static Cursor current_cursor;
static Atom protocol_atom, kill_atom;

/* endianness */
static BOOL host_be;
static BOOL xserver_be;

/* software backing store */
static BOOL ownbackstore;
static Pixmap backstore;

/* MWM decorations */
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define PROP_MOTIF_WM_HINTS_ELEMENTS    5
typedef struct
{
	uint32 flags;
	uint32 functions;
	uint32 decorations;
	sint32 inputMode;
	uint32 status;
}
PropMotifWmHints;


#define FILL_RECTANGLE(x,y,cx,cy)\
{ \
	XFillRectangle(display, wnd, gc, x, y, cx, cy); \
	if (ownbackstore) \
		XFillRectangle(display, backstore, gc, x, y, cx, cy); \
}

#define FILL_RECTANGLE_BACKSTORE(x,y,cx,cy)\
{ \
	XFillRectangle(display, ownbackstore ? backstore : wnd, gc, x, y, cx, cy); \
}

/* colour maps */
BOOL owncolmap = False;
static Colormap xcolmap;
static uint32 *colmap;

#define TRANSLATE(col)		( owncolmap ? col : translate_colour(colmap[col]) )
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

void
mwm_hide_decorations(void)
{
	PropMotifWmHints motif_hints;
	Atom hintsatom;

	/* setup the property */
	motif_hints.flags = MWM_HINTS_DECORATIONS;
	motif_hints.decorations = 0;

	/* get the atom for the property */
	hintsatom = XInternAtom(display, "_MOTIF_WM_HINTS", False);
	if (!hintsatom)
	{
		warning("Failed to get atom _MOTIF_WM_HINTS: probably your window manager does not support MWM hints\n");
		return;
	}

	XChangeProperty(display, wnd, hintsatom, hintsatom, 32, PropModeReplace,
			(unsigned char *) &motif_hints, PROP_MOTIF_WM_HINTS_ELEMENTS);
}

static void
translate8(uint8 * data, uint8 * out, uint8 * end)
{
	while (out < end)
		*(out++) = (uint8) colmap[*(data++)];
}

static void
translate16(uint8 * data, uint16 * out, uint16 * end)
{
	while (out < end)
		*(out++) = (uint16) colmap[*(data++)];
}

/* little endian - conversion happens when colourmap is built */
static void
translate24(uint8 * data, uint8 * out, uint8 * end)
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
translate32(uint8 * data, uint32 * out, uint32 * end)
{
	while (out < end)
		*(out++) = colmap[*(data++)];
}

static uint8 *
translate_image(int width, int height, uint8 * data)
{
	int size = width * height * bpp / 8;
	uint8 *out = xmalloc(size);
	uint8 *end = out + size;

	switch (bpp)
	{
		case 8:
			translate8(data, out, end);
			break;

		case 16:
			translate16(data, (uint16 *) out, (uint16 *) end);
			break;

		case 24:
			translate24(data, out, end);
			break;

		case 32:
			translate32(data, (uint32 *) out, (uint32 *) end);
			break;
	}

	return out;
}

#define BSWAP16(x) { x = (((x & 0xff) << 8) | (x >> 8)); }
#define BSWAP24(x) { x = (((x & 0xff) << 16) | (x >> 16) | ((x >> 8) & 0xff00)); }
#define BSWAP32(x) { x = (((x & 0xff00ff) << 8) | ((x >> 8) & 0xff00ff)); \
			x = (x << 16) | (x >> 16); }

static uint32
translate_colour(uint32 colour)
{
	switch (bpp)
	{
		case 16:
			if (host_be != xserver_be)
				BSWAP16(colour);
			break;

		case 24:
			if (xserver_be)
				BSWAP24(colour);
			break;

		case 32:
			if (host_be != xserver_be)
				BSWAP32(colour);
			break;
	}

	return colour;
}

BOOL
get_key_state(unsigned int state, uint32 keysym)
{
	int modifierpos, key, keysymMask = 0;
	int offset;

	KeyCode keycode = XKeysymToKeycode(display, keysym);

	if (keycode == NoSymbol)
		return False;

	for (modifierpos = 0; modifierpos < 8; modifierpos++)
	{
		offset = mod_map->max_keypermod * modifierpos;

		for (key = 0; key < mod_map->max_keypermod; key++)
		{
			if (mod_map->modifiermap[offset + key] == keycode)
				keysymMask |= 1 << modifierpos;
		}
	}

	return (state & keysymMask) ? True : False;
}

BOOL
ui_init(void)
{
	XPixmapFormatValues *pfm;
	uint16 test;
	int i;

	display = XOpenDisplay(NULL);
	if (display == NULL)
	{
		error("Failed to open display: %s\n", XDisplayName(NULL));
		return False;
	}

	x_socket = ConnectionNumber(display);
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
			if ((pfm[i].depth == depth) && (pfm[i].bits_per_pixel > bpp))
			{
				bpp = pfm[i].bits_per_pixel;
			}
		}
		XFree(pfm);
	}

	if (bpp < 8)
	{
		error("Less than 8 bpp not currently supported.\n");
		XCloseDisplay(display);
		return False;
	}

	if (owncolmap != True)
	{
		xcolmap = DefaultColormapOfScreen(screen);
		if (depth <= 8)
			warning("Screen depth is 8 bits or lower: you may want to use -C for a private colourmap\n");
	}

	gc = XCreateGC(display, RootWindowOfScreen(screen), 0, NULL);

	if (DoesBackingStore(screen) != Always)
		ownbackstore = True;

	test = 1;
	host_be = !(BOOL) (*(uint8 *) (&test));
	xserver_be = (ImageByteOrder(display) == MSBFirst);

	if ((width == 0) || (height == 0))
	{
		/* Fetch geometry from _NET_WORKAREA */
		uint32 x, y, cx, cy;

		if (get_current_workarea(&x, &y, &cx, &cy) == 0)
		{
			width = cx;
			height = cy;
		}
		else
		{
			warning("Failed to get workarea: probably your window manager does not support extended hints\n");
			width = 800;
			height = 600;
		}
	}

	if (fullscreen)
	{
		width = WidthOfScreen(screen);
		height = HeightOfScreen(screen);
	}

	/* make sure width is a multiple of 4 */
	width = (width + 3) & ~3;

	if (ownbackstore)
	{
		backstore =
			XCreatePixmap(display, RootWindowOfScreen(screen), width, height, depth);

		/* clear to prevent rubbish being exposed at startup */
		XSetForeground(display, gc, BlackPixelOfScreen(screen));
		XFillRectangle(display, backstore, gc, 0, 0, width, height);
	}

	mod_map = XGetModifierMapping(display);

	if (enable_compose)
		IM = XOpenIM(display, NULL, NULL, NULL);

	xkeymap_init();
	return True;
}

void
ui_deinit(void)
{
	if (IM != NULL)
		XCloseIM(IM);

	XFreeModifiermap(mod_map);

	if (ownbackstore)
		XFreePixmap(display, backstore);

	XFreeGC(display, gc);
	XCloseDisplay(display);
	display = NULL;
}

BOOL
ui_create_window(void)
{
	XSetWindowAttributes attribs;
	XClassHint *classhints;
	XSizeHints *sizehints;
	int wndwidth, wndheight;
	long input_mask, ic_input_mask;
	XEvent xevent;

	wndwidth = fullscreen ? WidthOfScreen(screen) : width;
	wndheight = fullscreen ? HeightOfScreen(screen) : height;

	attribs.background_pixel = BlackPixelOfScreen(screen);
	attribs.backing_store = ownbackstore ? NotUseful : Always;
	attribs.override_redirect = fullscreen;

	wnd = XCreateWindow(display, RootWindowOfScreen(screen), 0, 0, wndwidth, wndheight,
			    0, CopyFromParent, InputOutput, CopyFromParent,
			    CWBackPixel | CWBackingStore | CWOverrideRedirect, &attribs);

	XStoreName(display, wnd, title);

	if (hide_decorations)
		mwm_hide_decorations();

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

	input_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
		VisibilityChangeMask | FocusChangeMask;

	if (sendmotion)
		input_mask |= PointerMotionMask;
	if (ownbackstore)
		input_mask |= ExposureMask;
	if (fullscreen || grab_keyboard)
		input_mask |= EnterWindowMask;
	if (grab_keyboard)
		input_mask |= LeaveWindowMask;

	if (IM != NULL)
	{
		IC = XCreateIC(IM, XNInputStyle, (XIMPreeditNothing | XIMStatusNothing),
			       XNClientWindow, wnd, XNFocusWindow, wnd, NULL);

		if ((IC != NULL)
		    && (XGetICValues(IC, XNFilterEvents, &ic_input_mask, NULL) == NULL))
			input_mask |= ic_input_mask;
	}

	XSelectInput(display, wnd, input_mask);
	XMapWindow(display, wnd);

	/* wait for VisibilityNotify */
	do
	{
		XMaskEvent(display, VisibilityChangeMask, &xevent);
	}
	while (xevent.type != VisibilityNotify);

	focused = False;
	mouse_in_wnd = False;

	/* handle the WM_DELETE_WINDOW protocol */
	protocol_atom = XInternAtom(display, "WM_PROTOCOLS", True);
	kill_atom = XInternAtom(display, "WM_DELETE_WINDOW", True);
	XSetWMProtocols(display, wnd, &kill_atom, 1);

	return True;
}

void
ui_destroy_window(void)
{
	if (IC != NULL)
		XDestroyIC(IC);

	XDestroyWindow(display, wnd);
}

void
xwin_toggle_fullscreen(void)
{
	Pixmap contents = 0;

	if (!ownbackstore)
	{
		/* need to save contents of window */
		contents = XCreatePixmap(display, wnd, width, height, depth);
		XCopyArea(display, wnd, contents, gc, 0, 0, width, height, 0, 0);
	}

	ui_destroy_window();
	fullscreen = !fullscreen;
	ui_create_window();

	XDefineCursor(display, wnd, current_cursor);

	if (!ownbackstore)
	{
		XCopyArea(display, contents, wnd, gc, 0, 0, width, height, 0, 0);
		XFreePixmap(display, contents);
	}
}

/* Process all events in Xlib queue 
   Returns 0 after user quit, 1 otherwise */
static int
xwin_process_events(void)
{
	XEvent xevent;
	KeySym keysym;
	uint16 button, flags;
	uint32 ev_time;
	key_translation tr;
	char str[256];
	Status status;
	unsigned int state;
	Window wdummy;
	int dummy;

	while (XPending(display) > 0)
	{
		XNextEvent(display, &xevent);

		if ((IC != NULL) && (XFilterEvent(&xevent, None) == True))
		{
			DEBUG_KBD(("Filtering event\n"));
			continue;
		}

		flags = 0;

		switch (xevent.type)
		{
			case ClientMessage:
				/* the window manager told us to quit */
				if ((xevent.xclient.message_type == protocol_atom)
				    && (xevent.xclient.data.l[0] == kill_atom))
					/* Quit */
					return 0;
				break;

			case KeyPress:
				if (IC != NULL)
					/* Multi_key compatible version */
				{
					XmbLookupString(IC,
							(XKeyPressedEvent *) &
							xevent, str, sizeof(str), &keysym, &status);
					if (!((status == XLookupKeySym) || (status == XLookupBoth)))
					{
						error("XmbLookupString failed with status 0x%x\n",
						      status);
						break;
					}
				}
				else
				{
					/* Plain old XLookupString */
					DEBUG_KBD(("\nNo input context, using XLookupString\n"));
					XLookupString((XKeyEvent *) & xevent,
						      str, sizeof(str), &keysym, NULL);
				}

				DEBUG_KBD(("KeyPress for (keysym 0x%lx, %s)\n", keysym,
					   get_ksname(keysym)));

				ev_time = time(NULL);
				if (handle_special_keys(keysym, xevent.xkey.state, ev_time, True))
					break;

				tr = xkeymap_translate_key(keysym,
							   xevent.xkey.keycode, xevent.xkey.state);

				if (tr.scancode == 0)
					break;

				ensure_remote_modifiers(ev_time, tr);

				rdp_send_scancode(ev_time, RDP_KEYPRESS, tr.scancode);
				break;

			case KeyRelease:
				XLookupString((XKeyEvent *) & xevent, str,
					      sizeof(str), &keysym, NULL);

				DEBUG_KBD(("\nKeyRelease for (keysym 0x%lx, %s)\n", keysym,
					   get_ksname(keysym)));

				ev_time = time(NULL);
				if (handle_special_keys(keysym, xevent.xkey.state, ev_time, False))
					break;

				tr = xkeymap_translate_key(keysym,
							   xevent.xkey.keycode, xevent.xkey.state);

				if (tr.scancode == 0)
					break;

				rdp_send_scancode(ev_time, RDP_KEYRELEASE, tr.scancode);
				break;

			case ButtonPress:
				flags = MOUSE_FLAG_DOWN;
				/* fall through */

			case ButtonRelease:
				button = xkeymap_translate_button(xevent.xbutton.button);
				if (button == 0)
					break;

				rdp_send_input(time(NULL), RDP_INPUT_MOUSE,
					       flags | button, xevent.xbutton.x, xevent.xbutton.y);
				break;

			case MotionNotify:
				if (fullscreen && !focused)
					XSetInputFocus(display, wnd, RevertToPointerRoot,
						       CurrentTime);
				rdp_send_input(time(NULL), RDP_INPUT_MOUSE,
					       MOUSE_FLAG_MOVE, xevent.xmotion.x, xevent.xmotion.y);
				break;

			case FocusIn:
				if (xevent.xfocus.mode == NotifyGrab)
					break;
				focused = True;
				XQueryPointer(display, wnd, &wdummy, &wdummy, &dummy, &dummy,
					      &dummy, &dummy, &state);
				reset_modifier_keys(state);
				if (grab_keyboard && mouse_in_wnd)
					XGrabKeyboard(display, wnd, True,
						      GrabModeAsync, GrabModeAsync, CurrentTime);
				break;

			case FocusOut:
				if (xevent.xfocus.mode == NotifyUngrab)
					break;
				focused = False;
				if (xevent.xfocus.mode == NotifyWhileGrabbed)
					XUngrabKeyboard(display, CurrentTime);
				break;

			case EnterNotify:
				/* we only register for this event when in fullscreen mode */
				/* or grab_keyboard */
				mouse_in_wnd = True;
				if (fullscreen)
				{
					XSetInputFocus(display, wnd, RevertToPointerRoot,
						       CurrentTime);
					break;
				}
				if (focused)
					XGrabKeyboard(display, wnd, True,
						      GrabModeAsync, GrabModeAsync, CurrentTime);
				break;

			case LeaveNotify:
				/* we only register for this event when grab_keyboard */
				mouse_in_wnd = False;
				XUngrabKeyboard(display, CurrentTime);
				break;

			case Expose:
				XCopyArea(display, backstore, wnd, gc,
					  xevent.xexpose.x, xevent.xexpose.y,
					  xevent.xexpose.width,
					  xevent.xexpose.height,
					  xevent.xexpose.x, xevent.xexpose.y);
				break;

			case MappingNotify:
				/* Refresh keyboard mapping if it has changed. This is important for
				   Xvnc, since it allocates keycodes dynamically */
				if (xevent.xmapping.request == MappingKeyboard
				    || xevent.xmapping.request == MappingModifier)
					XRefreshKeyboardMapping(&xevent.xmapping);

				if (xevent.xmapping.request == MappingModifier)
				{
					XFreeModifiermap(mod_map);
					mod_map = XGetModifierMapping(display);
				}
				break;

		}
	}
	/* Keep going */
	return 1;
}

/* Returns 0 after user quit, 1 otherwise */
int
ui_select(int rdp_socket)
{
	int n = (rdp_socket > x_socket) ? rdp_socket + 1 : x_socket + 1;
	fd_set rfds;

	FD_ZERO(&rfds);

	while (True)
	{
		/* Process any events already waiting */
		if (!xwin_process_events())
			/* User quit */
			return 0;

		FD_ZERO(&rfds);
		FD_SET(rdp_socket, &rfds);
		FD_SET(x_socket, &rfds);

		switch (select(n, &rfds, NULL, NULL, NULL))
		{
			case -1:
				error("select: %s\n", strerror(errno));

			case 0:
				continue;
		}

		if (FD_ISSET(rdp_socket, &rfds))
			return 1;
	}
}

void
ui_move_pointer(int x, int y)
{
	XWarpPointer(display, wnd, wnd, 0, 0, 0, 0, x, y);
}

HBITMAP
ui_create_bitmap(int width, int height, uint8 * data)
{
	XImage *image;
	Pixmap bitmap;
	uint8 *tdata;

	tdata = (owncolmap ? data : translate_image(width, height, data));
	bitmap = XCreatePixmap(display, wnd, width, height, depth);
	image = XCreateImage(display, visual, depth, ZPixmap, 0,
			     (char *) tdata, width, height, 8, 0);

	XPutImage(display, bitmap, gc, image, 0, 0, 0, 0, width, height);

	XFree(image);
	if (!owncolmap)
		xfree(tdata);
	return (HBITMAP) bitmap;
}

void
ui_paint_bitmap(int x, int y, int cx, int cy, int width, int height, uint8 * data)
{
	XImage *image;
	uint8 *tdata;

	tdata = (owncolmap ? data : translate_image(width, height, data));
	image = XCreateImage(display, visual, depth, ZPixmap, 0,
			     (char *) tdata, width, height, 8, 0);

	if (ownbackstore)
	{
		XPutImage(display, backstore, gc, image, 0, 0, x, y, cx, cy);
		XCopyArea(display, backstore, wnd, gc, x, y, cx, cy, x, y);
	}
	else
	{
		XPutImage(display, wnd, gc, image, 0, 0, x, y, cx, cy);
	}

	XFree(image);
	if (!owncolmap)
		xfree(tdata);
}

void
ui_destroy_bitmap(HBITMAP bmp)
{
	XFreePixmap(display, (Pixmap) bmp);
}

HGLYPH
ui_create_glyph(int width, int height, uint8 * data)
{
	XImage *image;
	Pixmap bitmap;
	int scanline;
	GC gc;

	scanline = (width + 7) / 8;

	bitmap = XCreatePixmap(display, wnd, width, height, 1);
	gc = XCreateGC(display, bitmap, 0, NULL);

	image = XCreateImage(display, visual, 1, ZPixmap, 0, (char *) data,
			     width, height, 8, scanline);
	image->byte_order = MSBFirst;
	image->bitmap_bit_order = MSBFirst;
	XInitImage(image);

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

HCURSOR
ui_create_cursor(unsigned int x, unsigned int y, int width, int height,
		 uint8 * andmask, uint8 * xormask)
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

	xcursor =
		XCreatePixmapCursor(display, (Pixmap) cursorglyph,
				    (Pixmap) maskglyph, &fg, &bg, x, y);

	ui_destroy_glyph(maskglyph);
	ui_destroy_glyph(cursorglyph);
	xfree(mask);
	xfree(cursor);
	return (HCURSOR) xcursor;
}

void
ui_set_cursor(HCURSOR cursor)
{
	current_cursor = (Cursor) cursor;
	XDefineCursor(display, wnd, current_cursor);
}

void
ui_destroy_cursor(HCURSOR cursor)
{
	XFreeCursor(display, (Cursor) cursor);
}

#define MAKE_XCOLOR(xc,c) \
		(xc)->red   = ((c)->red   << 8) | (c)->red; \
		(xc)->green = ((c)->green << 8) | (c)->green; \
		(xc)->blue  = ((c)->blue  << 8) | (c)->blue; \
		(xc)->flags = DoRed | DoGreen | DoBlue;


HCOLOURMAP
ui_create_colourmap(COLOURMAP * colours)
{
	COLOURENTRY *entry;
	int i, ncolours = colours->ncolours;
	if (!owncolmap)
	{
		uint32 *map = xmalloc(sizeof(*colmap) * ncolours);
		XColor xentry;
		XColor xc_cache[256];
		uint32 colour;
		int colLookup = 256;
		for (i = 0; i < ncolours; i++)
		{
			entry = &colours->colours[i];
			MAKE_XCOLOR(&xentry, entry);

			if (XAllocColor(display, xcolmap, &xentry) == 0)
			{
				/* Allocation failed, find closest match. */
				int j = 256;
				int nMinDist = 3 * 256 * 256;
				long nDist = nMinDist;

				/* only get the colors once */
				while (colLookup--)
				{
					xc_cache[colLookup].pixel = colLookup;
					xc_cache[colLookup].red = xc_cache[colLookup].green =
						xc_cache[colLookup].blue = 0;
					xc_cache[colLookup].flags = 0;
					XQueryColor(display,
						    DefaultColormap(display,
								    DefaultScreen(display)),
						    &xc_cache[colLookup]);
				}
				colLookup = 0;

				/* approximate the pixel */
				while (j--)
				{
					if (xc_cache[j].flags)
					{
						nDist = ((long) (xc_cache[j].red >> 8) -
							 (long) (xentry.red >> 8)) *
							((long) (xc_cache[j].red >> 8) -
							 (long) (xentry.red >> 8)) +
							((long) (xc_cache[j].green >> 8) -
							 (long) (xentry.green >> 8)) *
							((long) (xc_cache[j].green >> 8) -
							 (long) (xentry.green >> 8)) +
							((long) (xc_cache[j].blue >> 8) -
							 (long) (xentry.blue >> 8)) *
							((long) (xc_cache[j].blue >> 8) -
							 (long) (xentry.blue >> 8));
					}
					if (nDist < nMinDist)
					{
						nMinDist = nDist;
						xentry.pixel = j;
					}
				}
			}
			colour = xentry.pixel;

			/* update our cache */
			if (xentry.pixel < 256)
			{
				xc_cache[xentry.pixel].red = xentry.red;
				xc_cache[xentry.pixel].green = xentry.green;
				xc_cache[xentry.pixel].blue = xentry.blue;

			}


			/* byte swap here to make translate_image faster */
			map[i] = translate_colour(colour);
		}
		return map;
	}
	else
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
		return (HCOLOURMAP) map;
	}
}

void
ui_destroy_colourmap(HCOLOURMAP map)
{
	if (!owncolmap)
		xfree(map);
	else
		XFreeColormap(display, (Colormap) map);
}

void
ui_set_colourmap(HCOLOURMAP map)
{
	if (!owncolmap)
		colmap = map;
	else
		XSetWindowColormap(display, wnd, (Colormap) map);
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
ui_reset_clip(void)
{
	XRectangle rect;

	rect.x = 0;
	rect.y = 0;
	rect.width = width;
	rect.height = height;
	XSetClipRectangles(display, gc, 0, 0, &rect, 1, YXBanded);
}

void
ui_bell(void)
{
	XBell(display, 0);
}

void
ui_destblt(uint8 opcode,
	   /* dest */ int x, int y, int cx, int cy)
{
	SET_FUNCTION(opcode);
	FILL_RECTANGLE(x, y, cx, cy);
	RESET_FUNCTION(opcode);
}

void
ui_patblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* brush */ BRUSH * brush, int bgcolour, int fgcolour)
{
	Pixmap fill;
	uint8 i, ipattern[8];

	SET_FUNCTION(opcode);

	switch (brush->style)
	{
		case 0:	/* Solid */
			SET_FOREGROUND(fgcolour);
			FILL_RECTANGLE(x, y, cx, cy);
			break;

		case 3:	/* Pattern */
			for (i = 0; i != 8; i++)
				ipattern[7 - i] = brush->pattern[i];
			fill = (Pixmap) ui_create_glyph(8, 8, ipattern);

			SET_FOREGROUND(bgcolour);
			SET_BACKGROUND(fgcolour);
			XSetFillStyle(display, gc, FillOpaqueStippled);
			XSetStipple(display, gc, fill);
			XSetTSOrigin(display, gc, brush->xorigin, brush->yorigin);

			FILL_RECTANGLE(x, y, cx, cy);

			XSetFillStyle(display, gc, FillSolid);
			XSetTSOrigin(display, gc, 0, 0);
			ui_destroy_glyph((HGLYPH) fill);
			break;

		default:
			unimpl("brush %d\n", brush->style);
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
	if (ownbackstore)
		XCopyArea(display, backstore, backstore, gc, srcx, srcy, cx, cy, x, y);
	RESET_FUNCTION(opcode);
}

void
ui_memblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* src */ HBITMAP src, int srcx, int srcy)
{
	SET_FUNCTION(opcode);
	XCopyArea(display, (Pixmap) src, wnd, gc, srcx, srcy, cx, cy, x, y);
	if (ownbackstore)
		XCopyArea(display, (Pixmap) src, backstore, gc, srcx, srcy, cx, cy, x, y);
	RESET_FUNCTION(opcode);
}

void
ui_triblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* src */ HBITMAP src, int srcx, int srcy,
	  /* brush */ BRUSH * brush, int bgcolour, int fgcolour)
{
	/* This is potentially difficult to do in general. Until someone
	   comes up with a more efficient way of doing it I am using cases. */

	switch (opcode)
	{
		case 0x69:	/* PDSxxn */
			ui_memblt(ROP2_XOR, x, y, cx, cy, src, srcx, srcy);
			ui_patblt(ROP2_NXOR, x, y, cx, cy, brush, bgcolour, fgcolour);
			break;

		case 0xb8:	/* PSDPxax */
			ui_patblt(ROP2_XOR, x, y, cx, cy, brush, bgcolour, fgcolour);
			ui_memblt(ROP2_AND, x, y, cx, cy, src, srcx, srcy);
			ui_patblt(ROP2_XOR, x, y, cx, cy, brush, bgcolour, fgcolour);
			break;

		case 0xc0:	/* PSa */
			ui_memblt(ROP2_COPY, x, y, cx, cy, src, srcx, srcy);
			ui_patblt(ROP2_AND, x, y, cx, cy, brush, bgcolour, fgcolour);
			break;

		default:
			unimpl("triblt 0x%x\n", opcode);
			ui_memblt(ROP2_COPY, x, y, cx, cy, src, srcx, srcy);
	}
}

void
ui_line(uint8 opcode,
	/* dest */ int startx, int starty, int endx, int endy,
	/* pen */ PEN * pen)
{
	SET_FUNCTION(opcode);
	SET_FOREGROUND(pen->colour);
	XDrawLine(display, wnd, gc, startx, starty, endx, endy);
	if (ownbackstore)
		XDrawLine(display, backstore, gc, startx, starty, endx, endy);
	RESET_FUNCTION(opcode);
}

void
ui_rect(
	       /* dest */ int x, int y, int cx, int cy,
	       /* brush */ int colour)
{
	SET_FOREGROUND(colour);
	FILL_RECTANGLE(x, y, cx, cy);
}

/* warning, this function only draws on wnd or backstore, not both */
void
ui_draw_glyph(int mixmode,
	      /* dest */ int x, int y, int cx, int cy,
	      /* src */ HGLYPH glyph, int srcx, int srcy,
	      int bgcolour, int fgcolour)
{
	SET_FOREGROUND(fgcolour);
	SET_BACKGROUND(bgcolour);

	XSetFillStyle(display, gc,
		      (mixmode == MIX_TRANSPARENT) ? FillStippled : FillOpaqueStippled);
	XSetStipple(display, gc, (Pixmap) glyph);
	XSetTSOrigin(display, gc, x, y);

	FILL_RECTANGLE_BACKSTORE(x, y, cx, cy);

	XSetFillStyle(display, gc, FillSolid);
}

#define DO_GLYPH(ttext,idx) \
{\
  glyph = cache_get_font (font, ttext[idx]);\
  if (!(flags & TEXT2_IMPLICIT_X))\
    {\
      xyoffset = ttext[++idx];\
      if ((xyoffset & 0x80))\
	{\
	  if (flags & TEXT2_VERTICAL) \
	    y += ttext[idx+1] | (ttext[idx+2] << 8);\
	  else\
	    x += ttext[idx+1] | (ttext[idx+2] << 8);\
	  idx += 2;\
	}\
      else\
	{\
	  if (flags & TEXT2_VERTICAL) \
	    y += xyoffset;\
	  else\
	    x += xyoffset;\
	}\
    }\
  if (glyph != NULL)\
    {\
      ui_draw_glyph (mixmode, x + glyph->offset,\
		     y + glyph->baseline,\
		     glyph->width, glyph->height,\
		     glyph->pixmap, 0, 0, bgcolour, fgcolour);\
      if (flags & TEXT2_IMPLICIT_X)\
	x += glyph->width;\
    }\
}

void
ui_draw_text(uint8 font, uint8 flags, int mixmode, int x, int y,
	     int clipx, int clipy, int clipcx, int clipcy,
	     int boxx, int boxy, int boxcx, int boxcy, int bgcolour,
	     int fgcolour, uint8 * text, uint8 length)
{
	FONTGLYPH *glyph;
	int i, j, xyoffset;
	DATABLOB *entry;

	SET_FOREGROUND(bgcolour);

	if (boxcx > 1)
	{
		FILL_RECTANGLE_BACKSTORE(boxx, boxy, boxcx, boxcy);
	}
	else if (mixmode == MIX_OPAQUE)
	{
		FILL_RECTANGLE_BACKSTORE(clipx, clipy, clipcx, clipcy);
	}

	/* Paint text, character by character */
	for (i = 0; i < length;)
	{
		switch (text[i])
		{
			case 0xff:
				if (i + 2 < length)
					cache_put_text(text[i + 1], text, text[i + 2]);
				else
				{
					error("this shouldn't be happening\n");
					exit(1);
				}
				/* this will move pointer from start to first character after FF command */
				length -= i + 3;
				text = &(text[i + 3]);
				i = 0;
				break;

			case 0xfe:
				entry = cache_get_text(text[i + 1]);
				if (entry != NULL)
				{
					if ((((uint8 *) (entry->data))[1] ==
					     0) && (!(flags & TEXT2_IMPLICIT_X)))
					{
						if (flags & TEXT2_VERTICAL)
							y += text[i + 2];
						else
							x += text[i + 2];
					}
					for (j = 0; j < entry->size; j++)
						DO_GLYPH(((uint8 *) (entry->data)), j);
				}
				if (i + 2 < length)
					i += 3;
				else
					i += 2;
				length -= i;
				/* this will move pointer from start to first character after FE command */
				text = &(text[i]);
				i = 0;
				break;

			default:
				DO_GLYPH(text, i);
				i++;
				break;
		}
	}
	if (ownbackstore)
	{
		if (boxcx > 1)
			XCopyArea(display, backstore, wnd, gc, boxx,
				  boxy, boxcx, boxcy, boxx, boxy);
		else
			XCopyArea(display, backstore, wnd, gc, clipx,
				  clipy, clipcx, clipcy, clipx, clipy);
	}
}

void
ui_desktop_save(uint32 offset, int x, int y, int cx, int cy)
{
	Pixmap pix;
	XImage *image;

	if (ownbackstore)
	{
		image = XGetImage(display, backstore, x, y, cx, cy, AllPlanes, ZPixmap);
	}
	else
	{
		pix = XCreatePixmap(display, wnd, cx, cy, depth);
		XCopyArea(display, wnd, pix, gc, x, y, cx, cy, 0, 0);
		image = XGetImage(display, pix, 0, 0, cx, cy, AllPlanes, ZPixmap);
		XFreePixmap(display, pix);
	}

	offset *= bpp / 8;
	cache_put_desktop(offset, cx, cy, image->bytes_per_line, bpp / 8, (uint8 *) image->data);

	XDestroyImage(image);
}

void
ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy)
{
	XImage *image;
	uint8 *data;

	offset *= bpp / 8;
	data = cache_get_desktop(offset, cx, cy, bpp / 8);
	if (data == NULL)
		return;

	image = XCreateImage(display, visual, depth, ZPixmap, 0,
			     (char *) data, cx, cy, BitmapPad(display), cx * bpp / 8);

	if (ownbackstore)
	{
		XPutImage(display, backstore, gc, image, 0, 0, x, y, cx, cy);
		XCopyArea(display, backstore, wnd, gc, x, y, cx, cy, x, y);
	}
	else
	{
		XPutImage(display, wnd, gc, image, 0, 0, x, y, cx, cy);
	}

	XFree(image);
}
