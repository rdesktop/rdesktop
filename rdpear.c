/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Remote Credential Guard authentication redirection channel.
   Copyright 2026 Marco Fortina

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

#define RDPEAR_CHANNEL_NAME "Microsoft::Windows::RDS::AuthRedirection"

extern RD_BOOL g_remote_guard;

static void
rdpear_process_pdu(STREAM s)
{
	STREAM plain;

	if (!g_remote_guard)
		return;

	logger(Protocol, Debug, "rdpear_process_pdu(), received %u bytes",
	       (unsigned) s_remaining(s));

	if (!cssp_remote_guard_has_security_context())
	{
		logger(Core, Error,
		       "rdpear_process_pdu(), Remote Guard channel data arrived before CredSSP security context was retained");
		return;
	}

	plain = cssp_remote_guard_unwrap(s);
	if (plain == NULL)
	{
		logger(Core, Warning,
		       "rdpear_process_pdu(), failed to decrypt Remote Guard authentication redirection payload");
		return;
	}

	logger(Core, Warning,
	       "rdpear_process_pdu(), Remote Guard authentication redirection payload received, but local credential package handling is not implemented yet");

	s_free(plain);
}

RD_BOOL
rdpear_init(void)
{
	if (!g_remote_guard)
		return True;

	if (!dvc_channels_register(RDPEAR_CHANNEL_NAME, rdpear_process_pdu))
	{
		logger(Core, Error,
		       "rdpear_init(), failed to register Remote Guard authentication redirection channel '%s'",
		       RDPEAR_CHANNEL_NAME);
		return False;
	}

	logger(Core, Verbose,
	       "Registered Remote Guard authentication redirection channel '%s'",
	       RDPEAR_CHANNEL_NAME);
	return True;
}
