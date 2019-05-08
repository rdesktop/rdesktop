/*
   rdesktop: A Remote Desktop Protocol client.
   Parsing primitives
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

#include <errno.h>
#include <iconv.h>
#include <stdlib.h>

#include "rdesktop.h"

extern char g_codepage[16];

STREAM
s_alloc(unsigned int size)
{
	STREAM s;

	s = xmalloc(sizeof(struct stream));
	memset(s, 0, sizeof(struct stream));
	s_realloc(s, size);

	return s;
}

STREAM
s_inherit(unsigned char *data, unsigned int size)
{
	STREAM s;

	s = xmalloc(sizeof(struct stream));
	memset(s, 0, sizeof(struct stream));
	s->p = s->data = data;
	s->size = size;

	return s;
}

void
s_realloc(STREAM s, unsigned int size)
{
	unsigned char *data;

	if (s->size >= size)
		return;

	data = s->data;
	s->size = size;
	s->data = xrealloc(data, size);
	s->p = s->data + (s->p - data);
	s->end = s->data + (s->end - data);
	s->iso_hdr = s->data + (s->iso_hdr - data);
	s->mcs_hdr = s->data + (s->mcs_hdr - data);
	s->sec_hdr = s->data + (s->sec_hdr - data);
	s->rdp_hdr = s->data + (s->rdp_hdr - data);
	s->channel_hdr = s->data + (s->channel_hdr - data);
}

void
s_reset(STREAM s)
{
	struct stream tmp;
	tmp = *s;
	memset(s, 0, sizeof(struct stream));
	s->size = tmp.size;
	s->end = s->p = s->data = tmp.data;
}


void
s_free(STREAM s)
{
	if (s == NULL)
		return;
	free(s->data);
	free(s);
}

static iconv_t
local_to_utf16()
{
	iconv_t icv;
	icv = iconv_open(WINDOWS_CODEPAGE, g_codepage);
	if (icv == (iconv_t) - 1)
	{
		logger(Core, Error, "locale_to_utf16(), iconv_open[%s -> %s] fail %p",
		       g_codepage, WINDOWS_CODEPAGE, icv);
		abort();
	}
	return icv;
}

/* Writes a utf16 encoded string into stream excluding null termination.
   This function assumes that input is ASCII compatible, such as UTF-8.
 */
static inline size_t
_out_utf16s(STREAM s, size_t maxlength, const char *string)
{
	static iconv_t icv_local_to_utf16;
	size_t bl, ibl, obl;
	const char *pin;
	char *pout;

	if (string == NULL)
		return 0;

	if (!icv_local_to_utf16)
	{
		icv_local_to_utf16 = local_to_utf16();
	}

	ibl = strlen(string);
	obl = maxlength ? maxlength : (size_t) s_left(s);
	pin = string;
	pout = (char *) s->p;

	if (iconv(icv_local_to_utf16, (char **) &pin, &ibl, &pout, &obl) == (size_t) - 1)
	{
		logger(Protocol, Error, "out_utf16s(), iconv(2) fail, errno %d", errno);
		abort();
	}

	bl = (unsigned char *) pout - s->p;

	s->p = (unsigned char *) pout;

	return bl;
}

/* Writes a utf16 encoded string into stream including a null
   termination. The length is fixed and defined by width. If result is
   longer than specified width, the string is truncated. pad,
   specifies a character to pad with.
*/
void
out_utf16s_padded(STREAM s, const char *string, size_t length, unsigned char pad)
{
	size_t i, bl;
	bl = _out_utf16s(s, length - 2, string);

	// append utf16 null termination
	out_uint16(s, 0);
	bl += 2;

	for (i = 0; i < (length - bl); i++)
		out_uint8(s, pad);
}

/* Writes a utf16 encoded string into stream including a null
   termination.
*/
void
out_utf16s(STREAM s, const char *string)
{
	_out_utf16s(s, 0, string);

	// append utf16 null termination
	out_uint16(s, 0);
}


/* Writes a utf16 encoded string into stream excluding null
   termination. */
void
out_utf16s_no_eos(STREAM s, const char *string)
{
	_out_utf16s(s, 0, string);
}

/* Read bytes from STREAM s into *string until a null terminator is
   found, or len bytes are read from the stream. Returns the number of
   bytes read. */
size_t
in_ansi_string(STREAM s, char *string, size_t len)
{
	char *ps;
	size_t left;
	ps = string;

	left = len;
	while (left--)
	{
		if (left == 0)
			break;

		in_uint8(s, *ps);

		if (*ps == '\0')
			break;

		ps++;
	}

	return len - left;
}
