/*
   rdesktop: A Remote Desktop Protocol client.
   Sound infrastructure
   Copyright (C) Michael Gernoth 2006

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

struct audio_packet
{
	struct stream s;
	uint16 tick;
	uint8 index;
};

extern BOOL g_dsp_busy;
extern int g_dsp_fd;

void rdpsnd_queue_write(STREAM s, uint16 tick, uint8 index);
inline struct audio_packet *rdpsnd_queue_current_packet(void);
inline BOOL rdpsnd_queue_empty(void);
inline void rdpsnd_queue_init(void);
inline void rdpsnd_queue_next(void);
inline int rdpsnd_queue_next_tick(void);
