/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions
   Copyright (C) Matthew Chapman 2003
   Copyright (C) GuoJunBo guojunbo@ict.ac.cn 2003

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
#include "rdpsnd.h"

#define RDPSND_CLOSE		1
#define RDPSND_WRITE		2
#define RDPSND_SET_VOLUME	3
#define RDPSND_UNKNOWN4		4
#define RDPSND_COMPLETION	5
#define RDPSND_SERVERTICK	6
#define RDPSND_NEGOTIATE	7

#define MAX_FORMATS		10
#define MAX_QUEUE		10

BOOL g_dsp_busy = False;
int g_dsp_fd;

static VCHANNEL *rdpsnd_channel;
static struct audio_driver *drivers = NULL;
static struct audio_driver *current_driver = NULL;

static BOOL device_open;
static WAVEFORMATEX formats[MAX_FORMATS];
static unsigned int format_count;
static unsigned int current_format;
static unsigned int queue_hi, queue_lo;
static struct audio_packet packet_queue[MAX_QUEUE];

void (*wave_out_play) (void);

static STREAM
rdpsnd_init_packet(uint16 type, uint16 size)
{
	STREAM s;

	s = channel_init(rdpsnd_channel, size + 4);
	out_uint16_le(s, type);
	out_uint16_le(s, size);
	return s;
}

static void
rdpsnd_send(STREAM s)
{
#ifdef RDPSND_DEBUG
	printf("RDPSND send:\n");
	hexdump(s->channel_hdr + 8, s->end - s->channel_hdr - 8);
#endif

	channel_send(s, rdpsnd_channel);
}

void
rdpsnd_send_completion(uint16 tick, uint8 packet_index)
{
	STREAM s;

	s = rdpsnd_init_packet(RDPSND_COMPLETION, 4);
	out_uint16_le(s, tick);
	out_uint8(s, packet_index);
	out_uint8(s, 0);
	s_mark_end(s);
	rdpsnd_send(s);
}

static void
rdpsnd_process_negotiate(STREAM in)
{
	unsigned int in_format_count, i;
	WAVEFORMATEX *format;
	STREAM out;
	BOOL device_available = False;
	int readcnt;
	int discardcnt;

	in_uint8s(in, 14);	/* flags, volume, pitch, UDP port */
	in_uint16_le(in, in_format_count);
	in_uint8s(in, 4);	/* pad, status, pad */

	if (current_driver->wave_out_open())
	{
		current_driver->wave_out_close();
		device_available = True;
	}

	format_count = 0;
	if (s_check_rem(in, 18 * in_format_count))
	{
		for (i = 0; i < in_format_count; i++)
		{
			format = &formats[format_count];
			in_uint16_le(in, format->wFormatTag);
			in_uint16_le(in, format->nChannels);
			in_uint32_le(in, format->nSamplesPerSec);
			in_uint32_le(in, format->nAvgBytesPerSec);
			in_uint16_le(in, format->nBlockAlign);
			in_uint16_le(in, format->wBitsPerSample);
			in_uint16_le(in, format->cbSize);

			/* read in the buffer of unknown use */
			readcnt = format->cbSize;
			discardcnt = 0;
			if (format->cbSize > MAX_CBSIZE)
			{
				fprintf(stderr, "cbSize too large for buffer: %d\n",
					format->cbSize);
				readcnt = MAX_CBSIZE;
				discardcnt = format->cbSize - MAX_CBSIZE;
			}
			in_uint8a(in, format->cb, readcnt);
			in_uint8s(in, discardcnt);

			if (device_available && current_driver->wave_out_format_supported(format))
			{
				format_count++;
				if (format_count == MAX_FORMATS)
					break;
			}
		}
	}

	out = rdpsnd_init_packet(RDPSND_NEGOTIATE | 0x200, 20 + 18 * format_count);
	out_uint32_le(out, 3);	/* flags */
	out_uint32(out, 0xffffffff);	/* volume */
	out_uint32(out, 0);	/* pitch */
	out_uint16(out, 0);	/* UDP port */

	out_uint16_le(out, format_count);
	out_uint8(out, 0x95);	/* pad? */
	out_uint16_le(out, 2);	/* status */
	out_uint8(out, 0x77);	/* pad? */

	for (i = 0; i < format_count; i++)
	{
		format = &formats[i];
		out_uint16_le(out, format->wFormatTag);
		out_uint16_le(out, format->nChannels);
		out_uint32_le(out, format->nSamplesPerSec);
		out_uint32_le(out, format->nAvgBytesPerSec);
		out_uint16_le(out, format->nBlockAlign);
		out_uint16_le(out, format->wBitsPerSample);
		out_uint16(out, 0);	/* cbSize */
	}

	s_mark_end(out);
	rdpsnd_send(out);
}

static void
rdpsnd_process_servertick(STREAM in)
{
	uint16 tick1, tick2;
	STREAM out;

	/* in_uint8s(in, 4); unknown */
	in_uint16_le(in, tick1);
	in_uint16_le(in, tick2);

	out = rdpsnd_init_packet(RDPSND_SERVERTICK | 0x2300, 4);
	out_uint16_le(out, tick1);
	out_uint16_le(out, tick2);
	s_mark_end(out);
	rdpsnd_send(out);
}

static void
rdpsnd_process(STREAM s)
{
	uint8 type;
	uint16 datalen;
	uint32 volume;
	static uint16 tick, format;
	static uint8 packet_index;
	static BOOL awaiting_data_packet;

#ifdef RDPSND_DEBUG
	printf("RDPSND recv:\n");
	hexdump(s->p, s->end - s->p);
#endif

	if (awaiting_data_packet)
	{
		if (format >= MAX_FORMATS)
		{
			error("RDPSND: Invalid format index\n");
			return;
		}

		if (!device_open || (format != current_format))
		{
			if (!device_open && !current_driver->wave_out_open())
			{
				rdpsnd_send_completion(tick, packet_index);
				return;
			}
			if (!current_driver->wave_out_set_format(&formats[format]))
			{
				rdpsnd_send_completion(tick, packet_index);
				current_driver->wave_out_close();
				device_open = False;
				return;
			}
			device_open = True;
			current_format = format;
		}

		current_driver->wave_out_write(s, tick, packet_index);
		awaiting_data_packet = False;
		return;
	}

	in_uint8(s, type);
	in_uint8s(s, 1);	/* unknown? */
	in_uint16_le(s, datalen);

	switch (type)
	{
		case RDPSND_WRITE:
			in_uint16_le(s, tick);
			in_uint16_le(s, format);
			in_uint8(s, packet_index);
			awaiting_data_packet = True;
			break;
		case RDPSND_CLOSE:
			current_driver->wave_out_close();
			device_open = False;
			break;
		case RDPSND_NEGOTIATE:
			rdpsnd_process_negotiate(s);
			break;
		case RDPSND_SERVERTICK:
			rdpsnd_process_servertick(s);
			break;
		case RDPSND_SET_VOLUME:
			in_uint32(s, volume);
			if (device_open)
			{
				current_driver->wave_out_volume((volume & 0xffff),
								(volume & 0xffff0000) >> 16);
			}
			break;
		default:
			unimpl("RDPSND packet type %d\n", type);
			break;
	}
}

BOOL
rdpsnd_init(void)
{
	rdpsnd_channel =
		channel_register("rdpsnd", CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP,
				 rdpsnd_process);

	return (rdpsnd_channel != NULL);
}

BOOL
rdpsnd_auto_open(void)
{
	current_driver = drivers;
	while (current_driver != NULL)
	{
		DEBUG(("trying %s...\n", current_driver->name));
		if (current_driver->wave_out_open())
		{
			DEBUG(("selected %s\n", current_driver->name));
			return True;
		}
		g_dsp_fd = 0;
		current_driver = current_driver->next;
	}

	warning("no working audio-driver found\n");

	return False;
}

void
rdpsnd_register_drivers(char *options)
{
	struct audio_driver **reg;

	/* The order of registrations define the probe-order
	   when opening the device for the first time */
	reg = &drivers;
#if defined(RDPSND_ALSA)
	*reg = alsa_register(options);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_OSS)
	*reg = oss_register(options);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_SUN)
	*reg = sun_register(options);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_SGI)
	*reg = sgi_register(options);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_LIBAO)
	*reg = libao_register(options);
	reg = &((*reg)->next);
#endif
}

BOOL
rdpsnd_select_driver(char *driver, char *options)
{
	static struct audio_driver auto_driver;
	struct audio_driver *pos;

	drivers = NULL;
	rdpsnd_register_drivers(options);

	if (!driver)
	{
		auto_driver.wave_out_open = &rdpsnd_auto_open;
		current_driver = &auto_driver;
		return True;
	}

	pos = drivers;
	while (pos != NULL)
	{
		if (!strcmp(pos->name, driver))
		{
			DEBUG(("selected %s\n", pos->name));
			current_driver = pos;
			return True;
		}
		pos = pos->next;
	}
	return False;
}

void
rdpsnd_show_help(void)
{
	struct audio_driver *pos;

	rdpsnd_register_drivers(NULL);

	pos = drivers;
	while (pos != NULL)
	{
		fprintf(stderr, "                     %s:\t%s\n", pos->name, pos->description);
		pos = pos->next;
	}
}

inline void
rdpsnd_play(void)
{
	current_driver->wave_out_play();
}

void
rdpsnd_queue_write(STREAM s, uint16 tick, uint8 index)
{
	struct audio_packet *packet = &packet_queue[queue_hi];
	unsigned int next_hi = (queue_hi + 1) % MAX_QUEUE;

	if (next_hi == queue_lo)
	{
		error("No space to queue audio packet\n");
		return;
	}

	queue_hi = next_hi;

	packet->s = *s;
	packet->tick = tick;
	packet->index = index;
	packet->s.p += 4;

	/* we steal the data buffer from s, give it a new one */
	s->data = malloc(s->size);

	if (!g_dsp_busy)
		current_driver->wave_out_play();
}

inline struct audio_packet *
rdpsnd_queue_current_packet(void)
{
	return &packet_queue[queue_lo];
}

inline BOOL
rdpsnd_queue_empty(void)
{
	return (queue_lo == queue_hi);
}

inline void
rdpsnd_queue_init(void)
{
	queue_lo = queue_hi = 0;
}

inline void
rdpsnd_queue_next(void)
{
	free(packet_queue[queue_lo].s.data);
	queue_lo = (queue_lo + 1) % MAX_QUEUE;
}

inline int
rdpsnd_queue_next_tick(void)
{
	if (((queue_lo + 1) % MAX_QUEUE) != queue_hi)
	{
		return packet_queue[(queue_lo + 1) % MAX_QUEUE].tick;
	}
	else
	{
		return (packet_queue[queue_lo].tick + 65535) % 65536;
	}
}
