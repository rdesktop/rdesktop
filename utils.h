/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.

   Copyright 2017-2019 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

#ifndef _utils_h
#define _utils_h

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "types.h"

uint32 utils_djb2_hash(const char *str);
char *utils_string_escape(const char *str);
char *utils_string_unescape(const char *str);
int utils_locale_to_utf8(const char *src, size_t is, char *dest, size_t os);
int utils_mkdir_safe(const char *path, int mask);
int utils_mkdir_p(const char *path, int mask);
void utils_calculate_dpi_scale_factors(uint32 width, uint32 height, uint32 dpi,
				       uint32 * physwidth, uint32 * physheight,
				       uint32 * desktopscale, uint32 * devicescale);
void utils_apply_session_size_limitations(uint32 * width, uint32 * height);

const char* util_dialog_choice(const char *message, ...);

int utils_cert_handle_exception(gnutls_session_t session, unsigned int status,
							    RD_BOOL hostname_mismatch, const char *hostname);

typedef enum log_level_t
{
	Debug = 0,
	Verbose,
	Warning,
	Error,
	Notice			/* special message level for end user messages with prefix */
} log_level_t;

typedef enum log_subject_t
{
	GUI = 0,
	Keyboard,
	Clipboard,
	Sound,
	Protocol,
	Graphics,
	Core,
	SmartCard,
	Disk
} log_subject_t;

void logger(log_subject_t c, log_level_t lvl, char *format, ...);
void logger_set_verbose(int verbose);
void logger_set_subjects(char *subjects);

#endif /* _utils_h */
