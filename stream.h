/*
   rdesktop: A Remote Desktop Protocol client.
   Parsing primitives
   Copyright (C) Matthew Chapman 1999-2008
   Copyright 2012-2017 Henrik Andersson <hean01@cendio.se> for Cendio AB
   Copyright 2017 Alexander Zakharov <uglym8@gmail.com>
   Copyright 2019 Karl Mikaelsson <derfian@cendio.se> for Cendio AB

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

#ifndef _STREAM_H
#define _STREAM_H

/* Parser state */
typedef struct stream
{
	unsigned char *p;
	unsigned char *end;
	unsigned char *data;
	unsigned int size;

	/* Offsets of various headers */
	unsigned char *iso_hdr;
	unsigned char *mcs_hdr;
	unsigned char *sec_hdr;
	unsigned char *rdp_hdr;
	unsigned char *channel_hdr;

}
 *STREAM;

/* Return a newly allocated STREAM object of the specified size */
STREAM s_alloc(unsigned int size);
/* Wrap an existing buffer in a STREAM object, transferring ownership */
STREAM s_inherit(unsigned char *data, unsigned int size);
/* Resize an existing STREAM object, keeping all data and offsets intact */
void s_realloc(STREAM s, unsigned int size);
/* Free STREAM object and its associated buffer */
void s_free(STREAM s);
/* Reset all internal offsets, but keep the allocated size */
void s_reset(STREAM s);

void out_utf16s(STREAM s, const char *string);
void out_utf16s_padded(STREAM s, const char *string, size_t width, unsigned char pad);
void out_utf16s_no_eos(STREAM s, const char *string);

size_t in_ansi_string(STREAM s, char *string, size_t len);


/* Store current offset as header h and skip n bytes */
#define s_push_layer(s,h,n)	{ (s)->h = (s)->p; (s)->p += n; }
/* Set header h as current offset */
#define s_pop_layer(s,h)	(s)->p = (s)->h;
/* Mark current offset as end of readable data */
#define s_mark_end(s)		(s)->end = (s)->p;
/* Return current read offset in the STREAM */
#define s_tell(s)		(size_t)((s)->p - (s)->data)
/* Set current read offset in the STREAM */
#define s_seek(s,o)		(s)->p = (s)->data; s_assert_r(s,o); (s)->p += o;
/* Returns number of bytes that can still be read from STREAM */
#define s_remaining(s)		(size_t)((s)->end - (s)->p)
/* True if at least n bytes can still be read */
#define s_check_rem(s,n)	(((s)->p <= (s)->end) && ((size_t)n <= s_remaining(s)))
/* True if all data has been read */
#define s_check_end(s)		((s)->p == (s)->end)
/* Return the total number of bytes that can be read */
#define s_length(s)		((s)->end - (s)->data)
/* Return the number of bytes that can still be written */
#define s_left(s)		((s)->size - (size_t)((s)->p - (s)->data))

/* Verify that there is enough data/space before accessing a STREAM */
#define s_assert_r(s,n)		{ if (!s_check_rem(s, n)) rdp_protocol_error( "unexpected stream overrun", s); }
#define s_assert_w(s,n)		{ if (s_left(s) < (size_t)n) { logger(Core, Error, "%s:%d: %s(), %s", __FILE__, __LINE__, __func__, "unexpected stream overrun"); exit(0); } }

/* Read/write an unsigned integer in little-endian order */
#if defined(L_ENDIAN) && !defined(NEED_ALIGN)
#define in_uint16_le(s,v)	{ s_assert_r(s, 2); v = *(uint16 *)((s)->p); (s)->p += 2; }
#define in_uint32_le(s,v)	{ s_assert_r(s, 4); v = *(uint32 *)((s)->p); (s)->p += 4; }
#define in_uint64_le(s,v)	{ s_assert_r(s, 8); v = *(uint64 *)((s)->p); (s)->p += 8; }
#define out_uint16_le(s,v)	{ s_assert_w(s, 2); *(uint16 *)((s)->p) = v; (s)->p += 2; }
#define out_uint32_le(s,v)	{ s_assert_w(s, 4); *(uint32 *)((s)->p) = v; (s)->p += 4; }
#define out_uint64_le(s,v)	{ s_assert_w(s, 8); *(uint64 *)((s)->p) = v; (s)->p += 8; }
#else
#define in_uint16_le(s,v)	{ s_assert_r(s, 2); v = *((s)->p++); v += *((s)->p++) << 8; }
#define in_uint32_le(s,v)	{ s_assert_r(s, 4); in_uint16_le(s,v) \
				v += *((s)->p++) << 16; v += *((s)->p++) << 24; }
#define in_uint64_le(s,v)	{ s_assert_r(s, 8); in_uint32_le(s,v) \
				v += *((s)->p++) << 32; v += *((s)->p++) << 40; \
				v += *((s)->p++) << 48; v += *((s)->p++) << 56; }
#define out_uint16_le(s,v)	{ s_assert_w(s, 2); *((s)->p++) = (v) & 0xff; *((s)->p++) = ((v) >> 8) & 0xff; }
#define out_uint32_le(s,v)	{ s_assert_w(s, 4); out_uint16_le(s, (v) & 0xffff); out_uint16_le(s, ((v) >> 16) & 0xffff); }
#define out_uint64_le(s,v)	{ s_assert_w(s, 8); out_uint32_le(s, (v) & 0xffffffff); out_uint32_le(s, ((v) >> 32) & 0xffffffff); }
#endif


/* Read/write an unsigned integer in big-endian order */
#if defined(B_ENDIAN) && !defined(NEED_ALIGN)
#define in_uint16_be(s,v)	{ s_assert_r(s, 2); v = *(uint16 *)((s)->p); (s)->p += 2; }
#define in_uint32_be(s,v)	{ s_assert_r(s, 4); v = *(uint32 *)((s)->p); (s)->p += 4; }
#define in_uint64_be(s,v)	{ s_assert_r(s, 8); v = *(uint64 *)((s)->p); (s)->p += 8; }
#define out_uint16_be(s,v)	{ s_assert_w(s, 2); *(uint16 *)((s)->p) = v; (s)->p += 2; }
#define out_uint32_be(s,v)	{ s_assert_w(s, 4); *(uint32 *)((s)->p) = v; (s)->p += 4; }
#define out_uint64_be(s,v)	{ s_assert_w(s, 8); *(uint64 *)((s)->p) = v; (s)->p += 8; }

#define B_ENDIAN_PREFERRED
#define in_uint16(s,v)		in_uint16_be(s,v)
#define in_uint32(s,v)		in_uint32_be(s,v)
#define in_uint64(s,v)		in_uint64_be(s,v)

#define out_uint16(s,v)		out_uint16_be(s,v)
#define out_uint32(s,v)		out_uint32_be(s,v)
#define out_uint64(s,v)		out_uint64_be(s,v)

#else
#define in_uint16_be(s,v)	{ s_assert_r(s, 2); v = *((s)->p++); next_be(s,v); }
#define in_uint32_be(s,v)	{ s_assert_r(s, 4); in_uint16_be(s,v); next_be(s,v); next_be(s,v); }
#define in_uint64_be(s,v)	{ s_assert_r(s, 8); in_uint32_be(s,v); next_be(s,v); next_be(s,v); next_be(s,v); next_be(s,v); }
#define out_uint16_be(s,v)	{ s_assert_w(s, 2); *((s)->p++) = ((v) >> 8) & 0xff; *((s)->p++) = (v) & 0xff; }
#define out_uint32_be(s,v)	{ s_assert_w(s, 4); out_uint16_be(s, ((v) >> 16) & 0xffff); out_uint16_be(s, (v) & 0xffff); }
#define out_uint64_be(s,v)	{ s_assert_w(s, 8); out_uint32_be(s, ((v) >> 32) & 0xffffffff); out_uint32_be(s, (v) & 0xffffffff); }
#endif

#ifndef B_ENDIAN_PREFERRED
#define in_uint16(s,v)		in_uint16_le(s,v)
#define in_uint32(s,v)		in_uint32_le(s,v)
#define in_uint64(s,v)		in_uint64_le(s,v)
#define out_uint16(s,v)		out_uint16_le(s,v)
#define out_uint32(s,v)		out_uint32_le(s,v)
#define out_uint64(s,v)		out_uint64_le(s,v)
#endif

/* Read a single unsigned byte in v from STREAM s */
#define in_uint8(s,v)		{ s_assert_r(s, 1); v = *((s)->p++); }
/* Return a pointer in v to manually read n bytes from STREAM s */
#define in_uint8p(s,v,n)	{ s_assert_r(s, n); v = (s)->p; (s)->p += n; }
/* Copy n bytes from STREAM s in to array v */
#define in_uint8a(s,v,n)	{ s_assert_r(s, n); memcpy(v,(s)->p,n); (s)->p += n; }
/* Skip reading n bytes in STREAM s */
#define in_uint8s(s,n)		{ s_assert_r(s, n); (s)->p += n; }
/* Write a single unsigned byte from v to STREAM s */
#define out_uint8(s,v)		{ s_assert_w(s, 1); *((s)->p++) = v; }
/* Return a pointer in v to manually fill in n bytes in STREAM s */
#define out_uint8p(s,v,n)	{ s_assert_w(s, n); v = (s)->p; (s)->p += n; }
/* Copy n bytes from array v in to STREAM s */
#define out_uint8a(s,v,n)	{ s_assert_w(s, n); memcpy((s)->p,v,n); (s)->p += n; }
/* Fill n bytes with 0:s in STREAM s */
#define out_uint8s(s,n)		{ s_assert_w(s, n); memset((s)->p,0,n); (s)->p += n; }

/* Copy n bytes from STREAM s in to STREAM v */
#define in_uint8stream(s,v,n)	{ s_assert_r(s, n); out_uint8a((v), (s)->p, n); (s)->p += n; }
/* Copy n bytes in to STREAM s from STREAM v */
#define out_uint8stream(s,v,n)	in_uint8stream(v,s,n)
/* Copy the entire STREAM v (ignoring offsets) in to STREAM s */
#define out_stream(s, v)	out_uint8a(s, (v)->data, s_length((v)))

/* Return a pointer in v to manually modify n bytes of STREAM s in place */
#define inout_uint8p(s,v,n)	{ s_assert_r(s, n); s_assert_w(s, n); v = (s)->p; (s)->p += n; }

/* Read one more byte of an unsigned big-endian integer */
#define next_be(s,v)		{ s_assert_r(s, 1); v = ((v) << 8) + *((s)->p++); }


#endif /* _STREAM_H */
