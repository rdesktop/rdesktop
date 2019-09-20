/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - TCP layer
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
#endif

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "rdesktop.h"
#include "ssl.h"
#include "asn.h"

#ifdef _WIN32
#define socklen_t int
#define TCP_CLOSE(_sck) closesocket(_sck)
#define TCP_STRERROR "tcp error"
#define TCP_BLOCKS (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#define TCP_CLOSE(_sck) close(_sck)
#define TCP_STRERROR strerror(errno)
#define TCP_BLOCKS (errno == EWOULDBLOCK)
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
int g_tcp_port_rdp = TCP_PORT_RDP;

extern RD_BOOL g_exit_mainloop;
extern RD_BOOL g_network_error;
extern RD_BOOL g_reconnect_loop;
extern char g_tls_version[];

static gnutls_session_t g_tls_session;

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
			sent = gnutls_record_send(g_tls_session, data, length);
			if (sent <= 0) {
				if (gnutls_error_is_fatal(sent)) {
#ifdef WITH_SCARD
					scard_unlock(SCARD_LOCK_TCP);
#endif
					logger(Core, Error, "tcp_send(), gnutls_record_send() failed with %d: %s\n", sent, gnutls_strerror(sent));
					g_network_error = True;
					return;
				} else {
					tcp_can_send(g_sock, 100);
					sent = 0;
				}
			}
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

		if ((!g_ssl_initialized || (gnutls_record_check_pending(g_tls_session) <= 0)) && g_run_ui)
		{
			ui_select(g_sock);

			/* break out of recv, if request of exiting
			   main loop has been done */
			if (g_exit_mainloop == True)
				return NULL;
		}

		before = s_tell(s);
		s_seek(s, s_length(s));

		out_uint8p(s, data, length);

		s_seek(s, before);

		if (g_ssl_initialized) {
			rcvd = gnutls_record_recv(g_tls_session, data, length);

			if (rcvd < 0) {
				if (gnutls_error_is_fatal(rcvd)) {
					logger(Core, Error, "tcp_recv(), gnutls_record_recv() failed with %d: %s\n", rcvd, gnutls_strerror(rcvd));
					g_network_error = True;
					return NULL;
				} else {
					rcvd = 0;
				}
			}

		}
		else
		{
			rcvd = recv(g_sock, data, length, 0);
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
				logger(Core, Error, "rcp_recv(), connection closed by peer");
				return NULL;
			}
		}

		// FIXME: Should probably have a macro for this
		s->end += rcvd;
		length -= rcvd;
	}

	return s;
}

/*
 * Callback during handshake to verify peer certificate
 */
static int
cert_verify_callback(gnutls_session_t session)
{
	int rv;
	int type;
	RD_BOOL hostname_mismatch = False;
	unsigned int status;
	gnutls_x509_crt_t cert;
	const gnutls_datum_t *cert_list;
	unsigned int cert_list_size;

	/*
	* verify certificate against system trust store
	*/
	rv = gnutls_certificate_verify_peers2(session, &status);
	if (rv == GNUTLS_E_SUCCESS)
	{
		logger(Core, Debug, "%s(), certificate verify status flags: %x", __func__, status);

		if (status == 0)
		{
			/* get list of certificates */
			cert_list = NULL;
			cert_list_size = 0;

			type = gnutls_certificate_type_get(session);
			if (type == GNUTLS_CRT_X509) {
				cert_list = gnutls_certificate_get_peers(session, &cert_list_size);
			}

			if (cert_list_size > 0)
			{
				/* validate hostname */
				gnutls_x509_crt_init(&cert);
				gnutls_x509_crt_import(cert, &cert_list[0], GNUTLS_X509_FMT_DER);
				if (gnutls_x509_crt_check_hostname(cert, g_last_server_name) != 0)
				{
					logger(Core, Debug, "%s(), certificate is valid", __func__);
					return 0;
				}
				else
				{
					logger(Core, Warning, "%s(), certificate hostname mismatch", __func__);
					hostname_mismatch = True;
				}
			}
			else
			{
				logger(Core, Error, "%s(), failed to get certificate list for peers", __func__);
				return 1;
			}
		}
	}

	/*
	 *  Use local store as fallback
	 */
	return utils_cert_handle_exception(session, status, hostname_mismatch, g_last_server_name);
}

static void
gnutls_fatal(const char *text, int status)
{
	logger(Core, Error, "%s: %s", text, gnutls_strerror(status));
	/* TODO: Lookup if exit(1) is just plain wrong, its used here to breakout of
		fallback code path for connection, eg. if TLS fails, a retry with plain
		RDP is made.
	*/
	exit(1);
}

/* Establish a SSL/TLS 1.0 connection */
RD_BOOL
tcp_tls_connect(void)
{
	int err;
	const char* priority;

	gnutls_certificate_credentials_t xcred;

	/* Initialize TLS session */
	if (!g_ssl_initialized)
	{
		gnutls_global_init();
		err = gnutls_init(&g_tls_session, GNUTLS_CLIENT);
		if (err < 0) {
			gnutls_fatal("Could not initialize GnuTLS", err);
		}
		g_ssl_initialized = True;
	}

	/* FIXME: It is recommended to use the default priorities, but
	          appending things requires GnuTLS 3.6.3 */

	priority = NULL;
	if (g_tls_version[0] == 0)
		priority = GNUTLS_PRIORITY;
	else if (!strcmp(g_tls_version, "1.0"))
		priority = GNUTLS_PRIORITY ":-VERS-ALL:+VERS-TLS1.0";
	else if (!strcmp(g_tls_version, "1.1"))
		priority = GNUTLS_PRIORITY ":-VERS-ALL:+VERS-TLS1.1";
	else if (!strcmp(g_tls_version, "1.2"))
		priority = GNUTLS_PRIORITY ":-VERS-ALL:+VERS-TLS1.2";

	if (priority == NULL)
	{
		logger(Core, Error,
		       "tcp_tls_connect(), TLS method should be 1.0, 1.1, or 1.2");
		goto fail;
	}

	err = gnutls_priority_set_direct(g_tls_session, priority, NULL);
	if (err < 0) {
		gnutls_fatal("Could not set GnuTLS priority setting", err);
	}

	err = gnutls_certificate_allocate_credentials(&xcred);
	if (err < 0) {
		gnutls_fatal("Could not allocate TLS certificate structure", err);
	}
	err = gnutls_credentials_set(g_tls_session, GNUTLS_CRD_CERTIFICATE, xcred);
	if (err < 0) {
		gnutls_fatal("Could not set TLS certificate structure", err);
	}
	err = gnutls_certificate_set_x509_system_trust(xcred);
	if (err < 0) {
		logger(Core, Error, "%s(), Could not load system trust database: %s",
			   __func__, gnutls_strerror(err));
	}
	gnutls_certificate_set_verify_function(xcred, cert_verify_callback);
	gnutls_transport_set_int(g_tls_session, g_sock);
	gnutls_handshake_set_timeout(g_tls_session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);

	/* Perform the TLS handshake */
	do {
		err = gnutls_handshake(g_tls_session);
	} while (err < 0 && gnutls_error_is_fatal(err) == 0);


	if (err < 0) {

		if (err == GNUTLS_E_CERTIFICATE_ERROR)
		{
			gnutls_fatal("Certificate error during TLS handshake", err);
		}

		/* Handshake failed with unknown error, lets log */
		logger(Core, Error, "%s(), TLS handshake failed. GnuTLS error: %s",
			   __func__, gnutls_strerror(err));

		goto fail;

	} else {
		char *desc;
		desc = gnutls_session_get_desc(g_tls_session);
		logger(Core, Verbose, "TLS  Session info: %s\n", desc);
		gnutls_free(desc);
	}

	return True;

fail:

	if (g_ssl_initialized) {
		gnutls_deinit(g_tls_session);
		// Not needed since 3.3.0
		gnutls_global_deinit();

		g_ssl_initialized = False;
	}

	return False;
}

/* Get public key from server of TLS 1.x connection */
STREAM
tcp_tls_get_server_pubkey()
{
	int ret;
	unsigned int list_size;
	const gnutls_datum_t *cert_list;
	gnutls_x509_crt_t cert;

	unsigned int algo, bits;
	gnutls_datum_t m, e;

	int pk_size;
	uint8_t pk_data[1024];

	STREAM s = NULL;

	cert_list = gnutls_certificate_get_peers(g_tls_session, &list_size);

	if (!cert_list) {
		logger(Core, Error, "%s:%s:%d Failed to get peer's certs' list\n", __FILE__, __func__, __LINE__);
		goto out;
	}

	if ((ret = gnutls_x509_crt_init(&cert)) != GNUTLS_E_SUCCESS) {
		logger(Core, Error, "%s:%s:%d Failed to init certificate structure. GnuTLS error: %s\n",
				__FILE__, __func__, __LINE__, gnutls_strerror(ret));
		goto out;
	}

	if ((ret = gnutls_x509_crt_import(cert, &cert_list[0], GNUTLS_X509_FMT_DER)) != GNUTLS_E_SUCCESS) {
		logger(Core, Error, "%s:%s:%d Failed to import DER certificate. GnuTLS error:%s\n",
				__FILE__, __func__, __LINE__, gnutls_strerror(ret));
		goto out;
	}

	algo = gnutls_x509_crt_get_pk_algorithm(cert, &bits);

	if (algo == GNUTLS_PK_RSA) {
		if ((ret = gnutls_x509_crt_get_pk_rsa_raw(cert, &m, &e)) !=  GNUTLS_E_SUCCESS) {
			logger(Core, Error, "%s:%s:%d Failed to get RSA public key parameters from certificate. GnuTLS error:%s\n",
					__FILE__, __func__, __LINE__, gnutls_strerror(ret));
			goto out;
		}
	} else {
			logger(Core, Error, "%s:%s:%d Peer's certificate public key algorithm is not RSA. GnuTLS error:%s\n",
					__FILE__, __func__, __LINE__, gnutls_strerror(algo));
			goto out;
	}

	pk_size = sizeof(pk_data);

	/*
	 * This key will be used further in cssp_connect() for server's key comparison.
	 *
	 * Note that we need to encode this RSA public key into PKCS#1 DER
	 * ATM there's no way to encode/export RSA public key to PKCS#1 using GnuTLS,
	 * gnutls_pubkey_export() encodes into PKCS#8. So besides fixing GnuTLS
	 * we can use libtasn1 for encoding.
	 */

	if ((ret = write_pkcs1_der_pubkey(&m, &e, pk_data, &pk_size)) != 0) {
			logger(Core, Error, "%s:%s:%d Failed to encode RSA public key to PKCS#1 DER\n",
					__FILE__, __func__, __LINE__);
			goto out;
	}

	s = s_alloc(pk_size);
	out_uint8a(s, pk_data, pk_size);
	s_mark_end(s);
	s_seek(s, 0);

out:
	if ((e.size != 0) && (e.data)) {
		free(e.data);
	}

	if ((m.size != 0) && (m.data)) {
		free(m.data);
	}

	return s;
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
		(void)gnutls_bye(g_tls_session, GNUTLS_SHUT_WR);
		gnutls_deinit(g_tls_session);
		// Not needed since 3.3.0
		gnutls_global_deinit();

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
