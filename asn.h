/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   ASN.1 utility functions header
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
#ifndef _RDASN_H
#define _RDASN_H

#include <gnutls/gnutls.h>
#include <libtasn1.h>
#include <stdint.h>

#include "utils.h"

#ifdef  __cplusplus
extern "C" {
#endif


int init_asn1_lib(void);
int write_pkcs1_der_pubkey(const gnutls_datum_t *m, const gnutls_datum_t *e, uint8_t *out, int *out_len);
int libtasn_read_cert_pk_parameters(uint8_t *data, size_t len, gnutls_datum_t *m, gnutls_datum_t *e);

#ifdef __cplusplus
}
#endif

#endif /* _RDASN_H */
