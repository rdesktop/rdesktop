/*
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - Multipoint Communications Service
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

/* Establish a connection up to the MCS layer */
HCONN mcs_connect(char *server)
{
	HCONN conn;
	MCS_CONNECT_RESPONSE mcr;
	MCS_AUCF aucf;

	if ((conn = iso_connect(server)) == NULL)
		return NULL;

	mcs_send_connect_initial(conn);
	if (!iso_recv(conn) || !mcs_io_connect_response(&conn->in, &mcr))
	{
		ERROR("MCS error, expected Connect-Response\n");
		iso_disconnect(conn);
		return NULL;
	}

	if (mcr.result != 0)
	{
		ERROR("MCS-Connect-Initial failed, result %d\n", mcr.result);
		iso_disconnect(conn);
		return NULL;
	}

	mcs_send_edrq(conn);

	mcs_send_aurq(conn);
	if (!iso_recv(conn) || !mcs_io_aucf(&conn->in, &aucf))
	{
		ERROR("MCS error, expected AUcf\n");
		mcs_disconnect(conn);
		return NULL;
	}

	if (aucf.result != 0)
	{
		ERROR("AUrq failed, result %d\n", mcr.result);
		mcs_disconnect(conn);
		return NULL;
	}

	conn->mcs_userid = aucf.userid;

	if (!mcs_join_channel(conn, aucf.userid + 1001)
	    || !mcs_join_channel(conn, MCS_GLOBAL_CHANNEL))
	{
		mcs_disconnect(conn);
		return NULL;
	}

	return conn;
}

BOOL mcs_join_channel(HCONN conn, uint16 chanid)
{
	MCS_CJCF cjcf;

	mcs_send_cjrq(conn, chanid);
	if (!iso_recv(conn) || !mcs_io_cjcf(&conn->in, &cjcf))
	{
		ERROR("MCS error, expected CJcf\n");
		return False;
	}

	if (cjcf.result != 0)
	{
		ERROR("CJrq failed, result %d\n", cjcf.result);
		return False;
	}

	return True;
}

/* Disconnect from the MCS layer */
void mcs_disconnect(HCONN conn)
{
	/* Not complete */
	iso_disconnect(conn);
}

/* Send a Connect-Initial message */
void mcs_send_connect_initial(HCONN conn)
{
	MCS_CONNECT_INITIAL mci;

	iso_init(conn);
	mcs_make_connect_initial(&mci);
	mcs_io_connect_initial(&conn->out, &mci);
	MARK_END(conn->out);
	iso_send(conn);
}

/* Send a EDrq message */
void mcs_send_edrq(HCONN conn)
{
	MCS_EDRQ edrq;

	iso_init(conn);
	edrq.height = edrq.interval = 1;
	mcs_io_edrq(&conn->out, &edrq);
	MARK_END(conn->out);
	iso_send(conn);
}

/* Send a AUrq message */
void mcs_send_aurq(HCONN conn)
{
	MCS_AURQ aurq;

	iso_init(conn);
	mcs_io_aurq(&conn->out, &aurq);
	MARK_END(conn->out);
	iso_send(conn);
}

/* Send a CJrq message */
void mcs_send_cjrq(HCONN conn, uint16 chanid)
{
	MCS_CJRQ cjrq;

	iso_init(conn);
	cjrq.userid = conn->mcs_userid;
	cjrq.chanid = chanid;
	mcs_io_cjrq(&conn->out, &cjrq);
	MARK_END(conn->out);
	iso_send(conn);
}

/* Initialise MCS transport data packet */
void mcs_init_data(HCONN conn)
{
	iso_init(conn);
	PUSH_LAYER(conn->out, mcs_offset, 8);
}

/* Transmit MCS transport data packet */
void mcs_send_data(HCONN conn, uint16 chanid, BOOL request)
{
	MCS_DATA dt;

	POP_LAYER(conn->out, mcs_offset);
	dt.userid = conn->mcs_userid;
	dt.chanid = chanid;
	dt.flags = 0x70;
	dt.length = conn->out.end - conn->out.offset - 8;
	mcs_io_data(&conn->out, &dt, request);
	iso_send(conn);
}

/* Receive a message on the MCS layer */
BOOL mcs_recv(HCONN conn, BOOL request)
{
	MCS_DATA data;

	if (!iso_recv(conn) || !mcs_io_data(&conn->in, &data, request))
		return False;

	conn->in.rdp_offset = conn->in.offset;
	return True;
}

/* Initialise a DOMAIN_PARAMS structure */
void mcs_make_domain_params(DOMAIN_PARAMS *dp, uint16 max_channels,
	      uint16 max_users, uint16 max_tokens, uint16 max_pdusize)
{
	dp->max_channels   = max_channels;
	dp->max_users      = max_users;
	dp->max_tokens     = max_tokens;
	dp->num_priorities = 1;
	dp->min_throughput = 0;
	dp->max_height     = 1;
	dp->max_pdusize    = max_pdusize;
	dp->ver_protocol   = 2;
}

/* RDP-specific 'user data'. Let's just get this right for now - to be
   decoded later. */
char precanned_connect_userdata[] = {
   0x00,0x05,0x00,0x14,0x7c,0x00,0x01,0x80,0x9e,0x00,0x08,0x00,0x10,0x00,
   0x01,0xc0,0x00,0x44,0x75,0x63,0x61,0x80,0x90,0x01,0xc0,0x88,0x00,0x01,
   0x00,0x08,0x00,0x80,0x02,0xe0,0x01,0x01,0xca,0x03,0xaa,0x09,0x04,0x00,
   0x00,0xa3,0x01,0x00,0x00,0x52,0x00,0x45,0x00,0x53,0x00,0x31,0x00,0x2d,
   0x00,0x4e,0x00,0x45,0x00,0x57,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x01,0xca,0x00,0x00,0x02,0xc0,0x08,0x00,
   /* encryption disabled */ 0x00,0x00,0x00,0x00 };

char precanned_connect_userdata_e[] = {
0x00,
0x05,0x00,0x14,0x7c,0x00,0x01,0x80,0x9e,0x00,0x08,0x00,0x10,0x00,0x01,0xc0,0x00,
0x44,0x75,0x63,0x61,0x80,0x90,0x01,0xc0,0x88,0x00,0x01,0x00,0x08,0x00,0x80,0x02,
0xe0,0x01,0x01,0xca,0x03,0xaa,0x09,0x04,0x00,0x00,0xa3,0x01,0x00,0x00,0x57,0x00,
0x49,0x00,0x4e,0x00,0x39,0x00,0x35,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xca,0x00,0x00,0x02,0xc0,
0x08,0x00,0x01,0x00,0x00,0x00
};

char domain_data[] = {0x01};

/* Initialise a MCS_CONNECT_INITIAL structure */
void mcs_make_connect_initial(MCS_CONNECT_INITIAL *mci)
{
	mci->calling_domain.length = 1;
	mci->calling_domain.data = domain_data;

	mci->called_domain.length = 1;
	mci->called_domain.data = domain_data;

	mci->upward_flag = 0xff;

	mcs_make_domain_params(&mci->target_params,  2, 2, 0, 0xffff);
	mcs_make_domain_params(&mci->minimum_params, 1, 1, 1, 0x420);
	mcs_make_domain_params(&mci->maximum_params, 0xffff, 0xfc17, 0xffff,
			       0xffff);

	mci->user_data.length = sizeof(precanned_connect_userdata);
	mci->user_data.data = precanned_connect_userdata;

	mci->length = 2*2 + 3 + 3*34 + 4 + mci->user_data.length;
}

/* Marshall/demarshall an ASN.1 BER header */
BOOL ber_io_header(STREAM s, BOOL islong, int tagval, int *length)
{
	uint16 word_tag;
	uint8 byte_tag;
	uint16 word_len;
	uint8 byte_len;
	uint8 byte_int;
	int tag;
	BOOL res;

	/* Read/write tag */
	if (islong)
	{
		word_tag = tagval;
		res = msb_io_uint16(s, &word_tag);
		tag = word_tag;
	}
	else
	{
		byte_tag = tagval;
		res = prs_io_uint8(s, &byte_tag);
		tag = byte_tag;
	}

	if (!res || (tag != tagval))
	{
		ERROR("Invalid ASN.1 tag\n");
		return False;
	}

	/* Read/write length */
	if (s->marshall)
	{
		if (*length >= 0x80)
		{
			byte_len = 0x82;
			word_len = (uint16)*length;
			res = prs_io_uint8(s, &byte_len);
			res = res ? msb_io_uint16(s, &word_len) : False;
		}
		else
		{
			byte_len = (uint8)*length;
			res = prs_io_uint8(s, &byte_len);
		}
	}
	else
	{
		if (!prs_io_uint8(s, &byte_len))
			return False;

		if (byte_len & 0x80)
		{
			byte_len &= ~0x80;
			*length = 0;
			while (byte_len--)
			{
				if (!prs_io_uint8(s, &byte_int))
					return False;

				*length <<= 8;
				*length += byte_int;
			}
		}
		else *length = byte_len;
	}

	return res;
}

/* Marshall/demarshall an octet string (ASN.1 BER) */
BOOL ber_io_octet_string(STREAM s, OCTET_STRING *os)
{
	if (!ber_io_header(s, False, 4, &os->length))
		return False;

	if (os->length > s->end - s->offset)
		return False;

	if (s->marshall)
	{
		memcpy(s->data + s->offset, os->data, os->length);
	}
	else
	{
		os->data = malloc(os->length);
		memcpy(os->data, s->data + s->offset, os->length);
	}

	s->offset += os->length;
	return True;
}

/* Marshall/demarshall an integer (ASN.1 BER) */
BOOL ber_io_integer(STREAM s, uint16 *word_int)
{
	int length = 2;
	uint8 byte_int;
	BOOL res;

	if (!ber_io_header(s, False, 2, &length))
		return False;

	if (s->marshall)
	{
		res = msb_io_uint16(s, word_int);
	}
	else
	{
		*word_int = 0;
		while (length--)
		{
			if (!prs_io_uint8(s, &byte_int))
				return False;

			*word_int <<= 8;
			*word_int += byte_int;
		}
	}

	return res;
}

/* Marshall/demarshall a simple uint8 type (ASN.1 BER) */
BOOL ber_io_uint8(STREAM s, uint8 *i, int tagval)
{
	int length = 1;

	if (!ber_io_header(s, False, tagval, &length))
		return False;

	if (length != 1)
	{
		ERROR("Wrong length for simple type\n");
		return False;
	}

	return prs_io_uint8(s, i);
}

/* Marshall/demarshall a DOMAIN_PARAMS structure (ASN.1 BER) */
BOOL mcs_io_domain_params(STREAM s, DOMAIN_PARAMS *dp)
{
	int length = 32;
	BOOL res;

	res = ber_io_header(s, False, 0x30, &length);
	res = res ? ber_io_integer(s, &dp->max_channels  ) : False;
	res = res ? ber_io_integer(s, &dp->max_users     ) : False;
	res = res ? ber_io_integer(s, &dp->max_tokens    ) : False;
	res = res ? ber_io_integer(s, &dp->num_priorities) : False;
	res = res ? ber_io_integer(s, &dp->min_throughput) : False;
	res = res ? ber_io_integer(s, &dp->max_height    ) : False;
	res = res ? ber_io_integer(s, &dp->max_pdusize   ) : False;
	res = res ? ber_io_integer(s, &dp->ver_protocol  ) : False;

	return res;
}

/* Marshall/demarshall a MCS_CONNECT_INITIAL structure (ASN.1 BER) */
BOOL mcs_io_connect_initial(STREAM s, MCS_CONNECT_INITIAL *mci)
{
	BOOL res;

	res = ber_io_header(s, True, 0x7f65, &mci->length);
	res = res ? ber_io_octet_string (s, &mci->calling_domain) : False;
	res = res ? ber_io_octet_string (s, &mci->called_domain ) : False;
        res = res ? ber_io_uint8        (s, &mci->upward_flag, 1) : False;
	res = res ? mcs_io_domain_params(s, &mci->target_params ) : False;
	res = res ? mcs_io_domain_params(s, &mci->minimum_params) : False;
	res = res ? mcs_io_domain_params(s, &mci->maximum_params) : False;
	res = res ? ber_io_octet_string (s, &mci->user_data     ) : False;

	return res;
}

/* Marshall/demarshall a MCS_CONNECT_RESPONSE structure (ASN.1 BER) */
BOOL mcs_io_connect_response(STREAM s, MCS_CONNECT_RESPONSE *mcr)
{
	BOOL res;

	res = ber_io_header(s, True, 0x7f66, &mcr->length);
	res = res ? ber_io_uint8        (s, &mcr->result, 10   ) : False;
	res = res ? ber_io_integer      (s, &mcr->connect_id   ) : False;
	res = res ? mcs_io_domain_params(s, &mcr->domain_params) : False;
	res = res ? ber_io_octet_string (s, &mcr->user_data    ) : False;

	return res;
}

/* Marshall/demarshall an EDrq structure (ASN.1 PER) */
BOOL mcs_io_edrq(STREAM s, MCS_EDRQ *edrq)
{
	uint8 opcode = (1) << 2;
	uint8 pkt_opcode = opcode;
	BOOL res;

	res = prs_io_uint8(s, &pkt_opcode);
	if (pkt_opcode != opcode)
	{
		ERROR("Expected EDrq, received %x\n", pkt_opcode);
		return False;
	}

	res = res ? msb_io_uint16(s, &edrq->height  ) : False;
	res = res ? msb_io_uint16(s, &edrq->interval) : False;

	return res;
}

/* Marshall/demarshall an AUrq structure (ASN.1 PER) */
BOOL mcs_io_aurq(STREAM s, MCS_AURQ *aurq)
{
	uint8 opcode = (10) << 2;
	uint8 pkt_opcode = opcode;
	BOOL res;

	res = prs_io_uint8(s, &pkt_opcode);
	if (pkt_opcode != opcode)
	{
		ERROR("Expected AUrq, received %x\n", pkt_opcode);
		return False;
	}

	return res;
}

/* Marshall/demarshall an AUcf structure (ASN.1 PER) */
BOOL mcs_io_aucf(STREAM s, MCS_AUCF *aucf)
{
	uint8 opcode = (11) << 2;
	uint8 pkt_opcode = opcode | 2;
	BOOL res;

	res = prs_io_uint8(s, &pkt_opcode);
	if ((pkt_opcode & 0xfc) != opcode)
	{
		ERROR("Expected AUcf, received %x\n", pkt_opcode);
		return False;
	}

	res = res ? prs_io_uint8 (s, &aucf->result) : False;
	if (pkt_opcode & 2)
		res = res ? msb_io_uint16(s, &aucf->userid) : False;

	return res;
}

/* Marshall/demarshall an CJrq structure (ASN.1 PER) */
BOOL mcs_io_cjrq(STREAM s, MCS_CJRQ *cjrq)
{
	uint8 opcode = (14) << 2;
	uint8 pkt_opcode = opcode;
	BOOL res;

	res = prs_io_uint8(s, &pkt_opcode);
	if (pkt_opcode != opcode)
	{
		ERROR("Expected CJrq, received %x\n", pkt_opcode);
		return False;
	}

	res = res ? msb_io_uint16(s, &cjrq->userid) : False;
	res = res ? msb_io_uint16(s, &cjrq->chanid) : False;

	return res;
}

/* Marshall/demarshall an CJcf structure (ASN.1 PER) */
BOOL mcs_io_cjcf(STREAM s, MCS_CJCF *cjcf)
{
	uint8 opcode = (15) << 2;
	uint8 pkt_opcode = opcode | 2;
	BOOL res;

	res = prs_io_uint8(s, &pkt_opcode);
	if ((pkt_opcode & 0xfc) != opcode)
	{
		ERROR("Expected CJcf, received %x\n", pkt_opcode);
		return False;
	}

	res = res ? prs_io_uint8 (s, &cjcf->result) : False;
	res = res ? msb_io_uint16(s, &cjcf->userid) : False;
	res = res ? msb_io_uint16(s, &cjcf->req_chanid) : False;
	if (pkt_opcode & 2)
		res = res ? msb_io_uint16(s, &cjcf->join_chanid) : False;

	return res;
}

/* Marshall/demarshall an SDrq or SDin packet (ASN.1 PER) */
BOOL mcs_io_data(STREAM s, MCS_DATA *dt, BOOL request)
{
	uint8 opcode = (request ? 25 : 26) << 2;
	uint8 pkt_opcode = opcode;
	uint8 byte1, byte2;
	BOOL res;

	res = prs_io_uint8(s, &pkt_opcode);
	if (pkt_opcode != opcode)
	{
		ERROR("Expected MCS data, received %x\n", pkt_opcode);
		return False;
	}

	res = res ? msb_io_uint16(s, &dt->userid) : False;
	res = res ? msb_io_uint16(s, &dt->chanid) : False;
	res = res ? prs_io_uint8 (s, &dt->flags ) : False;

	if (s->marshall)
	{
		dt->length |= 0x8000;
		res = res ? msb_io_uint16(s, &dt->length) : False;
	}
	else
	{
		res = res ? prs_io_uint8(s, &byte1) : False;
		if (byte1 & 0x80)
		{
			res = res ? prs_io_uint8(s, &byte2) : False;
			dt->length = ((byte1 & ~0x80) << 8) + byte2;
		}
		else dt->length = byte1;
	}

	return res;
}

