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

#include "includes.h"

HBITMAP cache_get_bitmap(HCONN conn, uint8 cache_id, uint16 cache_idx)
{
	HBITMAP bitmap;

	if ((cache_id < NUM_ELEMENTS(conn->bmpcache))
			&& (cache_idx < NUM_ELEMENTS(conn->bmpcache[0])))
	{
		bitmap = conn->bmpcache[cache_id][cache_idx];
		if (bitmap != NULL)
			return bitmap;
	}

	ERROR("Bitmap %d:%d not found\n", cache_id, cache_idx);
	return NULL;
}

void cache_put_bitmap(HCONN conn, uint8 cache_id, uint16 cache_idx, HBITMAP bitmap)
{
	HBITMAP old;

	if ((cache_id < NUM_ELEMENTS(conn->bmpcache))
			&& (cache_idx < NUM_ELEMENTS(conn->bmpcache[0])))
	{
		old = conn->bmpcache[cache_id][cache_idx];
		if (old != NULL)
			ui_destroy_bitmap(conn->wnd, old);

		conn->bmpcache[cache_id][cache_idx] = bitmap;
	}
	else
	{
		ERROR("Bitmap %d:%d past end of cache\n", cache_id, cache_idx);
	}
}

FONT_GLYPH *cache_get_font(HCONN conn, uint8 font, uint16 character)
{
	FONT_GLYPH *glyph;

	if ((font < NUM_ELEMENTS(conn->fontcache))
			&& (character < NUM_ELEMENTS(conn->fontcache[0])))
	{
		glyph = &conn->fontcache[font][character];
		if (glyph->pixmap != NULL)
			return glyph;
	}

	ERROR("Font %d character %d not found\n", font, character);
	return NULL;
}

void cache_put_font(HCONN conn, uint8 font, uint32 character, uint16 baseline,
		    uint16 width, uint16 height, HGLYPH pixmap)
{
	FONT_GLYPH *glyph;

	if ((font < NUM_ELEMENTS(conn->fontcache))
			&& (character < NUM_ELEMENTS(conn->fontcache[0])))
	{
		glyph = &conn->fontcache[font][character];
		if (glyph->pixmap != NULL)
			ui_destroy_glyph(conn->wnd, glyph->pixmap);

		glyph->baseline = baseline;
		glyph->width = width;
		glyph->height = height;
		glyph->pixmap = pixmap;
	}
	else
	{
		ERROR("Font %d character %d past end of cache\n",
		      font, character);
	}
}

BLOB *cache_get_text(HCONN conn, uint8 cache_id)
{
	BLOB *text;

	if (cache_id < NUM_ELEMENTS(conn->textcache))
	{
		text = &conn->textcache[cache_id];
		if (text->data != NULL)
			return text;
	}

	ERROR("Text cache id %d not found\n", cache_id);
	return NULL;
}

void cache_put_text(HCONN conn, uint8 cache_id, void *data, int length)
{
	BLOB *text;

	if (cache_id < NUM_ELEMENTS(conn->textcache))
	{
		text = &conn->textcache[cache_id];
		if (text->data != NULL)
			free(text->data);

		text->data = malloc(length);
		text->size = length;
		memcpy(text->data, data, length);
	}
	else
	{
		ERROR("Text cache id %d past end of cache\n", cache_id);
	}
}
