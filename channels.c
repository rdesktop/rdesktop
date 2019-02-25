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

#include <unistd.h>
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
channel_register(char *name, uint32 flags, void (*callback) (STREAM,char*))
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

void
channel_send(STREAM s, VCHANNEL * channel)
{
	uint32 length, flags;
	uint32 thislength, remaining;
	uint8 *data;

#ifdef WITH_SCARD
	scard_lock(SCARD_LOCK_CHANNEL);
#endif

	/* first fragment sent in-place */
	s_pop_layer(s, channel_hdr);
	length = s->end - s->p - 8;

	logger(Protocol, Debug, "channel_send(), channel = %d, length = %d", channel->mcs_id,
	       length);

	thislength = MIN(length, vc_chunk_size);
/* Note: In the original clipboard implementation, this number was
   1592, not 1600. However, I don't remember the reason and 1600 seems
   to work so.. This applies only to *this* length, not the length of
   continuation or ending packets. */

	/* Actually, CHANNEL_CHUNK_LENGTH (default value is 1600 bytes) is described
	   in MS-RDPBCGR (s. 2.2.6, s.3.1.5.2.1) and can be set by server only
	   in the optional field VCChunkSize of VC Caps) */

	remaining = length - thislength;
	flags = (remaining == 0) ? CHANNEL_FLAG_FIRST | CHANNEL_FLAG_LAST : CHANNEL_FLAG_FIRST;
	if (channel->flags & CHANNEL_OPTION_SHOW_PROTOCOL)
		flags |= CHANNEL_FLAG_SHOW_PROTOCOL;

	out_uint32_le(s, length);
	out_uint32_le(s, flags);
	data = s->end = s->p + thislength;
	logger(Protocol, Debug, "channel_send(), sending %d bytes with FLAG_FIRST set", thislength);
	sec_send_to_channel(s, g_encryption ? SEC_ENCRYPT : 0, channel->mcs_id);

	/* subsequent segments copied (otherwise would have to generate headers backwards) */
	while (remaining > 0)
	{
		thislength = MIN(remaining, vc_chunk_size);
		remaining -= thislength;
		flags = (remaining == 0) ? CHANNEL_FLAG_LAST : 0;
		if (channel->flags & CHANNEL_OPTION_SHOW_PROTOCOL)
			flags |= CHANNEL_FLAG_SHOW_PROTOCOL;

		logger(Protocol, Debug, "channel_send(), sending %d bytes with flags 0x%x",
		       thislength, flags);

		s = sec_init(g_encryption ? SEC_ENCRYPT : 0, thislength + 8);
		out_uint32_le(s, length);
		out_uint32_le(s, flags);
		out_uint8p(s, data, thislength);
		s_mark_end(s);
		sec_send_to_channel(s, g_encryption ? SEC_ENCRYPT : 0, channel->mcs_id);

		data += thislength;
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
		channel->process(s, channel->name);
	}
	else
	{
		/* add fragment to defragmentation buffer */
		in = &channel->in;
		if (flags & CHANNEL_FLAG_FIRST)
		{
			if (length > in->size)
			{
				in->data = (uint8 *) xrealloc(in->data, length);
				in->size = length;
			}
			in->p = in->data;
		}

		thislength = MIN(s->end - s->p, in->data + in->size - in->p);
		memcpy(in->p, s->p, thislength);
		in->p += thislength;

		if (flags & CHANNEL_FLAG_LAST)
		{
			in->end = in->p;
			in->p = in->data;
			channel->process(in, channel->name);
		}
	}
}

/* Generic callback for delivering data to third party add-ins */
void addin_callback(STREAM s, char *name)
{
	pid_t pid;
	int pipe_read;
	int pipe_write;
	uint32 blocksize;

	/* s->p is the start and s->end is the end plus 1 */
	blocksize = s->end - s->p;

	lookup_addin(name, &pid, &pipe_read, &pipe_write);
	if (!pid)
		perror("Can't locate addin");
	else
	{
		/* Prepend the block with the block size so the
		add-in can identify blocks */
		write(pipe_write, &blocksize, sizeof(uint32));
		write(pipe_write, s->p, blocksize);
	}
}

/* Add the add-in pipes to the set of file descriptors */
void addin_add_fds(int *n, fd_set * rfds)
{
	extern ADDIN_DATA addin_data[];
	extern int addin_count;

	int i;

	for (i = 0; i < addin_count; i++)
	{
		FD_SET(addin_data[i].pipe_read, rfds);
		*n = MAX(*n, addin_data[i].pipe_read);
	}
}

/* Check the add-in pipes for data to write */
void addin_check_fds(fd_set * rfds)
{
	extern ADDIN_DATA addin_data[];
	extern int addin_count;

	int i;
	char buffer[1024];
	ssize_t bytes_read;
	STREAM s;

	for (i = 0; i < addin_count; i++)
	{
		if (FD_ISSET(addin_data[i].pipe_read, rfds))
		{
			bytes_read = read(addin_data[i].pipe_read, buffer, 1024);
			if (bytes_read > 0)
			{
				/* write to appropriate vc */
				s = channel_init(addin_data[i].vchannel, bytes_read);
				memcpy(s->p, buffer, bytes_read);
				s->p += bytes_read;
				s->end = s->p;

				channel_send(s, addin_data[i].vchannel);
			}
		}
	}
}
