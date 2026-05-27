/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   X11 startup connection launcher
   Copyright (C) 2026 Marco Fortina <marco_fortina@hotmail.it>

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

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#ifdef HAVE_XFT
#include <X11/Xft/Xft.h>
#endif
#include <X11/keysym.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rdesktop.h"
#include "xgui_banner.h"

#define XGUI_MAX_TEXT 256
#define XGUI_MAX_FIELDS 64
#define XGUI_MAX_CHECKS 48
#define XGUI_MAX_ARGS 192
#define XGUI_TAB_COUNT 6
#define XGUI_MAIN_WIDTH 600
#define XGUI_OPTIONS_WIDTH 880
#define XGUI_MAIN_HEIGHT 430
#define XGUI_OPTIONS_HEIGHT 760
#define XGUI_FIELD_HEIGHT 28
#define XGUI_FIELD_LABEL_WIDTH 150
#define XGUI_FIELD_LABEL_GAP 14
#define XGUI_FIELD_WIDTH 330
#define XGUI_CHECK_WIDTH 270
#define XGUI_BANNER_X 24
#define XGUI_BANNER_Y 24
#define XGUI_BANNER_HEIGHT XGUI_BANNER_BITMAP_HEIGHT
#define XGUI_MAIN_FIELDS_Y 205
#define XGUI_OPTIONS_PANEL_Y 340
#define XGUI_OPTIONS_CONTENT_Y (XGUI_OPTIONS_PANEL_Y + 54)
#define XGUI_OPTIONS_FIELD_SPACING 28
#define XGUI_BUTTON_HEIGHT 32
#define XGUI_DIALOG_WIDTH 840
#define XGUI_DIALOG_HEIGHT 640
#define XGUI_DIALOG_MAX_LINES 96
#define XGUI_DIALOG_LINE_CHARS 102

typedef enum
{
	XGUI_TAB_GENERAL = 0,
	XGUI_TAB_DISPLAY,
	XGUI_TAB_RESOURCES,
	XGUI_TAB_PERFORMANCE,
	XGUI_TAB_SECURITY,
	XGUI_TAB_ADVANCED
} xgui_tab_t;

typedef enum
{
	XGUI_ACTION_NONE = 0,
	XGUI_ACTION_CONNECT,
	XGUI_ACTION_EXIT,
	XGUI_ACTION_OPTIONS
} xgui_action_t;

typedef enum
{
	XGUI_FOCUS_FIELD = 0,
	XGUI_FOCUS_CHECK,
	XGUI_FOCUS_TAB,
	XGUI_FOCUS_BUTTON
} xgui_focus_type_t;

typedef enum
{
	XGUI_BUTTON_OPTIONS = 0,
	XGUI_BUTTON_CONNECT,
	XGUI_BUTTON_EXIT
} xgui_button_t;

typedef struct
{
	const char *label;
	const char *option;
	int tab;
	RD_BOOL password;
	RD_BOOL main_field;
	char text[XGUI_MAX_TEXT];
	size_t cursor;
	int x, y, w, h;
	RD_BOOL visible;
	RD_BOOL revealed;
} xgui_field_t;

typedef struct
{
	const char *label;
	const char *option;
	const char *value;
	int tab;
	RD_BOOL inverted;
	RD_BOOL checked;
	int x, y, w, h;
	RD_BOOL visible;
} xgui_check_t;

typedef struct
{
	Display *display;
	int screen;
	Window window;
	GC gc;
	Pixmap banner_pixmap;
	Pixmap backstore_pixmap;
	Drawable drawable;
	int backstore_width;
	int backstore_height;
	XFontStruct *font;
	XFontStruct *title_font;
	XFontStruct *small_font;
#ifdef HAVE_XFT
	XftFont *xft_font;
	XftFont *xft_title_font;
	XftFont *xft_small_font;
#endif
	Atom wm_delete_window;
	Atom clipboard_atom;
	Atom utf8_string_atom;
	Atom targets_atom;
	Atom text_atom;
	Atom compound_text_atom;
	Atom clipboard_property_atom;
	unsigned long black;
	unsigned long white;
	unsigned long background;
	unsigned long banner;
	unsigned long banner_dark;
	unsigned long panel;
	unsigned long panel_border;
	unsigned long field_border;
	unsigned long button_fill;
	unsigned long button_active;
	unsigned long dark_gray;
	unsigned long mid_gray;
	unsigned long light_blue;
	unsigned long blue;
	unsigned long focus;
	int width;
	int height;
	RD_BOOL show_options;
	int active_tab;
	xgui_focus_type_t focus_type;
	int active_field;
	int active_check;
	int active_button;
	int active_reveal_field;
	char error[160];
	char clipboard_text[XGUI_MAX_TEXT];
	xgui_field_t fields[XGUI_MAX_FIELDS];
	unsigned int num_fields;
	xgui_check_t checks[XGUI_MAX_CHECKS];
	unsigned int num_checks;
} xgui_state_t;

static const char *g_tab_names[XGUI_TAB_COUNT] = {
	"General", "Display", "Local resources", "Performance", "Security", "Advanced"
};


#ifdef HAVE_XFT
static const char *const g_xgui_xft_font_fallbacks[] = {
	/*
	 * Out-of-the-box desktop UI font preference. X resources may override
	 * these names, but the launcher must look good without per-user setup.
	 */
	"Ubuntu Sans-11",
	"Adwaita Sans-11",
	"Noto Serif-11",
	"Cantarell-11",
	"DejaVu Sans-11",
	"SF Pro Text-13",
	"San Francisco-13",
	"Segoe UI-9",
	"Liberation Sans-11",
	"Arial-9",
	"sans-11",
	NULL
};

static const char *const g_xgui_xft_title_font_fallbacks[] = {
	"Ubuntu Sans Bold-13",
	"Adwaita Sans Bold-13",
	"Noto Serif Bold-13",
	"Cantarell Bold-13",
	"DejaVu Sans Bold-13",
	"SF Pro Text Semibold-15",
	"San Francisco Bold-15",
	"Segoe UI Bold-11",
	"Liberation Sans Bold-13",
	"Arial Bold-11",
	"sans:bold-13",
	NULL
};

static const char *const g_xgui_xft_small_font_fallbacks[] = {
	"Ubuntu Sans-9",
	"Adwaita Sans-9",
	"Noto Serif-9",
	"Cantarell-9",
	"DejaVu Sans-9",
	"SF Pro Text-11",
	"San Francisco-11",
	"Segoe UI-8",
	"Liberation Sans-9",
	"Arial-8",
	"sans-9",
	NULL
};
#endif

static const char *const g_xgui_font_fallbacks[] = {
	/* Prefer compact Windows-like UI core fonts when the X server exposes them. */
	"-*-tahoma-medium-r-normal--11-80-100-100-p-*-iso10646-1",
	"-*-tahoma-medium-r-normal--11-80-96-96-p-*-iso10646-1",
	"-*-segoe ui-medium-r-normal--12-90-96-96-p-*-iso10646-1",
	"-*-ui sans-medium-r-normal--12-90-96-96-p-*-iso10646-1",
	"-b&h-lucida-medium-r-normal-sans-10-100-75-75-p-58-iso8859-1",
	"-*-lucida-medium-r-normal-sans-10-100-75-75-p-*-iso8859-1",
	"-*-liberation sans-medium-r-normal--11-80-96-96-p-*-iso10646-1",
	"-*-dejavu sans-medium-r-normal--11-80-96-96-p-*-iso10646-1",
	"-adobe-helvetica-medium-r-normal--11-80-100-100-p-56-iso8859-1",
	"-adobe-helvetica-medium-r-normal--10-100-75-75-p-56-iso8859-1",
	"variable",
	"6x13",
	"fixed",
	NULL
};

static const char *const g_xgui_title_font_fallbacks[] = {
	"-*-tahoma-bold-r-normal--18-140-96-96-p-*-iso10646-1",
	"-*-segoe ui-bold-r-normal--18-140-96-96-p-*-iso10646-1",
	"-*-ui sans-bold-r-normal--18-140-96-96-p-*-iso10646-1",
	"-b&h-lucida-bold-r-normal-sans-18-180-75-75-p-107-iso8859-1",
	"-*-liberation sans-bold-r-normal--18-140-96-96-p-*-iso10646-1",
	"-*-dejavu sans-bold-r-normal--18-140-96-96-p-*-iso10646-1",
	"-adobe-helvetica-bold-r-normal--18-180-75-75-p-103-iso8859-1",
	"10x20",
	"fixed",
	NULL
};

static const char *const g_xgui_small_font_fallbacks[] = {
	"-*-tahoma-medium-r-normal--10-75-100-100-p-*-iso10646-1",
	"-*-tahoma-medium-r-normal--10-75-96-96-p-*-iso10646-1",
	"-*-segoe ui-medium-r-normal--10-75-96-96-p-*-iso10646-1",
	"-*-ui sans-medium-r-normal--10-75-96-96-p-*-iso10646-1",
	"-b&h-lucida-medium-r-normal-sans-10-100-75-75-p-58-iso8859-1",
	"-*-liberation sans-medium-r-normal--10-75-96-96-p-*-iso10646-1",
	"-*-dejavu sans-medium-r-normal--10-75-96-96-p-*-iso10646-1",
	"-adobe-helvetica-medium-r-normal--10-100-75-75-p-56-iso8859-1",
	"6x13",
	"fixed",
	NULL
};

static char *
xgui_strdup(const char *value)
{
	char *copy;

	copy = xmalloc(strlen(value) + 1);
	strcpy(copy, value);
	return copy;
}

static RD_BOOL
xgui_nonempty(const char *value)
{
	while (*value)
	{
		if (!isspace((unsigned char) *value))
			return True;
		value++;
	}
	return False;
}


static void
xgui_get_center_position(Display *display, int screen, int width, int height, int *x, int *y)
{
	int screen_width;
	int screen_height;

	screen_width = DisplayWidth(display, screen);
	screen_height = DisplayHeight(display, screen);

	*x = (screen_width > width) ? (screen_width - width) / 2 : 0;
	*y = (screen_height > height) ? (screen_height - height) / 2 : 0;
}

static void
xgui_move_centered(Display *display, int screen, Window window, int width, int height)
{
	int x;
	int y;

	xgui_get_center_position(display, screen, width, height, &x, &y);
	XMoveWindow(display, window, x, y);
}

static char *
xgui_lookup_xresource(Display *display, const char *name, const char *class_name)
{
	XrmDatabase database;
	XrmValue value;
	char *type;
	char *resources;
	char *result;

	resources = XResourceManagerString(display);
	if (resources == NULL)
		return NULL;

	database = XrmGetStringDatabase(resources);
	if (database == NULL)
		return NULL;

	result = NULL;
	type = NULL;
	memset(&value, 0, sizeof(value));
	if (XrmGetResource(database, name, class_name, &type, &value) &&
	    value.addr != NULL && value.size > 1)
		result = xgui_strdup(value.addr);

	XrmDestroyDatabase(database);
	return result;
}

#ifdef HAVE_XFT
static XftFont *
xgui_load_first_xft_font(Display *display, int screen, const char *const *names)
{
	XftFont *font;
	unsigned int i;

	for (i = 0; names[i] != NULL; i++)
	{
		font = XftFontOpenName(display, screen, names[i]);
		if (font != NULL)
			return font;
	}

	return XftFontOpenName(display, screen, "sans-11");
}

static XftFont *
xgui_load_resource_xft_font(Display *display, int screen, const char *name,
                            const char *class_name, const char *const *fallbacks)
{
	char *resource_font;
	XftFont *font;

	resource_font = xgui_lookup_xresource(display, name, class_name);
	if (resource_font != NULL)
	{
		font = XftFontOpenName(display, screen, resource_font);
		xfree(resource_font);
		if (font != NULL)
			return font;
	}

	return xgui_load_first_xft_font(display, screen, fallbacks);
}
#endif

static XFontStruct *
xgui_load_first_font(Display *display, const char *const *names)
{
	XFontStruct *font;
	unsigned int i;

	for (i = 0; names[i] != NULL; i++)
	{
		font = XLoadQueryFont(display, names[i]);
		if (font != NULL)
			return font;
	}

	return XLoadQueryFont(display, "fixed");
}

static XFontStruct *
xgui_load_resource_font(Display *display, const char *name, const char *class_name,
                         const char *const *fallbacks)
{
	char *resource_font;
	XFontStruct *font;

	resource_font = xgui_lookup_xresource(display, name, class_name);
	if (resource_font != NULL)
	{
		font = XLoadQueryFont(display, resource_font);
		xfree(resource_font);
		if (font != NULL)
			return font;
	}

	return xgui_load_first_font(display, fallbacks);
}

static void
xgui_set_font(xgui_state_t *gui, XFontStruct *font)
{
	if (font != NULL)
		XSetFont(gui->display, gui->gc, font->fid);
}

#ifdef HAVE_XFT
static XftFont *
xgui_xft_font_for_core_font(xgui_state_t *gui, XFontStruct *font)
{
	if (font == gui->title_font)
		return gui->xft_title_font;
	if (font == gui->small_font)
		return gui->xft_small_font;
	return gui->xft_font;
}
#endif

static int
xgui_text_width(xgui_state_t *gui, XFontStruct *font, const char *text)
{
#ifdef HAVE_XFT
	XGlyphInfo extents;
	XftFont *xft_font;
#endif

	if (text == NULL)
		return 0;

#ifndef HAVE_XFT
	(void) gui;
#endif

#ifdef HAVE_XFT
	xft_font = xgui_xft_font_for_core_font(gui, font);
	if (xft_font != NULL)
	{
		XftTextExtentsUtf8(gui->display, xft_font, (const FcChar8 *) text,
		                   strlen(text), &extents);
		return extents.xOff;
	}
#endif

	if (font == NULL)
		return 0;
	return XTextWidth(font, text, strlen(text));
}

static void
xgui_draw_string_font(xgui_state_t *gui, XFontStruct *font, int x, int y, const char *text)
{
#ifdef HAVE_XFT
	XftDraw *draw;
	XftColor color;
	XftFont *xft_font;
	XGCValues values;
	XColor xcolor;
	Colormap colormap;

	xft_font = xgui_xft_font_for_core_font(gui, font);
	if (xft_font != NULL && text != NULL)
	{
		colormap = DefaultColormap(gui->display, gui->screen);
		memset(&values, 0, sizeof(values));
		if (XGetGCValues(gui->display, gui->gc, GCForeground, &values))
		{
			memset(&xcolor, 0, sizeof(xcolor));
			xcolor.pixel = values.foreground;
			XQueryColor(gui->display, colormap, &xcolor);
			color.pixel = values.foreground;
			color.color.red = xcolor.red;
			color.color.green = xcolor.green;
			color.color.blue = xcolor.blue;
			color.color.alpha = 0xffff;
			draw = XftDrawCreate(gui->display, gui->drawable,
			                     DefaultVisual(gui->display, gui->screen), colormap);
			if (draw != NULL)
			{
				XftDrawStringUtf8(draw, &color, xft_font, x, y,
				                  (const FcChar8 *) text, strlen(text));
				XftDrawDestroy(draw);
				return;
			}
		}
	}
#endif

	xgui_set_font(gui, font);
	XDrawString(gui->display, gui->drawable, gui->gc, x, y, text, strlen(text));
}

static void
xgui_draw_string(xgui_state_t *gui, int x, int y, const char *text)
{
	xgui_draw_string_font(gui, gui->font, x, y, text);
}

static unsigned long
xgui_alloc_color(xgui_state_t *gui, const char *name, unsigned long fallback)
{
	XColor color, exact;

	if (XAllocNamedColor(gui->display, DefaultColormap(gui->display, gui->screen), name,
	                    &color, &exact))
		return color.pixel;
	return fallback;
}

static unsigned long
xgui_alloc_rgb_color(xgui_state_t *gui, unsigned int red, unsigned int green,
                     unsigned int blue, unsigned long fallback)
{
	XColor color;

	memset(&color, 0, sizeof(color));
	color.red = red * 257;
	color.green = green * 257;
	color.blue = blue * 257;
	color.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(gui->display, DefaultColormap(gui->display, gui->screen), &color))
		return color.pixel;
	return fallback;
}

static void
xgui_add_field(xgui_state_t *gui, int tab, const char *label, const char *option,
               RD_BOOL password, RD_BOOL main_field, const char *defvalue)
{
	xgui_field_t *field;

	if (gui->num_fields >= XGUI_MAX_FIELDS)
		return;

	field = &gui->fields[gui->num_fields++];
	memset(field, 0, sizeof(*field));
	field->label = label;
	field->option = option;
	field->tab = tab;
	field->password = password;
	field->main_field = main_field;
	if (defvalue != NULL)
		STRNCPY(field->text, defvalue, sizeof(field->text));
	field->cursor = strlen(field->text);
}

static void
xgui_add_check(xgui_state_t *gui, int tab, const char *label, const char *option,
               const char *value, RD_BOOL inverted, RD_BOOL checked)
{
	xgui_check_t *check;

	if (gui->num_checks >= XGUI_MAX_CHECKS)
		return;

	check = &gui->checks[gui->num_checks++];
	memset(check, 0, sizeof(*check));
	check->label = label;
	check->option = option;
	check->value = value;
	check->tab = tab;
	check->inverted = inverted;
	check->checked = checked;
}

static void
xgui_init_controls(xgui_state_t *gui)
{
	xgui_add_field(gui, XGUI_TAB_GENERAL, "Computer", NULL, False, True, "");
	xgui_add_field(gui, XGUI_TAB_GENERAL, "User name", "-u", False, True, "");
	xgui_add_field(gui, XGUI_TAB_GENERAL, "Password", "-p", True, True, "");
	xgui_add_field(gui, XGUI_TAB_GENERAL, "Domain", "-d", False, True, "");

	xgui_add_field(gui, XGUI_TAB_GENERAL, "Shell", "-s", False, False, "");
	xgui_add_field(gui, XGUI_TAB_GENERAL, "Working directory", "-c", False, False, "");
	xgui_add_field(gui, XGUI_TAB_GENERAL, "Client hostname", "-n", False, False, "");
	xgui_add_field(gui, XGUI_TAB_GENERAL, "Keyboard layout", "-k", False, False, "");
	xgui_add_field(gui, XGUI_TAB_GENERAL, "Local codepage", "-L", False, False, "");

	xgui_add_field(gui, XGUI_TAB_DISPLAY, "Geometry", "-g", False, False, "");
	xgui_add_field(gui, XGUI_TAB_DISPLAY, "Colour depth", "-a", False, False, "");
	xgui_add_field(gui, XGUI_TAB_DISPLAY, "Window title", "-T", False, False, "");
	xgui_add_field(gui, XGUI_TAB_DISPLAY, "WM_CLASS", "-w", False, False, "");
	xgui_add_field(gui, XGUI_TAB_DISPLAY, "Window icon PNG", "--window-icon", False, False, "");
	xgui_add_field(gui, XGUI_TAB_DISPLAY, "Embed window id", "-X", False, False, "");
	xgui_add_field(gui, XGUI_TAB_DISPLAY, "Keyboard ungrab", "-J", False, False, "");
	xgui_add_field(gui, XGUI_TAB_DISPLAY, "Caption button size", "-S", False, False, "");

	xgui_add_field(gui, XGUI_TAB_RESOURCES, "Disk", "-r", False, False, "");
	xgui_add_field(gui, XGUI_TAB_RESOURCES, "Serial", "-r", False, False, "");
	xgui_add_field(gui, XGUI_TAB_RESOURCES, "Parallel", "-r", False, False, "");
	xgui_add_field(gui, XGUI_TAB_RESOURCES, "Printer", "-r", False, False, "");
	xgui_add_field(gui, XGUI_TAB_RESOURCES, "Sound", "-r", False, False, "");
	xgui_add_field(gui, XGUI_TAB_RESOURCES, "Clipboard", "-r", False, False, "");
	xgui_add_field(gui, XGUI_TAB_RESOURCES, "RDPDR client name", "-r", False, False, "");
	xgui_add_field(gui, XGUI_TAB_RESOURCES, "Add-in channel", "-r", False, False, "");
	xgui_add_field(gui, XGUI_TAB_RESOURCES, "Smart card", "-r", False, False, "");

	xgui_add_field(gui, XGUI_TAB_PERFORMANCE, "Experience", "-x", False, False, "");
	xgui_add_field(gui, XGUI_TAB_PERFORMANCE, "TLS version", "-V", False, False, "");

	xgui_add_field(gui, XGUI_TAB_SECURITY, "Shadow session id", "--shadow", False, False, "");

	xgui_add_field(gui, XGUI_TAB_ADVANCED, "SeamlessRDP shell", "-A", False, False, "");
	xgui_add_field(gui, XGUI_TAB_ADVANCED, "Smartcard CSP", "-o", False, False, "");
	xgui_add_field(gui, XGUI_TAB_ADVANCED, "Smartcard reader", "-o", False, False, "");
	xgui_add_field(gui, XGUI_TAB_ADVANCED, "Smartcard card", "-o", False, False, "");
	xgui_add_field(gui, XGUI_TAB_ADVANCED, "Smartcard container", "-o", False, False, "");

	xgui_add_check(gui, XGUI_TAB_DISPLAY, "Fullscreen", "-f", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_DISPLAY, "Use multi-monitor layout", "--multimon", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_DISPLAY, "Hide window decorations", "-D", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_DISPLAY, "Use private colour map", "-C", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_DISPLAY, "Keep window manager key bindings", "-K", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_DISPLAY, "Use local mouse cursor", "-M", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_DISPLAY, "Enable numlock sync", "-N", NULL, False, False);

	xgui_add_check(gui, XGUI_TAB_RESOURCES, "Enable PCI redirection helper", "-r", "lspci", False, False);

	xgui_add_check(gui, XGUI_TAB_PERFORMANCE, "Enable compression", "-z", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_PERFORMANCE, "Use persistent bitmap cache", "-P", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_PERFORMANCE, "Force bitmap updates", "-b", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_PERFORMANCE, "Use X BackingStore", "-B", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_PERFORMANCE, "Do not send motion events", "-m", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_PERFORMANCE, "Retry on network errors", "-R", NULL, False, False);

	xgui_add_check(gui, XGUI_TAB_SECURITY, "Admin/console session", "-0", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_SECURITY, "Restricted Admin mode", "--restricted-admin", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_SECURITY, "Disable encryption", "-e", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_SECURITY, "Disable client packet encryption", "-E", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_SECURITY, "Use RDP4", "-4", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_SECURITY, "Use RDP5", "-5", NULL, False, False);

	xgui_add_check(gui, XGUI_TAB_ADVANCED, "Disable remote ctrl", "-t", NULL, False, False);
	xgui_add_check(gui, XGUI_TAB_ADVANCED, "Verbose logging", "-v", NULL, False, False);
#ifdef WITH_SCARD
	xgui_add_check(gui, XGUI_TAB_ADVANCED, "Use password as smartcard PIN", "-i", NULL, False, False);
#endif
}

static void
xgui_draw_bevel(xgui_state_t *gui, int x, int y, int w, int h, RD_BOOL active)
{
	XSetForeground(gui->display, gui->gc, active ? gui->button_active : gui->button_fill);
	XFillRectangle(gui->display, gui->drawable, gui->gc, x, y, w, h);
	XSetForeground(gui->display, gui->gc, gui->white);
	XDrawLine(gui->display, gui->drawable, gui->gc, x, y, x + w - 1, y);
	XDrawLine(gui->display, gui->drawable, gui->gc, x, y, x, y + h - 1);
	XSetForeground(gui->display, gui->gc, gui->panel_border);
	XDrawLine(gui->display, gui->drawable, gui->gc, x, y + h - 1, x + w - 1, y + h - 1);
	XDrawLine(gui->display, gui->drawable, gui->gc, x + w - 1, y, x + w - 1, y + h - 1);
}

static void
xgui_draw_focus_rect(xgui_state_t *gui, int x, int y, int w, int h)
{
	XSetForeground(gui->display, gui->gc, gui->focus);
	XDrawRectangle(gui->display, gui->drawable, gui->gc, x - 2, y - 2, w + 4, h + 4);
	XDrawRectangle(gui->display, gui->drawable, gui->gc, x - 3, y - 3, w + 6, h + 6);
}

static RD_BOOL
xgui_field_has_reveal_button(const xgui_field_t *field)
{
	return field != NULL && field->password && field->visible;
}

static int
xgui_reveal_button_x(const xgui_field_t *field)
{
	return field->x + field->w - 31;
}

static void
xgui_draw_reveal_button(xgui_state_t *gui, const xgui_field_t *field, RD_BOOL active)
{
	int x;
	int y;
	int cx;
	int cy;

	if (!xgui_field_has_reveal_button(field))
		return;

	x = xgui_reveal_button_x(field);
	y = field->y + 3;
	cx = x + 14;
	cy = y + 11;

	XSetForeground(gui->display, gui->gc, active ? gui->button_active : gui->button_fill);
	XFillRectangle(gui->display, gui->drawable, gui->gc, x, y, 26, field->h - 6);
	XSetForeground(gui->display, gui->gc, active ? gui->focus : gui->field_border);
	XDrawRectangle(gui->display, gui->drawable, gui->gc, x, y, 26, field->h - 6);

	/* Simple built-in eye glyph. Keep it Xlib-only and dependency-free. */
	XSetForeground(gui->display, gui->gc, gui->dark_gray);
	XDrawArc(gui->display, gui->drawable, gui->gc, x + 5, y + 5, 16, 10, 0, 360 * 64);
	XDrawLine(gui->display, gui->drawable, gui->gc, x + 5, y + 10, x + 2, y + 10);
	XDrawLine(gui->display, gui->drawable, gui->gc, x + 21, y + 10, x + 24, y + 10);
	if (active)
	{
		XFillArc(gui->display, gui->drawable, gui->gc, cx - 3, cy - 3, 6, 6, 0, 360 * 64);
	}
	else
	{
		XDrawLine(gui->display, gui->drawable, gui->gc, x + 5, y + 17, x + 21, y + 3);
	}
}

static void
xgui_draw_button(xgui_state_t *gui, int x, int y, int w, int h, const char *label,
                 xgui_button_t button, RD_BOOL active)
{
	int text_width;
	RD_BOOL focused;

	focused = (gui->focus_type == XGUI_FOCUS_BUTTON && gui->active_button == (int) button);
	xgui_draw_bevel(gui, x, y, w, h, active || focused);
	text_width = xgui_text_width(gui, gui->font, label);
	XSetForeground(gui->display, gui->gc, gui->black);
	xgui_draw_string(gui, x + (w - text_width) / 2, y + 21, label);
	if (focused)
		xgui_draw_focus_rect(gui, x, y, w, h);
}

static unsigned long
xgui_banner_palette_pixel(xgui_state_t *gui, uint32 rgb)
{
	return xgui_alloc_rgb_color(gui, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff,
	                           rgb & 0xff, gui->white);
}

static void
xgui_create_banner_pixmap(xgui_state_t *gui)
{
	unsigned long pixels[XGUI_BANNER_BITMAP_COLORS];
	unsigned int i;
	unsigned int pos;
	int x, y;

	if (gui->banner_pixmap != 0)
		return;

	gui->banner_pixmap = XCreatePixmap(gui->display, gui->window,
	                                  XGUI_BANNER_BITMAP_WIDTH,
	                                  XGUI_BANNER_BITMAP_HEIGHT,
	                                  DefaultDepth(gui->display, gui->screen));
	if (gui->banner_pixmap == 0)
		return;

	for (i = 0; i < XGUI_BANNER_BITMAP_COLORS; i++)
		pixels[i] = xgui_banner_palette_pixel(gui, g_xgui_banner_palette[i]);

	x = 0;
	y = 0;
	for (pos = 0; pos + 1 < sizeof(g_xgui_banner_rle) && y < XGUI_BANNER_BITMAP_HEIGHT; pos += 2)
	{
		int run = g_xgui_banner_rle[pos];
		int color = g_xgui_banner_rle[pos + 1];

		if (color >= XGUI_BANNER_BITMAP_COLORS)
			color = 0;
		XSetForeground(gui->display, gui->gc, pixels[color]);
		while (run > 0 && y < XGUI_BANNER_BITMAP_HEIGHT)
		{
			int take = XGUI_BANNER_BITMAP_WIDTH - x;
			if (take > run)
				take = run;
			XFillRectangle(gui->display, gui->banner_pixmap, gui->gc,
			               x, y, take, 1);
			x += take;
			run -= take;
			if (x >= XGUI_BANNER_BITMAP_WIDTH)
			{
				x = 0;
				y++;
			}
		}
	}
}

static void
xgui_draw_logo(xgui_state_t *gui)
{
	int x;
	int y;

	x = (gui->width - XGUI_BANNER_BITMAP_WIDTH) / 2;
	y = XGUI_BANNER_Y;
	if (gui->banner_pixmap == 0)
		xgui_create_banner_pixmap(gui);

	if (gui->banner_pixmap != 0)
	{
		XCopyArea(gui->display, gui->banner_pixmap, gui->drawable, gui->gc,
		          0, 0, XGUI_BANNER_BITMAP_WIDTH, XGUI_BANNER_BITMAP_HEIGHT,
		          x, y);
		return;
	}

	/* Minimal fallback for exotic visuals where the raster pixmap cannot be created. */
	XSetForeground(gui->display, gui->gc, gui->panel);
	XFillRectangle(gui->display, gui->drawable, gui->gc, XGUI_BANNER_X, XGUI_BANNER_Y,
	               gui->width - (XGUI_BANNER_X * 2), XGUI_BANNER_HEIGHT);
	XSetForeground(gui->display, gui->gc, gui->blue);
	xgui_draw_string_font(gui, gui->title_font, XGUI_BANNER_X + 24, XGUI_BANNER_Y + 56,
	                      "RDESKTOP");
	XSetForeground(gui->display, gui->gc, gui->dark_gray);
	xgui_draw_string_font(gui, gui->font, XGUI_BANNER_X + 24, XGUI_BANNER_Y + 84,
	                      "Remote Desktop Connection");
}

static void
xgui_layout_controls(xgui_state_t *gui)
{
	unsigned int i;
	int y;
	int col;
	int row;

	row = 0;
	for (i = 0; i < gui->num_fields; i++)
	{
		xgui_field_t *field = &gui->fields[i];
		field->visible = False;
		if (field->main_field)
		{
			field->x = 170;
			field->y = XGUI_MAIN_FIELDS_Y + (row * 34);
			field->w = 360;
			field->h = XGUI_FIELD_HEIGHT;
			field->visible = True;
			row++;
		}
	}

	for (i = 0; i < gui->num_checks; i++)
		gui->checks[i].visible = False;

	if (!gui->show_options)
		return;

	y = XGUI_OPTIONS_CONTENT_Y;
	for (i = 0; i < gui->num_fields; i++)
	{
		xgui_field_t *field = &gui->fields[i];
		if (field->main_field || field->tab != gui->active_tab)
			continue;

		field->x = 245;
		field->y = y;
		field->w = XGUI_FIELD_WIDTH;
		field->h = XGUI_FIELD_HEIGHT;
		field->visible = True;
		y += XGUI_OPTIONS_FIELD_SPACING;
	}

	col = 0;
	y = XGUI_OPTIONS_CONTENT_Y;
	for (i = 0; i < gui->num_checks; i++)
	{
		xgui_check_t *check = &gui->checks[i];
		if (check->tab != gui->active_tab)
			continue;

		check->x = 600 + col * XGUI_CHECK_WIDTH;
		check->y = y;
		check->w = XGUI_CHECK_WIDTH;
		check->h = 23;
		check->visible = True;
		y += 27;
		if (y > gui->height - 96)
		{
			y = XGUI_OPTIONS_CONTENT_Y;
			col++;
		}
	}
}

static void
xgui_draw_field(xgui_state_t *gui, unsigned int index)
{
	xgui_field_t *field = &gui->fields[index];
	char display[XGUI_MAX_TEXT];
	char prefix[XGUI_MAX_TEXT];
	unsigned int i;
	int text_x;
	int caret_x;
	RD_BOOL focused;

	if (!field->visible)
		return;

	focused = (gui->focus_type == XGUI_FOCUS_FIELD && gui->active_field == (int) index);
	XSetForeground(gui->display, gui->gc, gui->black);
	if (field->main_field)
	{
		xgui_draw_string(gui, field->x - XGUI_FIELD_LABEL_WIDTH, field->y + 18,
		                 field->label);
	}
	else
	{
		int label_width = xgui_text_width(gui, gui->font, field->label);
		xgui_draw_string(gui, field->x - XGUI_FIELD_LABEL_GAP - label_width,
		                 field->y + 18, field->label);
	}
	XSetForeground(gui->display, gui->gc, gui->white);
	XFillRectangle(gui->display, gui->drawable, gui->gc, field->x, field->y, field->w, field->h);
	XSetForeground(gui->display, gui->gc, focused ? gui->focus : gui->field_border);
	XDrawRectangle(gui->display, gui->drawable, gui->gc, field->x, field->y, field->w, field->h);
	if (focused)
		XDrawRectangle(gui->display, gui->drawable, gui->gc, field->x + 1, field->y + 1,
		               field->w - 2, field->h - 2);

	if (field->password && !field->revealed)
	{
		for (i = 0; i < strlen(field->text) && i + 1 < sizeof(display); i++)
			display[i] = '*';
		display[i] = '\0';
	}
	else
	{
		STRNCPY(display, field->text, sizeof(display));
	}

	XSetForeground(gui->display, gui->gc, gui->black);
	text_x = field->x + 8;
	xgui_draw_string(gui, text_x, field->y + 19, display);
	xgui_draw_reveal_button(gui, field, field->revealed);

	if (focused)
	{
		STRNCPY(prefix, display, sizeof(prefix));
		if (field->cursor < strlen(prefix))
			prefix[field->cursor] = '\0';
		caret_x = text_x + xgui_text_width(gui, gui->font, prefix);
		if (caret_x > field->x + field->w - 6)
			caret_x = field->x + field->w - 6;
		XSetForeground(gui->display, gui->gc, gui->focus);
		XDrawLine(gui->display, gui->drawable, gui->gc, caret_x, field->y + 5,
		          caret_x, field->y + field->h - 5);
	}
}

static void
xgui_draw_check(xgui_state_t *gui, unsigned int index)
{
	xgui_check_t *check = &gui->checks[index];
	RD_BOOL focused;

	if (!check->visible)
		return;

	focused = (gui->focus_type == XGUI_FOCUS_CHECK && gui->active_check == (int) index);
	XSetForeground(gui->display, gui->gc, gui->white);
	XFillRectangle(gui->display, gui->drawable, gui->gc, check->x, check->y, 16, 16);
	XSetForeground(gui->display, gui->gc, focused ? gui->focus : gui->field_border);
	XDrawRectangle(gui->display, gui->drawable, gui->gc, check->x, check->y, 16, 16);
	if (check->checked)
	{
		XSetForeground(gui->display, gui->gc, gui->blue);
		XDrawLine(gui->display, gui->drawable, gui->gc, check->x + 3, check->y + 8,
		          check->x + 7, check->y + 13);
		XDrawLine(gui->display, gui->drawable, gui->gc, check->x + 7, check->y + 13,
		          check->x + 14, check->y + 3);
	}
	XSetForeground(gui->display, gui->gc, gui->black);
	xgui_draw_string(gui, check->x + 24, check->y + 13, check->label);
	if (focused)
		xgui_draw_focus_rect(gui, check->x, check->y, check->w - 4, 20);
}

static void
xgui_draw_tabs(xgui_state_t *gui)
{
	int i;
	int x;
	int y;
	int w;

	y = XGUI_OPTIONS_PANEL_Y + 12;
	x = 42;
	w = (gui->width - 84) / XGUI_TAB_COUNT;
	for (i = 0; i < XGUI_TAB_COUNT; i++)
	{
		RD_BOOL active = (gui->active_tab == i);
		RD_BOOL focused = (gui->focus_type == XGUI_FOCUS_TAB && gui->active_tab == i);
		XSetForeground(gui->display, gui->gc, active ? gui->white : gui->button_fill);
		XFillRectangle(gui->display, gui->drawable, gui->gc, x, y, w - 4, 30);
		XSetForeground(gui->display, gui->gc, active ? gui->focus : gui->panel_border);
		XDrawRectangle(gui->display, gui->drawable, gui->gc, x, y, w - 4, 30);
		XSetForeground(gui->display, gui->gc, active ? gui->blue : gui->black);
		xgui_draw_string(gui, x + 12, y + 20, g_tab_names[i]);
		if (focused)
			xgui_draw_focus_rect(gui, x + 2, y + 2, w - 8, 26);
		x += w;
	}
}

static RD_BOOL
xgui_ensure_backstore(xgui_state_t *gui)
{
	if (gui->backstore_pixmap != 0 && gui->backstore_width == gui->width &&
	    gui->backstore_height == gui->height)
		return True;

	if (gui->backstore_pixmap != 0)
	{
		XFreePixmap(gui->display, gui->backstore_pixmap);
		gui->backstore_pixmap = 0;
	}

	gui->backstore_pixmap = XCreatePixmap(gui->display, gui->window,
	                                      gui->width, gui->height,
	                                      DefaultDepth(gui->display, gui->screen));
	if (gui->backstore_pixmap == 0)
	{
		gui->backstore_width = 0;
		gui->backstore_height = 0;
		return False;
	}

	gui->backstore_width = gui->width;
	gui->backstore_height = gui->height;
	return True;
}

static void
xgui_redraw(xgui_state_t *gui)
{
	unsigned int i;
	const char *options_label;
	RD_BOOL buffered;

	buffered = xgui_ensure_backstore(gui);
	gui->drawable = buffered ? gui->backstore_pixmap : gui->window;
	xgui_layout_controls(gui);
	XSetForeground(gui->display, gui->gc, gui->background);
	XFillRectangle(gui->display, gui->drawable, gui->gc, 0, 0, gui->width, gui->height);
	xgui_draw_logo(gui);

	for (i = 0; i < gui->num_fields; i++)
		xgui_draw_field(gui, i);

	if (gui->error[0])
	{
		XSetForeground(gui->display, gui->gc, gui->blue);
		xgui_draw_string(gui, 170, gui->height - 70, gui->error);
	}

	if (gui->show_options)
	{
		XSetForeground(gui->display, gui->gc, gui->panel);
		XFillRectangle(gui->display, gui->drawable, gui->gc, 24, XGUI_OPTIONS_PANEL_Y,
		               gui->width - 48, gui->height - XGUI_OPTIONS_PANEL_Y - 84);
		XSetForeground(gui->display, gui->gc, gui->panel_border);
		XDrawRectangle(gui->display, gui->drawable, gui->gc, 24, XGUI_OPTIONS_PANEL_Y,
		               gui->width - 48, gui->height - XGUI_OPTIONS_PANEL_Y - 84);
		xgui_draw_tabs(gui);

		XSetForeground(gui->display, gui->gc, gui->mid_gray);
		XDrawLine(gui->display, gui->drawable, gui->gc, 42, XGUI_OPTIONS_PANEL_Y + 48,
		          gui->width - 42, XGUI_OPTIONS_PANEL_Y + 48);

		for (i = 0; i < gui->num_fields; i++)
			xgui_draw_field(gui, i);
		for (i = 0; i < gui->num_checks; i++)
			xgui_draw_check(gui, i);
	}

	options_label = gui->show_options ? "Hide options" : "Options";
	xgui_draw_button(gui, 24, gui->height - 52, 126, XGUI_BUTTON_HEIGHT, options_label,
	                 XGUI_BUTTON_OPTIONS, gui->show_options);
	xgui_draw_button(gui, gui->width - 250, gui->height - 52, 108, XGUI_BUTTON_HEIGHT,
	                 "Connect", XGUI_BUTTON_CONNECT, False);
	xgui_draw_button(gui, gui->width - 126, gui->height - 52, 102, XGUI_BUTTON_HEIGHT,
	                 "Exit", XGUI_BUTTON_EXIT, False);

	if (buffered)
	{
		XCopyArea(gui->display, gui->backstore_pixmap, gui->window, gui->gc,
		          0, 0, gui->width, gui->height, 0, 0);
		gui->drawable = gui->window;
	}
	XFlush(gui->display);
}

static int
xgui_field_at(xgui_state_t *gui, int x, int y)
{
	unsigned int i;

	for (i = 0; i < gui->num_fields; i++)
	{
		xgui_field_t *field = &gui->fields[i];
		if (field->visible && x >= field->x && x <= field->x + field->w &&
		    y >= field->y && y <= field->y + field->h)
			return i;
	}
	return -1;
}

static int
xgui_reveal_button_at(xgui_state_t *gui, int x, int y)
{
	unsigned int i;

	for (i = 0; i < gui->num_fields; i++)
	{
		xgui_field_t *field = &gui->fields[i];
		int button_x;

		if (!xgui_field_has_reveal_button(field))
			continue;
		button_x = xgui_reveal_button_x(field);
		if (x >= button_x && x <= button_x + 26 && y >= field->y + 3 &&
		    y <= field->y + field->h - 3)
			return i;
	}
	return -1;
}

static void
xgui_clear_reveal(xgui_state_t *gui)
{
	if (gui->active_reveal_field >= 0 && gui->active_reveal_field < (int) gui->num_fields)
		gui->fields[gui->active_reveal_field].revealed = False;
	gui->active_reveal_field = -1;
}

static int
xgui_check_at(xgui_state_t *gui, int x, int y)
{
	unsigned int i;

	for (i = 0; i < gui->num_checks; i++)
	{
		xgui_check_t *check = &gui->checks[i];
		if (check->visible && x >= check->x && x <= check->x + check->w &&
		    y >= check->y && y <= check->y + check->h)
			return i;
	}
	return -1;
}

static int
xgui_tab_at(xgui_state_t *gui, int x, int y)
{
	int i;
	int tx;
	int tabw;

	if (!gui->show_options || y < XGUI_OPTIONS_PANEL_Y + 12 || y > XGUI_OPTIONS_PANEL_Y + 42)
		return -1;

	tx = 42;
	tabw = (gui->width - 84) / XGUI_TAB_COUNT;
	for (i = 0; i < XGUI_TAB_COUNT; i++)
	{
		if (x >= tx && x <= tx + tabw - 4)
			return i;
		tx += tabw;
	}
	return -1;
}

static int
xgui_button_at(xgui_state_t *gui, int x, int y)
{
	if (y < gui->height - 52 || y > gui->height - 52 + XGUI_BUTTON_HEIGHT)
		return -1;
	if (x >= 24 && x <= 150)
		return XGUI_BUTTON_OPTIONS;
	if (x >= gui->width - 250 && x <= gui->width - 142)
		return XGUI_BUTTON_CONNECT;
	if (x >= gui->width - 126 && x <= gui->width - 24)
		return XGUI_BUTTON_EXIT;
	return -1;
}

static void
xgui_focus_field(xgui_state_t *gui, int field)
{
	if (field < 0 || field >= (int) gui->num_fields)
		return;
	gui->focus_type = XGUI_FOCUS_FIELD;
	gui->active_field = field;
	gui->active_check = -1;
	gui->active_button = -1;
	if (gui->fields[field].cursor > strlen(gui->fields[field].text))
		gui->fields[field].cursor = strlen(gui->fields[field].text);
}

static void
xgui_focus_check(xgui_state_t *gui, int check)
{
	if (check < 0 || check >= (int) gui->num_checks)
		return;
	gui->focus_type = XGUI_FOCUS_CHECK;
	gui->active_check = check;
	gui->active_field = -1;
	gui->active_button = -1;
}

static void
xgui_focus_tab(xgui_state_t *gui, int tab)
{
	if (tab < 0 || tab >= XGUI_TAB_COUNT)
		return;
	gui->focus_type = XGUI_FOCUS_TAB;
	gui->active_tab = tab;
	gui->active_field = -1;
	gui->active_check = -1;
	gui->active_button = -1;
}

static void
xgui_focus_button(xgui_state_t *gui, int button)
{
	gui->focus_type = XGUI_FOCUS_BUTTON;
	gui->active_button = button;
	gui->active_field = -1;
	gui->active_check = -1;
}

static void
xgui_resize(xgui_state_t *gui)
{
	gui->height = gui->show_options ? XGUI_OPTIONS_HEIGHT : XGUI_MAIN_HEIGHT;
	gui->width = gui->show_options ? XGUI_OPTIONS_WIDTH : XGUI_MAIN_WIDTH;
	XResizeWindow(gui->display, gui->window, gui->width, gui->height);
	xgui_move_centered(gui->display, gui->screen, gui->window, gui->width, gui->height);
}

static void
xgui_focus_next(xgui_state_t *gui, RD_BOOL backward)
{
	unsigned int i;
	int current;
	int first_field = -1;
	int first_check = -1;
	int last_field = -1;
	int last_check = -1;

	for (i = 0; i < gui->num_fields; i++)
	{
		if (!gui->fields[i].visible)
			continue;
		if (first_field < 0)
			first_field = i;
		last_field = i;
	}
	for (i = 0; i < gui->num_checks; i++)
	{
		if (!gui->checks[i].visible)
			continue;
		if (first_check < 0)
			first_check = i;
		last_check = i;
	}

	if (backward)
	{
		if (gui->focus_type == XGUI_FOCUS_BUTTON)
		{
			if (gui->active_button > XGUI_BUTTON_OPTIONS)
			{
				xgui_focus_button(gui, gui->active_button - 1);
				return;
			}
			if (first_check >= 0)
			{
				xgui_focus_check(gui, last_check);
				return;
			}
			if (gui->show_options)
			{
				xgui_focus_tab(gui, XGUI_TAB_COUNT - 1);
				return;
			}
			xgui_focus_field(gui, last_field);
			return;
		}
		if (gui->focus_type == XGUI_FOCUS_CHECK)
		{
			for (current = gui->active_check - 1; current >= 0; current--)
				if (gui->checks[current].visible)
				{
					xgui_focus_check(gui, current);
					return;
				}
			if (gui->show_options)
			{
				xgui_focus_tab(gui, XGUI_TAB_COUNT - 1);
				return;
			}
		}
		if (gui->focus_type == XGUI_FOCUS_TAB)
		{
			if (gui->active_tab > 0)
			{
				xgui_focus_tab(gui, gui->active_tab - 1);
				return;
			}
		}
		for (current = gui->active_field - 1; current >= 0; current--)
			if (gui->fields[current].visible)
			{
				xgui_focus_field(gui, current);
				return;
			}
		xgui_focus_button(gui, XGUI_BUTTON_EXIT);
		return;
	}

	if (gui->focus_type == XGUI_FOCUS_FIELD)
	{
		for (current = gui->active_field + 1; current < (int) gui->num_fields; current++)
			if (gui->fields[current].visible)
			{
				xgui_focus_field(gui, current);
				return;
			}
		if (gui->show_options)
		{
			xgui_focus_tab(gui, 0);
			return;
		}
		xgui_focus_button(gui, XGUI_BUTTON_OPTIONS);
		return;
	}
	if (gui->focus_type == XGUI_FOCUS_TAB)
	{
		if (gui->active_tab < XGUI_TAB_COUNT - 1)
		{
			xgui_focus_tab(gui, gui->active_tab + 1);
			return;
		}
		if (first_check >= 0)
		{
			xgui_focus_check(gui, first_check);
			return;
		}
	}
	if (gui->focus_type == XGUI_FOCUS_CHECK)
	{
		for (current = gui->active_check + 1; current < (int) gui->num_checks; current++)
			if (gui->checks[current].visible)
			{
				xgui_focus_check(gui, current);
				return;
			}
	}
	if (gui->focus_type == XGUI_FOCUS_BUTTON && gui->active_button < XGUI_BUTTON_EXIT)
	{
		xgui_focus_button(gui, gui->active_button + 1);
		return;
	}
	xgui_focus_field(gui, first_field);
}

static xgui_action_t
xgui_handle_click(xgui_state_t *gui, int x, int y)
{
	int index;

	index = xgui_reveal_button_at(gui, x, y);
	if (index >= 0)
	{
		xgui_clear_reveal(gui);
		xgui_focus_field(gui, index);
		gui->fields[index].revealed = True;
		gui->active_reveal_field = index;
		return XGUI_ACTION_OPTIONS;
	}

	index = xgui_button_at(gui, x, y);
	if (index >= 0)
	{
		xgui_focus_button(gui, index);
		if (index == XGUI_BUTTON_OPTIONS)
		{
			gui->show_options = !gui->show_options;
			gui->error[0] = '\0';
			xgui_resize(gui);
			return XGUI_ACTION_OPTIONS;
		}
		if (index == XGUI_BUTTON_CONNECT)
			return XGUI_ACTION_CONNECT;
		return XGUI_ACTION_EXIT;
	}

	index = xgui_tab_at(gui, x, y);
	if (index >= 0)
	{
		xgui_focus_tab(gui, index);
		return XGUI_ACTION_OPTIONS;
	}

	index = xgui_field_at(gui, x, y);
	if (index >= 0)
	{
		xgui_focus_field(gui, index);
		gui->fields[index].cursor = strlen(gui->fields[index].text);
		return XGUI_ACTION_OPTIONS;
	}

	index = xgui_check_at(gui, x, y);
	if (index >= 0)
	{
		xgui_focus_check(gui, index);
		gui->checks[index].checked = !gui->checks[index].checked;
		return XGUI_ACTION_OPTIONS;
	}

	return XGUI_ACTION_OPTIONS;
}

static void
xgui_field_insert_text(xgui_field_t *field, const char *text)
{
	size_t len;
	size_t add;
	size_t pos;
	char filtered[XGUI_MAX_TEXT];
	size_t i, out;

	if (field == NULL || text == NULL)
		return;

	out = 0;
	for (i = 0; text[i] != '\0' && out + 1 < sizeof(filtered); i++)
	{
		unsigned char ch = (unsigned char) text[i];
		if (ch == '\r' || ch == '\n' || ch == '\t')
			ch = ' ';
		if (ch >= 32)
			filtered[out++] = (char) ch;
	}
	filtered[out] = '\0';

	len = strlen(field->text);
	pos = field->cursor;
	if (pos > len)
		pos = len;
	add = strlen(filtered);
	if (add == 0)
		return;
	if (len + add >= sizeof(field->text))
		add = sizeof(field->text) - len - 1;
	if (add == 0)
		return;
	memmove(field->text + pos + add, field->text + pos, len - pos + 1);
	memcpy(field->text + pos, filtered, add);
	field->cursor = pos + add;
}

static void
xgui_copy_active_field(xgui_state_t *gui)
{
	xgui_field_t *field;

	if (gui->focus_type != XGUI_FOCUS_FIELD || gui->active_field < 0 ||
	    gui->active_field >= (int) gui->num_fields)
		return;

	field = &gui->fields[gui->active_field];
	STRNCPY(gui->clipboard_text, field->text, sizeof(gui->clipboard_text));
	XStoreBuffer(gui->display, gui->clipboard_text, strlen(gui->clipboard_text), 0);
	XSetSelectionOwner(gui->display, gui->clipboard_atom, gui->window, CurrentTime);
	XSetSelectionOwner(gui->display, XA_PRIMARY, gui->window, CurrentTime);
}

static void
xgui_paste_text(xgui_state_t *gui, const char *text)
{
	if (gui->focus_type != XGUI_FOCUS_FIELD || gui->active_field < 0 ||
	    gui->active_field >= (int) gui->num_fields)
		return;
	xgui_field_insert_text(&gui->fields[gui->active_field], text);
}

static void
xgui_request_paste(xgui_state_t *gui)
{
	char *buffer;
	int nbytes;

	if (gui->focus_type != XGUI_FOCUS_FIELD)
		return;

	if (XGetSelectionOwner(gui->display, gui->clipboard_atom) != None)
	{
		XConvertSelection(gui->display, gui->clipboard_atom, gui->utf8_string_atom,
		                  gui->clipboard_property_atom, gui->window, CurrentTime);
		return;
	}

	buffer = XFetchBuffer(gui->display, &nbytes, 0);
	if (buffer != NULL)
	{
		xgui_paste_text(gui, buffer);
		XFree(buffer);
	}
}

static void
xgui_handle_selection_notify(xgui_state_t *gui, XSelectionEvent *event)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *data;

	if (event->property == None)
		return;

	data = NULL;
	if (XGetWindowProperty(gui->display, gui->window, event->property, 0, XGUI_MAX_TEXT,
	                       True, AnyPropertyType, &actual_type, &actual_format, &nitems,
	                       &bytes_after, &data) == Success && data != NULL)
	{
		xgui_paste_text(gui, (const char *) data);
		XFree(data);
	}
}

static void
xgui_handle_selection_request(xgui_state_t *gui, XSelectionRequestEvent *request)
{
	XEvent reply;
	Atom property;

	memset(&reply, 0, sizeof(reply));
	reply.xselection.type = SelectionNotify;
	reply.xselection.display = request->display;
	reply.xselection.requestor = request->requestor;
	reply.xselection.selection = request->selection;
	reply.xselection.target = request->target;
	reply.xselection.time = request->time;
	reply.xselection.property = None;

	property = request->property != None ? request->property : request->target;
	if (request->target == gui->targets_atom)
	{
		Atom targets[4];
		targets[0] = gui->utf8_string_atom;
		targets[1] = XA_STRING;
		targets[2] = gui->text_atom;
		targets[3] = gui->compound_text_atom;
		XChangeProperty(gui->display, request->requestor, property, XA_ATOM, 32,
		                PropModeReplace, (unsigned char *) targets, 4);
		reply.xselection.property = property;
	}
	else if (request->target == gui->utf8_string_atom || request->target == XA_STRING ||
	         request->target == gui->text_atom || request->target == gui->compound_text_atom)
	{
		XChangeProperty(gui->display, request->requestor, property, request->target, 8,
		                PropModeReplace, (unsigned char *) gui->clipboard_text,
		                strlen(gui->clipboard_text));
		reply.xselection.property = property;
	}

	XSendEvent(gui->display, request->requestor, False, 0, &reply);
	XFlush(gui->display);
}

static xgui_action_t
xgui_handle_key(xgui_state_t *gui, XKeyEvent *event)
{
	char buf[32];
	KeySym keysym;
	int count;
	xgui_field_t *field;
	size_t len;
	RD_BOOL ctrl;

	count = XLookupString(event, buf, sizeof(buf) - 1, &keysym, NULL);
	buf[count] = '\0';
	ctrl = (event->state & ControlMask) != 0;

	if (keysym == XK_Escape)
		return XGUI_ACTION_EXIT;
	if (keysym == XK_Tab)
	{
		xgui_focus_next(gui, (event->state & ShiftMask) != 0);
		return XGUI_ACTION_OPTIONS;
	}
	if (ctrl && (keysym == XK_c || keysym == XK_C))
	{
		xgui_copy_active_field(gui);
		return XGUI_ACTION_OPTIONS;
	}
	if (ctrl && (keysym == XK_v || keysym == XK_V))
	{
		xgui_request_paste(gui);
		return XGUI_ACTION_OPTIONS;
	}
	if ((event->state & ShiftMask) && keysym == XK_Insert)
	{
		xgui_request_paste(gui);
		return XGUI_ACTION_OPTIONS;
	}
	if (keysym == XK_space && gui->focus_type == XGUI_FOCUS_CHECK && gui->active_check >= 0)
	{
		gui->checks[gui->active_check].checked = !gui->checks[gui->active_check].checked;
		return XGUI_ACTION_OPTIONS;
	}
	if ((keysym == XK_Return || keysym == XK_KP_Enter || keysym == XK_space) &&
	    gui->focus_type == XGUI_FOCUS_BUTTON)
	{
		if (gui->active_button == XGUI_BUTTON_OPTIONS)
		{
			gui->show_options = !gui->show_options;
			xgui_resize(gui);
			return XGUI_ACTION_OPTIONS;
		}
		if (gui->active_button == XGUI_BUTTON_CONNECT)
			return XGUI_ACTION_CONNECT;
		return XGUI_ACTION_EXIT;
	}
	if ((keysym == XK_Return || keysym == XK_KP_Enter) &&
	    gui->focus_type != XGUI_FOCUS_FIELD)
		return XGUI_ACTION_CONNECT;

	if (gui->focus_type != XGUI_FOCUS_FIELD || gui->active_field < 0 ||
	    gui->active_field >= (int) gui->num_fields)
		return XGUI_ACTION_NONE;

	field = &gui->fields[gui->active_field];
	len = strlen(field->text);
	if (field->cursor > len)
		field->cursor = len;

	if (keysym == XK_Left)
	{
		if (field->cursor > 0)
			field->cursor--;
		return XGUI_ACTION_OPTIONS;
	}
	if (keysym == XK_Right)
	{
		if (field->cursor < len)
			field->cursor++;
		return XGUI_ACTION_OPTIONS;
	}
	if (keysym == XK_Home)
	{
		field->cursor = 0;
		return XGUI_ACTION_OPTIONS;
	}
	if (keysym == XK_End)
	{
		field->cursor = len;
		return XGUI_ACTION_OPTIONS;
	}
	if (keysym == XK_BackSpace)
	{
		if (field->cursor > 0)
		{
			memmove(field->text + field->cursor - 1, field->text + field->cursor,
			        len - field->cursor + 1);
			field->cursor--;
		}
		return XGUI_ACTION_OPTIONS;
	}
	if (keysym == XK_Delete)
	{
		if (field->cursor < len)
			memmove(field->text + field->cursor, field->text + field->cursor + 1,
			        len - field->cursor);
		return XGUI_ACTION_OPTIONS;
	}
	if (count > 0 && !ctrl && isprint((unsigned char) buf[0]))
	{
		xgui_field_insert_text(field, buf);
		return XGUI_ACTION_OPTIONS;
	}

	return XGUI_ACTION_NONE;
}

static void
xgui_add_arg(char **argv, int *argc, const char *value)
{
	if (*argc >= XGUI_MAX_ARGS - 1)
		return;
	argv[(*argc)++] = xgui_strdup(value);
	argv[*argc] = NULL;
}

static void
xgui_add_option_arg(char **argv, int *argc, const char *option, const char *value)
{
	if (option == NULL || !xgui_nonempty(value))
		return;
#ifdef WITH_SCARD
	if (!strcmp(option, "-o") && !strchr(value, '='))
		return;
#else
	if (!strcmp(option, "-o"))
		return;
#endif
	xgui_add_arg(argv, argc, option);
	xgui_add_arg(argv, argc, value);
}

static RD_BOOL
xgui_build_argv(xgui_state_t *gui, int *argc_out, char ***argv_out)
{
	char **argv;
	int argc = 0;
	unsigned int i;
	const char *server;
	const char *username;
	const char *password;

	server = gui->fields[0].text;
	if (!xgui_nonempty(server))
	{
		STRNCPY(gui->error, "Computer name is required.", sizeof(gui->error));
		xgui_focus_field(gui, 0);
		return False;
	}

	username = gui->fields[1].text;
	if (!xgui_nonempty(username))
	{
		STRNCPY(gui->error, "User name is required.", sizeof(gui->error));
		xgui_focus_field(gui, 1);
		return False;
	}

	password = gui->fields[2].text;
	if (!xgui_nonempty(password))
	{
		STRNCPY(gui->error, "Password is required.", sizeof(gui->error));
		xgui_focus_field(gui, 2);
		return False;
	}

	argv = xmalloc(sizeof(char *) * XGUI_MAX_ARGS);
	memset(argv, 0, sizeof(char *) * XGUI_MAX_ARGS);
	xgui_add_arg(argv, &argc, "rdesktop");

	for (i = 1; i < gui->num_fields; i++)
	{
		xgui_field_t *field = &gui->fields[i];
		xgui_add_option_arg(argv, &argc, field->option, field->text);
	}

	for (i = 0; i < gui->num_checks; i++)
	{
		xgui_check_t *check = &gui->checks[i];
		RD_BOOL enabled = check->inverted ? !check->checked : check->checked;
		if (!enabled)
			continue;
		xgui_add_arg(argv, &argc, check->option);
		if (check->value != NULL)
			xgui_add_arg(argv, &argc, check->value);
	}

	xgui_add_arg(argv, &argc, server);
	*argc_out = argc;
	*argv_out = argv;
	return True;
}

static RD_BOOL
xgui_open(xgui_state_t *gui)
{
	XSizeHints hints;
	XClassHint class_hint;
	int window_x;
	int window_y;

	gui->display = XOpenDisplay(NULL);
	if (gui->display == NULL)
		return False;

	gui->screen = DefaultScreen(gui->display);
	gui->black = BlackPixel(gui->display, gui->screen);
	gui->white = WhitePixel(gui->display, gui->screen);
	gui->background = xgui_alloc_color(gui, "#f4f6fb", gui->white);
	gui->banner = xgui_alloc_color(gui, "#2f6ccf", gui->black);
	gui->banner_dark = xgui_alloc_color(gui, "#163b76", gui->black);
	gui->panel = xgui_alloc_color(gui, "#ffffff", gui->white);
	gui->panel_border = xgui_alloc_color(gui, "#9aa7b8", gui->black);
	gui->field_border = xgui_alloc_color(gui, "#7c8798", gui->black);
	gui->button_fill = xgui_alloc_color(gui, "#eef3fb", gui->white);
	gui->button_active = xgui_alloc_color(gui, "#d7e6fb", gui->white);
	gui->dark_gray = xgui_alloc_color(gui, "#344054", gui->black);
	gui->mid_gray = xgui_alloc_color(gui, "#c0c8d2", gui->black);
	gui->light_blue = xgui_alloc_color(gui, "#e8f1ff", gui->white);
	gui->blue = xgui_alloc_color(gui, "#204f9f", gui->black);
	gui->focus = xgui_alloc_color(gui, "#0b65d8", gui->black);
	gui->width = XGUI_MAIN_WIDTH;
	gui->height = XGUI_MAIN_HEIGHT;
	gui->drawable = 0;
	xgui_get_center_position(gui->display, gui->screen, gui->width, gui->height,
	                         &window_x, &window_y);

	gui->window = XCreateSimpleWindow(gui->display, RootWindow(gui->display, gui->screen),
	                                  window_x, window_y, gui->width, gui->height, 1,
	                                  gui->panel_border, gui->background);
	if (gui->window == 0)
		return False;

	XStoreName(gui->display, gui->window, "rdesktop connection launcher");
	class_hint.res_name = "rdesktop";
	class_hint.res_class = "Rdesktop";
	XSetClassHint(gui->display, gui->window, &class_hint);
	gui->wm_delete_window = XInternAtom(gui->display, "WM_DELETE_WINDOW", False);
	gui->clipboard_atom = XInternAtom(gui->display, "CLIPBOARD", False);
	gui->utf8_string_atom = XInternAtom(gui->display, "UTF8_STRING", False);
	gui->targets_atom = XInternAtom(gui->display, "TARGETS", False);
	gui->text_atom = XInternAtom(gui->display, "TEXT", False);
	gui->compound_text_atom = XInternAtom(gui->display, "COMPOUND_TEXT", False);
	gui->clipboard_property_atom = XInternAtom(gui->display, "RDESKTOP_XGUI_CLIPBOARD", False);
	XSetWMProtocols(gui->display, gui->window, &gui->wm_delete_window, 1);
	XSelectInput(gui->display, gui->window,
	             ExposureMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask | StructureNotifyMask |
	             SelectionNotify | SelectionRequest);

	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize | PMaxSize | PPosition;
	hints.x = window_x;
	hints.y = window_y;
	hints.min_width = XGUI_MAIN_WIDTH;
	hints.min_height = XGUI_MAIN_HEIGHT;
	hints.max_width = XGUI_OPTIONS_WIDTH;
	hints.max_height = XGUI_OPTIONS_HEIGHT;
	XSetWMNormalHints(gui->display, gui->window, &hints);

	gui->gc = XCreateGC(gui->display, gui->window, 0, NULL);
	gui->drawable = gui->window;
	XrmInitialize();
	gui->font = xgui_load_resource_font(gui->display, "rdesktop.launcher.font",
	                                     "Rdesktop.Launcher.Font",
	                                     g_xgui_font_fallbacks);
	gui->title_font = xgui_load_resource_font(gui->display, "rdesktop.launcher.titleFont",
	                                           "Rdesktop.Launcher.TitleFont",
	                                           g_xgui_title_font_fallbacks);
	gui->small_font = xgui_load_resource_font(gui->display, "rdesktop.launcher.smallFont",
	                                           "Rdesktop.Launcher.SmallFont",
	                                           g_xgui_small_font_fallbacks);
#ifdef HAVE_XFT
	gui->xft_font = xgui_load_resource_xft_font(gui->display, gui->screen,
	                                           "rdesktop.launcher.font",
	                                           "Rdesktop.Launcher.Font",
	                                           g_xgui_xft_font_fallbacks);
	gui->xft_title_font = xgui_load_resource_xft_font(gui->display, gui->screen,
	                                                 "rdesktop.launcher.titleFont",
	                                                 "Rdesktop.Launcher.TitleFont",
	                                                 g_xgui_xft_title_font_fallbacks);
	gui->xft_small_font = xgui_load_resource_xft_font(gui->display, gui->screen,
	                                                 "rdesktop.launcher.smallFont",
	                                                 "Rdesktop.Launcher.SmallFont",
	                                                 g_xgui_xft_small_font_fallbacks);
#endif
	if (gui->font != NULL)
		XSetFont(gui->display, gui->gc, gui->font->fid);

	XMapWindow(gui->display, gui->window);
	return True;
}

static void
xgui_close(xgui_state_t *gui)
{
	if (gui->display == NULL)
		return;
#ifdef HAVE_XFT
	if (gui->xft_font != NULL)
		XftFontClose(gui->display, gui->xft_font);
	if (gui->xft_title_font != NULL)
		XftFontClose(gui->display, gui->xft_title_font);
	if (gui->xft_small_font != NULL)
		XftFontClose(gui->display, gui->xft_small_font);
#endif
	if (gui->font != NULL)
		XFreeFont(gui->display, gui->font);
	if (gui->title_font != NULL)
		XFreeFont(gui->display, gui->title_font);
	if (gui->small_font != NULL)
		XFreeFont(gui->display, gui->small_font);
	if (gui->banner_pixmap != 0)
		XFreePixmap(gui->display, gui->banner_pixmap);
	if (gui->backstore_pixmap != 0)
		XFreePixmap(gui->display, gui->backstore_pixmap);
	if (gui->gc != 0)
		XFreeGC(gui->display, gui->gc);
	if (gui->window != 0)
		XDestroyWindow(gui->display, gui->window);
	XCloseDisplay(gui->display);
}

int
xgui_startup(int *argc, char ***argv)
{
	xgui_state_t gui;
	RD_BOOL running = True;
	RD_BOOL dirty = True;
	int ret = -1;

	memset(&gui, 0, sizeof(gui));
	gui.active_field = 0;
	gui.active_check = -1;
	gui.active_button = -1;
	gui.active_reveal_field = -1;
	gui.focus_type = XGUI_FOCUS_FIELD;
	gui.active_tab = XGUI_TAB_GENERAL;
	xgui_init_controls(&gui);

	if (!xgui_open(&gui))
	{
		xgui_close(&gui);
		return -1;
	}

	while (running)
	{
		XEvent event;

		if (dirty)
		{
			xgui_redraw(&gui);
			dirty = False;
		}

		XNextEvent(gui.display, &event);

		switch (event.type)
		{
			case Expose:
				if (event.xexpose.count == 0)
					dirty = True;
				break;
			case ButtonPress:
				dirty = True;
				{
					xgui_action_t action;
					action = xgui_handle_click(&gui, event.xbutton.x, event.xbutton.y);
					if (action == XGUI_ACTION_EXIT)
					{
						ret = 0;
						running = False;
					}
					else if (action == XGUI_ACTION_CONNECT && xgui_build_argv(&gui, argc, argv))
					{
						ret = 1;
						running = False;
					}
					break;
				}
			case ButtonRelease:
				xgui_clear_reveal(&gui);
				dirty = True;
				break;
			case KeyPress:
				dirty = True;
				{
					xgui_action_t action = xgui_handle_key(&gui, &event.xkey);
					if (action == XGUI_ACTION_EXIT)
					{
						ret = 0;
						running = False;
					}
					else if (action == XGUI_ACTION_CONNECT && xgui_build_argv(&gui, argc, argv))
					{
						ret = 1;
						running = False;
					}
					break;
				}
			case SelectionNotify:
				xgui_handle_selection_notify(&gui, &event.xselection);
				dirty = True;
				break;
			case SelectionRequest:
				xgui_handle_selection_request(&gui, &event.xselectionrequest);
				break;
			case ConfigureNotify:
				dirty = True;
				break;
			case ClientMessage:
				if ((Atom) event.xclient.data.l[0] == gui.wm_delete_window)
				{
					ret = 0;
					running = False;
				}
				break;
		}
	}

	xgui_close(&gui);
	return ret;
}

static int
xgui_dialog_wrap_line(char lines[XGUI_DIALOG_MAX_LINES][XGUI_DIALOG_LINE_CHARS + 1], int count,
                      const char *line)
{
	char chunk[XGUI_DIALOG_LINE_CHARS + 1];
	size_t len;
	size_t pos;
	size_t take;
	size_t cut;

	len = strlen(line);
	pos = 0;
	if (len == 0 && count < XGUI_DIALOG_MAX_LINES)
	{
		lines[count++][0] = '\0';
		return count;
	}

	while (pos < len && count < XGUI_DIALOG_MAX_LINES)
	{
		take = len - pos;
		if (take > XGUI_DIALOG_LINE_CHARS)
		{
			cut = XGUI_DIALOG_LINE_CHARS;
			while (cut > 40 && line[pos + cut] != ' ')
				cut--;
			if (cut <= 40)
				cut = XGUI_DIALOG_LINE_CHARS;
			take = cut;
		}
		memcpy(chunk, line + pos, take);
		chunk[take] = '\0';
		strcpy(lines[count++], chunk);
		pos += take;
		while (line[pos] == ' ')
			pos++;
	}
	return count;
}

static int
xgui_dialog_wrap_message(const char *message,
                         char lines[XGUI_DIALOG_MAX_LINES][XGUI_DIALOG_LINE_CHARS + 1])
{
	char line[1024];
	int count;
	size_t i;
	size_t out;

	count = 0;
	out = 0;
	for (i = 0; message[i] != '\0' && count < XGUI_DIALOG_MAX_LINES; i++)
	{
		if (message[i] == '\n')
		{
			line[out] = '\0';
			count = xgui_dialog_wrap_line(lines, count, line);
			out = 0;
			continue;
		}
		if (out + 1 < sizeof(line))
			line[out++] = message[i];
	}
	if (out > 0 && count < XGUI_DIALOG_MAX_LINES)
	{
		line[out] = '\0';
		count = xgui_dialog_wrap_line(lines, count, line);
	}
	return count;
}

static void
xgui_dialog_redraw(xgui_state_t *gui,
                   char lines[XGUI_DIALOG_MAX_LINES][XGUI_DIALOG_LINE_CHARS + 1],
                   int line_count, int scroll, int max_visible,
                   const char *dialog_title, const char *dialog_subtitle,
                   const char *accept_label, const char *reject_label,
                   int accept_x, int reject_x, int button_y)
{
	int i;
	int y;

	XSetForeground(gui->display, gui->gc, gui->background);
	XFillRectangle(gui->display, gui->drawable, gui->gc, 0, 0,
	               XGUI_DIALOG_WIDTH, XGUI_DIALOG_HEIGHT);

	XSetForeground(gui->display, gui->gc, gui->blue);
	XFillRectangle(gui->display, gui->drawable, gui->gc, 0, 0,
	               XGUI_DIALOG_WIDTH, 70);
	XSetForeground(gui->display, gui->gc, gui->white);
	xgui_draw_string_font(gui, gui->title_font, 24, 42, dialog_title);
	xgui_draw_string_font(gui, gui->small_font, 24, 62, dialog_subtitle);

	XSetForeground(gui->display, gui->gc, gui->panel);
	XFillRectangle(gui->display, gui->drawable, gui->gc, 24, 88,
	               XGUI_DIALOG_WIDTH - 48, XGUI_DIALOG_HEIGHT - 166);
	XSetForeground(gui->display, gui->gc, gui->panel_border);
	XDrawRectangle(gui->display, gui->drawable, gui->gc, 24, 88,
	               XGUI_DIALOG_WIDTH - 48, XGUI_DIALOG_HEIGHT - 166);

	XSetForeground(gui->display, gui->gc, gui->black);
	y = 112;
	for (i = scroll; i < line_count && i < scroll + max_visible; i++)
	{
		xgui_draw_string_font(gui, gui->font, 40, y, lines[i]);
		y += 19;
	}

	if (line_count > max_visible)
	{
		char hint[96];
		snprintf(hint, sizeof(hint), "Use Up/Down to scroll (%d/%d)",
		         scroll + 1, line_count);
		XSetForeground(gui->display, gui->gc, gui->blue);
		xgui_draw_string_font(gui, gui->small_font, 40, XGUI_DIALOG_HEIGHT - 78, hint);
	}

	xgui_draw_button(gui, accept_x, button_y, 120, XGUI_BUTTON_HEIGHT,
	                 accept_label, XGUI_BUTTON_CONNECT, False);
	if (reject_label != NULL)
	{
		xgui_draw_button(gui, reject_x, button_y, 120, XGUI_BUTTON_HEIGHT,
		                 reject_label, XGUI_BUTTON_EXIT, False);
	}
}

RD_BOOL
xgui_choice_dialog(const char *title, const char *message, const char *accept_label,
                   const char *reject_label)
{
	xgui_state_t gui;
	XSizeHints hints;
	XClassHint class_hint;
	XEvent event;
	RD_BOOL running;
	RD_BOOL accepted;
	RD_BOOL dirty;
	char lines[XGUI_DIALOG_MAX_LINES][XGUI_DIALOG_LINE_CHARS + 1];
	const char *dialog_title;
	const char *dialog_subtitle;
	int line_count;
	int scroll;
	int max_visible;
	int accept_x, reject_x;
	int button_y;
	int window_x;
	int window_y;

	memset(&gui, 0, sizeof(gui));
	gui.display = XOpenDisplay(NULL);
	if (gui.display == NULL)
		return False;

	gui.screen = DefaultScreen(gui.display);
	gui.black = BlackPixel(gui.display, gui.screen);
	gui.white = WhitePixel(gui.display, gui.screen);
	gui.background = xgui_alloc_color(&gui, "#f4f6fb", gui.white);
	gui.panel = xgui_alloc_color(&gui, "#ffffff", gui.white);
	gui.panel_border = xgui_alloc_color(&gui, "#9aa7b8", gui.black);
	gui.field_border = xgui_alloc_color(&gui, "#7c8798", gui.black);
	gui.button_fill = xgui_alloc_color(&gui, "#eef3fb", gui.white);
	gui.button_active = xgui_alloc_color(&gui, "#d7e6fb", gui.white);
	gui.dark_gray = xgui_alloc_color(&gui, "#344054", gui.black);
	gui.mid_gray = xgui_alloc_color(&gui, "#c0c8d2", gui.black);
	gui.light_blue = xgui_alloc_color(&gui, "#e8f1ff", gui.white);
	gui.blue = xgui_alloc_color(&gui, "#204f9f", gui.black);
	gui.focus = xgui_alloc_color(&gui, "#0b65d8", gui.black);
	gui.width = XGUI_DIALOG_WIDTH;
	gui.height = XGUI_DIALOG_HEIGHT;
	gui.focus_type = XGUI_FOCUS_BUTTON;
	gui.active_button = reject_label != NULL ? XGUI_BUTTON_EXIT : XGUI_BUTTON_CONNECT;
	gui.active_field = -1;
	gui.active_check = -1;
	gui.active_reveal_field = -1;

	xgui_get_center_position(gui.display, gui.screen, XGUI_DIALOG_WIDTH, XGUI_DIALOG_HEIGHT,
	                         &window_x, &window_y);
	gui.window = XCreateSimpleWindow(gui.display, RootWindow(gui.display, gui.screen),
	                                 window_x, window_y, XGUI_DIALOG_WIDTH,
	                                 XGUI_DIALOG_HEIGHT, 1, gui.panel_border,
	                                 gui.background);
	if (gui.window == 0)
	{
		XCloseDisplay(gui.display);
		return False;
	}

	gui.gc = XCreateGC(gui.display, gui.window, 0, NULL);
	gui.drawable = gui.window;
	XrmInitialize();
	gui.font = xgui_load_resource_font(gui.display, "rdesktop.launcher.font",
	                                  "Rdesktop.Launcher.Font",
	                                  g_xgui_font_fallbacks);
	gui.title_font = xgui_load_resource_font(gui.display, "rdesktop.launcher.titleFont",
	                                        "Rdesktop.Launcher.TitleFont",
	                                        g_xgui_title_font_fallbacks);
	gui.small_font = xgui_load_resource_font(gui.display, "rdesktop.launcher.smallFont",
	                                        "Rdesktop.Launcher.SmallFont",
	                                        g_xgui_small_font_fallbacks);
#ifdef HAVE_XFT
	gui.xft_font = xgui_load_resource_xft_font(gui.display, gui.screen,
	                                         "rdesktop.launcher.font",
	                                         "Rdesktop.Launcher.Font",
	                                         g_xgui_xft_font_fallbacks);
	gui.xft_title_font = xgui_load_resource_xft_font(gui.display, gui.screen,
	                                               "rdesktop.launcher.titleFont",
	                                               "Rdesktop.Launcher.TitleFont",
	                                               g_xgui_xft_title_font_fallbacks);
	gui.xft_small_font = xgui_load_resource_xft_font(gui.display, gui.screen,
	                                               "rdesktop.launcher.smallFont",
	                                               "Rdesktop.Launcher.SmallFont",
	                                               g_xgui_xft_small_font_fallbacks);
#endif
	if (gui.font != NULL)
		XSetFont(gui.display, gui.gc, gui.font->fid);

	XStoreName(gui.display, gui.window, title != NULL ? title : "rdesktop certificate warning");
	class_hint.res_name = "rdesktop";
	class_hint.res_class = "Rdesktop";
	XSetClassHint(gui.display, gui.window, &class_hint);
	gui.wm_delete_window = XInternAtom(gui.display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(gui.display, gui.window, &gui.wm_delete_window, 1);
	XSelectInput(gui.display, gui.window,
	             ExposureMask | ButtonPressMask | KeyPressMask | StructureNotifyMask);

	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize | PMaxSize | PPosition;
	hints.x = window_x;
	hints.y = window_y;
	hints.min_width = XGUI_DIALOG_WIDTH;
	hints.max_width = XGUI_DIALOG_WIDTH;
	hints.min_height = XGUI_DIALOG_HEIGHT;
	hints.max_height = XGUI_DIALOG_HEIGHT;
	XSetWMNormalHints(gui.display, gui.window, &hints);

	line_count = xgui_dialog_wrap_message(message, lines);
	dialog_title = title != NULL ? title : "rdesktop message";
	dialog_subtitle = reject_label != NULL ?
	                  "Review the certificate before adding a local trust exception." :
	                  "Review the connection message before returning to the launcher.";
	scroll = 0;
	max_visible = 23;
	if (reject_label != NULL)
	{
		accept_x = XGUI_DIALOG_WIDTH - 300;
		reject_x = XGUI_DIALOG_WIDTH - 160;
	}
	else
	{
		accept_x = XGUI_DIALOG_WIDTH - 160;
		reject_x = 0;
	}
	button_y = XGUI_DIALOG_HEIGHT - 58;

	XMapWindow(gui.display, gui.window);
	running = True;
	accepted = False;
	dirty = True;
	while (running)
	{
		if (dirty)
		{
			xgui_dialog_redraw(&gui, lines, line_count, scroll, max_visible,
			                   dialog_title, dialog_subtitle,
			                   accept_label, reject_label, accept_x, reject_x, button_y);
			dirty = False;
		}

		XNextEvent(gui.display, &event);
		switch (event.type)
		{
			case Expose:
				dirty = True;
				break;
			case ButtonPress:
				if (event.xbutton.y >= button_y && event.xbutton.y <= button_y + XGUI_BUTTON_HEIGHT &&
				    event.xbutton.x >= accept_x && event.xbutton.x <= accept_x + 120)
				{
					gui.active_button = XGUI_BUTTON_CONNECT;
					accepted = True;
					running = False;
				}
				else if (reject_label != NULL && event.xbutton.y >= button_y &&
				         event.xbutton.y <= button_y + XGUI_BUTTON_HEIGHT &&
				         event.xbutton.x >= reject_x && event.xbutton.x <= reject_x + 120)
				{
					gui.active_button = XGUI_BUTTON_EXIT;
					accepted = False;
					running = False;
				}
				break;
			case KeyPress:
				{
					KeySym keysym;
					XLookupString(&event.xkey, NULL, 0, &keysym, NULL);
					if (reject_label != NULL &&
					    (keysym == XK_Tab || keysym == XK_Left || keysym == XK_Right))
					{
						gui.active_button = gui.active_button == XGUI_BUTTON_CONNECT ?
						                    XGUI_BUTTON_EXIT : XGUI_BUTTON_CONNECT;
						dirty = True;
					}
					else if (keysym == XK_Return || keysym == XK_KP_Enter || keysym == XK_space)
					{
						accepted = gui.active_button == XGUI_BUTTON_CONNECT;
						running = False;
					}
					else if (keysym == XK_y || keysym == XK_Y)
					{
						accepted = True;
						running = False;
					}
					else if (keysym == XK_Escape || keysym == XK_n || keysym == XK_N)
					{
						accepted = False;
						running = False;
					}
					else if (keysym == XK_Down && scroll + max_visible < line_count)
					{
						scroll++;
						dirty = True;
					}
					else if (keysym == XK_Up && scroll > 0)
					{
						scroll--;
						dirty = True;
					}
					break;
				}
			case ClientMessage:
				if ((Atom) event.xclient.data.l[0] == gui.wm_delete_window)
				{
					accepted = False;
					running = False;
				}
				break;
		}
	}

	xgui_close(&gui);
	return accepted;
}

void
xgui_message_dialog(const char *title, const char *message, const char *button_label)
{
	(void) xgui_choice_dialog(title, message, button_label != NULL ? button_label : "OK", NULL);
}
