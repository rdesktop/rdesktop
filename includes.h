/*
   rdesktop: A Remote Desktop Protocol client.
   Global include file
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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>

#if 0
#define False (0)
#define True  (1)
#endif

typedef int BOOL;
typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int   uint32;

#include "xwin.h"
#include "misc.h"
#include "parse.h"
#include "tcp.h"
#include "iso.h"
#include "mcs.h"
#include "rdp.h"

#ifndef MAKE_PROTO
#include "proto.h"
#endif
