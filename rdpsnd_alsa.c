/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - alsa-driver
   Copyright (C) Matthew Chapman 2003
   Copyright (C) GuoJunBo guojunbo@ict.ac.cn 2003
   Copyright (C) Michael Gernoth mike@zerfleddert.de 2006

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
#include <alsa/asoundlib.h>
#include <sys/time.h>

#define DEFAULTDEVICE	"default"
#define MAX_FRAMES	32

static snd_pcm_t *pcm_handle = NULL;
static snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
static BOOL reopened;
static short samplewidth;
static int audiochannels;
static char *pcm_name;

BOOL
alsa_open(void)
{
	int err;

	if ((err = snd_pcm_open(&pcm_handle, pcm_name, stream, 0)) < 0)
	{
		error("snd_pcm_open: %s\n", snd_strerror(err));
		return False;
	}

	g_dsp_fd = 0;
	rdpsnd_queue_init();

	reopened = True;

	return True;
}

void
alsa_close(void)
{
	/* Ack all remaining packets */
	while (!rdpsnd_queue_empty())
	{
		rdpsnd_send_completion(rdpsnd_queue_current_packet()->tick,
				       rdpsnd_queue_current_packet()->index);
		rdpsnd_queue_next();
	}

	if (pcm_handle)
	{
		snd_pcm_drop(pcm_handle);
		snd_pcm_close(pcm_handle);
	}
}

BOOL
alsa_format_supported(WAVEFORMATEX * pwfx)
{
#if 0
	int err;
	snd_pcm_hw_params_t *hwparams = NULL;

	if ((err = snd_pcm_hw_params_malloc(&hwparams)) < 0)
	{
		error("snd_pcm_hw_params_malloc: %s\n", snd_strerror(err));
		return False;
	}

	if ((err = snd_pcm_hw_params_any(pcm_handle, hwparams)) < 0)
	{
		error("snd_pcm_hw_params_malloc: %s\n", snd_strerror(err));
		return False;
	}
	snd_pcm_hw_params_free(hwparams);
#endif

	if (pwfx->wFormatTag != WAVE_FORMAT_PCM)
		return False;
	if ((pwfx->nChannels != 1) && (pwfx->nChannels != 2))
		return False;
	if ((pwfx->wBitsPerSample != 8) && (pwfx->wBitsPerSample != 16))
		return False;
	if ((pwfx->nSamplesPerSec != 44100) && (pwfx->nSamplesPerSec != 22050))
		return False;

	return True;
}

BOOL
alsa_set_format(WAVEFORMATEX * pwfx)
{
	snd_pcm_hw_params_t *hwparams = NULL;
	unsigned int rate, exact_rate;
	int err;
	unsigned int buffertime;

	samplewidth = pwfx->wBitsPerSample / 8;

	if ((err = snd_pcm_hw_params_malloc(&hwparams)) < 0)
	{
		error("snd_pcm_hw_params_malloc: %s\n", snd_strerror(err));
		return False;
	}

	if ((err = snd_pcm_hw_params_any(pcm_handle, hwparams)) < 0)
	{
		error("snd_pcm_hw_params_any: %s\n", snd_strerror(err));
		return False;
	}

	if ((err =
	     snd_pcm_hw_params_set_access(pcm_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
	{
		error("snd_pcm_hw_params_set_access: %s\n", snd_strerror(err));
		return False;
	}

	if (pwfx->wBitsPerSample == 16)
	{
		if ((err =
		     snd_pcm_hw_params_set_format(pcm_handle, hwparams, SND_PCM_FORMAT_S16_LE)) < 0)
		{
			error("snd_pcm_hw_params_set_format: %s\n", snd_strerror(err));
			return False;
		}
	}
	else
	{
		if ((err =
		     snd_pcm_hw_params_set_format(pcm_handle, hwparams, SND_PCM_FORMAT_S8)) < 0)
		{
			error("snd_pcm_hw_params_set_format: %s\n", snd_strerror(err));
			return False;
		}
	}

#if 0
	if ((err = snd_pcm_hw_params_set_rate_resample(pcm_handle, hwparams, 1)) < 0)
	{
		error("snd_pcm_hw_params_set_rate_resample: %s\n", snd_strerror(err));
		return False;
	}
#endif

	exact_rate = rate = pwfx->nSamplesPerSec;
	if ((err = snd_pcm_hw_params_set_rate_near(pcm_handle, hwparams, &exact_rate, 0)) < 0)
	{
		error("snd_pcm_hw_params_set_rate_near: %s\n", snd_strerror(err));
		return False;
	}

	audiochannels = pwfx->nChannels;
	if ((err = snd_pcm_hw_params_set_channels(pcm_handle, hwparams, pwfx->nChannels)) < 0)
	{
		error("snd_pcm_hw_params_set_channels: %s\n", snd_strerror(err));
		return False;
	}


	buffertime = 500000;	/* microseconds */
	if ((err =
	     snd_pcm_hw_params_set_buffer_time_near(pcm_handle, hwparams, &buffertime, 0)) < 0)
	{
		error("snd_pcm_hw_params_set_buffer_time_near: %s\n", snd_strerror(err));
		return False;
	}

	if ((err = snd_pcm_hw_params(pcm_handle, hwparams)) < 0)
	{
		error("snd_pcm_hw_params: %s\n", snd_strerror(err));
		return False;
	}

	snd_pcm_hw_params_free(hwparams);

	if ((err = snd_pcm_prepare(pcm_handle)) < 0)
	{
		error("snd_pcm_prepare: %s\n", snd_strerror(err));
		return False;
	}

	reopened = True;

	return True;
}

void
alsa_play(void)
{
	struct audio_packet *packet;
	STREAM out;
	int len;
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

	len = (out->end - out->p) / (samplewidth * audiochannels);
	if ((len = snd_pcm_writei(pcm_handle, out->p, ((MAX_FRAMES < len) ? MAX_FRAMES : len))) < 0)
	{
		snd_pcm_prepare(pcm_handle);
		len = 0;
	}
	out->p += (len * samplewidth * audiochannels);

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
alsa_register(char *options)
{
	static struct audio_driver alsa_driver;

	alsa_driver.wave_out_write = rdpsnd_queue_write;
	alsa_driver.wave_out_open = alsa_open;
	alsa_driver.wave_out_close = alsa_close;
	alsa_driver.wave_out_format_supported = alsa_format_supported;
	alsa_driver.wave_out_set_format = alsa_set_format;
	alsa_driver.wave_out_volume = rdpsnd_dsp_softvol_set;
	alsa_driver.wave_out_play = alsa_play;
	alsa_driver.name = xstrdup("alsa");
	alsa_driver.description = xstrdup("ALSA output driver, default device: " DEFAULTDEVICE);
	alsa_driver.need_byteswap_on_be = 0;
	alsa_driver.next = NULL;

	if (options)
	{
		pcm_name = xstrdup(options);
	}
	else
	{
		pcm_name = xstrdup(DEFAULTDEVICE);
	}

	return &alsa_driver;
}
