/*
   rdesktop: A Remote Desktop Protocol client.
   Entrypoint and utility functions
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

void *xmalloc(int size)
{
	void *mem = malloc(size);
	if (mem == NULL)
	{
		fprintf(stderr, "xmalloc: Out of memory.\n");
		exit(1);
	}
	return mem;
}

void *xrealloc(void *oldmem, int size)
{
	void *mem = realloc(oldmem, size);
	if (mem == NULL)
	{
		fprintf(stderr, "xrealloc: Out of memory.\n");
		exit(1);
	}
	return mem;
}

void dump_data(unsigned char *p, int len)
{
	unsigned char *line = p;
	int i, j;

	fprintf(stderr, "0000 ");

	for (i = 0; i < len; i++)
	{
		if ((i & 15) == 0)
		{
			if (i != 0)
			{
				for (j = 0; j < 16; j++)
					fputc(isprint(line[j]) ? line[j] : '.',
					      stderr);
				line = p + i;
				fprintf(stderr, "\n%04x ", line-p);
			}
		}

		fprintf(stderr, "%02X ", p[i]);
	}

	fputc('\n', stderr);
}
