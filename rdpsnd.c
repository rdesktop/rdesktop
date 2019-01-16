/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions
   Copyright 2006-2010 Pierre Ossman <ossman@cendio.se> for Cendio AB
   Copyright 2009-2011 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright 2017 Henrik Andersson <hean01@cendio.se> for Cendio AB
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 2003-2008
   Copyright (C) GuoJunBo <guojunbo@ict.ac.cn> 2003
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

#include <assert.h>

#include "rdesktop.h"
#include "rdpsnd.h"
#include "rdpsnd_dsp.h"

#define SNDC_CLOSE		0x01
#define SNDC_WAVE		0x02
#define SNDC_SETVOLUME		0x03
#define SNDC_SETPITCH		0x04
#define SNDC_WAVECONFIRM	0x05
#define SNDC_TRAINING		0x06
#define SNDC_FORMATS		0x07
#define SNDC_CRYPTKEY		0x08
#define SNDC_WAVEENCRYPT	0x09
#define SNDC_UDPWAVE		0x0A
#define SNDC_UDPWAVELAST	0x0B
#define SNDC_QUALITYMODE	0x0C
#define SNDC_WAVE2		0x0D

#define MAX_FORMATS		10
#define MAX_QUEUE		50

extern RD_BOOL g_rdpsnd;

static VCHANNEL *rdpsnd_channel;
static VCHANNEL *rdpsnddbg_channel;
static struct audio_driver *drivers = NULL;
struct audio_driver *current_driver = NULL;

static RD_BOOL rdpsnd_negotiated;

static RD_BOOL device_open;

static RD_WAVEFORMATEX formats[MAX_FORMATS];
static unsigned int format_count;
static unsigned int current_format;

unsigned int queue_hi, queue_lo, queue_pending;
struct audio_packet packet_queue[MAX_QUEUE];

static uint8 packet_opcode;
static struct stream packet;

void (*wave_out_play) (void);

static void rdpsnd_queue_write(STREAM s, uint16 tick, uint8 index);
static void rdpsnd_queue_init(void);
static void rdpsnd_queue_clear(void);
static void rdpsnd_queue_complete_pending(void);
static long rdpsnd_queue_next_completion(void);

static STREAM
rdpsnd_init_packet(uint8 type, uint16 size)
{
	STREAM s;

	s = channel_init(rdpsnd_channel, size + 4);
	out_uint8(s, type);
	out_uint8(s, 0);	/* protocol-mandated padding */
	out_uint16_le(s, size);
	return s;
}

static void
rdpsnd_send(STREAM s)
{
	channel_send(s, rdpsnd_channel);
}

static void
rdpsnd_send_waveconfirm(uint16 tick, uint8 packet_index)
{
	STREAM s;

	s = rdpsnd_init_packet(SNDC_WAVECONFIRM, 4);
	out_uint16_le(s, tick);
	out_uint8(s, packet_index);
	out_uint8(s, 0);
	s_mark_end(s);
	rdpsnd_send(s);

	logger(Sound, Debug, "rdpsnd_send_waveconfirm(), tick=%u, index=%u",
	       (unsigned) tick, (unsigned) packet_index);
}

void
rdpsnd_record(const void *data, unsigned int size)
{
	UNUSED(data);
	UNUSED(size);
	/* TODO: Send audio over RDP */
}

static RD_BOOL
rdpsnd_auto_select(void)
{
	static RD_BOOL failed = False;

	if (!failed)
	{
		current_driver = drivers;
		while (current_driver != NULL)
		{
			logger(Sound, Debug, "rdpsnd_auto_select(), trying driver '%s'",
			       current_driver->name);
			if (current_driver->wave_out_open())
			{
				logger(Sound, Verbose, "rdpsnd_auto_select(), using driver '%s'",
				       current_driver->name);
				current_driver->wave_out_close();
				return True;
			}
			current_driver = current_driver->next;
		}

		logger(Sound, Debug, "no working audio-driver found");
		failed = True;
		current_driver = NULL;
	}

	return False;
}

static void
rdpsnd_process_negotiate(STREAM in)
{
	uint16 in_format_count, i;
	uint8 pad;
	uint16 version;
	RD_WAVEFORMATEX *format;
	STREAM out;
	RD_BOOL device_available = False;
	int readcnt;
	int discardcnt;

	in_uint8s(in, 14);	/* initial bytes not valid from server */
	in_uint16_le(in, in_format_count);
	in_uint8(in, pad);
	in_uint16_le(in, version);
	in_uint8s(in, 1);	/* padding */

	logger(Sound, Debug,
	       "rdpsnd_process_negotiate(), formats = %d, pad = 0x%02x, version = 0x%x",
	       (int) in_format_count, (unsigned) pad, (unsigned) version);

	if (rdpsnd_negotiated)
	{
		/* Do a complete reset of the sound state */
		rdpsnd_reset_state();
	}

	if (!current_driver && g_rdpsnd)
		device_available = rdpsnd_auto_select();

	if (current_driver && !device_available && current_driver->wave_out_open())
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
				logger(Sound, Debug,
				       "rdpsnd_process_negotiate(), cbSize too large for buffer: %d",
				       format->cbSize);
				readcnt = MAX_CBSIZE;
				discardcnt = format->cbSize - MAX_CBSIZE;
			}
			in_uint8a(in, format->cb, readcnt);
			in_uint8s(in, discardcnt);

			if (current_driver && current_driver->wave_out_format_supported(format))
			{
				format_count++;
				if (format_count == MAX_FORMATS)
					break;
			}
		}
	}

	out = rdpsnd_init_packet(SNDC_FORMATS, 20 + 18 * format_count);

	uint32 flags = TSSNDCAPS_VOLUME;

	/* if sound is enabled, set snd caps to alive to enable
	   transmission of audio from server */
	if (g_rdpsnd)
	{
		flags |= TSSNDCAPS_ALIVE;
	}
	out_uint32_le(out, flags);	/* TSSNDCAPS flags */

	out_uint32(out, 0xffffffff);	/* volume */
	out_uint32(out, 0);	/* pitch */
	out_uint16(out, 0);	/* UDP port */

	out_uint16_le(out, format_count);
	out_uint8(out, 0);	/* padding */
	out_uint16_le(out, 2);	/* version */
	out_uint8(out, 0);	/* padding */

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

	logger(Sound, Debug, "rdpsnd_process_negotiate(), %d formats available",
	       (int) format_count);

	rdpsnd_send(out);

	rdpsnd_negotiated = True;
}

static void
rdpsnd_process_training(STREAM in)
{
	uint16 tick;
	uint16 packsize;
	STREAM out;
	struct stream packet = *in;

	if (!s_check_rem(in, 4))
	{
		rdp_protocol_error("rdpsnd_process_training(), consume of training data from stream would overrun", &packet);
	}

	in_uint16_le(in, tick);
	in_uint16_le(in, packsize);

	logger(Sound, Debug, "rdpsnd_process_training(), tick=0x%04x", (unsigned) tick);

	out = rdpsnd_init_packet(SNDC_TRAINING, 4);
	out_uint16_le(out, tick);
	out_uint16_le(out, packsize);
	s_mark_end(out);
	rdpsnd_send(out);
}

static void
rdpsnd_process_packet(uint8 opcode, STREAM s)
{
	uint16 vol_left, vol_right;
	static uint16 tick, format;
	static uint8 packet_index;

	switch (opcode)
	{
		case SNDC_WAVE:
			in_uint16_le(s, tick);
			in_uint16_le(s, format);
			in_uint8(s, packet_index);
			in_uint8s(s, 3);
			logger(Sound, Debug,
			       "rdpsnd_process_packet(), RDPSND_WRITE(tick: %u, format: %u, index: %u, data: %u bytes)\n",
			       (unsigned) tick, (unsigned) format, (unsigned) packet_index,
			       (unsigned) s->size - 8);

			if (format >= MAX_FORMATS)
			{
				logger(Sound, Error,
				       "rdpsnd_process_packet(), invalid format index");
				break;
			}

			if (!device_open || (format != current_format))
			{
				/*
				 * If we haven't selected a device by now, then either
				 * we've failed to find a working device, or the server
				 * is sending bogus SNDC_WAVE.
				 */
				if (!current_driver)
				{
					rdpsnd_send_waveconfirm(tick, packet_index);
					break;
				}
				if (!device_open && !current_driver->wave_out_open())
				{
					rdpsnd_send_waveconfirm(tick, packet_index);
					break;
				}
				if (!current_driver->wave_out_set_format(&formats[format]))
				{
					rdpsnd_send_waveconfirm(tick, packet_index);
					current_driver->wave_out_close();
					device_open = False;
					break;
				}
				device_open = True;
				current_format = format;
			}

			rdpsnd_queue_write(rdpsnd_dsp_process
					   (s->p, s->end - s->p, current_driver,
					    &formats[current_format]), tick, packet_index);
			return;
			break;
		case SNDC_CLOSE:
			logger(Sound, Debug, "rdpsnd_process_packet(), SNDC_CLOSE()");
			if (device_open)
				current_driver->wave_out_close();
			device_open = False;
			break;
		case SNDC_FORMATS:
			rdpsnd_process_negotiate(s);
			break;
		case SNDC_TRAINING:
			rdpsnd_process_training(s);
			break;
		case SNDC_SETVOLUME:
			in_uint16_le(s, vol_left);
			in_uint16_le(s, vol_right);
			logger(Sound, Debug,
			       "rdpsnd_process_packet(), SNDC_SETVOLUME(left: 0x%04x (%u %%), right: 0x%04x (%u %%))",
			       (unsigned) vol_left, (unsigned) vol_left / 655, (unsigned) vol_right,
			       (unsigned) vol_right / 655);
			if (device_open)
				current_driver->wave_out_volume(vol_left, vol_right);
			break;
		default:
			logger(Sound, Warning, "rdpsnd_process_packet(), Unhandled opcode 0x%x",
			       opcode);
			break;
	}
}

static void
rdpsnd_process(STREAM s)
{
	uint16 len;

	while (!s_check_end(s))
	{
		/* New packet */
		if (packet.size == 0)
		{
			if ((s->end - s->p) < 4)
			{
				logger(Sound, Error,
				       "rdpsnd_process(), split at packet header, things will go south from here...");
				return;
			}
			in_uint8(s, packet_opcode);
			in_uint8s(s, 1);	/* Padding */
			in_uint16_le(s, len);

			logger(Sound, Debug, "rdpsnd_process(), Opcode = 0x%x Length= %d",
			       (int) packet_opcode, (int) len);

			packet.p = packet.data;
			packet.end = packet.data + len;
			packet.size = len;
		}
		else
		{
			len = MIN(s->end - s->p, packet.end - packet.p);

			/* Microsoft's server is so broken it's not even funny... */
			if (packet_opcode == SNDC_WAVE)
			{
				if ((packet.p - packet.data) < 12)
					len = MIN(len, 12 - (packet.p - packet.data));
				else if ((packet.p - packet.data) == 12)
				{
					logger(Sound, Debug,
					       "rdpsnd_process(), eating 4 bytes of %d bytes...",
					       len);
					in_uint8s(s, 4);
					len -= 4;
				}
			}

			in_uint8a(s, packet.p, len);
			packet.p += len;
		}

		/* Packet fully assembled */
		if (packet.p == packet.end)
		{
			packet.p = packet.data;
			rdpsnd_process_packet(packet_opcode, &packet);
			packet.size = 0;
		}
	}
}

static RD_BOOL
rdpsnddbg_line_handler(const char *line, void *data)
{
	UNUSED(data);
	logger(Sound, Debug, "rdpsnddbg_line_handler(), \"%s\"", line);
	return True;
}

static void
rdpsnddbg_process(STREAM s)
{
	unsigned int pkglen;
	static char *rest = NULL;
	char *buf;

	if (!s_check(s))
	{
		rdp_protocol_error("rdpsnddbg_process(), stream is in unstable state", s);
	}

	pkglen = s->end - s->p;
	/* str_handle_lines requires null terminated strings */
	buf = (char *) xmalloc(pkglen + 1);
	STRNCPY(buf, (char *) s->p, pkglen + 1);

	str_handle_lines(buf, &rest, rdpsnddbg_line_handler, NULL);

	xfree(buf);
}

static void
rdpsnd_register_drivers(char *options)
{
	struct audio_driver **reg;

	/* The order of registrations define the probe-order
	   when opening the device for the first time */
	reg = &drivers;
#if defined(RDPSND_PULSE)
	*reg = pulse_register(options);
	assert(*reg);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_ALSA)
	*reg = alsa_register(options);
	assert(*reg);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_SUN)
	*reg = sun_register(options);
	assert(*reg);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_OSS)
	*reg = oss_register(options);
	assert(*reg);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_SGI)
	*reg = sgi_register(options);
	assert(*reg);
	reg = &((*reg)->next);
#endif
#if defined(RDPSND_LIBAO)
	*reg = libao_register(options);
	assert(*reg);
	reg = &((*reg)->next);
#endif
	*reg = NULL;
}

RD_BOOL
rdpsnd_init(char *optarg)
{
	struct audio_driver *pos;
	char *driver = NULL, *options = NULL;

	drivers = NULL;

	packet.data = (uint8 *) xmalloc(65536);
	packet.p = packet.end = packet.data;
	packet.size = 0;

	rdpsnd_channel =
		channel_register("rdpsnd", CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP,
				 rdpsnd_process);

	rdpsnddbg_channel =
		channel_register("snddbg", CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP,
				 rdpsnddbg_process);

	if ((rdpsnd_channel == NULL) || (rdpsnddbg_channel == NULL))
	{
		logger(Sound, Error,
		       "rdpsnd_init(), failed to register rdpsnd / snddbg virtual channels");
		return False;
	}

	rdpsnd_queue_init();

	if (optarg != NULL && strlen(optarg) > 0)
	{
		driver = options = optarg;

		while (*options != '\0' && *options != ':')
			options++;

		if (*options == ':')
		{
			*options = '\0';
			options++;
		}

		if (*options == '\0')
			options = NULL;
	}

	rdpsnd_register_drivers(options);

	if (!driver)
		return True;

	pos = drivers;
	while (pos != NULL)
	{
		if (!strcmp(pos->name, driver))
		{
			logger(Sound, Debug, "rdpsnd_init(), using driver '%s'", pos->name);
			current_driver = pos;
			return True;
		}
		pos = pos->next;
	}
	return False;
}

void
rdpsnd_reset_state(void)
{
	if (device_open)
		current_driver->wave_out_close();
	device_open = False;
	rdpsnd_queue_clear();
	rdpsnd_negotiated = False;
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

void
rdpsnd_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv)
{
	long next_pending;

	if (device_open)
		current_driver->add_fds(n, rfds, wfds, tv);

	next_pending = rdpsnd_queue_next_completion();
	if (next_pending >= 0)
	{
		long cur_timeout;

		cur_timeout = tv->tv_sec * 1000000 + tv->tv_usec;
		if (cur_timeout > next_pending)
		{
			tv->tv_sec = next_pending / 1000000;
			tv->tv_usec = next_pending % 1000000;
		}
	}
}

void
rdpsnd_check_fds(fd_set * rfds, fd_set * wfds)
{
	rdpsnd_queue_complete_pending();

	if (device_open)
		current_driver->check_fds(rfds, wfds);
}

static void
rdpsnd_queue_write(STREAM s, uint16 tick, uint8 index)
{
	struct audio_packet *packet = &packet_queue[queue_hi];
	unsigned int next_hi = (queue_hi + 1) % MAX_QUEUE;

	if (next_hi == queue_pending)
	{
		logger(Sound, Error, "rdpsnd_queue_write(), no space to queue audio packet");
		return;
	}

	queue_hi = next_hi;

	packet->s = *s;
	packet->tick = tick;
	packet->index = index;

	gettimeofday(&packet->arrive_tv, NULL);
}

struct audio_packet *
rdpsnd_queue_current_packet(void)
{
	return &packet_queue[queue_lo];
}

RD_BOOL
rdpsnd_queue_empty(void)
{
	return (queue_lo == queue_hi);
}

static void
rdpsnd_queue_init(void)
{
	queue_pending = queue_lo = queue_hi = 0;
}

static void
rdpsnd_queue_clear(void)
{
	struct audio_packet *packet;

	/* Go through everything, not just the pending packets */
	while (queue_pending != queue_hi)
	{
		packet = &packet_queue[queue_pending];
		xfree(packet->s.data);
		queue_pending = (queue_pending + 1) % MAX_QUEUE;
	}

	/* Reset everything back to the initial state */
	queue_pending = queue_lo = queue_hi = 0;
}

void
rdpsnd_queue_next(unsigned long completed_in_us)
{
	struct audio_packet *packet;

	assert(!rdpsnd_queue_empty());

	packet = &packet_queue[queue_lo];

	gettimeofday(&packet->completion_tv, NULL);

	packet->completion_tv.tv_usec += completed_in_us;
	packet->completion_tv.tv_sec += packet->completion_tv.tv_usec / 1000000;
	packet->completion_tv.tv_usec %= 1000000;

	queue_lo = (queue_lo + 1) % MAX_QUEUE;

	rdpsnd_queue_complete_pending();
}

int
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

static void
rdpsnd_queue_complete_pending(void)
{
	struct timeval now;
	long elapsed;
	struct audio_packet *packet;

	gettimeofday(&now, NULL);

	while (queue_pending != queue_lo)
	{
		packet = &packet_queue[queue_pending];

		if (now.tv_sec < packet->completion_tv.tv_sec)
			break;

		if ((now.tv_sec == packet->completion_tv.tv_sec) &&
		    (now.tv_usec < packet->completion_tv.tv_usec))
			break;

		elapsed = (packet->completion_tv.tv_sec - packet->arrive_tv.tv_sec) * 1000000 +
			(packet->completion_tv.tv_usec - packet->arrive_tv.tv_usec);
		elapsed /= 1000;

		xfree(packet->s.data);
		rdpsnd_send_waveconfirm((packet->tick + elapsed) % 65536, packet->index);
		queue_pending = (queue_pending + 1) % MAX_QUEUE;
	}
}

static long
rdpsnd_queue_next_completion(void)
{
	struct audio_packet *packet;
	long remaining;
	struct timeval now;

	if (queue_pending == queue_lo)
		return -1;

	gettimeofday(&now, NULL);

	packet = &packet_queue[queue_pending];

	remaining = (packet->completion_tv.tv_sec - now.tv_sec) * 1000000 +
		(packet->completion_tv.tv_usec - now.tv_usec);

	if (remaining < 0)
		return 0;

	return remaining;
}
