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

/* An ASN.1 octet string */
typedef struct _OCTET_STRING
{
	int length;
	unsigned char *data;

} OCTET_STRING;

/* MCS domain parameters */
typedef struct _DOMAIN_PARAMS
{
	uint16 max_channels;
	uint16 max_users;
	uint16 max_tokens;
	uint16 num_priorities;
	uint16 min_throughput;
	uint16 max_height;
	uint16 max_pdusize;
	uint16 ver_protocol;

} DOMAIN_PARAMS;

/* MCS-CONNECT-INITIAL request */
typedef struct _MCS_CONNECT_INITIAL
{
	int length;

	OCTET_STRING calling_domain;
	OCTET_STRING called_domain;
	uint8 upward_flag;
	DOMAIN_PARAMS target_params;
	DOMAIN_PARAMS minimum_params;
	DOMAIN_PARAMS maximum_params;
	OCTET_STRING user_data;

} MCS_CONNECT_INITIAL;

/* MCS-CONNECT-RESPONSE */
typedef struct _MCS_CONNECT_RESPONSE
{
	int length;

	uint8 result;
	uint16 connect_id;
	DOMAIN_PARAMS domain_params;
	OCTET_STRING user_data;

} MCS_CONNECT_RESPONSE;

/* EDrq - Erect Domain Request */
typedef struct _MCS_EDRQ
{
	uint16 height;
	uint16 interval;

} MCS_EDRQ;

/* AUrq - Attach User Request */
typedef struct _MCS_AURQ
{

} MCS_AURQ;

/* AUcf - Attach User Confirm */
typedef struct _MCS_AUCF
{
	uint8 result;
	uint16 userid;

} MCS_AUCF;

/* CJrq - Channel Join Request */
typedef struct _MCS_CJRQ
{
	uint16 userid;
	uint16 chanid;

} MCS_CJRQ;

/* CJcf - Channel Join Confirm */
typedef struct _MCS_CJCF
{
	uint8 result;
	uint16 userid;
	uint16 req_chanid;
	uint16 join_chanid;

} MCS_CJCF;

/* SDrq/SDin - Send Data */
typedef struct _MCS_DATA
{
	uint16 userid;
	uint16 chanid;
	uint8 flags;
	uint16 length;

} MCS_DATA;

#define MCS_GLOBAL_CHANNEL 1003
