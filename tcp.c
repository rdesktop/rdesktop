/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop RDP_Protocol client.
   RDP_Protocol services - TCP layer
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright 2005-2011 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright 2012-2019 Henrik Andersson <hean01@cendio.se> for Cendio AB
   Copyright 2017-2018 Alexander Zakharov <uglym8@gmail.com>

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

#ifndef _WIN32
#include <unistd.h>		/* select read write close */
#include <sys/socket.h>		/* socket connect setsockopt */
#include <sys/time.h>		/* timeval */
#include <sys/stat.h>
#include <netdb.h>		/* gethostbyname */
#include <netinet/in.h>		/* sockaddr_in */
#include <netinet/tcp.h>	/* TCP_NODELAY */
#include <arpa/inet.h>		/* inet_addr */
#include <errno.h>		/* errno */
#include <assert.h>
#include <fcntl.h>		/* fcntl O_NONBLOCK */
#endif

#include "rdesktop.h"

#if defined(__APPLE__) && !defined(USE_OPENSSL)
/* Use macOS native Secure Transport instead of OpenSSL */
#include <Security/Security.h>
#include <Security/SecureTransport.h>
#else
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#endif

#ifdef _WIN32
#define socklen_t int
#define TCP_CLOSE(_sck) closesocket(_sck)
#define TCP_STRERROR "tcp error"
#define TCP_BLOCKS (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#define TCP_CLOSE(_sck) close(_sck)
#define TCP_STRERROR strerror(errno)
#define TCP_BLOCKS (errno == EWOULDBLOCK || errno == EAGAIN)
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned long) -1)
#endif

/* Windows' self signed certificates omit the required Digital
   Signature key usage flag, and only %COMPAT makes GnuTLS ignore
   that violation. */
#define GNUTLS_PRIORITY "NORMAL:%COMPAT"

#ifdef IPv6
static struct addrinfo *g_server_address = NULL;
#else
struct sockaddr_in *g_server_address = NULL;
#endif

static char *g_last_server_name = NULL;
static RD_BOOL g_ssl_initialized = False;
static int g_sock;
static RD_BOOL g_run_ui = False;
static struct stream g_in;
/* Set by the macOS Network.framework preflight when access is denied. */
#ifdef __APPLE__
RD_BOOL g_local_network_access_denied = False;
#endif
int g_tcp_port_rdp = TCP_PORT_RDP;

extern RD_BOOL g_exit_mainloop;
extern RD_BOOL g_network_error;
extern RD_BOOL g_reconnect_loop;
extern char g_tls_version[];

#if defined(__APPLE__) && !defined(USE_OPENSSL)
static SSLContextRef g_ssl_ctx = NULL;
#else
static SSL_CTX *g_ssl_ctx = NULL;
static SSL *g_ssl = NULL;
#endif

#if defined(__APPLE__) && !defined(USE_OPENSSL)
/* Secure Transport read callback */
static OSStatus
st_read_func(SSLConnectionRef connection, void *data, size_t *dataLength)
{
	int sock = *(int *)connection;
	ssize_t result;

	do {
		result = recv(sock, data, *dataLength, 0);
	} while (result == -1 && errno == EINTR);

	if (result > 0) {
		*dataLength = result;
		return noErr;
	} else if (result == 0) {
		*dataLength = 0;
		fprintf(stderr, "[macOS SSL] st_read_func: socket EOF (connection closed by peer)\n");
		fflush(stderr);
		return errSSLClosedGraceful;
	} else {
		*dataLength = 0;
		int err = errno;
		if (err == EAGAIN || err == EWOULDBLOCK) {
			return errSSLWouldBlock;
		}
		fprintf(stderr, "[macOS SSL] st_read_func: socket error %d: %s\n", err, strerror(err));
		fflush(stderr);
		return errSSLClosedAbort;
	}
}

/* Secure Transport write callback */
static OSStatus
st_write_func(SSLConnectionRef connection, const void *data, size_t *dataLength)
{
	int sock = *(int *)connection;
	ssize_t result;

	do {
		result = send(sock, data, *dataLength, 0);
	} while (result == -1 && errno == EINTR);

	if (result > 0) {
		*dataLength = result;
		return noErr;
	} else if (result == 0) {
		*dataLength = 0;
		fprintf(stderr, "[macOS SSL] st_write_func: socket EOF\n");
		fflush(stderr);
		return errSSLClosedGraceful;
	} else {
		*dataLength = 0;
		int err = errno;
		if (err == EAGAIN || err == EWOULDBLOCK) {
			return errSSLWouldBlock;
		}
		fprintf(stderr, "[macOS SSL] st_write_func: socket error %d: %s\n", err, strerror(err));
		fflush(stderr);
		return errSSLClosedAbort;
	}
}
#endif

/* wait till socket is ready to write or timeout */
static RD_BOOL
tcp_can_send(int sck, int millis)
{
	fd_set wfds;
	struct timeval time;
	int sel_count;

	time.tv_sec = millis / 1000;
	time.tv_usec = (millis * 1000) % 1000000;
	FD_ZERO(&wfds);
	FD_SET(sck, &wfds);
	sel_count = select(sck + 1, 0, &wfds, 0, &time);
	if (sel_count > 0)
	{
		return True;
	}
	return False;
}

/* Wait for data when the connection is being established off the UI thread. */
static RD_BOOL
tcp_can_recv(int sck, int millis)
{
	fd_set rfds;
	struct timeval time;
	int sel_count;

	time.tv_sec = millis / 1000;
	time.tv_usec = (millis * 1000) % 1000000;
	FD_ZERO(&rfds);
	FD_SET(sck, &rfds);
	sel_count = select(sck + 1, &rfds, 0, 0, &time);
	return (sel_count > 0 && FD_ISSET(sck, &rfds));
}

/* Initialise TCP transport data packet */
STREAM
tcp_init(uint32 maxlen)
{
	return s_alloc(maxlen);
}

/* Send TCP transport data packet */
void
tcp_send(STREAM s)
{
	size_t before;
	int length, sent;
	unsigned char *data;

	if (g_network_error == True)
		return;

#ifdef WITH_SCARD
	scard_lock(SCARD_LOCK_TCP);
#endif

	s_seek(s, 0);

	while (!s_check_end(s))
	{
		before = s_tell(s);
		length = s_remaining(s);
		in_uint8p(s, data, length);

		if (g_ssl_initialized) {
#if defined(__APPLE__) && !defined(USE_OPENSSL)
			size_t processed = 0;
			int flags = fcntl(g_sock, F_GETFL, 0);
			if (flags >= 0) {
				fcntl(g_sock, F_SETFL, flags & ~O_NONBLOCK);
			}
			OSStatus status = SSLWrite(g_ssl_ctx, data, length, &processed);
			if (flags >= 0) {
				fcntl(g_sock, F_SETFL, flags);
			}
			if (status == noErr || status == errSSLWouldBlock) {
				sent = processed;
				if (status == errSSLWouldBlock && sent == 0) {
					tcp_can_send(g_sock, 100);
				}
			} else {
#ifdef WITH_SCARD
				scard_unlock(SCARD_LOCK_TCP);
#endif
				logger(Core, Error, "tcp_send(), SSLWrite() failed with status %d\n", (int)status);
				g_network_error = True;
				return;
			}
#else
			sent = SSL_write(g_ssl, data, length);
			if (sent <= 0) {
				int ssl_error = SSL_get_error(g_ssl, sent);
				if (ssl_error != SSL_ERROR_WANT_WRITE && ssl_error != SSL_ERROR_WANT_READ) {
#ifdef WITH_SCARD
					scard_unlock(SCARD_LOCK_TCP);
#endif
					logger(Core, Error, "tcp_send(), SSL_write() failed with error %d: %s\n",
						   ssl_error, ERR_error_string(ERR_get_error(), NULL));
					g_network_error = True;
					return;
				} else {
					tcp_can_send(g_sock, 100);
					sent = 0;
				}
			}
#endif
		}
		else
		{
			sent = send(g_sock, data, length, 0);
			if (sent <= 0)
			{
				if (sent == -1 && TCP_BLOCKS)
				{
					tcp_can_send(g_sock, 100);
					sent = 0;
				}
				else
				{
#ifdef WITH_SCARD
					scard_unlock(SCARD_LOCK_TCP);
#endif
					logger(Core, Error, "tcp_send(), send() failed: %s",
					       TCP_STRERROR);
					g_network_error = True;
					return;
				}
			}
		}

		/* Everything might not have been sent */
		s_seek(s, before + sent);
	}
#ifdef WITH_SCARD
	scard_unlock(SCARD_LOCK_TCP);
#endif
}

/* Receive a message on the TCP layer */
STREAM
tcp_recv(STREAM s, uint32 length)
{
	size_t before;
	unsigned char *data;
	int rcvd = 0;

	if (g_network_error == True)
		return NULL;

	if (s == NULL)
	{
		/* read into "new" stream */
		s_realloc(&g_in, length);
		s_reset(&g_in);
		s = &g_in;
	}
	else
	{
		/* append to existing stream */
		s_realloc(s, s_length(s) + length);
	}

	while (length > 0)
	{

#if defined(__APPLE__) && !defined(USE_OPENSSL)
		/* Secure Transport doesn't have a pending check like OpenSSL */
		if (g_ssl_initialized && g_run_ui)
#else
		if ((!g_ssl_initialized || (SSL_pending(g_ssl) <= 0)) && g_run_ui)
#endif
		{
			ui_select(g_sock);

			/* break out of recv, if request of exiting
			   main loop has been done */
			if (g_exit_mainloop == True)
			{
				logger(Core, Debug, "tcp_recv(): exiting due to g_exit_mainloop=True");
				return NULL;
			}
		}
		else
		{
#if defined(__APPLE__) && !defined(USE_OPENSSL)
			if (g_ssl_initialized && !g_run_ui)
#else
			if (g_ssl_initialized && !g_run_ui && SSL_pending(g_ssl) <= 0)
#endif
				tcp_can_recv(g_sock, 1000);
		}

		before = s_tell(s);
		s_seek(s, s_length(s));

		out_uint8p(s, data, length);

		s_seek(s, before);

		if (g_ssl_initialized) {
#if defined(__APPLE__) && !defined(USE_OPENSSL)
			size_t processed = 0;
			OSStatus status = SSLRead(g_ssl_ctx, data, length, &processed);

			if (status == noErr || status == errSSLWouldBlock) {
				rcvd = processed;
				if (status == errSSLWouldBlock && rcvd == 0) {
					if (g_run_ui) {
						ui_select(g_sock);
					}
					else {
						tcp_can_recv(g_sock, 1000);
					}
					if (g_exit_mainloop == True) {
						logger(Core, Debug, "tcp_recv(): exiting due to g_exit_mainloop=True");
						return NULL;
					}
				}
			} else if (status == errSSLClosedGraceful) {
				logger(Core, Error, "tcp_recv(): SSL connection closed gracefully by peer");
				g_network_error = True;
				return NULL;
			} else {
				logger(Core, Error, "tcp_recv(): SSLRead() failed with status %d (g_ssl_ctx=%p, data=%p, length=%u)", 
					   (int)status, g_ssl_ctx, data, length);
				g_network_error = True;
				return NULL;
			}

			if (rcvd > 0) {
				logger(Core, Debug, "tcp_recv(): successfully read %d bytes via SSL", rcvd);
			}
#else
//			logger(Core, Debug, "tcp_recv(): calling SSL_read() for %d bytes...", length);
			rcvd = SSL_read(g_ssl, data, length);
//			logger(Core, Debug, "tcp_recv(): SSL_read() returned %d", rcvd);

			if (rcvd <= 0) {
				int ssl_error = SSL_get_error(g_ssl, rcvd);
				unsigned long err_code = ERR_get_error();

				if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
					/* Non-blocking socket would block - process UI events while waiting */
//					logger(Core, Debug, "tcp_recv(): SSL_ERROR_WANT_READ/WRITE - processing UI events...");
					if (g_run_ui) {
						ui_select(g_sock);
					}
					else {
						/* The macOS GUI performs connection setup on a worker thread. */
						tcp_can_recv(g_sock, 1000);
					}
					rcvd = 0;  /* Retry the read */

					/* Check if we should exit */
					if (g_exit_mainloop == True) {
						logger(Core, Debug, "tcp_recv(): exiting due to g_exit_mainloop=True");
						return NULL;
					}
				} else {
					/* Real error */
					logger(Core, Error, "========================================");
					logger(Core, Error, "tcp_recv(): SSL CONNECTION ERROR");
					logger(Core, Error, "  SSL_read() returned: %d", rcvd);
					logger(Core, Error, "  SSL_get_error(): %d", ssl_error);
					logger(Core, Error, "  ERR_get_error(): 0x%lx", err_code);
					logger(Core, Error, "  Error string: %s", ERR_error_string(err_code, NULL));
					logger(Core, Error, "  Socket fd: %d", g_sock);
					logger(Core, Error, "  Bytes requested: %d", length);
					logger(Core, Error, "========================================");

					if (ssl_error == SSL_ERROR_ZERO_RETURN) {
						logger(Core, Error, "tcp_recv(): SSL connection closed gracefully by peer");
					} else if (ssl_error == SSL_ERROR_SYSCALL) {
						if (err_code == 0) {
							logger(Core, Error, "tcp_recv(): SSL_ERROR_SYSCALL with EOF (unexpected connection termination)");
							logger(Core, Error, "tcp_recv(): This usually means the server crashed or forcibly closed the connection");
						} else {
							logger(Core, Error, "tcp_recv(): SSL_ERROR_SYSCALL: %s", strerror(errno));
						}
					}

					g_network_error = True;
					return NULL;
				}
			} else {
				logger(Core, Debug, "tcp_recv(): successfully read %d bytes via SSL", rcvd);
			}
#endif
		}
		else
		{
			logger(Core, Debug, "tcp_recv(): calling recv() for %d bytes (non-SSL)...", length);
			rcvd = recv(g_sock, data, length, 0);
			logger(Core, Debug, "tcp_recv(): recv() returned %d", rcvd);

			if (rcvd < 0)
			{
				if (rcvd == -1 && TCP_BLOCKS)
				{
					rcvd = 0;
				}
				else
				{
					logger(Core, Error, "tcp_recv(), recv() failed: %s",
							TCP_STRERROR);
					g_network_error = True;
					return NULL;
				}
			}
			else if (rcvd == 0)
			{
				logger(Core, Error, "tcp_recv(), connection closed by peer");
				return NULL;
			}
		}

		// FIXME: Should probably have a macro for this
		s->end += rcvd;
		length -= rcvd;
	}

	return s;
}

#if !defined(__APPLE__) || defined(USE_OPENSSL)
/*
 * Callback during handshake to verify peer certificate (OpenSSL only)
 */
static int
cert_verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
	int err, depth;

	err = X509_STORE_CTX_get_error(ctx);
	depth = X509_STORE_CTX_get_error_depth(ctx);

	logger(Core, Debug, "%s(), preverify_ok=%d, error=%d, depth=%d", __func__, preverify_ok, err, depth);

	/* Accept self-signed certificates and hostname mismatches for now */
	/* TODO: Implement proper certificate validation with user prompts */
	if (!preverify_ok) {
		logger(Core, Warning, "%s(), certificate verification failed: %s", __func__,
			   X509_verify_cert_error_string(err));

		/* Accept common RDP server certificate issues */
		if (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ||
			err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN ||
			err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY ||
			err == X509_V_ERR_CERT_UNTRUSTED ||
			err == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE)
		{
			logger(Core, Warning, "%s(), accepting self-signed certificate", __func__);
			return 1; /* Accept */
		}
	}

	return preverify_ok;
}

static void
openssl_fatal(const char *text)
{
	logger(Core, Error, "%s: %s", text, ERR_error_string(ERR_get_error(), NULL));
	/* TODO: Lookup if exit(1) is just plain wrong, its used here to breakout of
		fallback code path for connection, eg. if TLS fails, a retry with plain
		RDP is made.
	*/
	exit(1);
}
#endif

/* Establish a SSL/TLS 1.2 connection */
RD_BOOL
tcp_tls_connect(void)
{
#if defined(__APPLE__) && !defined(USE_OPENSSL)
	OSStatus status;
	SSLProtocol protocol;

	/* Create SSL context */
	g_ssl_ctx = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
	if (!g_ssl_ctx) {
		logger(Core, Error, "tcp_tls_connect(), could not create SSL context");
		return False;
	}

	/* Set IO callbacks */
	status = SSLSetIOFuncs(g_ssl_ctx, st_read_func, st_write_func);
	if (status != noErr) {
		logger(Core, Error, "tcp_tls_connect(), SSLSetIOFuncs failed: %d", (int)status);
		goto fail;
	}

	/* Set connection (socket pointer) */
	status = SSLSetConnection(g_ssl_ctx, &g_sock);
	if (status != noErr) {
		logger(Core, Error, "tcp_tls_connect(), SSLSetConnection failed: %d", (int)status);
		goto fail;
	}

	/* Set TLS protocol version based on g_tls_version */
	if (g_tls_version[0] == 0 || !strcmp(g_tls_version, "1.2"))
	{
		/* TLS 1.2 only */
		SSLSetProtocolVersionMin(g_ssl_ctx, kTLSProtocol12);
		SSLSetProtocolVersionMax(g_ssl_ctx, kTLSProtocol12);
		logger(Core, Debug, "tcp_tls_connect(), using TLS 1.2");
	}
	else if (!strcmp(g_tls_version, "1.1"))
	{
		/* TLS 1.1 only */
		SSLSetProtocolVersionMin(g_ssl_ctx, kTLSProtocol11);
		SSLSetProtocolVersionMax(g_ssl_ctx, kTLSProtocol11);
		logger(Core, Debug, "tcp_tls_connect(), using TLS 1.1");
	}
	else if (!strcmp(g_tls_version, "1.0"))
	{
		/* TLS 1.0 only */
		SSLSetProtocolVersionMin(g_ssl_ctx, kTLSProtocol1);
		SSLSetProtocolVersionMax(g_ssl_ctx, kTLSProtocol1);
		logger(Core, Debug, "tcp_tls_connect(), using TLS 1.0");
	}
	else
	{
		logger(Core, Error, "tcp_tls_connect(), TLS method should be 1.0, 1.1, or 1.2");
		goto fail;
	}

	/* Disable certificate verification for self-signed certs (common in RDP) */
	/* TODO: Implement proper certificate validation with user prompts */
	SSLSetSessionOption(g_ssl_ctx, kSSLSessionOptionBreakOnServerAuth, true);

	/* Perform TLS handshake */
	logger(Core, Debug, "tcp_tls_connect(), starting SSLHandshake()");
	do {
		status = SSLHandshake(g_ssl_ctx);

		/* Handle certificate verification */
		if (status == errSSLServerAuthCompleted) {
			/* Accept self-signed certs for now */
			logger(Core, Warning, "tcp_tls_connect(), accepting server certificate (verification disabled)");
			continue; /* Retry handshake */
		}

		if (status == errSSLWouldBlock) {
			/* Non-blocking socket needs more data */
			continue;
		}

	} while (status == errSSLWouldBlock || status == errSSLServerAuthCompleted);

	if (status != noErr) {
		logger(Core, Error, "%s(), TLS handshake failed with status %d",
			   __func__, (int)status);
		goto fail;
	}

	/* Get negotiated protocol version */
	status = SSLGetNegotiatedProtocolVersion(g_ssl_ctx, &protocol);
	if (status == noErr) {
		const char *version_str = "Unknown";
		switch (protocol) {
			case kTLSProtocol1:   version_str = "TLS 1.0"; break;
			case kTLSProtocol11:  version_str = "TLS 1.1"; break;
			case kTLSProtocol12:  version_str = "TLS 1.2"; break;
			case kTLSProtocol13:  version_str = "TLS 1.3"; break;
			default: break;
		}
		logger(Core, Verbose, "TLS Session info: %s (Secure Transport)\n", version_str);
		/* Do not overwrite g_tls_version option to avoid buffer overflow and retain retry settings */
	}

	logger(Core, Debug, "tcp_tls_connect(), connection established successfully");

	/* Set socket to non-blocking mode for UI responsiveness (after SSL handshake) */
	int flags = fcntl(g_sock, F_GETFL, 0);
	if (flags >= 0) {
		fcntl(g_sock, F_SETFL, flags | O_NONBLOCK);
		logger(Core, Debug, "tcp_tls_connect(): socket set to non-blocking mode");
	}

	g_ssl_initialized = True;
	return True;

fail:
	if (g_ssl_ctx) {
		CFRelease(g_ssl_ctx);
		g_ssl_ctx = NULL;
	}
	g_ssl_initialized = False;
	return False;

#else /* OpenSSL implementation */
	int err;
	const SSL_METHOD *method;

	/* Initialize OpenSSL library */
	if (!g_ssl_initialized)
	{
		SSL_library_init();
		SSL_load_error_strings();
		OpenSSL_add_all_algorithms();
		g_ssl_initialized = True;
	}

	/* Create SSL context */
	method = TLS_client_method();
	g_ssl_ctx = SSL_CTX_new(method);
	if (!g_ssl_ctx) {
		openssl_fatal("Could not create SSL context");
		return False;
	}

	/* Set minimum TLS version based on g_tls_version */
	if (g_tls_version[0] == 0 || !strcmp(g_tls_version, "1.2"))
	{
		SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_2_VERSION);
		SSL_CTX_set_max_proto_version(g_ssl_ctx, TLS1_2_VERSION);
		logger(Core, Debug, "tcp_tls_connect(), using TLS 1.2");
	}
	else if (!strcmp(g_tls_version, "1.1"))
	{
		SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_1_VERSION);
		SSL_CTX_set_max_proto_version(g_ssl_ctx, TLS1_1_VERSION);
		logger(Core, Debug, "tcp_tls_connect(), using TLS 1.1");
	}
	else if (!strcmp(g_tls_version, "1.0"))
	{
		SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_VERSION);
		SSL_CTX_set_max_proto_version(g_ssl_ctx, TLS1_VERSION);
		logger(Core, Debug, "tcp_tls_connect(), using TLS 1.0");
	}
	else
	{
		logger(Core, Error, "tcp_tls_connect(), TLS method should be 1.0, 1.1, or 1.2");
		goto fail;
	}

	/* Load system trust store */
	if (!SSL_CTX_set_default_verify_paths(g_ssl_ctx)) {
		logger(Core, Warning, "%s(), Could not load system trust database: %s",
			   __func__, ERR_error_string(ERR_get_error(), NULL));
	}

	/* Set certificate verification callback */
	SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_PEER, cert_verify_callback);

	/* Create SSL connection */
	g_ssl = SSL_new(g_ssl_ctx);
	if (!g_ssl) {
		openssl_fatal("Could not create SSL connection");
		goto fail;
	}

	/* Attach socket to SSL */
	if (!SSL_set_fd(g_ssl, g_sock)) {
		openssl_fatal("Could not attach socket to SSL");
		goto fail;
	}

	/* Perform TLS handshake */
	logger(Core, Debug, "tcp_tls_connect(), starting SSL_connect()");
	err = SSL_connect(g_ssl);
	if (err != 1) {
		int ssl_error = SSL_get_error(g_ssl, err);
		logger(Core, Error, "%s(), TLS handshake failed. SSL_connect returned %d, error: %d (%s)",
			   __func__, err, ssl_error, ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	/* Log TLS session info */
	{
		const char *version = SSL_get_version(g_ssl);
		const char *cipher = SSL_get_cipher(g_ssl);
		logger(Core, Verbose, "TLS  Session info: %s-%s (stub)\n", version, cipher);
		/* Do not overwrite g_tls_version option to avoid buffer overflow and retain retry settings */
	}

	logger(Core, Debug, "tcp_tls_connect(), connection established successfully");

	/* Set socket to non-blocking mode for UI responsiveness (after SSL handshake) */
	int flags = fcntl(g_sock, F_GETFL, 0);
	if (flags >= 0) {
		fcntl(g_sock, F_SETFL, flags | O_NONBLOCK);
		logger(Core, Debug, "tcp_tls_connect(): socket set to non-blocking mode");
	}

	return True;

fail:
	if (g_ssl) {
		SSL_free(g_ssl);
		g_ssl = NULL;
	}
	if (g_ssl_ctx) {
		SSL_CTX_free(g_ssl_ctx);
		g_ssl_ctx = NULL;
	}
	g_ssl_initialized = False;
	return False;
#endif
}

/* Get public key from server of TLS 1.x connection */
STREAM
tcp_tls_get_server_pubkey()
{
#if defined(__APPLE__) && !defined(USE_OPENSSL)
	SecTrustRef trust = NULL;
	SecCertificateRef cert = NULL;
	SecKeyRef pubkey = NULL;
	CFDataRef pk_data_cf = NULL;
	unsigned char *pk_data = NULL;
	CFIndex pk_size;
	STREAM s = NULL;
	OSStatus status;

	/* Get peer trust from SSL session */
	status = SSLCopyPeerTrust(g_ssl_ctx, &trust);
	if (status != noErr || !trust) {
		logger(Core, Error, "%s:%s:%d Failed to get peer trust (status %d)\n",
				__FILE__, __func__, __LINE__, (int)status);
		return NULL;
	}

	/* Get certificate from trust */
	if (SecTrustGetCertificateCount(trust) > 0) {
		cert = SecTrustGetCertificateAtIndex(trust, 0);
		if (cert) {
			CFRetain(cert); /* Retain since we'll use it after releasing trust */
		}
	}

	CFRelease(trust);

	if (!cert) {
		logger(Core, Error, "%s:%s:%d Failed to get peer certificate\n",
				__FILE__, __func__, __LINE__);
		return NULL;
	}

	/* Extract public key from certificate */
	pubkey = SecCertificateCopyKey(cert);
	if (!pubkey) {
		logger(Core, Error, "%s:%s:%d Failed to get public key from certificate\n",
				__FILE__, __func__, __LINE__);
		CFRelease(cert);
		return NULL;
	}

	/* Export public key in X.509 SubjectPublicKeyInfo DER format */
	CFErrorRef error = NULL;
	pk_data_cf = SecKeyCopyExternalRepresentation(pubkey, &error);
	if (!pk_data_cf) {
		if (error) {
			CFStringRef errorDesc = CFErrorCopyDescription(error);
			char errorStr[256];
			CFStringGetCString(errorDesc, errorStr, sizeof(errorStr), kCFStringEncodingUTF8);
			logger(Core, Error, "%s:%s:%d Failed to export public key: %s\n",
					__FILE__, __func__, __LINE__, errorStr);
			CFRelease(errorDesc);
			CFRelease(error);
		}
		CFRelease(pubkey);
		CFRelease(cert);
		return NULL;
	}

	/* Get raw key data */
	pk_size = CFDataGetLength(pk_data_cf);
	pk_data = (unsigned char *)CFDataGetBytePtr(pk_data_cf);

	/* Create stream and copy public key data */
	s = s_alloc(pk_size);
	out_uint8a(s, pk_data, pk_size);
	s_mark_end(s);
	s_seek(s, 0);

	CFRelease(pk_data_cf);
	CFRelease(pubkey);
	CFRelease(cert);

	return s;

#else /* OpenSSL implementation */
	X509 *cert = NULL;
	EVP_PKEY *pkey = NULL;
	unsigned char *pk_data = NULL;
	int pk_size;
	STREAM s = NULL;

	/* Get peer certificate from SSL connection */
	cert = SSL_get_peer_certificate(g_ssl);
	if (!cert) {
		logger(Core, Error, "%s:%s:%d Failed to get peer certificate\n",
				__FILE__, __func__, __LINE__);
		goto out;
	}

	/* Extract public key from certificate */
	pkey = X509_get_pubkey(cert);
	if (!pkey) {
		logger(Core, Error, "%s:%s:%d Failed to get public key from certificate. OpenSSL error: %s\n",
				__FILE__, __func__, __LINE__, ERR_error_string(ERR_get_error(), NULL));
		goto out;
	}

	/* Preserve the RSA PKCS#1 encoding required by CredSSP. */
	if (!EVP_PKEY_is_a(pkey, "RSA")) {
		logger(Core, Error, "%s:%s:%d Certificate public key is not RSA. OpenSSL error: %s\n",
				__FILE__, __func__, __LINE__, ERR_error_string(ERR_get_error(), NULL));
		goto out;
	}

	/*
	 * Encode RSA public key to PKCS#1 DER format
	 * EVP's type-specific encoder produces PKCS#1 DER for RSA keys
	 */
	pk_size = i2d_PublicKey(pkey, NULL);
	if (pk_size <= 0) {
		logger(Core, Error, "%s:%s:%d Failed to determine RSA public key size. OpenSSL error: %s\n",
				__FILE__, __func__, __LINE__, ERR_error_string(ERR_get_error(), NULL));
		goto out;
	}

	pk_data = (unsigned char *)malloc(pk_size);
	if (!pk_data) {
		logger(Core, Error, "%s:%s:%d Failed to allocate memory for public key\n",
				__FILE__, __func__, __LINE__);
		goto out;
	}

	unsigned char *pk_ptr = pk_data;
	if (i2d_PublicKey(pkey, &pk_ptr) != pk_size) {
		logger(Core, Error, "%s:%s:%d Failed to encode RSA public key to PKCS#1 DER. OpenSSL error: %s\n",
				__FILE__, __func__, __LINE__, ERR_error_string(ERR_get_error(), NULL));
		goto out;
	}

	/* Create stream and copy public key data */
	s = s_alloc(pk_size);
	out_uint8a(s, pk_data, pk_size);
	s_mark_end(s);
	s_seek(s, 0);

out:
	if (pk_data)
		free(pk_data);
	if (pkey)
		EVP_PKEY_free(pkey);
	if (cert)
		X509_free(cert);

	return s;
#endif
}

/* Helper function to determine if rdesktop should resolve hostnames again or not */
static RD_BOOL
tcp_connect_resolve_hostname(const char *server)
{
	return (g_server_address == NULL ||
		g_last_server_name == NULL || strcmp(g_last_server_name, server) != 0);
}

/* Establish a connection on the TCP layer

   This function tries to avoid resolving any server address twice. The
   official Windows 2008 documentation states that the windows farm name
   should be a round-robin DNS entry containing all the terminal servers
   in the farm. When connected to the farm address, if we look up the
   address again when reconnecting (for any reason) we risk reconnecting
   to a different server in the farm.
*/

RD_BOOL
tcp_connect(char *server)
{
	socklen_t option_len;
	uint32 option_value;
	char buf[NI_MAXHOST];
#ifdef IPv6

	int n;
	struct addrinfo hints, *res, *addr;
	struct sockaddr *oldaddr;
	char tcp_port_rdp_s[10];

	if (tcp_connect_resolve_hostname(server))
	{
		snprintf(tcp_port_rdp_s, 10, "%d", g_tcp_port_rdp);

		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		if ((n = getaddrinfo(server, tcp_port_rdp_s, &hints, &res)))
		{
			logger(Core, Error, "tcp_connect(), getaddrinfo() failed: %s",
			       gai_strerror(n));
			return False;
		}
	}
	else
	{
		res = g_server_address;
	}

	g_sock = -1;

	for (addr = res; addr != NULL; addr = addr->ai_next)
	{
		g_sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (g_sock < 0)
		{
			logger(Core, Debug, "tcp_connect(), socket() failed: %s", TCP_STRERROR);
			continue;
		}

		n = getnameinfo(addr->ai_addr, addr->ai_addrlen, buf, sizeof(buf), NULL, 0,
				NI_NUMERICHOST);
		if (n != 0)
		{
			logger(Core, Error, "tcp_connect(), getnameinfo() failed: %s",
			       gai_strerror(n));
			return False;
		}

		logger(Core, Debug, "tcp_connect(), trying %s (%s)", server, buf);

		if (connect(g_sock, addr->ai_addr, addr->ai_addrlen) == 0)
			break;

		TCP_CLOSE(g_sock);
		g_sock = -1;
	}

	if (g_sock == -1)
	{
		logger(Core, Error, "tcp_connect(), unable to connect to %s", server);
		return False;
	}

	/* Save server address for later use, if we haven't already. */

	if (g_server_address == NULL)
	{
		g_server_address = xmalloc(sizeof(struct addrinfo));
		g_server_address->ai_addr = xmalloc(sizeof(struct sockaddr_storage));
	}

	if (g_server_address != addr)
	{
		/* don't overwrite ptr to allocated sockaddr */
		oldaddr = g_server_address->ai_addr;
		memcpy(g_server_address, addr, sizeof(struct addrinfo));
		g_server_address->ai_addr = oldaddr;

		memcpy(g_server_address->ai_addr, addr->ai_addr, addr->ai_addrlen);

		g_server_address->ai_canonname = NULL;
		g_server_address->ai_next = NULL;

		freeaddrinfo(res);
	}

#else /* no IPv6 support */
	struct hostent *nslookup = NULL;

	if (tcp_connect_resolve_hostname(server))
	{
		if (g_server_address != NULL)
			xfree(g_server_address);
		g_server_address = xmalloc(sizeof(struct sockaddr_in));
		g_server_address->sin_family = AF_INET;
		g_server_address->sin_port = htons((uint16) g_tcp_port_rdp);

		if ((nslookup = gethostbyname(server)) != NULL)
		{
			memcpy(&g_server_address->sin_addr, nslookup->h_addr,
			       sizeof(g_server_address->sin_addr));
		}
		else if ((g_server_address->sin_addr.s_addr = inet_addr(server)) == INADDR_NONE)
		{
			logger(Core, Error, "tcp_connect(), unable to resolve host '%s'", server);
			return False;
		}
	}

	if ((g_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		logger(Core, Error, "tcp_connect(), socket() failed: %s", TCP_STRERROR);
		return False;
	}

	logger(Core, Debug, "tcp_connect(), trying %s (%s)",
	       server, inet_ntop(g_server_address->sin_family,
				 &g_server_address->sin_addr, buf, sizeof(buf)));

	if (connect(g_sock, (struct sockaddr *) g_server_address, sizeof(struct sockaddr)) < 0)
	{
		if (!g_reconnect_loop)
			logger(Core, Error, "tcp_connect(), connect() failed: %s", TCP_STRERROR);

		TCP_CLOSE(g_sock);
		g_sock = -1;
		return False;
	}

#endif /* IPv6 */

	option_value = 1;
	option_len = sizeof(option_value);
	setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, (void *) &option_value, option_len);
	/* receive buffer must be a least 16 K */
	if (getsockopt(g_sock, SOL_SOCKET, SO_RCVBUF, (void *) &option_value, &option_len) == 0)
	{
		if (option_value < (1024 * 16))
		{
			option_value = 1024 * 16;
			option_len = sizeof(option_value);
			setsockopt(g_sock, SOL_SOCKET, SO_RCVBUF, (void *) &option_value,
				   option_len);
		}
	}

	g_in.size = 4096;
	g_in.data = (uint8 *) xmalloc(g_in.size);

	/* After successful connect: update the last server name */
	if (g_last_server_name)
		xfree(g_last_server_name);
	g_last_server_name = strdup(server);
	return True;
}

/* Disconnect on the TCP layer */
void
tcp_disconnect(void)
{
	if (g_ssl_initialized) {
#if defined(__APPLE__) && !defined(USE_OPENSSL)
		if (g_ssl_ctx) {
			SSLClose(g_ssl_ctx);
			CFRelease(g_ssl_ctx);
			g_ssl_ctx = NULL;
		}
#else
		if (g_ssl) {
			SSL_shutdown(g_ssl);
			SSL_free(g_ssl);
			g_ssl = NULL;
		}
		if (g_ssl_ctx) {
			SSL_CTX_free(g_ssl_ctx);
			g_ssl_ctx = NULL;
		}
#endif
		g_ssl_initialized = False;
	}

	TCP_CLOSE(g_sock);
	g_sock = -1;

	g_in.size = 0;
	xfree(g_in.data);
	g_in.data = NULL;
}

char *
tcp_get_address()
{
	static char ipaddr[32];
	struct sockaddr_in sockaddr;
	socklen_t len = sizeof(sockaddr);
	if (getsockname(g_sock, (struct sockaddr *) &sockaddr, &len) == 0)
	{
		uint8 *ip = (uint8 *) & sockaddr.sin_addr;
		sprintf(ipaddr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	}
	else
		strcpy(ipaddr, "127.0.0.1");
	return ipaddr;
}

RD_BOOL
tcp_is_connected()
{
	struct sockaddr_in sockaddr;
	socklen_t len = sizeof(sockaddr);
	if (getpeername(g_sock, (struct sockaddr *) &sockaddr, &len))
		return True;
	return False;
}

/* reset the state of the tcp layer */
/* Support for Session Directory */
void
tcp_reset_state(void)
{
	/* Clear the incoming stream */
	s_reset(&g_in);
}

void
tcp_run_ui(RD_BOOL run)
{
	g_run_ui = run;
}
