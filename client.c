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

int main(int argc, char *argv[])
{
	HCONN conn;

	fprintf(stderr, "rdesktop: A Remote Desktop Protocol client.\n");
	fprintf(stderr, "Version 0.9.0-prealpha. Copyright (C) 1999-2000 Matt Chapman.\n\n");

	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s <server>\n", argv[0]);
		return 1;
	}

	if ((conn = rdp_connect(argv[1])) == NULL)
		return 1;

	fprintf(stderr, "Connection successful.\n");

	conn->wnd = ui_create_window(640, 480);
	rdp_main_loop(conn);

	ui_destroy_window(conn->wnd);
	rdp_disconnect(conn);

	return 0;
}
