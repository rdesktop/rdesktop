/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Secure sockets abstraction layer
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright (C) Jay Sorg <j@american-data.com> 2006-2008

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

void
ssl_sha1_init(SSL_SHA1 * sha1)
{
	SHA1_Init(sha1);
}

void
ssl_sha1_update(SSL_SHA1 * sha1, uint8 * data, uint32 len)
{
	SHA1_Update(sha1, data, len);
}

void
ssl_sha1_final(SSL_SHA1 * sha1, uint8 * out_data)
{
	SHA1_Final(out_data, sha1);
}

void
ssl_md5_init(SSL_MD5 * md5)
{
	MD5_Init(md5);
}

void
ssl_md5_update(SSL_MD5 * md5, uint8 * data, uint32 len)
{
	MD5_Update(md5, data, len);
}

void
ssl_md5_final(SSL_MD5 * md5, uint8 * out_data)
{
	MD5_Final(out_data, md5);
}

void
ssl_rc4_set_key(SSL_RC4 * rc4, uint8 * key, uint32 len)
{
	RC4_set_key(rc4, len, key);
}

void
ssl_rc4_crypt(SSL_RC4 * rc4, uint8 * in_data, uint8 * out_data, uint32 len)
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
ssl_rsa_encrypt(uint8 * out, uint8 * in, int len, uint32 modulus_size, uint8 * modulus,
		uint8 * exponent)
{
	BN_CTX *ctx;
	BIGNUM mod, exp, x, y;
	uint8 inr[SEC_MAX_MODULUS_SIZE];
	int outlen;

	reverse(modulus, modulus_size);
	reverse(exponent, SEC_EXPONENT_SIZE);
	memcpy(inr, in, len);
	reverse(inr, len);

	ctx = BN_CTX_new();
	BN_init(&mod);
	BN_init(&exp);
	BN_init(&x);
	BN_init(&y);

	BN_bin2bn(modulus, modulus_size, &mod);
	BN_bin2bn(exponent, SEC_EXPONENT_SIZE, &exp);
	BN_bin2bn(inr, len, &x);
	BN_mod_exp(&y, &x, &exp, &mod, ctx);
	outlen = BN_bn2bin(&y, out);
	reverse(out, outlen);
	if (outlen < (int) modulus_size)
		memset(out + outlen, 0, modulus_size - outlen);

	BN_free(&y);
	BN_clear_free(&x);
	BN_free(&exp);
	BN_free(&mod);
	BN_CTX_free(ctx);
}

/* returns newly allocated SSL_CERT or NULL */
SSL_CERT *
ssl_cert_read(uint8 * data, uint32 len)
{
	/* this will move the data pointer but we don't care, we don't use it again */
	return d2i_X509(NULL, (D2I_X509_CONST unsigned char **) &data, len);
}

void
ssl_cert_free(SSL_CERT * cert)
{
	X509_free(cert);
}

/* returns newly allocated SSL_RKEY or NULL */
SSL_RKEY *
ssl_cert_to_rkey(SSL_CERT * cert, uint32 * key_len)
{
	EVP_PKEY *epk = NULL;
	SSL_RKEY *lkey;
	int nid;

	/* By some reason, Microsoft sets the OID of the Public RSA key to
	   the oid for "MD5 with RSA Encryption" instead of "RSA Encryption"

	   Kudos to Richard Levitte for the following (. intiutive .) 
	   lines of code that resets the OID and let's us extract the key. */
	nid = OBJ_obj2nid(cert->cert_info->key->algor->algorithm);
	if ((nid == NID_md5WithRSAEncryption) || (nid == NID_shaWithRSAEncryption))
	{
		DEBUG_RDP5(("Re-setting algorithm type to RSA in server certificate\n"));
		ASN1_OBJECT_free(cert->cert_info->key->algor->algorithm);
		cert->cert_info->key->algor->algorithm = OBJ_nid2obj(NID_rsaEncryption);
	}
	epk = X509_get_pubkey(cert);
	if (NULL == epk)
	{
		error("Failed to extract public key from certificate\n");
		return NULL;
	}

	lkey = RSAPublicKey_dup(EVP_PKEY_get1_RSA(epk));
	EVP_PKEY_free(epk);
	*key_len = RSA_size(lkey);
	return lkey;
}

/* returns boolean */
RD_BOOL
ssl_certs_ok(SSL_CERT * server_cert, SSL_CERT * cacert)
{
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
ssl_cert_print_fp(FILE * fp, SSL_CERT * cert)
{
	return X509_print_fp(fp, cert);
}

void
ssl_rkey_free(SSL_RKEY * rkey)
{
	RSA_free(rkey);
}

/* returns error */
int
ssl_rkey_get_exp_mod(SSL_RKEY * rkey, uint8 * exponent, uint32 max_exp_len, uint8 * modulus,
		     uint32 max_mod_len)
{
	int len;

	if ((BN_num_bytes(rkey->e) > (int) max_exp_len) ||
	    (BN_num_bytes(rkey->n) > (int) max_mod_len))
	{
		return 1;
	}
	len = BN_bn2bin(rkey->e, exponent);
	reverse(exponent, len);
	len = BN_bn2bin(rkey->n, modulus);
	reverse(modulus, len);
	return 0;
}

/* returns boolean */
RD_BOOL
ssl_sig_ok(uint8 * exponent, uint32 exp_len, uint8 * modulus, uint32 mod_len,
	   uint8 * signature, uint32 sig_len)
{
	/* Currently, we don't check the signature
	   FIXME:
	 */
	return True;
}


void
ssl_hmac_md5(const void *key, int key_len, const unsigned char *msg, int msg_len, unsigned char *md)
{
	HMAC_CTX ctx;
	HMAC_CTX_init(&ctx);
	HMAC(EVP_md5(), key, key_len, msg, msg_len, md, NULL);
	HMAC_CTX_cleanup(&ctx);
}
