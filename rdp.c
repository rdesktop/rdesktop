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
HCONN rdp_connect(char *server, int width, int height)
{
	HCONN conn;

	if ((conn = mcs_connect(server)) == NULL)
		return NULL;

	rdp_establish_key(conn);
	mcs_recv(conn, False); /* Server's licensing certificate */
	rdp_send_cert(conn);
	mcs_recv(conn, False);
	mcs_recv(conn, False);

	return conn;

}

/* Work this out later. This is useless anyway when encryption is off. */
uint8 precanned_key_packet[] = {
   0x48,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x33,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x6c,0x86, 
   0xf7,0x99,0xef,0x60,0xc4,0x49,0x52,0xd0,0xd8,0xea,0xb5,0x4f,0x58,0x19,
   0x52,0x2a,0x93,0x83,0x57,0x4f,0x4e,0x04,0xde,0x96,0x51,0xab,0x13,0x20,
   0xd8,0xe5,0x00,0x00,0x00,0x00,0x00,0x00
};

uint8 precanned_key_packet_e1[] = {
0x01,0x00,0x00,0x00,0x48,0x00,0x00,0x00,0x7c,0xbd,0x8b,0x8f,0x16,0x2b,0xa1,0x00,
0xc6,0xfb,0x8a,0x39,0xf5,0x33,0xed,0x36,0x14,0x55,0x17,0x8c,0x3a,0xde,0x5e,0xdf,
0xcb,0x41,0x4c,0xc7,0x89,0x7d,0xe3,0xe9,0x34,0x08,0xda,0xdc,0x08,0x77,0x98,0xda,
0x65,0xae,0x27,0x74,0xf1,0x79,0xd0,0x28,0x54,0x64,0x86,0x7f,0x02,0xe0,0x71,0x51,
0x56,0x4e,0xca,0x72,0x94,0x62,0x49,0x27,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

uint8 precanned_key_packet_e2[] = {
0x48,0x00,0x00,0x00,0x8a,0xe4,0x9f,0x8a,0xd5,0x04,0x02,0xfd,0x09,0x1f,0xff,0x53,
0xe0,0xb2,0x72,0x8b,0x19,0xba,0x22,0xe4,0x2a,0x7b,0xeb,0x79,0xa8,0x83,0x31,0x6f,
0x5c,0xcc,0x37,0x9c,0xe8,0x73,0x64,0x64,0xd3,0xab,0xaa,0x9f,0xbe,0x49,0x27,0xfc,
0x95,0xf3,0x6e,0xf8,0xb1,0x01,0x7c,0xba,0xa9,0xc5,0x35,0x9c,0x8f,0x74,0x3a,0x9f,
0xd4,0x26,0x4d,0x39,0x90,0xbe,0xf4,0xfb,0x72,0x9e,0x54,0x18
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

/* Create an RC4 key and transfer it to the server */
void rdp_establish_key_e1(HCONN conn)
{
	mcs_init_data(conn);
	memcpy(conn->out.data + conn->out.offset, precanned_key_packet_e1,
	       sizeof(precanned_key_packet_e1));
	conn->out.offset += sizeof(precanned_key_packet_e1);
	MARK_END(conn->out);
	mcs_send_data(conn, MCS_GLOBAL_CHANNEL, True);
}

/* Create an RC4 key and transfer it to the server */
void rdp_establish_key_e2(HCONN conn)
{
	mcs_init_data(conn);
	memcpy(conn->out.data + conn->out.offset, precanned_key_packet_e2,
	       sizeof(precanned_key_packet_e2));
	conn->out.offset += sizeof(precanned_key_packet_e2);
	MARK_END(conn->out);
	mcs_send_data(conn, MCS_GLOBAL_CHANNEL, True);
}

/* Horrible horrible certificate stuff. Work out later. */
uint8 precanned_cert_packet[] = { // 4c8
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

void rdp_send_confirm_active(HCONN conn, uint32 shareid, int width, int height)
{
	RDP_ACTIVE_PDU active;

	rdp_init(conn);
	rdp_make_active_pdu(&active, shareid, conn->mcs_userid, width, height);
	rdp_io_active_pdu(&conn->out, &active, RDP_PDU_CONFIRM_ACTIVE);
	MARK_END(conn->out);
	rdp_send(conn, RDP_PDU_CONFIRM_ACTIVE);
}

void rdp_send_synchronize(HCONN conn)
{
	RDP_SYNCHRONISE_PDU sync;

	rdp_init_data(conn);
	rdp_make_synchronise_pdu(&sync, 1002);
	rdp_io_synchronise_pdu(&conn->out, &sync);
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

void rdp_send_input(HCONN conn, uint16 message_type, uint16 device_flags,
				uint16 param1, uint16 param2)
{
	RDP_INPUT_PDU input;

	rdp_init_data(conn);
	rdp_make_input_pdu(&input, message_type, device_flags, param1, param2);
	rdp_io_input_pdu(&conn->out, &input);
	MARK_END(conn->out);
	rdp_send_data(conn, RDP_DATA_PDU_INPUT);
}

BOOL rdp_recv_pdu(HCONN conn, uint8 *type)
{
	RDP_HEADER hdr;

	conn->in.offset = conn->in.rdp_offset;

	if (conn->in.offset >= conn->in.end)
	{
		if (!mcs_recv(conn, False))
			return False;
	}

	if (!rdp_io_header(&conn->in, &hdr))
		return False;

	conn->in.rdp_offset += hdr.length;
	*type = hdr.pdu_type & 0xf;

#if DUMP
	fprintf(stderr, "RDP packet (type %x):\n", *type);
	dump_data(conn->in.data+conn->in.offset, conn->in.rdp_offset-conn->in.offset);
#endif

	return True;
}

/* Disconnect from the RDP layer */
void rdp_disconnect(HCONN conn)
{
	mcs_disconnect(conn);
}

/* Construct an RDP header */
void rdp_make_header(RDP_HEADER *hdr, uint16 length, uint16 pdu_type,
		     uint16 userid)
{
	hdr->length = length;
	hdr->pdu_type = pdu_type | 0x10; /* Version 1 */
	hdr->userid = userid + 1001;
}

/* Parse an RDP header */
BOOL rdp_io_header(STREAM s, RDP_HEADER *hdr)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &hdr->length  ) : False;
	res = res ? lsb_io_uint16(s, &hdr->pdu_type) : False;
	if ((hdr->pdu_type & 0xf) != RDP_PDU_DEACTIVATE)
		res = res ? lsb_io_uint16(s, &hdr->userid  ) : False;

	return res;
}

/* Construct a data header */
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

/* Parse a data header */
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

BOOL rdp_io_present(STREAM s, uint32 *present, uint8 flags, int size)
{
	uint8 bits;
	int i;

	if (flags & RDP_ORDER_SMALL)
	{
		size--;
	}

	if (flags & RDP_ORDER_TINY)
	{
		if (size < 2)
			return False;

		size -= 2;
	}

	*present = 0;
	for (i = 0; i < size; i++)
	{
		prs_io_uint8(s, &bits);
		*present |= bits << (i * 8);
	}

	return True;
}

BOOL rdp_io_coord(STREAM s, uint16 *coord, BOOL delta)
{
	uint8 change;
	BOOL res;

	if (delta)
	{
		res = prs_io_uint8(s, &change);
		*coord += (char)change;
	}
	else
	{
		res = lsb_io_uint16(s, coord);
	}

	return res;
}

BOOL rdp_io_colour(STREAM s, uint8 *colour)
{
	BOOL res;

	res = prs_io_uint8(s, colour);
	s->offset += 2;

	return res;
}

BOOL rdp_io_colourmap(STREAM s, COLOURMAP *colours)
{
	int datasize;

	lsb_io_uint16(s, &colours->ncolours);
	datasize = colours->ncolours * 3;

	if (datasize > sizeof(colours->colours))
		return False;

	memcpy(colours->colours, s->data + s->offset, datasize);
	s->offset += datasize;
	return True;
}

BOOL rdp_io_bounds(STREAM s, BOUNDS *bounds)
{
	uint8 present;

	prs_io_uint8(s, &present);

	if (present & 1)
		rdp_io_coord(s, &bounds->left, False);
	else if (present & 16)
		rdp_io_coord(s, &bounds->left, True);

	if (present & 2)
		rdp_io_coord(s, &bounds->top, False);
	else if (present & 32)
		rdp_io_coord(s, &bounds->top, True);

	if (present & 4)
		rdp_io_coord(s, &bounds->right, False);
	else if (present & 64)
		rdp_io_coord(s, &bounds->right, True);

	if (present & 8)
		rdp_io_coord(s, &bounds->bottom, False);
	else if (present & 128)
		rdp_io_coord(s, &bounds->bottom, True);

	return True;
}

BOOL rdp_io_pen(STREAM s, PEN *pen, uint32 present)
{
	BOOL res = True;

	if (present & 1)
		res = res ? prs_io_uint8(s, &pen->style) : False;

	if (present & 2)
		res = res ? prs_io_uint8(s, &pen->width) : False;

	if (present & 4)
		res = res ? rdp_io_colour(s, &pen->colour) : False;

	return res;
}

BOOL rdp_io_brush(STREAM s, BRUSH *brush, uint32 present)
{
	BOOL res = True;

	if (present & 1)
		res = res ? prs_io_uint8(s, &brush->xorigin) : False;

	if (present & 2)
		res = res ? prs_io_uint8(s, &brush->yorigin) : False;

	if (present & 4)
		res = res ? prs_io_uint8(s, &brush->style) : False;

	if (present & 8)
		res = res ? prs_io_uint8(s, &brush->pattern[0]) : False;

	if (present & 16)
		res = res ? prs_io_uint8s(s, &brush->pattern[1], 7) : False;

	return res;
}

/* Construct a confirm/demand active PDU */
void rdp_make_active_pdu(RDP_ACTIVE_PDU *pdu, uint32 shareid, uint16 userid,
				int width, int height)
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
	rdp_make_bitmap_caps  (&pdu->bitmap_caps, width, height);
	rdp_make_order_caps   (&pdu->order_caps   );
	rdp_make_bmpcache_caps(&pdu->bmpcache_caps);
	rdp_make_control_caps (&pdu->control_caps );
	rdp_make_activate_caps(&pdu->activate_caps);
	rdp_make_pointer_caps (&pdu->pointer_caps );
	rdp_make_share_caps   (&pdu->share_caps, userid);
	rdp_make_colcache_caps(&pdu->colcache_caps);
}

/* Parse a confirm/demand active PDU */
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
		ERROR("RDP source descriptor too long\n");
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
				NOTIMP("capset 0x%x\n", capset);

				if (!lsb_io_uint16(s, &length))
					return False;

				s->offset += (length - 4);
			}
		}
	}

	return res;
}

/* Construct a control PDU */
void rdp_make_control_pdu(RDP_CONTROL_PDU *pdu, uint16 action)
{
	pdu->action = action;
	pdu->userid = 0;
	pdu->controlid = 0;
}

/* Parse a control PDU */
BOOL rdp_io_control_pdu(STREAM s, RDP_CONTROL_PDU *pdu)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &pdu->action   ) : False;
	res = res ? lsb_io_uint16(s, &pdu->userid   ) : False;
	res = res ? lsb_io_uint32(s, &pdu->controlid) : False;

	return res;
}

/* Construct a synchronisation PDU */
void rdp_make_synchronise_pdu(RDP_SYNCHRONISE_PDU *pdu, uint16 userid)
{
	pdu->type = 1;
	pdu->userid = userid;
}

/* Parse a synchronisation PDU */
BOOL rdp_io_synchronise_pdu(STREAM s, RDP_SYNCHRONISE_PDU *pdu)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &pdu->type  ) : False;
	res = res ? lsb_io_uint16(s, &pdu->userid) : False;

	return res;
}

/* Parse a single input event */
BOOL rdp_io_input_event(STREAM s, RDP_INPUT_EVENT *evt)
{
	BOOL res = True;

	res = res ? lsb_io_uint32(s, &evt->event_time)   : False;
	res = res ? lsb_io_uint16(s, &evt->message_type) : False;
	res = res ? lsb_io_uint16(s, &evt->device_flags) : False;

	if (!res)
		return False;

	switch (evt->message_type)
	{
	case RDP_INPUT_CODEPOINT:
	case RDP_INPUT_VIRTKEY:
		res = res ? lsb_io_uint16(s, &evt->param1) : False;
		break;
	case RDP_INPUT_SYNCHRONIZE:
	case RDP_INPUT_SCANCODE:
	case RDP_INPUT_MOUSE:
		res = res ? lsb_io_uint16(s, &evt->param1) : False;
		res = res ? lsb_io_uint16(s, &evt->param2) : False;
		break;
	default:
		NOTIMP("input type %d\n", evt->message_type);
		return False;
	}

	return res;
}

/* Construct an input PDU */
void rdp_make_input_pdu(RDP_INPUT_PDU *pdu, uint16 message_type,
			uint16 device_flags, uint16 param1, uint16 param2)
{
	uint32 now = time(NULL);

	pdu->num_events = 1;
	pdu->pad = 0;

	pdu->event[0].event_time = now;
	pdu->event[0].message_type = message_type;
	pdu->event[0].device_flags = device_flags;
	pdu->event[0].param1 = param1;
	pdu->event[0].param2 = param2;
}

/* Parse an input PDU */
BOOL rdp_io_input_pdu(STREAM s, RDP_INPUT_PDU *pdu)
{
	BOOL res = True;
	int i;

	res = res ? lsb_io_uint16(s, &pdu->num_events) : False;
	res = res ? lsb_io_uint16(s, &pdu->pad       ) : False;

	if (pdu->num_events > RDP_MAX_EVENTS)
	{
		ERROR("Too many events in one PDU\n");
		return False;
	}

	for (i = 0; i < pdu->num_events; i++)
	{
		res = res ? rdp_io_input_event(s, &pdu->event[i]) : False;
	}

	return res;
}

/* Construct a font information PDU */
void rdp_make_font_pdu(RDP_FONT_PDU *pdu, uint16 seqno)
{
	pdu->num_fonts = 0;
	pdu->unknown1 = 0x3e;
	pdu->unknown2 = seqno;
	pdu->entry_size = RDP_FONT_INFO_SIZE;
}

/* Parse a font information structure */
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

/* Parse a font information PDU */
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
		ERROR("Too many fonts in one PDU\n");
		return False;
	}

	for (i = 0; i < pdu->num_fonts; i++)
	{
		res = res ? rdp_io_font_info(s, &pdu->font[i]) : False;
	}

	return res;
}

/* Parse a pointer PDU */
BOOL rdp_io_pointer_pdu(STREAM s, RDP_POINTER_PDU *ptr)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &ptr->message) : False;
	res = res ? lsb_io_uint16(s, &ptr->pad    ) : False;

	switch (ptr->message)
	{
		case RDP_POINTER_MOVE:
			res = res ? lsb_io_uint16(s, &ptr->x      ) : False;
			res = res ? lsb_io_uint16(s, &ptr->y      ) : False;
			break;
	}

	return res;
}

/* Parse an update PDU */
BOOL rdp_io_update_pdu(STREAM s, RDP_UPDATE_PDU *pdu)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &pdu->update_type) : False;
	res = res ? lsb_io_uint16(s, &pdu->pad        ) : False;

	return res;
}


/* PRIMARY ORDERS */

/* Parse an destination blt order */
BOOL rdp_io_destblt_order(STREAM s, DESTBLT_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x01)
		rdp_io_coord(s, &os->x, delta);

	if (present & 0x02)
		rdp_io_coord(s, &os->y, delta);

	if (present & 0x04)
		rdp_io_coord(s, &os->cx, delta);

	if (present & 0x08)
		rdp_io_coord(s, &os->cy, delta);

	if (present & 0x10)
		prs_io_uint8(s, &os->opcode);

	return PRS_ERROR(s);
}

/* Parse an pattern blt order */
BOOL rdp_io_patblt_order(STREAM s, PATBLT_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x0001)
		rdp_io_coord(s, &os->x, delta);

	if (present & 0x0002)
		rdp_io_coord(s, &os->y, delta);

	if (present & 0x0004)
		rdp_io_coord(s, &os->cx, delta);

	if (present & 0x0008)
		rdp_io_coord(s, &os->cy, delta);

	if (present & 0x0010)
		prs_io_uint8(s, &os->opcode);

	if (present & 0x0020)
		rdp_io_colour(s, &os->bgcolour);

	if (present & 0x0040)
		rdp_io_colour(s, &os->fgcolour);

	rdp_io_brush(s, &os->brush, present >> 7);

	return PRS_ERROR(s);
}

/* Parse an screen blt order */
BOOL rdp_io_screenblt_order(STREAM s, SCREENBLT_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x0001)
		rdp_io_coord(s, &os->x, delta);

	if (present & 0x0002)
		rdp_io_coord(s, &os->y, delta);

	if (present & 0x0004)
		rdp_io_coord(s, &os->cx, delta);

	if (present & 0x0008)
		rdp_io_coord(s, &os->cy, delta);

	if (present & 0x0010)
		prs_io_uint8(s, &os->opcode);

	if (present & 0x0020)
		rdp_io_coord(s, &os->srcx, delta);

	if (present & 0x0040)
		rdp_io_coord(s, &os->srcy, delta);

	return PRS_ERROR(s);
}

/* Parse a line order */
BOOL rdp_io_line_order(STREAM s, LINE_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x0001)
		lsb_io_uint16(s, &os->mixmode);

	if (present & 0x0002)
		rdp_io_coord(s, &os->startx, delta);

	if (present & 0x0004)
		rdp_io_coord(s, &os->starty, delta);

	if (present & 0x0008)
		rdp_io_coord(s, &os->endx, delta);

	if (present & 0x0010)
		rdp_io_coord(s, &os->endy, delta);

	if (present & 0x0020)
		rdp_io_colour(s, &os->bgcolour);

	if (present & 0x0040)
		prs_io_uint8(s, &os->opcode);

	rdp_io_pen(s, &os->pen, present >> 7);

	return PRS_ERROR(s);
}

/* Parse an opaque rectangle order */
BOOL rdp_io_rect_order(STREAM s, RECT_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x01)
		rdp_io_coord(s, &os->x, delta);

	if (present & 0x02)
		rdp_io_coord(s, &os->y, delta);

	if (present & 0x04)
		rdp_io_coord(s, &os->cx, delta);

	if (present & 0x08)
		rdp_io_coord(s, &os->cy, delta);

	if (present & 0x10)
		prs_io_uint8(s, &os->colour);

	return PRS_ERROR(s);
}

/* Parse a desktop save order */
BOOL rdp_io_desksave_order(STREAM s, DESKSAVE_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x01)
		lsb_io_uint32(s, &os->offset);

	if (present & 0x02)
		rdp_io_coord(s, &os->left, delta);

	if (present & 0x04)
		rdp_io_coord(s, &os->top, delta);

	if (present & 0x08)
		rdp_io_coord(s, &os->right, delta);

	if (present & 0x10)
		rdp_io_coord(s, &os->bottom, delta);

	if (present & 0x20)
		prs_io_uint8(s, &os->action);

	return PRS_ERROR(s);
}

/* Parse a memory blt order */
BOOL rdp_io_memblt_order(STREAM s, MEMBLT_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x0001)
	{
		prs_io_uint8(s, &os->cache_id);
		prs_io_uint8(s, &os->colour_table);
	}

	if (present & 0x0002)
		rdp_io_coord(s, &os->x, delta);

	if (present & 0x0004)
		rdp_io_coord(s, &os->y, delta);

	if (present & 0x0008)
		rdp_io_coord(s, &os->cx, delta);

	if (present & 0x0010)
		rdp_io_coord(s, &os->cy, delta);

	if (present & 0x0020)
		prs_io_uint8(s, &os->opcode);

	if (present & 0x0040)
		rdp_io_coord(s, &os->srcx, delta);

	if (present & 0x0080)
		rdp_io_coord(s, &os->srcy, delta);

	if (present & 0x0100)
		lsb_io_uint16(s, &os->cache_idx);

	return PRS_ERROR(s);
}

/* Parse a 3-way blt order */
BOOL rdp_io_triblt_order(STREAM s, TRIBLT_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x000001)
	{
		prs_io_uint8(s, &os->cache_id);
		prs_io_uint8(s, &os->colour_table);
	}

	if (present & 0x000002)
		rdp_io_coord(s, &os->x, delta);

	if (present & 0x000004)
		rdp_io_coord(s, &os->y, delta);

	if (present & 0x000008)
		rdp_io_coord(s, &os->cx, delta);

	if (present & 0x000010)
		rdp_io_coord(s, &os->cy, delta);

	if (present & 0x000020)
		prs_io_uint8(s, &os->opcode);

	if (present & 0x000040)
		rdp_io_coord(s, &os->srcx, delta);

	if (present & 0x000080)
		rdp_io_coord(s, &os->srcy, delta);

	if (present & 0x000100)
		rdp_io_colour(s, &os->bgcolour);

	if (present & 0x000200)
		rdp_io_colour(s, &os->fgcolour);

	rdp_io_brush(s, &os->brush, present >> 10);

	if (present & 0x008000)
		lsb_io_uint16(s, &os->cache_idx);

	if (present & 0x010000)
		lsb_io_uint16(s, &os->unknown);

	return PRS_ERROR(s);
}

/* Parse a text order */
BOOL rdp_io_text2_order(STREAM s, TEXT2_ORDER *os, uint32 present, BOOL delta)
{
	if (present & 0x000001)
		prs_io_uint8(s, &os->font);

	if (present & 0x000002)
		prs_io_uint8(s, &os->flags);

	if (present & 0x000004)
		prs_io_uint8(s, &os->unknown);

	if (present & 0x000008)
		prs_io_uint8(s, &os->mixmode);

	if (present & 0x000010)
		rdp_io_colour(s, &os->fgcolour);

	if (present & 0x000020)
		rdp_io_colour(s, &os->bgcolour);

	if (present & 0x000040)
		lsb_io_uint16(s, &os->clipleft);

	if (present & 0x000080)
		lsb_io_uint16(s, &os->cliptop);

	if (present & 0x000100)
		lsb_io_uint16(s, &os->clipright);

	if (present & 0x000200)
		lsb_io_uint16(s, &os->clipbottom);

	if (present & 0x000400)
		lsb_io_uint16(s, &os->boxleft);

	if (present & 0x000800)
		lsb_io_uint16(s, &os->boxtop);

	if (present & 0x001000)
		lsb_io_uint16(s, &os->boxright);

	if (present & 0x002000)
		lsb_io_uint16(s, &os->boxbottom);

	if (present & 0x080000)
		lsb_io_uint16(s, &os->x);

	if (present & 0x100000)
		lsb_io_uint16(s, &os->y);

	if (present & 0x200000)
	{
		prs_io_uint8(s, &os->length);
		prs_io_uint8s(s, os->text, os->length);
	}

	return PRS_ERROR(s);
}


/* SECONDARY ORDERS */

BOOL rdp_io_secondary_order(STREAM s, RDP_SECONDARY_ORDER *rso)
{
	BOOL res = True;

	res = res ? lsb_io_uint16(s, &rso->length) : False;
	res = res ? lsb_io_uint16(s, &rso->flags ) : False;
	res = res ? prs_io_uint8 (s, &rso->type  ) : False;

	return res;
}

BOOL rdp_io_raw_bmpcache_order(STREAM s, RDP_RAW_BMPCACHE_ORDER *rbo)
{
	BOOL res = True;

	res = res ? prs_io_uint8 (s, &rbo->cache_id  ) : False;
	res = res ? prs_io_uint8 (s, &rbo->pad1      ) : False;
	res = res ? prs_io_uint8 (s, &rbo->width     ) : False;
	res = res ? prs_io_uint8 (s, &rbo->height    ) : False;
	res = res ? prs_io_uint8 (s, &rbo->bpp       ) : False;
	res = res ? lsb_io_uint16(s, &rbo->bufsize   ) : False;
	res = res ? lsb_io_uint16(s, &rbo->cache_idx ) : False;

	rbo->data = s->data + s->offset;
	s->offset += rbo->bufsize;

	return res;
}

BOOL rdp_io_bmpcache_order(STREAM s, RDP_BMPCACHE_ORDER *rbo)
{
	BOOL res = True;

	res = res ? prs_io_uint8 (s, &rbo->cache_id  ) : False;
	res = res ? prs_io_uint8 (s, &rbo->pad1      ) : False;
	res = res ? prs_io_uint8 (s, &rbo->width     ) : False;
	res = res ? prs_io_uint8 (s, &rbo->height    ) : False;
	res = res ? prs_io_uint8 (s, &rbo->bpp       ) : False;
	res = res ? lsb_io_uint16(s, &rbo->bufsize   ) : False;
	res = res ? lsb_io_uint16(s, &rbo->cache_idx ) : False;
	res = res ? lsb_io_uint16(s, &rbo->pad2      ) : False;
	res = res ? lsb_io_uint16(s, &rbo->size      ) : False;
	res = res ? lsb_io_uint16(s, &rbo->row_size  ) : False;
	res = res ? lsb_io_uint16(s, &rbo->final_size) : False;

	rbo->data = s->data + s->offset;
	s->offset += rbo->size;

	return res;
}

BOOL rdp_io_colcache_order(STREAM s, RDP_COLCACHE_ORDER *colours)
{
	COLOURENTRY *entry;
	int i;

	prs_io_uint8(s, &colours->cache_id);
	lsb_io_uint16(s, &colours->map.ncolours);

	for (i = 0; i < colours->map.ncolours; i++)
	{
		entry = &colours->map.colours[i];
		prs_io_uint8(s, &entry->blue);
		prs_io_uint8(s, &entry->green);
		prs_io_uint8(s, &entry->red);
		s->offset++;
	}

	return True;
}

BOOL rdp_io_fontcache_order(STREAM s, RDP_FONTCACHE_ORDER *font)
{
	RDP_FONT_GLYPH *glyph;
	BOOL res = True;
	int i, j, datasize;
	uint8 in, out;

	res = res ? prs_io_uint8(s, &font->font   ) : False;
	res = res ? prs_io_uint8(s, &font->nglyphs) : False;

	for (i = 0; i < font->nglyphs; i++)
	{
		glyph = &font->glyphs[i];
		res = res ? lsb_io_uint16(s, &glyph->character) : False;
		res = res ? lsb_io_uint16(s, &glyph->unknown  ) : False;
		res = res ? lsb_io_uint16(s, &glyph->baseline ) : False;
		res = res ? lsb_io_uint16(s, &glyph->width    ) : False;
		res = res ? lsb_io_uint16(s, &glyph->height   ) : False;

		datasize = (glyph->height * ((glyph->width + 7) / 8) + 3) & ~3;
		res = res ? prs_io_uint8s(s, glyph->data, datasize) : False;
		for (j = 0; j < datasize; j++)
		{
			in = glyph->data[j];
			out = 0;
			if (in & 1) out |= 128;
			if (in & 2) out |= 64;
			if (in & 4) out |= 32;
			if (in & 8) out |= 16;
			if (in & 16) out |= 8;
			if (in & 32) out |= 4;
			if (in & 64) out |= 2;
			if (in & 128) out |= 1;
			glyph->data[j] = out;
		}
	} 

	return res;
}


/* CAPABILITIES */

/* Construct a general capability set */
void rdp_make_general_caps(RDP_GENERAL_CAPS *caps)
{
	caps->os_major_type = 1;
	caps->os_minor_type = 3;
	caps->ver_protocol = 0x200;
}

/* Parse general capability set */
BOOL rdp_io_general_caps(STREAM s, RDP_GENERAL_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_GENERAL;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
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

/* Construct a bitmap capability set */
void rdp_make_bitmap_caps(RDP_BITMAP_CAPS *caps, int width, int height)
{
	caps->preferred_bpp = 8;
	caps->receive1bpp = 1;
	caps->receive4bpp = 1;
	caps->receive8bpp = 1;
	caps->width = width;
	caps->height = height;
	caps->compression = 1;
	caps->unknown2 = 1;
}

/* Parse bitmap capability set */
BOOL rdp_io_bitmap_caps(STREAM s, RDP_BITMAP_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_BITMAP;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
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

/* Construct an order capability set */
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

/* Parse order capability set */
BOOL rdp_io_order_caps(STREAM s, RDP_ORDER_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_ORDER;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
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

/* Construct a bitmap cache capability set */
void rdp_make_bmpcache_caps(RDP_BMPCACHE_CAPS *caps)
{
	caps->caches[0].entries = 0x258;
	caps->caches[0].max_cell_size = 0x100;
	caps->caches[1].entries = 0x12c;
	caps->caches[1].max_cell_size = 0x400;
	caps->caches[2].entries = 0x106;
	caps->caches[2].max_cell_size = 0x1000;
}

/* Parse single bitmap cache information structure */
BOOL rdp_io_bmpcache_info(STREAM s, RDP_BMPCACHE_INFO *info)
{
	if (!lsb_io_uint16(s, &info->entries      ))
		return False;

	if (!lsb_io_uint16(s, &info->max_cell_size))
		return False;

	return True;
}

/* Parse bitmap cache capability set */
BOOL rdp_io_bmpcache_caps(STREAM s, RDP_BMPCACHE_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_BMPCACHE;
	uint16 pkt_length = length;
	BOOL res;
	int i;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
		return False;
	}

	for (i = 0; i < 6; i++)
		res = res ? lsb_io_uint32(s, &caps->unused[i]) : False;

	for (i = 0; i < 3; i++)
		res = res ? rdp_io_bmpcache_info(s, &caps->caches[i]) : False;

	return res;
}

/* Construct a control capability set */
void rdp_make_control_caps(RDP_CONTROL_CAPS *caps)
{
	caps->control_interest = 2;
	caps->detach_interest = 2;
}

/* Parse control capability set */
BOOL rdp_io_control_caps(STREAM s, RDP_CONTROL_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_CONTROL;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->control_caps    ) : False;
	res = res ? lsb_io_uint16(s, &caps->remote_detach   ) : False;
	res = res ? lsb_io_uint16(s, &caps->control_interest) : False;
	res = res ? lsb_io_uint16(s, &caps->detach_interest ) : False;

	return res;
}

/* Construct an activation capability set */
void rdp_make_activate_caps(RDP_ACTIVATE_CAPS *caps)
{
}

/* Parse activation capability set */
BOOL rdp_io_activate_caps(STREAM s, RDP_ACTIVATE_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_ACTIVATE;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->help_key         ) : False;
	res = res ? lsb_io_uint16(s, &caps->help_index_key   ) : False;
	res = res ? lsb_io_uint16(s, &caps->help_extended_key) : False;
	res = res ? lsb_io_uint16(s, &caps->window_activate  ) : False;

	return res;
}

/* Construct a pointer capability set */
void rdp_make_pointer_caps(RDP_POINTER_CAPS *caps)
{
	caps->colour_pointer = 0;
	caps->cache_size = 20;
}

/* Parse pointer capability set */
BOOL rdp_io_pointer_caps(STREAM s, RDP_POINTER_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_POINTER;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->colour_pointer) : False;
	res = res ? lsb_io_uint16(s, &caps->cache_size    ) : False;

	return res;
}

/* Construct a share capability set */
void rdp_make_share_caps(RDP_SHARE_CAPS *caps, uint16 userid)
{
}

/* Parse share capability set */
BOOL rdp_io_share_caps(STREAM s, RDP_SHARE_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_SHARE;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
		return False;
	}

	res = res ? lsb_io_uint16(s, &caps->userid) : False;
	res = res ? lsb_io_uint16(s, &caps->pad   ) : False;

	return res;
}

/* Construct a colour cache capability set */
void rdp_make_colcache_caps(RDP_COLCACHE_CAPS *caps)
{
	caps->cache_size = 6;
}

/* Parse colour cache capability set */
BOOL rdp_io_colcache_caps(STREAM s, RDP_COLCACHE_CAPS *caps)
{
	uint16 length = RDP_CAPLEN_COLCACHE;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
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

/* Insert canned capabilities */
BOOL rdp_io_unknown_caps(STREAM s, void *caps)
{
	uint16 length = 0x58;
	uint16 pkt_length = length;
	BOOL res;

	res = lsb_io_uint16(s, &pkt_length);
	if (pkt_length != length)
	{
		ERROR("Unrecognised capabilities size\n");
		return False;
	}

	res = res ? prs_io_uint8s(s, canned_caps, RDP_CAPLEN_UNKNOWN-4) : False;

	return res;
}
