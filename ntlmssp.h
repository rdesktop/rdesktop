/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   NTLMSSP client support for CredSSP.
   Copyright 2026 Marco Fortina <marco_fortina@hotmail.it>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#ifndef _NTLMSSP_H
#define _NTLMSSP_H

#include "ssl.h"

typedef struct _NTLMSSP_CONTEXT
{
	uint8 exported_session_key[16];
	uint8 client_signing_key[16];
	uint8 server_signing_key[16];
	RDSSL_RC4 client_seal;
	RDSSL_RC4 server_seal;
	uint32 send_seq;
	uint32 recv_seq;
	RD_BOOL established;
}
NTLMSSP_CONTEXT;

void ntlmssp_init(NTLMSSP_CONTEXT *ctx);
STREAM ntlmssp_build_negotiate(void);
RD_BOOL ntlmssp_build_authenticate(NTLMSSP_CONTEXT *ctx, STREAM negotiate, STREAM challenge,
				       const char *username, const char *domain,
				       const char *password, const char *server, STREAM *auth);
STREAM ntlmssp_wrap(NTLMSSP_CONTEXT *ctx, STREAM plain);
STREAM ntlmssp_unwrap(NTLMSSP_CONTEXT *ctx, STREAM wrapped);

#endif
