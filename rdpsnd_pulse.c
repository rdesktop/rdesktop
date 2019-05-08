/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - PulseAudio
   Copyright (C) Krupen'ko Nikita <krnekit@gmail.com> 2010
   Copyright (C) Henrik Andersson <hean01@cendio.com> 2017

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

#include "rdesktop.h"
#include "rdpsnd.h"
#include "rdpsnd_dsp.h"
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>

#include <pulse/version.h>

#ifndef PA_CHECK_VERSION
#define PA_CHECK_VERSION(major, minor, micro)	(0)
#endif

#include <pulse/thread-mainloop.h>
#if PA_CHECK_VERSION(0,9,11)
#include <pulse/proplist.h>
#endif
#include <pulse/context.h>
#include <pulse/sample.h>
#include <pulse/stream.h>
#include <pulse/error.h>

#define DEFAULTDEVICE	NULL

/* Messages that may be sent from the PulseAudio thread */
enum RDPSND_PULSE_MSG_TYPE
{
	RDPSND_PULSE_OUT_AVAIL,	// Output space available in output stream
	RDPSND_PULSE_IN_AVAIL,	// Input data available in input stream
	RDPSND_PULSE_OUT_ERR,	// An error occured in the output stream
	RDPSND_PULSE_IN_ERR	// An error occured in the input strem
};

static pa_threaded_mainloop *mainloop;
#if PA_CHECK_VERSION(0,9,11)
static pa_proplist *proplist;
#endif
static pa_context *context;
static pa_stream *playback_stream;
static pa_stream *capture_stream;
static int pulse_ctl[2] = { -1, -1 };	// Pipe for comminicating with main thread

/* Streams states for the possibility of the proper reinitialization */
static RD_BOOL playback_started = False;
static RD_BOOL capture_started = False;

/* Device's parameters */
static const char *device;
static int playback_channels;
static int playback_samplerate;
static int playback_samplewidth;
static int capture_channels;
static int capture_samplerate;
static int capture_samplewidth;

/* Internal audio buffer sizes (latency) */
static const int playback_latency_part = 10;	// Playback latency (in part of second)
static const int capture_latency_part = 10;	// Capture latency (in part of second)

/* Capture buffer */
static void *capture_buf = NULL;
static size_t capture_buf_size = 0;

static RD_BOOL pulse_init(void);
static void pulse_deinit(void);
static RD_BOOL pulse_context_init(void);
static void pulse_context_deinit(void);
static RD_BOOL pulse_stream_open(pa_stream ** stream, int channels, int samplerate, int samplewidth,
				 pa_stream_flags_t flags);
static void pulse_stream_close(pa_stream ** stream);

static void pulse_send_msg(int fd, char message);

static RD_BOOL pulse_playback_start(void);
static RD_BOOL pulse_playback_stop(void);
static RD_BOOL pulse_playback_set_audio(int channels, int samplerate, int samplewidth);
static RD_BOOL pulse_play(void);

static RD_BOOL pulse_capture_start(void);
static RD_BOOL pulse_capture_stop(void);
static RD_BOOL pulse_capture_set_audio(int channels, int samplerate, int samplewidth);
static RD_BOOL pulse_record(void);

static RD_BOOL pulse_recover(pa_stream ** stream);

/* Callbacks for the PulseAudio events */
static void pulse_context_state_cb(pa_context * c, void *userdata);
static void pulse_stream_state_cb(pa_stream * p, void *userdata);
static void pulse_write_cb(pa_stream *, size_t nbytes, void *userdata);
static void pulse_read_cb(pa_stream * p, size_t nbytes, void *userdata);

static void pulse_cork_cb(pa_stream * p, int success, void *userdata);
static void pulse_flush_cb(pa_stream * p, int success, void *userdata);
static void pulse_update_timing_cb(pa_stream * p, int success, void *userdata);

static RD_BOOL
pulse_init(void)
{
	RD_BOOL ret = False;


	do
	{
		/* PulsaAudio mainloop thread initialization */
		mainloop = pa_threaded_mainloop_new();
		if (mainloop == NULL)
		{
			logger(Sound, Error,
			       "pulse_init(), Error creating PulseAudio threaded mainloop");
			break;
		}
		if (pa_threaded_mainloop_start(mainloop) != 0)
		{
			logger(Sound, Error,
			       "pulse_init(), Error starting PulseAudio threaded mainloop");
			break;
		}
#if PA_CHECK_VERSION(0,9,11)
		/* PulseAudio proplist initialization */
		proplist = pa_proplist_new();
		if (proplist == NULL)
		{
			logger(Sound, Error, "pulse_init(), Error creating PulseAudio proplist");
			break;
		}
		if (pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "rdesktop") != 0)
		{
			logger(Sound, Error,
			       "pulse_init(), Error setting option to PulseAudio proplist");
			break;
		}
#endif

		if (pulse_context_init() != True)
			break;

		ret = True;
	}
	while (0);

	if (ret != True)
		pulse_deinit();

	return ret;
}

static void
pulse_deinit(void)
{
	pulse_stream_close(&capture_stream);
	pulse_stream_close(&playback_stream);
	pulse_context_deinit();
#if PA_CHECK_VERSION(0,9,11)
	if (proplist != NULL)
	{
		pa_proplist_free(proplist);
		proplist = NULL;
	}
#endif
	if (mainloop != NULL)
	{
		pa_threaded_mainloop_stop(mainloop);
		pa_threaded_mainloop_free(mainloop);
		mainloop = NULL;
	}
}

static RD_BOOL
pulse_context_init(void)
{
	pa_context_flags_t flags;
	pa_context_state_t context_state;
	int err;
	RD_BOOL ret = False;


	pa_threaded_mainloop_lock(mainloop);

	do
	{
		/* Pipe for the control information from the audio thread */
		if (pipe(pulse_ctl) != 0)
		{
			logger(Sound, Error, "pulse_context_init(), pipe: %s", strerror(errno));
			pulse_ctl[0] = pulse_ctl[1] = -1;
			break;
		}
		if (fcntl(pulse_ctl[0], F_SETFL, O_NONBLOCK) == -1)
		{
			logger(Sound, Error, "pulse_context_init(), fcntl: %s", strerror(errno));
			break;
		}
#if PA_CHECK_VERSION(0,9,11)
		context =
			pa_context_new_with_proplist(pa_threaded_mainloop_get_api(mainloop), NULL,
						     proplist);
#else
		context = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "rdesktop");
#endif
		if (context == NULL)
		{
			logger(Sound, Error,
			       "pulse_context_init(), error creating PulseAudio context");
			break;
		}
		pa_context_set_state_callback(context, pulse_context_state_cb, mainloop);
		/* PulseAudio context connection */
#if PA_CHECK_VERSION(0,9,15)
		flags = PA_CONTEXT_NOFAIL;
#else
		flags = 0;
#endif
		if (pa_context_connect(context, NULL, flags, NULL) != 0)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_context_init(), %s", pa_strerror(err));
			break;
		}
		do
		{
			context_state = pa_context_get_state(context);
			if (context_state == PA_CONTEXT_READY || context_state == PA_CONTEXT_FAILED)
				break;
			else
				pa_threaded_mainloop_wait(mainloop);
		}
		while (1);
		if (context_state != PA_CONTEXT_READY)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_context_init(), %s", pa_strerror(err));
			break;
		}

		ret = True;
	}
	while (0);

	pa_threaded_mainloop_unlock(mainloop);

	return ret;
}

static void
pulse_context_deinit(void)
{
	int err;


	if (context != NULL)
	{
		pa_threaded_mainloop_lock(mainloop);

		pa_context_disconnect(context);
		pa_context_unref(context);
		context = NULL;

		pa_threaded_mainloop_unlock(mainloop);
	}

	if (pulse_ctl[0] != -1)
	{
		do
			err = close(pulse_ctl[0]);
		while (err == -1 && errno == EINTR);
		if (err == -1)
			logger(Sound, Error, "pulse_context_deinit(), close: %s", strerror(errno));
		pulse_ctl[0] = -1;
	}
	if (pulse_ctl[1] != -1)
	{
		do
			err = close(pulse_ctl[1]);
		while (err == -1 && errno == EINTR);
		if (err == -1)
			logger(Sound, Error, "pulse_context_deinit(), close: %s", strerror(errno));
		pulse_ctl[1] = 0;
	}
}

static RD_BOOL
pulse_stream_open(pa_stream ** stream, int channels, int samplerate, int samplewidth,
		  pa_stream_flags_t flags)
{
	pa_sample_spec samples;
	pa_buffer_attr buffer_attr;
	pa_stream_state_t state;
	int err;
	RD_BOOL ret = False;


	assert(stream != NULL);
	assert(stream == &playback_stream || stream == &capture_stream);

	logger(Sound, Debug, "pulse_stream_open(), channels=%d, samplerate=%d, samplewidth=%d",
	       channels, samplerate, samplewidth);

	pa_threaded_mainloop_lock(mainloop);

	do
	{
		/* PulseAudio sample format initialization */
#if PA_CHECK_VERSION(0,9,13)
		if (pa_sample_spec_init(&samples) == NULL)
		{
			logger(Sound, Error,
			       "pulse_stream_open(), error initializing PulseAudio sample format");
			break;
		}
#endif
		if (samplewidth == 2)
			samples.format = PA_SAMPLE_S16LE;
		else if (samplewidth == 1)
			samples.format = PA_SAMPLE_U8;
		else
		{
			logger(Sound, Error,
			       "pulse_stream_open(), wrong samplewidth for the PulseAudio stream: %d",
			       samplewidth);
			break;
		}
		samples.rate = samplerate;
		samples.channels = channels;
		if (!pa_sample_spec_valid(&samples))
		{
			logger(Sound, Error,
			       "pulse_stream_open(), Invalid PulseAudio sample format");
			break;
		}
		/* PulseAudio stream creation */
#if PA_CHECK_VERSION(0,9,11)
		if (stream == &playback_stream)
			*stream =
				pa_stream_new_with_proplist(context, "Playback Stream", &samples,
							    NULL, proplist);
		else
			*stream =
				pa_stream_new_with_proplist(context, "Capture Stream", &samples,
							    NULL, proplist);
#else
		if (stream == &playback_stream)
			*stream = pa_stream_new(context, "Playback Stream", &samples, NULL);
		else
			*stream = pa_stream_new(context, "Capture Stream", &samples, NULL);
#endif
		if (*stream == NULL)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_stream_open(), pa_stream_new: %s",
			       pa_strerror(err));
			break;
		}
		pa_stream_set_state_callback(*stream, pulse_stream_state_cb, mainloop);

		buffer_attr.maxlength = (uint32_t) - 1;
		buffer_attr.minreq = (uint32_t) - 1;
		buffer_attr.prebuf = (uint32_t) - 1;
		buffer_attr.tlength = (uint32_t) - 1;
		buffer_attr.fragsize = (uint32_t) - 1;

		/* PulseAudio stream connection */
		if (stream == &playback_stream)
		{
#if PA_CHECK_VERSION(0,9,0)
			buffer_attr.tlength =
				pa_usec_to_bytes(1000000 / playback_latency_part, &samples);
#else
			buffer_attr.tlength =
				(samples.rate / playback_latency_part) * samples.channels *
				(samples.format == PA_SAMPLE_S16LE ? 2 : 1);
#endif
			buffer_attr.prebuf = 0;
			buffer_attr.maxlength = buffer_attr.tlength;
		}
		else
		{
#if PA_CHECK_VERSION(0,9,0)
			buffer_attr.fragsize =
				pa_usec_to_bytes(1000000 / capture_latency_part, &samples);
#else
			buffer_attr.fragsize =
				(samples.rate / capture_latency_part) * samples.channels *
				(samples.format == PA_SAMPLE_S16LE ? 2 : 1);
#endif
			buffer_attr.maxlength = buffer_attr.fragsize;
		}

#if !PA_CHECK_VERSION(0,9,16)
		buffer_attr.minreq = (samples.rate / 50) * samples.channels * (samples.format == PA_SAMPLE_S16LE ? 2 : 1);	// 20 ms
#endif

		if (stream == &playback_stream)
			err = pa_stream_connect_playback(*stream, device, &buffer_attr, flags, NULL,
							 NULL);
		else
			err = pa_stream_connect_record(*stream, device, &buffer_attr, flags);
		if (err)
		{
			err = pa_context_errno(context);
			logger(Sound, Error,
			       "pulse_stream_open(), error connecting PulseAudio stream: %s",
			       pa_strerror(err));
			break;
		}
		do
		{
			state = pa_stream_get_state(*stream);
			if (state == PA_STREAM_READY || state == PA_STREAM_FAILED)
				break;
			else
				pa_threaded_mainloop_wait(mainloop);
		}
		while (1);
		if (state != PA_STREAM_READY)
		{
			err = pa_context_errno(context);
			logger(Sound, Error,
			       "pulse_stream_open(), error connecting PulseAudio stream: %s",
			       pa_strerror(err));
			break;
		}

#if PA_CHECK_VERSION(0,9,8)
		logger(Sound, Debug, "pulse_stream_open(), opened PulseAudio stream on device %s",
		       pa_stream_get_device_name(*stream));
#endif
#if PA_CHECK_VERSION(0,9,0)
		const pa_buffer_attr *res_ba;
		res_ba = pa_stream_get_buffer_attr(*stream);
		logger(Sound, Debug,
		       "pulse_stream_open(), PulseAudio stream buffer metrics: maxlength %u, minreq %u, prebuf %u, tlength %u, fragsize %u",
		       res_ba->maxlength, res_ba->minreq, res_ba->prebuf, res_ba->tlength,
		       res_ba->fragsize);
#endif

		/* Set the data callbacks for the PulseAudio stream */
		if (stream == &playback_stream)
			pa_stream_set_write_callback(*stream, pulse_write_cb, mainloop);
		else
			pa_stream_set_read_callback(*stream, pulse_read_cb, mainloop);

		ret = True;
	}
	while (0);

	pa_threaded_mainloop_unlock(mainloop);

	return ret;
}

static void
pulse_stream_close(pa_stream ** stream)
{
	pa_stream_state_t state;
	int err;


	assert(stream != NULL);

	if (*stream != NULL)
	{
		pa_threaded_mainloop_lock(mainloop);

		state = pa_stream_get_state(*stream);
		if (state == PA_STREAM_READY)
		{
			if (pa_stream_disconnect(*stream) != 0)
			{
				err = pa_context_errno(context);
				logger(Sound, Error,
				       "pulse_stream_close(), pa_stream_disconnect: %s\n",
				       pa_strerror(err));
			}
		}
		pa_stream_unref(*stream);
		*stream = NULL;

		pa_threaded_mainloop_unlock(mainloop);
	}
}

static void
pulse_send_msg(int fd, char message)
{
	int ret;


	do
		ret = write(fd, &message, sizeof message);
	while (ret == -1 && errno == EINTR);
	if (ret == -1)
		logger(Sound, Error, "pulse_send_msg(), error writing message to the pipe: %s\n",
		       strerror(errno));
}

static RD_BOOL
pulse_playback_start(void)
{
	RD_BOOL result = False;
	int ret;
	int err;
	pa_operation *po;


	if (playback_stream == NULL)
	{
		logger(Sound, Warning,
		       "pulse_playback_start(), trying to start PulseAudio stream while it's not exists");
		return True;
	}

	pa_threaded_mainloop_lock(mainloop);

	do
	{
		if (pa_stream_get_state(playback_stream) != PA_STREAM_READY)
		{
			logger(Sound, Warning,
			       "pulse_playback_start(), trying to start PulseAudio stream while it's not ready");
			break;
		}
#if PA_CHECK_VERSION(0,9,11)
		ret = pa_stream_is_corked(playback_stream);
#else
		ret = 1;
#endif
		if (ret < 0)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_playback_start(), pa_stream_is_corked: %s",
			       pa_strerror(err));
			break;
		}
		else if (ret != 0)
		{
			po = pa_stream_cork(playback_stream, 0, pulse_cork_cb, mainloop);
			if (po == NULL)
			{
				err = pa_context_errno(context);
				logger(Sound, Error, "pulse_playback_start(), pa_stream_corked: %s",
				       pa_strerror(err));
				break;
			}
			while (pa_operation_get_state(po) == PA_OPERATION_RUNNING)
				pa_threaded_mainloop_wait(mainloop);
			pa_operation_unref(po);
		}

		result = True;
	}
	while (0);

	pa_threaded_mainloop_unlock(mainloop);

	return result;
}

static RD_BOOL
pulse_playback_stop(void)
{
	RD_BOOL result = False;
	int ret;
	int err;
	pa_operation *po;


	if (playback_stream == NULL)
	{
		logger(Sound, Debug,
		       "pulse_playback_stop(), trying to stop PulseAudio stream while it's not exists");
		return True;
	}

	pa_threaded_mainloop_lock(mainloop);

	do
	{
		if (pa_stream_get_state(playback_stream) != PA_STREAM_READY)
		{
			logger(Sound, Error,
			       "pulse_playback_stop(), trying to stop PulseAudio stream while it's not ready");
			break;
		}
#if PA_CHECK_VERSION(0,9,11)
		ret = pa_stream_is_corked(playback_stream);
#else
		ret = 0;
#endif
		if (ret < 0)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_playback_stop(), pa_stream_is_corked: %s",
			       pa_strerror(err));
			break;
		}
		else if (ret == 0)
		{
			po = pa_stream_cork(playback_stream, 1, pulse_cork_cb, mainloop);
			if (po == NULL)
			{
				err = pa_context_errno(context);
				logger(Sound, Error, "pulse_playback_stop(), pa_stream_cork: %s",
				       pa_strerror(err));
				break;
			}
			while (pa_operation_get_state(po) == PA_OPERATION_RUNNING)
				pa_threaded_mainloop_wait(mainloop);
			pa_operation_unref(po);
		}
		po = pa_stream_flush(playback_stream, pulse_flush_cb, mainloop);
		if (po == NULL)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_playback_stop(), pa_stream_flush: %s",
			       pa_strerror(err));
			break;
		}
		while (pa_operation_get_state(po) == PA_OPERATION_RUNNING)
			pa_threaded_mainloop_wait(mainloop);
		pa_operation_unref(po);

		result = True;
	}
	while (0);

	pa_threaded_mainloop_unlock(mainloop);

	return result;
}

static RD_BOOL
pulse_playback_set_audio(int channels, int samplerate, int samplewidth)
{
	pa_stream_flags_t flags;


	pulse_stream_close(&playback_stream);

	flags = PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING |
		PA_STREAM_AUTO_TIMING_UPDATE;
#if PA_CHECK_VERSION(0,9,11)
	flags |= PA_STREAM_ADJUST_LATENCY;
#endif
	if (pulse_stream_open(&playback_stream, channels, samplerate, samplewidth, flags) != True)
		return False;

	return True;
}

static RD_BOOL
pulse_capture_start(void)
{
	RD_BOOL result = False;
	int ret;
	int err;
	pa_operation *po;


	if (capture_stream == NULL)
	{
		logger(Sound, Warning,
		       "pulse_capture_start(), trying to start PulseAudio stream while it's not exists");
		return True;
	}

	pa_threaded_mainloop_lock(mainloop);

	do
	{
		if (pa_stream_get_state(capture_stream) != PA_STREAM_READY)
		{
			logger(Sound, Error,
			       "pulse_capture_start(), trying to start PulseAudio stream while it's not exists");
			break;
		}
#if PA_CHECK_VERSION(0,9,11)
		ret = pa_stream_is_corked(capture_stream);
#else
		ret = 1;
#endif
		if (ret < 0)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_capture_start(), pa_stream_is_corked: %s",
			       pa_strerror(err));
			break;
		}
		else if (ret != 0)
		{
			po = pa_stream_cork(capture_stream, 0, pulse_cork_cb, mainloop);
			if (po == NULL)
			{
				err = pa_context_errno(context);
				logger(Sound, Error, "pulse_capture_start(), pa_stream_cork: %s\n",
				       pa_strerror(err));
				break;
			}
			while (pa_operation_get_state(po) == PA_OPERATION_RUNNING)
				pa_threaded_mainloop_wait(mainloop);
			pa_operation_unref(po);
		}

		result = True;
	}
	while (0);

	pa_threaded_mainloop_unlock(mainloop);

	return result;
}

static RD_BOOL
pulse_capture_stop(void)
{
	RD_BOOL result = False;
	int ret;
	int err;
	pa_operation *po;


	if (capture_stream == NULL)
	{
		logger(Sound, Debug,
		       "pulse_capture_stop(), trying to stop PulseAudio stream while it's not exists");
		return True;
	}

	pa_threaded_mainloop_lock(mainloop);

	do
	{
		if (pa_stream_get_state(capture_stream) != PA_STREAM_READY)
		{
			logger(Sound, Error,
			       "pulse_capture_stop(), trying to stop PulseAudio stream while it's not exists");
			break;
		}
#if PA_CHECK_VERSION(0,9,11)
		ret = pa_stream_is_corked(capture_stream);
#else
		ret = 0;
#endif
		if (ret < 0)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_capture_stop(), pa_stream_is_corked: %s\n",
			       pa_strerror(err));
			break;
		}
		else if (ret == 0)
		{
			po = pa_stream_cork(capture_stream, 1, pulse_cork_cb, mainloop);
			if (po == NULL)
			{
				err = pa_context_errno(context);
				logger(Sound, Error, "pulse_capture_stop(), pa_stream_cork: %s\n",
				       pa_strerror(err));
				break;
			}
			while (pa_operation_get_state(po) == PA_OPERATION_RUNNING)
				pa_threaded_mainloop_wait(mainloop);
			pa_operation_unref(po);
		}

		result = True;
	}
	while (0);

	pa_threaded_mainloop_unlock(mainloop);

	return result;
}

static RD_BOOL
pulse_capture_set_audio(int channels, int samplerate, int samplewidth)
{
	pa_stream_flags_t flags;
	pa_stream_state_t state;
	int ret;
	int err;


	flags = PA_STREAM_START_CORKED;
#if PA_CHECK_VERSION(0,9,11)
	flags |= PA_STREAM_ADJUST_LATENCY;
#endif

	if (capture_stream != NULL)
	{
		pa_threaded_mainloop_lock(mainloop);
		state = pa_stream_get_state(capture_stream);
		if (state == PA_STREAM_READY)
		{
#if PA_CHECK_VERSION(0,9,11)
			ret = pa_stream_is_corked(capture_stream);
#else
			ret = (capture_started == False);
#endif
			if (ret == 0)
				flags &= ~PA_STREAM_START_CORKED;
			else if (ret < 0)
			{
				err = pa_context_errno(context);
				pa_threaded_mainloop_unlock(mainloop);
				logger(Sound, Error,
				       "pulse_capture_set_audio(), pa_stream_is_corked: %s\n",
				       pa_strerror(err));
				return False;
			}
		}
		pa_threaded_mainloop_unlock(mainloop);
	}

	pulse_stream_close(&capture_stream);

	if (pulse_stream_open(&capture_stream, channels, samplerate, samplewidth, flags) != True)
		return False;

	return True;
}

static void
pulse_context_state_cb(pa_context * c, void *userdata)
{
	pa_context_state_t state;


	assert(userdata != NULL);

	state = pa_context_get_state(c);
	if (state == PA_CONTEXT_READY || state == PA_CONTEXT_FAILED)
		pa_threaded_mainloop_signal((pa_threaded_mainloop *) userdata, 0);
}

static void
pulse_stream_state_cb(pa_stream * p, void *userdata)
{
	pa_stream_state_t state;


	assert(userdata != NULL);

	state = pa_stream_get_state(p);
	if (state == PA_STREAM_FAILED)
	{
		if (p == playback_stream)
		{
			logger(Sound, Debug,
			       "pulse_stream_state_cb(), PulseAudio playback stream is in a fail state");
			pulse_send_msg(pulse_ctl[1], RDPSND_PULSE_OUT_ERR);
		}
		else
		{
			logger(Sound, Debug,
			       "pulse_stream_state_cb(), PulseAudio capture stream is in a fail state");
			pulse_send_msg(pulse_ctl[1], RDPSND_PULSE_IN_ERR);
		}
	}
	if (state == PA_STREAM_READY || state == PA_STREAM_FAILED)
		pa_threaded_mainloop_signal((pa_threaded_mainloop *) userdata, 0);
}

static void
pulse_read_cb(pa_stream * p, size_t nbytes, void *userdata)
{
	assert(userdata != NULL);

	pulse_send_msg(pulse_ctl[1], RDPSND_PULSE_IN_AVAIL);
}

static void
pulse_write_cb(pa_stream * p, size_t nbytes, void *userdata)
{
	assert(userdata != NULL);

	pulse_send_msg(pulse_ctl[1], RDPSND_PULSE_OUT_AVAIL);
}

static void
pulse_cork_cb(pa_stream * p, int success, void *userdata)
{
	assert(userdata != NULL);

	if (!success)
	{
		if (p == playback_stream)
		{
			logger(Sound, Warning,
			       "pulse_cork_cb(), fail to cork/uncork the PulseAudio playback stream: %s",
			       pa_strerror(pa_context_errno(context)));
			pulse_send_msg(pulse_ctl[1], RDPSND_PULSE_OUT_ERR);
		}
		else
		{
			logger(Sound, Warning,
			       "pulse_cork_cb(), fail to cork/uncork the PulseAudio capture stream: %s",
			       pa_strerror(pa_context_errno(context)));
			pulse_send_msg(pulse_ctl[1], RDPSND_PULSE_IN_ERR);
		}
	}

	pa_threaded_mainloop_signal((pa_threaded_mainloop *) userdata, 0);
}

static void
pulse_flush_cb(pa_stream * p, int success, void *userdata)
{
	assert(userdata != NULL);

	if (!success)
	{
		logger(Sound, Warning, "pulse_flush_cb(), Fail to flush the PulseAudio stream: %s",
		       pa_strerror(pa_context_errno(context)));
		pulse_send_msg(pulse_ctl[1], RDPSND_PULSE_OUT_ERR);
	}

	pa_threaded_mainloop_signal((pa_threaded_mainloop *) userdata, 0);
}

static void
pulse_update_timing_cb(pa_stream * p, int success, void *userdata)
{
	assert(userdata != NULL);

	if (!success)
	{
		logger(Sound, Warning,
		       "pulse_update_timing_cb(), fail to update timing info of the PulseAudio stream: %s",
		       pa_strerror(pa_context_errno(context)));
		pulse_send_msg(pulse_ctl[1], RDPSND_PULSE_OUT_ERR);
	}

	pa_threaded_mainloop_signal((pa_threaded_mainloop *) userdata, 0);
}

void
pulse_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv)
{
	if (pulse_ctl[0] != -1)
	{
		if (pulse_ctl[0] > *n)
			*n = pulse_ctl[0];

		FD_SET(pulse_ctl[0], rfds);
	}
}

void
pulse_check_fds(fd_set * rfds, fd_set * wfds)
{
	char audio_cmd;
	int n;


	if (pulse_ctl[0] == -1)
		return;

	if (FD_ISSET(pulse_ctl[0], rfds))
	{
		do
		{
			n = read(pulse_ctl[0], &audio_cmd, sizeof audio_cmd);
			if (n == -1)
			{
				if (errno == EINTR)
					continue;
				else if (errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				else
				{
					logger(Sound, Error, "pulse_check_fds(), read: %s\n",
					       strerror(errno));
					return;
				}
			}
			else if (n == 0)
			{
				logger(Sound, Warning,
				       "pulse_check_fds(), audio control pipe was closed");
				break;
			}
			else
				switch (audio_cmd)
				{
					case RDPSND_PULSE_OUT_AVAIL:
						if (pulse_play() != True)
							if (pulse_recover(&playback_stream) != True)
							{
								logger(Sound, Error,
								       "pulse_check_fds(), PulseAudio playback error");
								return;
							}
						break;
					case RDPSND_PULSE_IN_AVAIL:
						if (pulse_record() != True)
							if (pulse_recover(&capture_stream) != True)
							{
								logger(Sound, Error,
								       "pulse_check_fds(), PulseAudio capture error");
								return;
							}
						break;
					case RDPSND_PULSE_OUT_ERR:
						if (pulse_recover(&playback_stream) != True)
						{
							logger(Sound, Error,
							       "pulse_check_fds(), an error occured in audio thread with PulseAudio playback stream");
							return;
						}
						break;
					case RDPSND_PULSE_IN_ERR:
						if (pulse_recover(&capture_stream) != True)
						{
							logger(Sound, Error,
							       "pulse_check_fds(), an error occured in audio thread with PulseAudio capture stream");
							return;
						}
						break;
					default:
						logger(Sound, Error,
						       "pulse_check_fds(), wrong command from the audio thread: %d",
						       audio_cmd);
						break;
				}
		}
		while (1);
	}

	return;
}

RD_BOOL
pulse_open_out(void)
{
	if (context == NULL || mainloop == NULL)
		if (pulse_init() != True)
			return False;

	return True;
}

void
pulse_close_out(void)
{
	/* Ack all remaining packets */
	while (!rdpsnd_queue_empty())
		rdpsnd_queue_next(0);

	playback_started = False;

	if (playback_stream && pulse_playback_stop() != True)
		if (pulse_recover(&playback_stream) != True)
		{
			logger(Sound, Error,
			       "pulse_close_out(), fail to close the PulseAudio playback stream");
			return;
		}
}

RD_BOOL
pulse_format_supported(RD_WAVEFORMATEX * pwfx)
{
	if (pwfx->wFormatTag != WAVE_FORMAT_PCM)
		return False;
	if ((pwfx->nChannels != 1) && (pwfx->nChannels != 2))
		return False;
	if ((pwfx->wBitsPerSample != 8) && (pwfx->wBitsPerSample != 16))
		return False;

	return True;
}

RD_BOOL
pulse_set_format_out(RD_WAVEFORMATEX * pwfx)
{
	if (playback_stream == NULL
	    || playback_channels != pwfx->nChannels
	    || playback_samplerate != pwfx->nSamplesPerSec
	    || playback_samplewidth != pwfx->wBitsPerSample / 8)
	{
		playback_channels = pwfx->nChannels;
		playback_samplerate = pwfx->nSamplesPerSec;
		playback_samplewidth = pwfx->wBitsPerSample / 8;

		if (pulse_playback_set_audio
		    (pwfx->nChannels, pwfx->nSamplesPerSec, pwfx->wBitsPerSample / 8) != True)
			if (pulse_recover(&playback_stream) != True)
			{
				logger(Sound, Error,
				       "pulse_set_format_out(), fail to open the PulseAudio playback stream");
				return False;
			}
	}

	playback_started = True;

	if (pulse_playback_start() != True)
		if (pulse_recover(&playback_stream) != True)
		{
			logger(Sound, Error,
			       "pulse_set_format_out(), fail to start the PulseAudio playback stream");
			return False;
		}

	return True;
}

RD_BOOL
pulse_play(void)
{
	struct audio_packet *packet;
	STREAM out;
	const pa_timing_info *ti;
	pa_operation *po;
	pa_seek_mode_t playback_seek;
	size_t avail_space, audio_size;
	pa_usec_t delay = 0;
	int ret;
	int err;
	RD_BOOL result = False;


	if (rdpsnd_queue_empty())
		return True;

	if (playback_stream == NULL)
		return False;

	pa_threaded_mainloop_lock(mainloop);

	do
	{
		packet = rdpsnd_queue_current_packet();
		out = packet->s;

		ti = pa_stream_get_timing_info(playback_stream);
		if (ti == NULL)
		{
			err = pa_context_errno(context);
			logger(Sound, Error, "pulse_play(), pa_stream_get_timing_info: %s",
			       pa_strerror(err));
			break;
		}

		if (ti->read_index_corrupt || ti->write_index_corrupt)
		{
			po = pa_stream_update_timing_info(playback_stream, pulse_update_timing_cb,
							  mainloop);
			if (po == NULL)
			{
				err = pa_context_errno(context);
				logger(Sound, Error,
				       "pulse_play(), pa_stream_update_timing_info: %s",
				       pa_strerror(err));
				break;
			}
			while (pa_operation_get_state(po) == PA_OPERATION_RUNNING)
				pa_threaded_mainloop_wait(mainloop);
			pa_operation_unref(po);
		}

		if (ti->read_index > ti->write_index)
		{
			logger(Sound, Debug, "pulse_play(), PulseAudio stream underflow %ld bytes",
			       (long) (ti->read_index - ti->write_index));
			playback_seek = PA_SEEK_RELATIVE_ON_READ;
		}
		else
			playback_seek = PA_SEEK_RELATIVE;

		avail_space = pa_stream_writable_size(playback_stream);
		audio_size = MIN(s_remaining(out), avail_space);
		if (audio_size)
		{
			unsigned char *data;

			in_uint8p(out, data, audio_size);
			if (pa_stream_write
			    (playback_stream, data, audio_size, NULL, 0, playback_seek) != 0)
			{
				err = pa_context_errno(context);
				logger(Sound, Error, "pulse_play(), pa_stream_write: %s",
				       pa_strerror(err));
				break;
			}
			else if (playback_seek == PA_SEEK_RELATIVE_ON_READ)
				playback_seek = PA_SEEK_RELATIVE;
		}

		if (s_check_end(out))
		{
			ret = pa_stream_get_latency(playback_stream, &delay, NULL);
			if (ret != 0 && (err = pa_context_errno(context)) == PA_ERR_NODATA)
			{
				po = pa_stream_update_timing_info(playback_stream,
								  pulse_update_timing_cb, mainloop);
				if (po == NULL)
				{
					delay = 0;
					err = pa_context_errno(context);
					logger(Sound, Error,
					       "pulse_play(), pa_stream_update_timing_info: %s",
					       pa_strerror(err));
					break;
				}
				while (pa_operation_get_state(po) == PA_OPERATION_RUNNING)
					pa_threaded_mainloop_wait(mainloop);
				pa_operation_unref(po);

				ret = pa_stream_get_latency(playback_stream, &delay, NULL);
			}
			if (ret != 0)
			{
				delay = 0;
				err = pa_context_errno(context);
				logger(Sound, Error, "pulse_play(), pa_stream_get_latency: %s",
				       pa_strerror(err));
				break;
			}

			logger(Sound, Debug,
			       "pulse_play(), PulseAudio playback stream latency %lu usec",
			       (long) delay);
		}

		result = True;
	}
	while (0);

	pa_threaded_mainloop_unlock(mainloop);

	if (s_check_end(out))
		rdpsnd_queue_next(delay);

	return result;
}

RD_BOOL
pulse_open_in(void)
{
	if (context == NULL || mainloop == NULL)
		if (pulse_init() != True)
			return False;

	return True;
}

void
pulse_close_in(void)
{
	capture_started = False;

	if (capture_stream && pulse_capture_stop() != True)
		if (pulse_recover(&capture_stream) != True)
		{
			logger(Sound, Error,
			       "pulse_close_in(), fail to close the PulseAudio capture stream");
			return;
		}
}

RD_BOOL
pulse_set_format_in(RD_WAVEFORMATEX * pwfx)
{
	if (capture_stream == NULL
	    || capture_channels != pwfx->nChannels
	    || capture_samplerate != pwfx->nSamplesPerSec
	    || capture_samplewidth != pwfx->wBitsPerSample / 8)
	{
		capture_channels = pwfx->nChannels;
		capture_samplerate = pwfx->nSamplesPerSec;
		capture_samplewidth = pwfx->wBitsPerSample / 8;

		if (pulse_capture_set_audio
		    (pwfx->nChannels, pwfx->nSamplesPerSec, pwfx->wBitsPerSample / 8) != True)
			if (pulse_recover(&capture_stream) != True)
			{
				logger(Sound, Error,
				       "pulse_set_format_in(), fail to open the PulseAudio capture stream");
				return False;
			}
	}

	capture_started = True;

	if (pulse_capture_start() != True)
		if (pulse_recover(&capture_stream) != True)
		{
			logger(Sound, Error,
			       "pulse_set_format_in(), fail to start the PulseAudio capture stream");
			return False;
		}

	return True;
}

RD_BOOL
pulse_record(void)
{
	const void *pulse_buf;
	size_t audio_size;
	RD_BOOL result = False;


	if (capture_stream == NULL)
		return False;

	pa_threaded_mainloop_lock(mainloop);

	do
	{
		if (pa_stream_peek(capture_stream, &pulse_buf, &audio_size) != 0)
		{
			logger(Sound, Error, "pulse_record(), pa_stream_peek: %s",
			       pa_strerror(pa_context_errno(context)));
			break;
		}

		/* Stretch the buffer, if needed */
		if (capture_buf_size < audio_size)
		{
			capture_buf_size = audio_size;
			if (capture_buf != NULL)
				free(capture_buf);
			capture_buf = malloc(capture_buf_size);
			if (capture_buf == NULL)
			{
				logger(Sound, Error, "pulse_record(), malloc error");
				capture_buf_size = 0;
				break;
			}
		}

		memcpy(capture_buf, pulse_buf, audio_size);

		if (pa_stream_drop(capture_stream) != 0)
		{
			logger(Sound, Error, "pulse_record(), pa_stream_drop: %s",
			       pa_strerror(pa_context_errno(context)));
			break;
		}

		result = True;
	}
	while (0);

	pa_threaded_mainloop_unlock(mainloop);

	if (result == True)
		rdpsnd_record(capture_buf, audio_size);

	return result;
}

static RD_BOOL
pulse_recover(pa_stream ** stream)
{
	RD_BOOL playback, capture;


	playback = capture = False;

	if (playback_stream != NULL)
		playback = True;
	if (capture_stream != NULL)
		capture = True;

	if (stream == &playback_stream)
	{
		if (pulse_playback_set_audio
		    (playback_channels, playback_samplerate, playback_samplewidth) == True)
			if (playback_started != True || pulse_playback_start() == True)
				return True;
	}
	else if (stream == &capture_stream)
	{
		if (pulse_capture_set_audio
		    (capture_channels, capture_samplerate, capture_samplewidth) == True)
			if (capture_started != True || pulse_capture_start() == True)
				return True;
	}

	pulse_deinit();

	if (pulse_init() != True)
		return False;

	do
	{
		if (playback == True)
		{
			if (pulse_playback_set_audio
			    (playback_channels, playback_samplerate, playback_samplewidth) != True
			    || (playback_started == True && pulse_playback_start() != True))
				break;
		}
		if (capture == True)
		{
			if (pulse_capture_set_audio
			    (capture_channels, capture_samplerate, capture_samplewidth) != True
			    || (capture_started == True && pulse_capture_start() != True))
				break;
		}

		return True;
	}
	while (0);

	pulse_deinit();

	return False;
}

struct audio_driver *
pulse_register(char *options)
{
	static struct audio_driver pulse_driver;

	memset(&pulse_driver, 0, sizeof(pulse_driver));

	pulse_driver.name = "pulse";
	pulse_driver.description = "PulseAudio output driver, default device: system dependent";

	pulse_driver.add_fds = pulse_add_fds;
	pulse_driver.check_fds = pulse_check_fds;

	pulse_driver.wave_out_open = pulse_open_out;
	pulse_driver.wave_out_close = pulse_close_out;
	pulse_driver.wave_out_format_supported = pulse_format_supported;
	pulse_driver.wave_out_set_format = pulse_set_format_out;
	pulse_driver.wave_out_volume = rdpsnd_dsp_softvol_set;

	pulse_driver.wave_in_open = pulse_open_in;
	pulse_driver.wave_in_close = pulse_close_in;
	pulse_driver.wave_in_format_supported = pulse_format_supported;
	pulse_driver.wave_in_set_format = pulse_set_format_in;
	pulse_driver.wave_in_volume = NULL;	/* FIXME */

	pulse_driver.need_byteswap_on_be = 0;
	pulse_driver.need_resampling = 0;

	if (options != NULL)
	{
		device = xstrdup(options);
	}
	else
	{
		device = DEFAULTDEVICE;
	}

	return &pulse_driver;
}
