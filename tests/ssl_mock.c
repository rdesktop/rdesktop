#include <cgreen/mocks.h>
#include "../rdesktop.h"
#include "../ssl.h"

void rdssl_hmac_md5(const void *key, int key_len,
                    const unsigned char *msg, int msg_len, unsigned char *md)
{
  mock(key, key_len, msg, msg_len, md);
}

void
rdssl_cert_free(RDSSL_CERT * cert)
{
  mock(cert);
}

RDSSL_CERT *
rdssl_cert_read(uint8 *data, uint32 len)
{
  return (RDSSL_CERT *) mock(data, len);
}

RD_BOOL
rdssl_certs_ok(RDSSL_CERT * server_cert, RDSSL_CERT * cacert)
{
  return mock(server_cert, cacert);
}

RDSSL_RKEY *
rdssl_cert_to_rkey(RDSSL_CERT * cert, uint32 * key_len)
{
  return (RDSSL_RKEY *) mock(cert, key_len);
}

void
rdssl_md5_init(RDSSL_MD5 * md5)
{
  mock(md5);
}

void
rdssl_md5_update(RDSSL_MD5 * md5, uint8 * data, uint32 len)
{
  mock(md5, data, len);
}

void
rdssl_md5_final(RDSSL_MD5 * md5, uint8 * out_data)
{
  mock(md5, out_data);
}

void
rdssl_rc4_set_key(RDSSL_RC4 * rc4, uint8 * key, uint32 len)
{
  mock(rc4, key, len);
}

void
rdssl_rc4_crypt(RDSSL_RC4 * rc4, uint8 * in_data, uint8 * out_data, uint32 len)
{
  mock(rc4, in_data, out_data, len);
}

void
rdssl_rkey_free(RDSSL_RKEY * rkey)
{
  mock(rkey);
}

int
rdssl_rkey_get_exp_mod(RDSSL_RKEY * rkey, uint8 * exponent, uint32 max_exp_len, uint8 * modulus,
		       uint32 max_mod_len)
{
  return mock(rkey, exponent, max_exp_len, modulus, max_mod_len);
}

void
rdssl_rsa_encrypt(uint8 * out, uint8 * in, int len, uint32 modulus_size, uint8 * modulus,
		  uint8 * exponent)
{
  mock(out, in, len, modulus_size, modulus, exponent);
}

void
rdssl_sha1_final(RDSSL_SHA1 * sha1, uint8 * out_data)
{
  mock(sha1, out_data);
}

void
rdssl_sha1_init(RDSSL_SHA1 *sha1)
{
  mock(sha1);
}

void
rdssl_sha1_update(RDSSL_SHA1 *sha1, uint8 *data, uint32 len)
{
  mock(sha1, data, len);
}

RD_BOOL
rdssl_sig_ok(uint8 *exponent, uint32 exp_len, uint8 *modulus, uint32 mod_len,
	     uint8 *signature, uint32 sig_len)
{
  return mock(exponent, exp_len, modulus, mod_len, signature, sig_len);
}
