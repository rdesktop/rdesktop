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

#include <unistd.h>		/* select read write close */
#include <sys/socket.h>		/* socket connect setsockopt */
#include <sys/time.h>		/* timeval */
#include <netdb.h>		/* gethostbyname */
#include <netinet/in.h>		/* sockaddr_in */
#include <netinet/tcp.h>	/* TCP_NODELAY */
#include <arpa/inet.h>		/* inet_addr */
#include <errno.h>		/* errno */
#include "rdesktop.h"

static int sock;
static struct stream in;
static struct stream out;

/* Initialise TCP transport data packet */
STREAM tcp_init(int maxlen)
{
	if (maxlen > out.size)
	{
		out.data = xrealloc(out.data, maxlen);
		out.size = maxlen;
	}

	out.p = out.data;
	out.end = out.data + out.size;
	return &out;
}

/* Send TCP transport data packet */
void tcp_send(STREAM s)
{
	int length = s->end - s->data;
	int sent, total = 0;

	while (total < length)
	{
		sent = write(sock, s->data + total, length - total);

		if (sent <= 0)
		{
			STATUS("write: %s\n", strerror(errno));
			return;
		}

		total += sent;
	}
}

/* Receive a message on the TCP layer */
STREAM tcp_recv(int length)
{
	int ret, rcvd = 0;
	struct timeval tv;
	fd_set rfds;

	if (length > in.size)
	{
		in.data = xrealloc(in.data, length);
		in.size = length;
	}

	in.end = in.p = in.data;

	while (length > 0)
	{
		ui_process_events();

		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 100;

		ret = select(sock + 1, &rfds, NULL, NULL, &tv);

		if (ret)
		{
			rcvd = read(sock, in.end, length);

			if (rcvd <= 0)
			{
				STATUS("read: %s\n", strerror(errno));
				return NULL;
			}

			in.end += rcvd;
			length -= rcvd;
		}
	}

	return &in;
}

/* Establish a connection on the TCP layer */
BOOL tcp_connect(char *server)
{
	struct hostent *nslookup;
	struct sockaddr_in servaddr;
	int true = 1;

	if ((nslookup = gethostbyname(server)) != NULL)
	{
		memcpy(&servaddr.sin_addr, nslookup->h_addr,
		       sizeof(servaddr.sin_addr));
	}
	else if (!(servaddr.sin_addr.s_addr = inet_addr(server)))
	{
		STATUS("%s: unable to resolve host\n", server);
		return False;
	}

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		STATUS("socket: %s\n", strerror(errno));
		return False;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(TCP_PORT_RDP);

	if (connect
	    (sock, (struct sockaddr *) &servaddr,
	     sizeof(struct sockaddr)) < 0)
	{
		STATUS("connect: %s\n", strerror(errno));
		close(sock);
		return False;
	}

	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void *) &true,
		   sizeof(true));

	in.size = 4096;
	in.data = xmalloc(in.size);

	out.size = 4096;
	out.data = xmalloc(out.size);

	return True;
}

/* Disconnect on the TCP layer */
void tcp_disconnect()
{
	close(sock);
}
