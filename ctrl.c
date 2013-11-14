/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Master/Slave remote controlling
   Copyright 2013 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <limits.h>
#include <unistd.h>

#define CTRL_LINEBUF_SIZE 1024
#define CTRL_RESULT_SIZE 32
#define RDESKTOP_CTRLSOCK_STORE "/.local/share/rdesktop/ctrl"

#define CTRL_HASH_FLAG_SEAMLESS  1

#define ERR_RESULT_OK 0x00
#define ERR_RESULT_NO_SUCH_COMMAND 0xffffffff

extern RD_BOOL g_seamless_rdp;
extern uint8 g_static_rdesktop_salt_16[];

static RD_BOOL _ctrl_is_slave;
static int ctrlsock;
static char ctrlsock_name[PATH_MAX];
static struct _ctrl_slave_t *_ctrl_slaves;

#define CMD_SEAMLESS_SPAWN "seamless.spawn"

typedef struct _ctrl_slave_t
{
	struct _ctrl_slave_t *prev, *next;
	int sock;
	char linebuf[CTRL_LINEBUF_SIZE];
} _ctrl_slave_t;


static void
_ctrl_slave_new(int sock)
{
	_ctrl_slave_t *it, *ns;

	/* initialize new slave list item */
	ns = (_ctrl_slave_t *) xmalloc(sizeof(_ctrl_slave_t));
	memset(ns, 0, sizeof(_ctrl_slave_t));
	ns->sock = sock;

	/* append new slave to end of list */
	it = _ctrl_slaves;

	/* find last element in list */
	while (it && it->next)
		it = it->next;

	/* if last found append new */
	if (it)
	{
		it->next = ns;
		ns->prev = it;
	}
	else
	{
		/* no elemnts in list, lets add first */
		_ctrl_slaves = ns;
	}
}

static void
_ctrl_slave_disconnect(int sock)
{
	_ctrl_slave_t *it;

	if (!_ctrl_slaves)
		return;

	it = _ctrl_slaves;

	/* find slave with sock */
	while (it->next && it->sock != sock)
		it = it->next;

	if (it->sock == sock)
	{
		/* shutdown socket */
		shutdown(sock, SHUT_RDWR);
		close(sock);

		/* remove item from list */
		if (it == _ctrl_slaves)
		{
			if (it->next)
				_ctrl_slaves = it->next;
			else
				_ctrl_slaves = NULL;
		}

		if (it->prev)
		{
			(it->prev)->next = it->next;
			if (it->next)
				(it->next)->prev = it->prev;
		}
		else if (it->next)
			(it->next)->prev = NULL;

		xfree(it);

	}
}

static void
_ctrl_command_result(_ctrl_slave_t * slave, int result)
{
	char buf[64] = { 0 };

	/* translate and send result code back to client */
	if (result == 0)
		send(slave->sock, "OK\n", 3, 0);
	else
	{
		snprintf(buf, 64, "ERROR %x\n", result);
		send(slave->sock, buf, strlen(buf), 0);
	}
}

static void
_ctrl_dispatch_command(_ctrl_slave_t * slave)
{
	char *p;
	char *cmd;
	unsigned int res;

	/* unescape linebuffer */
	cmd = utils_string_unescape(slave->linebuf);
	if (strncmp(cmd, CMD_SEAMLESS_SPAWN " ", strlen(CMD_SEAMLESS_SPAWN) + 1) == 0)
	{
		/* process seamless spawn request */
		p = strstr(cmd, "seamlessrdpshell.exe");
		if (p)
			p += strlen("seamlessrdpshell.exe") + 1;
		else
			p = cmd + strlen(CMD_SEAMLESS_SPAWN) + 1;

		res = ERR_RESULT_OK;

		if (seamless_send_spawn(p) == (unsigned int) -1)
			res = 1;
	}
	else
	{
		res = ERR_RESULT_NO_SUCH_COMMAND;
	}
	xfree(cmd);

	_ctrl_command_result(slave, res);
}

static RD_BOOL
_ctrl_verify_unix_socket()
{
	int s, len;
	struct sockaddr_un saun;

	memset(&saun, 0, sizeof(struct sockaddr_un));

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		perror("Error creating ctrl client socket: socket()");
		exit(1);
	}

	saun.sun_family = AF_UNIX;
	strcpy(saun.sun_path, ctrlsock_name);
	len = sizeof(saun.sun_family) + strlen(saun.sun_path);

	/* test connection */
	if (connect(s, (struct sockaddr *) &saun, len) != 0)
		return False;

	shutdown(s, SHUT_RDWR);
	close(s);
	return True;
}


static void
_ctrl_create_hash(const char *user, const char *domain, const char *host, char *hash, size_t hsize)
{
	RDSSL_SHA1 sha1;
	uint8 out[20], delim;
	uint16 version;
	uint32 flags;

	/* version\0user\0domain\0host\0flags */
	flags = 0;
	delim = '\0';
	version = 0x0100;

	if (g_seamless_rdp)
		flags = CTRL_HASH_FLAG_SEAMLESS;

	rdssl_sha1_init(&sha1);
	rdssl_sha1_update(&sha1, (uint8 *) & version, sizeof(version));
	rdssl_sha1_update(&sha1, &delim, 1);

	if (user)
		rdssl_sha1_update(&sha1, (uint8 *) user, strlen(user));
	rdssl_sha1_update(&sha1, &delim, 1);

	if (domain)
		rdssl_sha1_update(&sha1, (uint8 *) domain, strlen(domain));
	rdssl_sha1_update(&sha1, &delim, 1);

	if (host)
		rdssl_sha1_update(&sha1, (uint8 *) host, strlen(host));
	rdssl_sha1_update(&sha1, &delim, 1);

	rdssl_sha1_update(&sha1, (uint8 *) & flags, sizeof(flags));
	rdssl_sha1_final(&sha1, out);

	sec_hash_to_string(hash, hsize, out, sizeof(out));
}


/** Initialize ctrl
    Ret values: <0 failure, 0 master, 1 client
 */
int
ctrl_init(const char *user, const char *domain, const char *host)
{
	struct stat st;
	struct sockaddr_un saun;
	char hash[41], path[PATH_MAX];
	char *home;

	/* check if ctrl already initialized */
	if (ctrlsock != 0 || _ctrl_is_slave)
		return 0;

	home = getenv("HOME");
	if (home == NULL)
	{
		return -1;
	}

	/* get uniq hash for ctrlsock name */
	_ctrl_create_hash(user, domain, host, hash, 41);
	snprintf(ctrlsock_name, PATH_MAX, "%s" RDESKTOP_CTRLSOCK_STORE "/%s.ctl", home, hash);
	ctrlsock_name[sizeof(ctrlsock_name) - 1] = '\0';

	/* make sure that ctrlsock store path exists */
	snprintf(path, PATH_MAX, "%s" RDESKTOP_CTRLSOCK_STORE, home);
	path[sizeof(path) - 1] = '\0';
	if (utils_mkdir_p(path, 0700) == -1)
	{
		perror(path);
		return -1;
	}

	/* check if ctrl socket already exist then this process becomes a client */
	if (stat(ctrlsock_name, &st) == 0)
	{
		/* verify that unix socket is not stale */
		if (_ctrl_verify_unix_socket() == True)
		{
			_ctrl_is_slave = True;
			return 1;
		}
		else
		{
			unlink(ctrlsock_name);
		}
	}

	/* setup ctrl socket and start listening for connections */
	if ((ctrlsock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		perror("Error creating ctrl socket:");
		exit(1);
	}

	/* bind and start listening on server socket */
	memset(&saun, 0, sizeof(struct sockaddr_un));
	saun.sun_family = AF_UNIX;
	strncpy(saun.sun_path, ctrlsock_name, sizeof(saun.sun_path));
	if (bind(ctrlsock, (struct sockaddr *) &saun, sizeof(struct sockaddr_un)) < 0)
	{
		perror("Error binding ctrl socket:");
		exit(1);
	}

	if (listen(ctrlsock, 5) < 0)
	{
		perror("Error listening on socket:");
		exit(1);
	}

	/* add ctrl cleanup func to exit hooks */
	atexit(ctrl_cleanup);

	return 0;
}

void
ctrl_cleanup()
{
	if (ctrlsock)
	{
		close(ctrlsock);
		unlink(ctrlsock_name);
	}
}

RD_BOOL
ctrl_is_slave()
{
	return _ctrl_is_slave;
}


void
ctrl_add_fds(int *n, fd_set * rfds)
{
	_ctrl_slave_t *it;
	if (ctrlsock == 0)
		return;

	FD_SET(ctrlsock, rfds);
	*n = MAX(*n, ctrlsock);


	/* add connected slaves to fd set */
	it = _ctrl_slaves;
	while (it)
	{
		FD_SET(it->sock, rfds);
		*n = MAX(*n, it->sock);
		it = it->next;
	}
}

void
ctrl_check_fds(fd_set * rfds, fd_set * wfds)
{
	int ns, res, offs;
	struct sockaddr_un fsaun;
	socklen_t fromlen;
	_ctrl_slave_t *it;

	if (ctrlsock == 0)
		return;

	memset(&fsaun, 0, sizeof(struct sockaddr_un));

	/* check if we got any connections on server socket */
	if (FD_ISSET(ctrlsock, rfds))
	{
		FD_CLR(ctrlsock, rfds);
		fromlen = sizeof(fsaun);
		ns = accept(ctrlsock, (struct sockaddr *) &fsaun, &fromlen);
		if (ns < 0)
		{
			perror("server: accept()");
			exit(1);
		}

		_ctrl_slave_new(ns);
		return;
	}

	/* check if any of our slaves fds has data */
	it = _ctrl_slaves;
	while (it)
	{
		if (FD_ISSET(it->sock, rfds))
		{
			offs = strlen(it->linebuf);
			res = recv(it->sock, it->linebuf + offs, CTRL_LINEBUF_SIZE - offs, 0);
			FD_CLR(it->sock, rfds);

			/* linebuffer full let's disconnect slave */
			if (it->linebuf[CTRL_LINEBUF_SIZE - 1] != '\0' &&
			    it->linebuf[CTRL_LINEBUF_SIZE - 1] != '\n')
			{
				_ctrl_slave_disconnect(it->sock);
				break;
			}

			if (res > 0)
			{
				/* Check if we got full command line */
				char *p;
				if ((p = strchr(it->linebuf, '\n')) == NULL)
					continue;

				/* iterate over string and check against escaped \n */
				while (p)
				{
					/* Check if newline is escaped */
					if (p > it->linebuf && *(p - 1) != '\\')
						break;
					p = strchr(p + 1, '\n');
				}

				/* If we havent found an nonescaped \n we need more data */
				if (p == NULL)
					continue;

				/* strip new linebuf and dispatch command */
				*p = '\0';
				_ctrl_dispatch_command(it);
				memset(it->linebuf, 0, CTRL_LINEBUF_SIZE);
			}
			else
			{
				/* Peer disconnected or socket error */
				_ctrl_slave_disconnect(it->sock);
				break;
			}
		}
		it = it->next;
	}
}

#if HAVE_ICONV
extern char g_codepage[16];
#endif

int
ctrl_send_command(const char *cmd, const char *arg)
{
	FILE *fp;
	struct sockaddr_un saun;
	int s, len, index, ret;
	char data[CTRL_LINEBUF_SIZE], tmp[CTRL_LINEBUF_SIZE];
	char result[CTRL_RESULT_SIZE], c, *escaped;

	escaped = NULL;

	if (!_ctrl_is_slave)
		return -1;

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		perror("Error creating ctrl client socket: socket()");
		exit(1);
	}

	memset(&saun, 0, sizeof(struct sockaddr_un));
	saun.sun_family = AF_UNIX;
	strcpy(saun.sun_path, ctrlsock_name);
	len = sizeof(saun.sun_family) + strlen(saun.sun_path);

	if (connect(s, (struct sockaddr *) &saun, len) < 0)
	{
		perror("Error connecting to ctrl socket: connect()");
		exit(1);
	}

	/* Bundle cmd and argument into string, convert to UTF-8 if needed */
	snprintf(data, CTRL_LINEBUF_SIZE, "%s %s", cmd, arg);
	ret = utils_locale_to_utf8(data, strlen(data), tmp, CTRL_LINEBUF_SIZE - 1);

	if (ret != 0)
		goto bail_out;

	/* escape the utf-8 string */
	escaped = utils_string_escape(tmp);
	if ((strlen(escaped) + 1) > CTRL_LINEBUF_SIZE - 1)
		goto bail_out;

	/* send escaped utf-8 command to master */
	send(s, escaped, strlen(escaped), 0);
	send(s, "\n", 1, 0);

	/* read result from master */
	fp = fdopen(s, "r");
	index = 0;
	while ((c = fgetc(fp)) != EOF && index < CTRL_RESULT_SIZE && c != '\n')
	{
		result[index] = c;
		index++;
	}
	result[index - 1] = '\0';

	if (strncmp(result, "ERROR ", 6) == 0)
	{
		if (sscanf(result, "ERROR %d", &ret) != 1)
			ret = -1;
	}

      bail_out:
	xfree(escaped);
	shutdown(s, SHUT_RDWR);
	close(s);

	return ret;
}
