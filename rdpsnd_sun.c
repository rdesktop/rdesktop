/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - Sun
   Copyright (C) Matthew Chapman 2003-2007
   Copyright (C) GuoJunBo guojunbo@ict.ac.cn 2003
   Copyright (C) Michael Gernoth mike@zerfleddert.de 2003-2007

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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>

#if (defined(sun) && (defined(__svr4__) || defined(__SVR4)))
#include <stropts.h>
#endif

#define DEFAULTDEVICE	"/dev/audio"

static int dsp_fd = -1;
static RD_BOOL dsp_busy;

static RD_BOOL g_reopened;
static short g_samplewidth;
static char *dsp_dev;

void sun_play(void);

void
sun_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv)
{
	if (dsp_fd == -1)
		return;

	if (rdpsnd_queue_empty())
		return;

	FD_SET(dsp_fd, wfds);
	if (dsp_fd > *n)
		*n = dsp_fd;
}

void
sun_check_fds(fd_set * rfds, fd_set * wfds)
{
	if (FD_ISSET(dsp_fd, wfds))
		sun_play();
}

RD_BOOL
sun_open(void)
{
	if ((dsp_fd = open(dsp_dev, O_WRONLY | O_NONBLOCK)) == -1)
	{
		perror(dsp_dev);
		return False;
	}

	/* Non-blocking so that user interface is responsive */
	fcntl(dsp_fd, F_SETFL, fcntl(dsp_fd, F_GETFL) | O_NONBLOCK);

	g_reopened = True;

	return True;
}

void
sun_close(void)
{
	/* Ack all remaining packets */
	while (!rdpsnd_queue_empty())
		rdpsnd_queue_next(0);

#if defined I_FLUSH && defined FLUSHW
	/* Flush the audiobuffer */
	ioctl(dsp_fd, I_FLUSH, FLUSHW);
#endif
#if defined AUDIO_FLUSH
	ioctl(dsp_fd, AUDIO_FLUSH, NULL);
#endif
	close(dsp_fd);
	dsp_fd = -1;
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


	if (pwfx->wBitsPerSample == 8)
	{
		info.play.encoding = AUDIO_ENCODING_LINEAR8;
	}
	else if (pwfx->wBitsPerSample == 16)
	{
		info.play.encoding = AUDIO_ENCODING_LINEAR;
	}

	g_samplewidth = pwfx->wBitsPerSample / 8;

	if (pwfx->nChannels == 1)
	{
		info.play.channels = 1;
	}
	else if (pwfx->nChannels == 2)
	{
		info.play.channels = 2;
		g_samplewidth *= 2;
	}

	info.play.sample_rate = pwfx->nSamplesPerSec;
	info.play.precision = pwfx->wBitsPerSample;
	info.play.samples = 0;
	info.play.eof = 0;
	info.play.error = 0;
	g_reopened = True;

	if (ioctl(dsp_fd, AUDIO_SETINFO, &info) == -1)
	{
		perror("AUDIO_SETINFO");
		sun_close();
		return False;
	}

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
	audio_info_t info;
	ssize_t len;
	unsigned int i;
	STREAM out;
	static RD_BOOL sentcompletion = True;
	static uint32 samplecnt = 0;
	static uint32 numsamples;

	while (1)
	{
		if (g_reopened)
		{
			/* Device was just (re)openend */
			samplecnt = 0;
			sentcompletion = True;
			g_reopened = False;
		}

		if (rdpsnd_queue_empty())
			return;

		packet = rdpsnd_queue_current_packet();
		out = &packet->s;

		if (sentcompletion)
		{
			sentcompletion = False;
			numsamples = (out->end - out->p) / g_samplewidth;
		}

		len = 0;

		if (out->end != out->p)
		{
			len = write(dsp_fd, out->p, out->end - out->p);
			if (len == -1)
			{
				if (errno != EWOULDBLOCK)
					perror("write audio");
				return;
			}
		}

		out->p += len;
		if (out->p == out->end)
		{
			if (ioctl(dsp_fd, AUDIO_GETINFO, &info) == -1)
			{
				perror("AUDIO_GETINFO");
				return;
			}

			/* Ack the packet, if we have played at least 70% */
			if (info.play.samples >= samplecnt + ((numsamples * 7) / 10))
			{
				samplecnt += numsamples;
				/* We need to add 50 to tell windows that time has passed while
				 * playing this packet */
				rdpsnd_queue_next(50);
				sentcompletion = True;
			}
			else
			{
				return;
			}
		}
	}
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

	sun_driver.wave_out_open = sun_open;
	sun_driver.wave_out_close = sun_close;
	sun_driver.wave_out_format_supported = sun_format_supported;
	sun_driver.wave_out_set_format = sun_set_format;
	sun_driver.wave_out_volume = sun_volume;

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
