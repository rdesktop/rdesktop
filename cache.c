/*
   rdesktop: A Remote Desktop Protocol client.
   Cache routines
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

#include "rdesktop.h"

#define NUM_ELEMENTS(array) (sizeof(array) / sizeof(array[0]))


/* BITMAP CACHE */
static HBITMAP bmpcache[3][600];

/* Retrieve a bitmap from the cache */
HBITMAP cache_get_bitmap(uint8 cache_id, uint16 cache_idx)
{
	HBITMAP bitmap;

	if ((cache_id < NUM_ELEMENTS(bmpcache))
	    && (cache_idx < NUM_ELEMENTS(bmpcache[0])))
	{
		bitmap = bmpcache[cache_id][cache_idx];
		if (bitmap != NULL)
			return bitmap;
	}

	ERROR("get bitmap %d:%d\n", cache_id, cache_idx);
	return NULL;
}

/* Store a bitmap in the cache */
void cache_put_bitmap(uint8 cache_id, uint16 cache_idx, HBITMAP bitmap)
{
	HBITMAP old;

	if ((cache_id < NUM_ELEMENTS(bmpcache))
	    && (cache_idx < NUM_ELEMENTS(bmpcache[0])))
	{
		old = bmpcache[cache_id][cache_idx];
		if (old != NULL)
			ui_destroy_bitmap(old);

		bmpcache[cache_id][cache_idx] = bitmap;
	}
	else
	{
		ERROR("put bitmap %d:%d\n", cache_id, cache_idx);
	}
}


/* FONT CACHE */
static FONTGLYPH fontcache[12][256];

/* Retrieve a glyph from the font cache */
FONTGLYPH *cache_get_font(uint8 font, uint16 character)
{
	FONTGLYPH *glyph;

	if ((font < NUM_ELEMENTS(fontcache))
	    && (character < NUM_ELEMENTS(fontcache[0])))
	{
		glyph = &fontcache[font][character];
		if (glyph->pixmap != NULL)
			return glyph;
	}

	ERROR("get font %d:%d\n", font, character);
	return NULL;
}

/* Store a glyph in the font cache */
void cache_put_font(uint8 font, uint16 character, uint16 offset,
		    uint16 baseline, uint16 width, uint16 height,
		    HGLYPH pixmap)
{
	FONTGLYPH *glyph;

	if ((font < NUM_ELEMENTS(fontcache))
	    && (character < NUM_ELEMENTS(fontcache[0])))
	{
		glyph = &fontcache[font][character];
		if (glyph->pixmap != NULL)
			ui_destroy_glyph(glyph->pixmap);

		glyph->offset = offset;
		glyph->baseline = baseline;
		glyph->width = width;
		glyph->height = height;
		glyph->pixmap = pixmap;
	}
	else
	{
		ERROR("put font %d:%d\n", font, character);
	}
}


/* TEXT CACHE */
static DATABLOB textcache[256];

/* Retrieve a text item from the cache */
DATABLOB *cache_get_text(uint8 cache_id)
{
	DATABLOB *text;

	if (cache_id < NUM_ELEMENTS(textcache))
	{
		text = &textcache[cache_id];
		if (text->data != NULL)
			return text;
	}

	ERROR("get text %d\n", cache_id);
	return NULL;
}

/* Store a text item in the cache */
void cache_put_text(uint8 cache_id, void *data, int length)
{
	DATABLOB *text;

	if (cache_id < NUM_ELEMENTS(textcache))
	{
		text = &textcache[cache_id];
		if (text->data != NULL)
			xfree(text->data);

		text->data = xmalloc(length);
		text->size = length;
		memcpy(text->data, data, length);
	}
	else
	{
		ERROR("put text %d\n", cache_id);
	}
}


/* DESKTOP CACHE */
static uint8 deskcache[0x38400];

/* Retrieve desktop data from the cache */
uint8 *cache_get_desktop(uint32 offset, int cx, int cy)
{
	int length = cx * cy;

	if ((offset + length) <= sizeof(deskcache))
	{
		return &deskcache[offset];
	}

	ERROR("get desktop %d:%d\n", offset, length);
	return NULL;
}

/* Store desktop data in the cache */
void cache_put_desktop(uint32 offset, int cx, int cy, int scanline,
		       uint8 *data)
{
	int length = cx * cy;

	if ((offset + length) <= sizeof(deskcache))
	{
		while (cy--)
		{
			memcpy(&deskcache[offset], data, cx);
			data += scanline;
			offset += cx;
		}
	}
	else
	{
		ERROR("put desktop %d:%d\n", offset, length);
	}
}
