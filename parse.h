/*
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - parsing layer
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

/* Parser state */
typedef struct stream
{
	/* Parsing layer */
        unsigned char *data;
	unsigned int size;
        unsigned int offset;
	unsigned int end;
        BOOL marshall;
        BOOL error;

	/* Other layers */
	int iso_offset;
	int mcs_offset;
	int rdp_offset;

} *STREAM;

/* Connection state */
typedef struct connection
{
	/* User interface */
	HWINDOW wnd;
	HBITMAP bmpcache[8];

	/* Parsing layer */
	struct stream in;
	struct stream out;

	/* TCP layer */
	int tcp_socket;

	/* MCS layer */
	uint16 mcs_userid;

} *HCONN;

#define STREAM_INIT(s,m)   { s.data = xmalloc(2048); s.end = s.size = 2048; s.offset = 0; s.marshall = m; s.error = False; }
#define STREAM_SIZE(s,l)   { if (l > s.size) { s.data = xrealloc(s.data,l); s.end = s.size = l; } }
#define REMAINING(s)       ( s->end - s->offset )
#define PUSH_LAYER(s,v,l)  { s.v = s.offset; s.offset += l; }
#define POP_LAYER(s,v)     { s.offset = s.v; }
#define MARK_END(s)        { s.end = s.offset; }
