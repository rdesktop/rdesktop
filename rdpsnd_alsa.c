/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - alsa-driver
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 2003-2008
   Copyright (C) GuoJunBo <guojunbo@ict.ac.cn> 2003
   Copyright (C) Michael Gernoth <mike@zerfleddert.de> 2006-2008
   Copyright 2006-2008 Pierre Ossman <ossman@cendio.se> for Cendio AB

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
#include "rdpsnd.h"
#include "rdpsnd_dsp.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <alsa/asoundlib.h>
#include <sys/time.h>

#define DEFAULTDEVICE	"default"
#define MAX_FRAMES	32

static struct pollfd pfds_out[32];
static int num_fds_out;

static struct pollfd pfds_in[32];
static int num_fds_in;

static snd_pcm_t *out_handle = NULL;
static snd_pcm_t *in_handle = NULL;

static RD_BOOL reopened;

static short samplewidth_out;
static int audiochannels_out;
static unsigned int rate_out;

static short samplewidth_in;
static int audiochannels_in;
static unsigned int rate_in;

static char *pcm_name;

void alsa_play(void);
void alsa_record(void);

void
alsa_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv)
{
	int err;
	struct pollfd *f;

	if (out_handle && !rdpsnd_queue_empty())
	{
		num_fds_out = snd_pcm_poll_descriptors_count(out_handle);

		if (num_fds_out > sizeof(pfds_out) / sizeof(*pfds_out))
			return;

		err = snd_pcm_poll_descriptors(out_handle, pfds_out, num_fds_out);
		if (err < 0)
			return;

		for (f = pfds_out; f < &pfds_out[num_fds_out]; f++)
		{
			if (f->events & POLLIN)
				FD_SET(f->fd, rfds);
			if (f->events & POLLOUT)
				FD_SET(f->fd, wfds);
			if (f->fd > *n && (f->events & (POLLIN | POLLOUT)))
				*n = f->fd;
		}
	}

	if (in_handle)
	{
		num_fds_in = snd_pcm_poll_descriptors_count(in_handle);

		if (num_fds_in > sizeof(pfds_in) / sizeof(*pfds_in))
			return;

		err = snd_pcm_poll_descriptors(in_handle, pfds_in, num_fds_in);
		if (err < 0)
			return;

		for (f = pfds_in; f < &pfds_in[num_fds_in]; f++)
		{
			if (f->events & POLLIN)
				FD_SET(f->fd, rfds);
			if (f->events & POLLOUT)
				FD_SET(f->fd, wfds);
			if (f->fd > *n && (f->events & (POLLIN | POLLOUT)))
				*n = f->fd;
		}
	}
}

void
alsa_check_fds(fd_set * rfds, fd_set * wfds)
{
	struct pollfd *f;
	int err;
	unsigned short revents;

	if (out_handle && !rdpsnd_queue_empty())
	{
		for (f = pfds_out; f < &pfds_out[num_fds_out]; f++)
		{
			f->revents = 0;
			if (f->fd != -1)
			{
				/* Fixme: This doesn't properly deal with things like POLLHUP */
				if (FD_ISSET(f->fd, rfds))
					f->revents |= POLLIN;
				if (FD_ISSET(f->fd, wfds))
					f->revents |= POLLOUT;
			}
		}

		err = snd_pcm_poll_descriptors_revents(out_handle, pfds_out, num_fds_out, &revents);
		if (err < 0)
			return;

		if (revents & POLLOUT)
			alsa_play();
	}


	if (in_handle)
	{
		for (f = pfds_in; f < &pfds_in[num_fds_in]; f++)
		{
			f->revents = 0;
			if (f->fd != -1)
			{
				/* Fixme: This doesn't properly deal with things like POLLHUP */
				if (FD_ISSET(f->fd, rfds))
					f->revents |= POLLIN;
				if (FD_ISSET(f->fd, wfds))
					f->revents |= POLLOUT;
			}
		}

		err = snd_pcm_poll_descriptors_revents(in_handle, pfds_in, num_fds_in, &revents);
		if (err < 0)
			return;

		if (revents & POLLIN)
			alsa_record();
	}
}

static RD_BOOL
alsa_set_format(snd_pcm_t * pcm, RD_WAVEFORMATEX * pwfx)
{
	snd_pcm_hw_params_t *hwparams = NULL;
	int err;
	unsigned int buffertime;
	short samplewidth;
	int audiochannels;
	unsigned int rate;

	samplewidth = pwfx->wBitsPerSample / 8;

	if ((err = snd_pcm_hw_params_malloc(&hwparams)) < 0)
	{
		error("snd_pcm_hw_params_malloc: %s\n", snd_strerror(err));
		return False;
	}

	if ((err = snd_pcm_hw_params_any(pcm, hwparams)) < 0)
	{
		error("snd_pcm_hw_params_any: %s\n", snd_strerror(err));
		return False;
	}

	if ((err = snd_pcm_hw_params_set_access(pcm, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
	{
		error("snd_pcm_hw_params_set_access: %s\n", snd_strerror(err));
		return False;
	}

	if (pwfx->wBitsPerSample == 16)
	{
		if ((err = snd_pcm_hw_params_set_format(pcm, hwparams, SND_PCM_FORMAT_S16_LE)) < 0)
		{
			error("snd_pcm_hw_params_set_format: %s\n", snd_strerror(err));
			return False;
		}
	}
	else
	{
		if ((err = snd_pcm_hw_params_set_format(pcm, hwparams, SND_PCM_FORMAT_S8)) < 0)
		{
			error("snd_pcm_hw_params_set_format: %s\n", snd_strerror(err));
			return False;
		}
	}

#if 0
	if ((err = snd_pcm_hw_params_set_rate_resample(pcm, hwparams, 1)) < 0)
	{
		error("snd_pcm_hw_params_set_rate_resample: %s\n", snd_strerror(err));
		return False;
	}
#endif

	rate = pwfx->nSamplesPerSec;
	if ((err = snd_pcm_hw_params_set_rate_near(pcm, hwparams, &rate, 0)) < 0)
	{
		error("snd_pcm_hw_params_set_rate_near: %s\n", snd_strerror(err));
		return False;
	}

	audiochannels = pwfx->nChannels;
	if ((err = snd_pcm_hw_params_set_channels(pcm, hwparams, pwfx->nChannels)) < 0)
	{
		error("snd_pcm_hw_params_set_channels: %s\n", snd_strerror(err));
		return False;
	}


	buffertime = 500000;	/* microseconds */
	if ((err = snd_pcm_hw_params_set_buffer_time_near(pcm, hwparams, &buffertime, 0)) < 0)
	{
		error("snd_pcm_hw_params_set_buffer_time_near: %s\n", snd_strerror(err));
		return False;
	}

	if ((err = snd_pcm_hw_params(pcm, hwparams)) < 0)
	{
		error("snd_pcm_hw_params: %s\n", snd_strerror(err));
		return False;
	}

	snd_pcm_hw_params_free(hwparams);

	if ((err = snd_pcm_prepare(pcm)) < 0)
	{
		error("snd_pcm_prepare: %s\n", snd_strerror(err));
		return False;
	}

	reopened = True;

	return True;
}

RD_BOOL
alsa_open_out(void)
{
	int err;

	if ((err = snd_pcm_open(&out_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
	{
		error("snd_pcm_open: %s\n", snd_strerror(err));
		return False;
	}

	reopened = True;

	return True;
}

void
alsa_close_out(void)
{
	/* Ack all remaining packets */
	while (!rdpsnd_queue_empty())
		rdpsnd_queue_next(0);

	if (out_handle)
	{
		snd_pcm_close(out_handle);
		out_handle = NULL;
	}
}

RD_BOOL
alsa_format_supported(RD_WAVEFORMATEX * pwfx)
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

RD_BOOL
alsa_set_format_out(RD_WAVEFORMATEX * pwfx)
{
	if (!alsa_set_format(out_handle, pwfx))
		return False;

	samplewidth_out = pwfx->wBitsPerSample / 8;
	audiochannels_out = pwfx->nChannels;
	rate_out = pwfx->nSamplesPerSec;

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

	/* We shouldn't be called if the queue is empty, but still */
	if (rdpsnd_queue_empty())
		return;

	packet = rdpsnd_queue_current_packet();
	out = &packet->s;

	next_tick = rdpsnd_queue_next_tick();

	len = (out->end - out->p) / (samplewidth_out * audiochannels_out);
	if ((len = snd_pcm_writei(out_handle, out->p, ((MAX_FRAMES < len) ? MAX_FRAMES : len))) < 0)
	{
		printf("Fooo!\n");
		snd_pcm_prepare(out_handle);
		len = 0;
	}
	out->p += (len * samplewidth_out * audiochannels_out);

	gettimeofday(&tv, NULL);

	duration = ((tv.tv_sec - prev_s) * 1000000 + (tv.tv_usec - prev_us)) / 1000;

	if (packet->tick > next_tick)
		next_tick += 65536;

	if ((out->p == out->end) || duration > next_tick - packet->tick + 500)
	{
		snd_pcm_sframes_t delay_frames;
		unsigned long delay_us;

		prev_s = tv.tv_sec;
		prev_us = tv.tv_usec;

		if (abs((next_tick - packet->tick) - duration) > 20)
		{
			DEBUG(("duration: %d, calc: %d, ", duration, next_tick - packet->tick));
			DEBUG(("last: %d, is: %d, should: %d\n", packet->tick,
			       (packet->tick + duration) % 65536, next_tick % 65536));
		}

		if (snd_pcm_delay(out_handle, &delay_frames) < 0)
			delay_frames = out->size / (samplewidth_out * audiochannels_out);
		if (delay_frames < 0)
			delay_frames = 0;

		delay_us = delay_frames * (1000000 / rate_out);

		rdpsnd_queue_next(delay_us);
	}
}

RD_BOOL
alsa_open_in(void)
{
	int err;

	if ((err =
	     snd_pcm_open(&in_handle, pcm_name, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0)
	{
		error("snd_pcm_open: %s\n", snd_strerror(err));
		return False;
	}

	return True;
}

void
alsa_close_in(void)
{
	if (in_handle)
	{
		snd_pcm_close(in_handle);
		in_handle = NULL;
	}
}

RD_BOOL
alsa_set_format_in(RD_WAVEFORMATEX * pwfx)
{
	int err;

	if (!alsa_set_format(in_handle, pwfx))
		return False;

	if ((err = snd_pcm_start(in_handle)) < 0)
	{
		error("snd_pcm_start: %s\n", snd_strerror(err));
		return False;
	}

	samplewidth_in = pwfx->wBitsPerSample / 8;
	audiochannels_in = pwfx->nChannels;
	rate_in = pwfx->nSamplesPerSec;

	return True;
}

void
alsa_record(void)
{
	int len;
	char buffer[32768];

	len = snd_pcm_readi(in_handle, buffer,
			    sizeof(buffer) / (samplewidth_in * audiochannels_in));
	if (len < 0)
	{
		snd_pcm_prepare(in_handle);
		len = 0;
	}

	rdpsnd_record(buffer, len * samplewidth_in * audiochannels_in);
}

struct audio_driver *
alsa_register(char *options)
{
	static struct audio_driver alsa_driver;

	memset(&alsa_driver, 0, sizeof(alsa_driver));

	alsa_driver.name = "alsa";
	alsa_driver.description = "ALSA output driver, default device: " DEFAULTDEVICE;

	alsa_driver.add_fds = alsa_add_fds;
	alsa_driver.check_fds = alsa_check_fds;

	alsa_driver.wave_out_open = alsa_open_out;
	alsa_driver.wave_out_close = alsa_close_out;
	alsa_driver.wave_out_format_supported = alsa_format_supported;
	alsa_driver.wave_out_set_format = alsa_set_format_out;
	alsa_driver.wave_out_volume = rdpsnd_dsp_softvol_set;

	alsa_driver.wave_in_open = alsa_open_in;
	alsa_driver.wave_in_close = alsa_close_in;
	alsa_driver.wave_in_format_supported = alsa_format_supported;
	alsa_driver.wave_in_set_format = alsa_set_format_in;
	alsa_driver.wave_in_volume = NULL;	/* FIXME */

	alsa_driver.need_byteswap_on_be = 0;
	alsa_driver.need_resampling = 0;

	if (options)
	{
		pcm_name = xstrdup(options);
	}
	else
	{
		pcm_name = DEFAULTDEVICE;
	}

	return &alsa_driver;
}
