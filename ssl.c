/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop RDP_Protocol client.
   Secure sockets abstraction layer
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright (C) Jay Sorg <j@american-data.com> 2006-2008
   Copyright 2016-2017 Henrik Andersson <hean01@cendio.se> for Cendio AB
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

#include "rdesktop.h"
#include "ssl.h"
#include "asn.h"

#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>

void
rdssl_sha1_init(RDSSL_SHA1 * sha1)
{
	sha1_init(sha1);
}

void
rdssl_sha1_update(RDSSL_SHA1 * sha1, uint8 * data, uint32 len)
{
	sha1_update(sha1, len, data);
}

void
rdssl_sha1_final(RDSSL_SHA1 * sha1, uint8 * out_data)
{
	sha1_digest(sha1, SHA1_DIGEST_SIZE, out_data);
}

void
rdssl_md5_init(RDSSL_MD5 * md5)
{
	md5_init(md5);
}

void
rdssl_md5_update(RDSSL_MD5 * md5, uint8 * data, uint32 len)
{
	md5_update(md5, len, data);
}

void
rdssl_md5_final(RDSSL_MD5 * md5, uint8 * out_data)
{
	md5_digest(md5, MD5_DIGEST_SIZE, out_data);
}

void
rdssl_rc4_set_key(RDSSL_RC4 * rc4, uint8 * key, uint32 len)
{
	arcfour_set_key(rc4, len, key);
}

void
rdssl_rc4_crypt(RDSSL_RC4 * rc4, uint8 * in_data, uint8 * out_data, uint32 len)
{
	arcfour_crypt(rc4, len, out_data, in_data);
}

void
rdssl_rsa_encrypt(uint8 * out, uint8 * in, int len, uint32 modulus_size, uint8 * modulus,
		  uint8 * exponent)
{
	mpz_t exp, mod;

	mpz_t y;
	mpz_t x;

	size_t outlen;

	mpz_init(y);
	mpz_init(x);
	mpz_init(exp);
	mpz_init(mod);

	mpz_import(mod, modulus_size, -1, sizeof(modulus[0]), 0, 0, modulus);
	mpz_import(exp, SEC_EXPONENT_SIZE, -1, sizeof(exponent[0]), 0, 0, exponent);

	mpz_import(x, len, -1, sizeof(in[0]), 0, 0, in);

	mpz_powm(y, x, exp, mod);

	mpz_export(out, &outlen, -1, sizeof(out[0]), 0, 0, y);

	mpz_clear(y);
	mpz_clear(x);
	mpz_clear(exp);
	mpz_clear(mod);

	if (outlen < modulus_size)
		memset(out + outlen, 0, modulus_size - outlen);
}

/* returns newly allocated RDSSL_CERT or NULL */
RDSSL_CERT *
rdssl_cert_read(uint8 * data, uint32 len)
{
	const unsigned char *p = data;
	X509 *cert = d2i_X509(NULL, &p, len);

	if (!cert) {
		logger(RDP_Protocol, Error, "%s:%s:%d: Failed to parse DER encoded certificate. OpenSSL error = %s\n",
				__FILE__, __func__, __LINE__, ERR_error_string(ERR_get_error(), NULL));
		return NULL;
	}

	return cert;
}

void
rdssl_cert_free(RDSSL_CERT * cert)
{
	X509_free(cert);
}


/*
 * AFAIK, there's no way to alter the decoded certificate using GnuTLS.
 *
 * Upon detecting "problem" (wrong public RSA key OID) certificate
 * we basically have two options:
 *
 * 1)) encode certificate back to DER, then parse it using libtasn1,
 * fix public key OID (set it to 1.2.840.113549.1.1.1), encode to DER again
 * and finally reparse using GnuTLS
 *
 * 2) encode cert back to DER, get RSA public key parameters using libtasn1
 *
 * Or can rewrite the whole certificate related stuff later.
 */

/* returns newly allocated RDSSL_RKEY or NULL */
RDSSL_RKEY *
rdssl_cert_to_rkey(RDSSL_CERT * cert, uint32 * key_len)
{
	EVP_PKEY *pkey = NULL;
	BIGNUM *n = NULL, *e = NULL;
	RDSSL_RKEY *rkey = NULL;
	uint8_t *n_buf = NULL, *e_buf = NULL;
	size_t n_len, e_len;

	pkey = X509_get_pubkey(cert);
	if (!pkey || !EVP_PKEY_is_a(pkey, "RSA") ||
	    !EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N, &n) ||
	    !EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_E, &e))
	{
		logger(RDP_Protocol, Error, "rdssl_cert_to_rkey(), failed to get RSA modulus/exponent: %s",
		       ERR_error_string(ERR_get_error(), NULL));
		goto out;
	}

	n_len = BN_num_bytes(n);
	e_len = BN_num_bytes(e);
	if (n_len == 0 || e_len == 0)
		goto out;

	n_buf = malloc(n_len);
	e_buf = malloc(e_len);
	rkey = malloc(sizeof(*rkey));
	if (!n_buf || !e_buf || !rkey)
	{
		logger(RDP_Protocol, Error, "rdssl_cert_to_rkey(), failed to allocate RSA public key");
		free(rkey);
		rkey = NULL;
		goto out;
	}

	rsa_public_key_init(rkey);
	BN_bn2bin(n, n_buf);
	mpz_import(rkey->n, n_len, 1, 1, 0, 0, n_buf);
	BN_bn2bin(e, e_buf);
	mpz_import(rkey->e, e_len, 1, 1, 0, 0, e_buf);

	if (!rsa_public_key_prepare(rkey))
	{
		rsa_public_key_clear(rkey);
		free(rkey);
		rkey = NULL;
		goto out;
	}
	*key_len = rkey->size;

out:
	free(n_buf);
	free(e_buf);
	BN_free(n);
	BN_free(e);
	EVP_PKEY_free(pkey);
	return rkey;
}

/* returns boolean */
RD_BOOL
rdssl_certs_ok(RDSSL_CERT * server_cert, RDSSL_CERT * cacert)
{
	UNUSED(server_cert);
	UNUSED(cacert);
	/* Currently, we don't use the CA Certificate.
	   FIXME:
	   *) Verify the server certificate (server_cert) with the
	   CA certificate.
	   *) Store the CA Certificate with the hostname of the
	   server we are connecting to as key, and compare it
	   when we connect the next time, in order to prevent
	   MITM-attacks.
	 */
	return True;
}

int
rdssl_cert_print_fp(FILE * fp, RDSSL_CERT * cert)
{
	X509_NAME *subject;
	char *subj_str;

	subject = X509_get_subject_name(cert);
	if (!subject) {
		logger(RDP_Protocol, Error, "%s:%s:%d: Failed to get certificate subject\n",
				__FILE__, __func__, __LINE__);
		return 1;
	}

	subj_str = X509_NAME_oneline(subject, NULL, 0);
	if (!subj_str) {
		logger(RDP_Protocol, Error, "%s:%s:%d: Failed to convert subject to string\n",
				__FILE__, __func__, __LINE__);
		return 1;
	}

	fprintf(fp, "\t%s\n", subj_str);
	OPENSSL_free(subj_str);

	return 0;
}

void
rdssl_rkey_free(RDSSL_RKEY * rkey)
{
	rsa_public_key_clear(rkey);
	free(rkey);
}

/* Actually we can get rid of this function and use rsa_public)_key in rdssl_rsa_encrypt */
/* returns error */
int
rdssl_rkey_get_exp_mod(RDSSL_RKEY * rkey, uint8 * exponent, uint32 max_exp_len, uint8 * modulus,
		       uint32 max_mod_len)
{
	size_t outlen;

	outlen = (mpz_sizeinbase(rkey->n, 2) + 7) / 8;
	if (outlen > max_mod_len)
		return 1;
	outlen = (mpz_sizeinbase(rkey->e, 2) + 7) / 8;
	if (outlen > max_exp_len)
		return 1;

	mpz_export(modulus, &outlen, -1, sizeof(uint8), 0, 0, rkey->n);
	mpz_export(exponent, &outlen, -1, sizeof(uint8), 0, 0, rkey->e);

	/*
	 * Note that gnutls_x509_crt_get_pk_rsa_raw() exports modulus with additional
	 * zero byte as signed bignum. We can easily import this value using mpz_import()
	 * After we use mpz_export() on pkey.n (modulus) it will (according to GMP docs)
	 * export data without sign byte.
	 *
	 * This is only important if you get modulus from certificate using GnuTLS,
	 * save it somewhere, import it into mpz  and then export it from the said mpz to some
	 * buffer. If you then compare initiail (saved) modulus with newly exported one they
	 * will be different.
	 *
	 * On the other hand if we use mpz_t all the way, there will be no such situation.
	 */

	return 0;
}

/* returns boolean */
RD_BOOL
rdssl_sig_ok(uint8 * exponent, uint32 exp_len, uint8 * modulus, uint32 mod_len,
	     uint8 * signature, uint32 sig_len)
{
	UNUSED(exponent);
	UNUSED(exp_len);
	UNUSED(modulus);
	UNUSED(mod_len);
	UNUSED(signature);
	UNUSED(sig_len);
	/* Currently, we don't check the signature
	   FIXME:
	 */
	return True;
}


void
rdssl_hmac_md5(const void *key, int key_len, const unsigned char *msg, int msg_len,
	       unsigned char *md)
{
	struct hmac_md5_ctx ctx;

	hmac_md5_set_key(&ctx, key_len, key);
	hmac_md5_update(&ctx, msg_len, msg);
	hmac_md5_digest(&ctx, MD5_DIGEST_SIZE, md);
}
