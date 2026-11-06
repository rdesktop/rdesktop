/* Display-selection policy tests; no running X server is needed. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <X11/Xlib.h>

static int scans, attempts, default_ok, sockets_available, foreign_owner;
static const char *working_display;
static int display_token;

static Display *
mock_open_display(const char *name)
{
	attempts++;
	if ((!name && default_ok) ||
	    (name && working_display && strcmp(name, working_display) == 0))
		return (Display *) &display_token;
	return NULL;
}

static int
mock_scandir(const char *path, struct dirent ***entries,
             int (*filter)(const struct dirent *),
             int (*compare)(const struct dirent **, const struct dirent **))
{
	const char *names[] = { "X0", "X1", "Xbad", "X", "." };
	int i, count = 0;
	(void) compare;
	assert(strcmp(path, "/tmp/.X11-unix") == 0);
	scans++;
	if (!sockets_available)
		return -1;
	*entries = calloc(5, sizeof(**entries));
	assert(*entries);
	for (i = 0; i < 5; i++)
	{
		struct dirent *entry = calloc(1, sizeof(*entry));
		assert(entry);
		strcpy(entry->d_name, names[i]);
		if (filter(entry))
			(*entries)[count++] = entry;
		else
			free(entry);
	}
	assert(count == 2);
	return count;
}

static int
mock_lstat(const char *path, struct stat *info)
{
	(void) path;
	memset(info, 0, sizeof(*info));
	info->st_mode = S_IFSOCK;
	info->st_uid = getuid() + foreign_owner;
	return 0;
}

#define XOpenDisplay mock_open_display
#define scandir mock_scandir
#define lstat mock_lstat
#include "../xdisplay.c"

static void
reset(void)
{
	scans = attempts = default_ok = foreign_owner = 0;
	sockets_available = 1;
	working_display = ":1";
	unsetenv("DISPLAY");
	unsetenv("WAYLAND_DISPLAY");
	unsetenv("XDG_SESSION_TYPE");
}

int
main(void)
{
	int fallback;
	Display *display;

	/* Working X11 display always wins, including forwarded displays. */
	reset();
	setenv("DISPLAY", "localhost:10.0", 1);
	setenv("WAYLAND_DISPLAY", "wayland-0", 1);
	default_ok = 1;
	assert(xdisplay_open(&fallback) != NULL);
	assert(!fallback && scans == 0 && attempts == 1);

	/* A failed explicit X11 selection must not connect to another session. */
	default_ok = 0;
	assert(xdisplay_open(&fallback) == NULL);
	assert(!fallback && scans == 0 && attempts == 2);

	/* Recover DISPLAY incorrectly set to the Wayland socket name. */
	reset();
	setenv("DISPLAY", "wayland-0", 1);
	setenv("WAYLAND_DISPLAY", "wayland-0", 1);
	display = xdisplay_open(&fallback);
	assert(display != NULL && fallback && scans == 1 && attempts == 3);
	assert(strcmp(getenv("DISPLAY"), "wayland-0") == 0);

	/* A missing DISPLAY is recoverable in a Wayland session only. */
	reset();
	setenv("XDG_SESSION_TYPE", "wayland", 1);
	assert(xdisplay_open(&fallback) != NULL && fallback);
	reset();
	assert(xdisplay_open(&fallback) == NULL);
	assert(!fallback && scans == 0);

	/* Custom Wayland socket names are supported too. */
	reset();
	setenv("DISPLAY", "compositor-socket", 1);
	setenv("WAYLAND_DISPLAY", "compositor-socket", 1);
	assert(xdisplay_open(&fallback) != NULL && fallback);

	/* Missing sockets or sockets owned by another user are not usable. */
	reset();
	setenv("WAYLAND_DISPLAY", "wayland-0", 1);
	sockets_available = 0;
	assert(xdisplay_open(&fallback) == NULL && !fallback);
	assert(attempts == 1);
	sockets_available = 1;
	foreign_owner = 1;
	assert(xdisplay_open(&fallback) == NULL && !fallback);
	assert(attempts == 2);

	/* Xlib authentication failures must be respected. */
	foreign_owner = 0;
	working_display = NULL;
	assert(xdisplay_open(&fallback) == NULL && !fallback);
	puts("Display selection tests passed.");
	return 0;
}
