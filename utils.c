/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Generic utility functions
   Copyright 2013-2017 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <iconv.h>

#include "rdesktop.h"

#include "utils.h"

extern char g_codepage[16];
static RD_BOOL g_iconv_works = True;

char *
utils_string_escape(const char *str)
{
	const char *p;
	char *pe, *e, esc[4];
	size_t es;
	int cnt;

	/* count indices */
	cnt = 0;
	p = str;
	while (*(p++) != '\0')
		if ((unsigned char) *p < 32 || *p == '%')
			cnt++;

	/* if no characters needs escaping return copy of str */
	if (cnt == 0)
		return strdup(str);

	/* allocate new mem for result */
	es = strlen(str) + (cnt * 3) + 1;
	pe = e = xmalloc(es);
	memset(e, 0, es);
	p = str;
	while (*p != '\0')
	{
		if ((unsigned char) *p < 32 || *p == '%')
		{
			snprintf(esc, 4, "%%%02X", *p);
			memcpy(pe, esc, 3);
			pe += 3;
		}
		else
		{
			*pe = *p;
			pe++;
		}

		p++;
	}

	return e;
}

char *
utils_string_unescape(const char *str)
{
	char *ns, *ps, *pd, c;

	ns = xmalloc(strlen(str) + 1);
	memcpy(ns, str, strlen(str) + 1);
	ps = pd = ns;

	while (*ps != '\0')
	{
		/* check if found escaped character */
		if (ps[0] == '%')
		{
			if (sscanf(ps, "%%%2hhX", &c) == 1)
			{
				pd[0] = c;
				ps += 3;
				pd++;
				continue;
			}
		}

		/* just copy over the char */
		*pd = *ps;
		ps++;
		pd++;
	}
	pd[0] = '\0';

	return ns;
}

int
utils_mkdir_safe(const char *path, int mask)
{
	int res = 0;
	struct stat st;

	res = stat(path, &st);
	if (res == -1)
		return mkdir(path, mask);

	if (!S_ISDIR(st.st_mode))
	{
		errno = EEXIST;
		return -1;
	}

	return 0;
}

int
utils_mkdir_p(const char *path, int mask)
{
	int res;
	char *ptok;
	char pt[PATH_MAX];
	char bp[PATH_MAX];

	if (!path || strlen(path) == 0)
	{
		errno = EINVAL;
		return -1;
	}
	if (strlen(path) > PATH_MAX)
	{
		errno = E2BIG;
		return -1;
	}

	res = 0;
	pt[0] = bp[0] = '\0';
	strcpy(bp, path);

	ptok = strtok(bp, "/");
	if (ptok == NULL)
		return utils_mkdir_safe(path, mask);

	do
	{
		if (ptok != bp)
			strcat(pt, "/");

		strcat(pt, ptok);
		res = utils_mkdir_safe(pt, mask);
		if (res != 0)
			return res;

	}
	while ((ptok = strtok(NULL, "/")) != NULL);

	return 0;
}

/* Convert from system locale string to utf-8 */
int
utils_locale_to_utf8(const char *src, size_t is, char *dest, size_t os)
{
	static iconv_t *iconv_h = (iconv_t) - 1;
	if (strncmp(g_codepage, "UTF-8", strlen("UTF-8")) == 0)
		goto pass_trough_as_is;

	if (g_iconv_works == False)
		goto pass_trough_as_is;

	/* if not already initialize */
	if (iconv_h == (iconv_t) - 1)
	{
		if ((iconv_h = iconv_open("UTF-8", g_codepage)) == (iconv_t) - 1)
		{
			logger(Core, Warning,
			       "utils_string_to_utf8(), iconv_open[%s -> %s] fail %p", g_codepage,
			       "UTF-8", iconv_h);

			g_iconv_works = False;
			goto pass_trough_as_is;
		}
	}

	/* convert string */
	if (iconv(iconv_h, (char **) &src, &is, &dest, &os) == (size_t) - 1)
	{
		iconv_close(iconv_h);
		iconv_h = (iconv_t) - 1;
		logger(Core, Warning, "utils_string_to_utf8, iconv(1) fail, errno %d", errno);

		g_iconv_works = False;
		goto pass_trough_as_is;
	}

	/* Out couldn't hold the entire convertion */
	if (is != 0)
		return -1;

      pass_trough_as_is:
	/* can dest hold strcpy of src */
	if (os < (strlen(src) + 1))
		return -1;

	memcpy(dest, src, strlen(src) + 1);
	return 0;
}


/*
 * component logging
 *
 */
#include <stdarg.h>

static char *level[] = {
	"debug",
	"verbose",		/* Verbose mesasge for end user, no prefixed lines */
	"warning",
	"error",
	"notice"		/* Normal messages for end user, no prefixed lines */
};

static char *subject[] = {
	"UI",
	"Keyboard",
	"Clipboard",
	"Sound",
	"Protocol",
	"Graphics",
	"Core",
	"SmartCard",
	"Disk"
};

static log_level_t _logger_level = Warning;

#define DEFAULT_LOGGER_SUBJECTS (1 << Core);

#define ALL_LOGGER_SUBJECTS			\
	  (1 << GUI)				\
	| (1 << Keyboard)			\
	| (1 << Clipboard)			\
	| (1 << Sound)				\
	| (1 << Protocol)			\
	| (1 << Graphics)			\
	| (1 << Core)				\
	| (1 << SmartCard)                      \
	| (1 << Disk)


static int _logger_subjects = DEFAULT_LOGGER_SUBJECTS;

void
logger(log_subject_t s, log_level_t lvl, char *format, ...)
{
	va_list ap;
	char buf[1024];

	// Do not log if message is below global log level
	if (_logger_level > lvl)
		return;

	// Skip debug logging for non specified subjects
	if (lvl < Verbose && !(_logger_subjects & (1 << s)))
		return;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);

	// Notice and Verbose messages goes without prefix
	if (lvl == Notice || lvl == Verbose)
		fprintf(stdout, "%s\n", buf);
	else
		fprintf(stderr, "%s(%s): %s\n", subject[s], level[lvl], buf);

	va_end(ap);
}

void
logger_set_verbose(int verbose)
{
	if (_logger_level < Verbose)
		return;

	if (verbose)
		_logger_level = Verbose;
	else
		_logger_level = Warning;
}

void
logger_set_subjects(char *subjects)
{
	int clear;
	int bit;
	char *pcs;
	char *token;

	if (!subjects || !strlen(subjects))
		return;

	pcs = strdup(subjects);

	token = strtok(pcs, ",");
	if (token == NULL)
	{
		free(pcs);
		return;
	}

	_logger_subjects = 0;

	do
	{

		if (token == NULL)
			break;

		bit = 0;
		clear = (token[0] == '-') ? 1 : 0;

		if (clear == 1)
			token++;

		if (strcmp(token, "All") == 0)
			_logger_subjects |= ALL_LOGGER_SUBJECTS;
		else if (strcmp(token, "UI") == 0)
			bit = (1 << GUI);
		else if (strcmp(token, "Keyboard") == 0)
			bit = (1 << Keyboard);
		else if (strcmp(token, "Clipboard") == 0)
			bit = (1 << Clipboard);
		else if (strcmp(token, "Sound") == 0)
			bit = (1 << Sound);
		else if (strcmp(token, "Protocol") == 0)
			bit = (1 << Protocol);
		else if (strcmp(token, "Graphics") == 0)
			bit = (1 << Graphics);
		else if (strcmp(token, "Core") == 0)
			bit = (1 << Core);
		else if (strcmp(token, "SmartCard") == 0)
			bit = (1 << SmartCard);
		else if (strcmp(token, "Disk") == 0)
			bit = (1 << Disk);
		else
			continue;

		// set or clear logger subject bit
		if (clear)
			_logger_subjects &= ~bit;
		else
			_logger_subjects |= bit;

	}
	while ((token = strtok(NULL, ",")) != NULL);

	_logger_level = Debug;

	free(pcs);
}
