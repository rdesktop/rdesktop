/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Display Update Virtual Channel Extension.
   Copyright 2017 Henrik Andersson <hean01@cendio.com> for Cendio AB
   Copyright 2017 Karl Mikaelsson <derfian@cendio.se> for Cendio AB

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

#define DISPLAYCONTROL_PDU_TYPE_CAPS 0x5
#define DISPLAYCONTROL_PDU_TYPE_MONITOR_LAYOUT 0x2

#define DISPLAYCONTROL_MONITOR_PRIMARY 0x1
#define RDPEDISP_CHANNEL_NAME "Microsoft::Windows::RDS::DisplayControl"

extern int g_dpi;
extern RD_BOOL g_multimon;
extern RD_BOOL g_pending_resize_defer;
extern struct timeval g_pending_resize_defer_timer;

static void rdpedisp_send(STREAM s);
static void rdpedisp_init_packet(STREAM s, uint32 type, uint32 length);

static void
rdpedisp_process_caps_pdu(STREAM s)
{
	uint32 tmp[3];

	in_uint32_le(s, tmp[0]);	/* MaxNumMonitors */
	in_uint32_le(s, tmp[1]);	/* MaxMonitorAreaFactorA */
	in_uint32_le(s, tmp[2]);	/* MaxMonitorAreaFactorB */

	logger(Protocol, Debug,
	       "rdpedisp_process_caps_pdu(), Max supported monitor area (square pixels) is %d",
	       tmp[0] * tmp[1] * tmp[2]);

	/* When the RDPEDISP channel is established, we allow dynamic
	   session resize straight away by clearing the defer flag and
	   the defer timer. This lets process_pending_resize() start
	   processing pending resizes immediately. We expect that
	   process_pending_resize will prefer RDPEDISP resizes over
	   disconnect/reconnect resizes. */
	g_pending_resize_defer = False;
	g_pending_resize_defer_timer.tv_sec = 0;
	g_pending_resize_defer_timer.tv_usec = 0;
}

static void
rdpedisp_process_pdu(STREAM s)
{
	uint32 type;

	/* Read DISPLAYCONTROL_HEADER */
	in_uint32_le(s, type);	/* type */
	in_uint8s(s, 4);	/* length */

	logger(Protocol, Debug, "rdpedisp_process_pdu(), Got PDU type %d", type);

	switch (type)
	{
		case DISPLAYCONTROL_PDU_TYPE_CAPS:
			rdpedisp_process_caps_pdu(s);
			break;

		default:
			logger(Protocol, Warning, "rdpedisp_process_pdu(), Unhandled PDU type %d",
			       type);
			break;
	}
}

static void
rdpedisp_write_monitor_layout(STREAM s, const RDP_MONITOR_LAYOUT *monitor,
                              uint32 width, uint32 height)
{
	uint32 physwidth, physheight, desktopscale, devicescale;

	out_uint32_le(s, monitor->flags & RDESKTOP_MONITOR_PRIMARY ?
	              DISPLAYCONTROL_MONITOR_PRIMARY : 0);
	out_uint32_le(s, (uint32) monitor->left);
	out_uint32_le(s, (uint32) monitor->top);
	out_uint32_le(s, width);
	out_uint32_le(s, height);

	utils_calculate_dpi_scale_factors(width, height, g_dpi,
	                                  &physwidth, &physheight, &desktopscale, &devicescale);

	out_uint32_le(s, physwidth);     /* physicalWidth */
	out_uint32_le(s, physheight);    /* physicalHeight */
	out_uint32_le(s, ORIENTATION_LANDSCAPE); /* orientation */
	out_uint32_le(s, desktopscale);  /* desktopScaleFactor */
	out_uint32_le(s, devicescale);   /* deviceScaleFactor */
}

static void
rdpedisp_send_monitor_layout_pdu(uint32 width, uint32 height)
{
	struct stream s;
	RDP_MONITOR_LAYOUT monitors[RDESKTOP_MAX_MONITORS];
	uint32 monitor_count = 1;
	uint32 desktop_width = width;
	uint32 desktop_height = height;
	uint32 i;

	memset(&s, 0, sizeof(s));
	memset(monitors, 0, sizeof(monitors));

	if (!g_multimon ||
	    !ui_get_monitor_layout(monitors, RDESKTOP_MAX_MONITORS, &monitor_count,
	                           &desktop_width, &desktop_height))
	{
		monitors[0].left = 0;
		monitors[0].top = 0;
		monitors[0].right = width - 1;
		monitors[0].bottom = height - 1;
		monitors[0].flags = RDESKTOP_MONITOR_PRIMARY;
		monitor_count = 1;
	}

	logger(Protocol, Debug,
	       "rdpedisp_send_monitor_layout_pdu(), monitors = %u, desktop = %ux%u",
	       monitor_count, desktop_width, desktop_height);

	rdpedisp_init_packet(&s, DISPLAYCONTROL_PDU_TYPE_MONITOR_LAYOUT,
	                    16 + monitor_count * 40);

	out_uint32_le(&s, 40);          /* MonitorLayoutSize - spec mandates 40 */
	out_uint32_le(&s, monitor_count);       /* NumMonitors */

	for (i = 0; i < monitor_count; i++)
	{
		uint32 monitor_width = monitors[i].right - monitors[i].left + 1;
		uint32 monitor_height = monitors[i].bottom - monitors[i].top + 1;

		rdpedisp_write_monitor_layout(&s, &monitors[i], monitor_width, monitor_height);
	}

	s_mark_end(&s);
	rdpedisp_send(&s);
}

static void
rdpedisp_init_packet(STREAM s, uint32 type, uint32 length)
{
	s_realloc(s, length);
	s_reset(s);

	out_uint32_le(s, type);
	out_uint32_le(s, length);
}

static void
rdpedisp_send(STREAM s)
{
	dvc_send(RDPEDISP_CHANNEL_NAME, s);
}

RD_BOOL
rdpedisp_is_available()
{
	return dvc_channels_is_available(RDPEDISP_CHANNEL_NAME);
}

void
rdpedisp_set_session_size(uint32 width, uint32 height)
{
	if (rdpedisp_is_available() == False)
		return;

	/* monitor width MUST be even number */
	utils_apply_session_size_limitations(&width, &height);

	rdpedisp_send_monitor_layout_pdu(width, height);
}

void
rdpedisp_init(void)
{
	dvc_channels_register(RDPEDISP_CHANNEL_NAME, rdpedisp_process_pdu);
}
