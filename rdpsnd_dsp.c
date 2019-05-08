/*
   rdesktop: A Remote Desktop Protocol client.
   Sound DSP routines
   Copyright (C) Michael Gernoth <mike@zerfleddert.de> 2006-2008
   Copyright 2017 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

#include <strings.h>

#include "rdesktop.h"
#include "rdpsnd.h"
#include "rdpsnd_dsp.h"

#ifdef HAVE_LIBSAMPLERATE
#include <samplerate.h>

#define SRC_CONVERTER SRC_SINC_MEDIUM_QUALITY
#endif

#define MAX_VOLUME 65535

static uint16 softvol_left = MAX_VOLUME;
static uint16 softvol_right = MAX_VOLUME;
static uint32 resample_to_srate = 44100;
static uint16 resample_to_bitspersample = 16;
static uint16 resample_to_channels = 2;
#ifdef HAVE_LIBSAMPLERATE
static SRC_STATE *src_converter = NULL;
#endif

void
rdpsnd_dsp_softvol_set(uint16 left, uint16 right)
{
	softvol_left = left;
	softvol_right = right;
	logger(Sound, Debug, "rdpsnd_dsp_softvol_set(), left: %u, right: %u\n", left, right);
}

void
rdpsnd_dsp_softvol(unsigned char *buffer, unsigned int size, RD_WAVEFORMATEX * format)
{
	unsigned int factor_left, factor_right;
	unsigned char *posin = buffer;
	unsigned char *posout = buffer;

	if ((softvol_left == MAX_VOLUME) && (softvol_right == MAX_VOLUME))
		return;

	factor_left = (softvol_left * 256) / MAX_VOLUME;
	factor_right = (softvol_right * 256) / MAX_VOLUME;

	if (format->nChannels == 1)
	{
		factor_left = factor_right = (factor_left + factor_right) / 2;
	}

	if (format->wBitsPerSample == 8)
	{
		sint8 val;

		while (posout < buffer + size)
		{
			/* Left */
			val = *posin++;
			val = (val * factor_left) >> 8;
			*posout++ = val;

			/* Right */
			val = *posin++;
			val = (val * factor_right) >> 8;
			*posout++ = val;
		}
	}
	else
	{
		sint16 val;

		while (posout < buffer + size)
		{
			/* Left */
			val = *posin++;
			val |= *posin++ << 8;
			val = (val * factor_left) >> 8;
			*posout++ = val & 0xff;
			*posout++ = val >> 8;

			/* Right */
			val = *posin++;
			val |= *posin++ << 8;
			val = (val * factor_right) >> 8;
			*posout++ = val & 0xff;
			*posout++ = val >> 8;
		}
	}

	logger(Sound, Debug,
	       "rdpsnd_dsp_softvol(), using softvol with factors left: %d, right: %d (%d/%d)",
	       factor_left, factor_right, format->wBitsPerSample, format->nChannels);
}

void
rdpsnd_dsp_swapbytes(unsigned char *buffer, unsigned int size, RD_WAVEFORMATEX * format)
{
	int i;
	uint8 swap;

	if (format->wBitsPerSample == 8)
		return;

	if (size & 0x1)
		logger(Sound, Warning, "rdpsnd_dsp_swapbytes(), badly aligned sound data");

	for (i = 0; i < (int) size; i += 2)
	{
		swap = *(buffer + i);
		*(buffer + i) = *(buffer + i + 1);
		*(buffer + i + 1) = swap;
	}
}

RD_BOOL
rdpsnd_dsp_resample_set(uint32 device_srate, uint16 device_bitspersample, uint16 device_channels)
{
#ifdef HAVE_LIBSAMPLERATE
	int err;
#endif

	if (device_bitspersample != 16 && device_bitspersample != 8)
		return False;

	if (device_channels != 1 && device_channels != 2)
		return False;

	resample_to_srate = device_srate;
	resample_to_bitspersample = device_bitspersample;
	resample_to_channels = device_channels;

#ifdef HAVE_LIBSAMPLERATE
	if (src_converter != NULL)
		src_converter = src_delete(src_converter);

	if ((src_converter = src_new(SRC_CONVERTER, device_channels, &err)) == NULL)
	{
		logger(Sound, Warning, "rdpsnd_dsp_resample_set(), src_new() failed with %d", err);
		return False;
	}
#endif

	return True;
}

RD_BOOL
rdpsnd_dsp_resample_supported(RD_WAVEFORMATEX * format)
{
	if (format->wFormatTag != WAVE_FORMAT_PCM)
		return False;
	if ((format->nChannels != 1) && (format->nChannels != 2))
		return False;
	if ((format->wBitsPerSample != 8) && (format->wBitsPerSample != 16))
		return False;

	return True;
}

STREAM
rdpsnd_dsp_resample(unsigned char *in, unsigned int size,
		    RD_WAVEFORMATEX * format, RD_BOOL stream_be)
{
	UNUSED(stream_be);
#ifdef HAVE_LIBSAMPLERATE
	SRC_DATA resample_data;
	float *infloat, *outfloat;
	int err;
#else
	int ratio1k = (resample_to_srate * 1000) / format->nSamplesPerSec;
#endif
	int innum, outnum;
	unsigned char *tmpdata = NULL, *tmp = NULL;
	int samplewidth = format->wBitsPerSample / 8;
	STREAM out;
	int outsize = 0;
	unsigned char *data;
	int i;

	if ((resample_to_bitspersample == format->wBitsPerSample) &&
	    (resample_to_channels == format->nChannels) &&
	    (resample_to_srate == format->nSamplesPerSec))
		return NULL;

#ifdef B_ENDIAN
	if (!stream_be)
		rdpsnd_dsp_swapbytes(in, size, format);
#endif

	if (resample_to_channels != format->nChannels)
	{
		int newsize = (size / format->nChannels) * resample_to_channels;
		tmpdata = (unsigned char *) xmalloc(newsize);

		for (i = 0; i < newsize / samplewidth; i++)
		{
			if (format->nChannels > resample_to_channels)
				memcpy(tmpdata + (i * samplewidth),
				       in +
				       (((i * format->nChannels) / resample_to_channels) *
					samplewidth), samplewidth);
			else
				memcpy(tmpdata + (i * samplewidth),
				       in +
				       (((i / resample_to_channels) * format->nChannels +
					 (i % format->nChannels)) * samplewidth), samplewidth);

		}

		in = tmpdata;
		size = newsize;
	}


	/* Expand 8-bit input-samples to 16-bit */
#ifndef HAVE_LIBSAMPLERATE	/* libsamplerate needs 16-bit samples */
	if (format->wBitsPerSample != resample_to_bitspersample)
#endif
	{
		/* source: 8 bit, dest: 16 bit */
		if (format->wBitsPerSample == 8)
		{
			tmp = tmpdata;
			tmpdata = (unsigned char *) xmalloc(size * 2);
			for (i = 0; i < (int) size; i++)
			{
				tmpdata[i * 2] = in[i];
				tmpdata[(i * 2) + 1] = 0x00;
			}
			in = tmpdata;
			samplewidth = 16 / 2;
			size *= 2;

			if (tmp != NULL)
				xfree(tmp);
		}
	}

	innum = size / samplewidth;

	/* Do the resampling */
#ifdef HAVE_LIBSAMPLERATE
	if (src_converter == NULL)
	{
		logger(Sound, Warning,
		       "rdpsndp_dsp_resample_set(), no sample rate converter available");
		return NULL;
	}

	outnum = ((float) innum * ((float) resample_to_srate / (float) format->nSamplesPerSec)) + 1;

	infloat = (float *) xmalloc(sizeof(float) * innum);
	outfloat = (float *) xmalloc(sizeof(float) * outnum);

	src_short_to_float_array((short *) in, infloat, innum);

	bzero(&resample_data, sizeof(resample_data));
	resample_data.data_in = infloat;
	resample_data.data_out = outfloat;
	resample_data.input_frames = innum / resample_to_channels;
	resample_data.output_frames = outnum / resample_to_channels;
	resample_data.src_ratio = (double) resample_to_srate / (double) format->nSamplesPerSec;
	resample_data.end_of_input = 0;

	if ((err = src_process(src_converter, &resample_data)) != 0)
		logger(Sound, Warning, "rdpsnd_dsp_resample_set(), src_process(): '%s'",
		       src_strerror(err));

	xfree(infloat);

	outsize = resample_data.output_frames_gen * resample_to_channels * samplewidth;
	out = s_alloc(outsize);
	out_uint8p(out, data, outsize);
	src_float_to_short_array(outfloat, (short *) data,
				 resample_data.output_frames_gen * resample_to_channels);
	xfree(outfloat);

#else
	/* Michaels simple linear resampler */
	if (resample_to_srate < format->nSamplesPerSec)
	{
		logger(Sound, Warning,
		       "rdpsnd_dsp_reasmple_set(), downsampling currently not supported");
		return 0;
	}

	outnum = (innum * ratio1k) / 1000;

	outsize = outnum * samplewidth;
	out = s_alloc(outsize);
	out_uint8p(out, data, outsize);
	bzero(data, outsize);

	for (i = 0; i < outsize / (resample_to_channels * samplewidth); i++)
	{
		int source = (i * 1000) / ratio1k;
#if 0				/* Partial for linear resampler */
		int part = (i * 100000) / ratio1k - source * 100;
#endif
		int j;

		if (source * resample_to_channels + samplewidth > (int) size)
			break;

#if 0				/* Linear resampling, TODO: soundquality fixes (LP filter) */
		if (samplewidth == 1)
		{
			sint8 cval1, cval2;
			for (j = 0; j < resample_to_channels; j++)
			{
				memcpy(&cval1,
				       in + (source * resample_to_channels * samplewidth) +
				       (samplewidth * j), samplewidth);
				memcpy(&cval2,
				       in + ((source + 1) * resample_to_channels * samplewidth) +
				       (samplewidth * j), samplewidth);

				cval1 += (sint8) (cval2 * part) / 100;

				memcpy(data + (i * resample_to_channels * samplewidth) +
				       (samplewidth * j), &cval1, samplewidth);
			}
		}
		else
		{
			sint16 sval1, sval2;
			for (j = 0; j < resample_to_channels; j++)
			{
				memcpy(&sval1,
				       in + (source * resample_to_channels * samplewidth) +
				       (samplewidth * j), samplewidth);
				memcpy(&sval2,
				       in + ((source + 1) * resample_to_channels * samplewidth) +
				       (samplewidth * j), samplewidth);

				sval1 += (sint16) (sval2 * part) / 100;

				memcpy(data + (i * resample_to_channels * samplewidth) +
				       (samplewidth * j), &sval1, samplewidth);
			}
		}
#else /* Nearest neighbor search */
		for (j = 0; j < resample_to_channels; j++)
		{
			memcpy(out + (i * resample_to_channels * samplewidth) + (samplewidth * j),
			       in + (source * resample_to_channels * samplewidth) +
			       (samplewidth * j), samplewidth);
		}
#endif
	}
	outsize = i * resample_to_channels * samplewidth;
#endif

	if (tmpdata != NULL)
		xfree(tmpdata);

	/* Shrink 16-bit output-samples to 8-bit */
#ifndef HAVE_LIBSAMPLERATE	/* libsamplerate produces 16-bit samples */
	if (format->wBitsPerSample != resample_to_bitspersample)
#endif
	{
		/* source: 16 bit, dest: 8 bit */
		if (resample_to_bitspersample == 8)
		{
			for (i = 0; i < outsize; i++)
			{
				data[i] = data[i * 2];
			}
			outsize /= 2;
		}
	}

#ifdef B_ENDIAN
	if (!stream_be)
		rdpsnd_dsp_swapbytes(data, outsize, format);
#endif

	return out;
}

STREAM
rdpsnd_dsp_process(unsigned char *data, unsigned int size, struct audio_driver * current_driver,
		   RD_WAVEFORMATEX * format)
{
	STREAM out;
	RD_BOOL stream_be = False;

	/* softvol and byteswap do not change the amount of data they
	   return, so they can operate on the input-stream */
	if (current_driver->wave_out_volume == rdpsnd_dsp_softvol_set)
		rdpsnd_dsp_softvol(data, size, format);

#ifdef B_ENDIAN
	if (current_driver->need_byteswap_on_be)
	{
		rdpsnd_dsp_swapbytes(data, size, format);
		stream_be = True;
	}
#endif

	out = NULL;

	if (current_driver->need_resampling)
		out = rdpsnd_dsp_resample(data, size, format, stream_be);

	if (out == NULL)
	{
		out = s_alloc(size);
		out_uint8a(out, data, size);
	}

	s_mark_end(out);
	s_seek(out, 0);

	return out;
}
