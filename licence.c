/*
   rdesktop: A Remote Desktop Protocol client.
   RDP licensing negotiation
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "rdesktop.h"

#ifdef WITH_OPENSSL
#include <openssl/rc4.h>
#else
#include "crypto/rc4.h"
#endif

extern char username[16];
extern char hostname[16];

static uint8 licence_key[16];
static uint8 licence_sign_key[16];

BOOL licence_issued = False;


int
load_licence(unsigned char **data)
{
	char *path;
	char *home;
	struct stat st;
	int fd;

	home = getenv("HOME");
	if (home == NULL)
		return -1;

	path = xmalloc(strlen(home) + strlen(hostname) + 20);
	sprintf(path, "%s/.rdesktop/licence.%s", home, hostname);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	if (fstat(fd, &st))
		return -1;

	*data = xmalloc(st.st_size);
	return read(fd, *data, st.st_size);
}

void
save_licence(unsigned char *data, int length)
{
	char *fpath;		/* file path for licence */
	char *fname, *fnamewrk;	/* file name for licence .inkl path. */
	char *home;
	uint32 y;
	struct flock fnfl;
	int fnfd, fnwrkfd, i, wlen;
	struct stream s, *s_ptr;
	uint32 len;

	/* Construct a stream, so that we can use macros to extract the
	 * licence.
	 */
	s_ptr = &s;
	s_ptr->p = data;
	/* Skip first two bytes */
	in_uint16(s_ptr, len);

	/* Skip three strings */
	for (i = 0; i < 3; i++)
	{
		in_uint32(s_ptr, len);
		s_ptr->p += len;
		/* Make sure that we won't be past the end of data after
		 * reading the next length value
		 */
		if ((s_ptr->p) + 4 > data + length)
		{
			printf("Error in parsing licence key.\n");
			printf("Strings %d end value %x > supplied length (%x)\n", i,
			       (unsigned int) s_ptr->p, (unsigned int) data + length);
			return;
		}
	}
	in_uint32(s_ptr, len);
	if (s_ptr->p + len > data + length)
	{
		printf("Error in parsing licence key.\n");
		printf("End of licence %x > supplied length (%x)\n",
		       (unsigned int) s_ptr->p + len, (unsigned int) data + length);
		return;
	}

	home = getenv("HOME");
	if (home == NULL)
		return;

	/* set and create the directory -- if it doesn't exist. */
	fpath = xmalloc(strlen(home) + 11);
	STRNCPY(fpath, home, strlen(home) + 1);

	sprintf(fpath, "%s/.rdesktop", fpath);
	if (mkdir(fpath, 0700) == -1 && errno != EEXIST)
	{
		perror("mkdir");
		exit(1);
	}

	/* set the real licence filename, and put a write lock on it. */
	fname = xmalloc(strlen(fpath) + strlen(hostname) + 10);
	sprintf(fname, "%s/licence.%s", fpath, hostname);
	fnfd = open(fname, O_RDONLY);
	if (fnfd != -1)
	{
		fnfl.l_type = F_WRLCK;
		fnfl.l_whence = SEEK_SET;
		fnfl.l_start = 0;
		fnfl.l_len = 1;
		fcntl(fnfd, F_SETLK, &fnfl);
	}

	/* create a temporary licence file */
	fnamewrk = xmalloc(strlen(fname) + 12);
	for (y = 0;; y++)
	{
		sprintf(fnamewrk, "%s.%lu", fname, (long unsigned int) y);
		fnwrkfd = open(fnamewrk, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (fnwrkfd == -1)
		{
			if (errno == EINTR || errno == EEXIST)
				continue;
			perror("create");
			exit(1);
		}
		break;
	}
	/* write to the licence file */
	for (y = 0; y < len;)
	{
		do
		{
			wlen = write(fnwrkfd, s_ptr->p + y, len - y);
		}
		while (wlen == -1 && errno == EINTR);
		if (wlen < 1)
		{
			perror("write");
			unlink(fnamewrk);
			exit(1);
		}
		y += wlen;
	}

	/* close the file and rename it to fname */
	if (close(fnwrkfd) == -1)
	{
		perror("close");
		unlink(fnamewrk);
		exit(1);
	}
	if (rename(fnamewrk, fname) == -1)
	{
		perror("rename");
		unlink(fnamewrk);
		exit(1);
	}
	/* close the file lock on fname */
	if (fnfd != -1)
	{
		fnfl.l_type = F_UNLCK;
		fnfl.l_whence = SEEK_SET;
		fnfl.l_start = 0;
		fnfl.l_len = 1;
		fcntl(fnfd, F_SETLK, &fnfl);
		close(fnfd);
	}

}

/* Generate a session key and RC4 keys, given client and server randoms */
static void
licence_generate_keys(uint8 * client_key, uint8 * server_key, uint8 * client_rsa)
{
	uint8 session_key[48];
	uint8 temp_hash[48];

	/* Generate session key - two rounds of sec_hash_48 */
	sec_hash_48(temp_hash, client_rsa, client_key, server_key, 65);
	sec_hash_48(session_key, temp_hash, server_key, client_key, 65);

	/* Store first 16 bytes of session key, for generating signatures */
	memcpy(licence_sign_key, session_key, 16);

	/* Generate RC4 key */
	sec_hash_16(licence_key, &session_key[16], client_key, server_key);
}

static void
licence_generate_hwid(uint8 * hwid)
{
	buf_out_uint32(hwid, 2);
	strncpy((char *) (hwid + 4), hostname, LICENCE_HWID_SIZE - 4);
}

/* Present an existing licence to the server */
static void
licence_present(uint8 * client_random, uint8 * rsa_data,
		uint8 * licence_data, int licence_size, uint8 * hwid, uint8 * signature)
{
	uint32 sec_flags = SEC_LICENCE_NEG;
	uint16 length =
		16 + SEC_RANDOM_SIZE + SEC_MODULUS_SIZE + SEC_PADDING_SIZE +
		licence_size + LICENCE_HWID_SIZE + LICENCE_SIGNATURE_SIZE;
	STREAM s;

	s = sec_init(sec_flags, length + 4);

	out_uint16_le(s, LICENCE_TAG_PRESENT);
	out_uint16_le(s, length);

	out_uint32_le(s, 1);
	out_uint16(s, 0);
	out_uint16_le(s, 0x0201);

	out_uint8p(s, client_random, SEC_RANDOM_SIZE);
	out_uint16(s, 0);
	out_uint16_le(s, (SEC_MODULUS_SIZE + SEC_PADDING_SIZE));
	out_uint8p(s, rsa_data, SEC_MODULUS_SIZE);
	out_uint8s(s, SEC_PADDING_SIZE);

	out_uint16_le(s, 1);
	out_uint16_le(s, licence_size);
	out_uint8p(s, licence_data, licence_size);

	out_uint16_le(s, 1);
	out_uint16_le(s, LICENCE_HWID_SIZE);
	out_uint8p(s, hwid, LICENCE_HWID_SIZE);

	out_uint8p(s, signature, LICENCE_SIGNATURE_SIZE);

	s_mark_end(s);
	sec_send(s, sec_flags);
}

/* Send a licence request packet */
static void
licence_send_request(uint8 * client_random, uint8 * rsa_data, char *user, char *host)
{
	uint32 sec_flags = SEC_LICENCE_NEG;
	uint16 userlen = strlen(user) + 1;
	uint16 hostlen = strlen(host) + 1;
	uint16 length = 128 + userlen + hostlen;
	STREAM s;

	s = sec_init(sec_flags, length + 2);

	out_uint16_le(s, LICENCE_TAG_REQUEST);
	out_uint16_le(s, length);

	out_uint32_le(s, 1);
	out_uint16(s, 0);
	out_uint16_le(s, 0xff01);

	out_uint8p(s, client_random, SEC_RANDOM_SIZE);
	out_uint16(s, 0);
	out_uint16_le(s, (SEC_MODULUS_SIZE + SEC_PADDING_SIZE));
	out_uint8p(s, rsa_data, SEC_MODULUS_SIZE);
	out_uint8s(s, SEC_PADDING_SIZE);

	out_uint16(s, LICENCE_TAG_USER);
	out_uint16(s, userlen);
	out_uint8p(s, user, userlen);

	out_uint16(s, LICENCE_TAG_HOST);
	out_uint16(s, hostlen);
	out_uint8p(s, host, hostlen);

	s_mark_end(s);
	sec_send(s, sec_flags);
}

/* Process a licence demand packet */
static void
licence_process_demand(STREAM s)
{
	uint8 null_data[SEC_MODULUS_SIZE];
	uint8 *server_random;
	uint8 signature[LICENCE_SIGNATURE_SIZE];
	uint8 hwid[LICENCE_HWID_SIZE];
	uint8 *licence_data;
	int licence_size;
	RC4_KEY crypt_key;

	/* Retrieve the server random from the incoming packet */
	in_uint8p(s, server_random, SEC_RANDOM_SIZE);

	/* We currently use null client keys. This is a bit naughty but, hey,
	   the security of licence negotiation isn't exactly paramount. */
	memset(null_data, 0, sizeof(null_data));
	licence_generate_keys(null_data, server_random, null_data);

	licence_size = load_licence(&licence_data);
	if (licence_size != -1)
	{
		/* Generate a signature for the HWID buffer */
		licence_generate_hwid(hwid);
		sec_sign(signature, 16, licence_sign_key, 16, hwid, sizeof(hwid));

		/* Now encrypt the HWID */
		RC4_set_key(&crypt_key, 16, licence_key);
		RC4(&crypt_key, sizeof(hwid), hwid, hwid);

		licence_present(null_data, null_data, licence_data, licence_size, hwid, signature);
		xfree(licence_data);
		return;
	}

	licence_send_request(null_data, null_data, username, hostname);
}

/* Send an authentication response packet */
static void
licence_send_authresp(uint8 * token, uint8 * crypt_hwid, uint8 * signature)
{
	uint32 sec_flags = SEC_LICENCE_NEG;
	uint16 length = 58;
	STREAM s;

	s = sec_init(sec_flags, length + 2);

	out_uint16_le(s, LICENCE_TAG_AUTHRESP);
	out_uint16_le(s, length);

	out_uint16_le(s, 1);
	out_uint16_le(s, LICENCE_TOKEN_SIZE);
	out_uint8p(s, token, LICENCE_TOKEN_SIZE);

	out_uint16_le(s, 1);
	out_uint16_le(s, LICENCE_HWID_SIZE);
	out_uint8p(s, crypt_hwid, LICENCE_HWID_SIZE);

	out_uint8p(s, signature, LICENCE_SIGNATURE_SIZE);

	s_mark_end(s);
	sec_send(s, sec_flags);
}

/* Parse an authentication request packet */
static BOOL
licence_parse_authreq(STREAM s, uint8 ** token, uint8 ** signature)
{
	uint16 tokenlen;

	in_uint8s(s, 6);	/* unknown: f8 3d 15 00 04 f6 */

	in_uint16_le(s, tokenlen);
	if (tokenlen != LICENCE_TOKEN_SIZE)
	{
		error("token len %d\n", tokenlen);
		return False;
	}

	in_uint8p(s, *token, tokenlen);
	in_uint8p(s, *signature, LICENCE_SIGNATURE_SIZE);

	return s_check_end(s);
}

/* Process an authentication request packet */
static void
licence_process_authreq(STREAM s)
{
	uint8 *in_token, *in_sig;
	uint8 out_token[LICENCE_TOKEN_SIZE], decrypt_token[LICENCE_TOKEN_SIZE];
	uint8 hwid[LICENCE_HWID_SIZE], crypt_hwid[LICENCE_HWID_SIZE];
	uint8 sealed_buffer[LICENCE_TOKEN_SIZE + LICENCE_HWID_SIZE];
	uint8 out_sig[LICENCE_SIGNATURE_SIZE];
	RC4_KEY crypt_key;

	/* Parse incoming packet and save the encrypted token */
	licence_parse_authreq(s, &in_token, &in_sig);
	memcpy(out_token, in_token, LICENCE_TOKEN_SIZE);

	/* Decrypt the token. It should read TEST in Unicode. */
	RC4_set_key(&crypt_key, 16, licence_key);
	RC4(&crypt_key, LICENCE_TOKEN_SIZE, in_token, decrypt_token);

	/* Generate a signature for a buffer of token and HWID */
	licence_generate_hwid(hwid);
	memcpy(sealed_buffer, decrypt_token, LICENCE_TOKEN_SIZE);
	memcpy(sealed_buffer + LICENCE_TOKEN_SIZE, hwid, LICENCE_HWID_SIZE);
	sec_sign(out_sig, 16, licence_sign_key, 16, sealed_buffer, sizeof(sealed_buffer));

	/* Now encrypt the HWID */
	RC4_set_key(&crypt_key, 16, licence_key);
	RC4(&crypt_key, LICENCE_HWID_SIZE, hwid, crypt_hwid);

	licence_send_authresp(out_token, crypt_hwid, out_sig);
}

/* Process an licence issue packet */
static void
licence_process_issue(STREAM s)
{
	RC4_KEY crypt_key;
	uint32 length;
	uint16 check;

	in_uint8s(s, 2);	/* 3d 45 - unknown */
	in_uint16_le(s, length);
	if (!s_check_rem(s, length))
		return;

	RC4_set_key(&crypt_key, 16, licence_key);
	RC4(&crypt_key, length, s->p, s->p);

	in_uint16(s, check);
	if (check != 0)
		return;

	licence_issued = True;
	save_licence(s->p, length - 2);
}

/* Process a licence packet */
void
licence_process(STREAM s)
{
	uint16 tag;

	in_uint16_le(s, tag);
	in_uint8s(s, 2);	/* length */

	switch (tag)
	{
		case LICENCE_TAG_DEMAND:
			licence_process_demand(s);
			break;

		case LICENCE_TAG_AUTHREQ:
			licence_process_authreq(s);
			break;

		case LICENCE_TAG_ISSUE:
			licence_process_issue(s);
			break;

		case LICENCE_TAG_REISSUE:
			break;

		case LICENCE_TAG_RESULT:
			break;

		default:
			unimpl("licence tag 0x%x\n", tag);
	}
}
