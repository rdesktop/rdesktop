/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   CredSSP layer and Kerberos support.
   Copyright 2012-2017 Henrik Andersson <hean01@cendio.se> for Cendio AB
   Copyright 2026 Marco Fortina <marco_fortina@hotmail.it>

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

#ifdef WITH_GSSAPI_CREDSSP
#include <gssapi/gssapi.h>
#endif
#include <nettle/sha2.h>
#include "rdesktop.h"
#include "ntlmssp.h"
#include "spnego.h"

extern RD_BOOL g_use_password_as_pin;
extern RD_BOOL g_restricted_admin;
extern RD_BOOL g_remote_guard;

#define RDPEAR_LOGON_CRED_PACKAGE "__rdesktop_remote_guard_logon_cred__"

extern char *g_sc_csp_name;
extern char *g_sc_reader_name;
extern char *g_sc_card_name;
extern char *g_sc_container_name;

#ifdef WITH_GSSAPI_CREDSSP
static gss_OID_desc _gss_spnego_krb5_mechanism_oid_desc =
	{ 9, (void *) "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02" };
static gss_ctx_id_t g_remote_guard_gss_ctx = GSS_C_NO_CONTEXT;
#endif

static STREAM
ber_wrap_hdr_data(int tagval, STREAM in)
{
	STREAM out;
	int size = s_length(in) + 16;

	out = s_alloc(size);
	ber_out_header(out, tagval, s_length(in));
	out_stream(out, in);
	s_mark_end(out);

	return out;
}


#ifdef WITH_GSSAPI_CREDSSP
static void
cssp_gss_report_error(OM_uint32 code, char *str, OM_uint32 major_status, OM_uint32 minor_status)
{
	OM_uint32 msgctx = 0, ms;
	gss_buffer_desc status_string;

	logger(Core, Debug, "GSS error [%d:%d:%d]: %s", (major_status & 0xff000000) >> 24,	// Calling error
	       (major_status & 0xff0000) >> 16,	// Routine error
	       major_status & 0xffff,	// Supplementary info bits
	       str);

	do
	{
		ms = gss_display_status(&minor_status, major_status,
					code, GSS_C_NULL_OID, &msgctx, &status_string);
		if (ms != GSS_S_COMPLETE)
			continue;

		logger(Core, Debug, " - %s", status_string.value);

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

static STREAM
cssp_gss_wrap(gss_ctx_id_t ctx, STREAM in)
{
	int conf_state;
	OM_uint32 major_status;
	OM_uint32 minor_status;
	gss_buffer_desc inbuf, outbuf;
	STREAM out;

	s_seek(in, 0);
	inbuf.length = s_length(in);
	in_uint8p(in, inbuf.value, s_length(in));
	s_seek(in, 0);

	major_status = gss_wrap(&minor_status, ctx, True,
				GSS_C_QOP_DEFAULT, &inbuf, &conf_state, &outbuf);

	if (major_status != GSS_S_COMPLETE)
	{
		cssp_gss_report_error(GSS_C_GSS_CODE, "Failed to encrypt and sign message",
				      major_status, minor_status);
		return NULL;
	}

	if (!conf_state)
	{
		logger(Core, Error,
		       "cssp_gss_wrap(), GSS Confidentiality failed, no encryption of message performed.");
		return NULL;
	}

	// write enc data to out stream
	out = s_alloc(outbuf.length);
	out_uint8a(out, outbuf.value, outbuf.length);
	s_mark_end(out);
	s_seek(out, 0);

	gss_release_buffer(&minor_status, &outbuf);

	return out;
}

static STREAM
cssp_gss_unwrap(gss_ctx_id_t ctx, STREAM in)
{
	OM_uint32 major_status;
	OM_uint32 minor_status;
	gss_qop_t qop_state;
	gss_buffer_desc inbuf, outbuf;
	int conf_state;
	STREAM out;

	s_seek(in, 0);
	inbuf.length = s_length(in);
	in_uint8p(in, inbuf.value, s_length(in));
	s_seek(in, 0);

	major_status = gss_unwrap(&minor_status, ctx, &inbuf, &outbuf, &conf_state, &qop_state);

	if (major_status != GSS_S_COMPLETE)
	{
		cssp_gss_report_error(GSS_C_GSS_CODE, "Failed to decrypt message",
				      major_status, minor_status);
		return NULL;
	}

	out = s_alloc(outbuf.length);
	out_uint8a(out, outbuf.value, outbuf.length);
	s_mark_end(out);
	s_seek(out, 0);

	gss_release_buffer(&minor_status, &outbuf);

	return out;
}


RD_BOOL
cssp_remote_guard_has_security_context(void)
{
	return g_remote_guard_gss_ctx != GSS_C_NO_CONTEXT;
}

STREAM
cssp_remote_guard_wrap(STREAM in)
{
	if (g_remote_guard_gss_ctx == GSS_C_NO_CONTEXT)
		return NULL;

	return cssp_gss_wrap(g_remote_guard_gss_ctx, in);
}

STREAM
cssp_remote_guard_unwrap(STREAM in)
{
	if (g_remote_guard_gss_ctx == GSS_C_NO_CONTEXT)
		return NULL;

	return cssp_gss_unwrap(g_remote_guard_gss_ctx, in);
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

	s_realloc(&tmp, 512 * 4);

	// domainName [0]
	s_reset(&tmp);
	out_utf16s(&tmp, domain);
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// userName [1]
	s_reset(&tmp);
	out_utf16s(&tmp, username);
	s_mark_end(&tmp);

	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// password [2]
	s_reset(&tmp);
	out_utf16s(&tmp, password);
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
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

	s_realloc(&tmp, 512 * 4);

	// keySpec [0]
	s_reset(&tmp);
	out_uint8(&tmp, keyspec);
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_INTEGER, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// cardName [1]
	if (card)
	{
		s_reset(&tmp);
		out_utf16s(&tmp, card);
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_stream(&message, h1);
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// readerName [2]
	if (reader)
	{
		s_reset(&tmp);
		out_utf16s(&tmp, reader);
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_stream(&message, h1);
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// containerName [3]
	if (container)
	{
		s_reset(&tmp);
		out_utf16s(&tmp, container);
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 3, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_stream(&message, h1);
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// cspName [4]
	if (csp)
	{
		s_reset(&tmp);
		out_utf16s(&tmp, csp);
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 4, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_stream(&message, h1);
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
cssp_encode_tsremoteguard_packagecreds(const char *package_name, STREAM cred_buffer)
{
	STREAM out, h1, h2;
	struct stream tmp = { 0 };
	struct stream message = { 0 };

	s_realloc(&tmp, 512 * 4);

	/* packageName [0] */
	s_reset(&tmp);
	out_utf16s(&tmp, package_name);
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	/* credBuffer [1] */
	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, cred_buffer);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	s_mark_end(&message);
	out = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);

	free(tmp.data);
	free(message.data);
	return out;
}

static STREAM
cssp_remote_guard_get_logon_cred(void)
{
	STREAM cred_buffer;
	struct stream request = { 0 };

	s_realloc(&request, 1);
	s_reset(&request);
	s_mark_end(&request);

	cred_buffer = rdpear_call_helper(RDPEAR_LOGON_CRED_PACKAGE, &request);
	free(request.data);

	if (cred_buffer == NULL || s_length(cred_buffer) == 0)
	{
		if (cred_buffer != NULL)
			s_free(cred_buffer);
		logger(Core, Error,
		       "Remote Credential Guard requires a non-empty Negotiate logon credential from the RDPEAR helper");
		return NULL;
	}

	cred_buffer->p = cred_buffer->data;
	return cred_buffer;
}

static STREAM
cssp_encode_tsremoteguardcreds(void)
{
	STREAM out, h2, h3;
	STREAM cred_buffer;
	struct stream message = { 0 };

	cred_buffer = cssp_remote_guard_get_logon_cred();
	if (cred_buffer == NULL)
		return NULL;

	/* logonCred [0]: Negotiate consumes the logon credential generated by the helper. */
	h3 = cssp_encode_tsremoteguard_packagecreds("Negotiate", cred_buffer);
	h2 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h3);
	s_realloc(&message, s_length(&message) + s_length(h2));
	out_stream(&message, h2);
	s_mark_end(&message);
	s_free(cred_buffer);
	s_free(h3);
	s_free(h2);

	s_mark_end(&message);
	out = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);

	free(message.data);
	return out;
}

static STREAM
cssp_encode_tssmartcardcreds(char *username, char *password, char *domain)
{
	STREAM out, h1, h2;
	struct stream tmp = { 0 };
	struct stream message = { 0 };

	s_realloc(&tmp, 512 * 4);

	// pin [0]
	s_reset(&tmp);
	out_utf16s(&tmp, password);
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// cspData [1]
	h2 = cssp_encode_tscspdatadetail(AT_KEYEXCHANGE, g_sc_card_name, g_sc_reader_name,
					 g_sc_container_name, g_sc_csp_name);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// userHint [2]
	if (username && strlen(username))
	{
		s_reset(&tmp);
		out_utf16s(&tmp, username);
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_stream(&message, h1);
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// domainHint [3]
	if (domain && strlen(domain))
	{
		s_reset(&tmp);
		out_utf16s(&tmp, domain);
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 3, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_stream(&message, h1);
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
	if (g_remote_guard)
	{
		out_uint8(&tmp, 6);	// TSRemoteGuardCreds
	}
	else if (g_use_password_as_pin == False || g_restricted_admin)
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
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	// credentials [1]
	if (g_remote_guard)
	{
		/* Remote Guard uses redirected credentials instead of password or smartcard creds. */
		h3 = cssp_encode_tsremoteguardcreds();
		if (h3 == NULL)
		{
			free(tmp.data);
			free(message.data);
			return NULL;
		}
	}
	else if (g_restricted_admin)
	{
		/* Restricted Admin authenticates CredSSP locally, then sends empty logon credentials. */
		h3 = cssp_encode_tspasswordcreds("", "", "");
	}
	else if (g_use_password_as_pin == False)
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
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h3);
	s_free(h2);
	s_free(h1);

	// Construct ASN.1 message
	out = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);

	// cleanup
	xfree(message.data);
	xfree(tmp.data);

	return out;
}

#define CSSP_CLIENT_VERSION 6
#define CSSP_LEGACY_CLIENT_VERSION 2
#define CSSP_BINDING_HASH_SIZE 32
#define CSSP_NONCE_SIZE 32

static uint32 g_cssp_client_version = CSSP_CLIENT_VERSION;
static uint32 g_cssp_peer_version = 2;

static uint32
cssp_peer_version(void)
{
	if (g_cssp_peer_version > g_cssp_client_version)
		return g_cssp_client_version;
	return g_cssp_peer_version;
}

static void
cssp_set_client_version(uint32 version)
{
	g_cssp_client_version = version;
}

static uint32
cssp_read_integer(STREAM s, int length)
{
	uint32 value;
	uint8 byte;
	int i;

	value = 0;
	for (i = 0; i < length; i++)
	{
		in_uint8(s, byte);
		value = (value << 8) | byte;
	}
	return value;
}

static STREAM
cssp_build_pubkey_binding(STREAM pubkey, const uint8 *nonce, RD_BOOL client_to_server)
{
	STREAM out;
	struct sha256_ctx sha256;
	uint8 digest[CSSP_BINDING_HASH_SIZE];
	const char *magic;

	if (cssp_peer_version() < 5)
	{
		out = s_alloc(s_length(pubkey));
		out_stream(out, pubkey);
		s_mark_end(out);
		s_seek(out, 0);
		return out;
	}

	magic = client_to_server ? "CredSSP Client-To-Server Binding Hash" :
		"CredSSP Server-To-Client Binding Hash";
	/*
	 * FreeRDP/OpenSSL hashes i2d_PublicKey() output for RSA, which matches the
	 * SubjectPublicKey BIT STRING payload bytes, not the enclosing BIT STRING TLV.
	 */
	logger(Core, Debug, "cssp_build_pubkey_binding(), publicKey=%u", (unsigned) s_length(pubkey));
	sha256_init(&sha256);
	sha256_update(&sha256, strlen(magic) + 1, (const uint8 *) magic);
	sha256_update(&sha256, CSSP_NONCE_SIZE, nonce);
	sha256_update(&sha256, s_length(pubkey), pubkey->data);
	sha256_digest(&sha256, sizeof(digest), digest);

	out = s_alloc(sizeof(digest));
	out_uint8a(out, digest, sizeof(digest));
	s_mark_end(out);
	s_seek(out, 0);
	return out;
}

static RD_BOOL
cssp_validate_pubkey_response(STREAM pubkey, const uint8 *nonce, STREAM response)
{
	STREAM expected;
	uint8 first_byte;
	RD_BOOL ok;

	if (cssp_peer_version() >= 5)
	{
		expected = cssp_build_pubkey_binding(pubkey, nonce, False);
		ok = (s_length(expected) == s_length(response) &&
		      memcmp(expected->data, response->data, s_length(expected)) == 0);
		s_free(expected);
		return ok;
	}

	in_uint8(response, first_byte);
	s_seek(response, 0);
	out_uint8(response, first_byte - 1);
	s_seek(response, 0);

	return (s_length(pubkey) == s_length(response) &&
		memcmp(pubkey->data, response->data, s_length(pubkey)) == 0);
}

static RD_BOOL
cssp_send_tsrequest(STREAM token, STREAM auth, STREAM pubkey, const uint8 *nonce)
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
	out_uint8(&tmp, g_cssp_client_version);
	logger(Core, Debug, "cssp_send_tsrequest(), version=%u peer=%u token=%u auth=%u pubkey=%u nonce=%u",
	       g_cssp_client_version, cssp_peer_version(), token ? (unsigned) s_length(token) : 0,
	       auth ? (unsigned) s_length(auth) : 0, pubkey ? (unsigned) s_length(pubkey) : 0,
	       (nonce && cssp_peer_version() >= 5) ? CSSP_NONCE_SIZE : 0);
	s_mark_end(&tmp);
	h2 = ber_wrap_hdr_data(BER_TAG_INTEGER, &tmp);
	h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
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
		out_stream(&message, h1);
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
		out_stream(&message, h1);
		s_mark_end(&message);

		s_free(h2);
		s_free(h1);
	}

	// pubKeyAuth [3]
	if (pubkey && s_length(pubkey))
	{
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, pubkey);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 3, h2);

		s_realloc(&message, s_length(&message) + s_length(h1));
		out_stream(&message, h1);
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	// clientNonce [5]
	if (nonce && cssp_peer_version() >= 5)
	{
		s_reset(&tmp);
		s_realloc(&tmp, CSSP_NONCE_SIZE);
		out_uint8a(&tmp, nonce, CSSP_NONCE_SIZE);
		s_mark_end(&tmp);
		h2 = ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
		h1 = ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 5, h2);
		s_realloc(&message, s_length(&message) + s_length(h1));
		out_stream(&message, h1);
		s_mark_end(&message);
		s_free(h2);
		s_free(h1);
	}

	s_mark_end(&message);

	// Construct ASN.1 Message
	h1 = ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);
	logger(Core, Debug, "cssp_send_tsrequest(), der=%u payload=%u", (unsigned) s_length(h1),
	       (unsigned) s_length(&message));
	s = tcp_init(s_length(h1));
	out_stream(s, h1);
	s_mark_end(s);
	s_free(h1);

	tcp_send(s);
	s_free(s);

	// cleanup
	xfree(message.data);
	xfree(tmp.data);

	return True;
}

static RD_BOOL
cssp_parse_negodata_token(STREAM s, STREAM *out)
{
	int length;
	int tagval;

	if (!ber_in_header(s, &tagval, &length) ||
	    tagval != (BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED))
		return False;
	if (!ber_in_header(s, &tagval, &length) ||
	    tagval != (BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED))
		return False;
	if (!ber_in_header(s, &tagval, &length) ||
	    tagval != (BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0))
		return False;
	if (!ber_in_header(s, &tagval, &length) || tagval != BER_TAG_OCTET_STRING)
		return False;
	if (!s_check_rem(s, length))
		return False;

	*out = s_alloc(length);
	out_uint8stream(*out, s, length);
	s_mark_end(*out);
	s_seek(*out, 0);
	return True;
}

static STREAM
cssp_read_tsrequest(RD_BOOL pubkey)
{
	STREAM s, out;
	int length;
	int tagval;
	struct stream packet;
	struct stream field;

	out = NULL;
	s = tcp_recv(NULL, 4);

	if (s == NULL)
		return NULL;

	if (!ber_in_header(s, &tagval, &length) ||
	    tagval != (BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED))
		return NULL;

	length -= s_remaining(s);
	s = tcp_recv(s, length);
	if (s == NULL)
		return NULL;
	packet = *s;
	logger(Core, Debug, "cssp_read_tsrequest(), received der=%u pubkey_mode=%d",
	       (unsigned) s_length(s), pubkey);

	while (s_check_rem(s, 2))
	{
		if (!ber_in_header(s, &tagval, &length))
			return NULL;
		if (!s_check_rem(s, length))
		{
			rdp_protocol_error("consume of TSRequest field would overrun", &packet);
		}

		field = *s;
		field.end = field.p + length;
		field.size = length;
		s->p += length;

		switch (tagval)
		{
			case BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0:
				if (!ber_in_header(&field, &tagval, &length) || tagval != BER_TAG_INTEGER)
					return NULL;
				if (!s_check_rem(&field, length))
					return NULL;
				g_cssp_peer_version = cssp_read_integer(&field, length);
				logger(Core, Debug, "cssp_read_tsrequest(), peer version=%u", g_cssp_peer_version);
				break;

			case BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1:
				if (!pubkey && !out && !cssp_parse_negodata_token(&field, &out))
					return NULL;
				if (out)
					logger(Core, Debug, "cssp_read_tsrequest(), negoToken len=%u",
					       (unsigned) s_length(out));
				break;

			case BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 3:
				if (pubkey && !out)
				{
					if (!ber_in_header(&field, &tagval, &length) || tagval != BER_TAG_OCTET_STRING)
						return NULL;
					if (!s_check_rem(&field, length))
						return NULL;
					out = s_alloc(length);
					out_uint8stream(out, &field, length);
					s_mark_end(out);
					s_seek(out, 0);
					logger(Core, Debug, "cssp_read_tsrequest(), pubKeyAuth len=%u",
					       (unsigned) s_length(out));
				}
				break;

			case BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 4:
				if (!ber_in_header(&field, &tagval, &length) || tagval != BER_TAG_INTEGER)
					return NULL;
				if (!s_check_rem(&field, length))
					return NULL;
				logger(Core, Error, "cssp_read_tsrequest(), server returned NTSTATUS 0x%08x",
				       cssp_read_integer(&field, length));
				return NULL;
		}
	}

	logger(Core, Debug, "cssp_read_tsrequest(), returning %s len=%u peer=%u",
	       pubkey ? "pubKeyAuth" : "negoToken", out ? (unsigned) s_length(out) : 0,
	       g_cssp_peer_version);
	return out;
}


#ifdef WITH_GSSAPI_CREDSSP
static RD_BOOL
cssp_connect_gss(char *server, char *user, char *domain, char *password, STREAM s)
{
	UNUSED(s);
	OM_uint32 actual_time;
	gss_cred_id_t cred;
	gss_buffer_desc input_tok, output_tok;
	gss_name_t target_name;
	OM_uint32 major_status, minor_status;
	int context_established = 0;
	gss_ctx_id_t gss_ctx;
	gss_OID desired_mech = &_gss_spnego_krb5_mechanism_oid_desc;

	STREAM ts_creds;
	STREAM token;
	STREAM pubkey, pubkey_cmp;
	uint8 client_nonce[CSSP_NONCE_SIZE];
	STREAM plain_pubkey;

	RD_BOOL ret;
	STREAM blob;

	if (!cssp_gss_mech_available(desired_mech))
	{
		logger(Core, Debug,
		       "cssp_connect_gss(), system doesn't have support for desired authentication mechanism");
		return False;
	}

	if (!cssp_gss_get_service_name(server, &target_name))
	{
		logger(Core, Debug, "cssp_connect_gss(), failed to get target service name");
		return False;
	}

	if (!tcp_tls_connect())
	{
		logger(Core, Debug, "cssp_connect_gss(), failed to establish TLS connection");
		return False;
	}

	pubkey = tcp_tls_get_server_pubkey();
	if (pubkey == NULL)
		return False;
	pubkey_cmp = NULL;
	generate_random(client_nonce);

	OM_uint32 actual_services;
	gss_OID actual_mech;

	gss_ctx = GSS_C_NO_CONTEXT;
	cred = GSS_C_NO_CREDENTIAL;

	token = NULL;
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

		s_free(token);
		token = NULL;

		if (GSS_ERROR(major_status))
		{
			if (i == 0)
				logger(Core, Notice,
				       "Failed to initialize NLA with Kerberos, do you have a valid TGT?");
			else
				logger(Core, Error, "cssp_connect_gss(), negotiation failed");

			cssp_gss_report_error(GSS_C_GSS_CODE, "cssp_connect_gss(), negotiation failed.",
					      major_status, minor_status);
			goto bail_out;
		}

		if (!(actual_services & GSS_C_CONF_FLAG))
		{
			logger(Core, Error,
			       "cssp_connect_gss(), confidentiality service required but is not available");
			goto bail_out;
		}

		if (output_tok.length != 0)
		{
			token = s_alloc(output_tok.length);
			out_uint8a(token, output_tok.value, output_tok.length);
			s_mark_end(token);

			if ((major_status & GSS_S_CONTINUE_NEEDED) == 0)
			{
				plain_pubkey = cssp_build_pubkey_binding(pubkey, client_nonce, True);
				blob = cssp_gss_wrap(gss_ctx, plain_pubkey);
				s_free(plain_pubkey);
				if (blob == NULL)
					goto bail_out;
				ret = cssp_send_tsrequest(token, NULL, blob, client_nonce);
				s_free(blob);
			}
			else
			{
				ret = cssp_send_tsrequest(token, NULL, NULL, NULL);
			}

			s_free(token);
			token = NULL;
			(void) gss_release_buffer(&minor_status, &output_tok);

			if (!ret)
				goto bail_out;
		}

		if (major_status & GSS_S_CONTINUE_NEEDED)
		{
			token = cssp_read_tsrequest(False);
			if (token == NULL)
				goto bail_out;

			input_tok.length = s_length(token);
			in_uint8p(token, input_tok.value, input_tok.length);
		}
		else
		{
			context_established = 1;
		}

		i++;

	}
	while (!context_established);

	s_free(token);

	blob = cssp_read_tsrequest(True);
	if (blob == NULL)
		goto bail_out;

	pubkey_cmp = cssp_gss_unwrap(gss_ctx, blob);
	s_free(blob);
	if (pubkey_cmp == NULL)
		goto bail_out;

	if (!cssp_validate_pubkey_response(pubkey, client_nonce, pubkey_cmp))
	{
		logger(Core, Error,
		       "cssp_connect_gss(), public key binding mismatch, cannot guarantee integrity of server connection");
		goto bail_out;
	}

	s_free(pubkey);
	s_free(pubkey_cmp);
	pubkey = NULL;
	pubkey_cmp = NULL;

	ts_creds = cssp_encode_tscredentials(user, password, domain);
	if (ts_creds == NULL)
		goto bail_out;

	blob = cssp_gss_wrap(gss_ctx, ts_creds);
	s_free(ts_creds);

	if (blob == NULL)
		goto bail_out;

	ret = cssp_send_tsrequest(NULL, blob, NULL, NULL);
	s_free(blob);

	if (!ret)
		goto bail_out;

	if (g_remote_guard)
		g_remote_guard_gss_ctx = gss_ctx;

	return True;

      bail_out:
	s_free(token);
	s_free(pubkey);
	s_free(pubkey_cmp);
	return False;
}
#endif

static RD_BOOL
cssp_connect_ntlm(char *server, char *user, char *domain, char *password)
{
	UNUSED(server);
	NTLMSSP_CONTEXT ntlm;
	STREAM negotiate, negotiate_raw;
	STREAM challenge_spnego, challenge;
	STREAM authenticate;
	STREAM ts_creds;
	STREAM pubkey, pubkey_plain, pubkey_wrapped;
	STREAM pubkey_response, pubkey_response_plain;
	STREAM creds_wrapped;
	uint8 client_nonce[CSSP_NONCE_SIZE];
	RD_BOOL ret;

	if (password == NULL || password[0] == 0 || g_use_password_as_pin)
		return False;

	if (!tcp_tls_connect())
	{
		logger(Core, Debug, "cssp_connect_ntlm(), failed to establish TLS connection");
		return False;
	}

	pubkey = tcp_tls_get_server_pubkey();
	if (pubkey == NULL)
		return False;

	generate_random(client_nonce);
	ntlmssp_init(&ntlm);
	negotiate = ntlmssp_build_negotiate();
	negotiate_raw = s_alloc(s_length(negotiate));
	out_uint8a(negotiate_raw, negotiate->data, s_length(negotiate));
	s_mark_end(negotiate_raw);
	s_seek(negotiate_raw, 0);
	logger(Core, Debug, "cssp_connect_ntlm(), sending raw NTLM negotiate");
	ret = cssp_send_tsrequest(negotiate, NULL, NULL, client_nonce);
	s_free(negotiate);
	if (!ret)
		goto fail;

	challenge_spnego = cssp_read_tsrequest(False);
	if (challenge_spnego == NULL)
		goto fail;
	challenge = spnego_extract_ntlm_token(challenge_spnego);
	s_free(challenge_spnego);
	if (challenge == NULL)
		goto fail;

	if (!ntlmssp_build_authenticate(&ntlm, negotiate_raw, challenge, user, domain, password,
					   server, &authenticate))
	{
		s_free(challenge);
		goto fail;
	}
	s_free(challenge);
	s_free(negotiate_raw);
	negotiate_raw = NULL;

	pubkey_plain = cssp_build_pubkey_binding(pubkey, client_nonce, True);
	pubkey_wrapped = ntlmssp_wrap(&ntlm, pubkey_plain);
	s_free(pubkey_plain);
	if (pubkey_wrapped == NULL)
	{
		s_free(authenticate);
		goto fail;
	}

	logger(Core, Debug,
	       "cssp_connect_ntlm(), sending raw NTLM authenticate and pubKeyAuth in one TSRequest");
	ret = cssp_send_tsrequest(authenticate, NULL, pubkey_wrapped, client_nonce);
	s_free(authenticate);
	s_free(pubkey_wrapped);
	if (!ret)
		goto fail;

	logger(Core, Debug, "cssp_connect_ntlm(), waiting for server pubKeyAuth response");
	pubkey_response = cssp_read_tsrequest(True);
	if (pubkey_response == NULL)
		goto fail;
	pubkey_response_plain = ntlmssp_unwrap(&ntlm, pubkey_response);
	s_free(pubkey_response);
	if (pubkey_response_plain == NULL)
		goto fail;

	if (!cssp_validate_pubkey_response(pubkey, client_nonce, pubkey_response_plain))
	{
		s_free(pubkey_response_plain);
		logger(Core, Error,
		       "cssp_connect_ntlm(), public key binding mismatch, cannot guarantee integrity of server connection");
		goto fail;
	}
	s_free(pubkey_response_plain);
	s_free(pubkey);
	pubkey = NULL;

	ts_creds = cssp_encode_tscredentials(user, password, domain);
	if (ts_creds == NULL)
		goto fail;

	creds_wrapped = ntlmssp_wrap(&ntlm, ts_creds);
	s_free(ts_creds);
	if (creds_wrapped == NULL)
		goto fail;

	logger(Core, Debug, "cssp_connect_ntlm(), sending encrypted TSCredentials");
	ret = cssp_send_tsrequest(NULL, creds_wrapped, NULL, client_nonce);
	s_free(creds_wrapped);
	if (!ret)
		goto fail;

	logger(Core, Verbose, "CredSSP NTLMv2 authentication completed.");
	return True;

fail:
	s_free(negotiate_raw);
	s_free(pubkey);
	return False;
}


#ifndef WITH_GSSAPI_CREDSSP
RD_BOOL
cssp_remote_guard_has_security_context(void)
{
	return False;
}

STREAM
cssp_remote_guard_wrap(STREAM in)
{
	UNUSED(in);
	return NULL;
}

STREAM
cssp_remote_guard_unwrap(STREAM in)
{
	UNUSED(in);
	return NULL;
}
#endif

RD_BOOL
cssp_connect(char *server, char *user, char *domain, char *password, STREAM s)
{
	UNUSED(s);
	g_cssp_peer_version = 2;
	cssp_set_client_version(g_use_password_as_pin ? CSSP_LEGACY_CLIENT_VERSION :
	                        CSSP_CLIENT_VERSION);
	if (g_use_password_as_pin)
	{
		logger(Core, Debug,
		       "cssp_connect(), using legacy CredSSP version for smartcard credentials");
	}

	if (g_remote_guard)
	{
#ifdef WITH_GSSAPI_CREDSSP
		logger(Core, Verbose, "Trying NLA using GSSAPI/Kerberos CredSSP for Remote Credential Guard.");
		return cssp_connect_gss(server, user, domain, password, s);
#else
		logger(Core, Error, "Remote Credential Guard requires optional GSSAPI/Kerberos CredSSP support.");
		return False;
#endif
	}

	if (!g_use_password_as_pin && password && password[0])
	{
		logger(Core, Verbose, "Trying NLA using internal NTLMv2 CredSSP.");
		return cssp_connect_ntlm(server, user, domain, password);
	}

#ifdef WITH_GSSAPI_CREDSSP
	logger(Core, Verbose, "Trying NLA using GSSAPI/Kerberos CredSSP.");
	return cssp_connect_gss(server, user, domain, password, s);
#else
	logger(Core, Error, "CredSSP requires either a password for internal NTLMv2 or optional GSSAPI/Kerberos support.");
	return False;
#endif
}
