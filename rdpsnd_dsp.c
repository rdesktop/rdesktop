/*
   rdesktop: A Remote Desktop Protocol client.
   Sound DSP routines
   Copyright (C) Michael Gernoth 2006

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

#define MAX_VOLUME 65535

static uint16 softvol_left = MAX_VOLUME;
static uint16 softvol_right = MAX_VOLUME;

void
rdpsnd_dsp_softvol_set(uint16 left, uint16 right)
{
	softvol_left = left;
	softvol_right = right;
	DEBUG(("rdpsnd_dsp_softvol_set: left: %u, right: %u\n", left, right));
}

void
rdpsnd_dsp_softvol(unsigned char *buffer, unsigned int size, WAVEFORMATEX * format)
{
	unsigned int factor_left, factor_right;
	unsigned char *posin = buffer;
	unsigned char *posout = buffer;

	if ((softvol_left == MAX_VOLUME) && (softvol_right == MAX_VOLUME))
		return;

	factor_left = (softvol_left * 256) / 65535;
	factor_right = (softvol_right * 256) / 65535;

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

	DEBUG(("using softvol with factors left: %d, right: %d (%d/%d)\n", factor_left,
	       factor_right, format->wBitsPerSample, format->nChannels));
}

void
rdpsnd_dsp_swapbytes(unsigned char *buffer, unsigned int size, WAVEFORMATEX * format)
{
	int i;
	uint8 swap;

	if (format->wBitsPerSample == 8)
		return;

	for (i = 0; i < size; i += 2)
	{
		swap = *(buffer + i);
		*(buffer + i) = *(buffer + i + 1);
		*(buffer + i + 1) = swap;
	}
}


STREAM
rdpsnd_dsp_process(STREAM s, struct audio_driver *current_driver, WAVEFORMATEX * format)
{
	static struct stream out;

	/* softvol and byteswap do not change the amount of data they
	   return, so they can operate on the input-stream */
	if (current_driver->wave_out_volume == rdpsnd_dsp_softvol_set)
		rdpsnd_dsp_softvol(s->data, s->size, format);

#ifdef B_ENDIAN
	if (current_driver->need_byteswap_on_be)
		rdpsnd_dsp_swapbytes(s->data, s->size, format);
#endif

	out.data = xmalloc(s->size);

	memcpy(out.data, s->data, s->size);

	out.size = s->size;
	out.p = out.data;
	out.end = out.p + out.size;

	return &out;
}
