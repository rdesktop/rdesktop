/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - RDP Fast-Path PDU processing
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright 2003-2008 Erik Forsberg <forsberg@cendio.se> for Cendio AB
   Copyright 2017 Karl Mikaelsson <derfian@cendio.se> for Cendio AB

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

extern uint8 *g_next_packet;

extern RDPCOMP g_mppc_dict;


static void
process_ts_fp_update_by_code(STREAM s, uint8 code)
{
	uint16 count, x, y;

	switch (code)
	{
		case FASTPATH_UPDATETYPE_ORDERS:
			in_uint16_le(s, count);
			process_orders(s, count);
			break;
		case FASTPATH_UPDATETYPE_BITMAP:
			in_uint8s(s, 2);	/* part length */
			process_bitmap_updates(s);
			break;
		case FASTPATH_UPDATETYPE_PALETTE:
			in_uint8s(s, 2);	/* uint16 = 2 */
			process_palette(s);
			break;
		case FASTPATH_UPDATETYPE_SYNCHRONIZE:
			break;
		case FASTPATH_UPDATETYPE_PTR_NULL:
			ui_set_null_cursor();
			break;
		case FASTPATH_UPDATETYPE_PTR_DEFAULT:
			set_system_pointer(SYSPTR_DEFAULT);
			break;
		case FASTPATH_UPDATETYPE_PTR_POSITION:
			in_uint16_le(s, x);
			in_uint16_le(s, y);
			if (s_check(s))
				ui_move_pointer(x, y);
			break;
		case FASTPATH_UPDATETYPE_COLOR:
			process_colour_pointer_pdu(s);
			break;
		case FASTPATH_UPDATETYPE_CACHED:
			process_cached_pointer_pdu(s);
			break;
		case FASTPATH_UPDATETYPE_POINTER:
			process_new_pointer_pdu(s);
			break;
		default:
			logger(Protocol, Warning, "process_ts_fp_updates_by_code(), unhandled opcode %d",
			       code);
	}
}

void
process_ts_fp_updates(STREAM s)
{
	uint16 length;
	uint8 hdr, code, frag, comp, ctype = 0;
	uint8 *next;

	uint32 roff, rlen;
	struct stream *ns = &(g_mppc_dict.ns);
	struct stream *ts;

	static STREAM assembled[0x0F] = { 0 };

	ui_begin_update();
	while (s->p < s->end)
	{
		/* Reading a number of TS_FP_UPDATE structures from the stream here.. */
		in_uint8(s, hdr);	/* updateHeader */
		code = hdr & 0x0F;	/*  |- updateCode */
		frag = hdr & 0x30;	/*  |- fragmentation */
		comp = hdr & 0xC0;	/*  `- compression */

		if (comp & FASTPATH_OUTPUT_COMPRESSION_USED)
			in_uint8(s, ctype);	/* compressionFlags */

		in_uint16_le(s, length);	/* length */

		g_next_packet = next = s->p + length;

		if (ctype & RDP_MPPC_COMPRESSED)
		{
			if (mppc_expand(s->p, length, ctype, &roff, &rlen) == -1)
				logger(Protocol, Error,
				       "process_ts_fp_update_pdu(), error while decompressing packet");

			/* allocate memory and copy the uncompressed data into the temporary stream */
			ns->data = (uint8 *) xrealloc(ns->data, rlen);

			memcpy((ns->data), (unsigned char *) (g_mppc_dict.hist + roff), rlen);

			ns->size = rlen;
			ns->end = (ns->data + ns->size);
			ns->p = ns->data;
			ns->rdp_hdr = ns->p;

			ts = ns;
		}
		else
			ts = s;

		if (frag == FASTPATH_FRAGMENT_SINGLE)
		{
			process_ts_fp_update_by_code(ts, code);
		}
		else /* Fragmented packet, we must reassemble */
		{
			if (assembled[code] == NULL)
			{
				assembled[code] = xmalloc(sizeof(struct stream));
				memset(assembled[code], 0, sizeof(struct stream));
				s_realloc(assembled[code], RDESKTOP_FASTPATH_MULTIFRAGMENT_MAX_SIZE);
				s_reset(assembled[code]);
			}

			if (frag == FASTPATH_FRAGMENT_FIRST)
			{
				s_reset(assembled[code]);
			}

			out_uint8p(assembled[code], ts->p, length);

			if (frag == FASTPATH_FRAGMENT_LAST)
			{
				s_mark_end(assembled[code]);
				assembled[code]->p = assembled[code]->data;
				process_ts_fp_update_by_code(assembled[code], code);
			}
		}

		s->p = next;
	}
	ui_end_update();
}
