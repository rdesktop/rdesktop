/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - SGI/IRIX
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 2003-2008
   Copyright (C) GuoJunBo <guojunbo@ict.ac.cn> 2003
   Copyright (C) Jeremy Meng <void.foo@gmail.com> 2004-2005

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
#include <errno.h>
#include <dmedia/audio.h>
#include <unistd.h>

/* #define IRIX_DEBUG 1 */

#define IRIX_MAX_VOL     65535

ALconfig audioconfig;
ALport output_port;

static int g_snd_rate;
static int width = AL_SAMPLE_16;
static char *sgi_output_device = NULL;

double min_volume, max_volume, volume_range;
int resource, maxFillable;
int combinedFrameSize;

void sgi_play(void);

void
sgi_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv)
{
	/* We need to be called rather often... */
	if (output_port != (ALport) 0 && !rdpsnd_queue_empty())
		FD_SET(0, wfds);
}

void
sgi_check_fds(fd_set * rfds, fd_set * wfds)
{
	if (output_port == (ALport) 0)
		return;

	if (!rdpsnd_queue_empty())
		sgi_play();
}

RD_BOOL
sgi_open(void)
{
	ALparamInfo pinfo;
	static int warned = 0;

#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_open: begin\n");
#endif

	if (!warned && sgi_output_device)
	{
		warning("device-options not supported for libao-driver\n");
		warned = 1;
	}

	if (alGetParamInfo(AL_DEFAULT_OUTPUT, AL_GAIN, &pinfo) < 0)
	{
		fprintf(stderr, "sgi_open: alGetParamInfo failed: %s\n",
			alGetErrorString(oserror()));
	}
	min_volume = alFixedToDouble(pinfo.min.ll);
	max_volume = alFixedToDouble(pinfo.max.ll);
	volume_range = (max_volume - min_volume);
#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_open: minvol = %lf, maxvol= %lf, range = %lf.\n",
		min_volume, max_volume, volume_range);
#endif

	audioconfig = alNewConfig();
	if (audioconfig == (ALconfig) 0)
	{
		fprintf(stderr, "sgi_open: alNewConfig failed: %s\n", alGetErrorString(oserror()));
		return False;
	}

	output_port = alOpenPort("rdpsnd", "w", 0);
	if (output_port == (ALport) 0)
	{
		fprintf(stderr, "sgi_open: alOpenPort failed: %s\n", alGetErrorString(oserror()));
		return False;
	}

#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_open: returning\n");
#endif
	return True;
}

void
sgi_close(void)
{
	/* Ack all remaining packets */
#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_close: begin\n");
#endif

	while (!rdpsnd_queue_empty())
		rdpsnd_queue_next(0);
	alDiscardFrames(output_port, 0);

	alClosePort(output_port);
	output_port = (ALport) 0;
	alFreeConfig(audioconfig);
#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_close: returning\n");
#endif
}

RD_BOOL
sgi_format_supported(RD_WAVEFORMATEX * pwfx)
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
sgi_set_format(RD_WAVEFORMATEX * pwfx)
{
	int channels;
	int frameSize, channelCount;
	ALpv params;

#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_set_format: init...\n");
#endif

	if (pwfx->wBitsPerSample == 8)
		width = AL_SAMPLE_8;
	else if (pwfx->wBitsPerSample == 16)
		width = AL_SAMPLE_16;

	/* Limited support to configure an opened audio port in IRIX.  The
	   number of channels is a static setting and can not be changed after
	   a port is opened.  So if the number of channels remains the same, we
	   can configure other settings; otherwise we have to reopen the audio
	   port, using same config. */

	channels = pwfx->nChannels;
	g_snd_rate = pwfx->nSamplesPerSec;

	alSetSampFmt(audioconfig, AL_SAMPFMT_TWOSCOMP);
	alSetWidth(audioconfig, width);
	if (channels != alGetChannels(audioconfig))
	{
		alClosePort(output_port);
		alSetChannels(audioconfig, channels);
		output_port = alOpenPort("rdpsnd", "w", audioconfig);

		if (output_port == (ALport) 0)
		{
			fprintf(stderr, "sgi_set_format: alOpenPort failed: %s\n",
				alGetErrorString(oserror()));
			return False;
		}

	}

	resource = alGetResource(output_port);
	maxFillable = alGetFillable(output_port);
	channelCount = alGetChannels(audioconfig);
	frameSize = alGetWidth(audioconfig);

	if (frameSize == 0 || channelCount == 0)
	{
		fprintf(stderr, "sgi_set_format: bad frameSize or channelCount\n");
		return False;
	}
	combinedFrameSize = frameSize * channelCount;

	params.param = AL_RATE;
	params.value.ll = (long long) g_snd_rate << 32;

	if (alSetParams(resource, &params, 1) < 0)
	{
		fprintf(stderr, "wave_set_format: alSetParams failed: %s\n",
			alGetErrorString(oserror()));
		return False;
	}
	if (params.sizeOut < 0)
	{
		fprintf(stderr, "wave_set_format: invalid rate %d\n", g_snd_rate);
		return False;
	}

#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_set_format: returning...\n");
#endif
	return True;
}

void
sgi_volume(uint16 left, uint16 right)
{
	double gainleft, gainright;
	ALpv pv[1];
	ALfixed gain[8];

#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_volume: begin\n");
	fprintf(stderr, "left='%d', right='%d'\n", left, right);
#endif

	gainleft = (double) left / IRIX_MAX_VOL;
	gainright = (double) right / IRIX_MAX_VOL;

	gain[0] = alDoubleToFixed(min_volume + gainleft * volume_range);
	gain[1] = alDoubleToFixed(min_volume + gainright * volume_range);

	pv[0].param = AL_GAIN;
	pv[0].value.ptr = gain;
	pv[0].sizeIn = 8;
	if (alSetParams(AL_DEFAULT_OUTPUT, pv, 1) < 0)
	{
		fprintf(stderr, "sgi_volume: alSetParams failed: %s\n",
			alGetErrorString(oserror()));
		return;
	}

#if (defined(IRIX_DEBUG))
	fprintf(stderr, "sgi_volume: returning\n");
#endif
}

void
sgi_play(void)
{
	struct audio_packet *packet;
	ssize_t len;
	STREAM out;
	int gf;

	while (1)
	{
		if (rdpsnd_queue_empty())
			return;

		packet = rdpsnd_queue_current_packet();
		out = (STREAM)(void *)&(packet->s);

		len = out->end - out->p;

		alWriteFrames(output_port, out->p, len / combinedFrameSize);

		out->p += len;
		if (out->p == out->end)
		{
			gf = alGetFilled(output_port);
			if (gf < (4 * maxFillable / 10))
			{
				rdpsnd_queue_next(0);
			}
			else
			{
#if (defined(IRIX_DEBUG))
/*  				fprintf(stderr,"Busy playing...\n"); */
#endif
				usleep(10);
				return;
			}
		}
	}
}

struct audio_driver *
sgi_register(char *options)
{
	static struct audio_driver sgi_driver;

	memset(&sgi_driver, 0, sizeof(sgi_driver));

	sgi_driver.name = "sgi";
	sgi_driver.description = "SGI output driver";

	sgi_driver.add_fds = sgi_add_fds;
	sgi_driver.check_fds = sgi_check_fds;

	sgi_driver.wave_out_open = sgi_open;
	sgi_driver.wave_out_close = sgi_close;
	sgi_driver.wave_out_format_supported = sgi_format_supported;
	sgi_driver.wave_out_set_format = sgi_set_format;
	sgi_driver.wave_out_volume = sgi_volume;

	sgi_driver.need_byteswap_on_be = 1;
	sgi_driver.need_resampling = 0;

	if (options)
	{
		sgi_output_device = xstrdup(options);
	}
	return &sgi_driver;
}
