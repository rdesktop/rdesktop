/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - Multipoint Communications Service
   Copyright (C) Matthew Chapman 1999-2002
   Copyright (C) Erik Forsberg 2003
   
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

extern uint8 *g_next_packet;

void
rdp5_process(STREAM s, BOOL encryption)
{
	uint16 length, count, x, y;
	uint8 type;
	uint8 *next;

	if (encryption)
	{
		in_uint8s(s, 8);	/* signature */
		sec_decrypt(s->p, s->end - s->p);
	}

#if 0
	printf("RDP5 data:\n");
	hexdump(s->p, s->end - s->p);
#endif

	while (s->p < s->end)
	{
		in_uint8(s, type);
		in_uint16_le(s, length);
		g_next_packet = next = s->p + length;

		switch (type)
		{
				/* Thanks to Jeroen Meijer <jdmeijer at yahoo
				   dot com> for finding out the meaning of
				   most of the opcodes here. Especially opcode
				   8! :) */
			case 0:	/* orders */
				in_uint16_le(s, count);
				process_orders(s, count);
				break;
			case 1:	/* bitmap update (???) */
				in_uint8s(s, 2);	/* part length */
				process_bitmap_updates(s);
				break;
			case 2:	/* palette */
				in_uint8s(s, 2);	/* uint16 = 2 */
				process_palette(s);
				break;
			case 3:	/* probably an palette with offset 3. Weird */
				break;
			case 5:
				ui_set_null_cursor();
				break;
			case 8:
				in_uint16_le(s, x);
				in_uint16_le(s, y);
				if (s_check(s))
					ui_move_pointer(x, y);
				break;
			case 9:
				process_colour_pointer_pdu(s);
				break;
			case 10:
				process_cached_pointer_pdu(s);
				break;
			default:
				unimpl("RDP5 opcode %d\n", type);
		}

		s->p = next;
	}
}
