/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions
   Copyright 2006-2010 Pierre Ossman <ossman@cendio.se> for Cendio AB
   Copyright 2009-2011 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 2003-2008
   Copyright (C) GuoJunBo <guojunbo@ict.ac.cn> 2003

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

#define RDPSND_CLOSE		1
#define RDPSND_WRITE		2
#define RDPSND_SET_VOLUME	3
#define RDPSND_UNKNOWN4		4
#define RDPSND_COMPLETION	5
#define RDPSND_PING			6
#define RDPSND_NEGOTIATE	7

#define RDPSND_REC_NEGOTIATE	39
#define RDPSND_REC_START		40
#define RDPSND_REC_STOP			41
#define RDPSND_REC_DATA			42
#define RDPSND_REC_SET_VOLUME	43

/* Special flag for RDPSND recording extension,
   not defined in MS specs.

   See doc/rdpsnd-rec.txt for more information.
*/
#define RDPSND_FLAG_RECORD		0x00800000

#define MAX_FORMATS		10
#define MAX_QUEUE		50

extern RD_BOOL g_rdpsnd;

static VCHANNEL *rdpsnd_channel;
static VCHANNEL *rdpsnddbg_channel;
static struct audio_driver *drivers = NULL;
struct audio_driver *current_driver = NULL;

static RD_BOOL rdpsnd_negotiated;
static RD_BOOL rdpsnd_rec_negotiated;

static RD_BOOL device_open;
static RD_BOOL rec_device_open;

static RD_WAVEFORMATEX formats[MAX_FORMATS];
static unsigned int format_count;
static unsigned int current_format;

static RD_WAVEFORMATEX rec_formats[MAX_FORMATS];
static unsigned int rec_format_count;
static unsigned int rec_current_format;

unsigned int queue_hi, queue_lo, queue_pending;
struct audio_packet packet_queue[MAX_QUEUE];

static char record_buffer[8192];
static uint32 record_buffer_size;

static uint8 packet_opcode;
static struct stream packet;

void (*wave_out_play) (void);

static void rdpsnd_queue_write(STREAM s, uint16 tick, uint8 index);
static void rdpsnd_queue_init(void);
static void rdpsnd_queue_clear(void);
static void rdpsnd_queue_complete_pending(void);
static long rdpsnd_queue_next_completion(void);

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
	channel_send(s, rdpsnd_channel);
}

static void
rdpsnd_send_completion(uint16 tick, uint8 packet_index)
{
	STREAM s;

	s = rdpsnd_init_packet(RDPSND_COMPLETION, 4);
	out_uint16_le(s, tick);
	out_uint8(s, packet_index);
	out_uint8(s, 0);
	s_mark_end(s);
	rdpsnd_send(s);

	DEBUG_SOUND(("RDPSND: -> RDPSND_COMPLETION(tick: %u, index: %u)\n",
		     (unsigned) tick, (unsigned) packet_index));
}

static void
rdpsnd_flush_record(void)
{
	STREAM s;
	unsigned int chunk_size;
	char *data;

	if (record_buffer_size == 0)
		return;

	assert(record_buffer_size <= sizeof(record_buffer));

	data = record_buffer;

	/*
	 * Microsoft's RDP server keeps dropping chunks, so we need to
	 * transmit everything inside one channel fragment or we risk
	 * making the rdpsnd server go out of sync with the byte stream.
	 */
	while (record_buffer_size)
	{
		if (record_buffer_size < 1596)
			chunk_size = record_buffer_size;
		else
			chunk_size = 1596;

		s = rdpsnd_init_packet(RDPSND_REC_DATA, chunk_size);
		out_uint8p(s, data, chunk_size);

		s_mark_end(s);
		rdpsnd_send(s);

		data = data + chunk_size;
		record_buffer_size -= chunk_size;

		DEBUG_SOUND(("RDPSND: -> RDPSND_REC_DATA(length: %u)\n", (unsigned) chunk_size));
	}

	record_buffer_size = 0;
}

static void
rdpsnd_clear_record(void)
{
	/*
	 * Silently drop everything we have in the record buffer as
	 * we've somehow gotten a reset in regard to the server.
	 */
	record_buffer_size = 0;
}

void
rdpsnd_record(const void *data, unsigned int size)
{
	uint32 remain, chunk;

	assert(rec_device_open);

	while (size)
	{
		remain = sizeof(record_buffer) - record_buffer_size;

		if (size >= remain)
			chunk = remain;
		else
			chunk = size;

		memcpy(record_buffer + record_buffer_size, data, chunk);

#ifdef B_ENDIAN
		if (current_driver->need_byteswap_on_be)
			rdpsnd_dsp_swapbytes(record_buffer + record_buffer_size,
					     chunk, &rec_formats[rec_current_format]);
#endif

		record_buffer_size += chunk;

		data = (const char *) data + chunk;
		size -= chunk;

		if (record_buffer_size == sizeof(record_buffer))
			rdpsnd_flush_record();
	}
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
			DEBUG(("trying %s...\n", current_driver->name));
			if (current_driver->wave_out_open())
			{
				DEBUG(("selected %s\n", current_driver->name));
				current_driver->wave_out_close();
				return True;
			}
			current_driver = current_driver->next;
		}

		warning("no working audio-driver found\n");
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

	DEBUG_SOUND(("RDPSND: RDPSND_NEGOTIATE(formats: %d, pad: 0x%02x, version: %x)\n",
		     (int) in_format_count, (unsigned) pad, (unsigned) version));

	if (rdpsnd_negotiated)
	{
		error("RDPSND: Extra RDPSND_NEGOTIATE in the middle of a session\n");
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
				fprintf(stderr, "cbSize too large for buffer: %d\n",
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

	out = rdpsnd_init_packet(RDPSND_NEGOTIATE | 0x200, 20 + 18 * format_count);

	uint32 flags = TSSNDCAPS_VOLUME;

	/* if sound is enabled, set snd caps to alive to enable
	   transmision of audio from server */
	if (g_rdpsnd)
	{
		flags |= TSSNDCAPS_ALIVE;
		flags |= RDPSND_FLAG_RECORD;
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

	DEBUG_SOUND(("RDPSND: -> RDPSND_NEGOTIATE(formats: %d)\n", (int) format_count));

	rdpsnd_send(out);

	rdpsnd_negotiated = True;
}

static void
rdpsnd_process_ping(STREAM in)
{
	uint16 tick;
	STREAM out;

	in_uint16_le(in, tick);

	DEBUG_SOUND(("RDPSND: RDPSND_PING(tick: 0x%04x)\n", (unsigned) tick));

	out = rdpsnd_init_packet(RDPSND_PING | 0x2300, 4);
	out_uint16_le(out, tick);
	out_uint16_le(out, 0);
	s_mark_end(out);
	rdpsnd_send(out);

	DEBUG_SOUND(("RDPSND: -> (tick: 0x%04x)\n", (unsigned) tick));
}

static void
rdpsnd_process_rec_negotiate(STREAM in)
{
	uint16 in_format_count, i;
	uint16 version;
	RD_WAVEFORMATEX *format;
	STREAM out;
	RD_BOOL device_available = False;
	int readcnt;
	int discardcnt;

	in_uint8s(in, 8);	/* initial bytes not valid from server */
	in_uint16_le(in, in_format_count);
	in_uint16_le(in, version);

	DEBUG_SOUND(("RDPSND: RDPSND_REC_NEGOTIATE(formats: %d, version: %x)\n",
		     (int) in_format_count, (unsigned) version));

	if (rdpsnd_rec_negotiated)
	{
		error("RDPSND: Extra RDPSND_REC_NEGOTIATE in the middle of a session\n");
		/* Do a complete reset of the sound state */
		rdpsnd_reset_state();
	}

	if (!current_driver)
		device_available = rdpsnd_auto_select();

	if (current_driver && !device_available && current_driver->wave_in_open
	    && current_driver->wave_in_open())
	{
		current_driver->wave_in_close();
		device_available = True;
	}

	rec_format_count = 0;
	if (s_check_rem(in, 18 * in_format_count))
	{
		for (i = 0; i < in_format_count; i++)
		{
			format = &rec_formats[rec_format_count];
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

			if (current_driver && current_driver->wave_in_format_supported
			    && current_driver->wave_in_format_supported(format))
			{
				rec_format_count++;
				if (rec_format_count == MAX_FORMATS)
					break;
			}
		}
	}

	out = rdpsnd_init_packet(RDPSND_REC_NEGOTIATE, 12 + 18 * rec_format_count);
	out_uint32_le(out, 0x00000000);	/* flags */
	out_uint32_le(out, 0xffffffff);	/* volume */
	out_uint16_le(out, rec_format_count);
	out_uint16_le(out, 1);	/* version */

	for (i = 0; i < rec_format_count; i++)
	{
		format = &rec_formats[i];
		out_uint16_le(out, format->wFormatTag);
		out_uint16_le(out, format->nChannels);
		out_uint32_le(out, format->nSamplesPerSec);
		out_uint32_le(out, format->nAvgBytesPerSec);
		out_uint16_le(out, format->nBlockAlign);
		out_uint16_le(out, format->wBitsPerSample);
		out_uint16(out, 0);	/* cbSize */
	}

	s_mark_end(out);

	DEBUG_SOUND(("RDPSND: -> RDPSND_REC_NEGOTIATE(formats: %d)\n", (int) rec_format_count));

	rdpsnd_send(out);

	rdpsnd_rec_negotiated = True;
}

static void
rdpsnd_process_packet(uint8 opcode, STREAM s)
{
	uint16 vol_left, vol_right;
	static uint16 tick, format;
	static uint8 packet_index;

	switch (opcode)
	{
		case RDPSND_WRITE:
			in_uint16_le(s, tick);
			in_uint16_le(s, format);
			in_uint8(s, packet_index);
			in_uint8s(s, 3);
			DEBUG_SOUND(("RDPSND: RDPSND_WRITE(tick: %u, format: %u, index: %u, data: %u bytes)\n", (unsigned) tick, (unsigned) format, (unsigned) packet_index, (unsigned) s->size - 8));

			if (format >= MAX_FORMATS)
			{
				error("RDPSND: Invalid format index\n");
				break;
			}

			if (!device_open || (format != current_format))
			{
				/*
				 * If we haven't selected a device by now, then either
				 * we've failed to find a working device, or the server
				 * is sending bogus RDPSND_WRITE.
				 */
				if (!current_driver)
				{
					rdpsnd_send_completion(tick, packet_index);
					break;
				}
				if (!device_open && !current_driver->wave_out_open())
				{
					rdpsnd_send_completion(tick, packet_index);
					break;
				}
				if (!current_driver->wave_out_set_format(&formats[format]))
				{
					rdpsnd_send_completion(tick, packet_index);
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
		case RDPSND_CLOSE:
			DEBUG_SOUND(("RDPSND: RDPSND_CLOSE()\n"));
			if (device_open)
				current_driver->wave_out_close();
			device_open = False;
			break;
		case RDPSND_NEGOTIATE:
			rdpsnd_process_negotiate(s);
			break;
		case RDPSND_PING:
			rdpsnd_process_ping(s);
			break;
		case RDPSND_SET_VOLUME:
			in_uint16_le(s, vol_left);
			in_uint16_le(s, vol_right);
			DEBUG_SOUND(("RDPSND: RDPSND_VOLUME(left: 0x%04x (%u %%), right: 0x%04x (%u %%))\n", (unsigned) vol_left, (unsigned) vol_left / 655, (unsigned) vol_right, (unsigned) vol_right / 655));
			if (device_open)
				current_driver->wave_out_volume(vol_left, vol_right);
			break;
		case RDPSND_REC_NEGOTIATE:
			rdpsnd_process_rec_negotiate(s);
			break;
		case RDPSND_REC_START:
			in_uint16_le(s, format);
			DEBUG_SOUND(("RDPSND: RDPSND_REC_START(format: %u)\n", (unsigned) format));

			if (format >= MAX_FORMATS)
			{
				error("RDPSND: Invalid format index\n");
				break;
			}

			if (rec_device_open)
			{
				error("RDPSND: Multiple RDPSND_REC_START\n");
				break;
			}

			if (!current_driver->wave_in_open())
				break;

			if (!current_driver->wave_in_set_format(&rec_formats[format]))
			{
				error("RDPSND: Device not accepting format\n");
				current_driver->wave_in_close();
				break;
			}
			rec_current_format = format;
			rec_device_open = True;
			break;
		case RDPSND_REC_STOP:
			DEBUG_SOUND(("RDPSND: RDPSND_REC_STOP()\n"));
			rdpsnd_flush_record();
			if (rec_device_open)
				current_driver->wave_in_close();
			rec_device_open = False;
			break;
		case RDPSND_REC_SET_VOLUME:
			in_uint16_le(s, vol_left);
			in_uint16_le(s, vol_right);
			DEBUG_SOUND(("RDPSND: RDPSND_REC_VOLUME(left: 0x%04x (%u %%), right: 0x%04x (%u %%))\n", (unsigned) vol_left, (unsigned) vol_left / 655, (unsigned) vol_right, (unsigned) vol_right / 655));
			if (rec_device_open)
				current_driver->wave_in_volume(vol_left, vol_right);
			break;
		default:
			unimpl("RDPSND packet type %x\n", opcode);
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
				error("RDPSND: Split at packet header. Things will go south from here...\n");
				return;
			}
			in_uint8(s, packet_opcode);
			in_uint8s(s, 1);	/* Padding */
			in_uint16_le(s, len);

			DEBUG_SOUND(("RDPSND: == Opcode %x Length: %d ==\n",
				     (int) packet_opcode, (int) len));

			packet.p = packet.data;
			packet.end = packet.data + len;
			packet.size = len;
		}
		else
		{
			len = MIN(s->end - s->p, packet.end - packet.p);

			/* Microsoft's server is so broken it's not even funny... */
			if (packet_opcode == RDPSND_WRITE)
			{
				if ((packet.p - packet.data) < 12)
					len = MIN(len, 12 - (packet.p - packet.data));
				else if ((packet.p - packet.data) == 12)
				{
					DEBUG_SOUND(("RDPSND: Eating 4 bytes of %d bytes...\n",
						     len));
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
#ifdef WITH_DEBUG_SOUND
	fprintf(stderr, "SNDDBG: %s\n", line);
#endif
	return True;
}

static void
rdpsnddbg_process(STREAM s)
{
	unsigned int pkglen;
	static char *rest = NULL;
	char *buf;

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
		error("channel_register\n");
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
			DEBUG(("selected %s\n", pos->name));
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

	if (rec_device_open)
		current_driver->wave_in_close();
	rec_device_open = False;
	rdpsnd_clear_record();
	rdpsnd_rec_negotiated = False;
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

	if (device_open || rec_device_open)
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

	if (device_open || rec_device_open)
		current_driver->check_fds(rfds, wfds);
}

static void
rdpsnd_queue_write(STREAM s, uint16 tick, uint8 index)
{
	struct audio_packet *packet = &packet_queue[queue_hi];
	unsigned int next_hi = (queue_hi + 1) % MAX_QUEUE;

	if (next_hi == queue_pending)
	{
		error("No space to queue audio packet\n");
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
		rdpsnd_send_completion((packet->tick + elapsed) % 65536, packet->index);
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
