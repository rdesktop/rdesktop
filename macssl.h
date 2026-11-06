/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Secure sockets abstraction layer - macOS native
   Native macOS port using Security framework
*/

#ifndef _MACSSL_H
#define _MACSSL_H

#ifndef __APPLE__
#include <stdio.h>
#include <time.h>
#include <dirent.h>
#include "types.h"
#include "ssl.h"
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <libtasn1.h>
#else

#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dirent.h>  // Add for DIR type
#include "types.h"

/* SSL types for compatibility with rdesktop */
typedef struct {
    uint8_t data[64];  // Sufficient for SHA1 context
} SSL_SHA1;

typedef struct {
    uint8_t data[64];  // Sufficient for MD5 context  
} SSL_MD5;

typedef struct {
    uint8_t data[256]; // Sufficient for RC4 context
} SSL_RC4;

typedef struct {
    void *cert_info;   // SecCertificateRef
} SSL_CERT;

typedef struct {
    void *key_info;    // RSA key data
    uint32_t key_len;
} SSL_RKEY;

/* Compatibility aliases for old RDSSL_ names */
#define RDSSL_RC4 SSL_RC4
#define RDSSL_SHA1 SSL_SHA1
#define RDSSL_MD5 SSL_MD5
#define RDSSL_CERT SSL_CERT
#define RDSSL_RKEY SSL_RKEY

/* GNUTLS compatibility types and defines */
typedef struct {
    uint8_t *data;
    size_t size;
} gnutls_datum_t;

typedef void* gnutls_session_t;
typedef void* gnutls_x509_crt_t;
typedef void* gnutls_certificate_credentials_t;

#define GNUTLS_E_SUCCESS 0
#define GNUTLS_CRT_X509 1
#define GNUTLS_X509_FMT_DER 1
#define GNUTLS_CLIENT 1
#define GNUTLS_CRD_CERTIFICATE 1
#define GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT 120000
#define GNUTLS_E_CERTIFICATE_ERROR -50
#define GNUTLS_PK_RSA 1
#define GNUTLS_SHUT_WR 1

/* GNUTLS compatibility function stubs */
int gnutls_record_send(gnutls_session_t session, const void *data, size_t data_size);
int gnutls_record_recv(gnutls_session_t session, void *data, size_t data_size);  
int gnutls_record_check_pending(gnutls_session_t session);
int gnutls_error_is_fatal(int error);
const char* gnutls_strerror(int error);
int gnutls_certificate_verify_peers2(gnutls_session_t session, unsigned int *status);
int gnutls_certificate_type_get(gnutls_session_t session);
const gnutls_datum_t* gnutls_certificate_get_peers(gnutls_session_t session, unsigned int *list_size);
int gnutls_x509_crt_init(gnutls_x509_crt_t *cert);
int gnutls_x509_crt_import(gnutls_x509_crt_t cert, const gnutls_datum_t *data, int format);
int gnutls_x509_crt_check_hostname(gnutls_x509_crt_t cert, const char *hostname);
void gnutls_global_init(void);
int gnutls_init(gnutls_session_t *session, unsigned int flags);
int gnutls_priority_set_direct(gnutls_session_t session, const char *priorities, const char **err_pos);
int gnutls_certificate_allocate_credentials(gnutls_certificate_credentials_t *res);
int gnutls_credentials_set(gnutls_session_t session, int type, void *cred);
int gnutls_certificate_set_x509_system_trust(gnutls_certificate_credentials_t cred);
void gnutls_certificate_set_verify_function(gnutls_certificate_credentials_t cred, int (*func)(gnutls_session_t));
void gnutls_transport_set_int(gnutls_session_t session, int fd);
void gnutls_handshake_set_timeout(gnutls_session_t session, unsigned int ms);
int gnutls_handshake(gnutls_session_t session);
char* gnutls_session_get_desc(gnutls_session_t session);
void gnutls_free(void *ptr);
void gnutls_deinit(gnutls_session_t session);
void gnutls_global_deinit(void);
int gnutls_x509_crt_get_pk_algorithm(gnutls_x509_crt_t cert, unsigned int *bits);
int gnutls_x509_crt_get_pk_rsa_raw(gnutls_x509_crt_t crt, gnutls_datum_t *m, gnutls_datum_t *e);
int gnutls_bye(gnutls_session_t session, int how);

/* Additional GNUTLS types and functions for utils.c */
typedef void* gnutls_x509_dn_t;
typedef long time_t;

typedef struct {
    gnutls_datum_t oid;
    gnutls_datum_t value;
} gnutls_x509_ava_st;

#define GNUTLS_DIG_SHA1 1
#define GNUTLS_DIG_SHA256 2
#define GNUTLS_X509_FMT_DER 1
#define GNUTLS_SAN_DNSNAME 1
#define GNUTLS_SAN_RFC822NAME 2
#define GNUTLS_SAN_URI 3
#define GNUTLS_SAN_IPADDRESS 4

/* Certificate status flags */
#define GNUTLS_CERT_REVOKED (1 << 5)
#define GNUTLS_CERT_SIGNER_NOT_FOUND (1 << 6)
#define GNUTLS_CERT_SIGNER_NOT_CA (1 << 7)
#define GNUTLS_CERT_INSECURE_ALGORITHM (1 << 8)
#define GNUTLS_CERT_NOT_ACTIVATED (1 << 9)
#define GNUTLS_CERT_EXPIRED (1 << 10)
#define GNUTLS_CERT_SIGNATURE_FAILURE (1 << 11)
#define GNUTLS_CERT_REVOCATION_DATA_SUPERSEDED (1 << 12)
#define GNUTLS_CERT_UNEXPECTED_OWNER (1 << 13)
#define GNUTLS_CERT_REVOCATION_DATA_ISSUED_IN_FUTURE (1 << 14)
#define GNUTLS_CERT_SIGNER_CONSTRAINTS_FAILURE (1 << 15)
#define GNUTLS_CERT_MISMATCH (1 << 16)

/* GNUTLS error codes */
#define GNUTLS_E_CERTIFICATE_KEY_MISMATCH -50
#define GNUTLS_E_NO_CERTIFICATE_FOUND -51

int gnutls_x509_crt_get_subject(gnutls_x509_crt_t cert, gnutls_x509_dn_t *dn);
int gnutls_x509_crt_get_issuer(gnutls_x509_crt_t cert, gnutls_x509_dn_t *dn);
long gnutls_x509_crt_get_activation_time(gnutls_x509_crt_t cert);
long gnutls_x509_crt_get_expiration_time(gnutls_x509_crt_t cert);
int gnutls_x509_dn_get_rdn_ava(gnutls_x509_dn_t dn, int irdn, int iava, gnutls_x509_ava_st *ava);
int gnutls_x509_crt_get_fingerprint(gnutls_x509_crt_t cert, int algo, void *buf, size_t *buf_size);
int gnutls_x509_crt_get_subject_alt_name2(gnutls_x509_crt_t cert, unsigned int seq, void *san, size_t *san_size, unsigned int *san_type, unsigned int *critical);
int gnutls_verify_stored_pubkey(const char *db_name, const char *tdb_name, const char *host, const char *service, int cert_type, const gnutls_datum_t *cert, unsigned int flags);
int gnutls_store_pubkey(const char *db_name, const char *tdb_name, const char *host, const char *service, int cert_type, const gnutls_datum_t *cert, time_t expiration, unsigned int flags);

/* ASN.1 compatibility types and functions */
typedef struct {
    const char *name;
    unsigned int type;
    const char *value;
} asn1_static_node;

typedef void* asn1_node;
#define ASN1_SUCCESS 0
#define ASN1_MEM_ERROR -1

int asn1_array2tree(const asn1_static_node *array, asn1_node *definitions, char *errorDescription);
const char* asn1_strerror(int error);
int asn1_create_element(asn1_node definitions, const char *source_name, asn1_node *element);
int asn1_write_value(asn1_node node_root, const char *name, const void *ivalue, int len);
int asn1_der_coding(asn1_node element, const char *name, void *ider, int *len, char *ErrorDescription);
int asn1_der_decoding(asn1_node *element, const void *ider, int ider_len, char *errorDescription);
int asn1_read_value(asn1_node node_root, const char *name, void *ivalue, int *len);

/* Function declarations */
void rdssl_sha1_init(SSL_SHA1 * sha1);
void rdssl_sha1_update(SSL_SHA1 * sha1, uint8 * data, uint32 len);
void rdssl_sha1_final(SSL_SHA1 * sha1, uint8 * out);

void rdssl_md5_init(SSL_MD5 * md5);
void rdssl_md5_update(SSL_MD5 * md5, uint8 * data, uint32 len);
void rdssl_md5_final(SSL_MD5 * md5, uint8 * out);

void rdssl_rc4_set_key(SSL_RC4 * rc4, uint8 * key, uint32 len);
void rdssl_rc4_crypt(SSL_RC4 * rc4, uint8 * in, uint8 * out, uint32 len);

RD_BOOL rdssl_rsa_encrypt(uint8 * out, uint8 * in, int len, uint32 modulus_size, uint8 * modulus, uint8 * exponent);

SSL_CERT * rdssl_cert_read(uint8 * data, uint32 len);
void rdssl_cert_free(SSL_CERT * cert);
SSL_RKEY * rdssl_cert_to_rkey(SSL_CERT * cert, uint32 * key_len);
void rdssl_rkey_free(SSL_RKEY * rkey);

int rdssl_rkey_get_exp_mod(SSL_RKEY * rkey, uint8 * exponent, uint32 max_exp_len, uint8 * modulus, uint32 max_mod_len);
RD_BOOL rdssl_cert_verify(SSL_CERT * server_cert, SSL_CERT * cacert);
int rdssl_rkey_get_dword(SSL_RKEY * rkey, const char * name, uint8 * buf);
void rdssl_hmac_md5(const void *key, int key_len, const unsigned char *data, int data_len, unsigned char *result);
void rdssl_sig_sha1(SSL_CERT * cert, int len, uint8 * data, uint8 * hash);
RD_BOOL rdssl_sig_ok(uint8 * exponent, uint32 exp_len, uint8 * modulus, uint32 mod_len,
                   uint8 * signature, uint32 sig_len);
RD_BOOL rdssl_certs_ok(SSL_CERT * server_cert, SSL_CERT * cacert);

void rdssl_lib_init(void);
void rdssl_lib_cleanup(void);

#endif /* __APPLE__ */

#endif /* _MACSSL_H */