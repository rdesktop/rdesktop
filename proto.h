/*
   rdesktop: A Remote Desktop Protocol client.
   Function prototypes
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

/* Parsing layer */
BOOL prs_io_uint8(STREAM s, uint8 *i);
BOOL prs_io_uint8s(STREAM s, uint8 *p, unsigned int length);
BOOL msb_io_uint16(STREAM s, uint16 *i);
BOOL lsb_io_uint16(STREAM s, uint16 *i);
BOOL lsb_io_uint32(STREAM s, uint32 *i);

/* TCP layer */
HCONN tcp_connect(char *server);
void tcp_disconnect(HCONN conn);
BOOL tcp_send(HCONN conn);
BOOL tcp_recv(HCONN conn, int length);

/* ISO layer */
HCONN iso_connect(char *server);
void iso_disconnect(HCONN conn);
BOOL iso_send_msg(HCONN conn, uint8 code);
BOOL iso_recv_msg(HCONN conn, uint8 *code);
void iso_init(struct connection *conn);
BOOL iso_send(HCONN conn);
BOOL iso_recv(HCONN conn);
void iso_make_tpkt(TPKT *tpkt, int length);
BOOL iso_io_tpkt(STREAM s, TPKT *tpkt);
void iso_make_tpdu(TPDU *tpdu, uint8 code);
BOOL iso_io_tpdu(STREAM s, TPDU *tpdu);

/* MCS layer */
HCONN mcs_connect(char *server);
BOOL mcs_join_channel(HCONN conn, uint16 chanid);
void mcs_disconnect(HCONN conn);
void mcs_send_connect_initial(HCONN conn);
void mcs_send_edrq(HCONN conn);
void mcs_send_aurq(HCONN conn);
void mcs_send_cjrq(HCONN conn, uint16 chanid);
void mcs_init_data(HCONN conn);
void mcs_send_data(HCONN conn, uint16 chanid, BOOL request);
int mcs_recv(HCONN conn, BOOL request);
void mcs_make_domain_params(DOMAIN_PARAMS *dp, uint16 max_channels,
	   uint16 max_users, uint16 max_tokens, uint16 max_pdusize);
void mcs_make_connect_initial(MCS_CONNECT_INITIAL *mci);
BOOL ber_io_header(STREAM s, BOOL islong, int tagval, int *length);
BOOL ber_io_octet_string(STREAM s, OCTET_STRING *os);
BOOL ber_io_integer(STREAM s, uint16 *i);
BOOL ber_io_uint8(STREAM s, uint8 *i, int tagval);
BOOL mcs_io_domain_params(STREAM s, DOMAIN_PARAMS *dp);
BOOL mcs_io_connect_initial(STREAM s, MCS_CONNECT_INITIAL *mci);
BOOL mcs_io_connect_response(STREAM s, MCS_CONNECT_RESPONSE *mcr);
BOOL mcs_io_edrq(STREAM s, MCS_EDRQ *edrq);
BOOL mcs_io_aurq(STREAM s, MCS_AURQ *aurq);
BOOL mcs_io_aucf(STREAM s, MCS_AUCF *aucf);
BOOL mcs_io_cjrq(STREAM s, MCS_CJRQ *cjrq);
BOOL mcs_io_cjcf(STREAM s, MCS_CJCF *cjcf);
BOOL mcs_io_data(STREAM s, MCS_DATA *dt, BOOL request);

/* RDP layer */
HCONN rdp_connect(char *server);
void process_orders(HCONN conn, RDP_ORDER_STATE *os);
void rdp_establish_key(HCONN conn);
void rdp_send_cert(HCONN conn);
void rdp_send_confirm_active(HCONN conn);
void rdp_send_control(HCONN conn, uint16 action);
void rdp_send_synchronize(HCONN conn);
void rdp_send_fonts(HCONN conn, uint16 seqno);
void rdp_send_input(HCONN conn);
BOOL rdp_recv_pdu(HCONN conn, uint8 *type);
void rdp_disconnect(HCONN conn);
void rdp_make_header(RDP_HEADER *hdr, uint16 length, uint16 pdu_type,
		     uint16 userid);
void rdp_make_data_header(RDP_DATA_HEADER *hdr, uint32 shareid,
			  uint16 length, uint16 data_pdu_type);
void rdp_make_general_caps(RDP_GENERAL_CAPS *caps);
void rdp_make_bitmap_caps(RDP_BITMAP_CAPS *caps);
void rdp_make_order_caps(RDP_ORDER_CAPS *caps);
void rdp_make_bmpcache_caps(RDP_BMPCACHE_CAPS *caps);
void rdp_make_control_caps(RDP_CONTROL_CAPS *caps);
void rdp_make_activate_caps(RDP_ACTIVATE_CAPS *caps);
void rdp_make_pointer_caps(RDP_POINTER_CAPS *caps);
void rdp_make_share_caps(RDP_SHARE_CAPS *caps, uint16 userid);
void rdp_make_colcache_caps(RDP_COLCACHE_CAPS *caps);
void rdp_make_active_pdu(RDP_ACTIVE_PDU *pdu, uint32 shareid, uint16 userid);
void rdp_make_control_pdu(RDP_CONTROL_PDU *pdu, uint16 action);
void rdp_make_synchronize_pdu(RDP_SYNCHRONIZE_PDU *pdu, uint16 userid);
void rdp_make_font_pdu(RDP_FONT_PDU *pdu, uint16 seqno);
void rdp_make_input_pdu(RDP_INPUT_PDU *pdu);
BOOL rdp_io_header(STREAM s, RDP_HEADER *hdr);
BOOL rdp_io_data_header(STREAM s, RDP_DATA_HEADER *hdr);
BOOL rdp_io_general_caps(STREAM s, RDP_GENERAL_CAPS *caps);
BOOL rdp_io_bitmap_caps(STREAM s, RDP_BITMAP_CAPS *caps);
BOOL rdp_io_order_caps(STREAM s, RDP_ORDER_CAPS *caps);
BOOL rdp_io_bmpcache_info(STREAM s, RDP_BMPCACHE_INFO *info);
BOOL rdp_io_bmpcache_caps(STREAM s, RDP_BMPCACHE_CAPS *caps);
BOOL rdp_io_control_caps(STREAM s, RDP_CONTROL_CAPS *caps);
BOOL rdp_io_activate_caps(STREAM s, RDP_ACTIVATE_CAPS *caps);
BOOL rdp_io_pointer_caps(STREAM s, RDP_POINTER_CAPS *caps);
BOOL rdp_io_share_caps(STREAM s, RDP_SHARE_CAPS *caps);
BOOL rdp_io_colcache_caps(STREAM s, RDP_COLCACHE_CAPS *caps);
BOOL rdp_io_active_pdu(STREAM s, RDP_ACTIVE_PDU *pdu, int pdutype);
BOOL rdp_io_control_pdu(STREAM s, RDP_CONTROL_PDU *pdu);
BOOL rdp_io_synchronize_pdu(STREAM s, RDP_SYNCHRONIZE_PDU *pdu);
BOOL rdp_io_input_event(STREAM s, RDP_INPUT_EVENT *evt);
BOOL rdp_io_input_pdu(STREAM s, RDP_INPUT_PDU *pdu);
BOOL rdp_io_font_info(STREAM s, RDP_FONT_INFO *font);
BOOL rdp_io_font_pdu(STREAM s, RDP_FONT_PDU *pdu);
BOOL rdp_io_update_pdu(STREAM s, RDP_UPDATE_PDU *pdu);
BOOL rdp_io_secondary_order(STREAM s, RDP_SECONDARY_ORDER *rso);
BOOL rdp_io_bitmap_header(STREAM s, RDP_BITMAP_HEADER *rdh);

/* Utility routines */
void *xmalloc(int size);
void *xrealloc(void *oldmem, int size);
BOOL bitmap_decompress(unsigned char *input, int size,
                       unsigned char *output, int width);

