/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - Sun
   Copyright (C) Matthew Chapman 2003
   Copyright (C) GuoJunBo guojunbo@ict.ac.cn 2003
   Copyright (C) Michael Gernoth mike@zerfleddert.de 2003-2006

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

static BOOL g_reopened;
static BOOL g_swapaudio;
static short g_samplewidth;
static char *dsp_dev;

BOOL
sun_open(void)
{
	if ((g_dsp_fd = open(dsp_dev, O_WRONLY | O_NONBLOCK)) == -1)
	{
		perror(dsp_dev);
		return False;
	}

	/* Non-blocking so that user interface is responsive */
	fcntl(g_dsp_fd, F_SETFL, fcntl(g_dsp_fd, F_GETFL) | O_NONBLOCK);

	rdpsnd_queue_init();
	g_reopened = True;

	return True;
}

void
sun_close(void)
{
	/* Ack all remaining packets */
	while (!rdpsnd_queue_empty())
	{
		rdpsnd_send_completion(rdpsnd_queue_current_packet()->tick,
				       rdpsnd_queue_current_packet()->index);
		rdpsnd_queue_next();
	}

#if defined I_FLUSH && defined FLUSHW
	/* Flush the audiobuffer */
	ioctl(g_dsp_fd, I_FLUSH, FLUSHW);
#endif
#if defined AUDIO_FLUSH
	ioctl(g_dsp_fd, AUDIO_FLUSH, NULL);
#endif
	close(g_dsp_fd);
}

BOOL
sun_format_supported(WAVEFORMATEX * pwfx)
{
	if (pwfx->wFormatTag != WAVE_FORMAT_PCM)
		return False;
	if ((pwfx->nChannels != 1) && (pwfx->nChannels != 2))
		return False;
	if ((pwfx->wBitsPerSample != 8) && (pwfx->wBitsPerSample != 16))
		return False;

	return True;
}

BOOL
sun_set_format(WAVEFORMATEX * pwfx)
{
	audio_info_t info;

	ioctl(g_dsp_fd, AUDIO_DRAIN, 0);
	g_swapaudio = False;
	AUDIO_INITINFO(&info);


	if (pwfx->wBitsPerSample == 8)
	{
		info.play.encoding = AUDIO_ENCODING_LINEAR8;
	}
	else if (pwfx->wBitsPerSample == 16)
	{
		info.play.encoding = AUDIO_ENCODING_LINEAR;
		/* Do we need to swap the 16bit values? (Are we BigEndian) */
#ifdef B_ENDIAN
		g_swapaudio = 1;
#else
		g_swapaudio = 0;
#endif
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

	if (ioctl(g_dsp_fd, AUDIO_SETINFO, &info) == -1)
	{
		perror("AUDIO_SETINFO");
		close(g_dsp_fd);
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

	if (ioctl(g_dsp_fd, AUDIO_SETINFO, &info) == -1)
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
	uint8 swap;
	STREAM out;
	static BOOL swapped = False;
	static BOOL sentcompletion = True;
	static uint32 samplecnt = 0;
	static uint32 numsamples;

	while (1)
	{
		if (g_reopened)
		{
			/* Device was just (re)openend */
			samplecnt = 0;
			swapped = False;
			sentcompletion = True;
			g_reopened = False;
		}

		if (rdpsnd_queue_empty())
		{
			g_dsp_busy = 0;
			return;
		}

		packet = rdpsnd_queue_current_packet();
		out = &packet->s;

		/* Swap the current packet, but only once */
		if (g_swapaudio && !swapped)
		{
			for (i = 0; i < out->end - out->p; i += 2)
			{
				swap = *(out->p + i);
				*(out->p + i) = *(out->p + i + 1);
				*(out->p + i + 1) = swap;
			}
			swapped = True;
		}

		if (sentcompletion)
		{
			sentcompletion = False;
			numsamples = (out->end - out->p) / g_samplewidth;
		}

		len = 0;

		if (out->end != out->p)
		{
			len = write(g_dsp_fd, out->p, out->end - out->p);
			if (len == -1)
			{
				if (errno != EWOULDBLOCK)
					perror("write audio");
				g_dsp_busy = 1;
				return;
			}
		}

		out->p += len;
		if (out->p == out->end)
		{
			if (ioctl(g_dsp_fd, AUDIO_GETINFO, &info) == -1)
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
				rdpsnd_send_completion(packet->tick + 50, packet->index);
				rdpsnd_queue_next();
				swapped = False;
				sentcompletion = True;
			}
			else
			{
				g_dsp_busy = 1;
				return;
			}
		}
	}
}

static struct audio_driver sun_driver = {
      wave_out_write:rdpsnd_queue_write,
      wave_out_open:sun_open,
      wave_out_close:sun_close,
      wave_out_format_supported:sun_format_supported,
      wave_out_set_format:sun_set_format,
      wave_out_volume:sun_volume,
      wave_out_play:sun_play,
      name:"sun",
      description:"SUN/BSD output driver, default device: " DEFAULTDEVICE " or $AUDIODEV",
      next:NULL,
};

struct audio_driver *
sun_register(char *options)
{
	if (options)
	{
		dsp_dev = xstrdup(options);
	}
	else
	{
		dsp_dev = getenv("AUDIODEV");

		if (dsp_dev == NULL)
		{
			dsp_dev = xstrdup(DEFAULTDEVICE);
		}
	}

	return &sun_driver;
}
