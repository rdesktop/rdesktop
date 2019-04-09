/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - RDP encryption and licensing
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright 2005-2011 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright 2017-2018 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

#include "rdesktop.h"
#include "ssl.h"

extern char g_hostname[16];
extern uint32 g_requested_session_width;
extern uint32 g_requested_session_height;
extern int g_dpi;
extern unsigned int g_keylayout;
extern int g_keyboard_type;
extern int g_keyboard_subtype;
extern int g_keyboard_functionkeys;
extern RD_BOOL g_encryption;
extern RD_BOOL g_licence_issued;
extern RD_BOOL g_licence_error_result;
extern RDP_VERSION g_rdp_version;
extern RD_BOOL g_console_session;
extern uint32 g_redirect_session_id;
extern int g_server_depth;
extern VCHANNEL g_channels[];
extern unsigned int g_num_channels;
extern uint8 g_client_random[SEC_RANDOM_SIZE];

static int g_rc4_key_len;
static RDSSL_RC4 g_rc4_decrypt_key;
static RDSSL_RC4 g_rc4_encrypt_key;
static uint32 g_server_public_key_len;

static uint8 g_sec_sign_key[16];
static uint8 g_sec_decrypt_key[16];
static uint8 g_sec_encrypt_key[16];
static uint8 g_sec_decrypt_update_key[16];
static uint8 g_sec_encrypt_update_key[16];
static uint8 g_sec_crypted_random[SEC_MAX_MODULUS_SIZE];

uint16 g_server_rdp_version = 0;

/* These values must be available to reset state - Session Directory */
static int g_sec_encrypt_use_count = 0;
static int g_sec_decrypt_use_count = 0;

/*
 * I believe this is based on SSLv3 with the following differences:
 *  MAC algorithm (5.2.3.1) uses only 32-bit length in place of seq_num/type/length fields
 *  MAC algorithm uses SHA1 and MD5 for the two hash functions instead of one or other
 *  key_block algorithm (6.2.2) uses 'X', 'YY', 'ZZZ' instead of 'A', 'BB', 'CCC'
 *  key_block partitioning is different (16 bytes each: MAC secret, decrypt key, encrypt key)
 *  encryption/decryption keys updated every 4096 packets
 * See http://wp.netscape.com/eng/ssl3/draft302.txt
 */

/*
 * 48-byte transformation used to generate master secret (6.1) and key material (6.2.2).
 * Both SHA1 and MD5 algorithms are used.
 */
void
sec_hash_48(uint8 * out, uint8 * in, uint8 * salt1, uint8 * salt2, uint8 salt)
{
	uint8 shasig[20];
	uint8 pad[4];
	RDSSL_SHA1 sha1;
	RDSSL_MD5 md5;
	int i;

	for (i = 0; i < 3; i++)
	{
		memset(pad, salt + i, i + 1);

		rdssl_sha1_init(&sha1);
		rdssl_sha1_update(&sha1, pad, i + 1);
		rdssl_sha1_update(&sha1, in, 48);
		rdssl_sha1_update(&sha1, salt1, 32);
		rdssl_sha1_update(&sha1, salt2, 32);
		rdssl_sha1_final(&sha1, shasig);

		rdssl_md5_init(&md5);
		rdssl_md5_update(&md5, in, 48);
		rdssl_md5_update(&md5, shasig, 20);
		rdssl_md5_final(&md5, &out[i * 16]);
	}
}

/*
 * 16-byte transformation used to generate export keys (6.2.2).
 */
void
sec_hash_16(uint8 * out, uint8 * in, uint8 * salt1, uint8 * salt2)
{
	RDSSL_MD5 md5;

	rdssl_md5_init(&md5);
	rdssl_md5_update(&md5, in, 16);
	rdssl_md5_update(&md5, salt1, 32);
	rdssl_md5_update(&md5, salt2, 32);
	rdssl_md5_final(&md5, out);
}

/*
 * 16-byte sha1 hash
 */
void
sec_hash_sha1_16(uint8 * out, uint8 * in, uint8 * salt1)
{
	RDSSL_SHA1 sha1;
	rdssl_sha1_init(&sha1);
	rdssl_sha1_update(&sha1, in, 16);
	rdssl_sha1_update(&sha1, salt1, 16);
	rdssl_sha1_final(&sha1, out);
}

/* create string from hash */
void
sec_hash_to_string(char *out, int out_size, uint8 * in, int in_size)
{
	int k;
	memset(out, 0, out_size);
	for (k = 0; k < in_size; k++, out += 2)
	{
		sprintf(out, "%.2x", in[k]);
	}
}

/* Reduce key entropy from 64 to 40 bits */
static void
sec_make_40bit(uint8 * key)
{
	key[0] = 0xd1;
	key[1] = 0x26;
	key[2] = 0x9e;
}

/* Generate encryption keys given client and server randoms */
static void
sec_generate_keys(uint8 * client_random, uint8 * server_random, int rc4_key_size)
{
	uint8 pre_master_secret[48];
	uint8 master_secret[48];
	uint8 key_block[48];

	/* Construct pre-master secret */
	memcpy(pre_master_secret, client_random, 24);
	memcpy(pre_master_secret + 24, server_random, 24);

	/* Generate master secret and then key material */
	sec_hash_48(master_secret, pre_master_secret, client_random, server_random, 'A');
	sec_hash_48(key_block, master_secret, client_random, server_random, 'X');

	/* First 16 bytes of key material is MAC secret */
	memcpy(g_sec_sign_key, key_block, 16);

	/* Generate export keys from next two blocks of 16 bytes */
	sec_hash_16(g_sec_decrypt_key, &key_block[16], client_random, server_random);
	sec_hash_16(g_sec_encrypt_key, &key_block[32], client_random, server_random);

	if (rc4_key_size == 1)
	{
		logger(Protocol, Debug, "sec_generate_keys(), 40-bit encryption enabled");
		sec_make_40bit(g_sec_sign_key);
		sec_make_40bit(g_sec_decrypt_key);
		sec_make_40bit(g_sec_encrypt_key);
		g_rc4_key_len = 8;
	}
	else
	{
		logger(Protocol, Debug,
		       "sec_generate_key(), rc_4_key_size == %d, 128-bit encryption enabled",
		       rc4_key_size);
		g_rc4_key_len = 16;
	}

	/* Save initial RC4 keys as update keys */
	memcpy(g_sec_decrypt_update_key, g_sec_decrypt_key, 16);
	memcpy(g_sec_encrypt_update_key, g_sec_encrypt_key, 16);

	/* Initialise RC4 state arrays */
	rdssl_rc4_set_key(&g_rc4_decrypt_key, g_sec_decrypt_key, g_rc4_key_len);
	rdssl_rc4_set_key(&g_rc4_encrypt_key, g_sec_encrypt_key, g_rc4_key_len);
}

static uint8 pad_54[40] = {
	54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
	54, 54, 54,
	54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
	54, 54, 54
};

static uint8 pad_92[48] = {
	92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92,
	92, 92, 92, 92, 92, 92, 92,
	92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92,
	92, 92, 92, 92, 92, 92, 92
};

/* Output a uint32 into a buffer (little-endian) */
void
buf_out_uint32(uint8 * buffer, uint32 value)
{
	buffer[0] = (value) & 0xff;
	buffer[1] = (value >> 8) & 0xff;
	buffer[2] = (value >> 16) & 0xff;
	buffer[3] = (value >> 24) & 0xff;
}

/* Generate a MAC hash (5.2.3.1), using a combination of SHA1 and MD5 */
void
sec_sign(uint8 * signature, int siglen, uint8 * session_key, int keylen, uint8 * data, int datalen)
{
	uint8 shasig[20];
	uint8 md5sig[16];
	uint8 lenhdr[4];
	RDSSL_SHA1 sha1;
	RDSSL_MD5 md5;

	buf_out_uint32(lenhdr, datalen);

	rdssl_sha1_init(&sha1);
	rdssl_sha1_update(&sha1, session_key, keylen);
	rdssl_sha1_update(&sha1, pad_54, 40);
	rdssl_sha1_update(&sha1, lenhdr, 4);
	rdssl_sha1_update(&sha1, data, datalen);
	rdssl_sha1_final(&sha1, shasig);

	rdssl_md5_init(&md5);
	rdssl_md5_update(&md5, session_key, keylen);
	rdssl_md5_update(&md5, pad_92, 48);
	rdssl_md5_update(&md5, shasig, 20);
	rdssl_md5_final(&md5, md5sig);

	memcpy(signature, md5sig, siglen);
}

/* Update an encryption key */
static void
sec_update(uint8 * key, uint8 * update_key)
{
	uint8 shasig[20];
	RDSSL_SHA1 sha1;
	RDSSL_MD5 md5;
	RDSSL_RC4 update;

	rdssl_sha1_init(&sha1);
	rdssl_sha1_update(&sha1, update_key, g_rc4_key_len);
	rdssl_sha1_update(&sha1, pad_54, 40);
	rdssl_sha1_update(&sha1, key, g_rc4_key_len);
	rdssl_sha1_final(&sha1, shasig);

	rdssl_md5_init(&md5);
	rdssl_md5_update(&md5, update_key, g_rc4_key_len);
	rdssl_md5_update(&md5, pad_92, 48);
	rdssl_md5_update(&md5, shasig, 20);
	rdssl_md5_final(&md5, key);

	rdssl_rc4_set_key(&update, key, g_rc4_key_len);
	rdssl_rc4_crypt(&update, key, key, g_rc4_key_len);

	if (g_rc4_key_len == 8)
		sec_make_40bit(key);
}

/* Encrypt data using RC4 */
static void
sec_encrypt(uint8 * data, int length)
{
	if (g_sec_encrypt_use_count == 4096)
	{
		sec_update(g_sec_encrypt_key, g_sec_encrypt_update_key);
		rdssl_rc4_set_key(&g_rc4_encrypt_key, g_sec_encrypt_key, g_rc4_key_len);
		g_sec_encrypt_use_count = 0;
	}

	rdssl_rc4_crypt(&g_rc4_encrypt_key, data, data, length);
	g_sec_encrypt_use_count++;
}

/* Decrypt data using RC4 */
void
sec_decrypt(uint8 * data, int length)
{
	if (length <= 0)
		return;

	if (g_sec_decrypt_use_count == 4096)
	{
		sec_update(g_sec_decrypt_key, g_sec_decrypt_update_key);
		rdssl_rc4_set_key(&g_rc4_decrypt_key, g_sec_decrypt_key, g_rc4_key_len);
		g_sec_decrypt_use_count = 0;
	}

	rdssl_rc4_crypt(&g_rc4_decrypt_key, data, data, length);
	g_sec_decrypt_use_count++;
}

/* Perform an RSA public key encryption operation */
static void
sec_rsa_encrypt(uint8 * out, uint8 * in, int len, uint32 modulus_size, uint8 * modulus,
		uint8 * exponent)
{
	rdssl_rsa_encrypt(out, in, len, modulus_size, modulus, exponent);
}

/* Initialise secure transport packet */
STREAM
sec_init(uint32 flags, int maxlen)
{
	int hdrlen;
	STREAM s;

	if (!g_licence_issued && !g_licence_error_result)
		hdrlen = (flags & SEC_ENCRYPT) ? 12 : 4;
	else
		hdrlen = (flags & SEC_ENCRYPT) ? 12 : 0;
	s = mcs_init(maxlen + hdrlen);
	s_push_layer(s, sec_hdr, hdrlen);

	return s;
}

/* Transmit secure transport packet over specified channel */
void
sec_send_to_channel(STREAM s, uint32 flags, uint16 channel)
{
	int datalen;

#ifdef WITH_SCARD
	scard_lock(SCARD_LOCK_SEC);
#endif

	s_pop_layer(s, sec_hdr);
	if ((!g_licence_issued && !g_licence_error_result) || (flags & SEC_ENCRYPT))
		out_uint32_le(s, flags);

	if (flags & SEC_ENCRYPT)
	{
		unsigned char *data;
		flags &= ~SEC_ENCRYPT;
		datalen = s_remaining(s) - 8;
		inout_uint8p(s, data, datalen + 8);
		sec_sign(data, 8, g_sec_sign_key, g_rc4_key_len, data + 8, datalen);
		sec_encrypt(data + 8, datalen);
	}

	mcs_send_to_channel(s, channel);

#ifdef WITH_SCARD
	scard_unlock(SCARD_LOCK_SEC);
#endif
}

/* Transmit secure transport packet */

void
sec_send(STREAM s, uint32 flags)
{
	sec_send_to_channel(s, flags, MCS_GLOBAL_CHANNEL);
}


/* Transfer the client random to the server */
static void
sec_establish_key(void)
{
	uint32 length = g_server_public_key_len + SEC_PADDING_SIZE;
	uint32 flags = SEC_EXCHANGE_PKT;
	STREAM s;

	s = sec_init(flags, length + 4);

	out_uint32_le(s, length);
	out_uint8a(s, g_sec_crypted_random, g_server_public_key_len);
	out_uint8s(s, SEC_PADDING_SIZE);

	s_mark_end(s);
	sec_send(s, flags);
	s_free(s);
}

/* Output connect initial data blob */
static void
sec_out_mcs_connect_initial_pdu(STREAM s, uint32 selected_protocol)
{
	int length = 162 + 76 + 12 + 4 + (g_dpi > 0 ? 18 : 0);
	unsigned int i;
	uint32 rdpversion = RDP_40;
	uint16 capflags = RNS_UD_CS_SUPPORT_ERRINFO_PDU;
	uint16 colorsupport = RNS_UD_24BPP_SUPPORT | RNS_UD_16BPP_SUPPORT | RNS_UD_32BPP_SUPPORT;
	uint32 physwidth, physheight, desktopscale, devicescale;

	logger(Protocol, Debug, "%s()", __func__);

	if (g_rdp_version >= RDP_V5)
		rdpversion = RDP_50;

	if (g_num_channels > 0)
		length += g_num_channels * 12 + 8;

	/* Generic Conference Control (T.124) ConferenceCreateRequest */
	out_uint16_be(s, 5);
	out_uint16_be(s, 0x14);
	out_uint8(s, 0x7c);
	out_uint16_be(s, 1);

	out_uint16_be(s, (length | 0x8000));	/* remaining length */

	out_uint16_be(s, 8);	/* length? */
	out_uint16_be(s, 16);
	out_uint8(s, 0);
	out_uint16_le(s, 0xc001);
	out_uint8(s, 0);

	out_uint32_le(s, 0x61637544);	/* OEM ID: "Duca", as in Ducati. */
	out_uint16_be(s, ((length - 14) | 0x8000));	/* remaining length */

	/* Client information (TS_UD_CS_CORE) */
	out_uint16_le(s, CS_CORE);	/* type */
	out_uint16_le(s, 216 + (g_dpi > 0 ? 18 : 0));	/* length */
	out_uint32_le(s, rdpversion);	/* version */
	out_uint16_le(s, g_requested_session_width);	/* desktopWidth */
	out_uint16_le(s, g_requested_session_height);	/* desktopHeight */
	out_uint16_le(s, RNS_UD_COLOR_8BPP);	/* colorDepth */
	out_uint16_le(s, RNS_UD_SAS_DEL);	/* SASSequence */
	out_uint32_le(s, g_keylayout);		/* keyboardLayout */
	/*
	 * According to s.1.7 of MS-RDPESC if the build number is at least 4,304,
	 * SCREDIR_VERSION_LONGHORN is assumed; otherwise SCREDIR_VERSIONXP is to be used
	 */
	out_uint32_le(s, 2600);			/* Client build. We are now 2600 compatible :-) */

	/* Unicode name of client, padded to 32 bytes */
	out_utf16s_padded(s, g_hostname, 32, 0x00);

	out_uint32_le(s, g_keyboard_type);	/* keyboardType */
	out_uint32_le(s, g_keyboard_subtype);	/* keyboardSubtype */
	out_uint32_le(s, g_keyboard_functionkeys);	/* keyboardFunctionKey */
	out_uint8s(s, 64);	/* imeFileName */
	out_uint16_le(s, RNS_UD_COLOR_8BPP);	/* postBeta2ColorDepth (overrides colorDepth) */
	out_uint16_le(s, 1);	/* clientProductId (should be 1) */
	out_uint32_le(s, 0);	/* serialNumber (should be 0) */

	/* highColorDepth (overrides postBeta2ColorDepth). Capped at 24BPP.
	   To get 32BPP sessions, we need to set a capability flag. */
	out_uint16_le(s, MIN(g_server_depth, 24));
	if (g_server_depth == 32)
		capflags |= RNS_UD_CS_WANT_32BPP_SESSION;

	out_uint16_le(s, colorsupport);	/* supportedColorDepths */
	out_uint16_le(s, capflags);	/* earlyCapabilityFlags */
	out_uint8s(s, 64);	/* clientDigProductId */
	out_uint8(s, 0);	/* connectionType */
	out_uint8(s, 0);	/* pad */
	out_uint32_le(s, selected_protocol);	/* serverSelectedProtocol */
	if (g_dpi > 0)
	{
		/* Extended client info describing monitor geometry */
		utils_calculate_dpi_scale_factors(g_requested_session_width,
						  g_requested_session_height, g_dpi, &physwidth,
						  &physheight, &desktopscale, &devicescale);
		out_uint32_le(s, physwidth);	/* physicalwidth */
		out_uint32_le(s, physheight);	/* physicalheight */
		out_uint16_le(s, ORIENTATION_LANDSCAPE);	/* Orientation */
		out_uint32_le(s, desktopscale);	/* DesktopScaleFactor */
		out_uint32_le(s, devicescale);	/* DeviceScaleFactor */
	}

	/* Write a Client Cluster Data (TS_UD_CS_CLUSTER) */
	uint32 cluster_flags = 0;
	out_uint16_le(s, CS_CLUSTER);	/* header.type */
	out_uint16_le(s, 12);	/* length */

	cluster_flags |= SEC_CC_REDIRECTION_SUPPORTED;
	cluster_flags |= (SEC_CC_REDIRECT_VERSION_4 << 2);

	if (g_console_session || g_redirect_session_id != 0)
		cluster_flags |= SEC_CC_REDIRECT_SESSIONID_FIELD_VALID;

	out_uint32_le(s, cluster_flags);
	out_uint32(s, g_redirect_session_id);

	/* Client encryption settings (TS_UD_CS_SEC) */
	out_uint16_le(s, CS_SECURITY);	/* type */
	out_uint16_le(s, 12);	/* length */
	out_uint32_le(s, g_encryption ? 0x3 : 0);	/* encryptionMethods */
	out_uint32(s, 0);	/* extEncryptionMethods */

	/* Channel definitions (TS_UD_CS_NET) */
	logger(Protocol, Debug, "sec_out_mcs_data(), g_num_channels is %d", g_num_channels);
	if (g_num_channels > 0)
	{
		out_uint16_le(s, CS_NET);	/* type */
		out_uint16_le(s, g_num_channels * 12 + 8);	/* length */
		out_uint32_le(s, g_num_channels);	/* number of virtual channels */
		for (i = 0; i < g_num_channels; i++)
		{
			logger(Protocol, Debug, "sec_out_mcs_data(), requesting channel %s",
			       g_channels[i].name);
			out_uint8a(s, g_channels[i].name, 8);
			out_uint32_be(s, g_channels[i].flags);
		}
	}

	s_mark_end(s);
}

/* Parse a public key structure */
static RD_BOOL
sec_parse_public_key(STREAM s, uint8 * modulus, uint8 * exponent)
{
	uint32 magic, modulus_len;

	if (!s_check_rem(s, 8)) {
		return False;
	}

	in_uint32_le(s, magic);
	if (magic != SEC_RSA_MAGIC)
	{
		logger(Protocol, Error, "sec_parse_public_key(), magic (0x%x) != SEC_RSA_MAGIC",
		       magic);
		return False;
	}

	in_uint32_le(s, modulus_len);
	modulus_len -= SEC_PADDING_SIZE;
	if ((modulus_len < SEC_MODULUS_SIZE) || (modulus_len > SEC_MAX_MODULUS_SIZE))
	{
		logger(Protocol, Error,
		       "sec_parse_public_key(), invalid public key size (%u bits) from server",
		       modulus_len * 8);
		return False;
	}

	if (!s_check_rem(s, 1 + SEC_EXPONENT_SIZE + modulus_len + SEC_PADDING_SIZE)) {
		return False;
	}

	in_uint8s(s, 8);	/* modulus_bits, unknown */
	in_uint8a(s, exponent, SEC_EXPONENT_SIZE);
	in_uint8a(s, modulus, modulus_len);
	in_uint8s(s, SEC_PADDING_SIZE);
	g_server_public_key_len = modulus_len;

	return True;
}

/* Parse a public signature structure */
static RD_BOOL
sec_parse_public_sig(STREAM s, uint32 len, uint8 * modulus, uint8 * exponent)
{
	uint8 signature[SEC_MAX_MODULUS_SIZE];
	uint32 sig_len;

	if (len != 72)
	{
		return True;
	}
	memset(signature, 0, sizeof(signature));
	sig_len = len - 8;
	in_uint8a(s, signature, sig_len);
	return rdssl_sig_ok(exponent, SEC_EXPONENT_SIZE, modulus, g_server_public_key_len,
			    signature, sig_len);
}

/* Parse a crypto information structure */
static RD_BOOL
sec_parse_crypt_info(STREAM s, uint32 * rc4_key_size,
		     uint8 ** server_random, uint8 * modulus, uint8 * exponent)
{
	uint32 crypt_level, random_len, rsa_info_len;
	uint32 cacert_len, cert_len, flags;
	RDSSL_CERT *cacert, *server_cert;
	RDSSL_RKEY *server_public_key;
	uint16 tag, length;
	size_t next_tag;

	logger(Protocol, Debug, "%s()", __func__);

	in_uint32_le(s, *rc4_key_size);	/* 1 = 40-bit, 2 = 128-bit */
	in_uint32_le(s, crypt_level);	/* 1 = low, 2 = medium, 3 = high */
	if (crypt_level == 0)
	{
		/* no encryption */
		logger(Protocol, Debug, "sec_parse_crypt_info(), got ENCRYPTION_LEVEL_NONE");
		return False;
	}

	in_uint32_le(s, random_len);
	in_uint32_le(s, rsa_info_len);

	if (random_len != SEC_RANDOM_SIZE)
	{
		logger(Protocol, Error, "sec_parse_crypt_info(), got random len %d, expected %d",
		       random_len, SEC_RANDOM_SIZE);
		return False;
	}

	in_uint8p(s, *server_random, random_len);

	/* RSA info */
	if (!s_check_rem(s, rsa_info_len))
	{
		logger(Protocol, Error, "sec_parse_crypt_info(), !s_check_rem(s, rsa_info_len)");
		return False;
	}

	in_uint32_le(s, flags);	/* 1 = RDP4-style, 0x80000002 = X.509 */
	if (flags & 1)
	{
		logger(Protocol, Debug,
		       "sec_parse_crypt_info(), We're going for the RDP4-style encryption");
		in_uint8s(s, 8);	/* unknown */

		while (!s_check_end(s))
		{
			in_uint16_le(s, tag);
			in_uint16_le(s, length);

			next_tag = s_tell(s) + length;

			switch (tag)
			{
				case SEC_TAG_PUBKEY:
					if (!sec_parse_public_key(s, modulus, exponent))
					{
						logger(Protocol, Error,
						       "sec_parse_crypt_info(), invalid public key");
						return False;
					}
					logger(Protocol, Debug,
					       "sec_parse_crypt_info(), got public key");

					break;

				case SEC_TAG_KEYSIG:
					if (!sec_parse_public_sig(s, length, modulus, exponent))
					{
						logger(Protocol, Error,
						       "sec_parse_crypt_info(), invalid public sig");
						return False;
					}
					break;

				default:
					logger(Protocol, Warning,
					       "sec_parse_crypt_info(), unhandled crypt tag 0x%x",
					       tag);
			}

			s_seek(s, next_tag);
		}
	}
	else
	{
		uint32 certcount;
		unsigned char *certdata;

		logger(Protocol, Debug,
		       "sec_parse_crypt_info(), We're going for the RDP5-style encryption");
		in_uint32_le(s, certcount);	/* Number of certificates */
		if (certcount < 2)
		{
			logger(Protocol, Error,
			       "sec_parse_crypt_info(), server didn't send enough x509 certificates");
			return False;
		}
		for (; certcount > 2; certcount--)
		{		/* ignore all the certificates between the root and the signing CA */
			uint32 ignorelen;
			RDSSL_CERT *ignorecert;
			unsigned char *ignoredata;

			in_uint32_le(s, ignorelen);
			in_uint8p(s, ignoredata, ignorelen);
			ignorecert = rdssl_cert_read(ignoredata, ignorelen);
			if (ignorecert == NULL)
			{	/* XXX: error out? */
				logger(Protocol, Error,
				       "sec_parse_crypt_info(), got a bad cert: this will probably screw up the rest of the communication");
			}
			else
			{
				rdssl_cert_free(ignorecert);
			}
		}
		/* Do da funky X.509 stuffy

		   "How did I find out about this?  I looked up and saw a
		   bright light and when I came to I had a scar on my forehead
		   and knew about X.500"
		   - Peter Gutman in a early version of 
		   http://www.cs.auckland.ac.nz/~pgut001/pubs/x509guide.txt
		 */
		in_uint32_le(s, cacert_len);
		in_uint8p(s, certdata, cacert_len);
		logger(Protocol, Debug,
		       "sec_parse_crypt_info(), server CA Certificate length is %d", cacert_len);
		cacert = rdssl_cert_read(certdata, cacert_len);
		if (NULL == cacert)
		{
			logger(Protocol, Error,
			       "sec_parse_crypt_info(), couldn't load CA Certificate from server");
			return False;
		}
		in_uint32_le(s, cert_len);
		in_uint8p(s, certdata, cert_len);
		logger(Protocol, Debug, "sec_parse_crypt_info(), certificate length is %d",
		       cert_len);
		server_cert = rdssl_cert_read(certdata, cert_len);
		if (NULL == server_cert)
		{
			rdssl_cert_free(cacert);
			logger(Protocol, Error,
			       "sec_parse_crypt_info(), couldn't load Certificate from server");
			return False;
		}
		if (!rdssl_certs_ok(server_cert, cacert))
		{
			rdssl_cert_free(server_cert);
			rdssl_cert_free(cacert);
			logger(Protocol, Error,
			       "sec_parse_crypt_info(), security error, CA Certificate invalid");
			return False;
		}
		rdssl_cert_free(cacert);
		in_uint8s(s, 16);	/* Padding */
		server_public_key = rdssl_cert_to_rkey(server_cert, &g_server_public_key_len);
		if (NULL == server_public_key)
		{
			logger(Protocol, Debug,
			       "sec_parse_crypt_info(). failed to parse X509 correctly");
			rdssl_cert_free(server_cert);
			return False;
		}
		rdssl_cert_free(server_cert);
		if ((g_server_public_key_len < SEC_MODULUS_SIZE) ||
		    (g_server_public_key_len > SEC_MAX_MODULUS_SIZE))
		{
			logger(Protocol, Error,
			       "sec_parse_crypt_info(), bad server public key size (%u bits)",
			       g_server_public_key_len * 8);
			rdssl_rkey_free(server_public_key);
			return False;
		}
		if (rdssl_rkey_get_exp_mod(server_public_key, exponent, SEC_EXPONENT_SIZE,
					   modulus, SEC_MAX_MODULUS_SIZE) != 0)
		{
			logger(Protocol, Error,
			       "sec_parse_crypt_info(), problem extracting RSA exponent, modulus");
			rdssl_rkey_free(server_public_key);
			return False;
		}
		rdssl_rkey_free(server_public_key);
		return True;	/* There's some garbage here we don't care about */
	}
	return s_check_end(s);
}

/* Process crypto information blob */
static void
sec_process_crypt_info(STREAM s)
{
	uint8 *server_random = NULL;
	uint8 modulus[SEC_MAX_MODULUS_SIZE];
	uint8 exponent[SEC_EXPONENT_SIZE];
	uint32 rc4_key_size;

	logger(Protocol, Debug, "%s()", __func__);

	memset(modulus, 0, sizeof(modulus));
	memset(exponent, 0, sizeof(exponent));
	if (!sec_parse_crypt_info(s, &rc4_key_size, &server_random, modulus, exponent))
		return;

	logger(Protocol, Debug, "sec_parse_crypt_info(), generating client random");
	generate_random(g_client_random);
	sec_rsa_encrypt(g_sec_crypted_random, g_client_random, SEC_RANDOM_SIZE,
			g_server_public_key_len, modulus, exponent);
	sec_generate_keys(g_client_random, server_random, rc4_key_size);
}


/* Process SRV_INFO, find RDP version supported by server */
static void
sec_process_srv_info(STREAM s)
{
	in_uint16_le(s, g_server_rdp_version);
	logger(Protocol, Debug, "sec_process_srv_info(), server RDP version is %d",
	       g_server_rdp_version);
	if (1 == g_server_rdp_version)
	{
		g_rdp_version = RDP_V4;
		g_server_depth = 8;
	}
}


/* Process connect response data blob */
void
sec_process_mcs_data(STREAM s)
{
	uint16 tag, length;
	size_t next_tag;
	uint8 len;

	in_uint8s(s, 21);	/* header (T.124 ConferenceCreateResponse) */
	in_uint8(s, len);
	if (len & 0x80)
		in_uint8(s, len);
	logger(Protocol, Debug, "%s()", __func__);

	while (!s_check_end(s))
	{
		in_uint16_le(s, tag);
		in_uint16_le(s, length);

		if (length <= 4)
			return;

		next_tag = s_tell(s) + length - 4;

		switch (tag)
		{
			case SEC_TAG_SRV_INFO:
				logger(Protocol, Debug, "%s(), SEC_TAG_SRV_INFO", __func__);
				sec_process_srv_info(s);
				break;

			case SEC_TAG_SRV_CRYPT:
				logger(Protocol, Debug, "%s(), SEC_TAG_SRV_CRYPT", __func__);
				sec_process_crypt_info(s);
				break;

			case SEC_TAG_SRV_CHANNELS:
				logger(Protocol, Debug, "%s(), SEC_TAG_SRV_CHANNELS", __func__);
				/* FIXME: We should parse this information and
				   use it to map RDP5 channels to MCS 
				   channels */
				break;

			default:
				logger(Protocol, Warning, "Unhandled response tag 0x%x", tag);
		}

		s_seek(s, next_tag);
	}
}

/* Receive secure transport packet */
STREAM
sec_recv(RD_BOOL * is_fastpath)
{
	uint8 fastpath_hdr, fastpath_flags;
	uint16 sec_flags;
	uint16 channel;
	STREAM s;
	struct stream packet;
	size_t data_offset;
	unsigned char *data;

	while ((s = mcs_recv(&channel, is_fastpath, &fastpath_hdr)) != NULL)
	{
		packet = *s;
		if (*is_fastpath == True)
		{
			/* If fastpath packet is encrypted, read data
			   signature and decrypt */
			/* FIXME: extracting flags from hdr could be made less obscure */
			fastpath_flags = (fastpath_hdr & 0xC0) >> 6;
			if (fastpath_flags & FASTPATH_OUTPUT_ENCRYPTED)
			{
				if (!s_check_rem(s, 8)) {
					rdp_protocol_error("consume fastpath signature from stream would overrun", &packet);
				}

				in_uint8s(s, 8);	/* signature */

				data_offset = s_tell(s);

				inout_uint8p(s, data, s_remaining(s));
				sec_decrypt(data, s_remaining(s));

				s_seek(s, data_offset);
			}
			return s;
		}

		if (g_encryption || (!g_licence_issued && !g_licence_error_result))
		{
			data_offset = s_tell(s);

			/* TS_SECURITY_HEADER */
			in_uint16_le(s, sec_flags);
			in_uint8s(s, 2);	/* skip sec_flags_hi */

			if (g_encryption)
			{
				data_offset = s_tell(s);

				if (sec_flags & SEC_ENCRYPT)
				{
					if (!s_check_rem(s, 8)) {
						rdp_protocol_error("consume encrypt signature from stream would overrun", &packet);
					}

					in_uint8s(s, 8);	/* signature */

					data_offset = s_tell(s);

					inout_uint8p(s, data, s_remaining(s));
					sec_decrypt(data, s_remaining(s));
				}

				if (sec_flags & SEC_LICENSE_PKT)
				{
					s_seek(s, data_offset);
					licence_process(s);
					continue;
				}

				if (sec_flags & SEC_REDIRECTION_PKT)
				{
					uint8 swapbyte;

					if (!s_check_rem(s, 8)) {
						rdp_protocol_error("consume redirect signature from stream would overrun", &packet);
					}

					in_uint8s(s, 8);	/* signature */

					data_offset = s_tell(s);

					inout_uint8p(s, data, s_remaining(s));
					sec_decrypt(data, s_remaining(s));

					/* Check for a redirect packet, starts with 00 04 */
					if (data[0] == 0 && data[1] == 4)
					{
						/* for some reason the PDU and the length seem to be swapped.
						   This isn't good, but we're going to do a byte for byte
						   swap.  So the first four value appear as: 00 04 XX YY,
						   where XX YY is the little endian length. We're going to
						   use 04 00 as the PDU type, so after our swap this will look
						   like: XX YY 04 00 */
						swapbyte = data[0];
						data[0] = data[2];
						data[2] = swapbyte;

						swapbyte = data[1];
						data[1] = data[3];
						data[3] = swapbyte;

						swapbyte = data[2];
						data[2] = data[3];
						data[3] = swapbyte;
					}
				}
			}
			else
			{
				if (sec_flags & SEC_LICENSE_PKT)
				{
					licence_process(s);
					continue;
				}
			}

			s_seek(s, data_offset);
		}

		if (channel != MCS_GLOBAL_CHANNEL)
		{
			channel_process(s, channel);
			continue;
		}

		return s;
	}

	return NULL;
}

/* Establish a secure connection */
RD_BOOL
sec_connect(char *server, char *username, char *domain, char *password, RD_BOOL reconnect)
{
	uint32 selected_proto;
	STREAM mcs_data;

	/* Start a MCS connect sequence */
	if (!mcs_connect_start(server, username, domain, password, reconnect, &selected_proto))
		return False;

	/* We exchange some RDP data during the MCS-Connect */
	mcs_data = s_alloc(512);
	sec_out_mcs_connect_initial_pdu(mcs_data, selected_proto);

	/* finalize the MCS connect sequence */
	if (!mcs_connect_finalize(mcs_data))
		return False;

	/* sec_process_mcs_data(&mcs_data); */
	if (g_encryption)
		sec_establish_key();
	s_free(mcs_data);
	return True;
}

/* Disconnect a connection */
void
sec_disconnect(void)
{
	/* Perform a User-initiated disconnect sequence, see
	   [MS-RDPBCGR] 1.3.1.4 Disconnect Sequences */
	mcs_disconnect(RN_USER_REQUESTED);
}

/* reset the state of the sec layer */
void
sec_reset_state(void)
{
	g_server_rdp_version = 0;
	g_sec_encrypt_use_count = 0;
	g_sec_decrypt_use_count = 0;
	g_licence_issued = 0;
	g_licence_error_result = 0;
	mcs_reset_state();
}
