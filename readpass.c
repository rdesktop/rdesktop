/*
   rdesktop: A Remote Desktop Protocol client.
   User interface services - X keyboard mapping
   Copyright (C) Matthew Chapman 1999-2001
   
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

#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include "rdesktop.h"
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Execute specified askpass program and fetch password from standard
   output. Return NULL on failure, otherwise a pointer to the data
   read (which must be freed by caller) */

char *
askpass(char *askpass, const char *msg)
{
	pid_t pid;
	size_t len;
	char *pass;
	int p[2], status, ret;
	char buf[1024];
	int devnull;

	if (fflush(stdout) != 0)
		error("askpass: fflush: %s", strerror(errno));
	assert(askpass != NULL);
	if (pipe(p) < 0)
	{
		error("askpass: pipe: %s", strerror(errno));
		return NULL;
	}

	pid = fork();
	switch (pid)
	{
		case -1:
			error("askpass: fork: %s", strerror(errno));
			return NULL;
			break;
		case 0:
			/* Child */
			seteuid(getuid());
			setuid(getuid());
			/* Close read end */
			close(p[0]);

			/* Setup stdin */
			devnull = open("/dev/null", 0, O_RDONLY);
			if (dup2(devnull, STDIN_FILENO) < 0)
			{
				error("askpass: dup2: %s", strerror(errno));
				exit(1);
			}
			close(devnull);

			/* Setup stdout */
			if (dup2(p[1], STDOUT_FILENO) < 0)
			{
				error("askpass: dup2: %s", strerror(errno));
				exit(1);
			}
			close(p[1]);

			/* By now, the following fds are open: 
			   0 -> /dev/null 
			   1 -> pipe write end
			   2 -> users terminal */
			execlp(askpass, askpass, msg, (char *) 0);
			error("askpass: exec(%s): %s", askpass, strerror(errno));
			exit(1);
			break;
		default:
			/* Parent */
			break;
	}
	/* Close write end */
	close(p[1]);

	len = ret = 0;
	do
	{
		ret = read(p[0], buf + len, sizeof(buf) - 1 - len);

		if (ret == -1 && errno == EINTR)
			continue;
		if (ret <= 0)
			break;

		len += ret;
	}
	while (sizeof(buf) - 1 - len > 0);


	buf[len] = '\0';

	close(p[0]);
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			break;

	if (WIFEXITED(status))
	{
		if (WEXITSTATUS(status))
		{
			error("askpass program returned %d\n", WEXITSTATUS(status));
			return NULL;
		}
	}
	else
	{
		error("abnormal exit from askpass program");
		return NULL;
	}

	buf[strcspn(buf, "\r\n")] = '\0';
	pass = strdup(buf);
	memset(buf, 0, sizeof(buf));
	return pass;
}
