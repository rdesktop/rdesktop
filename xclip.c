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

/*
  To gain better understanding of this code, one could be assisted by the following documents:
  - Inter-Client Communication Conventions Manual (ICCCM)
    HTML: http://tronche.com/gui/x/icccm/
    PDF:  http://ftp.xfree86.org/pub/XFree86/4.5.0/doc/PDF/icccm.pdf
  - MSDN: Clipboard Formats
    http://msdn.microsoft.com/library/en-us/winui/winui/windowsuserinterface/dataexchange/clipboard/clipboardformats.asp
*/

#ifdef HAVE_ICONV
#ifdef HAVE_LANGINFO_H
#ifdef HAVE_ICONV_H
#include <langinfo.h>
#include <iconv.h>
#define USE_UNICODE_CLIPBOARD
#endif
#endif
#endif

#ifdef USE_UNICODE_CLIPBOARD
#define RDP_CF_TEXT CF_UNICODETEXT
#else
#define RDP_CF_TEXT CF_TEXT
#endif

#define MAX_TARGETS 7

extern Display *g_display;
extern Window g_wnd;
extern Time g_last_gesturetime;

/* Atoms of the two X selections we're dealing with: CLIPBOARD (explicit-copy) and PRIMARY (selection-copy) */
static Atom clipboard_atom, primary_atom;
/* Atom of the TARGETS clipboard target */
static Atom targets_atom;
/* Atom of the TIMESTAMP clipboard target */
static Atom timestamp_atom;
/* Atom _RDESKTOP_CLIPBOARD_TARGET which has multiple uses:
   - The 'property' argument in XConvertSelection calls: This is the property of our
     window into which XConvertSelection will store the received clipboard data.
   - In a clipboard request of target _RDESKTOP_CLIPBOARD_FORMATS, an XA_INTEGER-typed
     property carrying the Windows native (CF_...) format desired by the requestor.
     Requestor set this property (e.g. requestor_wnd[_RDESKTOP_CLIPBOARD_TARGET] = CF_TEXT)
     before requesting clipboard data from a fellow rdesktop using
     the _RDESKTOP_CLIPBOARD_FORMATS target. */
static Atom rdesktop_clipboard_target_atom;
/* Atom _RDESKTOP_CLIPBOARD_FORMATS which has multiple uses:
   - The clipboard target (X jargon for "clipboard format") for rdesktop-to-rdesktop interchange
     of Windows native clipboard data.
     This target cannot be used standalone; the requestor must keep the
     _RDESKTOP_CLIPBOARD_TARGET property on his window denoting
     the Windows native clipboard format being requested.
   - The root window property set by rdesktop when it owns the clipboard,
     denoting all Windows native clipboard formats it offers via
     requests of the _RDESKTOP_CLIPBOARD_FORMATS target. */
static Atom rdesktop_clipboard_formats_atom;
static Atom format_string_atom, format_utf8_string_atom, format_unicode_atom;
/* Atom of the INCR clipboard type (see ICCCM on "INCR Properties") */
static Atom incr_atom;
/* Stores the last "selection request" (= another X client requesting clipboard data from us).
   To satisfy such a request, we request the clipboard data from the RDP server.
   When we receive the response from the RDP server (asynchronously), this variable gives us
   the context to proceed. */
static XSelectionRequestEvent selection_request;
/* Denotes we have a pending selection request. */
static Bool has_selection_request;
/* Stores the clipboard format (CF_TEXT, CF_UNICODETEXT etc.) requested in the last
   CLIPDR_DATA_REQUEST (= the RDP server requesting clipboard data from us).
   When we receive this data from whatever X client offering it, this variable gives us
   the context to proceed.
 */
static int rdp_clipboard_request_format;
/* Array of offered clipboard targets that will be sent to fellow X clients upon a TARGETS request. */
static Atom targets[MAX_TARGETS];
static int num_targets;
/* Denotes that this client currently holds the PRIMARY selection. */
static int have_primary = 0;
/* Denotes that an rdesktop (not this rdesktop) is owning the selection,
   allowing us to interchange Windows native clipboard data directly. */
static int rdesktop_is_selection_owner = 0;

/* Denotes that an INCR ("chunked") transfer is in progress. */
static int g_waiting_for_INCR = 0;
/* Denotes the target format of the ongoing INCR ("chunked") transfer. */
static Atom g_incr_target = 0;
/* Buffers an INCR transfer. */
static uint8 *g_clip_buffer = 0;
/* Denotes the size of g_clip_buffer. */
static uint32 g_clip_buflen = 0;

/* Translate LF to CR-LF. To do this, we must allocate more memory.
   The returned string is null-terminated, as required by CF_TEXT.
   Does not stop on embedded nulls.
   The length is updated. */
static void
crlf2lf(uint8 * data, uint32 * length)
{
	uint8 *dst, *src;
	src = dst = data;
	while (src < data + *length)
	{
		if (*src != '\x0d')
			*dst++ = *src;
		src++;
	}
	*length = dst - data;
}

#ifdef USE_UNICODE_CLIPBOARD
/* Translate LF to CR-LF. To do this, we must allocate more memory.
   The returned string is null-terminated, as required by CF_UNICODETEXT.
   The size is updated. */
static uint8 *
utf16_lf2crlf(uint8 * data, uint32 * size)
{
	uint8 *result;
	uint16 *inptr, *outptr;

	/* Worst case: Every char is LF */
	result = xmalloc((*size * 2) + 2);
	if (result == NULL)
		return NULL;

	inptr = (uint16 *) data;
	outptr = (uint16 *) result;

	/* Check for a reversed BOM */
	Bool swap_endianess = (*inptr == 0xfffe);

	while ((uint8 *) inptr < data + *size)
	{
		uint16 uvalue = *inptr;
		if (swap_endianess)
			uvalue = ((uvalue << 8) & 0xff00) + (uvalue >> 8);
		if (uvalue == 0x0a)
			*outptr++ = swap_endianess ? 0x0d00 : 0x0d;
		*outptr++ = *inptr++;
	}
	*outptr++ = 0;		/* null termination */
	*size = (uint8 *) outptr - result;

	return result;
}
#else
/* Translate LF to CR-LF. To do this, we must allocate more memory.
   The length is updated. */
static uint8 *
lf2crlf(uint8 * data, uint32 * length)
{
	uint8 *result, *p, *o;

	/* Worst case: Every char is LF */
	result = xmalloc(*length * 2);

	p = data;
	o = result;

	while (p < data + *length)
	{
		if (*p == '\x0a')
			*o++ = '\x0d';
		*o++ = *p++;
	}
	*length = o - result;

	/* Convenience */
	*o++ = '\0';

	return result;
}
#endif

static void
xclip_provide_selection(XSelectionRequestEvent * req, Atom type, unsigned int format, uint8 * data,
			uint32 length)
{
	XEvent xev;

	XChangeProperty(g_display, req->requestor, req->property,
			type, format, PropModeReplace, data, length);

	xev.xselection.type = SelectionNotify;
	xev.xselection.serial = 0;
	xev.xselection.send_event = True;
	xev.xselection.requestor = req->requestor;
	xev.xselection.selection = req->selection;
	xev.xselection.target = req->target;
	xev.xselection.property = req->property;
	xev.xselection.time = req->time;
	XSendEvent(g_display, req->requestor, False, NoEventMask, &xev);
}

/* Replies a clipboard requestor, telling that we're unable to satisfy his request for whatever reason.
   This has the benefit of finalizing the clipboard negotiation and thus not leaving our requestor
   lingering (and, potentially, stuck). */
static void
xclip_refuse_selection(XSelectionRequestEvent * req)
{
	XEvent xev;

	xev.xselection.type = SelectionNotify;
	xev.xselection.serial = 0;
	xev.xselection.send_event = True;
	xev.xselection.requestor = req->requestor;
	xev.xselection.selection = req->selection;
	xev.xselection.target = req->target;
	xev.xselection.property = None;
	xev.xselection.time = req->time;
	XSendEvent(g_display, req->requestor, False, NoEventMask, &xev);
}

/* Wrapper for cliprdr_send_data which also cleans the request state. */
static void
helper_cliprdr_send_response(uint8 * data, uint32 length)
{
	if (rdp_clipboard_request_format != 0)
	{
		cliprdr_send_data(data, length);
		rdp_clipboard_request_format = 0;
		if (!rdesktop_is_selection_owner)
			cliprdr_send_simple_native_format_announce(RDP_CF_TEXT);
	}
}

/* Last resort, when we have to provide clipboard data but for whatever
   reason couldn't get any.
 */
static void
helper_cliprdr_send_empty_response()
{
	helper_cliprdr_send_response(NULL, 0);
}

/* Replies with clipboard data to RDP, converting it from the target format
   to the expected RDP format as necessary. Returns true if data was sent.
 */
static Bool
xclip_send_data_with_convert(uint8 * source, size_t source_size, Atom target)
{
#ifdef USE_UNICODE_CLIPBOARD
	if (target == format_string_atom ||
	    target == format_unicode_atom || target == format_utf8_string_atom)
	{
		if (rdp_clipboard_request_format != RDP_CF_TEXT)
			return False;

		/* Make an attempt to convert any string we send to Unicode.
		   We don't know what the RDP server's ANSI Codepage is, or how to convert
		   to it, so using CF_TEXT is not safe (and is unnecessary, since all
		   WinNT versions are Unicode-minded).
		 */
		size_t unicode_buffer_size;
		char *unicode_buffer;
		iconv_t cd;

		if (target == format_string_atom)
		{
			char *locale_charset = nl_langinfo(CODESET);
			cd = iconv_open(WINDOWS_CODEPAGE, locale_charset);
			if (cd == (iconv_t) - 1)
			{
				DEBUG_CLIPBOARD(("Locale charset %s not found in iconv. Unable to convert clipboard text.\n", locale_charset));
				return False;
			}
			unicode_buffer_size = source_size * 4;
		}
		else if (target == format_unicode_atom)
		{
			cd = iconv_open(WINDOWS_CODEPAGE, "UCS-2");
			if (cd == (iconv_t) - 1)
			{
				return False;
			}
			unicode_buffer_size = source_size;
		}
		else if (target == format_utf8_string_atom)
		{
			cd = iconv_open(WINDOWS_CODEPAGE, "UTF-8");
			if (cd == (iconv_t) - 1)
			{
				return False;
			}
			/* UTF-8 is guaranteed to be less or equally compact
			   as UTF-16 for all Unicode chars >=2 bytes.
			 */
			unicode_buffer_size = source_size * 2;
		}
		else
		{
			return False;
		}

		unicode_buffer = xmalloc(unicode_buffer_size);
		size_t unicode_buffer_size_remaining = unicode_buffer_size;
		char *unicode_buffer_remaining = unicode_buffer;
		char *data_remaining = (char *) source;
		size_t data_size_remaining = source_size;
		iconv(cd, &data_remaining, &data_size_remaining, &unicode_buffer_remaining,
		      &unicode_buffer_size_remaining);
		iconv_close(cd);

		/* translate linebreaks */
		uint32 translated_data_size = unicode_buffer_size - unicode_buffer_size_remaining;
		uint8 *translated_data =
			utf16_lf2crlf((uint8 *) unicode_buffer, &translated_data_size);
		if (translated_data != NULL)
		{
			DEBUG_CLIPBOARD(("Sending Unicode string of %d bytes\n",
					 translated_data_size));
			cliprdr_send_data(translated_data, translated_data_size);
			xfree(translated_data);	/* Not the same thing as XFree! */
		}

		xfree(unicode_buffer);

		return True;
	}
#else
	if (target == format_string_atom)
	{
		uint8 *translated_data;
		uint32 length = source_size;

		if (rdp_clipboard_request_format != RDP_CF_TEXT)
			return False;

		DEBUG_CLIPBOARD(("Translating linebreaks before sending data\n"));
		translated_data = lf2crlf(source, &length);
		if (translated_data != NULL)
		{
			cliprdr_send_data(translated_data, length);
			xfree(translated_data);	/* Not the same thing as XFree! */
		}

		return True;
	}
#endif
	else if (target == rdesktop_clipboard_formats_atom)
	{
		helper_cliprdr_send_response(source, source_size + 1);

		return True;
	}
	else
	{
		return False;
	}
}

/* This function is called for SelectionNotify events.
   The SelectionNotify event is sent from the clipboard owner to the requestor
   after his request was satisfied.
   If this function is called, we're the requestor side. */
#ifndef MAKE_PROTO
void
xclip_handle_SelectionNotify(XSelectionEvent * event)
{
	unsigned long nitems, bytes_left;
	XWindowAttributes wa;
	Atom type;
	Atom *supported_targets;
	int res, i, format;
	uint8 *data;

	if (event->property == None)
		goto fail;

	DEBUG_CLIPBOARD(("xclip_handle_SelectionNotify: selection=%s, target=%s, property=%s\n",
			 XGetAtomName(g_display, event->selection),
			 XGetAtomName(g_display, event->target),
			 XGetAtomName(g_display, event->property)));

	if (event->property == None)
		goto fail;

	res = XGetWindowProperty(g_display, g_wnd, rdesktop_clipboard_target_atom,
				 0, XMaxRequestSize(g_display), False, AnyPropertyType,
				 &type, &format, &nitems, &bytes_left, &data);

	if (res != Success)
	{
		DEBUG_CLIPBOARD(("XGetWindowProperty failed!\n"));
		goto fail;
	}

	if (type == incr_atom)
	{
		DEBUG_CLIPBOARD(("Received INCR.\n"));

		XGetWindowAttributes(g_display, g_wnd, &wa);
		if ((wa.your_event_mask | PropertyChangeMask) != wa.your_event_mask)
		{
			XSelectInput(g_display, g_wnd, (wa.your_event_mask | PropertyChangeMask));
		}
		XDeleteProperty(g_display, g_wnd, rdesktop_clipboard_target_atom);
		XFree(data);
		g_incr_target = event->target;
		g_waiting_for_INCR = 1;
		return;
	}

	XDeleteProperty(g_display, g_wnd, rdesktop_clipboard_target_atom);

	/* Negotiate target format */
	if (event->target == targets_atom)
	{
		/* Determine the best of text targets that we have available:
		   Prefer UTF8_STRING > text/unicode (unspecified encoding) > STRING
		   (ignore TEXT and COMPOUND_TEXT because we don't have code to handle them)
		 */
		int text_target_satisfaction = 0;
		Atom best_text_target = 0;	/* measures how much we're satisfied with what we found */
		if (type != None)
		{
			supported_targets = (Atom *) data;
			for (i = 0; i < nitems; i++)
			{
				DEBUG_CLIPBOARD(("Target %d: %s\n", i,
						 XGetAtomName(g_display, supported_targets[i])));
				if (supported_targets[i] == format_string_atom)
				{
					if (text_target_satisfaction < 1)
					{
						DEBUG_CLIPBOARD(("Other party supports STRING, choosing that as best_target\n"));
						best_text_target = supported_targets[i];
						text_target_satisfaction = 1;
					}
				}
#ifdef USE_UNICODE_CLIPBOARD
				else if (supported_targets[i] == format_unicode_atom)
				{
					if (text_target_satisfaction < 2)
					{
						DEBUG_CLIPBOARD(("Other party supports text/unicode, choosing that as best_target\n"));
						best_text_target = supported_targets[i];
						text_target_satisfaction = 2;
					}
				}
				else if (supported_targets[i] == format_utf8_string_atom)
				{
					if (text_target_satisfaction < 3)
					{
						DEBUG_CLIPBOARD(("Other party supports UTF8_STRING, choosing that as best_target\n"));
						best_text_target = supported_targets[i];
						text_target_satisfaction = 3;
					}
				}
#endif
			}
		}

		/* Kickstarting the next step in the process of satisfying RDP's
		   clipboard request -- specifically, requesting the actual clipboard data.
		 */
		if (best_text_target != 0)
		{
			XConvertSelection(g_display, clipboard_atom, best_text_target,
					  rdesktop_clipboard_target_atom, g_wnd, event->time);
			return;
		}
		else
		{
			DEBUG_CLIPBOARD(("Unable to find a textual target to satisfy RDP clipboard text request\n"));
			goto fail;
		}
	}
	else
	{
		if (!xclip_send_data_with_convert(data, nitems, event->target))
		{
			goto fail;
		}
	}

	XFree(data);

	return;

      fail:
	XDeleteProperty(g_display, g_wnd, rdesktop_clipboard_target_atom);
	XFree(data);
	helper_cliprdr_send_empty_response();
}

/* This function is called for SelectionRequest events.
   The SelectionRequest event is sent from the requestor to the clipboard owner
   to request clipboard data.
 */
void
xclip_handle_SelectionRequest(XSelectionRequestEvent * event)
{
	unsigned long nitems, bytes_left;
	unsigned char *prop_return;
	uint32 *wanted_format;
	int format, res;
	Atom type;

	DEBUG_CLIPBOARD(("xclip_handle_SelectionRequest: selection=%s, target=%s, property=%s\n",
			 XGetAtomName(g_display, event->selection),
			 XGetAtomName(g_display, event->target),
			 XGetAtomName(g_display, event->property)));

	if (event->target == targets_atom)
	{
		xclip_provide_selection(event, XA_ATOM, 32, (uint8 *) & targets, num_targets);
		return;
	}
	else if (event->target == timestamp_atom)
	{
		xclip_provide_selection(event, XA_INTEGER, 32, (uint8 *) & g_last_gesturetime, 1);
		return;
	}
	else
	{
		/* All the following targets require an async operation with the RDP server
		   and currently we don't do X clipboard request queueing so we can only
		   handle one such request at a time. */
		if (has_selection_request)
		{
			DEBUG_CLIPBOARD(("Error: Another clipboard request was already sent to the RDP server and not yet responded. Refusing this request.\n"));
			xclip_refuse_selection(event);
			return;
		}
		if (event->target == rdesktop_clipboard_formats_atom)
		{
			/* Before the requestor makes a request for the _RDESKTOP_CLIPBOARD_FORMATS target,
			   he should declare requestor[_RDESKTOP_CLIPBOARD_TARGET] = CF_SOMETHING.
			   Otherwise, we default to RDP_CF_TEXT.
			 */
			res = XGetWindowProperty(g_display, event->requestor,
						 rdesktop_clipboard_target_atom, 0, 1, True,
						 XA_INTEGER, &type, &format, &nitems, &bytes_left,
						 &prop_return);
			wanted_format = (uint32 *) prop_return;
			format = (res == Success) ? *wanted_format : RDP_CF_TEXT;
			XFree(prop_return);
		}
		else if (event->target == format_string_atom || event->target == XA_STRING)
		{
			/* STRING and XA_STRING are defined to be ISO8859-1 */
			format = CF_TEXT;
		}
		else if (event->target == format_utf8_string_atom)
		{
#ifdef USE_UNICODE_CLIPBOARD
			format = CF_UNICODETEXT;
#else
			DEBUG_CLIPBOARD(("Requested target unavailable due to lack of Unicode support. (It was not in TARGETS, so why did you ask for it?!)\n"));
			xclip_refuse_selection(event);
			return;
#endif
		}
		else if (event->target == format_unicode_atom)
		{
			/* Assuming text/unicode to be UTF-16 */
			format = CF_UNICODETEXT;
		}
		else
		{
			DEBUG_CLIPBOARD(("Requested target unavailable. (It was not in TARGETS, so why did you ask for it?!)\n"));
			xclip_refuse_selection(event);
			return;
		}

		cliprdr_send_data_request(format);
		selection_request = *event;
		has_selection_request = True;
		return;		/* wait for data */
	}
}

/* While this rdesktop holds ownership over the clipboard, it means the clipboard data
   is offered by the RDP server (and when it is pasted inside RDP, there's no network
   roundtrip).

   This event (SelectionClear) symbolizes this rdesktop lost onwership of the clipboard
   to some other X client. We should find out what clipboard formats this other
   client offers and announce that to RDP. */
void
xclip_handle_SelectionClear(void)
{
	DEBUG_CLIPBOARD(("xclip_handle_SelectionClear\n"));
	have_primary = 0;
	XDeleteProperty(g_display, DefaultRootWindow(g_display), rdesktop_clipboard_formats_atom);
	/* FIXME:
	   Without XFIXES, we cannot reliably know the formats offered by the
	   new owner of the X11 clipboard, so we just lie about him
	   offering RDP_CF_TEXT. */
	cliprdr_send_simple_native_format_announce(RDP_CF_TEXT);
}

/* Called when any property changes in our window or the root window. */
void
xclip_handle_PropertyNotify(XPropertyEvent * event)
{
	unsigned long nitems;
	unsigned long offset = 0;
	unsigned long bytes_left = 1;
	int format, res;
	XWindowAttributes wa;
	uint8 *data;
	Atom type;

	if (event->state == PropertyNewValue && g_waiting_for_INCR)
	{
		DEBUG_CLIPBOARD(("x_clip_handle_PropertyNotify: g_waiting_for_INCR != 0\n"));

		while (bytes_left > 0)
		{
			/* Unlike the specification, we don't set the 'delete' arugment to True
			   since we slurp the INCR's chunks in even-smaller chunks of 4096 bytes. */
			if ((XGetWindowProperty
			     (g_display, g_wnd, rdesktop_clipboard_target_atom, offset, 4096L,
			      False, AnyPropertyType, &type, &format, &nitems, &bytes_left,
			      &data) != Success))
			{
				XFree(data);
				return;
			}

			if (nitems == 0)
			{
				/* INCR transfer finished */
				XGetWindowAttributes(g_display, g_wnd, &wa);
				XSelectInput(g_display, g_wnd,
					     (wa.your_event_mask ^ PropertyChangeMask));
				XFree(data);
				g_waiting_for_INCR = 0;

				if (g_clip_buflen > 0)
				{
					if (!xclip_send_data_with_convert
					    (g_clip_buffer, g_clip_buflen, g_incr_target))
					{
						helper_cliprdr_send_empty_response();
					}
					xfree(g_clip_buffer);
					g_clip_buffer = NULL;
					g_clip_buflen = 0;
				}
			}
			else
			{
				/* Another chunk in the INCR transfer */
				offset += (nitems / 4);	/* offset at which to begin the next slurp */
				g_clip_buffer = xrealloc(g_clip_buffer, g_clip_buflen + nitems);
				memcpy(g_clip_buffer + g_clip_buflen, data, nitems);
				g_clip_buflen += nitems;

				XFree(data);
			}
		}
		XDeleteProperty(g_display, g_wnd, rdesktop_clipboard_target_atom);
		return;
	}

	if ((event->atom == rdesktop_clipboard_formats_atom) &&
	    (event->window == DefaultRootWindow(g_display)) &&
	    !have_primary /* not interested in our own events */ )
	{
		if (event->state == PropertyNewValue)
		{
			DEBUG_CLIPBOARD(("xclip_handle_PropertyNotify: getting fellow rdesktop formats\n"));

			res = XGetWindowProperty(g_display, DefaultRootWindow(g_display),
						 rdesktop_clipboard_formats_atom, 0,
						 XMaxRequestSize(g_display), False, XA_STRING,
						 &type, &format, &nitems, &bytes_left, &data);

			if ((res == Success) && (nitems > 0))
			{
				cliprdr_send_native_format_announce(data, nitems);
				rdesktop_is_selection_owner = 1;
				return;
			}
		}

		/* For some reason, we couldn't announce the native formats */
		cliprdr_send_simple_native_format_announce(RDP_CF_TEXT);
		rdesktop_is_selection_owner = 0;
	}
}
#endif


/* Called when the RDP server announces new clipboard data formats.
   In response, we:
   - take ownership over the clipboard
   - declare those formats in their Windows native form
     to other rdesktop instances on this X server */
void
ui_clip_format_announce(uint8 * data, uint32 length)
{
	XSetSelectionOwner(g_display, primary_atom, g_wnd, g_last_gesturetime);
	if (XGetSelectionOwner(g_display, primary_atom) != g_wnd)
	{
		warning("Failed to aquire ownership of PRIMARY clipboard\n");
		return;
	}

	have_primary = 1;
	XChangeProperty(g_display, DefaultRootWindow(g_display),
			rdesktop_clipboard_formats_atom, XA_STRING, 8, PropModeReplace, data,
			length);

	XSetSelectionOwner(g_display, clipboard_atom, g_wnd, g_last_gesturetime);
	if (XGetSelectionOwner(g_display, clipboard_atom) != g_wnd)
		warning("Failed to aquire ownership of CLIPBOARD clipboard\n");
}

/* Called when the RDP server responds with clipboard data (after we've requested it). */
void
ui_clip_handle_data(uint8 * data, uint32 length)
{
	BOOL free_data = False;

	if (selection_request.target == format_string_atom || selection_request.target == XA_STRING)
	{
		/* We're expecting a CF_TEXT response */
		uint8 *firstnull;

		/* translate linebreaks */
		crlf2lf(data, &length);

		/* Only send data up to null byte, if any */
		firstnull = (uint8 *) strchr((char *) data, '\0');
		if (firstnull)
		{
			length = firstnull - data + 1;
		}
	}
#ifdef USE_UNICODE_CLIPBOARD
	else if (selection_request.target == format_utf8_string_atom)
	{
		/* We're expecting a CF_UNICODETEXT response */
		iconv_t cd = iconv_open("UTF-8", WINDOWS_CODEPAGE);
		if (cd != (iconv_t) - 1)
		{
			size_t utf8_length = length * 2;
			char *utf8_data = malloc(utf8_length);
			size_t utf8_length_remaining = utf8_length;
			char *utf8_data_remaining = utf8_data;
			char *data_remaining = (char *) data;
			size_t length_remaining = (size_t) length;
			if (utf8_data == NULL)
			{
				iconv_close(cd);
				return;
			}
			iconv(cd, &data_remaining, &length_remaining, &utf8_data_remaining,
			      &utf8_length_remaining);
			iconv_close(cd);
			free_data = True;
			data = (uint8 *) utf8_data;
			length = utf8_length - utf8_length_remaining;
		}
	}
	else if (selection_request.target == format_unicode_atom)
	{
		/* We're expecting a CF_UNICODETEXT response, so what we're
		   receiving matches our requirements and there's no need
		   for further conversions. */
	}
#endif
	else if (selection_request.target == rdesktop_clipboard_formats_atom)
	{
		/* Pass as-is */
	}
	else
	{
		xclip_refuse_selection(&selection_request);
		has_selection_request = False;
		return;
	}

	xclip_provide_selection(&selection_request, selection_request.target, 8, data, length - 1);
	has_selection_request = False;

	if (free_data)
		free(data);
}

void
ui_clip_request_data(uint32 format)
{
	Window selectionowner;

	DEBUG_CLIPBOARD(("Request from server for format %d\n", format));
	rdp_clipboard_request_format = format;

	if (rdesktop_is_selection_owner)
	{
		XChangeProperty(g_display, g_wnd, rdesktop_clipboard_target_atom,
				XA_INTEGER, 32, PropModeReplace, (unsigned char *) &format, 1);

		XConvertSelection(g_display, primary_atom, rdesktop_clipboard_formats_atom,
				  rdesktop_clipboard_target_atom, g_wnd, CurrentTime);
		return;
	}

	selectionowner = XGetSelectionOwner(g_display, primary_atom);
	if (selectionowner != None)
	{
		XConvertSelection(g_display, primary_atom, targets_atom,
				  rdesktop_clipboard_target_atom, g_wnd, CurrentTime);
		return;
	}

	/* No PRIMARY, try CLIPBOARD */
	selectionowner = XGetSelectionOwner(g_display, clipboard_atom);
	if (selectionowner != None)
	{
		XConvertSelection(g_display, clipboard_atom, targets_atom,
				  rdesktop_clipboard_target_atom, g_wnd, CurrentTime);
		return;
	}

	/* No data available */
	cliprdr_send_data(NULL, 0);
}

void
ui_clip_sync(void)
{
	cliprdr_send_simple_native_format_announce(RDP_CF_TEXT);
}


void
xclip_init(void)
{
	if (!cliprdr_init())
		return;

	primary_atom = XInternAtom(g_display, "PRIMARY", False);
	clipboard_atom = XInternAtom(g_display, "CLIPBOARD", False);
	targets_atom = XInternAtom(g_display, "TARGETS", False);
	timestamp_atom = XInternAtom(g_display, "TIMESTAMP", False);
	rdesktop_clipboard_target_atom =
		XInternAtom(g_display, "_RDESKTOP_CLIPBOARD_TARGET", False);
	incr_atom = XInternAtom(g_display, "INCR", False);
	format_string_atom = XInternAtom(g_display, "STRING", False);
	format_utf8_string_atom = XInternAtom(g_display, "UTF8_STRING", False);
	format_unicode_atom = XInternAtom(g_display, "text/unicode", False);
	num_targets = 0;
	targets[num_targets++] = targets_atom;
	targets[num_targets++] = timestamp_atom;
	targets[num_targets++] = rdesktop_clipboard_formats_atom;
	targets[num_targets++] = format_string_atom;
#ifdef USE_UNICODE_CLIPBOARD
	targets[num_targets++] = format_utf8_string_atom;
#endif
	targets[num_targets++] = format_unicode_atom;
	targets[num_targets++] = XA_STRING;

	/* rdesktop sets _RDESKTOP_CLIPBOARD_FORMATS on the root window when acquiring the clipboard.
	   Other interested rdesktops can use this to notify their server of the available formats. */
	rdesktop_clipboard_formats_atom =
		XInternAtom(g_display, "_RDESKTOP_CLIPBOARD_FORMATS", False);
	XSelectInput(g_display, DefaultRootWindow(g_display), PropertyChangeMask);
}
