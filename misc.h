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
#define DEBUG(args...)  fprintf(stderr, args);
#define ERROR(args...)  fprintf(stderr, "ERROR: "args);
#define WARN(args...)   fprintf(stderr, "WARNING: "args);
#define NOTIMP(args...) fprintf(stderr, "NOT IMPLEMENTED: "args);

#define ROP2_S(rop3) (rop3 & 0xf)
#define ROP2_P(rop3) ((rop3 & 0x3) | ((rop3 & 0x30) >> 2))

#define ROP2_COPY    0xc
#define ROP2_XOR     0x6
#define ROP2_AND     0x8
#define ROP2_OR      0xe

#define MAX_COLOURS 256

typedef struct _colourentry
{
	uint8 blue;
	uint8 green;
	uint8 red;

} COLOURENTRY;

typedef struct _colourmap
{
	uint16 ncolours;
	COLOURENTRY colours[MAX_COLOURS];

} COLOURMAP;

typedef struct _bounds
{
	uint16 left;
	uint16 top;
	uint16 right;
	uint16 bottom;

} BOUNDS;

typedef struct _pen
{
	uint8 style;
	uint8 width;
	uint8 colour;

} PEN;

typedef struct _brush
{
	uint8 xorigin;
	uint8 yorigin;
	uint8 style;
	uint8 pattern[8];

} BRUSH;

typedef struct _font_glyph
{
	uint16 baseline;
	uint16 width;
	uint16 height;
	HBITMAP pixmap;

} FONT_GLYPH;

typedef struct _blob
{
	void *data;
	int size;

} BLOB;
