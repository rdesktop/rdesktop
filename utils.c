/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Generic utility functions
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

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef HAVE_ICONV_H
#include <iconv.h>
#endif
#include "rdesktop.h"


#ifdef HAVE_ICONV
extern char g_codepage[16];
static RD_BOOL g_iconv_works = True;
#endif



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
#ifdef HAVE_ICONV
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
			warning("utils_string_to_utf8: iconv_open[%s -> %s] fail %p\n",
				g_codepage, "UTF-8", iconv_h);

			g_iconv_works = False;
			goto pass_trough_as_is;
		}
	}

	/* convert string */
	if (iconv(iconv_h, (ICONV_CONST char **) &src, &is, &dest, &os) == (size_t) - 1)
	{
		iconv_close(iconv_h);
		iconv_h = (iconv_t) - 1;
		warning("utils_string_to_utf8: iconv(1) fail, errno %d\n", errno);

		g_iconv_works = False;
		goto pass_trough_as_is;
	}

	/* Out couldn't hold the entire convertion */
	if (is != 0)
		return -1;

#endif
      pass_trough_as_is:
	/* can dest hold strcpy of src */
	if (os < (strlen(src) + 1))
		return -1;

	memcpy(dest, src, strlen(src) + 1);
	return 0;
}
