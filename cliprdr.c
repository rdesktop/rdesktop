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
extern Time last_gesturetime;

// static Time selection_timestamp;
static Atom clipboard_atom, primary_atom, targets_atom, timestamp_atom;
static Atom rdesktop_clipboard_target_atom, incr_atom;
static cliprdr_dataformat *server_formats = NULL;
static uint16 num_server_formats = 0;
static XSelectionEvent selection_event;
static uint16 clipboard_channelno;
static Atom targets[NUM_TARGETS];

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
	DEBUG_CLIPBOARD(("There were %d server formats.\n", i));
#endif
}
/*
static void 
cliprdr_set_selection_timestamp(void)
{
	XEvent xev;
	DEBUG_CLIPBOARD(("Changing a property in order to get a timestamp\n"));
	fflush(stdout);
	XChangeProperty(display, wnd, rdesktop_clipboard_target_atom,
			XA_ATOM, 32, PropModeAppend, 0, 0);
	DEBUG_CLIPBOARD(("Waiting for PropertyChange on wnd\n"));
	fflush(stdout);	
	XWindowEvent(display, wnd, 
		     PropertyChangeMask, &xev);
	DEBUG_CLIPBOARD(("Setting selection_timestamp\n"));
	fflush(stdout);	
	selection_timestamp = xev.xproperty.time;
}	
*/	

static void
cliprdr_send_format_announce(void) 
{
	DEBUG_CLIPBOARD(("Sending format announce\n"));

	STREAM s;
	int number_of_formats = 1;
	s = sec_init(encryption ? SEC_ENCRYPT : 0, number_of_formats*36+12+4+4);
	out_uint32_le(s, number_of_formats*36+12);
	out_uint32_le(s, 0x13);
	out_uint16_le(s, 2);
	out_uint16_le(s, 0);
	out_uint32_le(s, number_of_formats*36);
	
	//	out_uint32_le(s, 0xd); // FIXME: This is a rather bogus unicode text description..
	//	rdp_out_unistr(s, "", 16);
	//	out_uint8s(s, 32);


	out_uint32_le(s, 1); // FIXME: This is a rather bogus text description..
	out_uint8s(s, 32);

	out_uint32_le(s, 0); 

	s_mark_end(s);
	sec_send_to_channel(s, encryption ? SEC_ENCRYPT : 0, 
			    clipboard_channelno); 
}


static void
cliprdr_send_empty_datapacket(void)
{
	STREAM out;
	out =  sec_init(encryption ? SEC_ENCRYPT : 0, 
			20);
	out_uint32_le(out, 12);
	out_uint32_le(out, 0x13);
	out_uint16_le(out, 5);
	out_uint16_le(out, 1);
	out_uint32_le(out, 0);
	/* Insert null string here? */
	out_uint32_le(out, 0);
	s_mark_end(out);
	
	sec_send_to_channel(out, encryption ? SEC_ENCRYPT : 0, 
			    clipboard_channelno); 
}


void 
cliprdr_handle_SelectionNotify(XSelectionEvent *event)
{

	unsigned char	*data, *datap;
	unsigned long	nitems, bytes_left;
	
	unsigned long bytes_left_to_transfer;
	int res, i;

	int format;
	Atom type_return;
	Atom best_target;
	Atom *supported_targets;

	STREAM out;
	
	DEBUG_CLIPBOARD(("cliprdr_handle_SelectionNotify\n"));

	if (None == event->property) {
		cliprdr_send_empty_datapacket();
		return; /* Selection failed */
	} 

	DEBUG_CLIPBOARD(("selection: %s, target: %s, property: %s\n",
			 XGetAtomName(display, event->selection),
			 XGetAtomName(display, event->target),
			 XGetAtomName(display, event->property)));

	if (targets_atom == event->target) {
		/* Response to TARGETS request. Let's find the target
		   we want and request that */
		res = XGetWindowProperty(display, wnd, 
					 rdesktop_clipboard_target_atom,
					 0L, 4096L, False, AnyPropertyType, 
					 &type_return,
					 &format, &nitems, &bytes_left, &data);

		if (Success != res) 
		{
			DEBUG_CLIPBOARD(("XGetWindowProperty failed!\n"));
			cliprdr_send_empty_datapacket();
			return;
		}

		if (None == type_return) 
			/* The owner might no support TARGETS. Just try
			   STRING */
			best_target = XA_STRING;
		else 
		{
			/* FIXME: We should choose format here based
			   on what the server wanted */
			supported_targets = (Atom *)data;
			for (i=0;i<nitems;i++) 
			{
				DEBUG_CLIPBOARD(("Target %d: %s\n", 
						 i, XGetAtomName(display,
								 supported_targets[i])));
			}
			best_target = XInternAtom(display, "TEXT", False);
			
			
		}

		XConvertSelection(display, primary_atom, 
				  best_target,
				  rdesktop_clipboard_target_atom, 
				  wnd, event->time);

	} 
	else  /* Other clipboard data */
	{
		
		res = XGetWindowProperty(display, wnd, 
					 rdesktop_clipboard_target_atom,
					 0L, 0x1FFFFFF, 
					 True, AnyPropertyType, 
					 &type_return,
					 &format, &nitems, &bytes_left, &data);


		/* FIXME: We need to handle INCR as well, 
		 this is a temporary solution. */

		if (incr_atom == type_return) 
		{
			warning("We don't support INCR transfers at this time. Try cutting less data\n");
			cliprdr_send_empty_datapacket();
		}


		if (Success != res) 
		{
			DEBUG_CLIPBOARD(("XGetWindowProperty failed!\n"));
			cliprdr_send_empty_datapacket();
			return;
		}

		DEBUG_CLIPBOARD(("Received %d bytes of clipboard data from X, there is %d remaining\n",
				 nitems, bytes_left));
		DEBUG_CLIPBOARD(("type_return is %s\n", 
				 XGetAtomName(display, type_return)));

		datap = data;

		if (nitems+1 <= MAX_CLIPRDR_STANDALONE_DATASIZE) 
		{
			out =  sec_init(encryption ? SEC_ENCRYPT : 0, 
					20+nitems+1);
			out_uint32_le(out, 12+nitems+1);
			out_uint32_le(out, 0x13);
			out_uint16_le(out, 5);
			out_uint16_le(out, 1);
			out_uint32_le(out, nitems+1);
			out_uint8p(out, datap, nitems+1);
			out_uint32_le(out, 0);
			s_mark_end(out);
	
			sec_send_to_channel(out, encryption ? SEC_ENCRYPT : 0, 
					    clipboard_channelno); 

		} 
		else
		{
			DEBUG_CLIPBOARD(("Sending %d bytes of data\n",
					 16+MAX_CLIPRDR_STANDALONE_DATASIZE));
			out =  sec_init(encryption ? SEC_ENCRYPT : 0, 
					16+MAX_CLIPRDR_STANDALONE_DATASIZE);
			out_uint32_le(out, nitems+12);
			out_uint32_le(out, 0x11);
			out_uint16_le(out, 5);
			out_uint16_le(out, 1);
			out_uint32_le(out, nitems);
			out_uint8p(out, datap, 
				   MAX_CLIPRDR_STANDALONE_DATASIZE);
			s_mark_end(out);
	
			sec_send_to_channel(out, encryption ? SEC_ENCRYPT : 0, 
					    clipboard_channelno); 

			bytes_left_to_transfer = nitems - MAX_CLIPRDR_STANDALONE_DATASIZE;
			datap+=MAX_CLIPRDR_STANDALONE_DATASIZE;

			while (bytes_left_to_transfer > MAX_CLIPRDR_STANDALONE_DATASIZE) 
			{
				DEBUG_CLIPBOARD(("Sending %d bytes of data\n",
					 16+MAX_CLIPRDR_CONTINUATION_DATASIZE));
				out =  sec_init(encryption ? SEC_ENCRYPT : 0, 
						8+MAX_CLIPRDR_CONTINUATION_DATASIZE);
				out_uint32_le(out, nitems);
				out_uint32_le(out, 0x10);
				out_uint8p(out, datap, 
					   MAX_CLIPRDR_CONTINUATION_DATASIZE);
				s_mark_end(out);

				sec_send_to_channel(out, 
						    encryption ? SEC_ENCRYPT : 0, 
						    clipboard_channelno);
				bytes_left_to_transfer-= MAX_CLIPRDR_CONTINUATION_DATASIZE;
				datap+=MAX_CLIPRDR_CONTINUATION_DATASIZE;
				
			}
			DEBUG_CLIPBOARD(("Sending %d bytes of data\n", 
					 12+bytes_left_to_transfer));
			out =  sec_init(encryption ? SEC_ENCRYPT : 0, 
					12+bytes_left_to_transfer);
			out_uint32_le(out, nitems);
			out_uint32_le(out, 0x12);
			out_uint8p(out, datap, 
				   bytes_left_to_transfer);
			out_uint32_le(out, 0x0);
			s_mark_end(out);
	
			sec_send_to_channel(out, encryption ? SEC_ENCRYPT : 0, 
					    clipboard_channelno); 

		}


		XFree(data);
		cliprdr_send_format_announce();
		
	}
	
	
}

void
cliprdr_handle_SelectionClear(void)
{
	DEBUG_CLIPBOARD(("cliprdr_handle_SelectionClear\n"));
	cliprdr_send_format_announce();
}


static void
cliprdr_request_clipboard_data(uint32 formatcode) 
{
	STREAM s;
	s = sec_init(encryption ? SEC_ENCRYPT : 0, 24);
	out_uint32_le(s, 16);
	out_uint32_le(s, 0x13);
	out_uint16_le(s, 4);
	out_uint16_le(s, 0);
	out_uint32_le(s, 4); // Remaining length
	out_uint32_le(s, formatcode);
	out_uint32_le(s, 0); // Unknown. Garbage pad?

	s_mark_end(s);

	sec_send_to_channel(s, encryption ? SEC_ENCRYPT : 0, 
			    clipboard_channelno); 
}


void
cliprdr_handle_SelectionRequest(XSelectionRequestEvent *xevent) 
{

	XSelectionEvent xev;
	DEBUG_CLIPBOARD(("cliprdr_handle_SelectionRequest\n"));
	DEBUG_CLIPBOARD(("Requestor window id 0x%x ", 
			 (unsigned)xevent->requestor));
	if (clipboard_atom == xevent->selection) {
		DEBUG_CLIPBOARD(("wants CLIPBOARD\n"));
	} 
	if (primary_atom == xevent->selection) {
		DEBUG_CLIPBOARD(("wants PRIMARY\n"));
	}  
	DEBUG_CLIPBOARD(("Target is %s (0x%x), property is %s (0x%x)\n", 
			 XGetAtomName(display, xevent->target), 
			 (unsigned)xevent->target, 
			 XGetAtomName(display, xevent->property), 
			 (unsigned)xevent->property));

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
		XChangeProperty(display, 
				xevent->requestor,
				xevent->property,
				XA_ATOM,
				32,
				PropModeAppend,
				(unsigned char *)&targets,
				NUM_TARGETS);

		XSendEvent(display, 
			   xevent->requestor, 
			   False, 
			   NoEventMask,
			   (XEvent *)&xev);
		return;
	} else if (timestamp_atom == xevent->target) 
	{
		XChangeProperty(display, 
				xevent->requestor,
				xevent->property,
				XA_INTEGER,
				32,
				PropModeAppend,
				(unsigned char *)&last_gesturetime,
				1);
		XSendEvent(display, 
			   xevent->requestor, 
			   False, 
			   NoEventMask,
			   (XEvent *)&xev);
	} else /* Some other target */
	{
		cliprdr_request_clipboard_data(CF_TEXT);
		memcpy(&selection_event, &xev, sizeof(xev));
		/* Return and wait for data, handled by 
		   cliprdr_handle_server_data */
	}
}


static void 
cliprdr_ack_format_list(void) 
{
	STREAM s;
	s = sec_init(encryption ? SEC_ENCRYPT : 0, 20);
	out_uint32_le(s, 12);
	out_uint32_le(s, 0x13);
	out_uint16_le(s, 3);
	out_uint16_le(s, 1);
	out_uint32_le(s, 0);
	out_uint32_le(s, 0x0000c0da);

	s_mark_end(s);

	sec_send_to_channel(s, encryption ? SEC_ENCRYPT : 0, 
			    clipboard_channelno); 
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
		DEBUG_CLIPBOARD(("Stored format description with numeric id %d\n", 
				 this->identifier));
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
	XSetSelectionOwner(display, primary_atom, wnd, last_gesturetime);
	if (wnd != XGetSelectionOwner(display, primary_atom))
	{
		warning("Failed to aquire ownership of PRIMARY clipboard\n");
	}
	XSetSelectionOwner(display, clipboard_atom, wnd, last_gesturetime);
	if (wnd != XGetSelectionOwner(display, clipboard_atom)) 
	{
		warning("Failed to aquire ownership of CLIPBOARD clipboard\n");
	}		
	
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

void cliprdr_handle_server_data(uint32 length, uint32 flags, STREAM s) 
{
	static uint32 remaining_length;
	static char *data, *datap;
	static uint32 bytes_left_to_read;
	DEBUG_CLIPBOARD(("In cliprdr_handle_server_data, flags is %d\n", 
			 flags));
	if (3 == flags)  /* One-op write, no packets follows */
	{
		in_uint32_le(s, remaining_length);
		data = s->p;
	} else if (1 == flags) /* First of several packets */
	{	
		in_uint32_le(s, remaining_length);
		DEBUG_CLIPBOARD(("Remaining length is %d\n", 
				 remaining_length));
		data = xmalloc(remaining_length);
		datap = data;
		DEBUG_CLIPBOARD(("Copying first %d bytes\n", 
				 MAX_CLIPRDR_STANDALONE_DATASIZE));
		memcpy(datap, s->p, MAX_CLIPRDR_STANDALONE_DATASIZE);

		datap+=MAX_CLIPRDR_STANDALONE_DATASIZE;
		bytes_left_to_read = remaining_length-MAX_CLIPRDR_STANDALONE_DATASIZE;
		return;
	} else if (0 == flags) 
	{
		DEBUG_CLIPBOARD(("Copying %d middle bytes",
				 MAX_CLIPRDR_CONTINUATION_DATASIZE));
		memcpy(datap, s->p, MAX_CLIPRDR_CONTINUATION_DATASIZE);

		datap+=MAX_CLIPRDR_CONTINUATION_DATASIZE;
		bytes_left_to_read-=MAX_CLIPRDR_CONTINUATION_DATASIZE;
		return;
	} else if (2 == flags)
	{
		DEBUG_CLIPBOARD(("Copying last %d bytes\n", 
				 bytes_left_to_read));
		memcpy(datap, s->p, bytes_left_to_read);
	}
	XChangeProperty(display, 
			selection_event.requestor,
			selection_event.property,
			XInternAtom(display, "STRING", False),
			8,
			PropModeAppend,
			data,
			remaining_length-1);

	XSendEvent(display, 
		   selection_event.requestor, 
		   False, 
		   NoEventMask,
		   (XEvent *)&selection_event);

	if (2 == flags)
		xfree(data);

}

void cliprdr_handle_server_data_request(STREAM s) 
{
	Window selectionowner;
	uint32 remaining_length;
	uint32 wanted_formatcode, pad;

	in_uint32_le(s, remaining_length);
	in_uint32_le(s, wanted_formatcode);
	in_uint32_le(s, pad);

	/* FIXME: Check that we support this formatcode */

	DEBUG_CLIPBOARD(("Request from server for format %d\n", 
			 wanted_formatcode));

	selectionowner = XGetSelectionOwner(display, primary_atom);

	if (None != selectionowner) 
	{
	
		/* FIXME: Perhaps we should check if we are the owner? */

		XConvertSelection(display, primary_atom, 
				  targets_atom,
				  rdesktop_clipboard_target_atom, 
				  wnd, CurrentTime);

		/* The rest of the transfer is handled in 
		   cliprdr_handle_SelectionNotify */

	} else 
	{
		DEBUG_CLIPBOARD(("There were no owner for PRIMARY, sending empty string\n")); // FIXME: Should we always send an empty string?

		cliprdr_send_empty_datapacket();
	}


}
	

void cliprdr_callback(STREAM s, uint16 channelno) 
{
	static int failed_clipboard_acks = 0;
	struct timeval timeval;
	uint32 length, flags;
	uint16 ptype0, ptype1;
	clipboard_channelno = channelno;
	DEBUG_CLIPBOARD(("cliprdr_callback called with channelno %d, clipboard data:\n", channelno));
#ifdef WITH_DEBUG_CLIPBOARD
	//	hexdump(s->p, s->end - s->p);
#endif
	in_uint32_le(s, length);
	in_uint32_le(s, flags);

	DEBUG_CLIPBOARD(("length is %d, flags are %d\n", length, flags));

	if (3 == flags || 1 == flags) /* Single-write op or first-packet-of-several op */
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
			failed_clipboard_acks = 0;
			return;

		} else if (3 == ptype0 && 2 == ptype1) 
		{
			DEBUG_CLIPBOARD(("Received failed clipboard format announce ACK, retrying\n"));

			/* This is a fairly portable way to sleep 1/10 of
			   a second.. */
			timeval.tv_sec = 0;
			timeval.tv_usec = 100;
			select(0, NULL, NULL, NULL, &timeval);
			if (failed_clipboard_acks < 3)
			{
				
				cliprdr_send_format_announce();
				/* Make sure we don't get stuck in this loop */
				failed_clipboard_acks++;
			} 
			else
			{
				warning("Reached maximum number of clipboard format announce attempts. Pasting in Windows probably won't work well now.\n");
			}
		} else if (2 == ptype0 && 0 == ptype1) 
		{
			cliprdr_register_server_formats(s);
			cliprdr_select_X_clipboards();
			cliprdr_ack_format_list();
			return;
		} else if (5 == ptype0 && 1 == ptype1) 
		{
			cliprdr_handle_server_data(length, flags, s);
		} else if (4 == ptype0 && 0 == ptype1) 
		{
			cliprdr_handle_server_data_request(s);
		}

		
	} 
	else 
	{
		DEBUG_CLIPBOARD(("Handling middle or last packet\n"));
		cliprdr_handle_server_data(length, flags, s);
	}
}


void cliprdr_init(void) 
{
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
	targets[5] = XInternAtom(display, "STRING", False);

}
