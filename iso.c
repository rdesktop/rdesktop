/*
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - ISO layer
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

/* Establish a connection up to the ISO layer */
HCONN iso_connect(char *server)
{
	HCONN conn;
	uint8 code;

	if ((conn = tcp_connect(server)) == NULL)
		return NULL;

	iso_send_msg(conn, ISO_PDU_CR);

	if (!iso_recv_msg(conn, &code) || (code != ISO_PDU_CC))
	{
		ERROR("ISO error, expected CC\n");
		tcp_disconnect(conn);
		return NULL;
	}

	return conn;
}

/* Disconnect from the ISO layer */
void iso_disconnect(HCONN conn)
{
	iso_send_msg(conn, ISO_PDU_DR);
	tcp_disconnect(conn);
}

/* Send self-contained ISO message identified by code */
BOOL iso_send_msg(HCONN conn, uint8 code)
{
	TPKT tpkt;
	TPDU tpdu;

	iso_make_tpkt(&tpkt, 11);
	iso_io_tpkt(&conn->out, &tpkt);
	iso_make_tpdu(&tpdu, code);
	iso_io_tpdu(&conn->out, &tpdu);
	MARK_END(conn->out);
	return tcp_send(conn);
}

/* Receive a message on the ISO layer, return code */
BOOL iso_recv_msg(HCONN conn, uint8 *code)
{
	TPDU tpdu;
	TPKT tpkt;
	BOOL res;

	res = tcp_recv(conn, 4);
	res = res ? iso_io_tpkt(&conn->in, &tpkt) : False;
	res = res ? tcp_recv(conn, tpkt.length - 4) : False;
	res = res ? iso_io_tpdu(&conn->in, &tpdu) : False;

	*code = tpdu.code;
	return res;
}

/* Initialise ISO transport data packet */
void iso_init(struct connection *conn)
{
	PUSH_LAYER(conn->out, iso_offset, 7);
}

/* Receive ISO transport data packet */
BOOL iso_recv(HCONN conn)
{
	uint8 code;

	if (!iso_recv_msg(conn, &code) || (code != ISO_PDU_DT))
	{
		ERROR("ISO error, expected DT\n");
		return False;
	}

	return True;
}

/* Receive ISO transport data packet */
BOOL iso_send(HCONN conn)
{
	TPKT tpkt;
	TPDU tpdu;

	POP_LAYER(conn->out, iso_offset);
	iso_make_tpkt(&tpkt, conn->out.end);
	iso_io_tpkt(&conn->out, &tpkt);
	iso_make_tpdu(&tpdu, ISO_PDU_DT);
	iso_io_tpdu(&conn->out, &tpdu);
	return tcp_send(conn);
}

/* Initialise a TPKT structure */
void iso_make_tpkt(TPKT *tpkt, int length)
{
	tpkt->version = 3;
	tpkt->reserved = 0;
	tpkt->length = length;
}

/* Marshall/demarshall a TPKT structure */
BOOL iso_io_tpkt(STREAM s, TPKT *tpkt)
{
	if (!prs_io_uint8(s, &tpkt->version))
		return False;

	if (tpkt->version != 3)
	{
		ERROR("Wrong TPKT version %d\n", tpkt->version);
		return False;
	}

	if (!prs_io_uint8 (s, &tpkt->reserved))
		return False;

	if (!msb_io_uint16(s, &tpkt->length))
		return False;

	return True;
}

/* Initialise a TPDU structure */
void iso_make_tpdu(TPDU *tpdu, uint8 code)
{
	tpdu->hlen = (code == ISO_PDU_DT) ? 2 : 6;
	tpdu->code = code;
	tpdu->dst_ref = tpdu->src_ref = 0;
	tpdu->class = 0;
	tpdu->eot = 0x80;
}

/* Marshall/demarshall a TPDU structure */
BOOL iso_io_tpdu(STREAM s, TPDU *tpdu)
{
	BOOL res = True;

	res = res ? prs_io_uint8 (s, &tpdu->hlen) : False;
	res = res ? prs_io_uint8 (s, &tpdu->code) : False;

	if (tpdu->code == ISO_PDU_DT)
	{
		res = res ? prs_io_uint8(s, &tpdu->eot) : False;
	}
	else
	{
		res = res ? msb_io_uint16(s, &tpdu->dst_ref) : False;
		res = res ? msb_io_uint16(s, &tpdu->src_ref) : False;
		res = res ? prs_io_uint8 (s, &tpdu->class  ) : False;
	}

	return res;
}
