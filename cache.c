/*
   rdesktop: A Remote Desktop Protocol client.
   Cache routines
   Copyright (C) Matthew Chapman 1999-2002

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
#define TOUCH(id, idx) (g_bmpcache[id][idx].usage = ++g_stamp)
#define IS_PERSISTENT(id) (g_pstcache_fd[id] > 0)

extern int g_pstcache_fd[];
extern BOOL g_use_rdp5;

uint32 g_stamp;

static int g_num_bitmaps_in_memory[3];

/* BITMAP CACHE */
static BMPCACHEENTRY g_bmpcache[3][0xa00];
static HBITMAP g_volatile_bc[3];

/* Remove the least-recently used bitmap from the cache */
void
cache_remove_lru_bitmap(uint8 cache_id)
{
	uint32 i;
	uint16 cache_idx = 0;
	uint32 m = 0xffffffff;
	BMPCACHEENTRY *pbce;

	for (i = 0; i < NUM_ELEMENTS(g_bmpcache[cache_id]); i++)
	{
		if (g_bmpcache[cache_id][i].bitmap && g_bmpcache[cache_id][i].usage < m)
		{
			cache_idx = i;
			m = g_bmpcache[cache_id][i].usage;
		}
	}

	pbce = &g_bmpcache[cache_id][cache_idx];
	ui_destroy_bitmap(pbce->bitmap);
	--g_num_bitmaps_in_memory[cache_id];
	pbce->bitmap = 0;
	pbce->usage = 0;
}

/* Retrieve a bitmap from the cache */
HBITMAP
cache_get_bitmap(uint8 cache_id, uint16 cache_idx)
{
	HBITMAP *pbitmap;

	if ((cache_id < NUM_ELEMENTS(g_bmpcache)) && (cache_idx < NUM_ELEMENTS(g_bmpcache[0])))
	{
		pbitmap = &g_bmpcache[cache_id][cache_idx].bitmap;
		if ((*pbitmap != 0) || pstcache_load_bitmap(cache_id, cache_idx))
		{
			if (IS_PERSISTENT(cache_id))
				TOUCH(cache_id, cache_idx);

			return *pbitmap;
		}
	}
	else if ((cache_id < NUM_ELEMENTS(g_volatile_bc)) && (cache_idx == 0x7fff))
	{
		return g_volatile_bc[cache_id];
	}

	error("get bitmap %d:%d\n", cache_id, cache_idx);
	return NULL;
}

/* Store a bitmap in the cache */
void
cache_put_bitmap(uint8 cache_id, uint16 cache_idx, HBITMAP bitmap, uint32 stamp)
{
	HBITMAP old;

	if ((cache_id < NUM_ELEMENTS(g_bmpcache)) && (cache_idx < NUM_ELEMENTS(g_bmpcache[0])))
	{
		old = g_bmpcache[cache_id][cache_idx].bitmap;
		if (old != NULL)
		{
			ui_destroy_bitmap(old);
		}
		else if (g_use_rdp5)
		{
			if (++g_num_bitmaps_in_memory[cache_id] > BMPCACHE2_C2_CELLS)
				cache_remove_lru_bitmap(cache_id);
		}

		g_bmpcache[cache_id][cache_idx].bitmap = bitmap;
		g_bmpcache[cache_id][cache_idx].usage = stamp;
	}
	else if ((cache_id < NUM_ELEMENTS(g_volatile_bc)) && (cache_idx == 0x7fff))
	{
		old = g_volatile_bc[cache_id];
		if (old != NULL)
			ui_destroy_bitmap(old);
		g_volatile_bc[cache_id] = bitmap;
	}
	else
	{
		error("put bitmap %d:%d\n", cache_id, cache_idx);
	}
}

/* Updates the persistent bitmap cache MRU information on exit */
void
cache_save_state(void)
{
	uint32 id, idx;

	for (id = 0; id < NUM_ELEMENTS(g_bmpcache); id++)
		if (IS_PERSISTENT(id))
			for (idx = 0; idx < NUM_ELEMENTS(g_bmpcache[id]); idx++)
				pstcache_touch_bitmap(id, idx, g_bmpcache[id][idx].usage);
}


/* FONT CACHE */
static FONTGLYPH g_fontcache[12][256];

/* Retrieve a glyph from the font cache */
FONTGLYPH *
cache_get_font(uint8 font, uint16 character)
{
	FONTGLYPH *glyph;

	if ((font < NUM_ELEMENTS(g_fontcache)) && (character < NUM_ELEMENTS(g_fontcache[0])))
	{
		glyph = &g_fontcache[font][character];
		if (glyph->pixmap != NULL)
			return glyph;
	}

	error("get font %d:%d\n", font, character);
	return NULL;
}

/* Store a glyph in the font cache */
void
cache_put_font(uint8 font, uint16 character, uint16 offset,
	       uint16 baseline, uint16 width, uint16 height, HGLYPH pixmap)
{
	FONTGLYPH *glyph;

	if ((font < NUM_ELEMENTS(g_fontcache)) && (character < NUM_ELEMENTS(g_fontcache[0])))
	{
		glyph = &g_fontcache[font][character];
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
		error("put font %d:%d\n", font, character);
	}
}


/* TEXT CACHE */
static DATABLOB g_textcache[256];

/* Retrieve a text item from the cache */
DATABLOB *
cache_get_text(uint8 cache_id)
{
	DATABLOB *text;

	if (cache_id < NUM_ELEMENTS(g_textcache))
	{
		text = &g_textcache[cache_id];
		if (text->data != NULL)
			return text;
	}

	error("get text %d\n", cache_id);
	return NULL;
}

/* Store a text item in the cache */
void
cache_put_text(uint8 cache_id, void *data, int length)
{
	DATABLOB *text;

	if (cache_id < NUM_ELEMENTS(g_textcache))
	{
		text = &g_textcache[cache_id];
		if (text->data != NULL)
			xfree(text->data);

		text->data = xmalloc(length);
		text->size = length;
		memcpy(text->data, data, length);
	}
	else
	{
		error("put text %d\n", cache_id);
	}
}


/* DESKTOP CACHE */
static uint8 g_deskcache[0x38400 * 4];

/* Retrieve desktop data from the cache */
uint8 *
cache_get_desktop(uint32 offset, int cx, int cy, int bytes_per_pixel)
{
	int length = cx * cy * bytes_per_pixel;

	if (offset > sizeof(g_deskcache))
		offset = 0;

	if ((offset + length) <= sizeof(g_deskcache))
	{
		return &g_deskcache[offset];
	}

	error("get desktop %d:%d\n", offset, length);
	return NULL;
}

/* Store desktop data in the cache */
void
cache_put_desktop(uint32 offset, int cx, int cy, int scanline, int bytes_per_pixel, uint8 * data)
{
	int length = cx * cy * bytes_per_pixel;

	if (offset > sizeof(g_deskcache))
		offset = 0;

	if ((offset + length) <= sizeof(g_deskcache))
	{
		cx *= bytes_per_pixel;
		while (cy--)
		{
			memcpy(&g_deskcache[offset], data, cx);
			data += scanline;
			offset += cx;
		}
	}
	else
	{
		error("put desktop %d:%d\n", offset, length);
	}
}


/* CURSOR CACHE */
static HCURSOR g_cursorcache[0x20];

/* Retrieve cursor from cache */
HCURSOR
cache_get_cursor(uint16 cache_idx)
{
	HCURSOR cursor;

	if (cache_idx < NUM_ELEMENTS(g_cursorcache))
	{
		cursor = g_cursorcache[cache_idx];
		if (cursor != NULL)
			return cursor;
	}

	error("get cursor %d\n", cache_idx);
	return NULL;
}

/* Store cursor in cache */
void
cache_put_cursor(uint16 cache_idx, HCURSOR cursor)
{
	HCURSOR old;

	if (cache_idx < NUM_ELEMENTS(g_cursorcache))
	{
		old = g_cursorcache[cache_idx];
		if (old != NULL)
			ui_destroy_cursor(old);

		g_cursorcache[cache_idx] = cursor;
	}
	else
	{
		error("put cursor %d\n", cache_idx);
	}
}
