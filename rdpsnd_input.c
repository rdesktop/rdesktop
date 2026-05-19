/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Audio Input Redirection Virtual Channel Extension
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

#include "rdesktop.h"
#include "rdpsnd.h"

#define AUDIO_INPUT_CHANNEL_NAME "AUDIO_INPUT"

#define MSG_SNDIN_VERSION       0x01
#define MSG_SNDIN_FORMATS       0x02
#define MSG_SNDIN_OPEN          0x03
#define MSG_SNDIN_OPEN_REPLY    0x04
#define MSG_SNDIN_DATA_INCOMING 0x05
#define MSG_SNDIN_DATA          0x06
#define MSG_SNDIN_FORMATCHANGE  0x07

#define SNDIN_VERSION_1         0x00000001
#define SNDIN_S_OK             0x00000000
#define SNDIN_E_FAIL           0x80004005

#define MAX_INPUT_FORMATS       16

static RD_WAVEFORMATEX g_server_formats[MAX_INPUT_FORMATS];
static unsigned int g_server_format_count;
static unsigned int g_current_format;
static uint32 g_frames_per_packet;
static RD_BOOL g_input_open;
static RD_BOOL g_warned_unavailable;

static void rdpsnd_input_process(STREAM s);
static RD_BOOL rdpsnd_input_select_driver(void);
static void rdpsnd_input_send_version(void);
static void rdpsnd_input_send_formats(void);
static void rdpsnd_input_send_format_change(uint32 format_index);
static void rdpsnd_input_send_open_reply(uint32 result);
static void rdpsnd_input_close_device(void);

static RD_BOOL
rdpsnd_input_select_driver(void)
{
	struct audio_driver *driver;

	driver = rdpsnd_select_driver();
	if (driver == NULL)
	{
		if (!g_warned_unavailable)
		{
			logger(Sound, Warning,
			       "rdpsnd_input_select_driver(), no audio driver available for recording");
			g_warned_unavailable = True;
		}
		return False;
	}

	if (driver->wave_in_open == NULL || driver->wave_in_close == NULL ||
	    driver->wave_in_format_supported == NULL || driver->wave_in_set_format == NULL)
	{
		logger(Sound, Warning,
		       "rdpsnd_input_select_driver(), audio driver '%s' does not support recording",
		       driver->name);
		return False;
	}

	return True;
}

static STREAM
rdpsnd_input_init_packet(uint8 message_id, size_t length)
{
	STREAM s;

	s = xmalloc(sizeof(struct stream));
	memset(s, 0, sizeof(struct stream));
	s_realloc(s, length + 1);
	s_reset(s);
	out_uint8(s, message_id);
	return s;
}

static void
rdpsnd_input_send_packet(STREAM s)
{
	s_mark_end(s);
	dvc_send(AUDIO_INPUT_CHANNEL_NAME, s);
	s_free(s);
	xfree(s);
}

static void
rdpsnd_input_send_version(void)
{
	STREAM s;

	s = rdpsnd_input_init_packet(MSG_SNDIN_VERSION, 4);
	out_uint32_le(s, SNDIN_VERSION_1);
	rdpsnd_input_send_packet(s);
}

static void
rdpsnd_input_out_format(STREAM s, RD_WAVEFORMATEX *format)
{
	out_uint16_le(s, format->wFormatTag);
	out_uint16_le(s, format->nChannels);
	out_uint32_le(s, format->nSamplesPerSec);
	out_uint32_le(s, format->nAvgBytesPerSec);
	out_uint16_le(s, format->nBlockAlign);
	out_uint16_le(s, format->wBitsPerSample);
	out_uint16_le(s, 0);       /* cbSize */
}

static void
rdpsnd_input_send_formats(void)
{
	STREAM s;
	struct audio_driver *driver;
	unsigned int i;
	unsigned int count;

	if (!rdpsnd_input_select_driver())
		return;

	driver = rdpsnd_select_driver();
	count = 0;
	for (i = 0; i < g_server_format_count; i++)
	{
		if (driver->wave_in_format_supported(&g_server_formats[i]))
			count++;
	}

	s = rdpsnd_input_init_packet(MSG_SNDIN_FORMATS, 8 + count * 18);
	out_uint32_le(s, count);
	out_uint32_le(s, 1 + 4 + 4 + count * 18);

	for (i = 0; i < g_server_format_count; i++)
	{
		if (driver->wave_in_format_supported(&g_server_formats[i]))
			rdpsnd_input_out_format(s, &g_server_formats[i]);
	}

	rdpsnd_input_send_packet(s);
}

static RD_BOOL
rdpsnd_input_parse_format(STREAM s, RD_WAVEFORMATEX *format)
{
	uint16 extra_size;

	if (!s_check_rem(s, 18))
		return False;

	in_uint16_le(s, format->wFormatTag);
	in_uint16_le(s, format->nChannels);
	in_uint32_le(s, format->nSamplesPerSec);
	in_uint32_le(s, format->nAvgBytesPerSec);
	in_uint16_le(s, format->nBlockAlign);
	in_uint16_le(s, format->wBitsPerSample);
	in_uint16_le(s, extra_size);

	if (!s_check_rem(s, extra_size))
		return False;

	in_uint8s(s, extra_size);
	return True;
}

static void
rdpsnd_input_process_version(STREAM s)
{
	uint32 version;

	if (!s_check_rem(s, 4))
		return;

	in_uint32_le(s, version);
	logger(Sound, Debug, "rdpsnd_input_process_version(), server version %u", version);
	rdpsnd_input_send_version();
}

static void
rdpsnd_input_process_formats(STREAM s)
{
	uint32 count;
	uint32 ignored_size;
	unsigned int i;

	if (!s_check_rem(s, 8))
		return;

	in_uint32_le(s, count);
	in_uint32_le(s, ignored_size);
	UNUSED(ignored_size);

	if (count > MAX_INPUT_FORMATS)
		count = MAX_INPUT_FORMATS;

	g_server_format_count = 0;
	for (i = 0; i < count; i++)
	{
		if (!rdpsnd_input_parse_format(s, &g_server_formats[g_server_format_count]))
			break;
		g_server_format_count++;
	}

	logger(Sound, Debug, "rdpsnd_input_process_formats(), server sent %u formats",
	       g_server_format_count);
	rdpsnd_input_send_formats();
}

static void
rdpsnd_input_send_format_change(uint32 format_index)
{
	STREAM s;

	s = rdpsnd_input_init_packet(MSG_SNDIN_FORMATCHANGE, 4);
	out_uint32_le(s, format_index);
	rdpsnd_input_send_packet(s);
}

static void
rdpsnd_input_send_open_reply(uint32 result)
{
	STREAM s;

	s = rdpsnd_input_init_packet(MSG_SNDIN_OPEN_REPLY, 4);
	out_uint32_le(s, result);
	rdpsnd_input_send_packet(s);
}

static RD_BOOL
rdpsnd_input_set_format(uint32 format_index)
{
	struct audio_driver *driver;

	if (format_index >= g_server_format_count)
		return False;

	if (!rdpsnd_input_select_driver())
		return False;

	driver = rdpsnd_select_driver();
	if (!driver->wave_in_format_supported(&g_server_formats[format_index]))
		return False;

	if (g_input_open)
		driver->wave_in_close();

	if (!driver->wave_in_open())
	{
		g_input_open = False;
		return False;
	}

	if (!driver->wave_in_set_format(&g_server_formats[format_index]))
	{
		driver->wave_in_close();
		g_input_open = False;
		return False;
	}

	g_current_format = format_index;
	g_input_open = True;
	return True;
}

static void
rdpsnd_input_process_open(STREAM s)
{
	uint32 initial_format;
	RD_WAVEFORMATEX capture_format;

	if (!s_check_rem(s, 8))
		return;

	in_uint32_le(s, g_frames_per_packet);
	in_uint32_le(s, initial_format);

	if (!rdpsnd_input_parse_format(s, &capture_format))
	{
		rdpsnd_input_send_open_reply(SNDIN_E_FAIL);
		return;
	}

	UNUSED(capture_format);
	rdpsnd_input_send_format_change(initial_format);

	if (!rdpsnd_input_set_format(initial_format))
	{
		rdpsnd_input_send_open_reply(SNDIN_E_FAIL);
		return;
	}

	rdpsnd_input_send_open_reply(SNDIN_S_OK);
}

static void
rdpsnd_input_process_format_change(STREAM s)
{
	uint32 new_format;

	if (!s_check_rem(s, 4))
		return;

	in_uint32_le(s, new_format);
	if (rdpsnd_input_set_format(new_format))
		rdpsnd_input_send_format_change(new_format);
}

static void
rdpsnd_input_close_device(void)
{
	struct audio_driver *driver;

	if (!g_input_open)
		return;

	driver = rdpsnd_select_driver();
	if (driver != NULL && driver->wave_in_close != NULL)
		driver->wave_in_close();

	g_input_open = False;
}

static void
rdpsnd_input_process(STREAM s)
{
	uint8 message_id;

	if (!s_check_rem(s, 1))
		return;

	in_uint8(s, message_id);
	switch (message_id)
	{
		case MSG_SNDIN_VERSION:
			rdpsnd_input_process_version(s);
			break;
		case MSG_SNDIN_FORMATS:
			rdpsnd_input_process_formats(s);
			break;
		case MSG_SNDIN_OPEN:
			rdpsnd_input_process_open(s);
			break;
		case MSG_SNDIN_FORMATCHANGE:
			rdpsnd_input_process_format_change(s);
			break;
		default:
			logger(Sound, Warning,
			       "rdpsnd_input_process(), unhandled audio input message 0x%x",
			       message_id);
			break;
	}
}

void
rdpsnd_input_record(const void *data, unsigned int size)
{
	STREAM s;
	unsigned int chunk_size;
	unsigned int offset;
	unsigned int max_chunk_size;

	if (!g_input_open || data == NULL || size == 0)
		return;

	max_chunk_size = size;
	if (g_frames_per_packet > 0 && g_current_format < g_server_format_count)
	{
		max_chunk_size = g_frames_per_packet * g_server_formats[g_current_format].nBlockAlign;
		if (max_chunk_size == 0)
			max_chunk_size = size;
	}

	offset = 0;
	while (offset < size)
	{
		chunk_size = MIN(size - offset, max_chunk_size);

		s = rdpsnd_input_init_packet(MSG_SNDIN_DATA_INCOMING, 0);
		rdpsnd_input_send_packet(s);

		s = rdpsnd_input_init_packet(MSG_SNDIN_DATA, chunk_size);
		out_uint8a(s, ((const uint8 *) data) + offset, chunk_size);
		rdpsnd_input_send_packet(s);

		offset += chunk_size;
	}
}

void
rdpsnd_input_init(void)
{
	g_server_format_count = 0;
	g_current_format = 0;
	g_frames_per_packet = 0;
	g_input_open = False;
	g_warned_unavailable = False;
	dvc_channels_register(AUDIO_INPUT_CHANNEL_NAME, rdpsnd_input_process);
}

void
rdpsnd_input_reset_state(void)
{
	rdpsnd_input_close_device();
	g_server_format_count = 0;
	g_current_format = 0;
	g_frames_per_packet = 0;
}
