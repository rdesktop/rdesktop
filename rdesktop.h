/*
   rdesktop: A Remote Desktop Protocol client.
   Master include file
   Copyright (C) Matthew Chapman 1999-2000
   
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

#include <stdio.h>
#include <string.h>

#define VERSION "0.9.0-alpha2"

#define STATUS(args...) fprintf(stderr, args);
#define ERROR(args...)  fprintf(stderr, "ERROR: "args);
#define WARN(args...)   fprintf(stderr, "WARNING: "args);
#define NOTIMP(args...) fprintf(stderr, "NOTIMP: "args);

#ifdef RDP_DEBUG
#define DEBUG(args...)  fprintf(stderr, args);
#else
#define DEBUG(args...)
#endif

#include "constants.h"
#include "types.h"
#include "parse.h"

#ifndef MAKE_PROTO
#include "proto.h"
#endif
