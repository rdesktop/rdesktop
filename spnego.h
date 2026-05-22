/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Minimal SPNEGO helpers for CredSSP NTLM.
   Copyright 2026 Marco Fortina <marco_fortina@hotmail.it>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#ifndef _SPNEGO_H
#define _SPNEGO_H

STREAM spnego_wrap_neg_token_init(STREAM mech_token);
STREAM spnego_wrap_neg_token_resp(STREAM mech_token);
STREAM spnego_extract_ntlm_token(STREAM token);

#endif
