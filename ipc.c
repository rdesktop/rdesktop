/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Communication between different rdesktop processes using X properties
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
#include "xproto.h"

Atom ipc_atom;

extern Display *display;
extern Window wnd;

static struct stream in;
static struct stream out;
static const uint16 headerlen = 12;

struct _ipc_channel;

typedef struct _ipc_channel
{
	uint16 messagetype;
	void (*callback) (unsigned char *, uint16 length);
	struct _ipc_channel *next;
}
ipc_channel;

static ipc_channel *ipc_channels = NULL;

void
ipc_register_ipcnotify(uint16 messagetype, void (*notifycallback) (unsigned char *, uint16))
{
	ipc_channel *this;
	if (NULL != ipc_channels)
	{
		this = ipc_channels;
		while (NULL != this->next)
			this = this->next;
		this->next = xmalloc(sizeof(ipc_channel));
		this->next->next = NULL;
		this = this->next;
	}
	else
	{
		this = xmalloc(sizeof(ipc_channel));
		this->next = NULL;
		ipc_channels = this;
	}
	this->messagetype = messagetype;
	this->callback = notifycallback;
}

void
ipc_deregister_ipcnotify(uint16 messagetype)
{
	ipc_channel *this, *prev;
	prev = this = ipc_channels;
	while (NULL != this)
	{
		if (this->messagetype == messagetype)
		{
			if (prev == this)
				ipc_channels = this->next;
			else
				prev->next = this->next;
			xfree(this);
			return;
		}
	}
}

void
ipc_recv_message(XPropertyEvent * xev)
{
	int actual_format_return;
	unsigned long nitems_return, bytes_after_return;
	unsigned char *prop_return;
	unsigned char *data = NULL;
	uint16 totalsize, rdesktop_ipc_version, messagetype;
	uint32 sender_wnd;
	Atom actual_type_return;
	ipc_channel *channel = ipc_channels;

	DEBUG_RDP5(("Got event in ipc_recv_message\n"));

	in.end = in.p = in.data;

	XGetWindowProperty(display, DefaultRootWindow(display), ipc_atom, 0, 1, False,	// Delete
			   AnyPropertyType,
			   &actual_type_return,
			   &actual_format_return,
			   &nitems_return, &bytes_after_return, &prop_return);

	memcpy(in.end, prop_return, 2);
	XFree(prop_return);
	in_uint32_le(&in, totalsize);
	in.end += 4;

	DEBUG_RDP5(("ipc totalsize is %d\n", totalsize));

	XGetWindowProperty(display, DefaultRootWindow(display), ipc_atom, 1, (totalsize - 1) / 4, False,	// Delete
			   AnyPropertyType,
			   &actual_type_return,
			   &actual_format_return,
			   &nitems_return, &bytes_after_return, &prop_return);

	memcpy(in.end, prop_return, totalsize - 4);
	XFree(prop_return);

	in_uint16_le(&in, rdesktop_ipc_version);

	DEBUG_RDP5(("Got rdesktop_ipc_version %d\n", rdesktop_ipc_version));

	if (rdesktop_ipc_version > RDESKTOP_IPC_VERSION)
	{
		warning("IPC version of sending Rdesktop is higher than ours. Returning without trying to parse message\n");
		return;
	}

	in_uint16_le(&in, messagetype);
	in_uint32_le(&in, sender_wnd);

	DEBUG_RDP5(("header: %d, %d, %d\n", rdesktop_ipc_version, messagetype, sender_wnd));


	if (sender_wnd == wnd)
	{
		DEBUG_RDP5(("We sent this message, returning..\n"));
		return;		/* We are not interested in our own events.. */
	}

	DEBUG_RDP5(("Not our window..\n"));

	/* Check if we are interested by traversing our callback list */
	while (NULL != channel)
	{
		DEBUG_RDP5(("Found channel for messagetype %d\n", channel->messagetype));
		if (messagetype == channel->messagetype)
		{
			data = xmalloc(in.size - headerlen);
			in_uint8p(&in, data, totalsize - headerlen);
			channel->callback(data, totalsize - headerlen);
			return;
		}
		/* Callback is responsible for freeing data */
		channel = channel->next;
	}
}

void
ipc_init(void)
{
	ipc_atom = XInternAtom(display, "_RDESKTOP_IPC", False);
	xwin_register_propertynotify(DefaultRootWindow(display), ipc_atom, ipc_recv_message);
	in.size = 4096;
	in.data = xmalloc(in.size);
	out.size = 4096;
	out.data = xmalloc(out.size);
}

void
ipc_send_message(uint16 messagetype, unsigned char *data, uint16 length)
{

	if ((length + headerlen) > out.size)
	{
		out.data = xrealloc(out.data, length + headerlen);
		out.size = length + headerlen;
	}
	out.p = out.data;
	out.end = out.data + out.size;

	out_uint32_le(&out, length + headerlen);

	out_uint16_le(&out, RDESKTOP_IPC_VERSION);
	out_uint16_le(&out, messagetype);
	out_uint32_le(&out, wnd);

	out_uint8p(&out, data, length);
	s_mark_end(&out);

	DEBUG_RDP5(("length+headerlen is %d\n", length + headerlen));

	XChangeProperty(display,
			DefaultRootWindow(display),
			ipc_atom, XA_STRING, 8, PropModeReplace, out.data, out.end - out.data);
	XFlush(display);
}
