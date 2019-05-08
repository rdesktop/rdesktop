/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Dynamic Channel Virtual Channel Extension.
   Copyright 2017 Henrik Andersson <hean01@cendio.com> for Cendio AB
   Copyright 2017 Karl Mikaelsson <derfian@cendio.se> for Cendio AB

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

#define MAX_DVC_CHANNELS 20
#define INVALID_CHANNEL ((uint32)-1)

#define DYNVC_CREATE_REQ		0x01
#define DYNVC_DATA_FIRST		0x02
#define DYNVC_DATA			0x03
#define DYNVC_CLOSE			0x04
#define DYNVC_CAPABILITIES              0x05
#define DYNVC_DATA_FIRST_COMPRESSED	0x06
#define DYNVC_DATA_COMPRESSED		0x07
#define DYNVC_SOFT_SYNC_REQUEST		0x08
#define DYNVC_SOFT_SYNC_RESPONSE	0x09

typedef union dvc_hdr_t
{
	uint8 data;
	struct
	{
		uint8 cbid:2;
		uint8 sp:2;
		uint8 cmd:4;
	} hdr;
} dvc_hdr_t;

typedef struct dvc_channel_t
{
	uint32 hash;
	uint32 channel_id;
	dvc_channel_process_fn handler;
} dvc_channel_t;

static VCHANNEL *dvc_channel;
static dvc_channel_t channels[MAX_DVC_CHANNELS];

static uint32 dvc_in_channelid(STREAM s, dvc_hdr_t hdr);

static RD_BOOL
dvc_channels_exists(const char *name)
{
	int i;
	uint32 hash;
	hash = utils_djb2_hash(name);
	for (i = 0; i < MAX_DVC_CHANNELS; i++)
	{
		if (channels[i].hash == hash)
			return True;
	}

	return False;
}

static const dvc_channel_t *
dvc_channels_get_by_id(uint32 id)
{
	int i;

	for (i = 0; i < MAX_DVC_CHANNELS; i++)
	{
		if (channels[i].channel_id == id)
		{
			return &channels[i];
		}
	}

	return NULL;
}

static uint32
dvc_channels_get_id(const char *name)
{
	int i;
	uint32 hash;
	hash = utils_djb2_hash(name);

	for (i = 0; i < MAX_DVC_CHANNELS; i++)
	{
		if (channels[i].hash == hash)
		{
			return channels[i].channel_id;
		}
	}

	return INVALID_CHANNEL;
}

static RD_BOOL
dvc_channels_remove_by_id(uint32 channelid)
{
	int i;

	for (i = 0; i < MAX_DVC_CHANNELS; i++)
	{
		if (channels[i].channel_id == channelid)
		{
			memset(&channels[i], 0, sizeof(dvc_channel_t));
			return True;
		}
	}
	return False;
}

static RD_BOOL
dvc_channels_add(const char *name, dvc_channel_process_fn handler, uint32 channel_id)
{
	int i;
	uint32 hash;

	if (dvc_channels_exists(name) == True)
	{
		logger(Core, Warning, "dvc_channels_add(), channel with name '%s' already exists",
		       name);
		return False;
	}

	for (i = 0; i < MAX_DVC_CHANNELS; i++)
	{
		if (channels[i].hash == 0)
		{
			hash = utils_djb2_hash(name);
			channels[i].hash = hash;
			channels[i].handler = handler;
			channels[i].channel_id = channel_id;
			logger(Core, Debug,
			       "dvc_channels_add(), Added hash=%x, channel_id=%d, name=%s, handler=%p",
			       hash, channel_id, name, handler);
			return True;
		}
	}

	logger(Core, Warning,
	       "dvc_channels_add(), Failed to add channel, maximum number of channels are being used");
	return False;
}

static int
dvc_channels_set_id(const char *name, uint32 channel_id)
{
	int i;
	uint32 hash;

	hash = utils_djb2_hash(name);

	for (i = 0; i < MAX_DVC_CHANNELS; i++)
	{
		if (channels[i].hash == hash)
		{
			logger(Core, Debug, "dvc_channels_set_id(), name = '%s', channel_id = %d",
			       name, channel_id);
			channels[i].channel_id = channel_id;
			return 0;
		}
	}

	return -1;
}

RD_BOOL
dvc_channels_is_available(const char *name)
{
	int i;
	uint32 hash;
	hash = utils_djb2_hash(name);

	for (i = 0; i < MAX_DVC_CHANNELS; i++)
	{
		if (channels[i].hash == hash)
		{
			return (channels[i].channel_id != INVALID_CHANNEL);
		}
	}

	return False;
}

RD_BOOL
dvc_channels_register(const char *name, dvc_channel_process_fn handler)
{
	return dvc_channels_add(name, handler, INVALID_CHANNEL);
}


static STREAM
dvc_init_packet(dvc_hdr_t hdr, uint32 channelid, size_t length)
{
	STREAM s;

	length += 1;		/* add 1 byte hdr */

	if (channelid != INVALID_CHANNEL)
	{
		if (hdr.hdr.cbid == 0)
			length += 1;
		else if (hdr.hdr.cbid == 1)
			length += 2;
		else if (hdr.hdr.cbid == 2)
			length += 4;
	}

	s = channel_init(dvc_channel, length);
	out_uint8(s, hdr.data);	/* DVC header */

	if (channelid != INVALID_CHANNEL)
	{
		if (hdr.hdr.cbid == 0)
		{
			out_uint8(s, channelid);
		}
		else if (hdr.hdr.cbid == 1)
		{
			out_uint16_le(s, channelid);
		}
		else if (hdr.hdr.cbid == 2)
		{
			out_uint32_le(s, channelid);
		}
	}

	return s;
}

void
dvc_send(const char *name, STREAM s)
{
	STREAM ls;
	dvc_hdr_t hdr;
	uint32 channel_id;

	channel_id = dvc_channels_get_id(name);
	if (channel_id == INVALID_CHANNEL)
	{
		logger(Core, Error, "dvc_send(), Trying to send data on invalid channel '%s'",
		       name);
		return;
	}

	/* FIXME: we assume length is less than 1600 */

	hdr.hdr.cmd = DYNVC_DATA;
	hdr.hdr.cbid = 2;
	hdr.hdr.sp = 0;

	ls = dvc_init_packet(hdr, channel_id, s_length(s));

	out_stream(ls, s);

	s_mark_end(ls);

	channel_send(ls, dvc_channel);
	s_free(ls);
}


static void
dvc_send_capabilities_response()
{
	STREAM s;
	dvc_hdr_t hdr;
	uint16 supportedversion = 0x01;

	hdr.hdr.cbid = 0x00;
	hdr.hdr.sp = 0x00;
	hdr.hdr.cmd = DYNVC_CAPABILITIES;

	logger(Protocol, Debug,
	       "dvc_send_capabilities_response(), offering support for dvc %d", supportedversion);

	s = dvc_init_packet(hdr, -1, 3);
	out_uint8(s, 0x00);	/* pad */
	out_uint16_le(s, supportedversion);	/* version */

	s_mark_end(s);

	channel_send(s, dvc_channel);
	s_free(s);
}

static void
dvc_process_caps_pdu(STREAM s)
{
	uint16 version;

	/* VERSION1 */
	in_uint8s(s, 1);	/* pad */
	in_uint16_le(s, version);	/* version */

	logger(Protocol, Debug, "dvc_process_caps(), server supports dvc %d", version);

	dvc_send_capabilities_response();
}

static void
dvc_send_create_response(RD_BOOL success, dvc_hdr_t hdr, uint32 channelid)
{
	STREAM s;

	logger(Protocol, Debug, "dvc_send_create_response(), %s request to create channelid %d",
	       (success ? "granted" : "denied"), channelid);
	s = dvc_init_packet(hdr, channelid, 4);
	out_uint32_le(s, success ? 0 : -1);
	s_mark_end(s);

	channel_send(s, dvc_channel);
	s_free(s);
}

static void
dvc_process_create_pdu(STREAM s, dvc_hdr_t hdr)
{
	char name[512];
	uint32 channelid;

	channelid = dvc_in_channelid(s, hdr);

	in_ansi_string(s, name, sizeof(name));

	logger(Protocol, Debug, "dvc_process_create(), server requests channelid = %d, name = '%s'",
	       channelid, name);

	if (dvc_channels_exists(name))
	{
		logger(Core, Verbose, "Established dynamic virtual channel '%s'", name);

		dvc_channels_set_id(name, channelid);
		dvc_send_create_response(True, hdr, channelid);
	}
	else
	{
		dvc_send_create_response(False, hdr, channelid);
	}

}

static uint32
dvc_in_channelid(STREAM s, dvc_hdr_t hdr)
{
	uint32 id;

	id = (uint32) - 1;

	switch (hdr.hdr.cbid)
	{
		case 0:
			in_uint8(s, id);
			break;
		case 1:
			in_uint16_le(s, id);
			break;
		case 2:
			in_uint32_le(s, id);
			break;
	}
	return id;
}

static void
dvc_process_data_pdu(STREAM s, dvc_hdr_t hdr)
{
	const dvc_channel_t *ch;
	uint32 channelid;

	channelid = dvc_in_channelid(s, hdr);
	ch = dvc_channels_get_by_id(channelid);
	if (ch == NULL)
	{
		logger(Protocol, Warning,
		       "dvc_process_data(), Received data on unregistered channel %d", channelid);
		return;
	}

	/* dispatch packet to channel handler */
	ch->handler(s);
}

static void
dvc_process_close_pdu(STREAM s, dvc_hdr_t hdr)
{
	uint32 channelid;

	channelid = dvc_in_channelid(s, hdr);
	logger(Protocol, Debug, "dvc_process_close_pdu(), close channel %d", channelid);

	if (!dvc_channels_remove_by_id(channelid))
	{
		logger(Protocol, Warning,
		       "dvc_process_close_pdu(), Received close request for unregistered channel %d",
		       channelid);
		return;
	}
}


static void
dvc_process_pdu(STREAM s)
{
	dvc_hdr_t hdr;

	in_uint8(s, hdr.data);

	switch (hdr.hdr.cmd)
	{
		case DYNVC_CAPABILITIES:
			dvc_process_caps_pdu(s);
			break;
		case DYNVC_CREATE_REQ:
			dvc_process_create_pdu(s, hdr);
			break;

		case DYNVC_DATA:
			dvc_process_data_pdu(s, hdr);
			break;

		case DYNVC_CLOSE:
			dvc_process_close_pdu(s, hdr);
			break;

#if 0				/* Unimplemented */

		case DYNVC_DATA_FIRST:
			break;
		case DYNVC_DATA_FIRST_COMPRESSED:
			break;
		case DYNVC_DATA_COMPRESSED:
			break;
		case DYNVC_SOFT_SYNC_REQUEST:
			break;
		case DYNVC_SOFT_SYNC_RESPONSE:
			break;

#endif

		default:
			logger(Protocol, Warning, "dvc_process_pdu(), Unhandled command type 0x%x",
			       hdr.hdr.cmd);
			break;
	}
}

RD_BOOL
dvc_init()
{
	memset(channels, 0, sizeof(channels));
	dvc_channel = channel_register("drdynvc",
				       CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP,
				       dvc_process_pdu);

	return (dvc_channel != NULL);
}
