/*
   rdesktop: A Remote Desktop Protocol client.
   RDP message processing
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

/* Process incoming packets */
void rdp_main_loop(HCONN conn)
{
	RDP_DATA_HEADER hdr;
	RDP_ORDER_STATE os;
	uint8 type;

	memset(&os, 0, sizeof(os));

	while (rdp_recv_pdu(conn, &type))
	{
		switch (type)
		{
			case RDP_PDU_DEMAND_ACTIVE:
				process_demand_active(conn);
				memset(&os, 0, sizeof(os));
				break;

			case RDP_PDU_DEACTIVATE:
				break;

			case RDP_PDU_DATA:
				rdp_io_data_header(&conn->in, &hdr);

				switch (hdr.data_pdu_type)
				{
					case RDP_DATA_PDU_UPDATE:
						process_update_pdu(conn, &os);
						break;

					case RDP_DATA_PDU_POINTER:
						process_pointer_pdu(conn);
						break;

					default:
						NOTIMP("data PDU 0x%x\n",
							  hdr.data_pdu_type);
				}
				break;

			default:
				NOTIMP("PDU 0x%x\n", type);
		}
	}
}

/* Respond to a demand active PDU */
void process_demand_active(HCONN conn)
{
	RDP_ACTIVE_PDU active;
	uint8 type;

	if (!rdp_io_active_pdu(&conn->in, &active, RDP_PDU_DEMAND_ACTIVE))
		return;

	DEBUG("DEMAND_ACTIVE(id=0x%x)\n", active.shareid);

	rdp_send_confirm_active(conn, active.shareid, 640, 480);
	rdp_send_synchronize(conn);
	rdp_send_control(conn, RDP_CTL_COOPERATE);
	rdp_send_control(conn, RDP_CTL_REQUEST_CONTROL);
	rdp_recv_pdu(conn, &type); // RDP_PDU_SYNCHRONIZE
	rdp_recv_pdu(conn, &type); // RDP_CTL_COOPERATE
	rdp_recv_pdu(conn, &type); // RDP_CTL_GRANT_CONTROL
	rdp_send_input(conn, RDP_INPUT_SYNCHRONIZE, 0, 0, 0);
	rdp_send_fonts(conn, 1);
	rdp_send_fonts(conn, 2);
	rdp_recv_pdu(conn, &type); // RDP_PDU_UNKNOWN 0x28
}

/* Process a pointer PDU */
void process_pointer_pdu(HCONN conn)
{
	RDP_POINTER_PDU pp;

	if (!rdp_io_pointer_pdu(&conn->in, &pp))
		return;

	switch (pp.message)
	{
		case RDP_POINTER_MOVE:
			ui_move_pointer(conn->wnd, pp.x, pp.y);
			break;

		default:
			NOTIMP("pointer message 0x%x\n", pp.message);
	}
}

/* Process an update PDU */
void process_update_pdu(HCONN conn, RDP_ORDER_STATE *os)
{
	RDP_UPDATE_PDU update;

	if (!rdp_io_update_pdu(&conn->in, &update))
		return;

	switch (update.update_type)
	{
		case RDP_UPDATE_ORDERS:
			process_orders(conn, os);
			break;

		case RDP_UPDATE_PALETTE:
		case RDP_UPDATE_SYNCHRONIZE:
			break;

		default:
			NOTIMP("update 0x%x\n", update.update_type);
	}

}


/* UPDATE PDUs */

/* Process an order */
void process_orders(HCONN conn, RDP_ORDER_STATE *os)
{
	RDP_SECONDARY_ORDER rso;
	uint32 present, num_orders;
	uint8 order_flags;
	int size, processed = 0;
	BOOL delta;

	lsb_io_uint32(&conn->in, &num_orders);
	num_orders &= 0xffff; /* second word padding */

	while (processed < num_orders)
	{
		if (!prs_io_uint8(&conn->in, &order_flags))
			break;

		if (!(order_flags & RDP_ORDER_STANDARD))
		{
			ERROR("Order parsing failed\n");
			return;
		}

		if (order_flags & RDP_ORDER_SECONDARY)
		{
			if (!rdp_io_secondary_order(&conn->in, &rso))
				break;

			switch (rso.type)
			{
				case RDP_ORDER_RAW_BMPCACHE:
					process_raw_bmpcache(conn);
					break;

				case RDP_ORDER_COLCACHE:
					process_colcache(conn);
					break;

				case RDP_ORDER_BMPCACHE:
					process_bmpcache(conn);
					break;

				case RDP_ORDER_FONTCACHE:
					process_fontcache(conn);
					break;

				default:
					NOTIMP("secondary order %d\n",
						rso.type);
					conn->in.offset += rso.length + 7;
			}
		}
		else
		{
			if (order_flags & RDP_ORDER_CHANGE)
			{
				prs_io_uint8(&conn->in, &os->order_type);
			}

			switch (os->order_type)
			{
				case RDP_ORDER_TRIBLT:
				case RDP_ORDER_TEXT2:
					size = 3;
					break;

				case RDP_ORDER_PATBLT:
				case RDP_ORDER_MEMBLT:
				case RDP_ORDER_LINE:
					size = 2;
					break;

				default:
					size = 1;
			}

			rdp_io_present(&conn->in, &present, order_flags, size);

			if (order_flags & RDP_ORDER_BOUNDS)
			{
				if (!(order_flags & RDP_ORDER_LASTBOUNDS))
					rdp_io_bounds(&conn->in, &os->bounds);

				ui_set_clip(conn->wnd, os->bounds.left,
					os->bounds.top,
					os->bounds.right - os->bounds.left + 1,
					os->bounds.bottom - os->bounds.top + 1);
			}

			delta = order_flags & RDP_ORDER_DELTA;

			switch (os->order_type)
			{
				case RDP_ORDER_DESTBLT:
					process_destblt(conn, &os->destblt,
							present, delta);
					break;

				case RDP_ORDER_PATBLT:
					process_patblt(conn, &os->patblt,
						       present, delta);
					break;

				case RDP_ORDER_SCREENBLT:
					process_screenblt(conn, &os->screenblt,
							  present, delta);
					break;

				case RDP_ORDER_LINE:
					process_line(conn, &os->line,
							present, delta);
					break;

				case RDP_ORDER_RECT:
					process_rect(conn, &os->rect,
						     present, delta);
					break;

				case RDP_ORDER_DESKSAVE:
					process_desksave(conn, &os->desksave,
							 present, delta);
					break;

				case RDP_ORDER_MEMBLT:
					process_memblt(conn, &os->memblt,
						       present, delta);
					break;

				case RDP_ORDER_TRIBLT:
					process_triblt(conn, &os->triblt,
						       present, delta);
					break;

				case RDP_ORDER_TEXT2:
					process_text2(conn, &os->text2,
						      present, delta);
					break;

				default:
					NOTIMP("order %d\n", os->order_type);
					return;
			}

			if (order_flags & RDP_ORDER_BOUNDS)
				ui_reset_clip(conn->wnd);
		}

		processed++;
	}

	if (conn->in.offset != conn->in.rdp_offset)
		WARN("Order data remaining\n");
}


/* PRIMARY ORDERS */

/* Process a destination blt order */
void process_destblt(HCONN conn, DESTBLT_ORDER *os, uint32 present, BOOL delta)
{
	if (!rdp_io_destblt_order(&conn->in, os, present, delta))
		return;

	DEBUG("DESTBLT(op=0x%x,x=%d,y=%d,cx=%d,cy=%d)\n",
	      os->opcode, os->x, os->y, os->cx, os->cy);

	ui_destblt(conn->wnd, ROP2_S(os->opcode), os->x, os->y, os->cx, os->cy);
}

/* Process a pattern blt order */
void process_patblt(HCONN conn, PATBLT_ORDER *os, uint32 present, BOOL delta)
{
	if (!rdp_io_patblt_order(&conn->in, os, present, delta))
		return;

	DEBUG("PATBLT(op=0x%x,x=%d,y=%d,cx=%d,cy=%d,bs=%d,bg=0x%x,fg=0x%x)\n",
	      os->opcode, os->x, os->y, os->cx, os->cy,
	      os->brush.style, os->bgcolour, os->fgcolour);

	ui_patblt(conn->wnd, ROP2_P(os->opcode), os->x, os->y, os->cx, os->cy,
				&os->brush, os->bgcolour, os->fgcolour);
}

/* Process a screen blt order */
void process_screenblt(HCONN conn, SCREENBLT_ORDER *os, uint32 present, BOOL delta)
{
	if (!rdp_io_screenblt_order(&conn->in, os, present, delta))
		return;

	DEBUG("SCREENBLT(op=0x%x,x=%d,y=%d,cx=%d,cy=%d,srcx=%d,srcy=%d)\n",
	      os->opcode, os->x, os->y, os->cx, os->cy, os->srcx, os->srcy);

	ui_screenblt(conn->wnd, ROP2_S(os->opcode), os->x, os->y, os->cx, os->cy,
				os->srcx, os->srcy);
}

/* Process a line order */
void process_line(HCONN conn, LINE_ORDER *os, uint32 present, BOOL delta)
{
	if (!rdp_io_line_order(&conn->in, os, present, delta))
		return;

	DEBUG("LINE(op=0x%x,sx=%d,sy=%d,dx=%d,dx=%d,fg=0x%x)\n",
	      os->opcode, os->startx, os->starty, os->endx, os->endy,
	      os->pen.colour);

	if (os->opcode < 0x01 || os->opcode > 0x10)
	{
		ERROR("Bad ROP2 opcode 0x%x\n", os->opcode);
		return;
	}

	ui_line(conn->wnd, os->opcode-1, os->startx, os->starty,
			os->endx, os->endy, &os->pen);
}

/* Process an opaque rectangle order */
void process_rect(HCONN conn, RECT_ORDER *os, uint32 present, BOOL delta)
{
	if (!rdp_io_rect_order(&conn->in, os, present, delta))
		return;

	DEBUG("RECT(x=%d,y=%d,cx=%d,cy=%d,fg=0x%x)\n",
	      os->x, os->y, os->cx, os->cy, os->colour);

	ui_rect(conn->wnd, os->x, os->y, os->cx, os->cy, os->colour);
}

/* Process a desktop save order */
void process_desksave(HCONN conn, DESKSAVE_ORDER *os, uint32 present, BOOL delta)
{
	int width, height;
	uint8 *data;

	if (!rdp_io_desksave_order(&conn->in, os, present, delta))
		return;

	DEBUG("DESKSAVE(l=%d,t=%d,r=%d,b=%d,off=%d,op=%d)\n",
	      os->left, os->top, os->right, os->bottom, os->offset,
	      os->action);

	data = conn->deskcache + os->offset;
	width = os->right - os->left + 1;
	height = os->bottom - os->top + 1;

	if (os->action == 0)
	{
		ui_desktop_save(conn->wnd, data, os->left, os->top,
					width, height);
	}
	else
	{
		ui_desktop_restore(conn->wnd, data, os->left, os->top,
					width, height);
	}
}

/* Process a memory blt order */
void process_memblt(HCONN conn, MEMBLT_ORDER *os, uint32 present, BOOL delta)
{
	HBITMAP bitmap;

	if (!rdp_io_memblt_order(&conn->in, os, present, delta))
		return;

	DEBUG("MEMBLT(op=0x%x,x=%d,y=%d,cx=%d,cy=%d,id=%d,idx=%d)\n",
	      os->opcode, os->x, os->y, os->cx, os->cy, os->cache_id,
	      os->cache_idx);

	bitmap = cache_get_bitmap(conn, os->cache_id, os->cache_idx);
	if (bitmap == NULL)
		return;

	ui_memblt(conn->wnd, ROP2_S(os->opcode), os->x, os->y, os->cx, os->cy,
			bitmap, os->srcx, os->srcy);
}

/* Process a 3-way blt order */
void process_triblt(HCONN conn, TRIBLT_ORDER *os, uint32 present, BOOL delta)
{
	HBITMAP bitmap;

	if (!rdp_io_triblt_order(&conn->in, os, present, delta))
		return;

	DEBUG("TRIBLT(op=0x%x,x=%d,y=%d,cx=%d,cy=%d,id=%d,idx=%d,bs=%d,bg=0x%x,fg=0x%x)\n",
	      os->opcode, os->x, os->y, os->cx, os->cy, os->cache_id,
	      os->cache_idx, os->brush.style, os->bgcolour, os->fgcolour);

	bitmap = cache_get_bitmap(conn, os->cache_id, os->cache_idx);
	if (bitmap == NULL)
		return;

	ui_triblt(conn->wnd, os->opcode, os->x, os->y, os->cx, os->cy,
			bitmap, os->srcx, os->srcy,
			&os->brush, os->bgcolour, os->fgcolour);
}

/* Process a text order */
void process_text2(HCONN conn, TEXT2_ORDER *os, uint32 present, BOOL delta)
{
	BLOB *entry;
	int i;

	if (!rdp_io_text2_order(&conn->in, os, present, delta))
		return;

	DEBUG("TEXT2(x=%d,y=%d,cl=%d,ct=%d,cr=%d,cb=%d,bl=%d,bt=%d,bb=%d,br=%d,fg=0x%x,bg=0x%x,font=%d,fl=0x%x,mix=%d,unk=0x%x,n=%d)\n",
		os->x, os->y, os->clipleft, os->cliptop, os->clipright,
		os->clipbottom, os->boxleft, os->boxtop, os->boxright,
		os->boxbottom, os->fgcolour, os->bgcolour, os->font,
		os->flags, os->mixmode, os->unknown, os->length);

	fprintf(stderr, "Text: ");

	for (i = 0; i < os->length; i++)
		fprintf(stderr, "%02x ", os->text[i]);

	fprintf(stderr, "\n");

	/* Process special cache strings */
	if ((os->length == 2) && (os->text[0] == 0xfe))
	{
		entry = cache_get_text(conn, os->text[1]);

		if (entry == NULL)
			return;
		
		memcpy(os->text, entry->data, entry->size);
		os->length = entry->size;
	}
	else if ((os->length >= 3) && (os->text[os->length-3] == 0xff))
	{
		os->length -= 3;
		cache_put_text(conn, os->text[os->length+1],
			       os->text, os->length);
	}

	ui_draw_text(conn->wnd, os->font, os->flags, os->mixmode,
			os->x, os->y, os->boxleft, os->boxtop,
			os->boxright - os->boxleft,
			os->boxbottom - os->boxtop,
			os->bgcolour, os->fgcolour, os->text, os->length);
}


/* SECONDARY ORDERS */

/* Process a raw bitmap cache order */
void process_raw_bmpcache(HCONN conn)
{
	RDP_RAW_BMPCACHE_ORDER order;
	HBITMAP bitmap;

	if (!rdp_io_raw_bmpcache_order(&conn->in, &order))
		return;

	DEBUG("RAW_BMPCACHE(cx=%d,cy=%d,id=%d,idx=%d)\n",
	      order.width, order.height, order.cache_id, order.cache_idx);

	bitmap = ui_create_bitmap(conn->wnd, order.width, order.height,
				  order.data);
	cache_put_bitmap(conn, order.cache_id, order.cache_idx, bitmap);
}

/* Process a bitmap cache order */
void process_bmpcache(HCONN conn)
{
	RDP_BMPCACHE_ORDER order;
	HBITMAP bitmap;
	char *bmpdata;

	if (!rdp_io_bmpcache_order(&conn->in, &order))
		return;

	DEBUG("BMPCACHE(cx=%d,cy=%d,id=%d,idx=%d)\n",
	      order.width, order.height, order.cache_id, order.cache_idx);

	bmpdata = malloc(order.width * order.height);

	if (bitmap_decompress(bmpdata, order.width, order.height,
			       order.data, order.size))
	{
		bitmap = ui_create_bitmap(conn->wnd, order.width, order.height,
					  bmpdata);
		cache_put_bitmap(conn, order.cache_id, order.cache_idx, bitmap);
	}

	free(bmpdata);
}

/* Process a colourmap cache order */
void process_colcache(HCONN conn)
{
	RDP_COLCACHE_ORDER order;
	HCOLOURMAP map;

	if (!rdp_io_colcache_order(&conn->in, &order))
		return;

	DEBUG("COLCACHE(id=%d,n=%d)\n", order.cache_id, order.map.ncolours);

	map = ui_create_colourmap(conn->wnd, &order.map);
	ui_set_colourmap(conn->wnd, map);
}

/* Process a font cache order */
void process_fontcache(HCONN conn)
{
	RDP_FONTCACHE_ORDER order;
	RDP_FONT_GLYPH *glyph;
	HGLYPH bitmap;
	int i;

	if (!rdp_io_fontcache_order(&conn->in, &order))
		return;

	DEBUG("FONTCACHE(font=%d,n=%d)\n", order.font, order.nglyphs);

	for (i = 0; i < order.nglyphs; i++)
	{
		glyph = &order.glyphs[i];

		bitmap = ui_create_glyph(conn->wnd, glyph->width,
					  glyph->height, glyph->data);

		cache_put_font(conn, order.font, glyph->character,
				glyph->baseline, glyph->width, glyph->height,
				bitmap);
	}
}
