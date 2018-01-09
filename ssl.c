/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Secure sockets abstraction layer
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright (C) Jay Sorg <j@american-data.com> 2006-2008
   Copyright 2016-2017 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

/* Helper function to log internal SSL errors using logger */
void
rdssl_log_ssl_errors(const char *prefix)
{
	unsigned long err;
	while (1)
	{
		err = ERR_get_error();
		if (err == 0)
			break;

		logger(Protocol, Error,
		       "%s, 0x%.8x:%s:%s: %s",
		       prefix, err, ERR_lib_error_string(err),
		       ERR_func_error_string(err), ERR_reason_error_string(err));
	}
}

void
rdssl_sha1_init(RDSSL_SHA1 * sha1)
{
	SHA1_Init(sha1);
}

void
rdssl_sha1_update(RDSSL_SHA1 * sha1, uint8 * data, uint32 len)
{
	SHA1_Update(sha1, data, len);
}

void
rdssl_sha1_final(RDSSL_SHA1 * sha1, uint8 * out_data)
{
	SHA1_Final(out_data, sha1);
}

void
rdssl_md5_init(RDSSL_MD5 * md5)
{
	MD5_Init(md5);
}

void
rdssl_md5_update(RDSSL_MD5 * md5, uint8 * data, uint32 len)
{
	MD5_Update(md5, data, len);
}

void
rdssl_md5_final(RDSSL_MD5 * md5, uint8 * out_data)
{
	MD5_Final(out_data, md5);
}

void
rdssl_rc4_set_key(RDSSL_RC4 * rc4, uint8 * key, uint32 len)
{
	RC4_set_key(rc4, len, key);
}

void
rdssl_rc4_crypt(RDSSL_RC4 * rc4, uint8 * in_data, uint8 * out_data, uint32 len)
{
	RC4(rc4, len, in_data, out_data);
}

static void
reverse(uint8 * p, int len)
{
	int i, j;
	uint8 temp;

	for (i = 0, j = len - 1; i < j; i++, j--)
	{
		temp = p[i];
		p[i] = p[j];
		p[j] = temp;
	}
}

void
rdssl_rsa_encrypt(uint8 * out, uint8 * in, int len, uint32 modulus_size, uint8 * modulus,
		  uint8 * exponent)
{
	BN_CTX *ctx;
	BIGNUM *mod, *exp, *x, *y;
	uint8 inr[SEC_MAX_MODULUS_SIZE];
	int outlen;

	reverse(modulus, modulus_size);
	reverse(exponent, SEC_EXPONENT_SIZE);
	memcpy(inr, in, len);
	reverse(inr, len);

	ctx = BN_CTX_new();
	mod = BN_new();
	exp = BN_new();
	x = BN_new();
	y = BN_new();

	BN_bin2bn(modulus, modulus_size, mod);
	BN_bin2bn(exponent, SEC_EXPONENT_SIZE, exp);
	BN_bin2bn(inr, len, x);
	BN_mod_exp(y, x, exp, mod, ctx);
	outlen = BN_bn2bin(y, out);
	reverse(out, outlen);
	if (outlen < (int) modulus_size)
		memset(out + outlen, 0, modulus_size - outlen);

	BN_free(y);
	BN_clear_free(x);
	BN_free(exp);
	BN_free(mod);
	BN_CTX_free(ctx);
}

/* returns newly allocated RDSSL_CERT or NULL */
RDSSL_CERT *
rdssl_cert_read(uint8 * data, uint32 len)
{
	/* this will move the data pointer but we don't care, we don't use it again */
	return d2i_X509(NULL, (D2I_X509_CONST unsigned char **) &data, len);
}

void
rdssl_cert_free(RDSSL_CERT * cert)
{
	X509_free(cert);
}

/* returns newly allocated RDSSL_RKEY or NULL */
RDSSL_RKEY *
rdssl_cert_to_rkey(RDSSL_CERT * cert, uint32 * key_len)
{
	EVP_PKEY *epk = NULL;
	RDSSL_RKEY *lkey;
	int nid;
	int ret;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	const unsigned char *p;
	RSA *rsa = NULL;
	int pklen;
#endif

	/* By some reason, Microsoft sets the OID of the Public RSA key to
	   the oid for "MD5 with RSA Encryption" instead of "RSA Encryption"

	   Kudos to Richard Levitte for the following (. intuitive .) 
	   lines of code that resets the OID and let's us extract the key. */

	X509_PUBKEY *key = NULL;
	X509_ALGOR *algor = NULL;

	key = X509_get_X509_PUBKEY(cert);
	if (key == NULL)
	{
		logger(Protocol, Error,
		       "rdssl_cert_to_key(), failed to get public key from certificate");
		rdssl_log_ssl_errors("rdssl_cert_to_key()");

		return NULL;
	}

	ret = X509_PUBKEY_get0_param(NULL, NULL, 0, &algor, key);
	if (ret != 1)
	{
		logger(Protocol, Error,
		       "rdssl_cert_to_key(), failed to get algorithm used for public key");
		rdssl_log_ssl_errors("rdssl_cert_to_key()");

		return NULL;
	}

	nid = OBJ_obj2nid(algor->algorithm);

	if ((nid == NID_md5WithRSAEncryption) || (nid == NID_shaWithRSAEncryption))
	{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
		logger(Protocol, Debug,
		       "rdssl_cert_to_key(), re-setting algorithm type to RSA in server certificate");
		X509_PUBKEY_set0_param(key, OBJ_nid2obj(NID_rsaEncryption), 0, NULL, NULL, 0);
#else

		if (!X509_PUBKEY_get0_param(NULL, &p, &pklen, NULL, key))
		{
			logger(Protocol, Error,
			       "rdssl_cert_to_key(), failed to get algorithm used for public key");
			rdssl_log_ssl_errors("rdssl_cert_to_key()");

			return NULL;
		}

		if (!(rsa = d2i_RSAPublicKey(NULL, &p, pklen)))
		{
			logger(Protocol, Error,
			       "rdssl_cert_to_key(), failed to extract public key from certificate");
			rdssl_log_ssl_errors("rdssl_cert_to_key()");

			return NULL;
		}

		lkey = RSAPublicKey_dup(rsa);
		*key_len = RSA_size(lkey);
		return lkey;
#endif

	}

	epk = X509_get_pubkey(cert);
	if (NULL == epk)
	{
		logger(Protocol, Error,
		       "rdssl_cert_to_key(), failed to extract public key from certificate");
		rdssl_log_ssl_errors("rdssl_cert_to_key()");

		return NULL;
	}

	lkey = RSAPublicKey_dup(EVP_PKEY_get1_RSA(epk));
	EVP_PKEY_free(epk);
	*key_len = RSA_size(lkey);
	return lkey;
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
	return X509_print_fp(fp, cert);
}

void
rdssl_rkey_free(RDSSL_RKEY * rkey)
{
	RSA_free(rkey);
}

/* returns error */
int
rdssl_rkey_get_exp_mod(RDSSL_RKEY * rkey, uint8 * exponent, uint32 max_exp_len, uint8 * modulus,
		       uint32 max_mod_len)
{
	int len;

	BIGNUM *e = NULL;
	BIGNUM *n = NULL;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	e = rkey->e;
	n = rkey->n;
#else
	RSA_get0_key(rkey, &n, &e, NULL);
#endif

	if ((BN_num_bytes(e) > (int) max_exp_len) || (BN_num_bytes(n) > (int) max_mod_len))
	{
		return 1;
	}
	len = BN_bn2bin(e, exponent);
	reverse(exponent, len);
	len = BN_bn2bin(n, modulus);
	reverse(modulus, len);
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
	HMAC(EVP_md5(), key, key_len, msg, msg_len, md, NULL);
}

#if defined(OPENSSL_VERSION_NUMBER) && (OPENSSL_VERSION_NUMBER < 0x1000000fL)
int X509_ALGOR_set0(X509_ALGOR *alg, ASN1_OBJECT *aobj, int ptype, void *pval)
{
    if (alg == NULL)
        return 0;

    if (ptype != V_ASN1_UNDEF) {
        if (alg->parameter == NULL)
            alg->parameter = ASN1_TYPE_new();
        if (alg->parameter == NULL)
            return 0;
    }

    ASN1_OBJECT_free(alg->algorithm);
    alg->algorithm = aobj;

    if (ptype == 0)
        return 1;
    if (ptype == V_ASN1_UNDEF) {
        ASN1_TYPE_free(alg->parameter);
        alg->parameter = NULL;
    } else
        ASN1_TYPE_set(alg->parameter, ptype, pval);
    return 1;
}

int X509_PUBKEY_set0_param(X509_PUBKEY *pub, ASN1_OBJECT *aobj,
                           int ptype, void *pval,
                           unsigned char *penc, int penclen)
{
    if (!X509_ALGOR_set0(pub->algor, aobj, ptype, pval))
        return 0;
    if (penc) {
        OPENSSL_free(pub->public_key->data);
        pub->public_key->data = penc;
        pub->public_key->length = penclen;
        /* Set number of unused bits to zero */
        pub->public_key->flags &= ~(ASN1_STRING_FLAG_BITS_LEFT | 0x07);
        pub->public_key->flags |= ASN1_STRING_FLAG_BITS_LEFT;
    }
    return 1;
}

int X509_PUBKEY_get0_param(ASN1_OBJECT **ppkalg,
                           const unsigned char **pk, int *ppklen,
                           X509_ALGOR **pa, X509_PUBKEY *pub)
{
    if (ppkalg)
        *ppkalg = pub->algor->algorithm;
    if (pk) {
        *pk = pub->public_key->data;
        *ppklen = pub->public_key->length;
    }
    if (pa)
        *pa = pub->algor;
    return 1;
}
#endif
