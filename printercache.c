/* -*- c-basic-offset: 8 -*-
 * rdesktop: A Remote Desktop Protocol client.
 * Entrypoint and utility functions
 * Copyright (C) Matthew Chapman 1999-2003
 * Copyright (C) Jeroen Meijer 2003
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* According to the W2K RDP Printer Redirection WhitePaper, a data
 * blob is sent to the client after the configuration of the printer
 * is changed at the server.
 *
 * This data blob is saved to the registry. The client returns this
 * data blob in a new session with the printer announce data.
 * The data is not interpreted by the client.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "rdesktop.h"

BOOL
printercache_mkdir(char *base, char *printer)
{
	char *path;

	path = (char *) xmalloc(strlen(base) + sizeof("/.rdesktop/rdpdr/") + strlen(printer));

	sprintf(path, "%s/.rdesktop", base);
	if ((mkdir(path, 0700) == -1) && errno != EEXIST)
	{
		perror(path);
		return False;
	}

	strcat(path, "/rdpdr");
	if ((mkdir(path, 0700) == -1) && errno != EEXIST)
	{
		perror(path);
		return False;
	}

	strcat(path, "/");
	strcat(path, printer);
	if ((mkdir(path, 0700) == -1) && errno != EEXIST)
	{
		perror(path);
		return False;
	}

	xfree(path);
	return True;
}

int
printercache_load_blob(char *printer_name, uint8 ** data)
{
	char *home, *path;
	struct stat st;
	int fd, length;

	if (printer_name == NULL)
		return 0;

	home = getenv("HOME");
	if (home == NULL)
		return 0;

	path = (char *) xmalloc(strlen(home) + sizeof("/.rdesktop/rdpdr/") + strlen(printer_name) +
				sizeof("/AutoPrinterCacheData"));
	sprintf(path, "%s/.rdesktop/rdpdr/%s/AutoPrinterCacheData", home, printer_name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return 0;

	if (fstat(fd, &st))
		return 0;

	*data = (uint8 *) xmalloc(st.st_size);
	length = read(fd, *data, st.st_size);
	close(fd);
	xfree(path);
	return length;
}

void
printercache_save_blob(char *printer_name, uint8 * data, uint32 length)
{
	char *home, *path;
	int fd;

	if (printer_name == NULL)
		return;

	home = getenv("HOME");
	if (home == NULL)
		return;

	if (!printercache_mkdir(home, printer_name))
		return;

	path = (char *) xmalloc(strlen(home) + sizeof("/.rdesktop/rdpdr/") + strlen(printer_name) +
				sizeof("/AutoPrinterCacheData"));
	sprintf(path, "%s/.rdesktop/rdpdr/%s/AutoPrinterCacheData", home, printer_name);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1)
	{
		perror(path);
		return;
	}

	if (write(fd, data, length) != length)
	{
		perror(path);
		unlink(path);
	}

	close(fd);
	xfree(path);
}

void
printercache_process(STREAM s)
{
	uint32 type, printer_length, driver_length, printer_unicode_length, blob_length;
	char device_name[9], printer[256], driver[256];

	in_uint32_le(s, type);
	switch (type)
	{
		/*case 4: renaming of item old name and then new name */
		/*case 3: delete item name */
		case 2:
			in_uint32_le(s, printer_unicode_length);
			in_uint32_le(s, blob_length);

			if (printer_unicode_length < 2 * 255)
			{
				rdp_in_unistr(s, printer, printer_unicode_length);
				printercache_save_blob(printer, s->p, blob_length);
			}
			break;

		/*case 1:*/
			// TODO: I think this one just tells us what printer is on LPT? but why?

			//
			// your name and the "users choice" of printer driver
			// my guess is that you can store it and automagically reconnect
			// the printer with correct driver next time.
		default:

			unimpl("RDPDR Printer Cache Packet Type: %d\n", type);
			break;
	}
}
