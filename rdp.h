/*
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - RDP layer
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

enum RDP_PDU_TYPE
{
	RDP_PDU_DEMAND_ACTIVE = 1,
	RDP_PDU_CONFIRM_ACTIVE = 3,
	RDP_PDU_DATA = 7
};

enum RDP_DATA_PDU_TYPE
{
	RDP_DATA_PDU_UPDATE = 2,
	RDP_DATA_PDU_CONTROL = 20,
	RDP_DATA_PDU_POINTER = 27,
	RDP_DATA_PDU_INPUT = 28,
	RDP_DATA_PDU_SYNCHRONIZE = 31,
	RDP_DATA_PDU_FONT2 = 39
};

typedef struct _RDP_HEADER
{
	uint16 length;
	uint16 pdu_type;
	uint16 userid;

} RDP_HEADER;

typedef struct _RDP_DATA_HEADER
{
	uint32 shareid;
	uint8 pad;
	uint8 streamid;
	uint16 length;
	uint8 data_pdu_type;
	uint8 compress_type;
	uint16 compress_len;

} RDP_DATA_HEADER;

#define RDP_CAPSET_GENERAL     1
#define RDP_CAPLEN_GENERAL     0x18
#define OS_MAJOR_TYPE_UNIX     4
#define OS_MINOR_TYPE_XSERVER  7

typedef struct _RDP_GENERAL_CAPS
{
	uint16 os_major_type;
	uint16 os_minor_type;
	uint16 ver_protocol;
	uint16 pad1;
	uint16 compress_types;
	uint16 pad2;
	uint16 cap_update;
	uint16 remote_unshare;
	uint16 compress_level;
	uint16 pad3;

} RDP_GENERAL_CAPS;

#define RDP_CAPSET_BITMAP      2
#define RDP_CAPLEN_BITMAP      0x1C

typedef struct _RDP_BITMAP_CAPS
{
	uint16 preferred_bpp;
	uint16 receive1bpp;
	uint16 receive4bpp;
	uint16 receive8bpp;
	uint16 width;
	uint16 height;
	uint16 pad1;
	uint16 allow_resize;
	uint16 compression;
	uint16 unknown1;
	uint16 unknown2;
	uint16 pad2;

} RDP_BITMAP_CAPS;

#define RDP_CAPSET_ORDER       3
#define RDP_CAPLEN_ORDER       0x58
#define ORDER_CAP_NEGOTIATE    2
#define ORDER_CAP_NOSUPPORT    4

typedef struct _RDP_ORDER_CAPS
{
	uint8 terminal_desc[16];
	uint32 pad1;
	uint16 xgranularity; // 1
	uint16 ygranularity; // 20
	uint16 pad2;
	uint16 max_order_level;
	uint16 num_fonts; // 0x2C
	uint16 cap_flags; // 0x22
	uint8 support[32];
	uint16 text_cap_flags; // 0x6A1
	uint16 pad3;
	uint32 pad4;
	uint32 desk_save_size;
	uint32 unknown1; // 1 from server, 0 from client
	uint32 unknown2; // 0x4E4 from client

} RDP_ORDER_CAPS;

#define RDP_CAPSET_BMPCACHE    4
#define RDP_CAPLEN_BMPCACHE    0x28

typedef struct _RDP_BMPCACHE_INFO
{
	uint16 entries;
	uint16 max_cell_size;

} RDP_BMPCACHE_INFO;

typedef struct _RDP_BMPCACHE_CAPS
{
	uint32 unused[6];
	RDP_BMPCACHE_INFO caches[3];

} RDP_BMPCACHE_CAPS;

#define RDP_CAPSET_CONTROL     5
#define RDP_CAPLEN_CONTROL     0x0C

typedef struct _RDP_CONTROL_CAPS
{
	uint16 control_caps;
	uint16 remote_detach;
	uint16 control_interest;
	uint16 detach_interest;

} RDP_CONTROL_CAPS;

#define RDP_CAPSET_ACTIVATE    7
#define RDP_CAPLEN_ACTIVATE    0x0C

typedef struct _RDP_ACTIVATE_CAPS
{
	uint16 help_key;
	uint16 help_index_key;
	uint16 help_extended_key;
	uint16 window_activate;

} RDP_ACTIVATE_CAPS;

#define RDP_CAPSET_POINTER    8
#define RDP_CAPLEN_POINTER    0x08

typedef struct _RDP_POINTER_CAPS
{
	uint16 colour_pointer;
	uint16 cache_size;

} RDP_POINTER_CAPS;

#define RDP_CAPSET_SHARE       9
#define RDP_CAPLEN_SHARE       0x08

typedef struct _RDP_SHARE_CAPS
{
	uint16 userid;
	uint16 pad;

} RDP_SHARE_CAPS;

#define RDP_CAPSET_COLCACHE    10
#define RDP_CAPLEN_COLCACHE    0x08

typedef struct _RDP_COLCACHE_CAPS
{
	uint16 cache_size;
	uint16 pad;

} RDP_COLCACHE_CAPS;

#define RDP_CAPSET_UNKNOWN     13
#define RDP_CAPLEN_UNKNOWN     0x9C

#define RDP_SOURCE  "MSTSC"

typedef struct _RDP_ACTIVE_PDU
{
	uint32 shareid;
	uint16 userid; // RDP_PDU_CONFIRM_ACTIVE only
	uint16 source_len;
	uint16 caps_len;
	uint8 source[48];
	uint16 num_caps;
	uint16 pad;

	RDP_GENERAL_CAPS  general_caps;
	RDP_BITMAP_CAPS   bitmap_caps;
	RDP_ORDER_CAPS    order_caps;
	RDP_BMPCACHE_CAPS bmpcache_caps;
	RDP_ACTIVATE_CAPS activate_caps;
	RDP_CONTROL_CAPS  control_caps;
	RDP_POINTER_CAPS  pointer_caps;
	RDP_SHARE_CAPS    share_caps;
	RDP_COLCACHE_CAPS colcache_caps;

} RDP_ACTIVE_PDU;

typedef struct _RDP_SYNCHRONIZE_PDU
{
	uint16 type;  // 1
	uint16 userid;

} RDP_SYNCHRONIZE_PDU;

#define RDP_CTL_REQUEST_CONTROL  1
#define RDP_CTL_GRANT_CONTROL    2
#define RDP_CTL_DETACH           3
#define RDP_CTL_COOPERATE        4

typedef struct _RDP_CONTROL_PDU
{
	uint16 action;  // see above
	uint16 userid;
	uint32 controlid;

} RDP_CONTROL_PDU;

#define RDP_INPUT_SYNCHRONIZE   0
#define RDP_INPUT_CODEPOINT     1
#define RDP_INPUT_VIRTKEY       2
#define RDP_INPUT_UNKNOWN       4
#define RDP_INPUT_MOUSE         0x8001

#define KBD_FLAG_RIGHT          0x0001
#define KBD_FLAG_QUIET          0x1000
#define KBD_FLAG_DOWN           0x4000
#define KBD_FLAG_RELEASE        0x8000

#define MOUSE_FLAG_MOVE         0x0800
#define MOUSE_FLAG_BUTTON1      0x1000
#define MOUSE_FLAG_BUTTON2      0x2000
#define MOUSE_FLAG_BUTTON3      0x4000
#define MOUSE_FLAG_DOWN         0x8000

#define RDP_MAX_EVENTS 50

typedef struct _RDP_INPUT_EVENT
{
	uint32 event_time;
	uint16 message_type;
	uint16 device_flags;

	uint16 kbd_keycode;

	uint16 mouse_x;
	uint16 mouse_y;

} RDP_INPUT_EVENT;

typedef struct _RDP_INPUT_PDU
{
	uint16 num_events;
	uint16 pad;
	RDP_INPUT_EVENT event[RDP_MAX_EVENTS];

} RDP_INPUT_PDU;

#define RDP_FONT_INFO_SIZE  0x32
#define RDP_MAX_FONTS       100

typedef struct _RDP_FONT_INFO
{
	uint8 name[32];
	uint16 flags;
	uint16 width;
	uint16 height;
	uint16 xaspect;
	uint16 yaspect;
	uint32 signature;
	uint16 codepage;
	uint16 ascent;

} RDP_FONT_INFO;

typedef struct _RDP_FONT_PDU
{
	uint16 num_fonts;
	uint16 unknown1; // 0x3e
	uint16 unknown2; // series number?
	uint16 entry_size;
	RDP_FONT_INFO font[RDP_MAX_FONTS];

} RDP_FONT_PDU;

#define RDP_UPDATE_ORDERS	0
#define RDP_UPDATE_PALETTE	2
#define RDP_UPDATE_SYNCHRONIZE	3

typedef struct _OPAQUE_RECT_ORDER
{
	uint16 x;
	uint16 y;
	uint16 cx;
	uint16 cy;
	uint8 colour;

} OPAQUE_RECT_ORDER;

typedef struct _MEMBLT_ORDER
{
	uint8 cache_id;
	uint16 x;
	uint16 y;
	uint16 cx;
	uint16 cy;
	uint8 opcode;
	uint16 cache_idx;

} MEMBLT_ORDER;

typedef struct _RDP_ORDER_STATE
{
	uint8 order_type;

	OPAQUE_RECT_ORDER opaque_rect;
	MEMBLT_ORDER memblt;

} RDP_ORDER_STATE;

typedef struct _RDP_UPDATE_PDU
{
	uint16 update_type;
	uint16 pad;

} RDP_UPDATE_PDU;

#define RDP_ORDER_STANDARD  1
#define RDP_ORDER_SECONDARY 2
#define RDP_ORDER_BOUNDS    4
#define RDP_ORDER_CHANGE    8
#define RDP_ORDER_DELTA     16
#define RDP_ORDER_REPEAT    64

enum RDP_ORDER_TYPE
{
        RDP_ORDER_OPAQUE_RECT = 10,
	RDP_ORDER_MEMBLT = 13
};

enum RDP_SECONDARY_ORDER_TYPE
{
	RDP_ORDER_BMPCACHE = 2
};

typedef struct _RDP_SECONDARY_ORDER
{
	uint16 length;
	uint16 flags;
	uint8 type;

} RDP_SECONDARY_ORDER;

typedef struct _RDP_BITMAP_HEADER
{
	uint8 cache_id;
	uint8 pad1;
	uint8 width;
	uint8 height;
	uint8 bpp;
	uint16 bufsize;
	uint16 cache_idx;
	uint16 pad2;
	uint16 size;
	uint16 row_size;
	uint16 final_size;

} RDP_BITMAP_HEADER;

#define RDP_POINTER_MOVE	3

typedef struct _RDP_POINTER
{
	uint16 message;
	uint16 pad;
	uint16 x;
	uint16 y;

} RDP_POINTER;

