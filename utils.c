/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Generic utility functions
   Copyright 2013-2019 Henrik Andersson <hean01@cendio.se> for Cendio AB

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
#include <stdarg.h>
#include <assert.h>

#include "rdesktop.h"

#include "utils.h"

extern char g_codepage[16];

static RD_BOOL g_iconv_works = True;

uint32
utils_djb2_hash(const char *str)
{
	uint8 c;
	uint8 *pstr;
	uint32 hash = 5381;

	pstr = (uint8 *) str;
	while ((c = *pstr++))
	{
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

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
	char *ns, *ps, *pd;
	unsigned char c;

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
				pd[0] = (char) c;
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

/* Convert from system locale string to UTF-8 */
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

	/* Out couldn't hold the entire conversion */
	if (is != 0)
		return -1;

      pass_trough_as_is:
	/* can dest hold strcpy of src */
	if (os < (strlen(src) + 1))
		return -1;

	memcpy(dest, src, strlen(src) + 1);
	return 0;
}


void
utils_calculate_dpi_scale_factors(uint32 width, uint32 height, uint32 dpi,
				  uint32 * physwidth, uint32 * physheight,
				  uint32 * desktopscale, uint32 * devicescale)
{
	*physwidth = *physheight = *desktopscale = *devicescale = 0;

	if (dpi > 0)
	{
		*physwidth = width * 254 / (dpi * 10);
		*physheight = height * 254 / (dpi * 10);

		/* the spec calls this out as being valid for range
		   100-500 but I doubt the upper range is accurate */
		*desktopscale = dpi < 96 ? 100 : (dpi * 100 + 48) / 96;

		/* the only allowed values for device scale factor are
		   100, 140, and 180. */
		*devicescale = dpi < 134 ? 100 : (dpi < 173 ? 140 : 180);

	}
}


void
utils_apply_session_size_limitations(uint32 * width, uint32 * height)
{
	/* width MUST be even number */
	*width -= (*width) % 2;

	if (*width > 8192)
		*width = 8192;
	else if (*width < 200)
		*width = 200;

	if (*height > 8192)
		*height = 8192;
	else if (*height < 200)
		*height = 200;
}

#define MAX_CHOICES 10
const char *
util_dialog_choice(const char *message, ...)
{
	int i;
	va_list ap;
	char *p;
	const char *choice;
	char response[512];
	const char *choices[MAX_CHOICES] = {0};

	/* gather choices into array */
	va_start(ap, message);
	for (i = 0; i < MAX_CHOICES; i++)
	{
		choices[i] = va_arg(ap, const char *);
		if (choices[i] == NULL)
			break;
    }
    va_end(ap);

	choice = NULL;
	while (choice == NULL)
	{
		/* display message */
		fprintf(stderr,"\n%s", message);

		/* read input */
		if (fgets(response, sizeof(response), stdin) != NULL)
		{
			/* strip final newline */
			p = strchr(response, '\n');
			if (p != NULL)
				*p = 0;

			for (i = 0; i < MAX_CHOICES; i++)
			{
				if (choices[i] == NULL)
					break;

				if (strcmp(response, choices[i]) == 0)
				{
					choice = choices[i];
					break;
				}
			}
		}
		else
		{
			logger(Core, Error, "Failed to read response from stdin");
			break;
		}
	}

	return choice;
}

/*
 * component logging
 *
 */

static char *level[] = {
	"debug",
	"verbose",		/* Verbose message for end user, no prefixed lines */
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

#define DEFAULT_LOGGER_SUBJECTS (1 << Core)

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

	fflush(stdout);

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

static size_t
_utils_data_to_hex(uint8 *data, size_t len, char *out, size_t size)
{
	size_t i;
	char hex[4];

	assert((len * 2) < size);

	memset(out, 0, size);
	for (i = 0; i < len; i++)
	{
		snprintf(hex, sizeof(hex), "%.2x", data[i]);
		strcat(out, hex);
	}

	return (len*2);
}

static size_t
_utils_oid_to_string(const char *oid, char *out, size_t size)
{
	memset(out, 0, size);
	if (strcmp(oid, "0.9.2342.19200300.100.1.25") == 0) {
		snprintf(out, size, "%s", "DC");
	}
	else if (strcmp(oid, "2.5.4.3") == 0) {
		snprintf(out, size, "%s", "CN");
	}
	else if (strcmp(oid, "1.2.840.113549.1.1.13") == 0)
	{
		snprintf(out, size, "%s", "sha512WithRSAEncryption");
	}
	else
	{
		snprintf(out, size, "%s", oid);
	}

	return strlen(out);
}

static int
_utils_dn_to_string(gnutls_x509_dn_t dn, RD_BOOL exclude_oid,
				    char *out, size_t size)
{
	int i, j;
	char buf[128] = {0};
	char name[64] = {0};
	char result[1024] = {0};
	size_t left;
	gnutls_x509_ava_st ava;

	left = sizeof(result);

	for (j = 0; j < 100; j++)
	{
		for (i = 0; i < 100; i++)
		{
			if (gnutls_x509_dn_get_rdn_ava(dn, j, i, &ava) != 0)
			{
				break;
			}

			if (exclude_oid)
			{
				snprintf(buf, sizeof(buf), "%.*s", ava.value.size, ava.value.data);
				strncat(result, buf, left);
				left -= strlen(buf);
			}
			else
			{
				_utils_oid_to_string((char *)ava.oid.data, name, sizeof(name));
				snprintf(buf, sizeof(buf), "%s%s=%.*s",
						 (j > 0)?", ":"", name, ava.value.size, ava.value.data);
				strncat(result, buf, left);
				left -= strlen(buf);
			}
		}

		if (i == 0)
		{
			break;
		}
	}

	snprintf(out, size, "%s", result);

	return 0;
}

static void
_utils_cert_get_info(gnutls_x509_crt_t cert, char *out, size_t size)
{
	char buf[128];
	size_t buf_size;
	char digest[128];
	gnutls_x509_dn_t dn;
	time_t expire_ts, activated_ts;

	char subject[256];
	char issuer[256];
	char valid_from[256];
	char valid_to[256];
	char sha1[256];
	char sha256[256];

	/* get subject */
	gnutls_x509_crt_get_subject(cert, &dn);
	if (_utils_dn_to_string(dn, False, buf, sizeof(buf)) == 0)
	{
		snprintf(subject, sizeof(subject), "    Subject: %s", buf);
	}

	/* get issuer */
	gnutls_x509_crt_get_issuer(cert, &dn);
	if (_utils_dn_to_string(dn, False, buf, sizeof(buf)) == 0)
	{
		snprintf(issuer, sizeof(issuer), "     Issuer: %s", buf);
	}

	/* get activation / expiration time */
	activated_ts = gnutls_x509_crt_get_activation_time(cert);
	snprintf(valid_from, sizeof(valid_from), " Valid From: %s", ctime(&activated_ts));

	expire_ts = gnutls_x509_crt_get_expiration_time(cert);
	snprintf(valid_to, sizeof(valid_to), "         To: %s", ctime(&expire_ts));

	/* get sha1 / sha256 fingerprint */
	buf_size = sizeof(buf);
	gnutls_x509_crt_get_fingerprint(cert, GNUTLS_DIG_SHA1, buf, &buf_size);
	_utils_data_to_hex((uint8 *)buf, buf_size, digest, sizeof(digest));
	snprintf(sha1, sizeof(sha1), "       sha1: %s", digest);

	buf_size = sizeof(buf);
	gnutls_x509_crt_get_fingerprint(cert, GNUTLS_DIG_SHA256, buf, &buf_size);
	_utils_data_to_hex((uint8 *)buf, buf_size, digest, sizeof(digest));
	snprintf(sha256, sizeof(sha256), "     sha256: %s", digest);

	/* render cert info into out */
	snprintf(out, size,
		"%s\n"
		"%s\n"
		"%s"
		"%s"
		"\n"
		"  Certificate fingerprints:\n\n"
		"%s\n"
		"%s\n", subject, issuer, valid_from, valid_to, sha1, sha256);
}

static int
_utils_cert_san_to_string(gnutls_x509_crt_t cert, char *out, size_t size)
{
	int i, res;
	char entries[1024] = {0};
	char san[128] = {0};
	ssize_t left;
	size_t san_size;
	unsigned int san_type, critical;

	left = sizeof(entries);

	for(i = 0; i < 50; i++)
	{
		san_size = sizeof(san);
		res = gnutls_x509_crt_get_subject_alt_name2(cert, i, san, &san_size, &san_type, &critical);

		/* break if there are no more SAN entries */
		if (res <= 0)
			break;

		/* log if we cant handle more san entires in buffer */
		if (left <= 0)
		{
			logger(Core, Warning, "%s(), buffer is full, at least one SAN entry is missing from list", __func__);
			break;
		}

		/* add SAN entry to list */
		switch(san_type)
		{
			case GNUTLS_SAN_IPADDRESS:
			case GNUTLS_SAN_DNSNAME:

				if (left < (ssize_t)sizeof(entries))
				{
					strncat(entries, ", ", left);
					left -= 2;
				}

				strncat(entries, san, left);
				left -= strlen(san);

			break;
		}
	}

	if (strlen(entries) == 0)
	{
		return 1;
	}
	snprintf(out, size, "%s", entries);

	return 0;
}

static void
_utils_cert_get_status_report(gnutls_x509_crt_t cert, unsigned int status,
						     RD_BOOL hostname_mismatch, const char *hostname,
						     char *out, size_t size)
{
	int i;
	char buf[1024];
	char str[1024 + 64];

	i = 1;

	if (hostname_mismatch == True)
	{
		snprintf(buf, sizeof(buf),
			" %d. The hostname used for this connection does not match any of the names\n"
			"    given in the certificate.\n\n"
			"             Hostname: %s\n"
			, i++, hostname);
		strncat(out, buf, size - 1);
		size -= strlen(buf);

		/* parse subject dn */
		gnutls_x509_dn_t dn;
		gnutls_x509_crt_get_subject(cert, &dn);

		memset(buf, 0, sizeof(buf));
		if (_utils_dn_to_string(dn, True, buf, sizeof(buf)) == 0)
		{
			snprintf(str, sizeof(str), "          Common Name: %s\n", buf);
			strncat(out, str, size);
			size -= strlen(str);
		}

		/* get SAN entries */
		if (_utils_cert_san_to_string(cert, buf, sizeof(buf)) == 0)
		{
			snprintf(str, sizeof(str), "      Alternate names: %s\n", buf);
			strncat(out, str, size);
			size -= strlen(str);
		}

		strcat(out, "\n");
		size -= 1;
	}

	if (status & GNUTLS_CERT_REVOKED) {
		snprintf(buf, sizeof(buf),
			" %d. Certificate is revoked by its authority\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_SIGNER_NOT_FOUND) {
		snprintf(buf, sizeof(buf),
			" %d. Certificate issuer is not trusted by this system.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);

		/* parse subject dn */
		gnutls_x509_dn_t dn;
		gnutls_x509_crt_get_issuer(cert, &dn);

		memset(buf, 0, sizeof(buf));
		if (_utils_dn_to_string(dn, False, buf, sizeof(buf)) == 0)
		{
			snprintf(str, sizeof(str), "     Issuer: %s\n\n", buf);
			strncat(out, str, size);
			size -= strlen(str);
		}
	}

	if (status & GNUTLS_CERT_SIGNER_NOT_CA) {
		snprintf(buf, sizeof(buf),
			" %d. Certificate signer is not a CA.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_INSECURE_ALGORITHM) {
		snprintf(buf, sizeof(buf),
			" %d. Certificate was signed using an insecure algorithm.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
		/* TODO: print algorithm*/
	}

	if (status & GNUTLS_CERT_NOT_ACTIVATED) {
		snprintf(buf, sizeof(buf),
			" %d. Certificate is not yet activated.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);

		time_t ts = gnutls_x509_crt_get_activation_time(cert);
		snprintf(buf, sizeof(buf), "     Valid From: %s\n\n", ctime(&ts));
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_EXPIRED) {
		snprintf(buf, sizeof(buf),
			" %d. Certificate has expired.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);

		time_t ts = gnutls_x509_crt_get_expiration_time(cert);
		snprintf(buf, sizeof(buf), "     Valid to: %s\n\n", ctime(&ts));
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_SIGNATURE_FAILURE) {
		snprintf(buf, sizeof(buf),
			" %d. Failed to verify the signature of the certificate.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_REVOCATION_DATA_SUPERSEDED) {
		snprintf(buf, sizeof(buf),
			" %d. Revocation data are old and have been superseded.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_UNEXPECTED_OWNER) {
		snprintf(buf, sizeof(buf),
			" %d. The owner is not the expected one.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_REVOCATION_DATA_ISSUED_IN_FUTURE) {
		snprintf(buf, sizeof(buf),
			" %d. The revocation data have a future issue date.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_SIGNER_CONSTRAINTS_FAILURE) {
		snprintf(buf, sizeof(buf),
			" %d. The certificate's signer constraints were violated.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_MISMATCH) {
		snprintf(buf, sizeof(buf),
			" %d. The certificate presented isn't the expected one (TOFU)\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

#if GNUTLS_VERSION_NUMBER >= 0x030400
	if (status & GNUTLS_CERT_PURPOSE_MISMATCH) {
		snprintf(buf, sizeof(buf),
			" %d. The certificate or an intermediate does not match the\n"
			"     intended purpose (extended key usage).\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}
#endif

#if GNUTLS_VERSION_NUMBER >= 0x030501
	if (status & GNUTLS_CERT_MISSING_OCSP_STATUS) {
		snprintf(buf, sizeof(buf),
			" %d. The certificate requires the server to send the certifiate\n"
			"     status, but no status was received.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}

	if (status & GNUTLS_CERT_INVALID_OCSP_STATUS) {
		snprintf(buf, sizeof(buf),
			" %d. The received OCSP status response is invalid.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}
#endif

#if GNUTLS_VERSION_NUMBER >= 0x030600
	if (status & GNUTLS_CERT_UNKNOWN_CRIT_EXTENSIONS) {
		snprintf(buf, sizeof(buf),
			" %d. The certificate has extensions marked as critical which are\n"
			"     not supported.\n\n", i++);
		strncat(out, buf, size);
		size -= strlen(buf);
	}
#endif
}

static int
_utils_cert_store_get_filename(char *out, size_t size)
{
	int rv;
	char *home;
	char dir[PATH_MAX - 12];
	struct stat sb;

	home = getenv("HOME");

	if (home == NULL)
		return 1;

	if (snprintf(dir, sizeof(dir) - 1, "%s/%s", home, ".local/share/rdesktop/certs/") > (int)sizeof(dir))
	{
		logger(Core, Error, "%s(), certificate store directory is truncated", __func__);
		return 1;
	}

	if ((rv = stat(dir, &sb)) == -1)
	{
		if (errno == ENOENT)
		{
			if (rd_certcache_mkdir() == False) {
				logger(Core, Error, "%s(), failed to create directory '%s'", __func__, dir);
				return 1;
			}
		}
	}
	else
	{
		if ((sb.st_mode & S_IFMT) != S_IFDIR)
		{
			logger(Core, Error, "%s(), %s exists but it's not a directory",
				   __func__, dir);
			return 1;
		}
	}

	if (snprintf(out, size, "%s/known_certs", dir) > (int)size)
	{
		logger(Core, Error, "%s(), certificate store filename is truncated", __func__);
		return 1;
	}

	return 0;
}

#define TRUST_CERT_PROMPT_TEXT "Do you trust this certificate (yes/no)? "
#define REVIEW_CERT_TEXT \
	"Review the following certificate info before you trust it to be added as an exception.\n" \
	"If you do not trust the certificate the connection atempt will be aborted:"

int
utils_cert_handle_exception(gnutls_session_t session, unsigned int status,
						    RD_BOOL hostname_mismatch, const char *hostname)
{
	int rv;
	int type;
	time_t exp_time;
	gnutls_x509_crt_t cert;
	const gnutls_datum_t *cert_list;
	unsigned int cert_list_size = 0;

	char certcache_fn[PATH_MAX];
	char cert_info[2048] = {0};
	char cert_invalid_reasons[2048] = {0};
	char message[8192] = {0};
	const char *response;

	/* get filename for certificate exception store */
	if (_utils_cert_store_get_filename(certcache_fn, sizeof(certcache_fn)) != 0)
	{
		logger(Core, Error, "%s(), Failed to get certificate store file, "
							"disabling exception handling.", __func__);
		return 1;
	}

	type = gnutls_certificate_type_get(session);
	if (type != GNUTLS_CRT_X509)
	{
		logger(Core, Error, "%s(), Certificate for session is not an x509 certificate, "
							"disabling exception handling.", __func__);
		return 1;
	}


	cert_list = gnutls_certificate_get_peers(session, &cert_list_size);
	if (cert_list_size == 0)
	{
		logger(Core, Error, "%s(), Failed to get certificate, "
							"disabling exception handling.", __func__);
		return 1;
	}

	rv = gnutls_verify_stored_pubkey(certcache_fn, NULL, hostname, "rdesktop", type, &cert_list[0], 0);
	if (rv == GNUTLS_E_SUCCESS)
	{
		/* Certificate found in store and matches server */
		logger(Core, Warning, "Certificate received from server is NOT trusted by this system, "
			"an exception has been added by the user to trust this specific certificate.");
		return 0;
	}
	else if (rv !=  GNUTLS_E_CERTIFICATE_KEY_MISMATCH && rv != GNUTLS_E_NO_CERTIFICATE_FOUND)
	{
		/* Unhandled errors */
		logger(Core, Error, "%s(), verification for host '%s' certificate failed. Error = 0x%x (%s)",
				__func__, hostname, rv, gnutls_strerror(rv));
		return 1;
	}

	/*
	 * Give user possibility to add / update certificate to store
	 */

	gnutls_x509_crt_init(&cert);
	gnutls_x509_crt_import(cert, &cert_list[0], GNUTLS_X509_FMT_DER);
	_utils_cert_get_info(cert, cert_info, sizeof(cert_info));

	if (rv == GNUTLS_E_CERTIFICATE_KEY_MISMATCH)
	{
		/* Certificate from server mismatches the one in store */

		snprintf(message, sizeof(message),
			"ATTENTION! Found a certificate stored for host '%s', but it does not match the certificate\n"
			"received from server.\n"
			REVIEW_CERT_TEXT
			"\n\n"
			"%s"
			"\n\n"
			TRUST_CERT_PROMPT_TEXT
			, hostname, cert_info);


	}
	else if (rv == GNUTLS_E_NO_CERTIFICATE_FOUND)
	{
		/* Certificate is not found in store, propose to add an exception */
		_utils_cert_get_status_report(cert, status, hostname_mismatch, hostname,
			cert_invalid_reasons, sizeof(cert_invalid_reasons));

		snprintf(message, sizeof(message),
			"ATTENTION! The server uses and invalid security certificate which can not be trusted for\n"
			"the following identified reasons(s);\n\n"
			"%s"
			"\n"
			REVIEW_CERT_TEXT
			"\n\n"
			"%s"
			"\n\n"
			TRUST_CERT_PROMPT_TEXT,
			cert_invalid_reasons, cert_info);
	}

	/* show dialog */
	response = util_dialog_choice(message, "no", "yes", NULL);
	if (strcmp(response, "no") == 0 || response == NULL)
	{
		return 1;
	}

	/* user responded with yes, lets add certificate to store */
	logger(Core, Debug, "%s(), adding a new certificate for the host '%s'", __func__, hostname);
	exp_time = gnutls_x509_crt_get_expiration_time(cert);
	rv = gnutls_store_pubkey(certcache_fn, NULL, hostname, "rdesktop", type, &cert_list[0], exp_time, 0);
	if (rv != GNUTLS_E_SUCCESS)
	{
		logger(Core, Error, "%s(), failed to store certificate. error = 0x%x (%s)", __func__, rv, gnutls_strerror(rv));
		return 1;
	}

	return 0;
}
