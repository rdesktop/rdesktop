/*
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - TCP layer
   Copyright (C) Matthew Chapman 1999-2000
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "includes.h"

/* Establish a connection on the TCP layer */
HCONN tcp_connect(char *server)
{
	struct hostent *nslookup;
	struct sockaddr_in servaddr;
	struct connection *conn;
	int sock;
	int true = 1;

	if ((nslookup = gethostbyname(server)) != NULL)
	{
		memcpy(&servaddr.sin_addr, nslookup->h_addr, sizeof(servaddr.sin_addr));
	}
	else if (!inet_aton(server, &servaddr.sin_addr))
	{
		fprintf(stderr, "%s: unable to resolve host\n", server);
		return NULL;
	}

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "socket: %s\n", strerror(errno));
		return NULL;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(TCP_PORT_RDP);

	if (connect(sock, (struct sockaddr *)&servaddr, sizeof(struct sockaddr)) < 0)
	{
		fprintf(stderr, "connect: %s\n", strerror(errno));
		close(sock);
		return NULL;
	}

	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &true, sizeof(true));

	conn = xmalloc(sizeof(struct connection));
	STREAM_INIT(conn->in,  False);
	STREAM_INIT(conn->out, True);

	conn->tcp_socket = sock;
	return conn;
}

/* Disconnect on the TCP layer */
void tcp_disconnect(HCONN conn)
{
	close(conn->tcp_socket);
	free(conn);
}

/* Send TCP transport data packet */
BOOL tcp_send(HCONN conn)
{
	int length = conn->out.end;
	int sent, total = 0;

	while (total < length)
	{
		sent = write(conn->tcp_socket, conn->out.data + total,
			     length - total);

		if (sent <= 0)
		{
			fprintf(stderr, "write: %s\n", strerror(errno));
			return False;
		}

		total += sent;
	}

	conn->out.offset = 0;
	conn->out.end = conn->out.size;
	return True;
}

/* Receive a message on the TCP layer */
BOOL tcp_recv(HCONN conn, int length)
{
	int ret, rcvd = 0;
	struct timeval tv;
	fd_set rfds;

	STREAM_SIZE(conn->in, length);
	conn->in.end = conn->in.offset = 0;

	while (length > 0)
	{
		ui_process_events(conn->wnd, conn);

		FD_ZERO(&rfds);
		FD_SET(conn->tcp_socket, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 100;

		ret = select(conn->tcp_socket+1, &rfds, NULL, NULL, &tv);

		if (ret)
		{
			rcvd = read(conn->tcp_socket, conn->in.data
					+ conn->in.end, length);

			if (rcvd <= 0)
			{
				fprintf(stderr, "read: %s\n",
						strerror(errno));
				return False;
			}

			conn->in.end += rcvd;
			length -= rcvd;
		}
	}

	return True;
}
