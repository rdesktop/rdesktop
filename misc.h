/*
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - RDP layer
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

#define NUM_ELEMENTS(array) (sizeof(array) / sizeof(array[0]))

#define MAX_COLORS 256

typedef struct _colorentry
{
	uint8 red;
	uint8 green;
	uint8 blue;

} COLORENTRY;

typedef struct _colormap
{
	uint16 ncolors;
	COLORENTRY colors[MAX_COLORS];

} COLORMAP;
