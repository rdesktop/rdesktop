/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Secure sockets abstraction layer
   Copyright (C) Matthew Chapman 1999-2008
   Copyright (C) Jay Sorg 2006-2008
   Copyright 2017 Henrik Andersson <hean01@cendio.se> for Cendio AB
   Copyright 2017 Alexander Zakharov <uglym8@gmail.com>

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

#ifndef _RDSSL_H
#define _RDSSL_H

#include <nettle/md5.h>
#include <nettle/sha1.h>
#include <nettle/arcfour.h>
#include <nettle/hmac.h>
#include <nettle/rsa.h>

#include <gnutls/x509.h>

#define RDSSL_RC4 struct arcfour_ctx
#define RDSSL_SHA1 struct sha1_ctx
#define RDSSL_MD5 struct md5_ctx
#define RDSSL_CERT gnutls_x509_crt_t
#define RDSSL_RKEY struct rsa_public_key

void rdssl_sha1_init(RDSSL_SHA1 * sha1);
void rdssl_sha1_update(RDSSL_SHA1 * sha1, uint8 * data, uint32 len);
void rdssl_sha1_final(RDSSL_SHA1 * sha1, uint8 * out_data);
void rdssl_md5_init(RDSSL_MD5 * md5);
void rdssl_md5_update(RDSSL_MD5 * md5, uint8 * data, uint32 len);
void rdssl_md5_final(RDSSL_MD5 * md5, uint8 * out_data);
void rdssl_rc4_set_key(RDSSL_RC4 * rc4, uint8 * key, uint32 len);
void rdssl_rc4_crypt(RDSSL_RC4 * rc4, uint8 * in_data, uint8 * out_data, uint32 len);
void rdssl_rsa_encrypt(uint8 * out, uint8 * in, int len, uint32 modulus_size, uint8 * modulus,
		       uint8 * exponent);
RDSSL_CERT *rdssl_cert_read(uint8 * data, uint32 len);
void rdssl_cert_free(RDSSL_CERT * cert);
RDSSL_RKEY *rdssl_cert_to_rkey(RDSSL_CERT * cert, uint32 * key_len);
RD_BOOL rdssl_certs_ok(RDSSL_CERT * server_cert, RDSSL_CERT * cacert);
int rdssl_cert_print_fp(FILE * fp, RDSSL_CERT * cert);
void rdssl_rkey_free(RDSSL_RKEY * rkey);
int rdssl_rkey_get_exp_mod(RDSSL_RKEY * rkey, uint8 * exponent, uint32 max_exp_len, uint8 * modulus,
			   uint32 max_mod_len);
RD_BOOL rdssl_sig_ok(uint8 * exponent, uint32 exp_len, uint8 * modulus, uint32 mod_len,
		     uint8 * signature, uint32 sig_len);

void rdssl_hmac_md5(const void *key, int key_len,
		    const unsigned char *msg, int msg_len, unsigned char *md);
#endif
