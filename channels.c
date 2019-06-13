/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - Virtual channels
   Copyright 2003 Erik Forsberg <forsberg@cendio.se> for Cendio AB
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 2003-2008
   Copyright 2016 Alexander Zakharov <uglym8@gmail.com>

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

#include "rdesktop.h"

#define MAX_CHANNELS			6
#define CHANNEL_CHUNK_LENGTH		1600
#define CHANNEL_FLAG_FIRST		0x01
#define CHANNEL_FLAG_LAST		0x02
#define CHANNEL_FLAG_SHOW_PROTOCOL	0x10

extern RDP_VERSION g_rdp_version;
extern RD_BOOL g_encryption;

uint32 vc_chunk_size = CHANNEL_CHUNK_LENGTH;

VCHANNEL g_channels[MAX_CHANNELS];
unsigned int g_num_channels;

/* FIXME: We should use the information in TAG_SRV_CHANNELS to map RDP5
   channels to MCS channels.

   The format of TAG_SRV_CHANNELS seems to be

   global_channel_no (uint16le)
   number_of_other_channels (uint16le)
   ..followed by uint16les for the other channels.
*/

VCHANNEL *
channel_register(char *name, uint32 flags, void (*callback) (STREAM))
{
	VCHANNEL *channel;

	if (g_rdp_version < RDP_V5)
		return NULL;

	if (g_num_channels >= MAX_CHANNELS)
	{
		logger(Core, Error,
		       "channel_register(), channel table full, increase MAX_CHANNELS");
		return NULL;
	}

	channel = &g_channels[g_num_channels];
	channel->mcs_id = MCS_GLOBAL_CHANNEL + 1 + g_num_channels;
	strncpy(channel->name, name, 8);
	channel->flags = flags;
	channel->process = callback;
	g_num_channels++;
	return channel;
}

STREAM
channel_init(VCHANNEL * channel, uint32 length)
{
	UNUSED(channel);
	STREAM s;

	s = sec_init(g_encryption ? SEC_ENCRYPT : 0, length + 8);
	s_push_layer(s, channel_hdr, 8);
	return s;
}

static void
channel_send_chunk(STREAM s, VCHANNEL * channel, uint32 length)
{
	uint32 flags;
	uint32 thislength;
	RD_BOOL inplace;
	STREAM chunk;

	/* Note: In the original clipboard implementation, this number was
	   1592, not 1600. However, I don't remember the reason and 1600 seems
	   to work so.. This applies only to *this* length, not the length of
	   continuation or ending packets. */

	/* Actually, CHANNEL_CHUNK_LENGTH (default value is 1600 bytes) is described
	   in MS-RDPBCGR (s. 2.2.6, s.3.1.5.2.1) and can be set by server only
	   in the optional field VCChunkSize of VC Caps) */

	thislength = MIN(s_remaining(s), vc_chunk_size);

	flags = 0;
	if (length == s_remaining(s))
	{
		flags |= CHANNEL_FLAG_FIRST;
	}
	if (s_remaining(s) == thislength)
	{
		flags |= CHANNEL_FLAG_LAST;
	}
	if (channel->flags & CHANNEL_OPTION_SHOW_PROTOCOL)
	{
		flags |= CHANNEL_FLAG_SHOW_PROTOCOL;
	}

	logger(Protocol, Debug, "channel_send_chunk(), sending %d bytes with flags 0x%x",
	       thislength, flags);

	/* first fragment sent in-place */
	inplace = False;
	if ((flags & (CHANNEL_FLAG_FIRST|CHANNEL_FLAG_LAST)) ==
	    (CHANNEL_FLAG_FIRST|CHANNEL_FLAG_LAST))
	{
		inplace = True;
	}

	if (inplace)
	{
		s_pop_layer(s, channel_hdr);
		chunk = s;
	}
	else
	{
		chunk = sec_init(g_encryption ? SEC_ENCRYPT : 0, thislength + 8);
	}

	out_uint32_le(chunk, length);
	out_uint32_le(chunk, flags);
	if (!inplace)
	{
		out_uint8stream(chunk, s, thislength);
		s_mark_end(chunk);
	}
	sec_send_to_channel(chunk, g_encryption ? SEC_ENCRYPT : 0, channel->mcs_id);

	/* Sending modifies the current offset, so make it is marked as
	   fully completed. */
	if (inplace)
	{
		in_uint8s(s, s_remaining(s));
	}

	if (!inplace)
	{
		s_free(chunk);
	}
}

void
channel_send(STREAM s, VCHANNEL * channel)
{
	uint32 length;

#ifdef WITH_SCARD
	scard_lock(SCARD_LOCK_CHANNEL);
#endif

	s_pop_layer(s, channel_hdr);
	in_uint8s(s, 8);
	length = s_remaining(s);

	logger(Protocol, Debug, "channel_send(), channel = %d, length = %d", channel->mcs_id,
	       length);

	while (!s_check_end(s))
	{
		channel_send_chunk(s, channel, length);
	}

#ifdef WITH_SCARD
	scard_unlock(SCARD_LOCK_CHANNEL);
#endif
}

void
channel_process(STREAM s, uint16 mcs_channel)
{
	uint32 length, flags;
	uint32 thislength;
	VCHANNEL *channel = NULL;
	unsigned int i;
	STREAM in;

	for (i = 0; i < g_num_channels; i++)
	{
		channel = &g_channels[i];
		if (channel->mcs_id == mcs_channel)
			break;
	}

	if (i >= g_num_channels)
		return;

	in_uint32_le(s, length);
	in_uint32_le(s, flags);
	if ((flags & CHANNEL_FLAG_FIRST) && (flags & CHANNEL_FLAG_LAST))
	{
		/* single fragment - pass straight up */
		channel->process(s);
	}
	else
	{
		/* add fragment to defragmentation buffer */
		in = &channel->in;
		if (flags & CHANNEL_FLAG_FIRST)
		{
			s_realloc(in, length);
			s_reset(in);
		}

		thislength = s_remaining(s);
		out_uint8stream(in, s, thislength);

		if (flags & CHANNEL_FLAG_LAST)
		{
			s_mark_end(in);
			s_seek(in, 0);
			channel->process(in);
		}
	}
}
