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

/* ISO PDU codes */
enum ISO_PDU_CODE
{
        ISO_PDU_CR = 0xE0, /* Connection Request */
        ISO_PDU_CC = 0xD0, /* Connection Confirm */
        ISO_PDU_DR = 0x80, /* Disconnect Request */
        ISO_PDU_DT = 0xF0, /* Data */
        ISO_PDU_ER = 0x70  /* Error */
};

/* ISO transport encapsulation over TCP (RFC2126) */
typedef struct _TPKT
{
	uint8 version;
	uint8 reserved;
	uint16 length;

} TPKT;

/* ISO transport protocol PDU (RFC905) */
typedef struct _TPDU
{
        uint8 hlen;
        uint8 code;   

        /* CR, CC, DR PDUs */
        uint16 dst_ref;
        uint16 src_ref;
        uint8 class;

        /* DT PDU */
        uint8 eot;

} TPDU;
