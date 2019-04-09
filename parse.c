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

#include <stdlib.h>
#include "rdesktop.h"

#include "parse.h"

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
