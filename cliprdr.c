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

#include "rdesktop.h"

#define CLIPRDR_CONNECT			1
#define CLIPRDR_FORMAT_ANNOUNCE		2
#define CLIPRDR_FORMAT_ACK		3
#define CLIPRDR_DATA_REQUEST		4
#define CLIPRDR_DATA_RESPONSE		5

#define CLIPRDR_REQUEST			0
#define CLIPRDR_RESPONSE		1
#define CLIPRDR_ERROR			2

static VCHANNEL *cliprdr_channel;

static void
cliprdr_send_packet(uint16 type, uint16 status, uint8 * data, uint32 length)
{
	STREAM s;

	DEBUG_CLIPBOARD(("CLIPRDR send: type=%d, status=%d, length=%d\n", type, status, length));

	s = channel_init(cliprdr_channel, length + 12);
	out_uint16_le(s, type);
	out_uint16_le(s, status);
	out_uint32_le(s, length);
	out_uint8p(s, data, length);
	out_uint32(s, 0);	/* pad? */
	s_mark_end(s);
	channel_send(s, cliprdr_channel);
}

void
cliprdr_send_text_format_announce(void)
{
	uint8 buffer[36];

	buf_out_uint32(buffer, CF_TEXT);
	memset(buffer + 4, 0, sizeof(buffer) - 4);	/* description */
	cliprdr_send_packet(CLIPRDR_FORMAT_ANNOUNCE, CLIPRDR_REQUEST, buffer, sizeof(buffer));
}

void
cliprdr_send_blah_format_announce(void)
{
	uint8 buffer[36];

	buf_out_uint32(buffer, CF_OEMTEXT);
	memset(buffer + 4, 0, sizeof(buffer) - 4);	/* description */
	cliprdr_send_packet(CLIPRDR_FORMAT_ANNOUNCE, CLIPRDR_REQUEST, buffer, sizeof(buffer));
}

void
cliprdr_send_native_format_announce(uint8 * data, uint32 length)
{
	cliprdr_send_packet(CLIPRDR_FORMAT_ANNOUNCE, CLIPRDR_REQUEST, data, length);
}

void
cliprdr_send_data_request(uint32 format)
{
	uint8 buffer[4];

	buf_out_uint32(buffer, format);
	cliprdr_send_packet(CLIPRDR_DATA_REQUEST, CLIPRDR_REQUEST, buffer, sizeof(buffer));
}

void
cliprdr_send_data(uint8 * data, uint32 length)
{
	cliprdr_send_packet(CLIPRDR_DATA_RESPONSE, CLIPRDR_RESPONSE, data, length);
}

static void
cliprdr_process(STREAM s)
{
	uint16 type, status;
	uint32 length, format;
	uint8 *data;

	in_uint16_le(s, type);
	in_uint16_le(s, status);
	in_uint32_le(s, length);
	data = s->p;

	DEBUG_CLIPBOARD(("CLIPRDR recv: type=%d, status=%d, length=%d\n", type, status, length));

	if (status == CLIPRDR_ERROR)
	{
		if (type == CLIPRDR_FORMAT_ACK)
		{
			/* FIXME: We seem to get this when we send an announce while the server is
			   still processing a paste. Try sending another announce. */
			cliprdr_send_text_format_announce();
			return;
		}

		DEBUG_CLIPBOARD(("CLIPRDR error (type=%d)\n", type));
		return;
	}

	switch (type)
	{
		case CLIPRDR_CONNECT:
			ui_clip_sync();
			break;
		case CLIPRDR_FORMAT_ANNOUNCE:
			ui_clip_format_announce(data, length);
			cliprdr_send_packet(CLIPRDR_FORMAT_ACK, CLIPRDR_RESPONSE, NULL, 0);
			return;
		case CLIPRDR_FORMAT_ACK:
			break;
		case CLIPRDR_DATA_REQUEST:
			in_uint32_le(s, format);
			ui_clip_request_data(format);
			break;
		case CLIPRDR_DATA_RESPONSE:
			ui_clip_handle_data(data, length);
			break;
		default:
			unimpl("CLIPRDR packet type %d\n", type);
	}
}

BOOL
cliprdr_init(void)
{
	cliprdr_channel =
		channel_register("cliprdr",
				 CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP |
				 CHANNEL_OPTION_COMPRESS_RDP | CHANNEL_OPTION_SHOW_PROTOCOL,
				 cliprdr_process);
	return (cliprdr_channel != NULL);
}
