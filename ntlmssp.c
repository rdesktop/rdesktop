/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   NTLMSSP client support for CredSSP.
   Copyright 2026 Marco Fortina <marco_fortina@hotmail.it>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include <ctype.h>
#include <time.h>
#include <nettle/md4.h>
#include <nettle/md5.h>
#include <nettle/sha2.h>

#include "rdesktop.h"
#include "ntlmssp.h"

#define NTLMSSP_NEGOTIATE_UNICODE                  0x00000001
#define NTLMSSP_NEGOTIATE_OEM                      0x00000002
#define NTLMSSP_REQUEST_TARGET                     0x00000004
#define NTLMSSP_NEGOTIATE_SIGN                     0x00000010
#define NTLMSSP_NEGOTIATE_SEAL                     0x00000020
#define NTLMSSP_NEGOTIATE_LM_KEY                   0x00000080
#define NTLMSSP_NEGOTIATE_NTLM                     0x00000200
#define NTLMSSP_NEGOTIATE_WORKSTATION_SUPPLIED     0x00002000
#define NTLMSSP_NEGOTIATE_ALWAYS_SIGN              0x00008000
#define NTLMSSP_TARGET_TYPE_DOMAIN                 0x00010000
#define NTLMSSP_TARGET_TYPE_SERVER                 0x00020000
#define NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY 0x00080000
#define NTLMSSP_NEGOTIATE_TARGET_INFO              0x00800000
#define NTLMSSP_NEGOTIATE_VERSION                  0x02000000
#define NTLMSSP_NEGOTIATE_128                      0x20000000
#define NTLMSSP_NEGOTIATE_KEY_EXCH                 0x40000000
#define NTLMSSP_NEGOTIATE_56                       0x80000000

#define NTLMSSP_VERSION_SIZE                        8
#define NTLMSSP_REVISION_W2K3                       0x0f

#define NTLMSSP_NEGOTIATE_BASE_FLAGS \
	(NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_OEM | \
	 NTLMSSP_REQUEST_TARGET | NTLMSSP_NEGOTIATE_SIGN | \
	 NTLMSSP_NEGOTIATE_SEAL | NTLMSSP_NEGOTIATE_LM_KEY | \
	 NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_ALWAYS_SIGN | \
	 NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY | \
	 NTLMSSP_NEGOTIATE_VERSION | NTLMSSP_NEGOTIATE_128 | \
	 NTLMSSP_NEGOTIATE_KEY_EXCH | NTLMSSP_NEGOTIATE_56)

/*
 * TARGET_INFO is returned by the server in CHALLENGE messages. Do not set it
 * in the initial NEGOTIATE message: modern Windows servers reject the first
 * CredSSP token before sending a challenge when Type 1 advertises it.
 */
#define NTLMSSP_NEGOTIATE_TYPE1_FLAGS NTLMSSP_NEGOTIATE_BASE_FLAGS
#define NTLMSSP_NEGOTIATE_AUTH_FLAGS \
	(NTLMSSP_NEGOTIATE_BASE_FLAGS | NTLMSSP_NEGOTIATE_TARGET_INFO)

#define NTLMSSP_SIG_SIZE 8
#define NTLMSSP_SIGNATURE_SIZE 16
#define NTLMSSP_CHALLENGE_SIZE 8
#define NTLMSSP_CLIENT_CHALLENGE_SIZE 8
#define NTLMSSP_SESSION_KEY_SIZE 16
#define NTLMSSP_MD5_SIZE 16
#define NTLMSSP_NTOWF_SIZE 16

#define NTLMSSP_MIC_OFFSET 72
#define NTLMSSP_MIC_SIZE   16

#define NTLMSSP_AV_EOL     0x0000
#define NTLMSSP_AV_FLAGS   0x0006
#define NTLMSSP_AV_TARGET_NAME 0x0009
#define NTLMSSP_AV_FLAGS_MIC_PRESENT 0x00000002
#define NTLMSSP_TARGET_NAME "TERMSRV"

static const uint8 ntlmssp_sig[NTLMSSP_SIG_SIZE] = { 'N', 'T', 'L', 'M', 'S', 'S', 'P', 0 };
static const char client_sign_magic[] = "session key to client-to-server signing key magic constant";
static const char server_sign_magic[] = "session key to server-to-client signing key magic constant";
static const char client_seal_magic[] = "session key to client-to-server sealing key magic constant";
static const char server_seal_magic[] = "session key to server-to-client sealing key magic constant";

extern void generate_random(uint8 * random);

static void
ntlmssp_hmac_md5(const uint8 *key, size_t key_len, const uint8 *data, size_t data_len, uint8 *out)
{
	rdssl_hmac_md5(key, key_len, data, data_len, out);
}

static RD_BOOL
ntlmssp_sensitive_trace_enabled(void)
{
	static int enabled = -1;
	const char *env;

	if (enabled >= 0)
		return enabled ? True : False;

	env = getenv("RDESKTOP_CREDSSP_TRACE");
	enabled = (env && env[0] && strcmp(env, "0") != 0) ? 1 : 0;
	return enabled ? True : False;
}

static void
ntlmssp_debug_bytes(const char *label, const uint8 *data, size_t len)
{
	uint8 first[8];
	uint8 last[8];
	size_t first_len, last_len, last_pos;

	if (!ntlmssp_sensitive_trace_enabled())
		return;

	memset(first, 0, sizeof(first));
	memset(last, 0, sizeof(last));
	if (data == NULL)
	{
		logger(Core, Debug, "%s len=0", label);
		return;
	}

	first_len = len < sizeof(first) ? len : sizeof(first);
	last_len = len < sizeof(last) ? len : sizeof(last);
	last_pos = len > sizeof(last) ? len - sizeof(last) : 0;
	if (first_len)
		memcpy(first, data, first_len);
	if (last_len)
		memcpy(last, data + last_pos, last_len);

	logger(Core, Debug,
	       "%s len=%u first=%02x%02x%02x%02x%02x%02x%02x%02x last=%02x%02x%02x%02x%02x%02x%02x%02x",
	       label, (unsigned) len,
	       first[0], first[1], first[2], first[3], first[4], first[5], first[6], first[7],
	       last[0], last[1], last[2], last[3], last[4], last[5], last[6], last[7]);
}


static STREAM
ntlmssp_string_utf16(const char *string)
{
	STREAM out;
	size_t len;

	len = string ? strlen(string) : 0;
	out = s_alloc((len + 1) * 4);
	if (string)
		out_utf16s_no_eos(out, string);
	s_mark_end(out);
	s_seek(out, 0);
	return out;
}

static char *
ntlmssp_upper_ascii(const char *string)
{
	char *out;
	size_t len, i;

	if (!string)
		string = "";

	len = strlen(string);
	out = xmalloc(len + 1);
	for (i = 0; i < len; i++)
		out[i] = toupper((unsigned char) string[i]);
	out[len] = 0;
	return out;
}

static void
ntlmssp_ntowfv1(const char *password, uint8 *hash)
{
	STREAM password_utf16;
	struct md4_ctx md4;

	password_utf16 = ntlmssp_string_utf16(password ? password : "");
	md4_init(&md4);
	md4_update(&md4, s_length(password_utf16), password_utf16->data);
	md4_digest(&md4, NTLMSSP_NTOWF_SIZE, hash);
	s_free(password_utf16);
}

static void
ntlmssp_ntowfv2(const char *username, const char *domain, const char *password, uint8 *hash)
{
	uint8 nt_hash[NTLMSSP_NTOWF_SIZE];
	char *upper_user;
	char *identity;
	STREAM identity_utf16;
	size_t identity_len;

	upper_user = ntlmssp_upper_ascii(username);
	identity_len = strlen(upper_user) + strlen(domain ? domain : "");
	identity = xmalloc(identity_len + 1);
	snprintf(identity, identity_len + 1, "%s%s", upper_user, domain ? domain : "");

	identity_utf16 = ntlmssp_string_utf16(identity);
	ntlmssp_ntowfv1(password, nt_hash);
	ntlmssp_hmac_md5(nt_hash, sizeof(nt_hash), identity_utf16->data, s_length(identity_utf16), hash);

	s_free(identity_utf16);
	xfree(identity);
	xfree(upper_user);
}

static uint64
ntlmssp_timestamp_now(void)
{
	uint64 unix_time;

	unix_time = (uint64) time(NULL);
	return (unix_time + 11644473600ULL) * 10000000ULL;
}

static RD_BOOL
ntlmssp_read_security_buffer(STREAM s, size_t packet_len, uint16 *len, uint32 *offset)
{
	uint16 alloc_len;

	if (!s_check_rem(s, 8))
		return False;
	in_uint16_le(s, *len);
	in_uint16_le(s, alloc_len);
	in_uint32_le(s, *offset);
	if (*len != alloc_len)
		return False;
	if (*offset > packet_len || *len > packet_len - *offset)
		return False;
	return True;
}

static char *
ntlmssp_decode_target_name(STREAM packet, uint16 len, uint32 offset)
{
	char *out;
	uint16 chars, i;
	uint8 lo, hi;

	if (len == 0)
		return NULL;
	if (offset > (uint32) s_length(packet) || len > (uint32) s_length(packet) - offset)
		return NULL;

	chars = len / 2;
	out = xmalloc(chars + 1);
	for (i = 0; i < chars; i++)
	{
		lo = packet->data[offset + (i * 2)];
		hi = packet->data[offset + (i * 2) + 1];
		out[i] = hi ? '?' : (char) lo;
	}
	out[chars] = 0;
	return out;
}

static void
ntlmssp_write_security_buffer(STREAM s, uint16 len, uint32 offset)
{
	out_uint16_le(s, len);
	out_uint16_le(s, len);
	out_uint32_le(s, offset);
}

static void
ntlmssp_md5_key(const uint8 *session_key, const char *magic, uint8 *key)
{
	struct md5_ctx md5;

	md5_init(&md5);
	md5_update(&md5, NTLMSSP_SESSION_KEY_SIZE, session_key);
	md5_update(&md5, strlen(magic) + 1, (const uint8 *) magic);
#if NETTLE_VERSION_MAJOR >= 4
	md5_digest(&md5, key);
#else
	md5_digest(&md5, NTLMSSP_MD5_SIZE, key);
#endif
}


static void
ntlmssp_out_version(STREAM s)
{
	/* Windows 10 compatible NTLMSSP version structure. */
	out_uint8(s, 10);
	out_uint8(s, 0);
	out_uint16_le(s, 19041);
	out_uint8(s, 0);
	out_uint8(s, 0);
	out_uint8(s, 0);
	out_uint8(s, NTLMSSP_REVISION_W2K3);
}

void
ntlmssp_init(NTLMSSP_CONTEXT *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

STREAM
ntlmssp_build_negotiate(void)
{
	STREAM out;

	out = s_alloc(32 + NTLMSSP_VERSION_SIZE);
	out_uint8a(out, ntlmssp_sig, sizeof(ntlmssp_sig));
	out_uint32_le(out, 1);
	logger(Core, Debug, "ntlmssp_build_negotiate(), flags=0x%08x",
	       NTLMSSP_NEGOTIATE_TYPE1_FLAGS);
	out_uint32_le(out, NTLMSSP_NEGOTIATE_TYPE1_FLAGS);
	ntlmssp_write_security_buffer(out, 0, 32 + NTLMSSP_VERSION_SIZE);
	ntlmssp_write_security_buffer(out, 0, 32 + NTLMSSP_VERSION_SIZE);
	ntlmssp_out_version(out);
	s_mark_end(out);
	s_seek(out, 0);
	return out;
}

static RD_BOOL
ntlmssp_parse_challenge(STREAM challenge, uint32 *flags, uint8 *server_challenge,
			       char **target_name, uint8 **target_info, uint16 *target_info_len)
{
	uint16 target_len, ti_len;
	uint32 target_offset, ti_offset;
	size_t packet_len;
	uint32 type;
	uint8 *ti;

	packet_len = s_length(challenge);
	*target_name = NULL;
	s_seek(challenge, 0);
	if (!s_check_rem(challenge, 48))
		return False;
	if (memcmp(challenge->p, ntlmssp_sig, sizeof(ntlmssp_sig)) != 0)
		return False;
	in_uint8s(challenge, sizeof(ntlmssp_sig));
	in_uint32_le(challenge, type);
	if (type != 2)
		return False;
	if (!ntlmssp_read_security_buffer(challenge, packet_len, &target_len, &target_offset))
		return False;
	*target_name = ntlmssp_decode_target_name(challenge, target_len, target_offset);
	in_uint32_le(challenge, *flags);
	in_uint8a(challenge, server_challenge, NTLMSSP_CHALLENGE_SIZE);
	in_uint8s(challenge, 8);
	if (!ntlmssp_read_security_buffer(challenge, packet_len, &ti_len, &ti_offset))
		return False;

	if (ti_len == 0)
	{
		*target_info = NULL;
		*target_info_len = 0;
		return True;
	}

	ti = xmalloc(ti_len);
	memcpy(ti, challenge->data + ti_offset, ti_len);
	*target_info = ti;
	*target_info_len = ti_len;
	return True;
}

static uint16
ntlmssp_target_info_len_with_auth_av_pairs(uint8 *target_info, uint16 target_info_len,
                                           uint16 target_name_len)
{
	uint16 pos, avid, avlen, out_len;
	RD_BOOL saw_flags;

	out_len = 0;
	saw_flags = False;
	pos = 0;
	while (pos + 4 <= target_info_len)
	{
		avid = target_info[pos] | (target_info[pos + 1] << 8);
		avlen = target_info[pos + 2] | (target_info[pos + 3] << 8);
		if (avid == NTLMSSP_AV_EOL)
			break;
		pos += 4;
		if (avlen > target_info_len - pos)
			break;
		if (avid != NTLMSSP_AV_TARGET_NAME)
		{
			out_len += 4 + avlen;
			if (avid == NTLMSSP_AV_FLAGS && avlen == 4)
				saw_flags = True;
		}
		pos += avlen;
	}

	/* MsvAvFlags, target name and EOL.
	 * Do not emit an empty MsvAvChannelBindings value: an explicitly
	 * present all-zero CBT is not equivalent to an omitted CBT.
	 */
	if (!saw_flags)
		out_len += 8;
	if (target_name_len > 0)
		out_len += 4 + target_name_len;
	out_len += 4;
	return out_len;
}

static void
ntlmssp_out_target_info_auth_av_pairs(STREAM out, uint8 *target_info, uint16 target_info_len,
                                      STREAM target_name)
{
	uint16 pos, avid, avlen;
	RD_BOOL saw_flags;

	saw_flags = False;
	pos = 0;
	while (pos + 4 <= target_info_len)
	{
		avid = target_info[pos] | (target_info[pos + 1] << 8);
		avlen = target_info[pos + 2] | (target_info[pos + 3] << 8);
		if (avid == NTLMSSP_AV_EOL)
			break;
		pos += 4;
		if (avlen > target_info_len - pos)
			break;

		if (avid != NTLMSSP_AV_TARGET_NAME)
		{
			out_uint16_le(out, avid);
			out_uint16_le(out, avlen);
			if (avid == NTLMSSP_AV_FLAGS && avlen == 4)
			{
				uint32 avflags;

				avflags = target_info[pos] | (target_info[pos + 1] << 8) |
					(target_info[pos + 2] << 16) | (target_info[pos + 3] << 24);
				out_uint32_le(out, avflags | NTLMSSP_AV_FLAGS_MIC_PRESENT);
				saw_flags = True;
			}
			else
			{
				out_uint8a(out, target_info + pos, avlen);
			}
		}
		pos += avlen;
	}

	if (!saw_flags)
	{
		out_uint16_le(out, NTLMSSP_AV_FLAGS);
		out_uint16_le(out, 4);
		out_uint32_le(out, NTLMSSP_AV_FLAGS_MIC_PRESENT);
	}

	logger(Core, Debug, "ntlmssp_build_authenticate(), channel bindings omitted");

	if (target_name && s_length(target_name) > 0)
	{
		out_uint16_le(out, NTLMSSP_AV_TARGET_NAME);
		out_uint16_le(out, s_length(target_name));
		out_stream(out, target_name);
	}

	out_uint16_le(out, NTLMSSP_AV_EOL);
	out_uint16_le(out, 0);
}


static STREAM
ntlmssp_build_blob(uint8 *target_info, uint16 target_info_len, uint8 *client_challenge,
			   STREAM target_name)
{
	STREAM blob;
	uint64 timestamp;
	uint16 av_len;

	av_len = ntlmssp_target_info_len_with_auth_av_pairs(target_info, target_info_len,
			target_name ? s_length(target_name) : 0);
	blob = s_alloc(32 + av_len);
	out_uint8(blob, 0x01);
	out_uint8(blob, 0x01);
	out_uint16_le(blob, 0);
	out_uint32_le(blob, 0);
	timestamp = ntlmssp_timestamp_now();
	out_uint64_le(blob, timestamp);
	out_uint8a(blob, client_challenge, NTLMSSP_CLIENT_CHALLENGE_SIZE);
	out_uint32_le(blob, 0);
	ntlmssp_out_target_info_auth_av_pairs(blob, target_info, target_info_len, target_name);
	s_mark_end(blob);
	s_seek(blob, 0);
	return blob;
}

static void
ntlmssp_make_keys(NTLMSSP_CONTEXT *ctx, const uint8 *exported_session_key)
{
	uint8 client_sealing_key[16];
	uint8 server_sealing_key[16];

	memcpy(ctx->exported_session_key, exported_session_key, NTLMSSP_SESSION_KEY_SIZE);
	ntlmssp_md5_key(exported_session_key, client_sign_magic, ctx->client_signing_key);
	ntlmssp_md5_key(exported_session_key, server_sign_magic, ctx->server_signing_key);
	ntlmssp_md5_key(exported_session_key, client_seal_magic, client_sealing_key);
	ntlmssp_md5_key(exported_session_key, server_seal_magic, server_sealing_key);
	rdssl_rc4_set_key(&ctx->client_seal, client_sealing_key, sizeof(client_sealing_key));
	rdssl_rc4_set_key(&ctx->server_seal, server_sealing_key, sizeof(server_sealing_key));
	ctx->send_seq = 0;
	ctx->recv_seq = 0;
	ctx->established = True;
	logger(Core, Debug, "ntlmssp_make_keys(), established send_seq=%u recv_seq=%u", ctx->send_seq,
	       ctx->recv_seq);
}

RD_BOOL
ntlmssp_build_authenticate(NTLMSSP_CONTEXT *ctx, STREAM negotiate, STREAM challenge,
				  const char *username, const char *domain,
				  const char *password, const char *server, STREAM *auth)
{
	uint32 flags, auth_flags;
	uint8 server_challenge[NTLMSSP_CHALLENGE_SIZE];
	char *target_name;
	uint8 *target_info;
	uint16 target_info_len;
	const char *effective_domain;
	uint8 response_key[16];
	uint8 client_challenge[SEC_RANDOM_SIZE];
	uint8 nt_proof[16];
	uint8 lm_hash_input[NTLMSSP_CHALLENGE_SIZE + NTLMSSP_CLIENT_CHALLENGE_SIZE];
	uint8 session_base_key[16];
	uint8 exported_session_key[16];
	uint8 encrypted_session_key[16];
	uint8 mic[NTLMSSP_MIC_SIZE];
	RDSSL_RC4 key_exchange;
	STREAM blob, lm_response, nt_response;
	STREAM domain_utf16, user_utf16, workstation_utf16, target_name_utf16;
	STREAM out;
	char *spn;
	uint32 payload_offset;
	uint16 lm_len, nt_len, domain_len, user_len, workstation_len, session_key_len;
	uint32 lm_offset, nt_offset, domain_offset, user_offset, workstation_offset, session_key_offset;
	const char *workstation;
	STREAM proof_input;

	UNUSED(server);
	*auth = NULL;
	target_name = NULL;
	target_info = NULL;
	target_name_utf16 = NULL;
	spn = NULL;
	if (!ntlmssp_parse_challenge(challenge, &flags, server_challenge, &target_name,
					   &target_info, &target_info_len))
		return False;

	logger(Core, Debug, "ntlmssp_build_authenticate(), challenge flags=0x%08x target='%s' target_info=%u",
	       flags, target_name ? target_name : "", target_info_len);
	effective_domain = (domain && domain[0]) ? domain : (target_name ? target_name : "");
	if ((!domain || !domain[0]) && target_name)
		logger(Core, Debug, "ntlmssp_build_authenticate(), using challenge target '%s' as domain",
		       target_name);

	if ((flags & NTLMSSP_NEGOTIATE_TARGET_INFO) == 0 || target_info_len == 0)
	{
		logger(Core, Error, "ntlmssp_build_authenticate(), server did not provide NTLMv2 target info");
		xfree(target_name);
		return False;
	}

	spn = xmalloc(strlen(NTLMSSP_TARGET_NAME) + 1);
	strcpy(spn, NTLMSSP_TARGET_NAME);
	target_name_utf16 = ntlmssp_string_utf16(spn);
	logger(Core, Debug, "ntlmssp_build_authenticate(), target name='%s' target_name=%u",
	       spn, (unsigned) s_length(target_name_utf16));

	generate_random(client_challenge);
	generate_random(exported_session_key);
	ntlmssp_ntowfv2(username, effective_domain, password, response_key);
	blob = ntlmssp_build_blob(target_info, target_info_len, client_challenge, target_name_utf16);

	proof_input = s_alloc(NTLMSSP_CHALLENGE_SIZE + s_length(blob));
	out_uint8a(proof_input, server_challenge, NTLMSSP_CHALLENGE_SIZE);
	out_stream(proof_input, blob);
	s_mark_end(proof_input);
	s_seek(proof_input, 0);
	ntlmssp_hmac_md5(response_key, sizeof(response_key), proof_input->data, s_length(proof_input), nt_proof);
	s_free(proof_input);

	nt_response = s_alloc(sizeof(nt_proof) + s_length(blob));
	out_uint8a(nt_response, nt_proof, sizeof(nt_proof));
	out_stream(nt_response, blob);
	s_mark_end(nt_response);
	s_seek(nt_response, 0);

	memcpy(lm_hash_input, server_challenge, NTLMSSP_CHALLENGE_SIZE);
	memcpy(lm_hash_input + NTLMSSP_CHALLENGE_SIZE, client_challenge, NTLMSSP_CLIENT_CHALLENGE_SIZE);
	lm_response = s_alloc(24);
	ntlmssp_hmac_md5(response_key, sizeof(response_key), lm_hash_input, sizeof(lm_hash_input), lm_response->p);
	lm_response->p += 16;
	out_uint8a(lm_response, client_challenge, NTLMSSP_CLIENT_CHALLENGE_SIZE);
	s_mark_end(lm_response);
	s_seek(lm_response, 0);

	ntlmssp_hmac_md5(response_key, sizeof(response_key), nt_proof, sizeof(nt_proof), session_base_key);
	rdssl_rc4_set_key(&key_exchange, session_base_key, sizeof(session_base_key));
	rdssl_rc4_crypt(&key_exchange, exported_session_key, encrypted_session_key, sizeof(encrypted_session_key));

	domain_utf16 = ntlmssp_string_utf16(effective_domain);
	user_utf16 = ntlmssp_string_utf16(username ? username : "");
	workstation = "RDESKTOP";
	workstation_utf16 = ntlmssp_string_utf16(workstation);

	lm_len = s_length(lm_response);
	nt_len = s_length(nt_response);
	domain_len = s_length(domain_utf16);
	user_len = s_length(user_utf16);
	workstation_len = s_length(workstation_utf16);
	session_key_len = sizeof(encrypted_session_key);

	payload_offset = 64 + NTLMSSP_VERSION_SIZE + NTLMSSP_MIC_SIZE;
	lm_offset = payload_offset;
	nt_offset = lm_offset + lm_len;
	domain_offset = nt_offset + nt_len;
	user_offset = domain_offset + domain_len;
	workstation_offset = user_offset + user_len;
	session_key_offset = workstation_offset + workstation_len;

	auth_flags = flags & NTLMSSP_NEGOTIATE_AUTH_FLAGS;
	auth_flags |= NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_KEY_EXCH |
		NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_SEAL |
		NTLMSSP_NEGOTIATE_WORKSTATION_SUPPLIED |
		NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY |
		NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_56 |
		NTLMSSP_NEGOTIATE_VERSION;
	logger(Core, Debug, "ntlmssp_build_authenticate(), auth flags=0x%08x lm=%u nt=%u domain=%u user=%u workstation=%u",
	       auth_flags, lm_len, nt_len, domain_len, user_len, workstation_len);
	logger(Core, Debug,
	       "ntlmssp_build_authenticate(), offsets payload=%u lm=%u nt=%u domain=%u user=%u workstation=%u session_key=%u total=%u",
	       payload_offset, lm_offset, nt_offset, domain_offset, user_offset,
	       workstation_offset, session_key_offset, session_key_offset + session_key_len);

	out = s_alloc(session_key_offset + session_key_len);
	out_uint8a(out, ntlmssp_sig, sizeof(ntlmssp_sig));
	out_uint32_le(out, 3);
	ntlmssp_write_security_buffer(out, lm_len, lm_offset);
	ntlmssp_write_security_buffer(out, nt_len, nt_offset);
	ntlmssp_write_security_buffer(out, domain_len, domain_offset);
	ntlmssp_write_security_buffer(out, user_len, user_offset);
	ntlmssp_write_security_buffer(out, workstation_len, workstation_offset);
	ntlmssp_write_security_buffer(out, session_key_len, session_key_offset);
	out_uint32_le(out, auth_flags);
	ntlmssp_out_version(out);
	out_uint8s(out, NTLMSSP_MIC_SIZE);
	out_stream(out, lm_response);
	out_stream(out, nt_response);
	out_stream(out, domain_utf16);
	out_stream(out, user_utf16);
	out_stream(out, workstation_utf16);
	out_uint8a(out, encrypted_session_key, sizeof(encrypted_session_key));
	s_mark_end(out);
	s_seek(out, 0);

	if (negotiate == NULL)
	{
		s_free(out);
		out = NULL;
		goto fail;
	}
	ntlmssp_debug_bytes("ntlmssp_build_authenticate(), type3-zero-mic", out->data, s_length(out));
	{
		STREAM mic_input;

		mic_input = s_alloc(s_length(negotiate) + s_length(challenge) + s_length(out));
		out_uint8a(mic_input, negotiate->data, s_length(negotiate));
		out_uint8a(mic_input, challenge->data, s_length(challenge));
		out_uint8a(mic_input, out->data, s_length(out));
		s_mark_end(mic_input);
		ntlmssp_hmac_md5(exported_session_key, sizeof(exported_session_key), mic_input->data,
			       s_length(mic_input), mic);
		s_free(mic_input);
		memcpy(out->data + NTLMSSP_MIC_OFFSET, mic, sizeof(mic));
		logger(Core, Debug, "ntlmssp_build_authenticate(), MIC present len=%u",
		       NTLMSSP_MIC_SIZE);
		ntlmssp_debug_bytes("ntlmssp_build_authenticate(), mic", mic, sizeof(mic));
		ntlmssp_debug_bytes("ntlmssp_build_authenticate(), type3-final", out->data, s_length(out));
	}
	ntlmssp_make_keys(ctx, exported_session_key);
	*auth = out;

	s_free(blob);
	s_free(lm_response);
	s_free(nt_response);
	s_free(domain_utf16);
	s_free(user_utf16);
	s_free(workstation_utf16);
	s_free(target_name_utf16);
	xfree(spn);
	xfree(target_info);
	xfree(target_name);
	return True;

fail:
	s_free(blob);
	s_free(lm_response);
	s_free(nt_response);
	s_free(domain_utf16);
	s_free(user_utf16);
	s_free(workstation_utf16);
	s_free(target_name_utf16);
	xfree(spn);
	xfree(target_info);
	xfree(target_name);
	return False;
}

static void
ntlmssp_out_uint32_le(uint8 *out, uint32 value)
{
	out[0] = value & 0xff;
	out[1] = (value >> 8) & 0xff;
	out[2] = (value >> 16) & 0xff;
	out[3] = (value >> 24) & 0xff;
}

static void
ntlmssp_checksum(const uint8 *signing_key, uint32 seq, const uint8 *message,
			 size_t message_len, uint8 *checksum)
{
	STREAM tmp;

	tmp = s_alloc(4 + message_len);
	out_uint32_le(tmp, seq);
	if (message_len)
		out_uint8a(tmp, message, message_len);
	s_mark_end(tmp);
	ntlmssp_hmac_md5(signing_key, 16, tmp->data, s_length(tmp), checksum);
	s_free(tmp);
}

static void
ntlmssp_make_signature(uint8 *signature, const uint8 *signing_key, RDSSL_RC4 *seal,
			      uint32 seq, const uint8 *message, size_t message_len)
{
	uint8 checksum[16];

	ntlmssp_checksum(signing_key, seq, message, message_len, checksum);
	memset(signature, 0, NTLMSSP_SIGNATURE_SIZE);
	ntlmssp_out_uint32_le(signature, 1);
	rdssl_rc4_crypt(seal, checksum, signature + 4, 8);
	ntlmssp_out_uint32_le(signature + 12, seq);
}

STREAM
ntlmssp_wrap(NTLMSSP_CONTEXT *ctx, STREAM plain)
{
	STREAM out;
	uint8 signature[NTLMSSP_SIGNATURE_SIZE];
	size_t len;

	if (!ctx || !ctx->established || !plain)
		return NULL;

	len = s_length(plain);
	logger(Core, Debug, "ntlmssp_wrap(), seq=%u plain=%u", ctx->send_seq, (unsigned) len);
	ntlmssp_debug_bytes("ntlmssp_wrap(), plain", plain->data, len);
	out = s_alloc(NTLMSSP_SIGNATURE_SIZE + len);
	/*
	 * GSS_WrapEx/NTLM SEAL consumes the RC4 sealing stream for the payload
	 * before KEY_EXCH encrypts the checksum with the same RC4 handle.
	 */
	out_uint8s(out, NTLMSSP_SIGNATURE_SIZE);
	if (len)
		rdssl_rc4_crypt(&ctx->client_seal, plain->data, out->p, len);
	if (len)
		ntlmssp_debug_bytes("ntlmssp_wrap(), sealed-payload", out->p, len);
	out->p += len;

	ntlmssp_make_signature(signature, ctx->client_signing_key, &ctx->client_seal,
			     ctx->send_seq, plain->data, len);
	ntlmssp_debug_bytes("ntlmssp_wrap(), signature-after-seal", signature, sizeof(signature));
	memcpy(out->data, signature, sizeof(signature));
	s_mark_end(out);
	s_seek(out, 0);
	logger(Core, Debug, "ntlmssp_wrap(), wrapped=%u next_seq=%u", (unsigned) s_length(out),
	       ctx->send_seq + 1);
	ctx->send_seq++;
	return out;
}

STREAM
ntlmssp_unwrap(NTLMSSP_CONTEXT *ctx, STREAM wrapped)
{
	STREAM out;
	uint8 version[4];
	uint8 encrypted_checksum[8];
	uint8 decrypted_checksum[8];
	uint8 expected_checksum[16];
	uint32 seq;
	size_t len;

	if (!ctx || !ctx->established || !wrapped || s_length(wrapped) < NTLMSSP_SIGNATURE_SIZE)
		return NULL;

	s_seek(wrapped, 0);
	in_uint8a(wrapped, version, 4);
	if (version[0] != 1 || version[1] != 0 || version[2] != 0 || version[3] != 0)
		return NULL;
	in_uint8a(wrapped, encrypted_checksum, sizeof(encrypted_checksum));
	in_uint32_le(wrapped, seq);
	logger(Core, Debug, "ntlmssp_unwrap(), wrapped=%u seq=%u expected=%u", (unsigned) s_length(wrapped),
	       seq, ctx->recv_seq);
	if (seq != ctx->recv_seq)
	{
		logger(Core, Error, "ntlmssp_unwrap(), sequence mismatch got=%u expected=%u", seq,
		       ctx->recv_seq);
		return NULL;
	}
	ntlmssp_debug_bytes("ntlmssp_unwrap(), encrypted-checksum", encrypted_checksum,
	                    sizeof(encrypted_checksum));
	len = s_remaining(wrapped);
	out = s_alloc(len);
	if (len)
		rdssl_rc4_crypt(&ctx->server_seal, wrapped->p, out->p, len);
	out->p += len;
	s_mark_end(out);
	s_seek(out, 0);

	ntlmssp_debug_bytes("ntlmssp_unwrap(), plain", out->data, len);
	rdssl_rc4_crypt(&ctx->server_seal, encrypted_checksum, decrypted_checksum, sizeof(decrypted_checksum));
	ntlmssp_debug_bytes("ntlmssp_unwrap(), decrypted-checksum-after-unseal", decrypted_checksum,
	                    sizeof(decrypted_checksum));
	ntlmssp_checksum(ctx->server_signing_key, ctx->recv_seq, out->data, len, expected_checksum);
	ntlmssp_debug_bytes("ntlmssp_unwrap(), expected-checksum", expected_checksum,
	                    sizeof(decrypted_checksum));
	if (memcmp(decrypted_checksum, expected_checksum, sizeof(decrypted_checksum)) != 0)
	{
		logger(Core, Error, "ntlmssp_unwrap(), checksum mismatch");
		s_free(out);
		return NULL;
	}

	ctx->recv_seq++;
	return out;
}
