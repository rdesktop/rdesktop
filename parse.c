/*
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - parsing layer
   Copyright (C) Matthew Chapman 1999-2000
   
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

#include "includes.h"

/* Parse a 8-bit integer */
BOOL prs_io_uint8(STREAM s, uint8 *i)
{
	if (s->offset + 1 > s->end)
	{
		ERROR("Parse past end of buffer\n");
		return False;
	}

	if (s->marshall)
		s->data[s->offset] = *i;
	else
		*i = s->data[s->offset];

	s->offset++;
	return True;
}

/* Parse a sequence of 8-bit integers */
BOOL prs_io_uint8s(STREAM s, uint8 *p, unsigned int length)
{
	if (s->offset + length > s->end)
	{
		ERROR("Parse past end of buffer\n");
		return False;
	}

	if (s->marshall)
		memcpy(s->data + s->offset, p, length);
	else
		memcpy(p, s->data + s->offset, length);

	s->offset += length;
	return True;
}

/* Parse a 16-bit integer, most significant bytes first */
BOOL msb_io_uint16(STREAM s, uint16 *i)
{
	int offset = s->offset;

	if (offset + 2 > s->end)
	{
		ERROR("Parse past end of buffer\n");
		return False;
	}

	if (s->marshall) {
		s->data[offset] = (uint8)(*i >> 8);
		s->data[offset+1] = (uint8)(*i);
	} else {
		*i = (s->data[offset] << 8) + (s->data[offset+1]);
	}

	s->offset+=2;
	return True;
}

/* Parse a 16-bit integer, least significant bytes first */
BOOL lsb_io_uint16(STREAM s, uint16 *i)
{
	int offset = s->offset;

	if (offset + 2 > s->end)
	{
		ERROR("Parse past end of buffer\n");
		return False;
	}

	if (s->marshall) {
		s->data[offset] = (uint8)(*i);
		s->data[offset+1] = (uint8)(*i >> 8);
	} else {
		*i = (s->data[offset]) + (s->data[offset+1] << 8);
	}

	s->offset += 2;
	return True;
}

/* Parse a 32-bit integer, least significant bytes first */
BOOL lsb_io_uint32(STREAM s, uint32 *i)
{
	int offset = s->offset;

	if (offset + 4 > s->end)
	{
		ERROR("Parse past end of buffer\n");
		return False;
	}

	if (s->marshall) {
		s->data[offset] = (uint8)(*i);
		s->data[offset+1] = (uint8)(*i >> 8);
		s->data[offset+2] = (uint8)(*i >> 16);
		s->data[offset+3] = (uint8)(*i >> 24);
	} else {
		*i = (s->data[offset]) + (s->data[offset+1] << 8)
		     + (s->data[offset+2] << 16) + (s->data[offset+3] << 24);
	}

	s->offset += 4;
	return True;
}
