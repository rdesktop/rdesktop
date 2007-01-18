/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - Open Sound System
   Copyright (C) Matthew Chapman 2003-2007
   Copyright (C) GuoJunBo guojunbo@ict.ac.cn 2003
   Copyright 2006-2007 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

/* 
   This is a workaround for Esound bug 312665. 
   FIXME: Remove this when Esound is fixed. 
*/
#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS
#endif

#include "rdesktop.h"
#include "rdpsnd.h"
#include "rdpsnd_dsp.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEFAULTDEVICE	"/dev/dsp"
#define MAX_LEN		512

static int dsp_fd = -1;
static int dsp_mode;
static int dsp_refs;

static RD_BOOL dsp_configured;
static RD_BOOL dsp_broken;

static RD_BOOL dsp_out;
static RD_BOOL dsp_in;

static int stereo;
static int format;
static uint32 snd_rate;
static short samplewidth;
static char *dsp_dev;
static RD_BOOL in_esddsp;

/* This is a just a forward declaration */
static struct audio_driver oss_driver;

void oss_play(void);
void oss_record(void);

void
oss_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv)
{
	if (dsp_fd == -1)
		return;

	if (dsp_out && !rdpsnd_queue_empty())
		FD_SET(dsp_fd, wfds);
	if (dsp_in)
		FD_SET(dsp_fd, rfds);
	if (dsp_fd > *n)
		*n = dsp_fd;
}

void
oss_check_fds(fd_set * rfds, fd_set * wfds)
{
	if (FD_ISSET(dsp_fd, wfds))
		oss_play();
	if (FD_ISSET(dsp_fd, rfds))
		oss_record();
}

static RD_BOOL
detect_esddsp(void)
{
	struct stat s;
	char *preload;

	if (fstat(dsp_fd, &s) == -1)
		return False;

	if (S_ISCHR(s.st_mode) || S_ISBLK(s.st_mode))
		return False;

	preload = getenv("LD_PRELOAD");
	if (preload == NULL)
		return False;

	if (strstr(preload, "esddsp") == NULL)
		return False;

	return True;
}

RD_BOOL
oss_open(int fallback)
{
	int caps;

	if (dsp_fd != -1)
	{
		dsp_refs++;

		if (dsp_mode == O_RDWR)
			return True;

		if (dsp_mode == fallback)
			return True;

		dsp_refs--;
		return False;
	}

	dsp_configured = False;
	dsp_broken = False;

	dsp_mode = O_RDWR;
	dsp_fd = open(dsp_dev, O_RDWR | O_NONBLOCK);
	if (dsp_fd != -1)
	{
		ioctl(dsp_fd, SNDCTL_DSP_SETDUPLEX, 0);

		if ((ioctl(dsp_fd, SNDCTL_DSP_GETCAPS, &caps) < 0) || !(caps & DSP_CAP_DUPLEX))
		{
			close(dsp_fd);
			dsp_fd = -1;
		}
	}

	if (dsp_fd == -1)
	{
		dsp_mode = fallback;

		dsp_fd = open(dsp_dev, dsp_mode | O_NONBLOCK);
		if (dsp_fd == -1)
		{
			perror(dsp_dev);
			return False;
		}
	}

	dsp_refs++;

	in_esddsp = detect_esddsp();

	return True;
}

void
oss_close(void)
{
	dsp_refs--;

	if (dsp_refs != 0)
		return;

	close(dsp_fd);
	dsp_fd = -1;
}

RD_BOOL
oss_open_out(void)
{
	if (!oss_open(O_WRONLY))
		return False;

	dsp_out = True;

	return True;
}

void
oss_close_out(void)
{
	oss_close();

	/* Ack all remaining packets */
	while (!rdpsnd_queue_empty())
		rdpsnd_queue_next(0);

	dsp_out = False;
}

RD_BOOL
oss_open_in(void)
{
	if (!oss_open(O_RDONLY))
		return False;

	dsp_in = True;

	return True;
}

void
oss_close_in(void)
{
	oss_close();

	dsp_in = False;
}

RD_BOOL
oss_format_supported(RD_WAVEFORMATEX * pwfx)
{
	if (pwfx->wFormatTag != WAVE_FORMAT_PCM)
		return False;
	if ((pwfx->nChannels != 1) && (pwfx->nChannels != 2))
		return False;
	if ((pwfx->wBitsPerSample != 8) && (pwfx->wBitsPerSample != 16))
		return False;

	return True;
}

RD_BOOL
oss_set_format(RD_WAVEFORMATEX * pwfx)
{
	int fragments;
	static RD_BOOL driver_broken = False;

	if (dsp_configured)
	{
		if ((pwfx->wBitsPerSample == 8) && (format != AFMT_U8))
			return False;
		if ((pwfx->wBitsPerSample == 16) && (format != AFMT_S16_LE))
			return False;

		if ((pwfx->nChannels == 2) != !!stereo)
			return False;

		if (pwfx->nSamplesPerSec != snd_rate)
			return False;

		return True;
	}

	ioctl(dsp_fd, SNDCTL_DSP_RESET, NULL);
	ioctl(dsp_fd, SNDCTL_DSP_SYNC, NULL);

	if (pwfx->wBitsPerSample == 8)
		format = AFMT_U8;
	else if (pwfx->wBitsPerSample == 16)
		format = AFMT_S16_LE;

	samplewidth = pwfx->wBitsPerSample / 8;

	if (ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &format) == -1)
	{
		perror("SNDCTL_DSP_SETFMT");
		oss_close();
		return False;
	}

	if (pwfx->nChannels == 2)
	{
		stereo = 1;
		samplewidth *= 2;
	}
	else
	{
		stereo = 0;
	}

	if (ioctl(dsp_fd, SNDCTL_DSP_STEREO, &stereo) == -1)
	{
		perror("SNDCTL_DSP_CHANNELS");
		oss_close();
		return False;
	}

	oss_driver.need_resampling = 0;
	snd_rate = pwfx->nSamplesPerSec;
	if (ioctl(dsp_fd, SNDCTL_DSP_SPEED, &snd_rate) == -1)
	{
		uint32 rates[] = { 44100, 48000, 0 };
		uint32 *prates = rates;

		while (*prates != 0)
		{
			if ((pwfx->nSamplesPerSec != *prates)
			    && (ioctl(dsp_fd, SNDCTL_DSP_SPEED, prates) != -1))
			{
				oss_driver.need_resampling = 1;
				snd_rate = *prates;
				if (rdpsnd_dsp_resample_set
				    (snd_rate, pwfx->wBitsPerSample, pwfx->nChannels) == False)
				{
					error("rdpsnd_dsp_resample_set failed");
					oss_close();
					return False;
				}

				break;
			}
			prates++;
		}

		if (*prates == 0)
		{
			perror("SNDCTL_DSP_SPEED");
			oss_close();
			return False;
		}
	}

	/* try to get 12 fragments of 2^12 bytes size */
	fragments = (12 << 16) + 12;
	ioctl(dsp_fd, SNDCTL_DSP_SETFRAGMENT, &fragments);

	if (!driver_broken)
	{
		audio_buf_info info;

		memset(&info, 0, sizeof(info));
		if (ioctl(dsp_fd, SNDCTL_DSP_GETOSPACE, &info) == -1)
		{
			perror("SNDCTL_DSP_GETOSPACE");
			oss_close();
			return False;
		}

		if (info.fragments == 0 || info.fragstotal == 0 || info.fragsize == 0)
		{
			fprintf(stderr,
				"Broken OSS-driver detected: fragments: %d, fragstotal: %d, fragsize: %d\n",
				info.fragments, info.fragstotal, info.fragsize);
			driver_broken = True;
		}
	}

	dsp_configured = True;

	return True;
}

void
oss_volume(uint16 left, uint16 right)
{
	uint32 volume;

	volume = left / (65536 / 100);
	volume |= right / (65536 / 100) << 8;

	if (ioctl(dsp_fd, MIXER_WRITE(SOUND_MIXER_PCM), &volume) == -1)
	{
		warning("hardware volume control unavailable, falling back to software volume control!\n");
		oss_driver.wave_out_volume = rdpsnd_dsp_softvol_set;
		rdpsnd_dsp_softvol_set(left, right);
		return;
	}
}

void
oss_play(void)
{
	struct audio_packet *packet;
	ssize_t len;
	STREAM out;

	/* We shouldn't be called if the queue is empty, but still */
	if (rdpsnd_queue_empty())
		return;

	packet = rdpsnd_queue_current_packet();
	out = &packet->s;

	len = out->end - out->p;

	len = write(dsp_fd, out->p, (len > MAX_LEN) ? MAX_LEN : len);
	if (len == -1)
	{
		if (errno != EWOULDBLOCK)
		{
			if (!dsp_broken)
				perror("RDPSND: write()");
			dsp_broken = True;
			rdpsnd_queue_next(0);
		}
		return;
	}

	dsp_broken = False;

	out->p += len;

	if (out->p == out->end)
	{
		int delay_bytes;
		unsigned long delay_us;
		audio_buf_info info;

		if (in_esddsp)
		{
			/* EsounD has no way of querying buffer status, so we have to
			 * go with a fixed size. */
			delay_bytes = out->size;
		}
		else
		{
#ifdef SNDCTL_DSP_GETODELAY
			delay_bytes = 0;
			if (ioctl(dsp_fd, SNDCTL_DSP_GETODELAY, &delay_bytes) == -1)
				delay_bytes = -1;
#else
			delay_bytes = -1;
#endif

			if (delay_bytes == -1)
			{
				if (ioctl(dsp_fd, SNDCTL_DSP_GETOSPACE, &info) != -1)
					delay_bytes = info.fragstotal * info.fragsize - info.bytes;
				else
					delay_bytes = out->size;
			}
		}

		delay_us = delay_bytes * (1000000 / (samplewidth * snd_rate));
		rdpsnd_queue_next(delay_us);
	}
}

void
oss_record(void)
{
	char buffer[32768];
	int len;

	len = read(dsp_fd, buffer, sizeof(buffer));
	if (len == -1)
	{
		if (errno != EWOULDBLOCK)
		{
			if (!dsp_broken)
				perror("RDPSND: read()");
			dsp_broken = True;
			rdpsnd_queue_next(0);
		}
		return;
	}

	dsp_broken = False;

	rdpsnd_record(buffer, len);
}

struct audio_driver *
oss_register(char *options)
{
	memset(&oss_driver, 0, sizeof(oss_driver));

	oss_driver.name = "oss";
	oss_driver.description =
		"OSS output driver, default device: " DEFAULTDEVICE " or $AUDIODEV";

	oss_driver.add_fds = oss_add_fds;
	oss_driver.check_fds = oss_check_fds;

	oss_driver.wave_out_open = oss_open_out;
	oss_driver.wave_out_close = oss_close_out;
	oss_driver.wave_out_format_supported = oss_format_supported;
	oss_driver.wave_out_set_format = oss_set_format;
	oss_driver.wave_out_volume = oss_volume;

	oss_driver.wave_in_open = oss_open_in;
	oss_driver.wave_in_close = oss_close_in;
	oss_driver.wave_in_format_supported = oss_format_supported;
	oss_driver.wave_in_set_format = oss_set_format;
	oss_driver.wave_in_volume = NULL;	/* FIXME */

	oss_driver.need_byteswap_on_be = 0;
	oss_driver.need_resampling = 0;

	if (options)
	{
		dsp_dev = xstrdup(options);
	}
	else
	{
		dsp_dev = getenv("AUDIODEV");

		if (dsp_dev == NULL)
		{
			dsp_dev = DEFAULTDEVICE;
		}
	}

	return &oss_driver;
}
