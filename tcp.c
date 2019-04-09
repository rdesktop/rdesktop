/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - TCP layer
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright 2005-2011 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright 2012-2013 Henrik Andersson <hean01@cendio.se> for Cendio AB

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
#include <netdb.h>		/* gethostbyname */
#include <netinet/in.h>		/* sockaddr_in */
#include <netinet/tcp.h>	/* TCP_NODELAY */
#include <arpa/inet.h>		/* inet_addr */
#include <errno.h>		/* errno */
#endif

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/err.h>

#include "rdesktop.h"

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

static RD_BOOL g_ssl_initialized = False;
static SSL *g_ssl = NULL;
static SSL_CTX *g_ssl_ctx = NULL;
static int g_sock;
static RD_BOOL g_run_ui = False;
static struct stream g_in;
int g_tcp_port_rdp = TCP_PORT_RDP;
extern RD_BOOL g_user_quit;
extern RD_BOOL g_network_error;
extern RD_BOOL g_reconnect_loop;

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
	int ssl_err;
	size_t before;
	int length;
	int sent;
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

		if (g_ssl)
		{
			sent = SSL_write(g_ssl, data, length);
			if (sent <= 0)
			{
				ssl_err = SSL_get_error(g_ssl, sent);
				if (sent < 0 && (ssl_err == SSL_ERROR_WANT_READ ||
						 ssl_err == SSL_ERROR_WANT_WRITE))
				{
					tcp_can_send(g_sock, 100);
					sent = 0;
				}
				else
				{
#ifdef WITH_SCARD
					scard_unlock(SCARD_LOCK_TCP);
#endif

					error("SSL_write: %d (%s)\n", ssl_err, TCP_STRERROR);
					g_network_error = True;
					return;
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

					error("send: %s\n", TCP_STRERROR);
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
	int rcvd = 0, ssl_err;

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
		if ((!g_ssl || SSL_pending(g_ssl) <= 0) && g_run_ui)
		{
			if (!ui_select(g_sock))
			{
				/* User quit */
				g_user_quit = True;
				return NULL;
			}
		}

		before = s_tell(s);
		s_seek(s, s_length(s));

		out_uint8p(s, data, length);

		s_seek(s, before);

		if (g_ssl)
		{
			rcvd = SSL_read(g_ssl, data, length);
			ssl_err = SSL_get_error(g_ssl, rcvd);

			if (ssl_err == SSL_ERROR_SSL)
			{
				if (SSL_get_shutdown(g_ssl) & SSL_RECEIVED_SHUTDOWN)
				{
					error("Remote peer initiated ssl shutdown.\n");
					return NULL;
				}

				ERR_print_errors_fp(stdout);
				g_network_error = True;
				return NULL;
			}

			if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE)
			{
				rcvd = 0;
			}
			else if (ssl_err != SSL_ERROR_NONE)
			{
				error("SSL_read: %d (%s)\n", ssl_err, TCP_STRERROR);
				g_network_error = True;
				return NULL;
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
					error("recv: %s\n", TCP_STRERROR);
					g_network_error = True;
					return NULL;
				}
			}
			else if (rcvd == 0)
			{
				error("Connection closed\n");
				return NULL;
			}
		}

		// FIXME: Should probably have a macro for this
		s->end += rcvd;
		length -= rcvd;
	}

	return s;
}

/* Establish a SSL/TLS 1.0 connection */
RD_BOOL
tcp_tls_connect(void)
{
	int err;
	long options;

	if (!g_ssl_initialized)
	{
		SSL_load_error_strings();
		SSL_library_init();
		g_ssl_initialized = True;
	}

	/* create process context */
	if (g_ssl_ctx == NULL)
	{
		g_ssl_ctx = SSL_CTX_new(TLSv1_client_method());
		if (g_ssl_ctx == NULL)
		{
			error("tcp_tls_connect: SSL_CTX_new() failed to create TLS v1.0 context\n");
			goto fail;
		}

		options = 0;
#ifdef SSL_OP_NO_COMPRESSION
		options |= SSL_OP_NO_COMPRESSION;
#endif // __SSL_OP_NO_COMPRESSION
		options |= SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS;
		SSL_CTX_set_options(g_ssl_ctx, options);
	}

	/* free old connection */
	if (g_ssl)
		SSL_free(g_ssl);

	/* create new ssl connection */
	g_ssl = SSL_new(g_ssl_ctx);
	if (g_ssl == NULL)
	{
		error("tcp_tls_connect: SSL_new() failed\n");
		goto fail;
	}

	if (SSL_set_fd(g_ssl, g_sock) < 1)
	{
		error("tcp_tls_connect: SSL_set_fd() failed\n");
		goto fail;
	}

	do
	{
		err = SSL_connect(g_ssl);
	}
	while (SSL_get_error(g_ssl, err) == SSL_ERROR_WANT_READ);

	if (err < 0)
	{
		ERR_print_errors_fp(stdout);
		goto fail;
	}

	return True;

      fail:
	if (g_ssl)
		SSL_free(g_ssl);
	if (g_ssl_ctx)
		SSL_CTX_free(g_ssl_ctx);

	g_ssl = NULL;
	g_ssl_ctx = NULL;
	return False;
}

/* Get public key from server of TLS 1.0 connection */
STREAM
tcp_tls_get_server_pubkey()
{
	X509 *cert = NULL;
	EVP_PKEY *pkey = NULL;

	size_t len;
	unsigned char *data;
	STREAM s = NULL;

	if (g_ssl == NULL)
		goto out;

	cert = SSL_get_peer_certificate(g_ssl);
	if (cert == NULL)
	{
		error("tcp_tls_get_server_pubkey: SSL_get_peer_certificate() failed\n");
		goto out;
	}

	pkey = X509_get_pubkey(cert);
	if (pkey == NULL)
	{
		error("tcp_tls_get_server_pubkey: X509_get_pubkey() failed\n");
		goto out;
	}

	len = i2d_PublicKey(pkey, NULL);
	if (len < 1)
	{
		error("tcp_tls_get_server_pubkey: i2d_PublicKey() failed\n");
		goto out;
	}

	s = s_alloc(len);
	out_uint8p(s, data, len);
	i2d_PublicKey(pkey, &data);
	s_mark_end(s);
	s_seek(s, 0);

      out:
	if (cert)
		X509_free(cert);
	if (pkey)
		EVP_PKEY_free(pkey);
	return s;
}

/* Establish a connection on the TCP layer */
RD_BOOL
tcp_connect(char *server)
{
	socklen_t option_len;
	uint32 option_value;

#ifdef IPv6

	int n;
	struct addrinfo hints, *res, *ressave;
	char tcp_port_rdp_s[10];

	snprintf(tcp_port_rdp_s, 10, "%d", g_tcp_port_rdp);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((n = getaddrinfo(server, tcp_port_rdp_s, &hints, &res)))
	{
		error("getaddrinfo: %s\n", gai_strerror(n));
		return False;
	}

	ressave = res;
	g_sock = -1;
	while (res)
	{
		g_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (!(g_sock < 0))
		{
			if (connect(g_sock, res->ai_addr, res->ai_addrlen) == 0)
				break;
			TCP_CLOSE(g_sock);
			g_sock = -1;
		}
		res = res->ai_next;
	}
	freeaddrinfo(ressave);

	if (g_sock == -1)
	{
		error("%s: unable to connect\n", server);
		return False;
	}

#else /* no IPv6 support */

	struct hostent *nslookup;
	struct sockaddr_in servaddr;

	if ((nslookup = gethostbyname(server)) != NULL)
	{
		memcpy(&servaddr.sin_addr, nslookup->h_addr, sizeof(servaddr.sin_addr));
	}
	else if ((servaddr.sin_addr.s_addr = inet_addr(server)) == INADDR_NONE)
	{
		error("%s: unable to resolve host\n", server);
		return False;
	}

	if ((g_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		error("socket: %s\n", TCP_STRERROR);
		return False;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons((uint16) g_tcp_port_rdp);

	if (connect(g_sock, (struct sockaddr *) &servaddr, sizeof(struct sockaddr)) < 0)
	{
		if (!g_reconnect_loop)
			error("connect: %s\n", TCP_STRERROR);

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

	return True;
}

/* Disconnect on the TCP layer */
void
tcp_disconnect(void)
{
	if (g_ssl)
	{
		if (!g_network_error)
			(void) SSL_shutdown(g_ssl);
		SSL_free(g_ssl);
		g_ssl = NULL;
		SSL_CTX_free(g_ssl_ctx);
		g_ssl_ctx = NULL;
	}

	TCP_CLOSE(g_sock);
	g_sock = -1;
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
	if (g_in.data != NULL)
		xfree(g_in.data);
	g_in.p = NULL;
	g_in.end = NULL;
	g_in.data = NULL;
	g_in.size = 0;
	g_in.iso_hdr = NULL;
	g_in.mcs_hdr = NULL;
	g_in.sec_hdr = NULL;
	g_in.rdp_hdr = NULL;
	g_in.channel_hdr = NULL;
}

void
tcp_run_ui(RD_BOOL run)
{
	g_run_ui = run;
}
