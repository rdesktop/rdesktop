/*
   rdesktop: A Remote Desktop Protocol client.
   Persistent Bitmap Cache routines
   Copyright (C) Jeroen Meijer 2004

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

#define MAX_CELL_SIZE		0x1000			/* pixels */

#define IS_PERSISTENT(id) (g_pstcache_fd[id] > 0)

extern int g_server_bpp;
extern uint32 g_stamp;
extern BOOL g_bitmap_cache;
extern BOOL g_bitmap_cache_persist_enable;
extern BOOL g_bitmap_cache_precache;

int g_pstcache_fd[8];
int g_pstcache_Bpp;
BOOL g_pstcache_enumerated = False;
uint8 zero_id[] = {0, 0, 0, 0, 0, 0, 0, 0};


/* Update usage info for a bitmap */
void
pstcache_touch_bitmap(uint8 cache_id, uint16 cache_idx, uint32 stamp)
{
	int fd;

	if (!IS_PERSISTENT(cache_id))
		return;

	fd = g_pstcache_fd[cache_id];
	rd_lseek_file(fd, 12 + cache_idx * (g_pstcache_Bpp * MAX_CELL_SIZE + sizeof(CELLHEADER)));
	rd_write_file(fd, &stamp, sizeof(stamp));
}

/* Load a bitmap from the persistent cache */
BOOL
pstcache_load_bitmap(uint8 cache_id, uint16 cache_idx)
{
	uint8 *celldata;
	int fd;
	CELLHEADER cellhdr;
	HBITMAP bitmap;

	if (!(g_bitmap_cache_persist_enable && IS_PERSISTENT(cache_id)))
		return False;

	fd = g_pstcache_fd[cache_id];
	rd_lseek_file(fd, cache_idx * (g_pstcache_Bpp * MAX_CELL_SIZE + sizeof(CELLHEADER)));
	rd_read_file(fd, &cellhdr, sizeof(CELLHEADER));
	celldata = (uint8 *)xmalloc(cellhdr.length);
	rd_read_file(fd, celldata, cellhdr.length);

	DEBUG(("Loading bitmap from disk (%d:%d)\n", cache_id, cache_idx));

	bitmap = ui_create_bitmap(cellhdr.width, cellhdr.height, celldata);
	cache_put_bitmap(cache_id, cache_idx, bitmap, cellhdr.stamp);

	xfree(celldata);
	return True;
}

/* Store a bitmap in the persistent cache */
BOOL
pstcache_put_bitmap(uint8 cache_id, uint16 cache_idx, uint8 *bitmap_id,
		uint16 width, uint16 height, uint16 length, uint8 *data)
{
	int fd;
	CELLHEADER cellhdr;

	if (!IS_PERSISTENT(cache_id))
		return False;

	memcpy(cellhdr.bitmap_id, bitmap_id, sizeof(BITMAP_ID));
	cellhdr.width = width;
	cellhdr.height = height;
	cellhdr.length = length;
	cellhdr.stamp = 0;

	fd = g_pstcache_fd[cache_id];
	rd_lseek_file(fd, cache_idx * (g_pstcache_Bpp * MAX_CELL_SIZE + sizeof(CELLHEADER)));
	rd_write_file(fd, &cellhdr, sizeof(CELLHEADER));
	rd_write_file(fd, data, length);

	return True;
}

/* list the bitmaps from the persistent cache file */
int
pstcache_enumerate(uint8 cache_id, uint8 *idlist)
{
	int fd, n, c = 0;
	CELLHEADER cellhdr;

	if (!(g_bitmap_cache && g_bitmap_cache_persist_enable && IS_PERSISTENT(cache_id)))
		return 0;

	/* The server disconnects if the bitmap cache content is sent more than once */
	if (g_pstcache_enumerated)
		return 0;

	DEBUG(("pstcache enumeration... "));
	for (n = 0; n < BMPCACHE2_NUM_PSTCELLS; n++)
	{
		fd = g_pstcache_fd[cache_id];
		rd_lseek_file(fd, n * (g_pstcache_Bpp * MAX_CELL_SIZE + sizeof(CELLHEADER)));
		if (rd_read_file(fd, &cellhdr, sizeof(CELLHEADER)) <= 0)
			break;

		if (memcmp(cellhdr.bitmap_id, zero_id, sizeof(BITMAP_ID)) != 0)
		{
			memcpy(idlist + n * sizeof(BITMAP_ID), cellhdr.bitmap_id,
					sizeof(BITMAP_ID));

			if (cellhdr.stamp)
			{
				/* Pre-caching is not possible with 8bpp because a colourmap
				 * is needed to load them */
				if (g_bitmap_cache_precache && (g_server_bpp > 8))
				{
					if (pstcache_load_bitmap(cache_id, n))
						c++;
				}

				g_stamp = MAX(g_stamp, cellhdr.stamp);
			}
		}
		else
		{
			break;
		}
	}

	DEBUG(("%d bitmaps in persistent cache, %d bitmaps loaded in memory\n", n, c));
	g_pstcache_enumerated = True;
	return n;
}

/* initialise the persistent bitmap cache */
BOOL
pstcache_init(uint8 cache_id)
{
	int fd;
	char filename[256];

	if (g_pstcache_enumerated)
		return True;

	g_pstcache_fd[cache_id] = 0;

	if (!(g_bitmap_cache && g_bitmap_cache_persist_enable))
		return False;

	if (!rd_pstcache_mkdir())
	{
		DEBUG(("failed to get/make cache directory!\n"));
		return False;
	}

	g_pstcache_Bpp = (g_server_bpp + 7) / 8;
	sprintf(filename, "cache/pstcache_%d_%d", cache_id, g_pstcache_Bpp);
	DEBUG(("persistent bitmap cache file: %s\n", filename));

	fd = rd_open_file(filename);
	if (fd == -1)
		return False;

	if (!rd_lock_file(fd, 0, 0))
	{
		warning("Persistent bitmap caching is disabled. (The file is already in use)\n");
		rd_close_file(fd);
		return False;
	}

	g_pstcache_fd[cache_id] = fd;
	return True;
}
