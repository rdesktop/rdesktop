/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   CredSSP layer and kerberos support.
   Copyright 2012-2013 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

#include <gssapi/gssapi.h>
#include "rdesktop.h"

extern RD_BOOL g_use_password_as_pin;

extern char *g_sc_csp_name;
extern char *g_sc_reader_name;
extern char *g_sc_card_name;
extern char *g_sc_container_name;

static gss_OID_desc _gss_spnego_krb5_mechanism_oid_desc =
	{ 9, (void *) "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02" };


static void
s_realloc(STREAM s, unsigned int size)
{
	unsigned char *data;

	if (s->size >= size)
		return;

	data = s->data;
	s->size = size;
	s->data = xrealloc(data, size);
	s->p = s->data + (s->p - data);
	s->end = s->data + (s->end - data);
	s->iso_hdr = s->data + (s->iso_hdr - data);
	s->mcs_hdr = s->data + (s->mcs_hdr - data);
	s->sec_hdr = s->data + (s->sec_hdr - data);
	s->rdp_hdr = s->data + (s->rdp_hdr - data);
	s->channel_hdr = s->data + (s->channel_hdr - data);
}

static void
s_free(STREAM s)
{
	free(s->data);
	free(s);
}

static STREAM
ber_wrap_hdr_data(int tagval, STREAM in)
{
	STREAM out;
	int size = s_length(in) + 16;

	out = xmalloc(sizeof(struct stream));
	memset(out, 0, sizeof(struct stream));
	out->data = xmalloc(size);
	out->size = size;
	out->p = out->data;

	ber_out_header(out, tagval, s_length(in));
	out_uint8p(out, in->data, s_length(in));
	s_mark_end(out);

	return out;
}


static void
cssp_gss_report_error(OM_uint32 code, char *str, OM_uint32 major_status, OM_uint32 minor_status)
{
	OM_uint32 msgctx = 0, ms;
	gss_buffer_desc status_string;

	error("GSS error [%d:%d:%d]: %s\n", (major_status & 0xff000000) >> 24,	// Calling error
	      (major_status & 0xff0000) >> 16,	// Routine error
	      major_status & 0xffff,	// Supplementary info bits
	      str);

	do
	{
		ms = gss_display_status(&minor_status, major_status,
					code, GSS_C_NULL_OID, &msgctx, &status_string);
		if (ms != GSS_S_COMPLETE)
			continue;

		error(" - %s\n", status_string.value);

	}
	while (ms == GSS_S_COMPLETE && msgctx);

}


static RD_BOOL
cssp_gss_mech_available(gss_OID mech)
{
	int mech_found;
	OM_uint32 major_status, minor_status;
	gss_OID_set mech_set;

	mech_found = 0;

	if (mech == GSS_C_NO_OID)
		return True;

	major_status = gss_indicate_mechs(&minor_status, &mech_set);
	if (!mech_set)
		return False;

	if (GSS_ERROR(major_status))
	{
		cssp_gss_report_error(GSS_C_GSS_CODE, "Failed to get available mechs on system",
				      major_status, minor_status);
		return False;
	}

	gss_test_oid_set_member(&minor_status, mech, mech_set, &mech_found);

	if (GSS_ERROR(major_status))
	{
		cssp_gss_report_error(GSS_C_GSS_CODE, "Failed to match mechanism in set",
				      major_status, minor_status);
		return False;
	}

	if (!mech_found)
		return False;

	return True;
}

static RD_BOOL
cssp_gss_get_service_name(char *server, gss_name_t * name)
{
	gss_buffer_desc output;
	OM_uint32 major_status, minor_status;

	const char service_name[] = "TERMSRV";

	gss_OID type = (gss_OID) GSS_C_NT_HOSTBASED_SERVICE;
	int size = (strlen(service_name) + 1 + strlen(server) + 1);

	output.value = malloc(size);
	snprintf(output.value, size, "%s@%s", service_name, server);
	output.length = strlen(output.value) + 1;

	major_status = gss_import_name(&minor_status, &output, type, name);

	if (GSS_ERROR(major_status))
	{
		cssp_gss_report_error(GSS_C_GSS_CODE, "Failed to create service principal name",
				      major_status, minor_status);
		return False;
	}

	gss_release_buffer(&minor_status, &output);

	return True;

}

static RD_BOOL
cssp_gss_wrap(gss_ctx_id_t * ctx, STREAM in, STREAM out)
{
	int conf_state;
	OM_uint32 major_status;
	OM_uint32 minor_status;
	gss_buffer_desc inbuf, outbuf;

	inbuf.value = in->data;
	inbuf.length = s_length(in);

	major_status = gss_wrap(&minor_status, ctx, True,
				GSS_C_QOP_DEFAULT, &inbuf, &conf_state, &outbuf);

	if (major_status != GSS_S_COMPLETE)
	{
		cssp_gss_report_error(GSS_C_GSS_CODE, "Failed to encrypt and sign message",
				      major_status, minor_status);
		return False;
	}

	if (!conf_state)
	{
		error("GSS Confidentiality failed, no encryption of message performed.");
		return False;
	}

	// write enc data to out stream
	out->data = out->p = xmalloc(outbuf.length);
	out->size = outbuf.length;
	out_uint8p(out, outbuf.value, outbuf.length);
	s_mark_end(out);

	gss_release_buffer(&minor_status, &outbuf);

	return True;
}

static RD_BOOL
cssp_gss_unwrap(gss_ctx_id_t * ctx, STREAM in, STREAM out)
{
	OM_uint32 major_status;
	OM_uint32 minor_status;
	gss_qop_t qop_state;
	gss_buffer_desc inbuf, outbuf;
	int conf_state;

	inbuf.value = in->data;
	inbuf.length = s_length(in);

	major_status = gss_unwrap(&minor_status, ctx, &inbuf, &outbuf, &conf_state, &qop_state);

	if (major_status != GSS_S_COMPLETE)
	{
		cssp_gss_report_error(GSS_C_GSS_CODE, "Failed to decrypt message",
				      major_status, minor_status);
		return False;
	}

	out->data = out->p = xmalloc(outbuf.length);
	out->size = outbuf.length;
	out_uint8p(out, outbuf.value, outbuf.length);
	s_mark_end(out);

	gss_release_buffer(&minor_status, &outbuf);

	return True;
}

#ifdef WITH_DEBUG_CREDSSP
void
streamsave(STREAM s, char *fn)
{
	FILE *f = fopen(fn, "wb");
	fwrite(s->data, s_length(s), 1, f);
	fclose(f);
}
#endif

static STREAM
cssp_encode_tspasswordcreds(char *username, char *password, char *domain)
{
	STREAM out, h1, h2;
	struct stream tmp = { 0 };
	struct stream message = { 0 };

	memset(&tmp, 0, sizeof(tmp));
	memset(&message, 0, sizeof(message));

	// domainName [0]
	s_realloc(&tmp, 4 + strlen(domain) * sizeof(uint16));
	s_reset(&tmp);
	rdp_out_unistr(&tmp, domain, strlen(domain) * sizeof(uint16));
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// userName [1]
	s_realloc(&tmp, 4 + strlen(username) * sizeof(uint16));
	s_reset(&tmp);
	rdp_out_unistr(&tmp, username, strlen(username) * sizeof(uint16));
	s_mark_end(&tmp);

	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// password [2]
	s_realloc(&tmp, 4 + strlen(password) * sizeof(uint16));
	s_reset(&tmp);
	rdp_out_unistr(&tmp, password, strlen(password) * sizeof(uint16));
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// build message
	out = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);

	// cleanup
	xfree(tmp.data);
	xfree(message.data);
	return out;
}

/* KeySpecs from wincrypt.h */
#define AT_KEYEXCHANGE 1
#define AT_SIGNATURE   2

static STREAM
cssp_encode_tscspdatadetail(unsigned char keyspec, char *card, char *reader, char *container,
			    char *csp)
{
	STREAM out;
	STREAM h1, h2;
	struct stream tmp = { 0 };
	struct stream message = { 0 };

	// keySpec [0]
	s_realloc(&tmp, sizeof(uint8));
	s_reset(&tmp);
	out_uint8(&tmp, keyspec);
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_INTEGER, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// cardName [1]
	if (card)
	{
		s_realloc(&tmp, 4 + strlen(card) * sizeof(uint16));
		s_reset(&tmp);
		rdp_out_unistr(&tmp, card, strlen(card) * sizeof(uint16));
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// readerName [2]
	if (reader)
	{
		s_realloc(&tmp, 4 + strlen(reader) * sizeof(uint16));
		s_reset(&tmp);
		rdp_out_unistr(&tmp, reader, strlen(reader) * sizeof(uint16));
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// containerName [3]
	if (container)
	{
		s_realloc(&tmp, 4 + strlen(container) * sizeof(uint16));
		s_reset(&tmp);
		rdp_out_unistr(&tmp, container, strlen(container) * sizeof(uint16));
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 3, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// cspName [4]
	if (csp)
	{
		s_realloc(&tmp, 4 + strlen(csp) * sizeof(uint16));
		s_reset(&tmp);
		rdp_out_unistr(&tmp, csp, strlen(csp) * sizeof(uint16));
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 4, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	s_mark_end(&message);

	// build message
	out = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);

	// cleanup
	free(tmp.data);
	free(message.data);
	return out;
}

static STREAM
cssp_encode_tssmartcardcreds(char *username, char *password, char *domain)
{
	STREAM out, h1, h2;
	struct stream tmp = { 0 };
	struct stream message = { 0 };

	// pin [0]
	s_realloc(&tmp, strlen(password) * sizeof(uint16));
	s_reset(&tmp);
	rdp_out_unistr(&tmp, password, strlen(password) * sizeof(uint16));
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// cspData[1]        
	h2 = cssp_encode_tscspdatadetail(AT_KEYEXCHANGE, g_sc_card_name, g_sc_reader_name,
					 g_sc_container_name, g_sc_csp_name);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// userHint [2]
	if (username && strlen(username))
	{
		s_realloc(&tmp, strlen(username) * sizeof(uint16));
		s_reset(&tmp);
		rdp_out_unistr(&tmp, username, strlen(username) * sizeof(uint16));
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// domainHint [3]
	if (domain && strlen(domain))
	{
		s_realloc(&tmp, strlen(domain) * sizeof(uint16));
		s_reset(&tmp);
		rdp_out_unistr(&tmp, domain, strlen(domain) * sizeof(uint16));
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 3, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	s_mark_end(&message);

	// build message
	out = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);

	// cleanup
	free(tmp.data);
	free(message.data);
	return out;
}

STREAM
cssp_encode_tscredentials(char *username, char *password, char *domain)
{
	STREAM out;
	STREAM h1, h2, h3;
	struct stream tmp = { 0 };
	struct stream message = { 0 };

	// credType [0]
	s_realloc(&tmp, sizeof(uint8));
	s_reset(&tmp);
	if (g_use_password_as_pin == False)
	{
		out_uint8(&tmp, 1);	// TSPasswordCreds
	}
	else
	{
		out_uint8(&tmp, 2);	// TSSmartCardCreds
	}

	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_INTEGER, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// credentials [1]
	if (g_use_password_as_pin == False)
	{
		h3 = cssp_encode_tspasswordcreds(username, password, domain);
	}
	else
	{
		h3 = cssp_encode_tssmartcardcreds(username, password, domain);
	}

	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, h3);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h3);
	s_free(h2);
	s_free(h1);

	// Construct ASN.1 message
	out = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);

#if WITH_DEBUG_CREDSSP
	streamsave(out, "tscredentials.raw");
	printf("Out TSCredentials %ld bytes\n", s_length(out));
	hexdump(out->data, s_length(out));
#endif

	// cleanup
	xfree(message.data);
	xfree(tmp.data);

	return out;
}

RD_BOOL
cssp_send_tsrequest(STREAM token, STREAM auth, STREAM pubkey)
{
	STREAM s;
	STREAM h1, h2, h3, h4, h5;

	struct stream tmp = { 0 };
	struct stream message = { 0 };

	memset(&message, 0, sizeof(message));
	memset(&tmp, 0, sizeof(tmp));

	// version [0]
	s_realloc(&tmp, sizeof(uint8));
	s_reset(&tmp);
	out_uint8(&tmp, 2);
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_INTEGER, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_uint8p(&message, h1->data, s_length(h1));
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// negoToken [1]
	if (token && s_length(token))
	{
		h5 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, token);
		h4 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h5);
		h3 = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, h4);
		h2 = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, h3);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));
		s_mark_end(&message);
		s_free(h5);
		s_free(h4);
		s_free(h3);
		s_free(h2);
		s_free(h1);
	}

	// authInfo [2]
	if (auth && s_length(auth))
	{
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, auth);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2, h2);

		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));

		s_free(h2);
		s_free(h1);
	}

	// pubKeyAuth [3]
	if (pubkey && s_length(pubkey))
	{
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, pubkey);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 3, h2);

		s_realloc(&message, s_length(&message) + s_length(h1));
		out_uint8p(&message, h1->data, s_length(h1));
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}
	s_mark_end(&message);

	// Construct ASN.1 Message
	// Todo: can h1 be send directly instead of tcp_init() approach
	h1 = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);
	s = tcp_init(s_length(h1));
	out_uint8p(s, h1->data, s_length(h1));
	s_mark_end(s);
	s_free(h1);

#if WITH_DEBUG_CREDSSP
	streamsave(s, "tsrequest_out.raw");
	printf("Out TSRequest %ld bytes\n", s_length(s));
	hexdump(s->data, s_length(s));
#endif

	tcp_send(s);

	// cleanup
	xfree(message.data);
	xfree(tmp.data);

	return True;
}


RD_BOOL
cssp_read_tsrequest(STREAM token, STREAM pubkey)
{
	STREAM s;
	int length;
	int tagval;

	s = tcp_recv(NULL, 4);

	if (s == NULL)
		return False;

	// verify ASN.1 header
	if (s->p[0] != (BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED))
	{
		error("Expected BER_TAG_SEQUENCE|BER_TAG_CONSTRUCTED, got %x", s->p[0]);
		return False;
	}

	// peek at first 4 bytes to get full message length
	if (s->p[1] < 0x80)
		length = s->p[1] - 2;
	else if (s->p[1] == 0x81)
		length = s->p[2] - 1;
	else if (s->p[1] == 0x82)
		length = (s->p[2] << 8) | s->p[3];
	else
		return False;

	// receive the remainings of message
	s = tcp_recv(s, length);

#if WITH_DEBUG_CREDSSP
	streamsave(s, "tsrequest_in.raw");
	printf("In TSRequest token %ld bytes\n", s_length(s));
	hexdump(s->data, s_length(s));
#endif

	// parse the response and into nego token
	if (!ber_in_header(s, &tagval, &length) ||
	    tagval != (BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED))
		return False;

	// version [0]
	if (!ber_in_header(s, &tagval, &length) ||
	    tagval != (BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0))
		return False;
	in_uint8s(s, length);

	// negoToken [1]
	if (token)
	{
		if (!ber_in_header(s, &tagval, &length)
		    || tagval != (BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1))
			return False;
		if (!ber_in_header(s, &tagval, &length)
		    || tagval != (BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED))
			return False;
		if (!ber_in_header(s, &tagval, &length)
		    || tagval != (BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED))
			return False;
		if (!ber_in_header(s, &tagval, &length)
		    || tagval != (BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0))
			return False;

		if (!ber_in_header(s, &tagval, &length) || tagval != BER_TAG_OCTET_STRING)
			return False;

		token->end = token->p = token->data;
		out_uint8p(token, s->p, length);
		s_mark_end(token);
	}

	// pubKey [3]
	if (pubkey)
	{
		if (!ber_in_header(s, &tagval, &length)
		    || tagval != (BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 3))
			return False;

		if (!ber_in_header(s, &tagval, &length) || tagval != BER_TAG_OCTET_STRING)
			return False;

		pubkey->data = pubkey->p = s->p;
		pubkey->end = pubkey->data + length;
		pubkey->size = length;
	}


	return True;
}

RD_BOOL
cssp_connect(char *server, char *user, char *domain, char *password, STREAM s)
{
	OM_uint32 actual_time;
	gss_cred_id_t cred;
	gss_buffer_desc input_tok, output_tok;
	gss_name_t target_name;
	OM_uint32 major_status, minor_status;
	int context_established = 0;
	gss_ctx_id_t gss_ctx;
	gss_OID desired_mech = &_gss_spnego_krb5_mechanism_oid_desc;

	STREAM ts_creds;
	struct stream token = { 0 };
	struct stream pubkey = { 0 };
	struct stream pubkey_cmp = { 0 };

	// Verify that system gss support spnego
	if (!cssp_gss_mech_available(desired_mech))
	{
		warning("CredSSP: System doesn't have support for desired authentication mechanism.\n");
		return False;
	}

	// Get service name
	if (!cssp_gss_get_service_name(server, &target_name))
	{
		warning("CredSSP: Failed to get target service name.\n");
		return False;
	}

	// Establish tls connection to server
	if (!tcp_tls_connect())
	{
		warning("CredSSP: Failed to establish TLS connection.\n");
		return False;
	}

	tcp_tls_get_server_pubkey(&pubkey);

#ifdef WITH_DEBUG_CREDSSP
	streamsave(&pubkey, "PubKey.raw");
#endif

	// Enter the spnego loop
	OM_uint32 actual_services;
	gss_OID actual_mech;
	struct stream blob = { 0 };

	gss_ctx = GSS_C_NO_CONTEXT;
	cred = GSS_C_NO_CREDENTIAL;

	input_tok.length = 0;
	output_tok.length = 0;
	minor_status = 0;

	int i = 0;

	do
	{
		major_status = gss_init_sec_context(&minor_status,
						    cred,
						    &gss_ctx,
						    target_name,
						    desired_mech,
						    GSS_C_MUTUAL_FLAG | GSS_C_DELEG_FLAG,
						    GSS_C_INDEFINITE,
						    GSS_C_NO_CHANNEL_BINDINGS,
						    &input_tok,
						    &actual_mech,
						    &output_tok, &actual_services, &actual_time);

		if (GSS_ERROR(major_status))
		{
			if (i == 0)
				error("CredSSP: Initialize failed, do you have correct kerberos tgt initialized ?\n");
			else
				error("CredSSP: Negotiation failed.\n");

#ifdef WITH_DEBUG_CREDSSP
			cssp_gss_report_error(GSS_C_GSS_CODE, "CredSSP: SPNEGO negotiation failed.",
					      major_status, minor_status);
#endif
			goto bail_out;
		}

		// validate required services
		if (!(actual_services & GSS_C_CONF_FLAG))
		{
			error("CredSSP: Confidiality service required but is not available.\n");
			goto bail_out;
		}

		// Send token to server
		if (output_tok.length != 0)
		{
			if (output_tok.length > token.size)
				s_realloc(&token, output_tok.length);
			s_reset(&token);

			out_uint8p(&token, output_tok.value, output_tok.length);
			s_mark_end(&token);

			if (!cssp_send_tsrequest(&token, NULL, NULL))
				goto bail_out;

			(void) gss_release_buffer(&minor_status, &output_tok);
		}

		// Read token from server
		if (major_status & GSS_S_CONTINUE_NEEDED)
		{
			(void) gss_release_buffer(&minor_status, &input_tok);

			if (!cssp_read_tsrequest(&token, NULL))
				goto bail_out;

			input_tok.value = token.data;
			input_tok.length = s_length(&token);
		}
		else
		{
			// Send encrypted pubkey for verification to server
			context_established = 1;

			if (!cssp_gss_wrap(gss_ctx, &pubkey, &blob))
				goto bail_out;

			if (!cssp_send_tsrequest(NULL, NULL, &blob))
				goto bail_out;

			context_established = 1;
		}

		i++;

	}
	while (!context_established);

	// read tsrequest response and decrypt for public key validation
	if (!cssp_read_tsrequest(NULL, &blob))
		goto bail_out;

	if (!cssp_gss_unwrap(gss_ctx, &blob, &pubkey_cmp))
		goto bail_out;

	pubkey_cmp.data[0] -= 1;

	// validate public key
	if (memcmp(pubkey.data, pubkey_cmp.data, s_length(&pubkey)) != 0)
	{
		error("CredSSP: Cannot guarantee integrity of server connection, MITM ? "
		      "(public key data mismatch)\n");
		goto bail_out;
	}

	// Send TSCredentials
	ts_creds = cssp_encode_tscredentials(user, password, domain);

	if (!cssp_gss_wrap(gss_ctx, ts_creds, &blob))
		goto bail_out;

	s_free(ts_creds);

	if (!cssp_send_tsrequest(NULL, &blob, NULL))
		goto bail_out;

	return True;

      bail_out:
	xfree(token.data);
	return False;
}
