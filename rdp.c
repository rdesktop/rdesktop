/*
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - RDP layer
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

/* Establish a connection up to the RDP layer */
HCONN rdp_connect(char *server)
{
	HCONN conn;
	RDP_ACTIVE_PDU active;
	uint8 type;

	if ((conn = mcs_connect(server)) == NULL)
		return NULL;

	rdp_establish_key(conn);
	mcs_recv(conn, False); /* Server's licensing certificate */
	rdp_send_cert(conn);
	mcs_recv(conn, False);

	if (!rdp_recv_pdu(conn, &type) || (type != RDP_PDU_DEMAND_ACTIVE))
	{
		fprintf(stderr, "RDP error, expected Demand Active\n");
		mcs_disconnect(conn);
		return NULL;
	}

	rdp_io_active_pdu(&conn->in, &active, RDP_PDU_DEMAND_ACTIVE);
	rdp_send_confirm_active(conn);
	rdp_send_synchronize(conn);
	rdp_send_control(conn, RDP_CTL_COOPERATE);
	rdp_send_control(conn, RDP_CTL_REQUEST_CONTROL);
	rdp_recv_pdu(conn, &type); // RDP_PDU_SYNCHRONIZE
	rdp_recv_pdu(conn, &type); // RDP_CTL_COOPERATE
	rdp_recv_pdu(conn, &type); // RDP_CTL_GRANT_CONTROL
	rdp_send_input(conn);
	rdp_send_fonts(conn, 1);
	rdp_send_fonts(conn, 2);
	rdp_recv_pdu(conn, &type); // RDP_PDU_UNKNOWN 0x28

	return conn;
}

void rdp_main_loop(HCONN conn)
{
	RDP_DATA_HEADER hdr;
	RDP_UPDATE_PDU update;
	RDP_ORDER_STATE os;
	uint8 type;

	memset(&os, 0, sizeof(os));

	while (rdp_recv_pdu(conn, &type))
	{
		if (type != RDP_PDU_DATA)
			continue;

		rdp_io_data_header(&conn->in, &hdr);

		switch (hdr.data_pdu_type)
		{
		case RDP_DATA_PDU_UPDATE:
			rdp_io_update_pdu(&conn->in, &update);
			if (update.update_type == RDP_UPDATE_ORDERS)
			{
				fprintf(stderr, "Received orders\n");
				process_orders(conn, &os);
			}
			break;
		}
	}
}

void prs_io_coord(STREAM s, uint16 *coord, BOOL delta)
{
	uint8 change;

	if (delta)
	{
		prs_io_uint8(s, &change);
		*coord += change;
	}
	else
	{
		lsb_io_uint16(s, coord);
	}
}

void process_opaque_rect(HCONN conn, RDP_ORDER_STATE *os, BOOL delta)
{
	uint8 present;
	prs_io_uint8(&conn->in, &present);

	if (present & 1)
		prs_io_coord(&conn->in, &os->opaque_rect.x, delta);

	if (present & 2)
		prs_io_coord(&conn->in, &os->opaque_rect.y, delta);

	if (present & 4)
		prs_io_coord(&conn->in, &os->opaque_rect.cx, delta);

	if (present & 8)
		prs_io_coord(&conn->in, &os->opaque_rect.cy, delta);

	if (present & 16)
		prs_io_uint8(&conn->in, &os->opaque_rect.colour);

	fprintf(stderr, "Opaque rectangle at %d, %d\n", os->opaque_rect.x, os->opaque_rect.y);
}

void process_bmpcache(HCONN conn)
{

	RDP_BITMAP_HEADER rbh;
	char *bmpdata;
	HBITMAP bmp;
	static int x = 0;

	rdp_io_bitmap_header(&conn->in, &rbh);
	fprintf(stderr, "Decompressing bitmap %d x %d, final size %d\n", rbh.width, rbh.height, rbh.final_size);

	bmpdata = malloc(rbh.width * rbh.height);
	bitmap_decompress(conn->in.data
			  + conn->in.offset, rbh.size,
			  bmpdata, rbh.width);
	conn->in.offset += rbh.size;

	bmp = ui_create_bitmap(conn->wnd, rbh.width, rbh.height, bmpdata);
	ui_paint_bitmap(conn->wnd, bmp, x, 0);
	ui_destroy_bitmap(bmp);

	x += rbh.width;
}

void process_orders(HCONN conn, RDP_ORDER_STATE *os)
{
	uint16 num_orders;
	int processed = 0;
	BOOL res = True;
	//	unsigned char *p;

	lsb_io_uint16(&conn->in, &num_orders);

	conn->in.offset += 2;
	//	p = &conn->in.data[conn->in.offset];

	//	fprintf(stderr, "%02X %02X %02X %02X\n", p[0], p[1], p[2], p[3]);

	while ((processed < num_orders) && res)
	{
		uint8 order_flags;

		prs_io_uint8(&conn->in, &order_flags);

		if (!(order_flags & RDP_ORDER_STANDARD))
			return;

		if (order_flags & RDP_ORDER_SECONDARY)
		{
			RDP_SECONDARY_ORDER rso;

			rdp_io_secondary_order(&conn->in, &rso);
			switch (rso.type)
			{
			case RDP_ORDER_BMPCACHE:
				process_bmpcache(conn);
				break;
			default:
				fprintf(stderr, "Unknown secondary order %d\n",
					rso.type);
				return;
			}


		}

		if (order_flags & RDP_ORDER_CHANGE)
			prs_io_uint8(&conn->in, &os->order_type);

		switch (os->order_type)
		{
		case RDP_ORDER_OPAQUE_RECT:
			process_opaque_rect(conn, os, order_flags & RDP_ORDER_DELTA);
			break;
		default:
			fprintf(stderr, "Unknown order %d\n", os->order_type);
			return;
		}

		processed++;
	}
}

/* Work this out later. This is useless anyway when encryption is off. */
uint8 precanned_key_packet[] = {
   0x48,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x33,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x6c,0x86, 
   0xf7,0x99,0xef,0x60,0xc4,0x49,0x52,0xd0,0xd8,0xea,0xb5,0x4f,0x58,0x19,
   0x52,0x2a,0x93,0x83,0x57,0x4f,0x4e,0x04,0xde,0x96,0x51,0xab,0x13,0x20,
   0xd8,0xe5,0x00,0x00,0x00,0x00,0x00,0x00
};

/* Create an RC4 key and transfer it to the server */
void rdp_establish_key(HCONN conn)
{
	mcs_init_data(conn);
	memcpy(conn->out.data + conn->out.offset, precanned_key_packet,
	       sizeof(precanned_key_packet));
	conn->out.offset += sizeof(precanned_key_packet);
	MARK_END(conn->out);
	mcs_send_data(conn, MCS_GLOBAL_CHANNEL, True);
}

/* Horrible horrible certificate stuff. Work out later. */
uint8 precanned_cert_packet[] = {
0x80,0x00,0x00,0x00,0x12,0x02,0xb4,0x04,0x01,0x00,0x00,
0x00,0x00,0x00,0x01,0x02,0x9d,0xa3,0x7a,0x93,0x34,0x7b,0x28,0x37,0x24,0xa0,0x1f,
0x61,0x26,0xfd,0x96,0x3a,0x92,0x83,0xf3,0xe9,0x6a,0x2e,0x81,0x7c,0x2c,0xe4,0x72,//
0x01,0x18,0xe9,0xa1,0x0f,0x00,0x00,0x48,0x00,0x84,0x23,0x90,0xe6,0xd3,0xf8,0x20,
0xdb,0xa8,0x1b,0xb2,0xd0,0x78,0x2c,0x35,0xde,0xe3,0x0e,0x63,0x40,0xca,0xac,0x71,
0xc9,0x17,0x49,0x05,0x25,0xeb,0x9b,0xd0,0xa6,0x5c,0x90,0x3e,0x9d,0x4b,0x27,0x01,
0x79,0x1c,0x22,0xfb,0x3c,0x2c,0xb9,0x9f,0xf5,0x21,0xf3,0xee,0xd5,0x4d,0x47,0x1c,
0x85,0xbe,0x83,0x93,0xe8,0xed,0x8c,0x5c,0x82,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x01,0x00,0x10,0x04,0x30,0x82,0x04,0x0c,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,
0x0d,0x01,0x07,0x02,0xa0,0x82,0x03,0xfd,0x30,0x82,0x03,0xf9,0x02,0x01,0x01,0x31,
0x00,0x30,0x0b,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x01,0xa0,0x82,
0x03,0xe1,0x30,0x82,0x01,0x77,0x30,0x82,0x01,0x25,0xa0,0x03,0x02,0x01,0x02,0x02,
0x08,0x01,0xbf,0x06,0x84,0x9d,0xdb,0x2d,0xe0,0x30,0x0d,0x06,0x09,0x2a,0x86,0x48,
0x86,0xf7,0x0d,0x01,0x01,0x01,0x05,0x00,0x30,0x38,0x31,0x36,0x30,0x11,0x06,0x03,
0x55,0x04,0x03,0x1e,0x0a,0x00,0x4e,0x00,0x54,0x00,0x54,0x00,0x53,0x00,0x45,0x30,
0x21,0x06,0x03,0x55,0x04,0x07,0x1e,0x1a,0x00,0x4d,0x00,0x69,0x00,0x63,0x00,0x72,
0x00,0x6f,0x00,0x73,0x00,0x6f,0x00,0x66,0x00,0x74,0x00,0x2e,0x00,0x63,0x00,0x6f,
0x00,0x6d,0x30,0x1e,0x17,0x0d,0x39,0x39,0x30,0x39,0x32,0x34,0x31,0x32,0x30,0x32,
0x30,0x34,0x5a,0x17,0x0d,0x34,0x39,0x30,0x39,0x32,0x34,0x31,0x32,0x30,0x32,0x30,
0x34,0x5a,0x30,0x38,0x31,0x36,0x30,0x11,0x06,0x03,0x55,0x04,0x03,0x1e,0x0a,0x00,
0x4e,0x00,0x54,0x00,0x54,0x00,0x53,0x00,0x45,0x30,0x21,0x06,0x03,0x55,0x04,0x07,
0x1e,0x1a,0x00,0x4d,0x00,0x69,0x00,0x63,0x00,0x72,0x00,0x6f,0x00,0x73,0x00,0x6f,
0x00,0x66,0x00,0x74,0x00,0x2e,0x00,0x63,0x00,0x6f,0x00,0x6d,0x30,0x5c,0x30,0x0d,
0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01,0x05,0x00,0x03,0x4b,0x00,
0x30,0x48,0x02,0x41,0x00,0x91,0xb2,0x16,0x1c,0xae,0x4f,0x7f,0x7c,0xaf,0x57,0x2b,
0x23,0x4c,0x0c,0x25,0x3c,0x4f,0x66,0x9d,0x25,0xc3,0x4f,0x29,0xee,0x8b,0xda,0x4e,
0x95,0xe7,0x3b,0xaa,0xc0,0xa7,0xba,0xaf,0x99,0x8c,0x47,0x24,0x8b,0x09,0x77,0xbc,
0x2c,0xf4,0xe7,0x1a,0x07,0x58,0x7b,0x11,0x37,0x2a,0xa8,0x90,0xc3,0x50,0x92,0x80,
0x15,0xc5,0xda,0x51,0x8b,0x02,0x03,0x01,0x00,0x01,0xa3,0x13,0x30,0x11,0x30,0x0f,
0x06,0x03,0x55,0x1d,0x13,0x04,0x08,0x30,0x06,0x01,0x01,0xff,0x02,0x01,0x00,0x30,
0x09,0x06,0x05,0x2b,0x0e,0x03,0x02,0x1d,0x05,0x00,0x03,0x41,0x00,0x14,0x04,0x67,
0x28,0xc8,0xd3,0x1f,0x13,0x14,0x2e,0x2c,0x93,0x09,0x25,0xbb,0xbe,0x86,0x6a,0xd3,
0x47,0x6f,0x44,0x16,0x7b,0x94,0x8c,0xb2,0xa2,0xd5,0xf7,0x4f,0xb1,0x8f,0x7f,0xde,
0x0b,0x88,0x34,0x4a,0x1d,0xdc,0xa1,0xfd,0x26,0xbd,0x43,0xbb,0x38,0xf1,0x87,0x34,
0xbb,0xe9,0x3b,0xfa,0x7f,0x1e,0xff,0xe1,0x10,0x7e,0xee,0x6e,0xd8,0x30,0x82,0x02,
0x62,0x30,0x82,0x02,0x10,0xa0,0x03,0x02,0x01,0x02,0x02,0x05,0x01,0x00,0x00,0x00,
0x01,0x30,0x09,0x06,0x05,0x2b,0x0e,0x03,0x02,0x1d,0x05,0x00,0x30,0x38,0x31,0x36,
0x30,0x11,0x06,0x03,0x55,0x04,0x03,0x1e,0x0a,0x00,0x4e,0x00,0x54,0x00,0x54,0x00,
0x53,0x00,0x45,0x30,0x21,0x06,0x03,0x55,0x04,0x07,0x1e,0x1a,0x00,0x4d,0x00,0x69,
0x00,0x63,0x00,0x72,0x00,0x6f,0x00,0x73,0x00,0x6f,0x00,0x66,0x00,0x74,0x00,0x2e,
0x00,0x63,0x00,0x6f,0x00,0x6d,0x30,0x1e,0x17,0x0d,0x39,0x39,0x30,0x39,0x32,0x34,
0x30,0x30,0x30,0x30,0x30,0x30,0x5a,0x17,0x0d,0x34,0x39,0x30,0x39,0x32,0x34,0x30,
0x30,0x30,0x30,0x30,0x30,0x5a,0x30,0x79,0x31,0x77,0x30,0x17,0x06,0x03,0x55,0x04,
0x03,0x1e,0x10,0x00,0x52,0x00,0x45,0x00,0x53,0x00,0x37,0x00,0x2d,0x00,0x4e,0x00,
0x45,0x00,0x57,0x30,0x17,0x06,0x03,0x55,0x04,0x07,0x1e,0x10,0x00,0x7a,0x00,0x32,
0x00,0x32,0x00,0x33,0x00,0x32,0x00,0x32,0x00,0x30,0x00,0x33,0x30,0x43,0x06,0x03,
0x55,0x04,0x05,0x1e,0x3c,0x00,0x31,0x00,0x42,0x00,0x63,0x00,0x4b,0x00,0x65,0x00,
0x57,0x00,0x50,0x00,0x6c,0x00,0x37,0x00,0x58,0x00,0x47,0x00,0x61,0x00,0x73,0x00,
0x38,0x00,0x4a,0x00,0x79,0x00,0x50,0x00,0x34,0x00,0x30,0x00,0x7a,0x00,0x49,0x00,
0x6d,0x00,0x6e,0x00,0x6f,0x00,0x51,0x00,0x5a,0x00,0x59,0x00,0x3d,0x00,0x0d,0x00,
0x0a,0x30,0x5c,0x30,0x0d,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01,
0x05,0x00,0x03,0x4b,0x00,0x30,0x48,0x02,0x41,0x00,0x91,0xb2,0x16,0x1c,0xae,0x4f,
0x7f,0x7c,0xaf,0x57,0x2b,0x23,0x4c,0x0c,0x25,0x3c,0x4f,0x66,0x9d,0x25,0xc3,0x4f,
0x29,0xee,0x8b,0xda,0x4e,0x95,0xe7,0x3b,0xaa,0xc0,0xa7,0xba,0xaf,0x99,0x8c,0x47,
0x24,0x8b,0x09,0x77,0xbc,0x2c,0xf4,0xe7,0x1a,0x07,0x58,0x7b,0x11,0x37,0x2a,0xa8,
0x90,0xc3,0x50,0x92,0x80,0x15,0xc5,0xda,0x51,0x8b,0x02,0x03,0x01,0x00,0x01,0xa3,
0x81,0xc3,0x30,0x81,0xc0,0x30,0x14,0x06,0x09,0x2b,0x06,0x01,0x04,0x01,0x82,0x37,
0x12,0x04,0x01,0x01,0xff,0x04,0x04,0x01,0x00,0x01,0x00,0x30,0x3c,0x06,0x09,0x2b,
0x06,0x01,0x04,0x01,0x82,0x37,0x12,0x02,0x01,0x01,0xff,0x04,0x2c,0x4d,0x00,0x69,
0x00,0x63,0x00,0x72,0x00,0x6f,0x00,0x73,0x00,0x6f,0x00,0x66,0x00,0x74,0x00,0x20,
0x00,0x43,0x00,0x6f,0x00,0x72,0x00,0x70,0x00,0x6f,0x00,0x72,0x00,0x61,0x00,0x74,
0x00,0x69,0x00,0x6f,0x00,0x6e,0x00,0x00,0x00,0x30,0x4c,0x06,0x09,0x2b,0x06,0x01,
0x04,0x01,0x82,0x37,0x12,0x05,0x01,0x01,0xff,0x04,0x3c,0x00,0x10,0x00,0x00,0x01,
0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x09,0x04,0x00,0x00,0x18,0x00,0x18,0x00,0x30,
0x00,0x01,0x00,0x32,0x00,0x33,0x00,0x36,0x00,0x2d,0x00,0x34,0x00,0x2e,0x00,0x30,
0x00,0x30,0x00,0x2d,0x00,0x45,0x00,0x58,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x1c,0x06,0x03,0x55,0x1d,0x23,0x01,0x01,
0xff,0x04,0x12,0x30,0x10,0xa1,0x07,0x81,0x05,0x4e,0x54,0x54,0x53,0x45,0x82,0x05,
0x01,0x00,0x00,0x00,0x01,0x30,0x09,0x06,0x05,0x2b,0x0e,0x03,0x02,0x1d,0x05,0x00,
0x03,0x41,0x00,0x7b,0x1d,0xfd,0x24,0xea,0xf2,0xe8,0x17,0xdd,0x88,0x7e,0xfd,0xee,
0x28,0x61,0x7a,0x02,0xc3,0x73,0xcf,0x32,0x0f,0x7c,0x66,0x87,0x31,0xa7,0xbe,0x1b,
0x31,0xe2,0x20,0xa5,0x76,0x91,0x68,0x97,0x53,0x9e,0x80,0xcd,0x2b,0xd0,0x8e,0x8b,
0x7f,0x89,0x1b,0x62,0xa8,0xf8,0xee,0x5e,0x56,0xbd,0x9c,0x6b,0x80,0x06,0x54,0xd3,
0xf0,0xbf,0xb2,0x31,0x00,0x01,0x00,0x14,0x00,0xc7,0x32,0xf2,0x5b,0x98,0x0e,0x04,
0x49,0xa0,0x27,0x7e,0xf5,0xf6,0x0f,0xda,0x08,0x1d,0xe9,0x79,0xd1,0x31,0xc6,0x50,
0x90,0x4a,0xd3,0x1f,0x1d,0xf0,0x65,0x0d,0xb6,0x1f,0xaf,0xc9,0x1d
};

/* Send license certificate and related data to the server */
void rdp_send_cert(HCONN conn)
{
	mcs_init_data(conn);
	prs_io_uint8s(&conn->out, precanned_cert_packet, sizeof(precanned_cert_packet));
	MARK_END(conn->out);
	mcs_send_data(conn, MCS_GLOBAL_CHANNEL, True);
}

/* Initialise RDP transport packet */
void rdp_init(HCONN conn)
{
	mcs_init_data(conn);
	PUSH_LAYER(conn->out, rdp_offset, 6);
}

/* Transmit RDP transport packet */
void rdp_send(HCONN conn, uint16 pdu_type)
{
	RDP_HEADER hdr;
	int length;

	POP_LAYER(conn->out, rdp_offset);
	length = conn->out.end - conn->out.offset;
	rdp_make_header(&hdr, length, pdu_type, conn->mcs_userid);
	rdp_io_header(&conn->out, &hdr);
	mcs_send_data(conn, MCS_GLOBAL_CHANNEL, True);
}

/* Initialise RDP transport data packet */
void rdp_init_data(HCONN conn)
{
	mcs_init_data(conn);
	PUSH_LAYER(conn->out, rdp_offset, 18);
}

/* Transmit RDP transport data packet */
void rdp_send_data(HCONN conn, uint16 data_pdu_type)
{
	RDP_HEADER hdr;
	RDP_DATA_HEADER datahdr;
	int length = conn->out.end - conn->out.offset;

	POP_LAYER(conn->out, rdp_offset);
	length = conn->out.end - conn->out.offset;
	rdp_make_header(&hdr, length, RDP_PDU_DATA, conn->mcs_userid);
	rdp_io_header(&conn->out, &hdr);
	rdp_make_data_header(&datahdr, 0x103ea, length, data_pdu_type);
	rdp_io_data_header(&conn->out, &datahdr);
	mcs_send_data(conn, MCS_GLOBAL_CHANNEL, True);
}

void rdp_send_confirm_active(HCONN conn)
{
	RDP_ACTIVE_PDU active;

	rdp_init(conn);
	rdp_make_active_pdu(&active, 0x103ea, conn->mcs_userid);
	rdp_io_active_pdu(&conn->out, &active, RDP_PDU_CONFIRM_ACTIVE);
	MARK_END(conn->out);
	rdp_send(conn, RDP_PDU_CONFIRM_ACTIVE);
}

void rdp_send_synchronize(HCONN conn)
{
	RDP_SYNCHRONIZE_PDU sync;

	rdp_init_data(conn);
	rdp_make_synchronize_pdu(&sync, 1002);
	rdp_io_synchronize_pdu(&conn->out, &sync);
	MARK_END(conn->out);
	rdp_send_data(conn, RDP_DATA_PDU_SYNCHRONIZE);
}

void rdp_send_control(HCONN conn, uint16 action)
{
	RDP_CONTROL_PDU control;

	rdp_init_data(conn);
	rdp_make_control_pdu(&control, action);
	rdp_io_control_pdu(&conn->out, &control);
	MARK_END(conn->out);
	rdp_send_data(conn, RDP_DATA_PDU_CONTROL);
}

void rdp_send_fonts(HCONN conn, uint16 seqno)
{
	RDP_FONT_PDU fonts;

	rdp_init_data(conn);
	rdp_make_font_pdu(&fonts, seqno);
	rdp_io_font_pdu(&conn->out, &fonts);
	MARK_END(conn->out);
	rdp_send_data(conn, RDP_DATA_PDU_FONT2);
}

void rdp_send_input(HCONN conn)
{
	RDP_INPUT_PDU input;

	rdp_init_data(conn);
	rdp_make_input_pdu(&input);
	rdp_io_input_pdu(&conn->out, &input);
	MARK_END(conn->out);
	rdp_send_data(conn, RDP_DATA_PDU_INPUT);
}

BOOL rdp_recv_pdu(HCONN conn, uint8 *type)
{
	RDP_HEADER hdr;

	if (!mcs_recv(conn, False) || !rdp_io_header(&conn->in, &hdr))
		return False;

	*type = hdr.pdu_type & 0xf;
	return True;
}

/* Disconnect from the RDP layer */
void rdp_disconnect(HCONN conn)
{
	mcs_disconnect(conn);
}

void rdp_make_header(RDP_HEADER *hdr, uint16 length, uint16 pdu_type,
		     uint16 userid)
{
	hdr->length = length;
	hdr->pdu_type = pdu_type | 0x10; /* Version 1 */
	hdr->userid = userid + 1001;
}

void rdp_make_data_header(RDP_DATA_HEADER *hdr, uint32 shareid,
			  uint16 length, uint16 data_pdu_type)
{
	hdr->shareid = shareid;
	hdr->pad = 0;
	hdr->streamid = 1;
	hdr->length = length - 14;
	hdr->data_pdu_type = data_pdu_type;
	hdr->compress_type = 0;
	hdr->compress_len = 0;
}

void rdp_make_general_caps(RDP_GENERAL_CAPS *caps)
{
	caps->os_major_type = 1;
	caps->os_minor_type = 3;
	caps->ver_protocol = 0x200;
}

void rdp_make_bitmap_caps(RDP_BITMAP_CAPS *caps)
{
	caps->preferred_bpp = 8;
	caps->receive1bpp = 1;
	caps->receive4bpp = 1;
	caps->receive8bpp = 1;
	caps->width = 640;
	caps->height = 480;
	caps->compression = 1;
	caps->unknown2 = 1;
}

void rdp_make_order_caps(RDP_ORDER_CAPS *caps)
{
	caps->xgranularity = 1;
	caps->ygranularity = 20;
	caps->max_order_level = 1;
	caps->num_fonts = 0x147;
	caps->cap_flags = 0x2A;

//	caps->cap_flags = ORDER_CAP_NEGOTIATE | ORDER_CAP_NOSUPPORT;

	caps->support[0] = caps->support[1] = caps->support[2]
		= caps->support[3] = caps->support[4] = caps->support[5]
		= caps->support[6] = caps->support[8] = caps->support[11]
		= caps->support[12] = caps->support[22] = caps->support[28]
		= caps->support[29] = caps->support[30] = 1;
	caps->text_cap_flags = 0x6A1;
	caps->desk_save_size = 0x38400;
	caps->unknown2 = 0x4E4;
}

void rdp_make_bmpcache_caps(RDP_BMPCACHE_CAPS *caps)
{
	caps->caches[0].entries = 0x258;
	caps->caches[0].max_cell_size = 0x100;
	caps->caches[1].entries = 0x12c;
	caps->caches[1].max_cell_size = 0x400;
	caps->caches[2].entries = 0x106;
	caps->caches[2].max_cell_size = 0x1000;
}

void rdp_make_control_caps(RDP_CONTROL_CAPS *caps)
{
	caps->control_interest = 2;
	caps->detach_interest = 2;
}

void rdp_make_activate_caps(RDP_ACTIVATE_CAPS *caps)
{
}

void rdp_make_pointer_caps(RDP_POINTER_CAPS *caps)
{
	caps->colour_pointer = 0;
	caps->cache_size = 20;
}

void rdp_make_share_caps(RDP_SHARE_CAPS *caps, uint16 userid)
{
}

void rdp_make_colcache_caps(RDP_COLCACHE_CAPS *caps)
{
	caps->cache_size = 6;
}

void rdp_make_active_pdu(RDP_ACTIVE_PDU *pdu, uint32 shareid, uint16 userid)
{
	memset(pdu, 0, sizeof(*pdu));
	pdu->shareid = shareid;
	pdu->userid  = 1002;
	pdu->source_len = sizeof(RDP_SOURCE);
	memcpy(pdu->source, RDP_SOURCE, sizeof(RDP_SOURCE));

	pdu->caps_len = RDP_CAPLEN_GENERAL + RDP_CAPLEN_BITMAP + RDP_CAPLEN_ORDER
		+ RDP_CAPLEN_BMPCACHE + RDP_CAPLEN_COLCACHE + RDP_CAPLEN_ACTIVATE
		+ RDP_CAPLEN_CONTROL + RDP_CAPLEN_POINTER + RDP_CAPLEN_SHARE
		+ RDP_CAPLEN_UNKNOWN;
	pdu->num_caps = 0xD;

	rdp_make_general_caps (&pdu->general_caps );
	rdp_make_bitmap_caps  (&pdu->bitmap_caps  );
	rdp_make_order_caps   (&pdu->order_caps   );
	rdp_make_bmpcache_caps(&pdu->bmpcache_caps);
	rdp_make_control_caps (&pdu->control_caps );
	rdp_make_activate_caps(&pdu->activate_caps);
	rdp_make_pointer_caps (&pdu->pointer_caps );
	rdp_make_share_caps   (&pdu->share_caps, userid);
	rdp_make_colcache_caps(&pdu->colcache_caps);
}

void rdp_make_control_pdu(RDP_CONTROL_PDU *pdu, uint16 action)
{
	pdu->action = action;
	pdu->userid = 0;
	pdu->controlid = 0;
}

void rdp_make_synchronize_pdu(RDP_SYNCHRONIZE_PDU *pdu, uint16 userid)
{
	pdu->type = 1;
	pdu->userid = userid;
}

void rdp_make_font_pdu(RDP_FONT_PDU *pdu, uint16 seqno)
{
	pdu->num_fonts = 0;
	pdu->unknown1 = 0x3e;
	pdu->unknown2 = seqno;
	pdu->entry_size = RDP_FONT_INFO_SIZE;
}

void rdp_make_input_pdu(RDP_INPUT_PDU *pdu)
{
	uint32 now = time(NULL);

	pdu->num_events = 3;
	pdu->pad = 0;

	pdu->event[0].event_time = now;
	pdu->event[0].message_type = RDP_INPUT_SYNCHRONIZE;
	pdu->event[0].device_flags = 0;
	pdu->event[0].mouse_x = 0;
	pdu->event[0].mouse_y = 0;

	pdu->event[1].event_time = now;
	pdu->event[1].message_type = RDP_INPUT_UNKNOWN;
	pdu->event[1].device_flags = 0x8000;
	pdu->event[1].mouse_x = 15;
	pdu->event[1].mouse_y = 0;

	pdu->event[2].event_time = now;
	pdu->event[2].message_type = RDP_INPUT_MOUSE;
	pdu->event[2].device_flags = MOUSE_FLAG_MOVE;
	pdu->event[2].mouse_x = 425;
	pdu->event[2].mouse_y = 493;
}

BOOL rdp_io_header(STREAM s, RDP_HEADER *hdr)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &hdr->length  ) : False;
	res = res ? lsb_io_uint16(s, &hdr->pdu_type) : False;
	res = res ? lsb_io_uint16(s, &hdr->userid  ) : False;

	return res;
}

BOOL rdp_io_data_header(STREAM s, RDP_DATA_HEADER *hdr)
{
	BOOL res = True;

	res = res ? lsb_io_uint32(s, &hdr->shareid      ) : False;
	res = res ? prs_io_uint8 (s, &hdr->pad          ) : False;
	res = res ? prs_io_uint8 (s, &hdr->streamid     ) : False;
	res = res ? lsb_io_uint16(s, &hdr->length       ) : False;
	res = res ? prs_io_uint8 (s, &hdr->data_pdu_type) : False;
	res = res ? prs_io_uint8 (s, &hdr->compress_type) : False;
	res = res ? lsb_io_uint16(s, &hdr->compress_len ) : False;

	return res;
}

BOOL rdp_io_general_caps(STREAM s, RDP_GENERAL_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_GENERAL;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->os_major_type ) : False;
	res = res ? lsb_io_uint16(s, &caps->os_minor_type ) : False;
	res = res ? lsb_io_uint16(s, &caps->ver_protocol  ) : False;
	res = res ? lsb_io_uint16(s, &caps->pad1          ) : False;
	res = res ? lsb_io_uint16(s, &caps->compress_types) : False;
	res = res ? lsb_io_uint16(s, &caps->pad2          ) : False;
	res = res ? lsb_io_uint16(s, &caps->cap_update    ) : False;
	res = res ? lsb_io_uint16(s, &caps->remote_unshare) : False;
	res = res ? lsb_io_uint16(s, &caps->compress_level) : False;
	res = res ? lsb_io_uint16(s, &caps->pad3          ) : False;

	return res;
}

BOOL rdp_io_bitmap_caps(STREAM s, RDP_BITMAP_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_BITMAP;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->preferred_bpp) : False;
	res = res ? lsb_io_uint16(s, &caps->receive1bpp  ) : False;
	res = res ? lsb_io_uint16(s, &caps->receive4bpp  ) : False;
	res = res ? lsb_io_uint16(s, &caps->receive8bpp  ) : False;
	res = res ? lsb_io_uint16(s, &caps->width        ) : False;
	res = res ? lsb_io_uint16(s, &caps->height       ) : False;
	res = res ? lsb_io_uint16(s, &caps->pad1         ) : False;
	res = res ? lsb_io_uint16(s, &caps->allow_resize ) : False;
	res = res ? lsb_io_uint16(s, &caps->compression  ) : False;
	res = res ? lsb_io_uint16(s, &caps->unknown1     ) : False;
	res = res ? lsb_io_uint16(s, &caps->unknown2     ) : False;
	res = res ? lsb_io_uint16(s, &caps->pad2         ) : False;

	return res;
}

BOOL rdp_io_order_caps(STREAM s, RDP_ORDER_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_ORDER;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? prs_io_uint8s(s,  caps->terminal_desc, 16) : False;
	res = res ? lsb_io_uint32(s, &caps->pad1             ) : False;
	res = res ? lsb_io_uint16(s, &caps->xgranularity     ) : False;
	res = res ? lsb_io_uint16(s, &caps->ygranularity     ) : False;
	res = res ? lsb_io_uint16(s, &caps->pad2             ) : False;
	res = res ? lsb_io_uint16(s, &caps->max_order_level  ) : False;
	res = res ? lsb_io_uint16(s, &caps->num_fonts        ) : False;
	res = res ? lsb_io_uint16(s, &caps->cap_flags        ) : False;
	res = res ? prs_io_uint8s(s,  caps->support      , 32) : False;
	res = res ? lsb_io_uint16(s, &caps->text_cap_flags   ) : False;
	res = res ? lsb_io_uint16(s, &caps->pad3             ) : False;
	res = res ? lsb_io_uint32(s, &caps->pad4             ) : False;
	res = res ? lsb_io_uint32(s, &caps->desk_save_size   ) : False;
	res = res ? lsb_io_uint32(s, &caps->unknown1         ) : False;
	res = res ? lsb_io_uint32(s, &caps->unknown2         ) : False;

	return res;
}

BOOL rdp_io_bmpcache_info(STREAM s, RDP_BMPCACHE_INFO *info)
{
	if (!lsb_io_uint16(s, &info->entries      ))
		return False;

	if (!lsb_io_uint16(s, &info->max_cell_size))
		return False;

	return True;
}

BOOL rdp_io_bmpcache_caps(STREAM s, RDP_BMPCACHE_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_BMPCACHE;
	uint16 pkt_length = length;
	BOOL res;
	int i;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	for (i = 0; i < 6; i++)
		res = res ? lsb_io_uint32(s, &caps->unused[i]) : False;

	for (i = 0; i < 3; i++)
		res = res ? rdp_io_bmpcache_info(s, &caps->caches[i]) : False;

	return res;
}

BOOL rdp_io_control_caps(STREAM s, RDP_CONTROL_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_CONTROL;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->control_caps    ) : False;
	res = res ? lsb_io_uint16(s, &caps->remote_detach   ) : False;
	res = res ? lsb_io_uint16(s, &caps->control_interest) : False;
	res = res ? lsb_io_uint16(s, &caps->detach_interest ) : False;

	return res;
}

BOOL rdp_io_activate_caps(STREAM s, RDP_ACTIVATE_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_ACTIVATE;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->help_key         ) : False;
	res = res ? lsb_io_uint16(s, &caps->help_index_key   ) : False;
	res = res ? lsb_io_uint16(s, &caps->help_extended_key) : False;
	res = res ? lsb_io_uint16(s, &caps->window_activate  ) : False;

	return res;
}

BOOL rdp_io_pointer_caps(STREAM s, RDP_POINTER_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_POINTER;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->colour_pointer) : False;
	res = res ? lsb_io_uint16(s, &caps->cache_size    ) : False;

	return res;
}

BOOL rdp_io_share_caps(STREAM s, RDP_SHARE_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_SHARE;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->userid) : False;
	res = res ? lsb_io_uint16(s, &caps->pad   ) : False;

	return res;
}

BOOL rdp_io_colcache_caps(STREAM s, RDP_COLCACHE_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_COLCACHE;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->cache_size) : False;
	res = res ? lsb_io_uint16(s, &caps->pad       ) : False;

	return res;
}

uint8 canned_caps[] = {
0x01,0x00,0x00,0x00,0x09,0x04,0x00,0x00,0x04,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0C,0x00,0x08,0x00,0x01,
0x00,0x00,0x00,0x0E,0x00,0x08,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x34,0x00,0xFE,
0x00,0x04,0x00,0xFE,0x00,0x04,0x00,0xFE,0x00,0x08,0x00,0xFE,0x00,0x08,0x00,0xFE,
0x00,0x10,0x00,0xFE,0x00,0x20,0x00,0xFE,0x00,0x40,0x00,0xFE,0x00,0x80,0x00,0xFE,
0x00,0x00,0x01,0x40,0x00,0x00,0x08,0x00,0x01,0x00,0x01,0x02,0x00,0x00,0x00
};

BOOL rdp_io_unknown_caps(STREAM s, void *caps)
{
	uint16 length = 0x58;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		fprintf(stderr, "Unrecognised capabilities size\n");
		return False;
	}

	res = res ? prs_io_uint8s(s, canned_caps, RDP_CAPLEN_UNKNOWN-4) : False;

	return res;
}

BOOL rdp_io_active_pdu(STREAM s, RDP_ACTIVE_PDU *pdu, int pdutype)
{
	uint16 capset;
	uint16 length;
	BOOL res;
	int i;

	res = lsb_io_uint32(s, &pdu->shareid);

	if (pdutype == RDP_PDU_CONFIRM_ACTIVE)
		res = res ? lsb_io_uint16(s, &pdu->userid    ) : False;

	res = res ? lsb_io_uint16(s, &pdu->source_len) : False;
	res = res ? lsb_io_uint16(s, &pdu->caps_len  ) : False;

	if (pdu->source_len > 48)
	{
		fprintf(stderr, "RDP source descriptor too long\n");
		return False;
	}

	res = res ? prs_io_uint8s(s,  pdu->source, pdu->source_len) : False;
	res = res ? lsb_io_uint16(s, &pdu->num_caps  ) : False;
	res = res ? lsb_io_uint16(s, &pdu->pad       ) : False;

	if (s->marshall)
        {
		capset = RDP_CAPSET_GENERAL;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_general_caps(s, &pdu->general_caps) : False;

		capset = RDP_CAPSET_BITMAP;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_bitmap_caps (s, &pdu->bitmap_caps ) : False;

		capset = RDP_CAPSET_ORDER;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_order_caps  (s, &pdu->order_caps  ) : False;

		capset = RDP_CAPSET_BMPCACHE;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_bmpcache_caps(s, &pdu->bmpcache_caps) : False;

		capset = RDP_CAPSET_COLCACHE;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_colcache_caps(s, &pdu->colcache_caps) : False;

		capset = RDP_CAPSET_ACTIVATE;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_activate_caps(s, &pdu->activate_caps) : False;

		capset = RDP_CAPSET_CONTROL;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_control_caps(s, &pdu->control_caps) : False;

		capset = RDP_CAPSET_POINTER;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_pointer_caps(s, &pdu->pointer_caps) : False;

		capset = RDP_CAPSET_SHARE;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_share_caps  (s, &pdu->share_caps  ) : False;

		capset = RDP_CAPSET_UNKNOWN;
		res = res ? lsb_io_uint16(s, &capset) : False;
		res = res ? rdp_io_unknown_caps(s, NULL) : False;
	}
	else
	{
		for (i = 0; i < pdu->num_caps; i++)
		{
			if (!res)
				return False;

			if (!lsb_io_uint16(s, &capset))
				return False;

			switch (capset)
			{
			case RDP_CAPSET_GENERAL:
				res = rdp_io_general_caps (s, &pdu->general_caps );
				break;
			case RDP_CAPSET_BITMAP:
				res = rdp_io_bitmap_caps  (s, &pdu->bitmap_caps  );
				break;
			case RDP_CAPSET_ORDER:
				res = rdp_io_order_caps   (s, &pdu->order_caps   );
				break;
			case RDP_CAPSET_BMPCACHE:
				res = rdp_io_bmpcache_caps(s, &pdu->bmpcache_caps);
				break;
			case RDP_CAPSET_CONTROL:
				res = rdp_io_control_caps (s, &pdu->control_caps );
				break;
			case RDP_CAPSET_ACTIVATE:
				res = rdp_io_activate_caps(s, &pdu->activate_caps);
				break;
			case RDP_CAPSET_POINTER:
				res = rdp_io_pointer_caps (s, &pdu->pointer_caps );
				break;
			case RDP_CAPSET_SHARE:
				res = rdp_io_share_caps   (s, &pdu->share_caps   );
				break;
			case RDP_CAPSET_COLCACHE:
				res = rdp_io_colcache_caps(s, &pdu->colcache_caps);
				break;
			default:
				fprintf(stderr, "Warning: Unrecognised capset %x\n",
					capset);

				if (!lsb_io_uint16(s, &length))
					return False;

				s->offset += (length - 4);
			}
		}
	}

	return res;
}

BOOL rdp_io_control_pdu(STREAM s, RDP_CONTROL_PDU *pdu)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &pdu->action   ) : False;
	res = res ? lsb_io_uint16(s, &pdu->userid   ) : False;
	res = res ? lsb_io_uint32(s, &pdu->controlid) : False;

	return res;
}

BOOL rdp_io_synchronize_pdu(STREAM s, RDP_SYNCHRONIZE_PDU *pdu)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &pdu->type  ) : False;
	res = res ? lsb_io_uint16(s, &pdu->userid) : False;

	return res;
}

BOOL rdp_io_input_event(STREAM s, RDP_INPUT_EVENT *evt)
{
	BOOL res = True;

	res = res ? lsb_io_uint32(s, &evt->event_time)   : False;
	res = res ? lsb_io_uint16(s, &evt->message_type) : False;

	if (!res)
		return False;

	switch (evt->message_type)
	{
	case RDP_INPUT_CODEPOINT:
	case RDP_INPUT_VIRTKEY:
		res = res ? lsb_io_uint16(s, &evt->device_flags) : False;
		res = res ? lsb_io_uint16(s, &evt->kbd_keycode ) : False;
		break;
	case RDP_INPUT_SYNCHRONIZE:
	case RDP_INPUT_UNKNOWN:
	case RDP_INPUT_MOUSE:
		res = res ? lsb_io_uint16(s, &evt->device_flags) : False;
		res = res ? lsb_io_uint16(s, &evt->mouse_x     ) : False;
		res = res ? lsb_io_uint16(s, &evt->mouse_y     ) : False;
		break;
	default:
		fprintf(stderr, "Unknown input type %d\n", evt->message_type);
		return False;
	}

	return res;
}

BOOL rdp_io_input_pdu(STREAM s, RDP_INPUT_PDU *pdu)
{
	BOOL res = True;
	int i;

	res = res ? lsb_io_uint16(s, &pdu->num_events) : False;
	res = res ? lsb_io_uint16(s, &pdu->pad       ) : False;

	if (pdu->num_events > RDP_MAX_EVENTS)
	{
		fprintf(stderr, "Too many events in one PDU\n");
		return False;
	}

	for (i = 0; i < pdu->num_events; i++)
	{
		res = res ? rdp_io_input_event(s, &pdu->event[i]) : False;
	}

	return res;
}

BOOL rdp_io_font_info(STREAM s, RDP_FONT_INFO *font)
{
	BOOL res = True;

	res = res ? prs_io_uint8s(s,  font->name, 32 ) : False;
	res = res ? lsb_io_uint16(s, &font->flags    ) : False;
	res = res ? lsb_io_uint16(s, &font->width    ) : False;
	res = res ? lsb_io_uint16(s, &font->height   ) : False;
	res = res ? lsb_io_uint16(s, &font->xaspect  ) : False;
	res = res ? lsb_io_uint16(s, &font->yaspect  ) : False;
	res = res ? lsb_io_uint32(s, &font->signature) : False;
	res = res ? lsb_io_uint16(s, &font->codepage ) : False;
	res = res ? lsb_io_uint16(s, &font->ascent   ) : False;

	return res;
}

BOOL rdp_io_font_pdu(STREAM s, RDP_FONT_PDU *pdu)
{
	BOOL res = True;
	int i;

	res = res ? lsb_io_uint16(s, &pdu->num_fonts ) : False;
	res = res ? lsb_io_uint16(s, &pdu->unknown1  ) : False;
	res = res ? lsb_io_uint16(s, &pdu->unknown2  ) : False;
	res = res ? lsb_io_uint16(s, &pdu->entry_size) : False;

	if (pdu->num_fonts > RDP_MAX_FONTS)
	{
		fprintf(stderr, "Too many fonts in one PDU\n");
		return False;
	}

	for (i = 0; i < pdu->num_fonts; i++)
	{
		res = res ? rdp_io_font_info(s, &pdu->font[i]) : False;
	}

	return res;
}

BOOL rdp_io_update_pdu(STREAM s, RDP_UPDATE_PDU *pdu)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &pdu->update_type) : False;
	res = res ? lsb_io_uint16(s, &pdu->pad        ) : False;

	return res;
}

BOOL rdp_io_secondary_order(STREAM s, RDP_SECONDARY_ORDER *rso)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &rso->length) : False;
	res = res ? lsb_io_uint16(s, &rso->flags ) : False;
	res = res ? prs_io_uint8 (s, &rso->type  ) : False;

	return res;
}

BOOL rdp_io_bitmap_header(STREAM s, RDP_BITMAP_HEADER *rdh)
{
	BOOL res = True;

	res = res ? prs_io_uint8 (s, &rdh->cache_id  ) : False;
	res = res ? prs_io_uint8 (s, &rdh->pad1      ) : False;
	res = res ? prs_io_uint8 (s, &rdh->width     ) : False;
	res = res ? prs_io_uint8 (s, &rdh->height    ) : False;
	res = res ? prs_io_uint8 (s, &rdh->bpp       ) : False;
	res = res ? lsb_io_uint16(s, &rdh->bufsize   ) : False;
	res = res ? lsb_io_uint16(s, &rdh->cache_idx ) : False;
	res = res ? lsb_io_uint16(s, &rdh->pad2      ) : False;
	res = res ? lsb_io_uint16(s, &rdh->size      ) : False;
	res = res ? lsb_io_uint16(s, &rdh->row_size  ) : False;
	res = res ? lsb_io_uint16(s, &rdh->final_size) : False;

	return res;
}
