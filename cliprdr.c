/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - Clipboard functions
   Copyright (C) Erik Forsberg <forsberg@cendio.se> 2003

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

extern BOOL encryption;
extern Display *display;
extern Window wnd;
extern Time last_keyrelease;

static Atom clipboard_atom, primary_atom, targets_atom, timestamp_atom;
static cliprdr_dataformat *server_formats = NULL;
static uint16 num_server_formats = 0;

static void
cliprdr_print_server_formats(void) 
{
#ifdef WITH_DEBUG_CLIPBOARD
	cliprdr_dataformat *this;
	uint16 i = 0;
	this = server_formats;
	DEBUG_CLIPBOARD(("There should be %d server formats.\n", num_server_formats));
	while (NULL != this) 
	{
		DEBUG_CLIPBOARD(("Format code %d\n", this->identifier));
		i++;
		this = this->next;
	}
	DEBUG_CLIPBOARD(("There was %d server formats.\n", i));
#endif
}

void 
cliprdr_handle_SelectionNotify(void)
{
	DEBUG_CLIPBOARD(("cliprdr_handle_SelectionNotify\n"));
}

void
cliprdr_handle_SelectionClear(void)
{
	DEBUG_CLIPBOARD(("cliprdr_handle_SelectionClear\n"));
}

void print_X_error(int res) 
{
	switch(res) {
	case Success:
		DEBUG_CLIPBOARD(("Success\n"));
		break;

	case BadAtom:
		DEBUG_CLIPBOARD(("BadAtom\n"));
		break;

	case BadRequest:
		DEBUG_CLIPBOARD(("BadRequest\n"));
		break;

	case BadAlloc:
		DEBUG_CLIPBOARD(("BadAlloc\n"));
		break;

	case BadMatch:
		DEBUG_CLIPBOARD(("BadMatch\n"));
		break;

	case BadValue:
		DEBUG_CLIPBOARD(("BadValue\n"));
		break;

	case BadWindow:
		DEBUG_CLIPBOARD(("BadWindo\n"));
		break;

	default:
		DEBUG_CLIPBOARD(("Unknown X error code %d\n", res));
	}
}

void
cliprdr_handle_SelectionRequest(XSelectionRequestEvent *xevent) 
{

	Atom type_return;
	Atom *targets;
	int format_return;
	long nitems_return;
	long bytes_after_return;
	char **prop_return;
	int res;

	XSelectionEvent xev;
	DEBUG_CLIPBOARD(("cliprdr_handle_SelectionRequest\n"));
	DEBUG_CLIPBOARD(("Requestor window id 0x%x ", xevent->requestor));
	if (clipboard_atom == xevent->selection) {
		DEBUG_CLIPBOARD(("wants CLIPBOARD\n"));
	} 
	if (primary_atom == xevent->selection) {
		DEBUG_CLIPBOARD(("wants PRIMARY\n"));
	}  
	DEBUG_CLIPBOARD(("Target is %s (0x%x), property is %s (0x%x)\n", 
			 XGetAtomName(display, xevent->target), 
			 xevent->target, 
			 XGetAtomName(display, xevent->property), 
			 xevent->property));

	xev.type = SelectionNotify;
	xev.serial = 0;
	xev.send_event = True;
	xev.requestor = xevent->requestor;
	xev.selection = xevent->selection;
	xev.target = xevent->target;
	xev.property = xevent->property;
	xev.time = xevent->time;

	if (targets_atom == xevent->target) 
	{
		DEBUG_CLIPBOARD(("TARGETS requested, sending list..\n"));
		targets = xmalloc(4*sizeof(Atom));
		targets[0] = xevent->target;
		targets[1] = XInternAtom(display, "TEXT", True);
		targets[2] = XInternAtom(display, "UTF8_STRING", True);
		targets[3] = XInternAtom(display, "TIMESTAMP", True);
		res = XChangeProperty(display, 
				      xevent->requestor,
				      xevent->property,
				      XA_ATOM,
				      32,
				      PropModeAppend,
				      (unsigned char *)targets,
				      3);
		DEBUG_CLIPBOARD(("res after XChangeProperty is "));
		print_X_error(res);	

		res = XSendEvent(display, 
				 xevent->requestor, 
				 False, 
				 NoEventMask,
				 (XEvent *)&xev);
		return;
	} else if (timestamp_atom == xevent->target) 
	{
		DEBUG_CLIPBOARD(("TIMESTAMP requested... sending 0x%x\n",
				 last_keyrelease));
		res = XChangeProperty(display, 
				      xevent->requestor,
				      xevent->property,
				      XA_INTEGER,
				      32,
				      PropModeAppend,
				      (unsigned char *)&last_keyrelease,
				      1);
		res = XSendEvent(display, 
				 xevent->requestor, 
				 False, 
				 NoEventMask,
				 (XEvent *)&xev);
	} else /* Some other target */
	{
		res = XChangeProperty(display, 
				      xevent->requestor,
				      xevent->property,
				      XInternAtom(display, "STRING", False),
				      8,
				      PropModeAppend,
				      "krattoflabkat",
				      13);

		DEBUG_CLIPBOARD(("res after XChangeProperty is "));
		print_X_error(res);	
	
		xev.type = SelectionNotify;
		xev.serial = 0;
		xev.send_event = True;
		xev.requestor = xevent->requestor;
		xev.selection = xevent->selection;
		xev.target = xevent->target;
		xev.property = xevent->property;
		xev.time = xevent->time;

		res = XSendEvent(display, 
				 xevent->requestor, 
				 False, 
				 NoEventMask,
				 (XEvent *)&xev);
	
		DEBUG_CLIPBOARD(("res after XSendEvent is "));
		print_X_error(res);
	}
}


static void
cliprdr_register_server_formats(STREAM s) 
{
	uint32 remaining_length, pad;
	uint16 num_formats;
	cliprdr_dataformat *this, *next;

	DEBUG_CLIPBOARD(("cliprdr_register_server_formats\n"));
	in_uint32_le(s, remaining_length);

	num_formats = remaining_length / 36;
	if (NULL != server_formats) {
		this = server_formats;
		next = this->next;
		while (NULL != next) {
			xfree(this);
			this = NULL;
			this = next;
			next = this->next;
		}
	} 
	this = xmalloc(sizeof(cliprdr_dataformat));
	this->next = NULL;
	server_formats = this;
	num_server_formats = num_formats;
	while (1 < num_formats) {
		in_uint32_le(s, this->identifier);
		in_uint8a(s, this->textual_description, 32);
		DEBUG_CLIPBOARD(("Stored format description with numeric id %d\n", this->identifier));
		this-> next = xmalloc(sizeof(cliprdr_dataformat));
		this = this->next;
		num_formats--;
	}
	in_uint32_le(s, this->identifier);
	DEBUG_CLIPBOARD(("Stored format description with numeric id %d\n", this->identifier));
	in_uint8a(s, this->textual_description, 32);
	this -> next = NULL;
	in_uint32_le(s, pad);
	cliprdr_print_server_formats();
}

static void
cliprdr_select_X_clipboards(void) 
{
	XSetSelectionOwner(display, primary_atom, wnd, last_keyrelease);
	if (wnd != XGetSelectionOwner(display, primary_atom))
	{
		warning("Failed to aquire ownership of PRIMARY clipboard\n");
	}
	XSetSelectionOwner(display, clipboard_atom, wnd, CurrentTime);
	if (wnd != XGetSelectionOwner(display, clipboard_atom)) 
	{
		warning("Failed to aquire ownership of CLIPBOARD clipboard\n");
	}		
	
}

static void
cliprdr_send_format_announce(void) 
{
	STREAM s;
	int number_of_formats = 1;
	s = sec_init(encryption ? SEC_ENCRYPT : 0, number_of_formats*36+12+4+4);
	out_uint32_le(s, number_of_formats*36+12);
	out_uint32_le(s, 0x13);
	out_uint16_le(s, 2);
	out_uint16_le(s, 0);
	out_uint32_le(s, number_of_formats*36);
	
	out_uint32_le(s, 0xd); // FIXME: This is a rather bogus unicode text description..
	//	rdp_out_unistr(s, "", 16);
	out_uint8s(s, 32);

	out_uint32_le(s, 0); 

	s_mark_end(s);
	sec_send_to_channel(s, encryption ? SEC_ENCRYPT : 0, 1005); // FIXME: Don't hardcode channel!
}
		

static void 
cliprdr_handle_first_handshake(STREAM s)
{
	uint32 remaining_length, pad;
	in_uint32_le(s, remaining_length);
	in_uint32_le(s, pad);
	DEBUG_CLIPBOARD(("Remaining length in first handshake frm server is %d, pad is %d\n", 
			 remaining_length, pad));
	cliprdr_send_format_announce();
}

void cliprdr_callback(STREAM s) 
{
	uint32 length, flags;
	uint16 ptype0, ptype1;
	DEBUG_CLIPBOARD(("cliprdr_callback called, clipboard data:\n"));
#ifdef WITH_DEBUG_CLIPBOARD
	hexdump(s->p, s->end - s->p);
#endif
	in_uint32_le(s, length);
	in_uint32_le(s, flags);

	DEBUG_CLIPBOARD(("length is %d, flags are %d\n", length, flags));

	if (flags & 0x03 || flags & 0x01) /* Single-write op or first-packet-of-several op */
	{
		in_uint16_le(s, ptype0);
		in_uint16_le(s, ptype1);
		DEBUG_CLIPBOARD(("ptype0 is %d, ptype1 is %d\n", ptype0, ptype1));
		if (1 == ptype0 && 0 == ptype1) {
			cliprdr_handle_first_handshake(s);
			return;
		} else if (3 == ptype0 && 1 == ptype1) 
		{
			// Acknowledgment on our format announce. Do we care? Not right now.
			// There is a strange pad in this packet that we might need some time,
			// but probably not.
			DEBUG_CLIPBOARD(("Received format announce ACK\n"));
			return;
		} else if (2 == ptype0 && 0 == ptype1) 
		{
			cliprdr_register_server_formats(s);
			cliprdr_select_X_clipboards();
			return;
		}
		
	}
}

void cliprdr_init(void) 
{
	primary_atom = XInternAtom(display, "PRIMARY", False);
	clipboard_atom = XInternAtom(display, "CLIPBOARD", False);
	targets_atom = XInternAtom(display, "TARGETS", True);
	timestamp_atom = XInternAtom(display, "TIMESTAMP", True);
}
