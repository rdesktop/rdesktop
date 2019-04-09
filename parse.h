/*
   rdesktop: A Remote Desktop Protocol client.
   Parsing primitives
   Copyright (C) Matthew Chapman 1999-2008
   Copyright 2012-2017 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

#ifndef _PARSE_H
#define _PARSE_H

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

#define s_push_layer(s,h,n)	{ (s)->h = (s)->p; (s)->p += n; }
#define s_pop_layer(s,h)	(s)->p = (s)->h;
#define s_mark_end(s)		(s)->end = (s)->p;
/* Return current read offset in the STREAM */
#define s_tell(s)		(size_t)((s)->p - (s)->data)
/* Set current read offset in the STREAM */
#define s_seek(s,o)		(s)->p = (s)->data; s_assert_r(s,o); (s)->p += o;
/* Returns number of bytes that can still be read from STREAM */
#define s_remaining(s)		(size_t)((s)->end - (s)->p)
#define s_check_rem(s,n)	(((s)->p <= (s)->end) && ((size_t)n <= s_remaining(s)))
#define s_check_end(s)		((s)->p == (s)->end)
#define s_length(s)		((s)->end - (s)->data)
#define s_left(s)		((s)->size - (size_t)((s)->p - (s)->data))

/* Verify that there is enough data/space before accessing a STREAM */
#define s_assert_r(s,n)		{ if (!s_check_rem(s, n)) rdp_protocol_error( "unexpected stream overrun", s); }
#define s_assert_w(s,n)		{ if (s_left(s) < (size_t)n) { error("%s:%d: %s(), %s", __FILE__, __LINE__, __func__, "unexpected stream overrun"); exit(0); } }

#if defined(L_ENDIAN) && !defined(NEED_ALIGN)
#define in_uint16_le(s,v)	{ s_assert_r(s, 2); v = *(uint16 *)((s)->p); (s)->p += 2; }
#define in_uint32_le(s,v)	{ s_assert_r(s, 4); v = *(uint32 *)((s)->p); (s)->p += 4; }
#define out_uint16_le(s,v)	{ s_assert_w(s, 2); *(uint16 *)((s)->p) = v; (s)->p += 2; }
#define out_uint32_le(s,v)	{ s_assert_w(s, 4); *(uint32 *)((s)->p) = v; (s)->p += 4; }

#else
#define in_uint16_le(s,v)	{ s_assert_r(s, 2); v = *((s)->p++); v += *((s)->p++) << 8; }
#define in_uint32_le(s,v)	{ s_assert_r(s, 4); in_uint16_le(s,v) \
				v += *((s)->p++) << 16; v += *((s)->p++) << 24; }
#define out_uint16_le(s,v)	{ s_assert_w(s, 2); *((s)->p++) = (v) & 0xff; *((s)->p++) = ((v) >> 8) & 0xff; }
#define out_uint32_le(s,v)	{ s_assert_w(s, 4); out_uint16_le(s, (v) & 0xffff); out_uint16_le(s, ((v) >> 16) & 0xffff); }
#endif

#if defined(B_ENDIAN) && !defined(NEED_ALIGN)
#define in_uint16_be(s,v)	{ s_assert_r(s, 2); v = *(uint16 *)((s)->p); (s)->p += 2; }
#define in_uint32_be(s,v)	{ s_assert_r(s, 4); v = *(uint32 *)((s)->p); (s)->p += 4; }
#define out_uint16_be(s,v)	{ s_assert_w(s, 2); *(uint16 *)((s)->p) = v; (s)->p += 2; }
#define out_uint32_be(s,v)	{ s_assert_w(s, 4); *(uint32 *)((s)->p) = v; (s)->p += 4; }

#define B_ENDIAN_PREFERRED
#define in_uint16(s,v)		in_uint16_be(s,v)
#define in_uint32(s,v)		in_uint32_be(s,v)
#define out_uint16(s,v)		out_uint16_be(s,v)
#define out_uint32(s,v)		out_uint32_be(s,v)

#else
#define in_uint16_be(s,v)	{ s_assert_r(s, 2); v = *((s)->p++); next_be(s,v); }
#define in_uint32_be(s,v)	{ s_assert_r(s, 4); in_uint16_be(s,v); next_be(s,v); next_be(s,v); }
#define out_uint16_be(s,v)	{ s_assert_w(s, 2); *((s)->p++) = ((v) >> 8) & 0xff; *((s)->p++) = (v) & 0xff; }
#define out_uint32_be(s,v)	{ s_assert_w(s, 4); out_uint16_be(s, ((v) >> 16) & 0xffff); out_uint16_be(s, (v) & 0xffff); }
#endif

#ifndef B_ENDIAN_PREFERRED
#define in_uint16(s,v)		in_uint16_le(s,v)
#define in_uint32(s,v)		in_uint32_le(s,v)
#define out_uint16(s,v)		out_uint16_le(s,v)
#define out_uint32(s,v)		out_uint32_le(s,v)
#endif

#define in_uint8(s,v)		{ s_assert_r(s, 1); v = *((s)->p++); }
/* Return a pointer in v to manually read n bytes from STREAM s */
#define in_uint8p(s,v,n)	{ s_assert_r(s, n); v = (s)->p; (s)->p += n; }
/* Copy n bytes from STREAM s in to array v */
#define in_uint8a(s,v,n)	{ s_assert_r(s, n); memcpy(v,(s)->p,n); (s)->p += n; }
#define in_uint8s(s,n)		{ s_assert_r(s, n); (s)->p += n; }
#define out_uint8(s,v)		{ s_assert_w(s, 1); *((s)->p++) = v; }
/* Return a pointer in v to manually fill in n bytes in STREAM s */
#define out_uint8p(s,v,n)	{ s_assert_w(s, n); v = (s)->p; (s)->p += n; }
/* Copy n bytes from array v in to STREAM s */
#define out_uint8a(s,v,n)	{ s_assert_w(s, n); memcpy((s)->p,v,n); (s)->p += n; }
#define out_uint8s(s,n)		{ s_assert_w(s, n); memset((s)->p,0,n); (s)->p += n; }

/* Copy n bytes from STREAM s in to STREAM v */
#define in_uint8stream(s,v,n)	{ s_assert_r(s, n); out_uint8a((v), (s)->p, n); (s)->p += n; }
/* Copy n bytes in to STREAM s from STREAM v */
#define out_uint8stream(s,v,n)	in_uint8stream(v,s,n)

/* Copy the entire STREAM v (ignoring offsets) in to STREAM s */
#define out_stream(s, v)	out_uint8a(s, (v)->data, s_length((v)))

/* Return a pointer in v to manually modify n bytes of STREAM s in place */
#define inout_uint8p(s,v,n)	{ s_assert_r(s, n); s_assert_w(s, n); v = (s)->p; (s)->p += n; }

#define next_be(s,v)		{ s_assert_r(s, 1); v = ((v) << 8) + *((s)->p++); }


#endif /* _PARSE_H */
