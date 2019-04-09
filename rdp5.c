/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - RDP5 short form PDU processing
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright 2003-2008 Erik Forsberg <forsberg@cendio.se> for Cendio AB

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "rdesktop.h"

extern size_t g_next_packet;

extern RDPCOMP g_mppc_dict;

void
rdp5_process(STREAM s)
{
	uint16 length, count, x, y;
	uint8 type, ctype;
	size_t next;

	uint8 *buf;
	uint32 roff, rlen;
	struct stream *ns = &(g_mppc_dict.ns);
	struct stream *ts;

#if 0
	printf("RDP5 data:\n");
	hexdump(s->p, s_remaining(s));
#endif

	ui_begin_update();
	while (!s_check_end(s))
	{
		in_uint8(s, type);
		if (type & RDP5_COMPRESSED)
		{
			in_uint8(s, ctype);
			in_uint16_le(s, length);
			type ^= RDP5_COMPRESSED;
		}
		else
		{
			ctype = 0;
			in_uint16_le(s, length);
		}
		g_next_packet = next = s_tell(s) + length;

		if (ctype & RDP_MPPC_COMPRESSED)
		{
			in_uint8p(s, buf, length);
			if (mppc_expand(buf, length, ctype, &roff, &rlen) == -1)
				error("error while decompressing packet\n");

			/* allocate memory and copy the uncompressed data into the temporary stream */
			s_realloc(ns, rlen);
			s_reset(ns);

			out_uint8a(ns, (unsigned char *) (g_mppc_dict.hist + roff), rlen);

			s_mark_end(ns);
			s_seek(ns, 0);
			s_push_layer(ns, rdp_hdr, 0);

			ts = ns;
		}
		else
			ts = s;

		switch (type)
		{
			case 0:	/* update orders */
				in_uint16_le(ts, count);
				process_orders(ts, count);
				break;
			case 1:	/* update bitmap */
				in_uint8s(ts, 2);	/* part length */
				process_bitmap_updates(ts);
				break;
			case 2:	/* update palette */
				in_uint8s(ts, 2);	/* uint16 = 2 */
				process_palette(ts);
				break;
			case 3:	/* update synchronize */
				break;
			case 5:	/* null pointer */
				ui_set_null_cursor();
				break;
			case 6:	/* default pointer */
				break;
			case 8:	/* pointer position */
				in_uint16_le(ts, x);
				in_uint16_le(ts, y);
				ui_move_pointer(x, y);
				break;
			case 9:	/* color pointer */
				process_colour_pointer_pdu(ts);
				break;
			case 10:	/* cached pointer */
				process_cached_pointer_pdu(ts);
				break;
			case 11:
				process_new_pointer_pdu(ts);
				break;
			default:
				unimpl("RDP5 opcode %d\n", type);
		}

		s_seek(s, next);
	}
	ui_end_update();
}
