/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - Clipboard functions
   Copyright (C) Erik Forsberg <forsberg@cendio.se> 2003
   Copyright (C) Matthew Chapman 2003

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
#include <X11/Xatom.h>
#include "rdesktop.h"

#define NUM_TARGETS 6

extern Display *display;
extern Window wnd;
extern Time last_gesturetime;

static Atom clipboard_atom, primary_atom, targets_atom, timestamp_atom;
static Atom rdesktop_clipboard_target_atom, rdesktop_clipboard_formats_atom, incr_atom;
static XSelectionRequestEvent selection_request;
static Atom targets[NUM_TARGETS];
static int have_primary = 0;
static int rdesktop_is_selection_owner = 0;

static void
xclip_provide_selection(XSelectionRequestEvent *req, Atom type, unsigned int format, uint8 *data, uint32 length)
{
	XEvent xev;

	XChangeProperty(display, req->requestor, req->property,
			type, format, PropModeReplace, data, length);

	xev.xselection.type = SelectionNotify;
	xev.xselection.serial = 0;
	xev.xselection.send_event = True;
	xev.xselection.requestor = req->requestor;
	xev.xselection.selection = req->selection;
	xev.xselection.target = req->target;
	xev.xselection.property = req->property;
	xev.xselection.time = req->time;
	XSendEvent(display, req->requestor, False, NoEventMask, &xev);
}

void
xclip_handle_SelectionNotify(XSelectionEvent * event)
{
	unsigned long nitems, bytes_left;
	Atom type, best_target, text_target;
	Atom *supported_targets;
	int res, i, format;
	uint8 *data;

	if (event->property == None)
		goto fail;

	DEBUG_CLIPBOARD(("xclip_handle_SelectionNotify: selection=%s, target=%s, property=%s\n",
			 XGetAtomName(display, event->selection),
			 XGetAtomName(display, event->target),
			 XGetAtomName(display, event->property)));

	if (event->property == None)
		goto fail;

	res = XGetWindowProperty(display, wnd, rdesktop_clipboard_target_atom,
				 0, XMaxRequestSize(display), True, AnyPropertyType,
				 &type, &format, &nitems, &bytes_left, &data);

	if (res != Success)
	{
		DEBUG_CLIPBOARD(("XGetWindowProperty failed!\n"));
		goto fail;
	}

	if (event->target == targets_atom)
	{
		/* FIXME: We should choose format here based on what the server wanted */
		best_target = XA_STRING;
		if (type != None)
		{
			supported_targets = (Atom *) data;
			text_target = XInternAtom(display, "TEXT", False);
			for (i = 0; i < nitems; i++)
			{
				DEBUG_CLIPBOARD(("Target %d: %s\n", i, XGetAtomName(display, supported_targets[i])));
				if (supported_targets[i] == text_target)
				{
					DEBUG_CLIPBOARD(("Other party supports TEXT, choosing that as best_target\n"));
					best_target = text_target;
				}
			}
			XFree(data);
		}

		XConvertSelection(display, primary_atom, best_target, rdesktop_clipboard_target_atom, wnd, event->time);
		return;
	}

	if (type == incr_atom)
	{
		warning("We don't support INCR transfers at this time. Try cutting less data.\n");
		goto fail;
	}

	cliprdr_send_data(data, nitems+1);
	XFree(data);

	if (!rdesktop_is_selection_owner)
		cliprdr_send_text_format_announce();
	return;

fail:
	cliprdr_send_data(NULL, 0);
}

void
xclip_handle_SelectionRequest(XSelectionRequestEvent *event)
{
	unsigned long nitems, bytes_left;
	uint32 *wanted_format;
	int format, res;
	Atom type;

	DEBUG_CLIPBOARD(("xclip_handle_SelectionRequest: selection=%s, target=%s, property=%s\n",
			 XGetAtomName(display, event->selection),
			 XGetAtomName(display, event->target),
			 XGetAtomName(display, event->property)));

	if (event->target == targets_atom)
	{
		xclip_provide_selection(event, XA_ATOM, 32, (uint8 *)&targets, NUM_TARGETS);
		return;
	}
	else if (event->target == timestamp_atom)
	{
		xclip_provide_selection(event, XA_INTEGER, 32, (uint8 *)&last_gesturetime, 1);
		return;
	}
	else if (event->target == rdesktop_clipboard_formats_atom)
	{
		res = XGetWindowProperty(display, event->requestor,
				rdesktop_clipboard_target_atom, 0, 1, True, XA_INTEGER,
				&type, &format, &nitems, &bytes_left, (unsigned char **) &wanted_format);
		format = (res == Success) ? *wanted_format : CF_TEXT;
	}
	else
	{
		format = CF_TEXT;
	}

	cliprdr_send_data_request(format);
	selection_request = *event;
	/* wait for data */
}

void
xclip_handle_SelectionClear(void)
{
	DEBUG_CLIPBOARD(("xclip_handle_SelectionClear\n"));
	have_primary = 0;
	XDeleteProperty(display, DefaultRootWindow(display), rdesktop_clipboard_formats_atom);
	cliprdr_send_text_format_announce();
}

void
xclip_handle_PropertyNotify(XPropertyEvent *event)
{
	unsigned long nitems, bytes_left;
	int format, res;
	uint8 *data;
	Atom type;

	if (event->atom != rdesktop_clipboard_formats_atom)
		return;

	if (have_primary) /* from us */
		return;

	if (event->state == PropertyNewValue)
	{
		res = XGetWindowProperty(display, DefaultRootWindow(display),
			rdesktop_clipboard_formats_atom, 0, XMaxRequestSize(display), False, XA_STRING,
			&type, &format, &nitems, &bytes_left, &data);

		if ((res == Success) && (nitems > 0))
		{
			cliprdr_send_native_format_announce(data, nitems);
			rdesktop_is_selection_owner = 1;
			return;
		}
	}

	/* PropertyDelete, or XGetWindowProperty failed */
	cliprdr_send_text_format_announce();
	rdesktop_is_selection_owner = 0;
}


void
ui_clip_format_announce(char *data, uint32 length)
{
	XSetSelectionOwner(display, primary_atom, wnd, last_gesturetime);
	if (XGetSelectionOwner(display, primary_atom) != wnd)
	{
		warning("Failed to aquire ownership of PRIMARY clipboard\n");
		return;
	}

	have_primary = 1;
	XChangeProperty(display, DefaultRootWindow(display),
			rdesktop_clipboard_formats_atom, XA_STRING, 8, PropModeReplace, data, length);

	XSetSelectionOwner(display, clipboard_atom, wnd, last_gesturetime);
	if (XGetSelectionOwner(display, clipboard_atom) != wnd)
		warning("Failed to aquire ownership of CLIPBOARD clipboard\n");
}


void
ui_clip_handle_data(char *data, uint32 length)
{
	xclip_provide_selection(&selection_request, XA_STRING, 8, data, length-1);
}

void
ui_clip_request_data(uint32 format)
{
	Window selectionowner;

	DEBUG_CLIPBOARD(("Request from server for format %d\n", format));

	if (rdesktop_is_selection_owner)
	{
		XChangeProperty(display, wnd, rdesktop_clipboard_target_atom,
				XA_INTEGER, 32, PropModeReplace, (unsigned char *) &format, 1);

		XConvertSelection(display, primary_atom, rdesktop_clipboard_formats_atom,
					rdesktop_clipboard_target_atom, wnd, CurrentTime);
		return;
	}

	selectionowner = XGetSelectionOwner(display, primary_atom);
	if (selectionowner != None)
	{
		XConvertSelection(display, primary_atom, targets_atom,
					rdesktop_clipboard_target_atom, wnd, CurrentTime);
		return;
	}

	/* No PRIMARY, try CLIPBOARD */
	selectionowner = XGetSelectionOwner(display, clipboard_atom);
	if (selectionowner != None)
	{
		XConvertSelection(display, clipboard_atom, targets_atom,
					rdesktop_clipboard_target_atom, wnd, CurrentTime);
		return;
	}

	/* No data available */
	cliprdr_send_data(NULL, 0);
}

void
ui_clip_sync(void)
{
	cliprdr_send_text_format_announce();
}


void
xclip_init(void)
{
	if (!cliprdr_init())
		return;

	primary_atom = XInternAtom(display, "PRIMARY", False);
	clipboard_atom = XInternAtom(display, "CLIPBOARD", False);
	targets_atom = XInternAtom(display, "TARGETS", False);
	timestamp_atom = XInternAtom(display, "TIMESTAMP", False);
	rdesktop_clipboard_target_atom = XInternAtom(display, "_RDESKTOP_CLIPBOARD_TARGET", False);
	incr_atom = XInternAtom(display, "INCR", False);
	targets[0] = targets_atom;
	targets[1] = XInternAtom(display, "TEXT", False);
	targets[2] = XInternAtom(display, "UTF8_STRING", False);
	targets[3] = XInternAtom(display, "text/unicode", False);
	targets[4] = XInternAtom(display, "TIMESTAMP", False);
	targets[5] = XA_STRING;

	/* rdesktop sets _RDESKTOP_CLIPBOARD_FORMATS on the root window when acquiring the clipboard.
	   Other interested rdesktops can use this to notify their server of the available formats. */
	rdesktop_clipboard_formats_atom = XInternAtom(display, "_RDESKTOP_CLIPBOARD_FORMATS", False);
	XSelectInput(display, DefaultRootWindow(display), PropertyChangeMask);
}
