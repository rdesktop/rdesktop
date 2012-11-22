/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - Sun
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 2003-2008
   Copyright (C) GuoJunBo <guojunbo@ict.ac.cn> 2003
   Copyright (C) Michael Gernoth <mike@zerfleddert.de> 2003-2008
   Copyright 2007-2008 Pierre Ossman <ossman@cendio.se> for Cendio AB
   Copyright 2008-2011 Peter Astrand <astrand@cendio.se> for Cendio AB

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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#include <string.h>

#if (defined(sun) && (defined(__svr4__) || defined(__SVR4)))
#include <stropts.h>
#endif

#define DEFAULTDEVICE	"/dev/audio"
#define MAX_LEN		512

static int dsp_fd = -1;
static int dsp_mode;
static int dsp_refs;

static RD_BOOL dsp_configured;
static RD_BOOL dsp_broken;
static RD_BOOL broken_2_channel_record = False;

static RD_BOOL dsp_out;
static RD_BOOL dsp_in;

static int stereo;
static int format;
static uint32 snd_rate;
static short samplewidth;
static char *dsp_dev;

static uint_t written_samples;

void sun_play(void);
void sun_record(void);

static int
sun_pause(void)
{
	audio_info_t info;

	AUDIO_INITINFO(&info);

	info.record.pause = 1;

	if (ioctl(dsp_fd, AUDIO_SETINFO, &info) == -1)
		return -1;

#if defined I_FLUSH && defined FLUSHR
	if (ioctl(dsp_fd, I_FLUSH, FLUSHR) == -1)
		return -1;
#endif

	return 0;
}

static int
sun_resume(void)
{
	audio_info_t info;

	AUDIO_INITINFO(&info);

	info.record.pause = 0;

	if (ioctl(dsp_fd, AUDIO_SETINFO, &info) == -1)
		return -1;

	return 0;
}

void
sun_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv)
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
sun_check_fds(fd_set * rfds, fd_set * wfds)
{
	if (FD_ISSET(dsp_fd, wfds))
		sun_play();
	if (FD_ISSET(dsp_fd, rfds))
		sun_record();
}

RD_BOOL
sun_open(int mode)
{
	audio_info_t info;

	if (dsp_fd != -1)
	{
		dsp_refs++;

		if (dsp_mode == O_RDWR)
			return True;

		if (dsp_mode == mode)
			return True;

		dsp_refs--;
		return False;
	}

	dsp_configured = False;
	dsp_broken = False;

	written_samples = 0;

	dsp_mode = O_RDWR;
	dsp_fd = open(dsp_dev, O_RDWR | O_NONBLOCK);
	if (dsp_fd != -1)
	{
		AUDIO_INITINFO(&info);

		if ((ioctl(dsp_fd, AUDIO_GETINFO, &info) == -1)
		    || !(info.hw_features & AUDIO_HWFEATURE_DUPLEX))
		{
			close(dsp_fd);
			dsp_fd = -1;
		}
	}

	if (dsp_fd == -1)
	{
		dsp_mode = mode;

		dsp_fd = open(dsp_dev, dsp_mode | O_NONBLOCK);
		if (dsp_fd == -1)
		{
			perror(dsp_dev);
			return False;
		}
	}

	/*
	 * Pause recording until we actually start using it.
	 */
	if (dsp_mode != O_WRONLY)
	{
		if (sun_pause() == -1)
		{
			close(dsp_fd);
			dsp_fd = -1;
			return False;
		}
	}

	dsp_refs++;

	return True;
}

void
sun_close(void)
{
	dsp_refs--;

	if (dsp_refs != 0)
		return;

	close(dsp_fd);
	dsp_fd = -1;
}

RD_BOOL
sun_open_out(void)
{
	if (!sun_open(O_WRONLY))
		return False;

	dsp_out = True;

	return True;
}

void
sun_close_out(void)
{
#if defined I_FLUSH && defined FLUSHW
	/* Flush the audiobuffer */
	ioctl(dsp_fd, I_FLUSH, FLUSHW);
#endif
#if defined AUDIO_FLUSH
	ioctl(dsp_fd, AUDIO_FLUSH, NULL);
#endif

	sun_close();

	/* Ack all remaining packets */
	while (!rdpsnd_queue_empty())
		rdpsnd_queue_next(0);

	dsp_out = False;
}

RD_BOOL
sun_open_in(void)
{
#if ! (defined I_FLUSH && defined FLUSHR)
	/*
	 * It is not possible to reliably use the recording without
	 * flush operations.
	 */
	return False;
#endif

	if (!sun_open(O_RDONLY))
		return False;

	/* 2 channel recording is known to be broken on Solaris x86
	   Sun Ray systems */
#ifdef L_ENDIAN
	if (strstr(dsp_dev, "/utaudio/"))
		broken_2_channel_record = True;
#endif

	/*
	 * Unpause the stream now that we have someone using it.
	 */
	if (sun_resume() == -1)
	{
		sun_close();
		return False;
	}

	dsp_in = True;

	return True;
}

void
sun_close_in(void)
{
	/*
	 * Repause the stream when the user goes away.
	 */
	sun_pause();

	sun_close();

	dsp_in = False;
}

RD_BOOL
sun_format_supported(RD_WAVEFORMATEX * pwfx)
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
sun_set_format(RD_WAVEFORMATEX * pwfx)
{
	audio_info_t info;

	ioctl(dsp_fd, AUDIO_DRAIN, 0);
	AUDIO_INITINFO(&info);

	if (dsp_configured)
	{
		if ((pwfx->wBitsPerSample == 8) && (format != AUDIO_ENCODING_LINEAR8))
			return False;
		if ((pwfx->wBitsPerSample == 16) && (format != AUDIO_ENCODING_LINEAR))
			return False;

		if ((pwfx->nChannels == 2) != ! !stereo)
			return False;

		if (pwfx->nSamplesPerSec != snd_rate)
			return False;

		return True;
	}

	sun_pause();

	if (pwfx->wBitsPerSample == 8)
		format = AUDIO_ENCODING_LINEAR8;
	else if (pwfx->wBitsPerSample == 16)
		format = AUDIO_ENCODING_LINEAR;

	samplewidth = pwfx->wBitsPerSample / 8;

	info.play.channels = pwfx->nChannels;
	info.record.channels = info.play.channels;

	if (pwfx->nChannels == 1)
	{
		stereo = 0;
	}
	else if (pwfx->nChannels == 2)
	{
		stereo = 1;
		samplewidth *= 2;

		if (broken_2_channel_record)
		{
			info.record.channels = 1;
		}
	}

	snd_rate = pwfx->nSamplesPerSec;

	info.play.sample_rate = pwfx->nSamplesPerSec;
	info.play.precision = pwfx->wBitsPerSample;
	info.play.encoding = format;
	info.play.samples = 0;
	info.play.eof = 0;
	info.play.error = 0;

	info.record.sample_rate = info.play.sample_rate;
	info.record.precision = info.play.precision;
	info.record.encoding = info.play.encoding;
	info.record.samples = 0;
	info.record.error = 0;

	if (ioctl(dsp_fd, AUDIO_SETINFO, &info) == -1)
	{
		perror("AUDIO_SETINFO");
		sun_close();
		return False;
	}

	dsp_configured = True;

	if (dsp_in)
		sun_resume();

	return True;
}

void
sun_volume(uint16 left, uint16 right)
{
	audio_info_t info;
	uint balance;
	uint volume;

	AUDIO_INITINFO(&info);

	volume = (left > right) ? left : right;

	if (volume / AUDIO_MID_BALANCE != 0)
	{
		balance =
			AUDIO_MID_BALANCE - (left / (volume / AUDIO_MID_BALANCE)) +
			(right / (volume / AUDIO_MID_BALANCE));
	}
	else
	{
		balance = AUDIO_MID_BALANCE;
	}

	info.play.gain = volume / (65536 / AUDIO_MAX_GAIN);
	info.play.balance = balance;

	if (ioctl(dsp_fd, AUDIO_SETINFO, &info) == -1)
	{
		perror("AUDIO_SETINFO");
		return;
	}
}

void
sun_play(void)
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

	written_samples += len / (samplewidth * (stereo ? 2 : 1));

	dsp_broken = False;

	out->p += len;

	if (out->p == out->end)
	{
		audio_info_t info;
		uint_t delay_samples;
		unsigned long delay_us;

		if (ioctl(dsp_fd, AUDIO_GETINFO, &info) != -1)
			delay_samples = written_samples - info.play.samples;
		else
			delay_samples = out->size / (samplewidth * (stereo ? 2 : 1));

		delay_us = delay_samples * (1000000 / snd_rate);
		rdpsnd_queue_next(delay_us);
	}
}

void
sun_record(void)
{
	char buffer[32768];
	int len;

	len = read(dsp_fd, buffer, sizeof(buffer) / 2);
	if (len == -1)
	{
		if (errno != EWOULDBLOCK)
			perror("read audio");
		return;
	}

	if (broken_2_channel_record)
	{
		unsigned int i;
		int rec_samplewidth = samplewidth / 2;
		/* Loop over each byte read backwards and put in place */
		i = len - 1;
		do
		{
			int samples_before = i / rec_samplewidth * 2;
			int sample_byte = i % rec_samplewidth;
			int ch1_offset = samples_before * rec_samplewidth + sample_byte;
			// Channel 1
			buffer[ch1_offset] = buffer[i];
			// Channel 2
			buffer[ch1_offset + rec_samplewidth] = buffer[i];

			i--;
		}
		while (i);
		len *= 2;
	}

	rdpsnd_record(buffer, len);
}

struct audio_driver *
sun_register(char *options)
{
	static struct audio_driver sun_driver;

	memset(&sun_driver, 0, sizeof(sun_driver));

	sun_driver.name = "sun";
	sun_driver.description =
		"SUN/BSD output driver, default device: " DEFAULTDEVICE " or $AUDIODEV";

	sun_driver.add_fds = sun_add_fds;
	sun_driver.check_fds = sun_check_fds;

	sun_driver.wave_out_open = sun_open_out;
	sun_driver.wave_out_close = sun_close_out;
	sun_driver.wave_out_format_supported = sun_format_supported;
	sun_driver.wave_out_set_format = sun_set_format;
	sun_driver.wave_out_volume = sun_volume;

	sun_driver.wave_in_open = sun_open_in;
	sun_driver.wave_in_close = sun_close_in;
	sun_driver.wave_in_format_supported = sun_format_supported;
	sun_driver.wave_in_set_format = sun_set_format;
	sun_driver.wave_in_volume = NULL;	/* FIXME */

	sun_driver.need_byteswap_on_be = 1;
	sun_driver.need_resampling = 0;

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

	return &sun_driver;
}
