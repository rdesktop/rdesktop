/* X11 display selection, including compatibility with Wayland sessions. */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xdisplay.h"

int
xdisplay_is_wayland_session(void)
{
	const char *wayland = getenv("WAYLAND_DISPLAY");
	const char *session = getenv("XDG_SESSION_TYPE");
	const char *display = getenv("DISPLAY");

	return (wayland && *wayland) || (session && strcmp(session, "wayland") == 0) ||
	       (display && strncmp(display, "wayland-", 8) == 0);
}

static int
is_x_socket_name(const struct dirent *entry)
{
	const char *name = entry->d_name;
	return name[0] == 'X' && name[1] &&
	       strspn(name + 1, "0123456789") == strlen(name + 1);
}

Display *
xdisplay_open(int *used_xwayland)
{
	const char *configured = getenv("DISPLAY");
	const char *wayland = getenv("WAYLAND_DISPLAY");
	struct dirent **entries;
	Display *display;
	int count, i;

	*used_xwayland = 0;
	display = XOpenDisplay(NULL);
	if (display || !xdisplay_is_wayland_session())
		return display;

	/* Never replace an explicitly selected X11 display, even if it fails. */
	if (configured && *configured &&
	    (strchr(configured, ':') ||
	     (strncmp(configured, "wayland-", 8) != 0 &&
	      (!wayland || strcmp(configured, wayland) != 0))))
		return NULL;

	count = scandir("/tmp/.X11-unix", &entries, is_x_socket_name, alphasort);
	if (count < 0)
		return NULL;

	for (i = 0; i < count; i++)
	{
		char path[PATH_MAX];
		char candidate[sizeof(entries[i]->d_name) + 1];
		struct stat info;

		/* Only try local sockets owned by this user; retain Xlib authentication. */
		snprintf(path, sizeof(path), "/tmp/.X11-unix/%s", entries[i]->d_name);
		if (!display && lstat(path, &info) == 0 && S_ISSOCK(info.st_mode) &&
		    info.st_uid == getuid())
		{
			snprintf(candidate, sizeof(candidate), ":%s", entries[i]->d_name + 1);
			display = XOpenDisplay(candidate);
		}
		free(entries[i]);
	}
	free(entries);
	*used_xwayland = display != NULL;
	return display;
}
