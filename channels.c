/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - Channel register
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

#include "rdesktop.h"

static uint16 num_channels;
static rdp5_channel *channels[MAX_RDP5_CHANNELS];

uint16
get_num_channels(void)
{
	return num_channels;
}

void
register_channel(char *name, uint32 flags, void (*callback) (STREAM, uint16))
{
	if (num_channels > MAX_RDP5_CHANNELS)
	{
		error("Maximum number of RDP5 channels reached. Redefine MAX_RDP5_CHANNELS in constants.h and recompile!\n!");
	}
	num_channels++;
	channels[num_channels - 1] = xrealloc(channels[num_channels - 1],
					      sizeof(rdp5_channel) * num_channels);
	channels[num_channels - 1]->channelno = MCS_GLOBAL_CHANNEL + num_channels;
	strcpy(channels[num_channels - 1]->name, name);
	channels[num_channels - 1]->channelflags = flags;
	channels[num_channels - 1]->channelcallback = callback;
}

rdp5_channel *
find_channel_by_channelno(uint16 channelno)
{
	if (channelno > MCS_GLOBAL_CHANNEL + num_channels)
	{
		warning("Channel %d not defined. Highest channel defined is %d\n",
			channelno, MCS_GLOBAL_CHANNEL + num_channels);
		return NULL;
	}
	else
	{
		return channels[channelno - MCS_GLOBAL_CHANNEL - 1];
	}
}

rdp5_channel *
find_channel_by_num(uint16 num)
{
	if (num > num_channels)
	{
		error("There are only %d channels defined, channel %d doesn't exist\n",
		      num_channels, num);
	}
	else
	{
		return channels[num];
	}
	return NULL;		// Shut the compiler up
}



void
dummy_callback(STREAM s, uint16 channelno)
{
	warning("Server is sending information on our dummy channel (%d). Why?\n", channelno);
}

void
channels_init(void)
{
	DEBUG_RDP5(("channels_init\n"));
	register_channel("dummych", 0xc0a0, dummy_callback);
	register_channel("cliprdr", 0xc0a0, cliprdr_callback);
}
