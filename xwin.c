/* -*- c-basic-offset: 8 -*-
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
#include "xproto.h"

extern int g_width;
extern int g_height;
extern BOOL g_sendmotion;
extern BOOL g_fullscreen;
extern BOOL g_grab_keyboard;
extern BOOL g_hide_decorations;
extern char g_title[];
extern int g_server_bpp;
extern int g_win_button_size;

Display *g_display;
Time g_last_gesturetime;
static int g_x_socket;
static Screen *g_screen;
Window g_wnd;
BOOL g_enable_compose = False;
static GC g_gc;
static Visual *g_visual;
static int g_depth;
static int g_bpp;
static XIM g_IM;
static XIC g_IC;
static XModifierKeymap *g_mod_map;
static Cursor g_current_cursor;
static HCURSOR g_null_cursor;
static Atom g_protocol_atom, g_kill_atom;
static BOOL g_focused;
static BOOL g_mouse_in_wnd;

/* endianness */
static BOOL g_host_be;
static BOOL g_xserver_be;
static int g_red_shift_r, g_blue_shift_r, g_green_shift_r;
static int g_red_shift_l, g_blue_shift_l, g_green_shift_l;

/* software backing store */
static BOOL g_ownbackstore;
static Pixmap g_backstore;

/* Moving in single app mode */
static BOOL g_moving_wnd;
static int g_move_x_offset = 0;
static int g_move_y_offset = 0;

#ifdef WITH_RDPSND
extern int g_dsp_fd;
extern BOOL g_dsp_busy;
extern BOOL g_rdpsnd;
#endif

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

typedef struct
{
	uint32 red;
	uint32 green;
	uint32 blue;
}
PixelColour;


#define FILL_RECTANGLE(x,y,cx,cy)\
{ \
	XFillRectangle(g_display, g_wnd, g_gc, x, y, cx, cy); \
	if (g_ownbackstore) \
		XFillRectangle(g_display, g_backstore, g_gc, x, y, cx, cy); \
}

#define FILL_RECTANGLE_BACKSTORE(x,y,cx,cy)\
{ \
	XFillRectangle(g_display, g_ownbackstore ? g_backstore : g_wnd, g_gc, x, y, cx, cy); \
}

/* colour maps */
BOOL g_owncolmap = False;
static Colormap g_xcolmap;
static uint32 *g_colmap = NULL;

#define TRANSLATE(col)		( g_server_bpp != 8 ? translate_colour(col) : g_owncolmap ? col : g_colmap[col] )
#define SET_FOREGROUND(col)	XSetForeground(g_display, g_gc, TRANSLATE(col));
#define SET_BACKGROUND(col)	XSetBackground(g_display, g_gc, TRANSLATE(col));

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

#define SET_FUNCTION(rop2)	{ if (rop2 != ROP2_COPY) XSetFunction(g_display, g_gc, rop2_map[rop2]); }
#define RESET_FUNCTION(rop2)	{ if (rop2 != ROP2_COPY) XSetFunction(g_display, g_gc, GXcopy); }

static void
mwm_hide_decorations(void)
{
	PropMotifWmHints motif_hints;
	Atom hintsatom;

	/* setup the property */
	motif_hints.flags = MWM_HINTS_DECORATIONS;
	motif_hints.decorations = 0;

	/* get the atom for the property */
	hintsatom = XInternAtom(g_display, "_MOTIF_WM_HINTS", False);
	if (!hintsatom)
	{
		warning("Failed to get atom _MOTIF_WM_HINTS: probably your window manager does not support MWM hints\n");
		return;
	}

	XChangeProperty(g_display, g_wnd, hintsatom, hintsatom, 32, PropModeReplace,
			(unsigned char *) &motif_hints, PROP_MOTIF_WM_HINTS_ELEMENTS);
}

static PixelColour
split_colour15(uint32 colour)
{
	PixelColour rv;
	rv.red = (colour & 0x7c00) >> 10;
	rv.red = (rv.red * 0xff) / 0x1f;
	rv.green = (colour & 0x03e0) >> 5;
	rv.green = (rv.green * 0xff) / 0x1f;
	rv.blue = (colour & 0x1f);
	rv.blue = (rv.blue * 0xff) / 0x1f;
	return rv;
}

static PixelColour
split_colour16(uint32 colour)
{
	PixelColour rv;
	rv.red = (colour & 0xf800) >> 11;
	rv.red = (rv.red * 0xff) / 0x1f;
	rv.green = (colour & 0x07e0) >> 5;
	rv.green = (rv.green * 0xff) / 0x3f;
	rv.blue = (colour & 0x001f);
	rv.blue = (rv.blue * 0xff) / 0x1f;
	return rv;
}

static PixelColour
split_colour24(uint32 colour)
{
	PixelColour rv;
	rv.blue = (colour & 0xff0000) >> 16;
	rv.green = (colour & 0xff00) >> 8;
	rv.red = (colour & 0xff);
	return rv;
}

static uint32
make_colour(PixelColour pc)
{
	return (((pc.red >> g_red_shift_r) << g_red_shift_l)
		| ((pc.green >> g_green_shift_r) << g_green_shift_l)
		| ((pc.blue >> g_blue_shift_r) << g_blue_shift_l));
}

#define BSWAP16(x) { x = (((x & 0xff) << 8) | (x >> 8)); }
#define BSWAP24(x) { x = (((x & 0xff) << 16) | (x >> 16) | ((x >> 8) & 0xff00)); }
#define BSWAP32(x) { x = (((x & 0xff00ff) << 8) | ((x >> 8) & 0xff00ff)); \
			x = (x << 16) | (x >> 16); }

static uint32
translate_colour(uint32 colour)
{
	PixelColour pc;
	switch (g_server_bpp)
	{
		case 15:
			pc = split_colour15(colour);
			break;
		case 16:
			pc = split_colour16(colour);
			break;
		case 24:
			pc = split_colour24(colour);
			break;
	}
	return make_colour(pc);
}

static void
translate8to8(uint8 * data, uint8 * out, uint8 * end)
{
	while (out < end)
		*(out++) = (uint8) g_colmap[*(data++)];
}

static void
translate8to16(uint8 * data, uint8 * out, uint8 * end)
{
	uint16 value;

	while (out < end)
	{
		value = (uint16) g_colmap[*(data++)];
		
		if (g_xserver_be)
		{
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
		}
	}
}

/* little endian - conversion happens when colourmap is built */
static void
translate8to24(uint8 * data, uint8 * out, uint8 * end)
{
	uint32 value;

	while (out < end)
	{
		value = g_colmap[*(data++)];
		
		if (g_xserver_be)
		{
			*(out++) = value >> 16;
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
			*(out++) = value >> 16;
		}
	}
}

static void
translate8to32(uint8 * data, uint8 * out, uint8 * end)
{
	uint32 value;

	while (out < end)
	{
		value = g_colmap[*(data++)];

		if (g_xserver_be)
		{
			*(out++) = value >> 24;
			*(out++) = value >> 16;
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
			*(out++) = value >> 16;
			*(out++) = value >> 24;
		}
	}
}

/* todo the remaining translate function might need some big endian check ?? */

static void
translate15to16(uint16 * data, uint8 * out, uint8 * end)
{
	uint16 pixel;
	uint16 value;

	while (out < end)
	{
		pixel = *(data++);

		if (g_host_be)
		{
			BSWAP16(pixel);
		}

		value = make_colour(split_colour15(pixel));

		if (g_xserver_be)
		{
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
		}
	}
}

static void
translate15to24(uint16 * data, uint8 * out, uint8 * end)
{
	uint32 value;
	uint16 pixel;

	while (out < end)
	{
		pixel = *(data++);

		if (g_host_be)
		{
			BSWAP16(pixel);
		}

		value = make_colour(split_colour15(pixel));
		if (g_xserver_be)
		{
			*(out++) = value >> 16;
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
			*(out++) = value >> 16;
		}
	}
}

static void
translate15to32(uint16 * data, uint8 * out, uint8 * end)
{
	uint16 pixel;
	uint32 value;

	while (out < end)
	{
		pixel = *(data++);

		if (g_host_be)
		{
			BSWAP16(pixel);
		}

		value = make_colour(split_colour15(pixel));

		if (g_xserver_be)
		{
			*(out++) = value >> 24;
			*(out++) = value >> 16;
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
			*(out++) = value >> 16;
			*(out++) = value >> 24;
		}
	}
}

static void
translate16to16(uint16 * data, uint8 * out, uint8 * end)
{
	uint16 pixel;
	uint16 value;

	while (out < end)
	{
		pixel = *(data++);

		if (g_host_be)
		{
			BSWAP16(pixel);
		}

		value = make_colour(split_colour16(pixel));

		if (g_xserver_be)
		{
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
		}
	}
}

static void
translate16to24(uint16 * data, uint8 * out, uint8 * end)
{
	uint32 value;
	uint16 pixel;

	while (out < end)
	{
		pixel = *(data++);

		if (g_host_be)
		{
			BSWAP16(pixel);
		}

		value = make_colour(split_colour16(pixel));

		if (g_xserver_be)
		{
			*(out++) = value >> 16;
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
			*(out++) = value >> 16;
		}
	}
}

static void
translate16to32(uint16 * data, uint8 * out, uint8 * end)
{
	uint16 pixel;
	uint32 value;

	while (out < end)
	{
		pixel = *(data++);

		if (g_host_be)
		{
		BSWAP16(pixel)}

		value = make_colour(split_colour16(pixel));

		if (g_xserver_be)
		{
			*(out++) = value >> 24;
			*(out++) = value >> 16;
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
			*(out++) = value >> 16;
			*(out++) = value >> 24;
		}
	}
}

static void
translate24to16(uint8 * data, uint8 * out, uint8 * end)
{
	uint32 pixel = 0;
	uint16 value;
	while (out < end)
	{
		pixel = *(data++) << 16;
		pixel |= *(data++) << 8;
		pixel |= *(data++);

		value = (uint16) make_colour(split_colour24(pixel));

		if (g_xserver_be)
		{
			*(out++) = value >> 8;
			*(out++) = value;
		}
		else
		{
			*(out++) = value;
			*(out++) = value >> 8;
		}
	}
}

static void
translate24to24(uint8 * data, uint8 * out, uint8 * end)
{
	while (out < end)
	{
		*(out++) = (*(data++));
	}
}

static void
translate24to32(uint8 * data, uint8 * out, uint8 * end)
{
	while (out < end)
	{
		if (g_xserver_be)
		{
			*(out++) = 0x00;
			*(out++) = *(data++);
			*(out++) = *(data++);
			*(out++) = *(data++);
		}
		else
		{
			*(out++) = *(data++);
			*(out++) = *(data++);
			*(out++) = *(data++);
			*(out++) = 0x00;
		}
	}
}

static uint8 *
translate_image(int width, int height, uint8 * data)
{
	int size = width * height * g_bpp / 8;
	uint8 *out = (uint8 *) xmalloc(size);
	uint8 *end = out + size;

	switch (g_server_bpp)
	{
		case 24:
			switch (g_bpp)
			{
				case 32:
					translate24to32(data, out, end);
					break;
				case 24:
					translate24to24(data, out, end);
					break;
				case 16:
					translate24to16(data, out, end);
					break;
			}
			break;
		case 16:
			switch (g_bpp)
			{
				case 32:
					translate16to32((uint16 *) data, out, end);
					break;
				case 24:
					translate16to24((uint16 *) data, out, end);
					break;
				case 16:
					translate16to16((uint16 *) data, out, end);
					break;
			}
			break;
		case 15:
			switch (g_bpp)
			{
				case 32:
					translate15to32((uint16 *) data, out, end);
					break;
				case 24:
					translate15to24((uint16 *) data, out, end);
					break;
				case 16:
					translate15to16((uint16 *) data, out, end);
					break;
			}
			break;
		case 8:
			switch (g_bpp)
			{
				case 8:
					translate8to8(data, out, end);
					break;
				case 16:
					translate8to16(data, out, end);
					break;
				case 24:
					translate8to24(data, out, end);
					break;
				case 32:
					translate8to32(data, out, end);
					break;
			}
			break;
	}
	return out;
}

BOOL
get_key_state(unsigned int state, uint32 keysym)
{
	int modifierpos, key, keysymMask = 0;
	int offset;

	KeyCode keycode = XKeysymToKeycode(g_display, keysym);

	if (keycode == NoSymbol)
		return False;

	for (modifierpos = 0; modifierpos < 8; modifierpos++)
	{
		offset = g_mod_map->max_keypermod * modifierpos;

		for (key = 0; key < g_mod_map->max_keypermod; key++)
		{
			if (g_mod_map->modifiermap[offset + key] == keycode)
				keysymMask |= 1 << modifierpos;
		}
	}

	return (state & keysymMask) ? True : False;
}

static void
calculate_shifts(uint32 mask, int *shift_r, int *shift_l)
{
	*shift_l = ffs(mask) - 1;
	mask >>= *shift_l;
	*shift_r = 8 - ffs(mask & ~(mask >> 1));
}

BOOL
ui_init(void)
{
	XVisualInfo vi;
	XPixmapFormatValues *pfm;
	uint16 test;
	int i, screen_num;

	g_display = XOpenDisplay(NULL);
	if (g_display == NULL)
	{
		error("Failed to open display: %s\n", XDisplayName(NULL));
		return False;
	}

	screen_num = DefaultScreen(g_display);
	g_x_socket = ConnectionNumber(g_display);
	g_screen = ScreenOfDisplay(g_display, screen_num);
	g_depth = DefaultDepthOfScreen(g_screen);

	if (g_server_bpp == 8)
	{
		/* we use a colourmap, so any visual should do */
		g_visual = DefaultVisualOfScreen(g_screen);
	}
	else
	{
		/* need a truecolour visual */
		if (!XMatchVisualInfo(g_display, screen_num, g_depth, TrueColor, &vi))
		{
			error("The display does not support true colour - high colour support unavailable.\n");
			return False;
		}

		g_visual = vi.visual;
		g_owncolmap = False;
		calculate_shifts(vi.red_mask,   &g_red_shift_r,   &g_red_shift_l);
		calculate_shifts(vi.blue_mask,  &g_blue_shift_r,  &g_blue_shift_l);
		calculate_shifts(vi.green_mask, &g_green_shift_r, &g_green_shift_l);
	}

	pfm = XListPixmapFormats(g_display, &i);
	if (pfm != NULL)
	{
		/* Use maximum bpp for this depth - this is generally
		   desirable, e.g. 24 bits->32 bits. */
		while (i--)
		{
			if ((pfm[i].depth == g_depth) && (pfm[i].bits_per_pixel > g_bpp))
			{
				g_bpp = pfm[i].bits_per_pixel;
			}
		}
		XFree(pfm);
	}

	if (g_bpp < 8)
	{
		error("Less than 8 bpp not currently supported.\n");
		XCloseDisplay(g_display);
		return False;
	}

	if (!g_owncolmap)
	{
		g_xcolmap = DefaultColormapOfScreen(g_screen);
		if (g_depth <= 8)
			warning("Screen depth is 8 bits or lower: you may want to use -C for a private colourmap\n");
	}

	g_gc = XCreateGC(g_display, RootWindowOfScreen(g_screen), 0, NULL);

	if (DoesBackingStore(g_screen) != Always)
		g_ownbackstore = True;

	test = 1;
	g_host_be = !(BOOL) (*(uint8 *) (&test));
	g_xserver_be = (ImageByteOrder(g_display) == MSBFirst);

	/*
	 * Determine desktop size
	 */
	if (g_width < 0)
	{
		/* Percent of screen */
		g_height = HeightOfScreen(g_screen) * (-g_width) / 100;
		g_width = WidthOfScreen(g_screen) * (-g_width) / 100;
	}
	else if (g_width == 0)
	{
		/* Fetch geometry from _NET_WORKAREA */
		uint32 x, y, cx, cy;

		if (get_current_workarea(&x, &y, &cx, &cy) == 0)
		{
			g_width = cx;
			g_height = cy;
		}
		else
		{
			warning("Failed to get workarea: probably your window manager does not support extended hints\n");
			g_width = 800;
			g_height = 600;
		}
	}
	else if (g_fullscreen)
	{
		g_width = WidthOfScreen(g_screen);
		g_height = HeightOfScreen(g_screen);
	}

	/* make sure width is a multiple of 4 */
	g_width = (g_width + 3) & ~3;

	if (g_ownbackstore)
	{
		g_backstore =
			XCreatePixmap(g_display, RootWindowOfScreen(g_screen), g_width, g_height,
				      g_depth);

		/* clear to prevent rubbish being exposed at startup */
		XSetForeground(g_display, g_gc, BlackPixelOfScreen(g_screen));
		XFillRectangle(g_display, g_backstore, g_gc, 0, 0, g_width, g_height);
	}

	g_mod_map = XGetModifierMapping(g_display);

	xkeymap_init();

	if (g_enable_compose)
		g_IM = XOpenIM(g_display, NULL, NULL, NULL);

	xclip_init();

	DEBUG_RDP5(("server bpp %d client bpp %d depth %d\n", g_server_bpp, g_bpp, g_depth));

	return True;
}

void
ui_deinit(void)
{
	if (g_IM != NULL)
		XCloseIM(g_IM);

	XFreeModifiermap(g_mod_map);

	if (g_ownbackstore)
		XFreePixmap(g_display, g_backstore);

	XFreeGC(g_display, g_gc);
	XCloseDisplay(g_display);
	g_display = NULL;
}

#define NULL_POINTER_MASK	"\x80"
#define NULL_POINTER_DATA	"\x0\x0\x0"
	
BOOL
ui_create_window(void)
{
	XSetWindowAttributes attribs;
	XClassHint *classhints;
	XSizeHints *sizehints;
	int wndwidth, wndheight;
	long input_mask, ic_input_mask;
	XEvent xevent;

	wndwidth = g_fullscreen ? WidthOfScreen(g_screen) : g_width;
	wndheight = g_fullscreen ? HeightOfScreen(g_screen) : g_height;

	attribs.background_pixel = BlackPixelOfScreen(g_screen);
	attribs.backing_store = g_ownbackstore ? NotUseful : Always;
	attribs.override_redirect = g_fullscreen;

	g_wnd = XCreateWindow(g_display, RootWindowOfScreen(g_screen), 0, 0, wndwidth, wndheight,
			      0, CopyFromParent, InputOutput, CopyFromParent,
			      CWBackPixel | CWBackingStore | CWOverrideRedirect, &attribs);

	XStoreName(g_display, g_wnd, g_title);

	if (g_hide_decorations)
		mwm_hide_decorations();

	classhints = XAllocClassHint();
	if (classhints != NULL)
	{
		classhints->res_name = classhints->res_class = "rdesktop";
		XSetClassHint(g_display, g_wnd, classhints);
		XFree(classhints);
	}

	sizehints = XAllocSizeHints();
	if (sizehints)
	{
		sizehints->flags = PMinSize | PMaxSize;
		sizehints->min_width = sizehints->max_width = g_width;
		sizehints->min_height = sizehints->max_height = g_height;
		XSetWMNormalHints(g_display, g_wnd, sizehints);
		XFree(sizehints);
	}

	input_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
		VisibilityChangeMask | FocusChangeMask;

	if (g_sendmotion)
		input_mask |= PointerMotionMask;
	if (g_ownbackstore)
		input_mask |= ExposureMask;
	if (g_fullscreen || g_grab_keyboard)
		input_mask |= EnterWindowMask;
	if (g_grab_keyboard)
		input_mask |= LeaveWindowMask;

	if (g_IM != NULL)
	{
		g_IC = XCreateIC(g_IM, XNInputStyle, (XIMPreeditNothing | XIMStatusNothing),
				 XNClientWindow, g_wnd, XNFocusWindow, g_wnd, NULL);

		if ((g_IC != NULL)
		    && (XGetICValues(g_IC, XNFilterEvents, &ic_input_mask, NULL) == NULL))
			input_mask |= ic_input_mask;
	}

	XSelectInput(g_display, g_wnd, input_mask);
	XMapWindow(g_display, g_wnd);

	/* wait for VisibilityNotify */
	do
	{
		XMaskEvent(g_display, VisibilityChangeMask, &xevent);
	}
	while (xevent.type != VisibilityNotify);

	g_focused = False;
	g_mouse_in_wnd = False;

	/* handle the WM_DELETE_WINDOW protocol */
	g_protocol_atom = XInternAtom(g_display, "WM_PROTOCOLS", True);
	g_kill_atom = XInternAtom(g_display, "WM_DELETE_WINDOW", True);
	XSetWMProtocols(g_display, g_wnd, &g_kill_atom, 1);

	/* create invisible 1x1 cursor to be used as null cursor */
	g_null_cursor = ui_create_cursor(0, 0, 1, 1, NULL_POINTER_MASK, NULL_POINTER_DATA);

	return True;
}

void
ui_destroy_window(void)
{
	ui_destroy_cursor(g_null_cursor);
	
	if (g_IC != NULL)
		XDestroyIC(g_IC);

	XDestroyWindow(g_display, g_wnd);
}

void
xwin_toggle_fullscreen(void)
{
	Pixmap contents = 0;

	if (!g_ownbackstore)
	{
		/* need to save contents of window */
		contents = XCreatePixmap(g_display, g_wnd, g_width, g_height, g_depth);
		XCopyArea(g_display, g_wnd, contents, g_gc, 0, 0, g_width, g_height, 0, 0);
	}

	ui_destroy_window();
	g_fullscreen = !g_fullscreen;
	ui_create_window();

	XDefineCursor(g_display, g_wnd, g_current_cursor);

	if (!g_ownbackstore)
	{
		XCopyArea(g_display, contents, g_wnd, g_gc, 0, 0, g_width, g_height, 0, 0);
		XFreePixmap(g_display, contents);
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

	while (XPending(g_display) > 0)
	{
		XNextEvent(g_display, &xevent);

		if ((g_IC != NULL) && (XFilterEvent(&xevent, None) == True))
		{
			DEBUG_KBD(("Filtering event\n"));
			continue;
		}

		flags = 0;

		switch (xevent.type)
		{
			case ClientMessage:
				/* the window manager told us to quit */
				if ((xevent.xclient.message_type == g_protocol_atom)
				    && ((Atom) xevent.xclient.data.l[0] == g_kill_atom))
					/* Quit */
					return 0;
				break;

			case KeyPress:
				g_last_gesturetime = xevent.xkey.time;
				if (g_IC != NULL)
					/* Multi_key compatible version */
				{
					XmbLookupString(g_IC,
							&xevent.xkey, str, sizeof(str), &keysym,
							&status);
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

				save_remote_modifiers(tr.scancode);
				ensure_remote_modifiers(ev_time, tr);
				rdp_send_scancode(ev_time, RDP_KEYPRESS, tr.scancode);
				restore_remote_modifiers(ev_time, tr.scancode);

				break;

			case KeyRelease:
				g_last_gesturetime = xevent.xkey.time;
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
				g_last_gesturetime = xevent.xbutton.time;
				button = xkeymap_translate_button(xevent.xbutton.button);
				if (button == 0)
					break;

				/* If win_button_size is nonzero, enable single app mode */
				if (xevent.xbutton.y < g_win_button_size)
				{
					/* Stop moving window when button is released, regardless of cursor position */
					if (g_moving_wnd && (xevent.type == ButtonRelease))
						g_moving_wnd = False;

					/*  Check from right to left: */

					if (xevent.xbutton.x >= g_width - g_win_button_size)
					{
						/* The close button, continue */
						;
					}
					else if (xevent.xbutton.x >=
						 g_width - g_win_button_size * 2)
					{
						/* The maximize/restore button. Do not send to
						   server.  It might be a good idea to change the
						   cursor or give some other visible indication
						   that rdesktop inhibited this click */
						break;
					}
					else if (xevent.xbutton.x >=
						 g_width - g_win_button_size * 3)
					{
						/* The minimize button. Iconify window. */
						XIconifyWindow(g_display, g_wnd,
							       DefaultScreen(g_display));
						break;
					}
					else if (xevent.xbutton.x <= g_win_button_size)
					{
						/* The system menu. Ignore. */
						break;
					}
					else
					{
						/* The title bar. */
						if ((xevent.type == ButtonPress) && !g_fullscreen
						    && g_hide_decorations)
						{
							g_moving_wnd = True;
							g_move_x_offset = xevent.xbutton.x;
							g_move_y_offset = xevent.xbutton.y;
						}
						break;

					}
				}

				rdp_send_input(time(NULL), RDP_INPUT_MOUSE,
					       flags | button, xevent.xbutton.x, xevent.xbutton.y);
				break;

			case MotionNotify:
				if (g_moving_wnd)
				{
					XMoveWindow(g_display, g_wnd,
						    xevent.xmotion.x_root - g_move_x_offset,
						    xevent.xmotion.y_root - g_move_y_offset);
					break;
				}

				if (g_fullscreen && !g_focused)
					XSetInputFocus(g_display, g_wnd, RevertToPointerRoot,
						       CurrentTime);
				rdp_send_input(time(NULL), RDP_INPUT_MOUSE,
					       MOUSE_FLAG_MOVE, xevent.xmotion.x, xevent.xmotion.y);
				break;

			case FocusIn:
				if (xevent.xfocus.mode == NotifyGrab)
					break;
				g_focused = True;
				XQueryPointer(g_display, g_wnd, &wdummy, &wdummy, &dummy, &dummy,
					      &dummy, &dummy, &state);
				reset_modifier_keys(state);
				if (g_grab_keyboard && g_mouse_in_wnd)
					XGrabKeyboard(g_display, g_wnd, True,
						      GrabModeAsync, GrabModeAsync, CurrentTime);
				break;

			case FocusOut:
				if (xevent.xfocus.mode == NotifyUngrab)
					break;
				g_focused = False;
				if (xevent.xfocus.mode == NotifyWhileGrabbed)
					XUngrabKeyboard(g_display, CurrentTime);
				break;

			case EnterNotify:
				/* we only register for this event when in fullscreen mode */
				/* or grab_keyboard */
				g_mouse_in_wnd = True;
				if (g_fullscreen)
				{
					XSetInputFocus(g_display, g_wnd, RevertToPointerRoot,
						       CurrentTime);
					break;
				}
				if (g_focused)
					XGrabKeyboard(g_display, g_wnd, True,
						      GrabModeAsync, GrabModeAsync, CurrentTime);
				break;

			case LeaveNotify:
				/* we only register for this event when grab_keyboard */
				g_mouse_in_wnd = False;
				XUngrabKeyboard(g_display, CurrentTime);
				break;

			case Expose:
				XCopyArea(g_display, g_backstore, g_wnd, g_gc,
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
					XFreeModifiermap(g_mod_map);
					g_mod_map = XGetModifierMapping(g_display);
				}
				break;

				/* clipboard stuff */
			case SelectionNotify:
				xclip_handle_SelectionNotify(&xevent.xselection);
				break;
			case SelectionRequest:
				xclip_handle_SelectionRequest(&xevent.xselectionrequest);
				break;
			case SelectionClear:
				xclip_handle_SelectionClear();
				break;
			case PropertyNotify:
				xclip_handle_PropertyNotify(&xevent.xproperty);
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
	int n = (rdp_socket > g_x_socket) ? rdp_socket + 1 : g_x_socket + 1;
	fd_set rfds, wfds;

	while (True)
	{
		/* Process any events already waiting */
		if (!xwin_process_events())
			/* User quit */
			return 0;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(rdp_socket, &rfds);
		FD_SET(g_x_socket, &rfds);

#ifdef WITH_RDPSND
		/* FIXME: there should be an API for registering fds */
		if (g_dsp_busy)
		{
			FD_SET(g_dsp_fd, &wfds);
			n = (g_dsp_fd + 1 > n) ? g_dsp_fd + 1 : n;
		}
#endif

		switch (select(n, &rfds, &wfds, NULL, NULL))
		{
			case -1:
				error("select: %s\n", strerror(errno));

			case 0:
				continue;
		}

		if (FD_ISSET(rdp_socket, &rfds))
			return 1;

#ifdef WITH_RDPSND
		if (g_dsp_busy && FD_ISSET(g_dsp_fd, &wfds))
			wave_out_play();
#endif
	}
}

void
ui_move_pointer(int x, int y)
{
	XWarpPointer(g_display, g_wnd, g_wnd, 0, 0, 0, 0, x, y);
}

HBITMAP
ui_create_bitmap(int width, int height, uint8 * data)
{
	XImage *image;
	Pixmap bitmap;
	uint8 *tdata;
	int bitmap_pad;

	if (g_server_bpp == 8)
	{
		bitmap_pad = 8;
	}
	else
	{
		bitmap_pad = g_bpp;

		if (g_bpp == 24)
			bitmap_pad = 32;
	}

	tdata = (g_owncolmap ? data : translate_image(width, height, data));
	bitmap = XCreatePixmap(g_display, g_wnd, width, height, g_depth);
	image = XCreateImage(g_display, g_visual, g_depth, ZPixmap, 0,
			     (char *) tdata, width, height, bitmap_pad, 0);

	XPutImage(g_display, bitmap, g_gc, image, 0, 0, 0, 0, width, height);

	XFree(image);
	if (!g_owncolmap)
		xfree(tdata);
	return (HBITMAP) bitmap;
}

void
ui_paint_bitmap(int x, int y, int cx, int cy, int width, int height, uint8 * data)
{
	XImage *image;
	uint8 *tdata;
	int bitmap_pad;

	if (g_server_bpp == 8)
	{
		bitmap_pad = 8;
	}
	else
	{
		bitmap_pad = g_bpp;

		if (g_bpp == 24)
			bitmap_pad = 32;
	}

	tdata = (g_owncolmap ? data : translate_image(width, height, data));
	image = XCreateImage(g_display, g_visual, g_depth, ZPixmap, 0,
			     (char *) tdata, width, height, bitmap_pad, 0);

	if (g_ownbackstore)
	{
		XPutImage(g_display, g_backstore, g_gc, image, 0, 0, x, y, cx, cy);
		XCopyArea(g_display, g_backstore, g_wnd, g_gc, x, y, cx, cy, x, y);
	}
	else
	{
		XPutImage(g_display, g_wnd, g_gc, image, 0, 0, x, y, cx, cy);
	}

	XFree(image);
	if (!g_owncolmap)
		xfree(tdata);
}

void
ui_destroy_bitmap(HBITMAP bmp)
{
	XFreePixmap(g_display, (Pixmap) bmp);
}

HGLYPH
ui_create_glyph(int width, int height, uint8 * data)
{
	XImage *image;
	Pixmap bitmap;
	int scanline;
	GC gc;

	scanline = (width + 7) / 8;

	bitmap = XCreatePixmap(g_display, g_wnd, width, height, 1);
	gc = XCreateGC(g_display, bitmap, 0, NULL);

	image = XCreateImage(g_display, g_visual, 1, ZPixmap, 0, (char *) data,
			     width, height, 8, scanline);
	image->byte_order = MSBFirst;
	image->bitmap_bit_order = MSBFirst;
	XInitImage(image);

	XPutImage(g_display, bitmap, gc, image, 0, 0, 0, 0, width, height);

	XFree(image);
	XFreeGC(g_display, gc);
	return (HGLYPH) bitmap;
}

void
ui_destroy_glyph(HGLYPH glyph)
{
	XFreePixmap(g_display, (Pixmap) glyph);
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

	cursor = (uint8 *) xmalloc(offset);
	memset(cursor, 0, offset);

	mask = (uint8 *) xmalloc(offset);
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
		XCreatePixmapCursor(g_display, (Pixmap) cursorglyph,
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
	g_current_cursor = (Cursor) cursor;
	XDefineCursor(g_display, g_wnd, g_current_cursor);
}

void
ui_destroy_cursor(HCURSOR cursor)
{
	XFreeCursor(g_display, (Cursor) cursor);
}

void
ui_set_null_cursor(void)
{
	ui_set_cursor(g_null_cursor);
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
	if (!g_owncolmap)
	{
		uint32 *map = (uint32 *) xmalloc(sizeof(*g_colmap) * ncolours);
		XColor xentry;
		XColor xc_cache[256];
		uint32 colour;
		int colLookup = 256;
		for (i = 0; i < ncolours; i++)
		{
			entry = &colours->colours[i];
			MAKE_XCOLOR(&xentry, entry);

			if (XAllocColor(g_display, g_xcolmap, &xentry) == 0)
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
					XQueryColor(g_display,
						    DefaultColormap(g_display,
								    DefaultScreen(g_display)),
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

			map[i] = colour;
		}
		return map;
	}
	else
	{
		XColor *xcolours, *xentry;
		Colormap map;

		xcolours = (XColor *) xmalloc(sizeof(XColor) * ncolours);
		for (i = 0; i < ncolours; i++)
		{
			entry = &colours->colours[i];
			xentry = &xcolours[i];
			xentry->pixel = i;
			MAKE_XCOLOR(xentry, entry);
		}

		map = XCreateColormap(g_display, g_wnd, g_visual, AllocAll);
		XStoreColors(g_display, map, xcolours, ncolours);

		xfree(xcolours);
		return (HCOLOURMAP) map;
	}
}

void
ui_destroy_colourmap(HCOLOURMAP map)
{
	if (!g_owncolmap)
		xfree(map);
	else
		XFreeColormap(g_display, (Colormap) map);
}

void
ui_set_colourmap(HCOLOURMAP map)
{
	if (!g_owncolmap)
	{
		if (g_colmap)
			xfree(g_colmap);

		g_colmap = (uint32 *) map;
	}
	else
		XSetWindowColormap(g_display, g_wnd, (Colormap) map);
}

void
ui_set_clip(int x, int y, int cx, int cy)
{
	XRectangle rect;

	rect.x = x;
	rect.y = y;
	rect.width = cx;
	rect.height = cy;
	XSetClipRectangles(g_display, g_gc, 0, 0, &rect, 1, YXBanded);
}

void
ui_reset_clip(void)
{
	XRectangle rect;

	rect.x = 0;
	rect.y = 0;
	rect.width = g_width;
	rect.height = g_height;
	XSetClipRectangles(g_display, g_gc, 0, 0, &rect, 1, YXBanded);
}

void
ui_bell(void)
{
	XBell(g_display, 0);
}

void
ui_destblt(uint8 opcode,
	   /* dest */ int x, int y, int cx, int cy)
{
	SET_FUNCTION(opcode);
	FILL_RECTANGLE(x, y, cx, cy);
	RESET_FUNCTION(opcode);
}

static uint8 hatch_patterns[] = {
	0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,	/* 0 - bsHorizontal */
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,	/* 1 - bsVertical */
	0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,	/* 2 - bsFDiagonal */
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,	/* 3 - bsBDiagonal */
	0x08, 0x08, 0x08, 0xff, 0x08, 0x08, 0x08, 0x08,	/* 4 - bsCross */
	0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81	/* 5 - bsDiagCross */
};

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

		case 2:	/* Hatch */
			fill = (Pixmap) ui_create_glyph(8, 8,
							hatch_patterns + brush->pattern[0] * 8);
			SET_FOREGROUND(fgcolour);
			SET_BACKGROUND(bgcolour);
			XSetFillStyle(g_display, g_gc, FillOpaqueStippled);
			XSetStipple(g_display, g_gc, fill);
			XSetTSOrigin(g_display, g_gc, brush->xorigin, brush->yorigin);
			FILL_RECTANGLE(x, y, cx, cy);
			XSetFillStyle(g_display, g_gc, FillSolid);
			XSetTSOrigin(g_display, g_gc, 0, 0);
			ui_destroy_glyph((HGLYPH) fill);
			break;

		case 3:	/* Pattern */
			for (i = 0; i != 8; i++)
				ipattern[7 - i] = brush->pattern[i];
			fill = (Pixmap) ui_create_glyph(8, 8, ipattern);

			SET_FOREGROUND(bgcolour);
			SET_BACKGROUND(fgcolour);
			XSetFillStyle(g_display, g_gc, FillOpaqueStippled);
			XSetStipple(g_display, g_gc, fill);
			XSetTSOrigin(g_display, g_gc, brush->xorigin, brush->yorigin);

			FILL_RECTANGLE(x, y, cx, cy);

			XSetFillStyle(g_display, g_gc, FillSolid);
			XSetTSOrigin(g_display, g_gc, 0, 0);
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
	XCopyArea(g_display, g_wnd, g_wnd, g_gc, srcx, srcy, cx, cy, x, y);
	if (g_ownbackstore)
		XCopyArea(g_display, g_backstore, g_backstore, g_gc, srcx, srcy, cx, cy, x, y);
	RESET_FUNCTION(opcode);
}

void
ui_memblt(uint8 opcode,
	  /* dest */ int x, int y, int cx, int cy,
	  /* src */ HBITMAP src, int srcx, int srcy)
{
	SET_FUNCTION(opcode);
	XCopyArea(g_display, (Pixmap) src, g_wnd, g_gc, srcx, srcy, cx, cy, x, y);
	if (g_ownbackstore)
		XCopyArea(g_display, (Pixmap) src, g_backstore, g_gc, srcx, srcy, cx, cy, x, y);
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
	XDrawLine(g_display, g_wnd, g_gc, startx, starty, endx, endy);
	if (g_ownbackstore)
		XDrawLine(g_display, g_backstore, g_gc, startx, starty, endx, endy);
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

	XSetFillStyle(g_display, g_gc,
		      (mixmode == MIX_TRANSPARENT) ? FillStippled : FillOpaqueStippled);
	XSetStipple(g_display, g_gc, (Pixmap) glyph);
	XSetTSOrigin(g_display, g_gc, x, y);

	FILL_RECTANGLE_BACKSTORE(x, y, cx, cy);

	XSetFillStyle(g_display, g_gc, FillSolid);
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
	if (g_ownbackstore)
	{
		if (boxcx > 1)
			XCopyArea(g_display, g_backstore, g_wnd, g_gc, boxx,
				  boxy, boxcx, boxcy, boxx, boxy);
		else
			XCopyArea(g_display, g_backstore, g_wnd, g_gc, clipx,
				  clipy, clipcx, clipcy, clipx, clipy);
	}
}

void
ui_desktop_save(uint32 offset, int x, int y, int cx, int cy)
{
	Pixmap pix;
	XImage *image;

	if (g_ownbackstore)
	{
		image = XGetImage(g_display, g_backstore, x, y, cx, cy, AllPlanes, ZPixmap);
	}
	else
	{
		pix = XCreatePixmap(g_display, g_wnd, cx, cy, g_depth);
		XCopyArea(g_display, g_wnd, pix, g_gc, x, y, cx, cy, 0, 0);
		image = XGetImage(g_display, pix, 0, 0, cx, cy, AllPlanes, ZPixmap);
		XFreePixmap(g_display, pix);
	}

	offset *= g_bpp / 8;
	cache_put_desktop(offset, cx, cy, image->bytes_per_line, g_bpp / 8, (uint8 *) image->data);

	XDestroyImage(image);
}

void
ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy)
{
	XImage *image;
	uint8 *data;

	offset *= g_bpp / 8;
	data = cache_get_desktop(offset, cx, cy, g_bpp / 8);
	if (data == NULL)
		return;

	image = XCreateImage(g_display, g_visual, g_depth, ZPixmap, 0,
			     (char *) data, cx, cy, BitmapPad(g_display), cx * g_bpp / 8);

	if (g_ownbackstore)
	{
		XPutImage(g_display, g_backstore, g_gc, image, 0, 0, x, y, cx, cy);
		XCopyArea(g_display, g_backstore, g_wnd, g_gc, x, y, cx, cy, x, y);
	}
	else
	{
		XPutImage(g_display, g_wnd, g_gc, image, 0, 0, x, y, cx, cy);
	}

	XFree(image);
}
