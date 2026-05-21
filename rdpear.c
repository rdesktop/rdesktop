/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Remote Credential Guard authentication redirection channel.
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

#include "rdesktop.h"

#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>

#define RDPEAR_CHANNEL_NAME "Microsoft::Windows::RDS::AuthRedirection"

#define RDPEAR_PROTOCOL_MAGIC 0x4eacc3c8
#define RDPEAR_PROTOCOL_VERSION 0
#define RDPEAR_OUTER_HEADER_LENGTH 24
#define RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH 16
#define RDPEAR_CALL_ID_OFFSET RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH

#define RDPEAR_REMOTE_CALL_KERB_NEGOTIATE_VERSION 0x0100
#define RDPEAR_REMOTE_CALL_NTLM_NEGOTIATE_VERSION 0x0200
#define RDPEAR_STATUS_SUCCESS 0x00000000
#define RDPEAR_STATUS_NOT_SUPPORTED 0xc00000bb

extern RD_BOOL g_remote_guard;
extern char *g_remote_guard_helper;

typedef struct rdpear_inner_packet_t
{
	sint32 version;
	char package_name[64];
	STREAM buffer;
} rdpear_inner_packet_t;


static RD_BOOL
rdpear_write_all(int fd, const uint8 *data, size_t length)
{
	while (length > 0)
	{
		ssize_t n = write(fd, data, length);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			return False;
		}
		if (n == 0)
			return False;
		data += n;
		length -= n;
	}
	return True;
}

static RD_BOOL
rdpear_read_all(int fd, uint8 *data, size_t length)
{
	while (length > 0)
	{
		ssize_t n = read(fd, data, length);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			return False;
		}
		if (n == 0)
			return False;
		data += n;
		length -= n;
	}
	return True;
}

static void
rdpear_put_uint32_le(uint8 out[4], uint32 value)
{
	out[0] = value & 0xff;
	out[1] = (value >> 8) & 0xff;
	out[2] = (value >> 16) & 0xff;
	out[3] = (value >> 24) & 0xff;
}

static uint32
rdpear_get_uint32_le(const uint8 in[4])
{
	return ((uint32) in[0]) | ((uint32) in[1] << 8) |
		((uint32) in[2] << 16) | ((uint32) in[3] << 24);
}

static STREAM
rdpear_ber_wrap_hdr_data(int tagval, STREAM in)
{
	STREAM out;
	int size = s_length(in) + 16;

	out = s_alloc(size);
	ber_out_header(out, tagval, s_length(in));
	out_stream(out, in);
	s_mark_end(out);
	return out;
}

static STREAM
rdpear_stream_from_bytes(const uint8 *data, size_t length)
{
	STREAM s;

	s = s_alloc(length);
	out_uint8a(s, data, length);
	s_mark_end(s);
	s->p = s->data;
	return s;
}

static RD_BOOL
rdpear_ber_read_header(STREAM s, int *tag, uint32 *length)
{
	int t, l;

	if (!s_check_rem(s, 2))
		return False;

	in_uint8(s, t);
	in_uint8(s, l);

	if (l & 0x80)
	{
		int lenbytes = l & 0x7f;

		if (lenbytes == 0 || lenbytes > 4 || !s_check_rem(s, lenbytes))
			return False;

		l = 0;
		while (lenbytes--)
		{
			uint8 b;
			in_uint8(s, b);
			l = (l << 8) | b;
		}
	}

	if (!s_check_rem(s, l))
		return False;

	*tag = t;
	*length = l;
	return True;
}

static RD_BOOL
rdpear_ber_read_octet_string(STREAM s, STREAM *value)
{
	int tag;
	uint32 length;
	uint8 *p;

	if (!rdpear_ber_read_header(s, &tag, &length))
		return False;

	if (tag != BER_TAG_OCTET_STRING)
	{
		logger(Protocol, Warning,
		       "rdpear_ber_read_octet_string(), expected OCTET STRING, got 0x%x", tag);
		return False;
	}

	in_uint8p(s, p, length);
	*value = rdpear_stream_from_bytes(p, length);
	return True;
}

static RD_BOOL
rdpear_ber_read_enumerated(STREAM s, sint32 *value)
{
	int tag;
	uint32 length;
	uint8 b;
	sint32 v = 0;

	if (!rdpear_ber_read_header(s, &tag, &length))
		return False;

	if (tag != BER_TAG_RESULT || length > 4)
	{
		logger(Protocol, Warning,
		       "rdpear_ber_read_enumerated(), expected ENUMERATED, got tag 0x%x length %u",
		       tag, (unsigned) length);
		return False;
	}

	while (length--)
	{
		in_uint8(s, b);
		v = (v << 8) | b;
	}

	*value = v;
	return True;
}

static RD_BOOL
rdpear_decode_package_name(STREAM s, char *out, size_t out_size)
{
	size_t i, j, len;
	RD_BOOL looks_utf16 = False;

	if (out_size == 0)
		return False;

	out[0] = 0;

	len = s_length(s);
	if (len == 0)
		return False;

	for (i = 1; i < len; i += 2)
	{
		if (s->data[i] == 0)
		{
			looks_utf16 = True;
			break;
		}
	}

	if (looks_utf16)
	{
		for (i = 0, j = 0; i + 1 < len && j + 1 < out_size; i += 2)
		{
			if (s->data[i] == 0 && s->data[i + 1] == 0)
				break;
			out[j++] = s->data[i];
		}
	}
	else
	{
		for (i = 0, j = 0; i < len && j + 1 < out_size; i++)
		{
			if (s->data[i] == 0)
				break;
			out[j++] = s->data[i];
		}
	}

	out[j] = 0;
	return j > 0;
}

static RD_BOOL
rdpear_parse_inner_packet(STREAM plain, rdpear_inner_packet_t *packet)
{
	int tag;
	uint32 length;
	STREAM sequence, field, value;
	uint8 *p;

	memset(packet, 0, sizeof(*packet));
	packet->version = 0;

	if (!rdpear_ber_read_header(plain, &tag, &length) ||
	    tag != (BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED))
	{
		logger(Protocol, Warning,
		       "rdpear_parse_inner_packet(), missing TSRemoteGuardInnerPacket sequence");
		return False;
	}

	in_uint8p(plain, p, length);
	sequence = rdpear_stream_from_bytes(p, length);

	while (s_remaining(sequence))
	{
		field = NULL;
		value = NULL;

		if (!rdpear_ber_read_header(sequence, &tag, &length) || !s_check_rem(sequence, length))
		{
			s_free(sequence);
			return False;
		}

		in_uint8p(sequence, p, length);
		field = rdpear_stream_from_bytes(p, length);

		switch (tag)
		{
			case BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 0:
				if (!rdpear_ber_read_enumerated(field, &packet->version))
				{
					s_free(field);
					s_free(sequence);
					return False;
				}
				break;

			case BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1:
				if (!rdpear_ber_read_octet_string(field, &value) ||
				    !rdpear_decode_package_name(value, packet->package_name,
							       sizeof(packet->package_name)))
				{
					s_free(field);
					s_free(sequence);
					s_free(value);
					return False;
				}
				s_free(value);
				break;

			case BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2:
				if (!rdpear_ber_read_octet_string(field, &packet->buffer))
				{
					s_free(field);
					s_free(sequence);
					return False;
				}
				break;

			default:
				logger(Protocol, Debug,
				       "rdpear_parse_inner_packet(), ignoring optional tag 0x%x", tag);
				break;
		}

		s_free(field);
	}

	s_free(sequence);

	if (packet->version != 0)
	{
		logger(Protocol, Warning,
		       "rdpear_parse_inner_packet(), unsupported inner packet version %d",
		       packet->version);
		return False;
	}

	if (packet->package_name[0] == 0 || packet->buffer == NULL)
	{
		logger(Protocol, Warning,
		       "rdpear_parse_inner_packet(), missing packageName or buffer");
		return False;
	}

	return True;
}

static STREAM
rdpear_encode_inner_packet(const char *package_name, STREAM buffer)
{
	STREAM out, h1, h2;
	struct stream tmp = { 0 };
	struct stream message = { 0 };

	s_realloc(&tmp, 512);

	/* packageName [1] */
	s_reset(&tmp);
	out_utf16s(&tmp, package_name);
	s_mark_end(&tmp);
	h2 = rdpear_ber_wrap_hdr_data(BER_TAG_OCTET_STRING, &tmp);
	h1 = rdpear_ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 1, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	/* buffer [2] */
	h2 = rdpear_ber_wrap_hdr_data(BER_TAG_OCTET_STRING, buffer);
	h1 = rdpear_ber_wrap_hdr_data(BER_TAG_CTXT_SPECIFIC | BER_TAG_CONSTRUCTED | 2, h2);
	s_realloc(&message, s_length(&message) + s_length(h1));
	out_stream(&message, h1);
	s_mark_end(&message);
	s_free(h2);
	s_free(h1);

	out = rdpear_ber_wrap_hdr_data(BER_TAG_SEQUENCE | BER_TAG_CONSTRUCTED, &message);

	free(tmp.data);
	free(message.data);
	return out;
}

static RD_BOOL
rdpear_parse_outer_packet(STREAM s, STREAM *payload)
{
	uint32 magic, length, version, reserved;
	uint64 context;
	uint8 *p;

	*payload = NULL;

	if (!s_check_rem(s, RDPEAR_OUTER_HEADER_LENGTH))
	{
		logger(Protocol, Warning, "rdpear_parse_outer_packet(), short outer packet");
		return False;
	}

	in_uint32_le(s, magic);
	in_uint32_le(s, length);
	in_uint32_le(s, version);
	in_uint32_le(s, reserved);
	in_uint64_le(s, context);

	if (magic != RDPEAR_PROTOCOL_MAGIC)
	{
		logger(Protocol, Warning,
		       "rdpear_parse_outer_packet(), bad protocol magic 0x%08x", magic);
		return False;
	}

	if (version != RDPEAR_PROTOCOL_VERSION || reserved != 0 || context != 0)
	{
		logger(Protocol, Warning,
		       "rdpear_parse_outer_packet(), unsupported header version=%u reserved=%u context=%llu",
		       version, reserved, (unsigned long long) context);
		return False;
	}

	if (length < RDPEAR_OUTER_HEADER_LENGTH || length > s_length(s))
	{
		logger(Protocol, Warning,
		       "rdpear_parse_outer_packet(), invalid outer length %u for packet length %u",
		       length, (unsigned) s_length(s));
		return False;
	}

	if (!s_check_rem(s, length - RDPEAR_OUTER_HEADER_LENGTH))
		return False;

	in_uint8p(s, p, length - RDPEAR_OUTER_HEADER_LENGTH);
	*payload = rdpear_stream_from_bytes(p, length - RDPEAR_OUTER_HEADER_LENGTH);
	return True;
}

static STREAM
rdpear_encode_outer_packet(STREAM encrypted_payload)
{
	STREAM out;
	uint32 length;

	length = RDPEAR_OUTER_HEADER_LENGTH + s_length(encrypted_payload);
	out = s_alloc(length);
	out_uint32_le(out, RDPEAR_PROTOCOL_MAGIC);
	out_uint32_le(out, length);
	out_uint32_le(out, RDPEAR_PROTOCOL_VERSION);
	out_uint32_le(out, 0);
	out_uint64_le(out, 0);
	out_stream(out, encrypted_payload);
	s_mark_end(out);
	out->p = out->data;
	return out;
}

static uint16
rdpear_read_call_id(STREAM buffer, RD_BOOL *wide_call_id)
{
	uint16 call_id16;
	uint32 call_id32;

	*wide_call_id = False;

	if (!s_check_rem(buffer, RDPEAR_CALL_ID_OFFSET + 2))
		return 0xffff;

	s_seek(buffer, RDPEAR_CALL_ID_OFFSET);
	in_uint16_le(buffer, call_id16);

	if (call_id16 == RDPEAR_REMOTE_CALL_KERB_NEGOTIATE_VERSION ||
	    call_id16 == RDPEAR_REMOTE_CALL_NTLM_NEGOTIATE_VERSION)
		return call_id16;

	if (s_check_rem(buffer, 2))
	{
		s_seek(buffer, RDPEAR_CALL_ID_OFFSET);
		in_uint32_le(buffer, call_id32);
		if (call_id32 == RDPEAR_REMOTE_CALL_KERB_NEGOTIATE_VERSION ||
		    call_id32 == RDPEAR_REMOTE_CALL_NTLM_NEGOTIATE_VERSION)
		{
			*wide_call_id = True;
			return call_id32;
		}
	}

	return call_id16;
}

static STREAM
rdpear_encode_negotiate_version_output(uint16 call_id, RD_BOOL wide_call_id)
{
	STREAM out;
	uint32 size;

	/* 16-byte package header + CallId + NTSTATUS + VersionToUse. */
	size = RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH + (wide_call_id ? 12 : 12);
	out = s_alloc(size);

	out_uint16_le(out, 1);
	out_uint8s(out, RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH - 2);

	if (wide_call_id)
	{
		out_uint32_le(out, call_id);
	}
	else
	{
		out_uint16_le(out, call_id);
		out_uint16_le(out, 0);
	}

	out_uint32_le(out, RDPEAR_STATUS_SUCCESS);
	out_uint32_le(out, 0);
	s_mark_end(out);
	out->p = out->data;
	return out;
}

static STREAM
rdpear_encode_status_output(uint16 call_id, RD_BOOL wide_call_id, uint32 status)
{
	STREAM out;
	uint32 size;

	size = RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH + (wide_call_id ? 8 : 8);
	out = s_alloc(size);

	out_uint16_le(out, 1);
	out_uint8s(out, RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH - 2);

	if (wide_call_id)
	{
		out_uint32_le(out, call_id);
	}
	else
	{
		out_uint16_le(out, call_id);
		out_uint16_le(out, 0);
	}

	out_uint32_le(out, status);
	s_mark_end(out);
	out->p = out->data;
	return out;
}


static STREAM
rdpear_call_helper(const char *package_name, STREAM request)
{
	int inpipe[2], outpipe[2];
	pid_t pid;
	uint8 hdr[8];
	uint32 package_len, request_len, status, response_len;
	STREAM response;
	int child_status;

	if (g_remote_guard_helper == NULL || g_remote_guard_helper[0] == 0)
		return NULL;

	if (pipe(inpipe) < 0 || pipe(outpipe) < 0)
	{
		logger(Core, Warning, "rdpear_call_helper(), pipe failed: %s", strerror(errno));
		return NULL;
	}

	pid = fork();
	if (pid < 0)
	{
		logger(Core, Warning, "rdpear_call_helper(), fork failed: %s", strerror(errno));
		close(inpipe[0]);
		close(inpipe[1]);
		close(outpipe[0]);
		close(outpipe[1]);
		return NULL;
	}

	if (pid == 0)
	{
		dup2(inpipe[0], STDIN_FILENO);
		dup2(outpipe[1], STDOUT_FILENO);
		close(inpipe[0]);
		close(inpipe[1]);
		close(outpipe[0]);
		close(outpipe[1]);
		execl("/bin/sh", "sh", "-c", g_remote_guard_helper, (char *) NULL);
		_exit(127);
	}

	close(inpipe[0]);
	close(outpipe[1]);

	package_len = strlen(package_name);
	request_len = s_length(request);
	rdpear_put_uint32_le(hdr, package_len);
	rdpear_put_uint32_le(hdr + 4, request_len);

	if (!rdpear_write_all(inpipe[1], hdr, sizeof(hdr)) ||
	    !rdpear_write_all(inpipe[1], (const uint8 *) package_name, package_len) ||
	    !rdpear_write_all(inpipe[1], request->data, request_len))
	{
		logger(Core, Warning, "rdpear_call_helper(), failed to write helper request");
		close(inpipe[1]);
		close(outpipe[0]);
		waitpid(pid, &child_status, 0);
		return NULL;
	}
	close(inpipe[1]);

	if (!rdpear_read_all(outpipe[0], hdr, sizeof(hdr)))
	{
		logger(Core, Warning, "rdpear_call_helper(), failed to read helper response header");
		close(outpipe[0]);
		waitpid(pid, &child_status, 0);
		return NULL;
	}

	status = rdpear_get_uint32_le(hdr);
	response_len = rdpear_get_uint32_le(hdr + 4);

	if (response_len > 1024 * 1024)
	{
		logger(Core, Warning,
		       "rdpear_call_helper(), refusing oversized helper response %u", response_len);
		close(outpipe[0]);
		waitpid(pid, &child_status, 0);
		return NULL;
	}

	response = s_alloc(response_len);
	if (response_len > 0 && !rdpear_read_all(outpipe[0], response->data, response_len))
	{
		logger(Core, Warning, "rdpear_call_helper(), failed to read helper response payload");
		s_free(response);
		close(outpipe[0]);
		waitpid(pid, &child_status, 0);
		return NULL;
	}
	response->p = response->data + response_len;
	s_mark_end(response);
	response->p = response->data;

	close(outpipe[0]);
	waitpid(pid, &child_status, 0);

	if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
	{
		logger(Core, Warning, "rdpear_call_helper(), helper exited unsuccessfully");
		s_free(response);
		return NULL;
	}

	if (status != RDPEAR_STATUS_SUCCESS)
	{
		logger(Core, Warning,
		       "rdpear_call_helper(), helper returned NTSTATUS 0x%08x", status);
		s_free(response);
		return NULL;
	}

	return response;
}

static STREAM
rdpear_process_package_buffer(const char *package_name, STREAM buffer)
{
	uint16 call_id;
	RD_BOOL wide_call_id;

	if (!s_check_rem(buffer, RDPEAR_PACKAGE_BUFFER_HEADER_LENGTH))
	{
		logger(Protocol, Warning,
		       "rdpear_process_package_buffer(), short package buffer for '%s'",
		       package_name);
		return NULL;
	}

	if (buffer->data[0] != 1 || buffer->data[1] != 0)
	{
		logger(Protocol, Warning,
		       "rdpear_process_package_buffer(), invalid package buffer header for '%s'",
		       package_name);
		return NULL;
	}

	call_id = rdpear_read_call_id(buffer, &wide_call_id);

	logger(Protocol, Debug,
	       "rdpear_process_package_buffer(), package='%s' call_id=0x%04x wide=%s",
	       package_name, call_id, wide_call_id ? "yes" : "no");

	switch (call_id)
	{
		case RDPEAR_REMOTE_CALL_KERB_NEGOTIATE_VERSION:
			if (strcmp(package_name, "Kerberos") != 0 &&
			    strcmp(package_name, "Negotiate") != 0)
			{
				logger(Protocol, Warning,
				       "rdpear_process_package_buffer(), Kerberos negotiate call routed to unexpected package '%s'",
				       package_name);
			}
			return rdpear_encode_negotiate_version_output(call_id, wide_call_id);

		case RDPEAR_REMOTE_CALL_NTLM_NEGOTIATE_VERSION:
			if (strcmp(package_name, "NTLM") != 0 && strcmp(package_name, "Negotiate") != 0)
			{
				logger(Protocol, Warning,
				       "rdpear_process_package_buffer(), NTLM negotiate call routed to unexpected package '%s'",
				       package_name);
			}
			return rdpear_encode_negotiate_version_output(call_id, wide_call_id);

		default:
			{
				STREAM helper_response = rdpear_call_helper(package_name, buffer);
				if (helper_response != NULL)
					return helper_response;

				logger(Core, Warning,
				       "rdpear_process_package_buffer(), unsupported Remote Guard package call 0x%04x for '%s'",
				       call_id, package_name);
				return rdpear_encode_status_output(call_id, wide_call_id, RDPEAR_STATUS_NOT_SUPPORTED);
			}
	}
}

static RD_BOOL
rdpear_send_response(const char *package_name, STREAM package_buffer)
{
	STREAM inner, wrapped, outer;

	inner = rdpear_encode_inner_packet(package_name, package_buffer);
	wrapped = cssp_remote_guard_wrap(inner);
	s_free(inner);

	if (wrapped == NULL)
		return False;

	outer = rdpear_encode_outer_packet(wrapped);
	s_free(wrapped);

	dvc_send(RDPEAR_CHANNEL_NAME, outer);
	s_free(outer);
	return True;
}

static void
rdpear_process_pdu(STREAM s)
{
	STREAM encrypted = NULL;
	STREAM plain = NULL;
	STREAM response = NULL;
	rdpear_inner_packet_t packet;

	if (!g_remote_guard)
		return;

	logger(Protocol, Debug, "rdpear_process_pdu(), received %u bytes",
	       (unsigned) s_remaining(s));

	if (!cssp_remote_guard_has_security_context())
	{
		logger(Core, Error,
		       "rdpear_process_pdu(), Remote Guard channel data arrived before CredSSP security context was retained");
		return;
	}

	if (!rdpear_parse_outer_packet(s, &encrypted))
		return;

	plain = cssp_remote_guard_unwrap(encrypted);
	s_free(encrypted);
	if (plain == NULL)
	{
		logger(Core, Warning,
		       "rdpear_process_pdu(), failed to decrypt Remote Guard authentication redirection payload");
		return;
	}

	if (!rdpear_parse_inner_packet(plain, &packet))
	{
		s_free(plain);
		return;
	}
	response = rdpear_process_package_buffer(packet.package_name, packet.buffer);

	if (response != NULL)
	{
		if (!rdpear_send_response(packet.package_name, response))
		{
			logger(Core, Warning,
			       "rdpear_process_pdu(), failed to send Remote Guard authentication redirection response");
		}
		s_free(response);
	}

	s_free(packet.buffer);
	s_free(plain);
}

RD_BOOL
rdpear_init(void)
{
	if (!g_remote_guard)
		return True;

	if (!dvc_channels_register(RDPEAR_CHANNEL_NAME, rdpear_process_pdu))
	{
		logger(Core, Error,
		       "rdpear_init(), failed to register Remote Guard authentication redirection channel '%s'",
		       RDPEAR_CHANNEL_NAME);
		return False;
	}

	logger(Core, Verbose,
	       "Registered Remote Guard authentication redirection channel '%s'",
	       RDPEAR_CHANNEL_NAME);
	return True;
}
