/*
   rdesktop: A Remote Desktop Protocol client.
   Common data types
   Copyright (C) Matthew Chapman 1999-2002
   
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

typedef int BOOL;

#ifndef True
#define True  (1)
#define False (0)
#endif

typedef unsigned char uint8;
typedef signed char sint8;
typedef unsigned short uint16;
typedef signed short sint16;
typedef unsigned int uint32;
typedef signed int sint32;

typedef void *HBITMAP;
typedef void *HGLYPH;
typedef void *HCOLOURMAP;
typedef void *HCURSOR;

typedef struct _COLOURENTRY
{
	uint8 red;
	uint8 green;
	uint8 blue;

}
COLOURENTRY;

typedef struct _COLOURMAP
{
	uint16 ncolours;
	COLOURENTRY *colours;

}
COLOURMAP;

typedef struct _BOUNDS
{
	sint16 left;
	sint16 top;
	sint16 right;
	sint16 bottom;

}
BOUNDS;

typedef struct _PEN
{
	uint8 style;
	uint8 width;
	uint32 colour;

}
PEN;

typedef struct _BRUSH
{
	uint8 xorigin;
	uint8 yorigin;
	uint8 style;
	uint8 pattern[8];

}
BRUSH;

typedef struct _FONTGLYPH
{
	sint16 offset;
	sint16 baseline;
	uint16 width;
	uint16 height;
	HBITMAP pixmap;

}
FONTGLYPH;

typedef struct _DATABLOB
{
	void *data;
	int size;

}
DATABLOB;

typedef struct _key_translation
{
	uint8 scancode;
	uint16 modifiers;
}
key_translation;

typedef struct _VCHANNEL
{
	uint16 mcs_id;
	char name[8];
	uint32 flags;
	struct stream in;
	void (*process) (STREAM);
}
VCHANNEL;

#define MAX_CBSIZE 256

/* RDPSND */
typedef struct
{
	uint16 wFormatTag;
	uint16 nChannels;
	uint32 nSamplesPerSec;
	uint32 nAvgBytesPerSec;
	uint16 nBlockAlign;
	uint16 wBitsPerSample;
	uint16 cbSize;
	uint8 cb[MAX_CBSIZE];
} WAVEFORMATEX;

/* RDPDR */
typedef uint32 NTSTATUS;
typedef uint32 HANDLE;

typedef struct _DEVICE_FNS
{
        NTSTATUS (*create)(uint32 device, uint32 desired_access, uint32 share_mode, uint32 create_disposition, uint32 flags_and_attributes, char *filename, HANDLE *handle);
        NTSTATUS (*close)(HANDLE handle);
        NTSTATUS (*read)(HANDLE handle, uint8 *data, uint32 length, uint32 offset, uint32 *result);
        NTSTATUS (*write)(HANDLE handle, uint8 *data, uint32 length, uint32 offset, uint32 *result);
        NTSTATUS (*device_control)(HANDLE handle, uint32 request, STREAM in, STREAM out);
}
DEVICE_FNS;


typedef struct rdpdr_device_info
{
        uint32  device_type;
        HANDLE  handle;
        char    name[8];
        char    *local_path;
        void    *pdevice_data;
}
RDPDR_DEVICE;

typedef struct rdpdr_serial_device_info
{
        int             dtr;
        uint32          baud_rate,
                        queue_in_size,
                        queue_out_size,
                        wait_mask,
                        read_interval_timeout,
                        read_total_timeout_multiplier,
                        read_total_timeout_constant,
                        write_total_timeout_multiplier,
                        write_total_timeout_constant,
                        posix_wait_mask;
        uint8           stop_bits,
                        parity,
                        word_length;
        struct termios  *ptermios,
                        *pold_termios;
}
SERIAL_DEVICE;

typedef struct rdpdr_parallel_device_info
{
        char    *driver,
                *printer;
        uint32  queue_in_size,
                queue_out_size,
                wait_mask,
                read_interval_timeout,
                read_total_timeout_multiplier,
                read_total_timeout_constant,
                write_total_timeout_multiplier,
                write_total_timeout_constant,
                posix_wait_mask,
                bloblen;
        uint8   *blob;
}
PARALLEL_DEVICE;

typedef struct rdpdr_printer_info
{
        FILE    *printer_fp;
        char    *driver,
                *printer;
        uint32  bloblen;
        uint8   *blob;
        BOOL    default_printer;
}
PRINTER;


