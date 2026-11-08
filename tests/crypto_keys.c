/* Standalone regression checks for stream bounds and certificate public keys. */
#include <assert.h>
#include <stdarg.h>

#include "../tcp.c"
#include "../ssl.c"
#include "../stream.c"

/* Only the tested code is retained by --gc-sections. */
void *xmalloc(int size)
{
	void *p = malloc(size);
	assert(p != NULL);
	return p;
}

void *xrealloc(void *old, size_t size)
{
	void *p = realloc(old, size);
	assert(p != NULL);
	return p;
}

void logger(log_subject_t subject, log_level_t level, char *format, ...)
{
	va_list args;
	(void) subject;
	(void) level;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}

void _rdp_protocol_error(const char *file, int line, const char *func,
			const char *message, STREAM s)
{
	(void) file;
	(void) line;
	(void) func;
	(void) message;
	(void) s;
	abort();
}

static void test_stream_bounds(void)
{
	unsigned char data[8];
	struct stream s = { .data = data, .p = data, .end = data + 4, .size = sizeof(data) };
	assert(s_check_rem(&s, 0));
	assert(s_check_rem(&s, 4));
	assert(!s_check_rem(&s, 5));
	assert(!s_check_rem(&s, SIZE_MAX));
	s.p = s.end;
	assert(s_check_rem(&s, 0));
	assert(!s_check_rem(&s, 1));
	s.p++;
	assert(!s_check_rem(&s, 0));
	s_seek(&s, 0);
	assert(s.p == data);

	STREAM allocated = s_alloc(8);
	assert(allocated->p == allocated->data && allocated->end == allocated->data);
	assert(allocated->iso_hdr == NULL && allocated->channel_hdr == NULL);
	memcpy(allocated->data, "test", 4);
	allocated->p += 2;
	allocated->end += 4;
	allocated->iso_hdr = allocated->data + 1;
	allocated->channel_hdr = allocated->data + 3;
	s_realloc(allocated, 4096);
	assert(memcmp(allocated->data, "test", 4) == 0);
	assert(allocated->p == allocated->data + 2);
	assert(allocated->end == allocated->data + 4);
	assert(allocated->iso_hdr == allocated->data + 1);
	assert(allocated->channel_hdr == allocated->data + 3);
	assert(allocated->mcs_hdr == NULL && allocated->sec_hdr == NULL && allocated->rdp_hdr == NULL);
	s_realloc(allocated, 1);
	assert(allocated->size == 4096 && allocated->p == allocated->data + 2);
	s_free(allocated);
}

static void check_rsa_parameters(X509 *cert, EVP_PKEY *key)
{
	uint32 key_len = 0;
	RDSSL_RKEY *rsa = rdssl_cert_to_rkey(cert, &key_len);
	BIGNUM *n = NULL, *e = NULL;
	char *hex;
	mpz_t expected;
	assert(rsa != NULL);
	assert(key_len == 256);
	assert(EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_N, &n));
	assert(EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_E, &e));
	mpz_init(expected);
	hex = BN_bn2hex(n);
	assert(hex != NULL && mpz_set_str(expected, hex, 16) == 0);
	assert(mpz_cmp(expected, rsa->n) == 0);
	OPENSSL_free(hex);
	hex = BN_bn2hex(e);
	assert(hex != NULL && mpz_set_str(expected, hex, 16) == 0);
	assert(mpz_cmp(expected, rsa->e) == 0);
	OPENSSL_free(hex);
	mpz_clear(expected);
	BN_free(n);
	BN_free(e);
	rdssl_rkey_free(rsa);
}

static void check_tls_public_key(X509 *cert, EVP_PKEY *key)
{
	SSL_CTX *client_ctx = SSL_CTX_new(TLS_client_method());
	SSL_CTX *server_ctx = SSL_CTX_new(TLS_server_method());
	SSL *server;
	BIO *client_bio, *server_bio;
	STREAM pubkey;
	const unsigned char *der;
	STACK_OF(ASN1_TYPE) *sequence;
	const char *names[] = { OSSL_PKEY_PARAM_RSA_N, OSSL_PKEY_PARAM_RSA_E };
	assert(client_ctx != NULL && server_ctx != NULL);
	assert(SSL_CTX_use_certificate(server_ctx, cert) == 1);
	assert(SSL_CTX_use_PrivateKey(server_ctx, key) == 1);
	g_ssl = SSL_new(client_ctx);
	server = SSL_new(server_ctx);
	assert(g_ssl != NULL && server != NULL);
	assert(BIO_new_bio_pair(&client_bio, 0, &server_bio, 0) == 1);
	SSL_set_bio(g_ssl, client_bio, client_bio);
	SSL_set_bio(server, server_bio, server_bio);
	SSL_set_connect_state(g_ssl);
	SSL_set_accept_state(server);
	for (int i = 0; i < 100 &&
	     (!SSL_is_init_finished(g_ssl) || !SSL_is_init_finished(server)); i++)
	{
		SSL *peers[] = { g_ssl, server };
		for (int j = 0; j < 2; j++)
		{
			int result = SSL_do_handshake(peers[j]);
			int error = SSL_get_error(peers[j], result);
			assert(result == 1 || error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
		}
	}
	assert(SSL_is_init_finished(g_ssl) && SSL_is_init_finished(server));
	pubkey = tcp_tls_get_server_pubkey();
	assert(pubkey != NULL);
	der = pubkey->data;
	sequence = d2i_ASN1_SEQUENCE_ANY(NULL, &der, s_length(pubkey));
	assert(sequence != NULL && sk_ASN1_TYPE_num(sequence) == 2);
	assert(der == pubkey->end);
	for (int i = 0; i < 2; i++)
	{
		ASN1_TYPE *field = sk_ASN1_TYPE_value(sequence, i);
		BIGNUM *actual, *expected = NULL;
		assert(field->type == V_ASN1_INTEGER);
		actual = ASN1_INTEGER_to_BN(field->value.integer, NULL);
		assert(EVP_PKEY_get_bn_param(key, names[i], &expected));
		assert(actual != NULL && BN_cmp(actual, expected) == 0);
		BN_free(actual);
		BN_free(expected);
	}
	sk_ASN1_TYPE_pop_free(sequence, ASN1_TYPE_free);
	s_free(pubkey);
	SSL_free(server);
	SSL_free(g_ssl);
	g_ssl = NULL;
	SSL_CTX_free(client_ctx);
	SSL_CTX_free(server_ctx);
}

int main(int argc, char **argv)
{
	EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", (size_t) 2048);
	EVP_PKEY *ec = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "prime256v1");
	X509 *cert = X509_new();
	uint32 key_len = 123;
	(void) argc;
	(void) argv;
	assert(key != NULL && ec != NULL && cert != NULL);
	test_stream_bounds();
	assert(rdssl_cert_to_rkey(cert, &key_len) == NULL);
	assert(key_len == 123);
	assert(X509_set_pubkey(cert, ec));
	assert(rdssl_cert_to_rkey(cert, &key_len) == NULL);
	assert(key_len == 123);
	assert(X509_set_version(cert, 2));
	assert(ASN1_INTEGER_set(X509_get_serialNumber(cert), 1));
	assert(X509_gmtime_adj(X509_getm_notBefore(cert), 0) != NULL);
	assert(X509_gmtime_adj(X509_getm_notAfter(cert), 3600) != NULL);
	assert(X509_set_pubkey(cert, key));
	assert(X509_sign(cert, key, EVP_sha256()) > 0);
	check_rsa_parameters(cert, key);
	check_tls_public_key(cert, key);
	X509_free(cert);
	EVP_PKEY_free(ec);
	EVP_PKEY_free(key);
	puts("Stream bounds, RSA extraction, invalid keys, and TLS PKCS#1 encoding passed.");
	return 0;
}
