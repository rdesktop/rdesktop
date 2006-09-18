/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - libao-driver
   Copyright (C) Matthew Chapman 2003
   Copyright (C) GuoJunBo guojunbo@ict.ac.cn 2003
   Copyright (C) Michael Gernoth mike@zerfleddert.de 2005-2006

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
#include "rdpsnd_dsp.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ao/ao.h>
#include <sys/time.h>

#define WAVEOUTBUF	16

static ao_device *o_device = NULL;
static int default_driver;
static int samplerate;
static int audiochannels;
static BOOL reopened;
static short samplewidth;
static char *libao_device = NULL;

BOOL
libao_open(void)
{
	ao_sample_format format;

	ao_initialize();

	if (libao_device)
	{
		default_driver = ao_driver_id(libao_device);
	}
	else
	{
		default_driver = ao_default_driver_id();
	}

	format.bits = 16;
	format.channels = 2;
	audiochannels = 2;
	format.rate = 44100;
	samplerate = 44100;
	format.byte_format = AO_FMT_LITTLE;

	o_device = ao_open_live(default_driver, &format, NULL);
	if (o_device == NULL)
	{
		return False;
	}

	g_dsp_fd = 0;
	rdpsnd_queue_init();

	reopened = True;

	return True;
}

void
libao_close(void)
{
	/* Ack all remaining packets */
	while (!rdpsnd_queue_empty())
	{
		rdpsnd_send_completion(rdpsnd_queue_current_packet()->tick,
				       rdpsnd_queue_current_packet()->index);
		rdpsnd_queue_next();
	}

	if (o_device != NULL)
		ao_close(o_device);

	ao_shutdown();
}

BOOL
libao_format_supported(WAVEFORMATEX * pwfx)
{
	if (pwfx->wFormatTag != WAVE_FORMAT_PCM)
		return False;
	if ((pwfx->nChannels != 1) && (pwfx->nChannels != 2))
		return False;
	if ((pwfx->wBitsPerSample != 8) && (pwfx->wBitsPerSample != 16))
		return False;
	/* The only common denominator between libao output drivers is a sample-rate of
	   44100, we need to upsample 22050 to it */
	if ((pwfx->nSamplesPerSec != 44100) && (pwfx->nSamplesPerSec != 22050))
		return False;

	return True;
}

BOOL
libao_set_format(WAVEFORMATEX * pwfx)
{
	ao_sample_format format;

	format.bits = pwfx->wBitsPerSample;
	format.channels = pwfx->nChannels;
	audiochannels = pwfx->nChannels;
	format.rate = 44100;
	samplerate = pwfx->nSamplesPerSec;
	format.byte_format = AO_FMT_LITTLE;

	samplewidth = pwfx->wBitsPerSample / 8;

	if (o_device != NULL)
		ao_close(o_device);

	o_device = ao_open_live(default_driver, &format, NULL);
	if (o_device == NULL)
	{
		return False;
	}

	reopened = True;

	return True;
}

void
libao_play(void)
{
	struct audio_packet *packet;
	STREAM out;
	char outbuf[WAVEOUTBUF];
	int offset, len, i;
	static long prev_s, prev_us;
	unsigned int duration;
	struct timeval tv;
	int next_tick;

	if (reopened)
	{
		reopened = False;
		gettimeofday(&tv, NULL);
		prev_s = tv.tv_sec;
		prev_us = tv.tv_usec;
	}

	if (rdpsnd_queue_empty())
	{
		g_dsp_busy = 0;
		return;
	}

	packet = rdpsnd_queue_current_packet();
	out = &packet->s;

	next_tick = rdpsnd_queue_next_tick();

	len = 0;

	if (samplerate == 22050)
	{
		/* Resample to 44100 */
		for (i = 0; (i < ((WAVEOUTBUF / 4) * (3 - samplewidth))) && (out->p < out->end);
		     i++)
		{
			/* On a stereo-channel we must make sure that left and right
			   does not get mixed up, so we need to expand the sample-
			   data with channels in mind: 1234 -> 12123434
			   If we have a mono-channel, we can expand the data by simply
			   doubling the sample-data: 1234 -> 11223344 */
			if (audiochannels == 2)
				offset = ((i * 2) - (i & 1)) * samplewidth;
			else
				offset = (i * 2) * samplewidth;

			memcpy(&outbuf[offset], out->p, samplewidth);
			memcpy(&outbuf[audiochannels * samplewidth + offset], out->p, samplewidth);

			out->p += samplewidth;
			len += 2 * samplewidth;
		}
	}
	else
	{
		len = (WAVEOUTBUF > (out->end - out->p)) ? (out->end - out->p) : WAVEOUTBUF;
		memcpy(outbuf, out->p, len);
		out->p += len;
	}

	ao_play(o_device, outbuf, len);

	gettimeofday(&tv, NULL);

	duration = ((tv.tv_sec - prev_s) * 1000000 + (tv.tv_usec - prev_us)) / 1000;

	if (packet->tick > next_tick)
		next_tick += 65536;

	if ((out->p == out->end) || duration > next_tick - packet->tick + 500)
	{
		prev_s = tv.tv_sec;
		prev_us = tv.tv_usec;

		if (abs((next_tick - packet->tick) - duration) > 20)
		{
			DEBUG(("duration: %d, calc: %d, ", duration, next_tick - packet->tick));
			DEBUG(("last: %d, is: %d, should: %d\n", packet->tick,
			       (packet->tick + duration) % 65536, next_tick % 65536));
		}

		rdpsnd_send_completion(((packet->tick + duration) % 65536), packet->index);
		rdpsnd_queue_next();
	}

	g_dsp_busy = 1;
	return;
}

struct audio_driver *
libao_register(char *options)
{
	static struct audio_driver libao_driver;
	static char description[101];

	libao_driver.wave_out_write = rdpsnd_queue_write;
	libao_driver.wave_out_open = libao_open;
	libao_driver.wave_out_close = libao_close;
	libao_driver.wave_out_format_supported = libao_format_supported;
	libao_driver.wave_out_set_format = libao_set_format;
	libao_driver.wave_out_volume = rdpsnd_dsp_softvol_set;
	libao_driver.wave_out_play = libao_play;
	libao_driver.name = xstrdup("libao");
	libao_driver.description = description;
	libao_driver.need_byteswap_on_be = 0;
	libao_driver.next = NULL;

	ao_initialize();
	snprintf(description, 100, "libao output driver, default device: %s",
		 ao_driver_info(ao_default_driver_id())->short_name);
	ao_shutdown();

	if (options)
	{
		libao_device = xstrdup(options);
	}

	return &libao_driver;
}
