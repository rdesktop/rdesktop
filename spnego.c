/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Minimal SPNEGO helpers for CredSSP NTLM.
   Copyright 2026 Marco Fortina <marco_fortina@hotmail.it>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include "rdesktop.h"
#include "spnego.h"

static const uint8 spnego_oid[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02 };
static const uint8 ntlmssp_oid[] = { 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a };
static const uint8 ntlmssp_sig[] = { 'N', 'T', 'L', 'M', 'S', 'S', 'P', 0 };

static size_t
spnego_len_size(size_t len)
{
	if (len < 0x80)
		return 1;
	if (len <= 0xff)
		return 2;
	if (len <= 0xffff)
		return 3;
	return 4;
}

static void
spnego_out_len(STREAM s, size_t len)
{
	if (len < 0x80)
	{
		out_uint8(s, len);
	}
	else if (len <= 0xff)
	{
		out_uint8(s, 0x81);
		out_uint8(s, len);
	}
	else if (len <= 0xffff)
	{
		out_uint8(s, 0x82);
		out_uint16_be(s, len);
	}
	else
	{
		out_uint8(s, 0x83);
		out_uint8(s, (len >> 16) & 0xff);
		out_uint16_be(s, len & 0xffff);
	}
}

static size_t
spnego_tlv_size(size_t len)
{
	return 1 + spnego_len_size(len) + len;
}

static void
spnego_out_tlv_header(STREAM s, uint8 tag, size_t len)
{
	out_uint8(s, tag);
	spnego_out_len(s, len);
}

STREAM
spnego_wrap_neg_token_init(STREAM mech_token)
{
	STREAM out;
	size_t mech_len;
	size_t spnego_oid_tlv, ntlm_oid_tlv;
	size_t mech_type_list_content, mech_type_list_tlv;
	size_t mech_types_field_content, mech_types_field_tlv;
	size_t mech_token_octet_tlv, mech_token_field_tlv;
	size_t neg_init_content, neg_init_seq_tlv;
	size_t neg_init_choice_content, neg_init_choice_tlv;
	size_t initial_content;

	mech_len = s_length(mech_token);
	spnego_oid_tlv = spnego_tlv_size(sizeof(spnego_oid));
	ntlm_oid_tlv = spnego_tlv_size(sizeof(ntlmssp_oid));
	mech_type_list_content = ntlm_oid_tlv;
	mech_type_list_tlv = spnego_tlv_size(mech_type_list_content);
	mech_types_field_content = mech_type_list_tlv;
	mech_types_field_tlv = spnego_tlv_size(mech_types_field_content);
	mech_token_octet_tlv = spnego_tlv_size(mech_len);
	mech_token_field_tlv = spnego_tlv_size(mech_token_octet_tlv);
	neg_init_content = mech_types_field_tlv + mech_token_field_tlv;
	neg_init_seq_tlv = spnego_tlv_size(neg_init_content);
	neg_init_choice_content = neg_init_seq_tlv;
	neg_init_choice_tlv = spnego_tlv_size(neg_init_choice_content);
	initial_content = spnego_oid_tlv + neg_init_choice_tlv;

	out = s_alloc(spnego_tlv_size(initial_content));
	spnego_out_tlv_header(out, 0x60, initial_content);
	spnego_out_tlv_header(out, 0x06, sizeof(spnego_oid));
	out_uint8a(out, spnego_oid, sizeof(spnego_oid));

	spnego_out_tlv_header(out, 0xa0, neg_init_choice_content);
	spnego_out_tlv_header(out, 0x30, neg_init_content);

	spnego_out_tlv_header(out, 0xa0, mech_types_field_content);
	spnego_out_tlv_header(out, 0x30, mech_type_list_content);
	spnego_out_tlv_header(out, 0x06, sizeof(ntlmssp_oid));
	out_uint8a(out, ntlmssp_oid, sizeof(ntlmssp_oid));

	spnego_out_tlv_header(out, 0xa2, mech_token_octet_tlv);
	spnego_out_tlv_header(out, 0x04, mech_len);
	out_stream(out, mech_token);

	s_mark_end(out);
	s_seek(out, 0);
	return out;
}

STREAM
spnego_wrap_neg_token_resp(STREAM mech_token)
{
	STREAM out;
	size_t mech_len;
	size_t token_octet_tlv, response_field_tlv;
	size_t response_seq_content, response_seq_tlv;
	size_t choice_content;

	mech_len = s_length(mech_token);
	token_octet_tlv = spnego_tlv_size(mech_len);
	response_field_tlv = spnego_tlv_size(token_octet_tlv);
	response_seq_content = response_field_tlv;
	response_seq_tlv = spnego_tlv_size(response_seq_content);
	choice_content = response_seq_tlv;

	out = s_alloc(spnego_tlv_size(choice_content));
	spnego_out_tlv_header(out, 0xa1, choice_content);
	spnego_out_tlv_header(out, 0x30, response_seq_content);
	spnego_out_tlv_header(out, 0xa2, token_octet_tlv);
	spnego_out_tlv_header(out, 0x04, mech_len);
	out_stream(out, mech_token);
	s_mark_end(out);
	s_seek(out, 0);
	return out;
}

static STREAM
spnego_copy_octet_ntlm_token(STREAM s, int length)
{
	STREAM out;

	if (length < (int) sizeof(ntlmssp_sig) || !s_check_rem(s, length))
		return NULL;
	if (memcmp(s->p, ntlmssp_sig, sizeof(ntlmssp_sig)) != 0)
		return NULL;

	out = s_alloc(length);
	out_uint8a(out, s->p, length);
	s_mark_end(out);
	s_seek(out, 0);
	return out;
}

static STREAM
spnego_find_ntlm_token(STREAM s, unsigned depth)
{
	STREAM out;
	struct stream field;
	int tagval, length;

	if (depth > 16)
		return NULL;

	while (s_check_rem(s, 2))
	{
		if (!ber_in_header(s, &tagval, &length) || !s_check_rem(s, length))
			return NULL;

		field = *s;
		field.end = field.p + length;
		field.size = length;
		s->p += length;

		if (tagval == BER_TAG_OCTET_STRING)
		{
			out = spnego_copy_octet_ntlm_token(&field, length);
			if (out)
				return out;
		}

		if ((tagval & BER_TAG_CONSTRUCTED) || tagval == 0x60 ||
		    (tagval & BER_TAG_CTXT_SPECIFIC))
		{
			out = spnego_find_ntlm_token(&field, depth + 1);
			if (out)
				return out;
		}
	}

	return NULL;
}

STREAM
spnego_extract_ntlm_token(STREAM token)
{
	struct stream cursor;
	STREAM out;
	size_t i, len;
	uint8 *p;

	if (!token)
		return NULL;

	cursor = *token;
	out = spnego_find_ntlm_token(&cursor, 0);
	if (out)
	{
		logger(Core, Debug, "spnego_extract_ntlm_token(), parsed NTLM token len=%u",
		       (unsigned) s_length(out));
		return out;
	}

	/* Compatibility fallback for raw NTLMSSP tokens. */
	len = s_length(token);
	p = token->data;
	for (i = 0; i + sizeof(ntlmssp_sig) <= len; i++)
	{
		if (memcmp(p + i, ntlmssp_sig, sizeof(ntlmssp_sig)) == 0)
		{
			out = s_alloc(len - i);
			out_uint8a(out, p + i, len - i);
			s_mark_end(out);
			s_seek(out, 0);
			logger(Core, Debug,
			       "spnego_extract_ntlm_token(), fallback NTLM token len=%u",
			       (unsigned) s_length(out));
			return out;
		}
	}

	return NULL;
}
