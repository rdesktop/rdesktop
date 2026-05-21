/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Remote Credential Guard helper backend.
   Copyright 2026 Marco Fortina

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

#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH 16
#define RDPEAR_CALL_ID_OFFSET RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH

#define RDPEAR_REMOTE_CALL_KERB_NEGOTIATE_VERSION 0x0100
#define RDPEAR_REMOTE_CALL_NTLM_NEGOTIATE_VERSION 0x0200
#define RDPEAR_STATUS_SUCCESS 0x00000000U
#define RDPEAR_STATUS_NOT_SUPPORTED 0xc00000bbU
#define RDPEAR_TRANSPORT_STATUS_SUCCESS 0x00000000U
#define RDPEAR_MAX_PAYLOAD_LENGTH (1024U * 1024U)
#define RDPEAR_LOGON_CRED_PACKAGE "__rdesktop_remote_guard_logon_cred__"

static uint32_t
get_uint32_le(const uint8_t in[4])
{
	return ((uint32_t) in[0]) | ((uint32_t) in[1] << 8) |
		((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
}

static uint16_t
get_uint16_le(const uint8_t in[2])
{
	return ((uint16_t) in[0]) | ((uint16_t) in[1] << 8);
}

static void
put_uint32_le(uint8_t out[4], uint32_t value)
{
	out[0] = value & 0xff;
	out[1] = (value >> 8) & 0xff;
	out[2] = (value >> 16) & 0xff;
	out[3] = (value >> 24) & 0xff;
}

static void
put_uint16_le(uint8_t out[2], uint16_t value)
{
	out[0] = value & 0xff;
	out[1] = (value >> 8) & 0xff;
}

static int
read_all(int fd, uint8_t *data, size_t length)
{
	while (length > 0)
	{
		ssize_t n = read(fd, data, length);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			return 0;
		}
		if (n == 0)
			return 0;
		data += n;
		length -= n;
	}
	return 1;
}

static int
write_all(int fd, const uint8_t *data, size_t length)
{
	while (length > 0)
	{
		ssize_t n = write(fd, data, length);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			return 0;
		}
		if (n == 0)
			return 0;
		data += n;
		length -= n;
	}
	return 1;
}

static uint16_t
read_call_id(const uint8_t *request, uint32_t request_len, int *wide_call_id)
{
	uint16_t call_id16;
	uint32_t call_id32;

	*wide_call_id = 0;
	if (request_len < RDPEAR_CALL_ID_OFFSET + 2)
		return 0xffff;

	call_id16 = get_uint16_le(request + RDPEAR_CALL_ID_OFFSET);
	if (call_id16 == RDPEAR_REMOTE_CALL_KERB_NEGOTIATE_VERSION ||
	    call_id16 == RDPEAR_REMOTE_CALL_NTLM_NEGOTIATE_VERSION)
		return call_id16;

	if (request_len >= RDPEAR_CALL_ID_OFFSET + 4)
	{
		call_id32 = get_uint32_le(request + RDPEAR_CALL_ID_OFFSET);
		if (call_id32 == RDPEAR_REMOTE_CALL_KERB_NEGOTIATE_VERSION ||
		    call_id32 == RDPEAR_REMOTE_CALL_NTLM_NEGOTIATE_VERSION)
		{
			*wide_call_id = 1;
			return (uint16_t) call_id32;
		}
	}

	return call_id16;
}

static int
valid_package_buffer(const uint8_t *request, uint32_t request_len)
{
	if (request_len < RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH)
		return 0;

	if (request[0] != 1 || request[1] != 0)
		return 0;

	return 1;
}

static uint8_t *
encode_package_status(uint16_t call_id, int wide_call_id, uint32_t status, uint32_t *out_len)
{
	uint8_t *out;
	uint32_t size = RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH + 8;

	out = calloc(1, size);
	if (out == NULL)
		return NULL;

	put_uint16_le(out, 1);
	if (wide_call_id)
	{
		put_uint32_le(out + RDPEAR_CALL_ID_OFFSET, call_id);
	}
	else
	{
		put_uint16_le(out + RDPEAR_CALL_ID_OFFSET, call_id);
		put_uint16_le(out + RDPEAR_CALL_ID_OFFSET + 2, 0);
	}
	put_uint32_le(out + RDPEAR_CALL_ID_OFFSET + 4, status);

	*out_len = size;
	return out;
}

static uint8_t *
encode_negotiate_version(uint16_t call_id, int wide_call_id, uint32_t *out_len)
{
	uint8_t *out;
	uint32_t size = RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH + 12;

	out = calloc(1, size);
	if (out == NULL)
		return NULL;

	put_uint16_le(out, 1);
	if (wide_call_id)
	{
		put_uint32_le(out + RDPEAR_CALL_ID_OFFSET, call_id);
	}
	else
	{
		put_uint16_le(out + RDPEAR_CALL_ID_OFFSET, call_id);
		put_uint16_le(out + RDPEAR_CALL_ID_OFFSET + 2, 0);
	}
	put_uint32_le(out + RDPEAR_CALL_ID_OFFSET + 4, RDPEAR_STATUS_SUCCESS);
	put_uint32_le(out + RDPEAR_CALL_ID_OFFSET + 8, 0);

	*out_len = size;
	return out;
}

static int
hex_value(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static uint8_t *
decode_hex_blob(const char *hex, uint32_t *out_len)
{
	uint8_t *out;
	size_t digits = 0, i = 0;
	const char *p;

	for (p = hex; *p; p++)
	{
		if (isspace((unsigned char) *p) || *p == ':' || *p == '-')
			continue;
		if (hex_value((unsigned char) *p) < 0)
			return NULL;
		digits++;
	}

	if (digits == 0 || (digits % 2) != 0 || digits / 2 > RDPEAR_MAX_PAYLOAD_LENGTH)
		return NULL;

	out = malloc(digits / 2);
	if (out == NULL)
		return NULL;

	for (p = hex; *p;)
	{
		int hi, lo;
		while (*p && (isspace((unsigned char) *p) || *p == ':' || *p == '-'))
			p++;
		if (!*p)
			break;
		hi = hex_value((unsigned char) *p++);
		while (*p && (isspace((unsigned char) *p) || *p == ':' || *p == '-'))
			p++;
		if (!*p)
		{
			free(out);
			return NULL;
		}
		lo = hex_value((unsigned char) *p++);
		if (hi < 0 || lo < 0)
		{
			free(out);
			return NULL;
		}
		out[i++] = (uint8_t) ((hi << 4) | lo);
	}

	*out_len = (uint32_t) i;
	return out;
}

static uint8_t *
read_file_blob(const char *path, uint32_t *out_len)
{
	FILE *fp;
	long size;
	uint8_t *out;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return NULL;

	if (fseek(fp, 0, SEEK_END) != 0)
	{
		fclose(fp);
		return NULL;
	}
	size = ftell(fp);
	if (size <= 0 || size > (long) RDPEAR_MAX_PAYLOAD_LENGTH || fseek(fp, 0, SEEK_SET) != 0)
	{
		fclose(fp);
		return NULL;
	}

	out = malloc((size_t) size);
	if (out == NULL)
	{
		fclose(fp);
		return NULL;
	}

	if (fread(out, 1, (size_t) size, fp) != (size_t) size)
	{
		free(out);
		fclose(fp);
		return NULL;
	}

	fclose(fp);
	*out_len = (uint32_t) size;
	return out;
}

static uint8_t *
load_logon_credential(uint32_t *response_len)
{
	const char *hex = getenv("RDESKTOP_RDPEAR_LOGON_CRED_HEX");
	const char *path = getenv("RDESKTOP_RDPEAR_LOGON_CRED_FILE");
	uint8_t *blob = NULL;

	if (hex != NULL && *hex != 0)
		blob = decode_hex_blob(hex, response_len);
	else if (path != NULL && *path != 0)
		blob = read_file_blob(path, response_len);

	return blob;
}

static uint8_t *
process_request(const char *package_name, const uint8_t *request, uint32_t request_len,
                uint32_t *response_len)
{
	uint16_t call_id;
	int wide_call_id;

	if (strcmp(package_name, RDPEAR_LOGON_CRED_PACKAGE) == 0)
	{
		if (request_len != 0)
			return NULL;
		return load_logon_credential(response_len);
	}

	if (!valid_package_buffer(request, request_len))
		return NULL;

	call_id = read_call_id(request, request_len, &wide_call_id);

	if ((strcmp(package_name, "Kerberos") == 0 || strcmp(package_name, "Negotiate") == 0) &&
	    call_id == RDPEAR_REMOTE_CALL_KERB_NEGOTIATE_VERSION)
		return encode_negotiate_version(call_id, wide_call_id, response_len);

	if ((strcmp(package_name, "NTLM") == 0 || strcmp(package_name, "Negotiate") == 0) &&
	    call_id == RDPEAR_REMOTE_CALL_NTLM_NEGOTIATE_VERSION)
		return encode_negotiate_version(call_id, wide_call_id, response_len);

	return encode_package_status(call_id, wide_call_id, RDPEAR_STATUS_NOT_SUPPORTED, response_len);
}

static int
run_once(void)
{
	uint8_t header[8];
	char *package_name = NULL;
	uint8_t *request = NULL;
	uint8_t *response = NULL;
	uint32_t package_len, request_len, response_len = 0;
	uint8_t response_header[8];
	int ok = 0;

	if (!read_all(STDIN_FILENO, header, sizeof(header)))
		goto out;

	package_len = get_uint32_le(header);
	request_len = get_uint32_le(header + 4);
	if (package_len == 0 || package_len > 256 || request_len > RDPEAR_MAX_PAYLOAD_LENGTH)
		goto out;

	package_name = calloc(1, package_len + 1);
	request = malloc(request_len);
	if (package_name == NULL || request == NULL)
		goto out;

	if (!read_all(STDIN_FILENO, (uint8_t *) package_name, package_len) ||
	    !read_all(STDIN_FILENO, request, request_len))
		goto out;

	response = process_request(package_name, request, request_len, &response_len);
	if (response == NULL)
		goto out;

	put_uint32_le(response_header, RDPEAR_TRANSPORT_STATUS_SUCCESS);
	put_uint32_le(response_header + 4, response_len);
	if (!write_all(STDOUT_FILENO, response_header, sizeof(response_header)) ||
	    !write_all(STDOUT_FILENO, response, response_len))
		goto out;

	ok = 1;

out:
	free(package_name);
	free(request);
	free(response);
	return ok ? 0 : 1;
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--version") == 0)
	{
		printf("rdesktop-rdpear-helper %s\n", PACKAGE_VERSION);
		return 0;
	}

	if (argc != 1)
	{
		fprintf(stderr, "usage: %s\n", argv[0]);
		return 2;
	}

	return run_once();
}
