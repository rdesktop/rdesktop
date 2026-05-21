/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Out-of-process static virtual channel add-ins
   Copyright (C) Matthew Chapman 1999-2008
   Copyright 2026 Marco Fortina <marco_fortina@hotmail.it>

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

#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "rdesktop.h"

#define ADDIN_MAX_CHANNELS 4
#define ADDIN_CHANNEL_NAME_MAX 8
#define ADDIN_READ_SIZE 4096

struct addin_channel
{
	char name[ADDIN_CHANNEL_NAME_MAX + 1];
	char *command;
	VCHANNEL *channel;
	pid_t pid;
	int child_stdin;
	int child_stdout;
	uint8 *pending;
	size_t pending_offset;
	size_t pending_length;
};

static struct addin_channel g_addins[ADDIN_MAX_CHANNELS];
static unsigned int g_num_addins;

static void addin_process_index(unsigned int index, STREAM s);

static void
addin_process_0(STREAM s)
{
	addin_process_index(0, s);
}

static void
addin_process_1(STREAM s)
{
	addin_process_index(1, s);
}

static void
addin_process_2(STREAM s)
{
	addin_process_index(2, s);
}

static void
addin_process_3(STREAM s)
{
	addin_process_index(3, s);
}

static void (*g_addin_callbacks[ADDIN_MAX_CHANNELS]) (STREAM) = {
	addin_process_0,
	addin_process_1,
	addin_process_2,
	addin_process_3
};

static RD_BOOL
addin_valid_channel_char(char c)
{
	return isalnum((unsigned char) c) || c == '_';
}

static int
addin_set_nonblocking(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1)
		return -1;

	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void
addin_close_fd(int *fd)
{
	if (*fd >= 0)
	{
		close(*fd);
		*fd = -1;
	}
}

static void
addin_release_pending(struct addin_channel *addin)
{
	if (addin->pending != NULL)
	{
		xfree(addin->pending);
		addin->pending = NULL;
	}

	addin->pending_offset = 0;
	addin->pending_length = 0;
}

static void
addin_shutdown_child(struct addin_channel *addin, RD_BOOL terminate)
{
	int status;

	addin_close_fd(&addin->child_stdin);
	addin_close_fd(&addin->child_stdout);
	addin_release_pending(addin);

	if (addin->pid > 0)
	{
		if (terminate)
		{
			kill(addin->pid, SIGTERM);
			kill(addin->pid, SIGKILL);
			while (waitpid(addin->pid, &status, 0) == -1 && errno == EINTR)
				;
		}
		else
		{
			while (waitpid(addin->pid, &status, WNOHANG) == -1 && errno == EINTR)
				;
		}
		addin->pid = -1;
	}
}

static void
addin_poll_child(struct addin_channel *addin)
{
	int status;
	pid_t result;

	if (addin->pid <= 0)
		return;

	result = waitpid(addin->pid, &status, WNOHANG);
	if (result == 0 || (result == -1 && errno == EINTR))
		return;

	if (result == addin->pid)
	{
		logger(Protocol, Warning, "addin '%s' process exited", addin->name);
		addin_shutdown_child(addin, False);
	}
	else if (result == -1 && errno == ECHILD)
	{
		addin_close_fd(&addin->child_stdin);
		addin_close_fd(&addin->child_stdout);
		addin_release_pending(addin);
		addin->pid = -1;
	}
}

static RD_BOOL
addin_spawn(struct addin_channel *addin)
{
	int in_pipe[2] = { -1, -1 };
	int out_pipe[2] = { -1, -1 };
	pid_t pid;

	if (pipe(in_pipe) != 0)
	{
		logger(Core, Error, "addin '%s' failed to create stdin pipe: %s",
		       addin->name, strerror(errno));
		return False;
	}

	if (pipe(out_pipe) != 0)
	{
		logger(Core, Error, "addin '%s' failed to create stdout pipe: %s",
		       addin->name, strerror(errno));
		close(in_pipe[0]);
		close(in_pipe[1]);
		return False;
	}

	pid = fork();
	if (pid == -1)
	{
		logger(Core, Error, "addin '%s' failed to fork: %s", addin->name,
		       strerror(errno));
		close(in_pipe[0]);
		close(in_pipe[1]);
		close(out_pipe[0]);
		close(out_pipe[1]);
		return False;
	}

	if (pid == 0)
	{
		signal(SIGPIPE, SIG_DFL);

		close(in_pipe[1]);
		close(out_pipe[0]);

		if (dup2(in_pipe[0], STDIN_FILENO) == -1 ||
		    dup2(out_pipe[1], STDOUT_FILENO) == -1)
			_exit(127);

		close(in_pipe[0]);
		close(out_pipe[1]);

		execl("/bin/sh", "sh", "-c", addin->command, (char *) NULL);
		_exit(127);
	}

	close(in_pipe[0]);
	close(out_pipe[1]);

	if (addin_set_nonblocking(in_pipe[1]) != 0 ||
	    addin_set_nonblocking(out_pipe[0]) != 0)
	{
		logger(Core, Error, "addin '%s' failed to set non-blocking pipes: %s",
		       addin->name, strerror(errno));
		close(in_pipe[1]);
		close(out_pipe[0]);
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		return False;
	}

	addin->pid = pid;
	addin->child_stdin = in_pipe[1];
	addin->child_stdout = out_pipe[0];
	logger(Protocol, Verbose, "registered addin channel '%s' with command '%s'",
	       addin->name, addin->command);
	return True;
}

static void
addin_flush_to_child(struct addin_channel *addin)
{
	ssize_t written;
	size_t remaining;

	addin_poll_child(addin);

	if (addin->child_stdin < 0 || addin->pending_length == 0)
		return;

	while (addin->pending_offset < addin->pending_length)
	{
		remaining = addin->pending_length - addin->pending_offset;
		written = write(addin->child_stdin, addin->pending + addin->pending_offset,
		                remaining);
		if (written > 0)
		{
			addin->pending_offset += written;
			continue;
		}

		if (written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
			return;

		logger(Protocol, Warning, "addin '%s' stdin write failed: %s", addin->name,
		       written == -1 ? strerror(errno) : "short write");
		addin_shutdown_child(addin, True);
		return;
	}

	addin_release_pending(addin);
}

static void
addin_queue_to_child(struct addin_channel *addin, STREAM s)
{
	size_t length;
	size_t old_remaining;
	uint8 *buffer;

	length = s_remaining(s);
	if (length == 0)
		return;

	if (addin->child_stdin < 0)
	{
		logger(Protocol, Warning, "discarding %u bytes for stopped addin '%s'",
		       (unsigned) length, addin->name);
		return;
	}

	old_remaining = addin->pending_length - addin->pending_offset;
	buffer = (uint8 *) xmalloc(old_remaining + length);
	if (old_remaining > 0)
		memcpy(buffer, addin->pending + addin->pending_offset, old_remaining);
	memcpy(buffer + old_remaining, s->p, length);

	addin_release_pending(addin);
	addin->pending = buffer;
	addin->pending_offset = 0;
	addin->pending_length = old_remaining + length;

	addin_flush_to_child(addin);
}

static void
addin_send_from_child(struct addin_channel *addin)
{
	uint8 buffer[ADDIN_READ_SIZE];
	ssize_t length;
	STREAM s;

	addin_poll_child(addin);

	if (addin->child_stdout < 0 || addin->channel == NULL)
		return;

	while (1)
	{
		length = read(addin->child_stdout, buffer, sizeof(buffer));
		if (length > 0)
		{
			s = channel_init(addin->channel, length);
			out_uint8a(s, buffer, length);
			s_mark_end(s);
			channel_send(s, addin->channel);
			s_free(s);
			continue;
		}

		if (length == 0)
		{
			logger(Protocol, Warning, "addin '%s' stdout closed", addin->name);
			addin_shutdown_child(addin, True);
			return;
		}

		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return;

		logger(Protocol, Warning, "addin '%s' stdout read failed: %s", addin->name,
		       strerror(errno));
		addin_shutdown_child(addin, True);
		return;
	}
}

static void
addin_process_index(unsigned int index, STREAM s)
{
	if (index >= g_num_addins)
		return;

	addin_queue_to_child(&g_addins[index], s);
}

RD_BOOL
addin_parse_option(const char *optarg)
{
	struct addin_channel *addin;
	const char *spec;
	const char *equals;
	size_t name_length;
	size_t i;

	if (g_num_addins >= ADDIN_MAX_CHANNELS)
	{
		logger(Core, Error, "too many -r addin options, maximum is %d",
		       ADDIN_MAX_CHANNELS);
		return False;
	}

	spec = optarg;
	if (*spec == ':')
		spec++;

	equals = strchr(spec, '=');
	if (equals == NULL || equals == spec || equals[1] == '\0')
	{
		logger(Core, Error, "invalid -r addin syntax, expected addin:<channel>=<command>");
		return False;
	}

	name_length = equals - spec;
	if (name_length > ADDIN_CHANNEL_NAME_MAX)
	{
		logger(Core, Error, "addin channel name '%.*s' is longer than %d bytes",
		       (int) name_length, spec, ADDIN_CHANNEL_NAME_MAX);
		return False;
	}

	for (i = 0; i < name_length; i++)
	{
		if (!addin_valid_channel_char(spec[i]))
		{
			logger(Core, Error,
			       "addin channel name '%.*s' contains invalid character '%c'",
			       (int) name_length, spec, spec[i]);
			return False;
		}
	}

	for (i = 0; i < g_num_addins; i++)
	{
		if (strlen(g_addins[i].name) == name_length &&
		    strncmp(g_addins[i].name, spec, name_length) == 0)
		{
			logger(Core, Error, "duplicate addin channel '%.*s'",
			       (int) name_length, spec);
			return False;
		}
	}

	addin = &g_addins[g_num_addins++];
	memset(addin, 0, sizeof(*addin));
	memcpy(addin->name, spec, name_length);
	addin->name[name_length] = '\0';
	addin->command = (char *) xmalloc(strlen(equals + 1) + 1);
	strcpy(addin->command, equals + 1);
	addin->pid = -1;
	addin->child_stdin = -1;
	addin->child_stdout = -1;
	return True;
}

RD_BOOL
addin_init(void)
{
	unsigned int i;
	RD_BOOL ok = True;

	if (g_num_addins == 0)
		return True;

	signal(SIGPIPE, SIG_IGN);

	for (i = 0; i < g_num_addins; i++)
	{
		g_addins[i].channel =
			channel_register(g_addins[i].name,
			                 CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP,
			                 g_addin_callbacks[i]);
		if (g_addins[i].channel == NULL)
		{
			logger(Core, Error, "failed to register addin channel '%s'",
			       g_addins[i].name);
			ok = False;
			continue;
		}

		if (!addin_spawn(&g_addins[i]))
			ok = False;
	}

	return ok;
}

void
addin_add_fds(int *n, fd_set * rfds, fd_set * wfds)
{
	unsigned int i;
	struct addin_channel *addin;

	for (i = 0; i < g_num_addins; i++)
	{
		addin = &g_addins[i];
		addin_poll_child(addin);

		if (addin->child_stdout >= 0)
		{
			FD_SET(addin->child_stdout, rfds);
			*n = MAX(*n, addin->child_stdout);
		}

		if (addin->child_stdin >= 0 && addin->pending_length > 0)
		{
			FD_SET(addin->child_stdin, wfds);
			*n = MAX(*n, addin->child_stdin);
		}
	}
}

void
addin_check_fds(fd_set * rfds, fd_set * wfds)
{
	unsigned int i;
	struct addin_channel *addin;

	for (i = 0; i < g_num_addins; i++)
	{
		addin = &g_addins[i];
		if (addin->child_stdin >= 0 && FD_ISSET(addin->child_stdin, wfds))
			addin_flush_to_child(addin);

		if (addin->child_stdout >= 0 && FD_ISSET(addin->child_stdout, rfds))
			addin_send_from_child(addin);

		addin_poll_child(addin);
	}
}

void
addin_deinit(void)
{
	unsigned int i;

	for (i = 0; i < g_num_addins; i++)
	{
		addin_shutdown_child(&g_addins[i], True);
		xfree(g_addins[i].command);
		g_addins[i].command = NULL;
	}

	g_num_addins = 0;
}
