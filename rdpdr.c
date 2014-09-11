/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright 2004-2011 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright 2010-2014 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

/*
  Here are some resources, for your IRP hacking pleasure:

  http://cvs.sourceforge.net/viewcvs.py/mingw/w32api/include/ddk/winddk.h?view=markup

  http://win32.mvps.org/ntfs/streams.cpp

  http://www.acc.umu.se/~bosse/ntifs.h

  http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/

  http://us1.samba.org/samba/ftp/specs/smb-nt01.txt

  http://www.osronline.com/
*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>		/* opendir, closedir, readdir */
#include <time.h>
#include <errno.h>
#include "rdesktop.h"

#define IRP_MJ_CREATE			0x00
#define IRP_MJ_CLOSE			0x02
#define IRP_MJ_READ			0x03
#define IRP_MJ_WRITE			0x04
#define	IRP_MJ_QUERY_INFORMATION	0x05
#define IRP_MJ_SET_INFORMATION		0x06
#define IRP_MJ_QUERY_VOLUME_INFORMATION	0x0a
#define IRP_MJ_DIRECTORY_CONTROL	0x0c
#define IRP_MJ_DEVICE_CONTROL		0x0e
#define IRP_MJ_LOCK_CONTROL             0x11

#define IRP_MN_QUERY_DIRECTORY          0x01
#define IRP_MN_NOTIFY_CHANGE_DIRECTORY  0x02

extern char g_hostname[16];
extern DEVICE_FNS serial_fns;
extern DEVICE_FNS printer_fns;
extern DEVICE_FNS parallel_fns;
extern DEVICE_FNS disk_fns;
#ifdef WITH_SCARD
extern DEVICE_FNS scard_fns;
#endif
extern FILEINFO g_fileinfo[];
extern RD_BOOL g_notify_stamp;

static VCHANNEL *rdpdr_channel;
static uint32 g_epoch;

/* If select() times out, the request for the device with handle g_min_timeout_fd is aborted */
RD_NTHANDLE g_min_timeout_fd;
uint32 g_num_devices;

uint32 g_client_id;

/* Table with information about rdpdr devices */
RDPDR_DEVICE g_rdpdr_device[RDPDR_MAX_DEVICES];
char *g_rdpdr_clientname = NULL;

/* Used to store incoming io request, until they are ready to be completed */
/* using a linked list ensures that they are processed in the right order, */
/* if multiple ios are being done on the same fd */
struct async_iorequest
{
	uint32 fd, major, minor, offset, device, id, length, partial_len;
	long timeout,		/* Total timeout */
	  itv_timeout;		/* Interval timeout (between serial characters) */
	uint8 *buffer;
	DEVICE_FNS *fns;

	struct async_iorequest *next;	/* next element in list */
};

struct async_iorequest *g_iorequest;

/* Return device_id for a given handle */
int
get_device_index(RD_NTHANDLE handle)
{
	int i;
	for (i = 0; i < RDPDR_MAX_DEVICES; i++)
	{
		if (g_rdpdr_device[i].handle == handle)
			return i;
	}
	return -1;
}

/* Converts a windows path to a unix path */
void
convert_to_unix_filename(char *filename)
{
	char *p;

	while ((p = strchr(filename, '\\')))
	{
		*p = '/';
	}
}

static RD_BOOL
rdpdr_handle_ok(int device, int handle)
{
	switch (g_rdpdr_device[device].device_type)
	{
		case DEVICE_TYPE_PARALLEL:
		case DEVICE_TYPE_SERIAL:
		case DEVICE_TYPE_PRINTER:
		case DEVICE_TYPE_SCARD:
			if (g_rdpdr_device[device].handle != handle)
				return False;
			break;
		case DEVICE_TYPE_DISK:
			if (g_fileinfo[handle].device_id != device)
				return False;
			break;
	}
	return True;
}

/* Add a new io request to the table containing pending io requests so it won't block rdesktop */
static RD_BOOL
add_async_iorequest(uint32 device, uint32 file, uint32 id, uint32 major, uint32 length,
		    DEVICE_FNS * fns, uint32 total_timeout, uint32 interval_timeout, uint8 * buffer,
		    uint32 offset)
{
	struct async_iorequest *iorq;

	if (g_iorequest == NULL)
	{
		g_iorequest = (struct async_iorequest *) xmalloc(sizeof(struct async_iorequest));
		if (!g_iorequest)
			return False;
		g_iorequest->fd = 0;
		g_iorequest->next = NULL;
	}

	iorq = g_iorequest;

	while (iorq->fd != 0)
	{
		/* create new element if needed */
		if (iorq->next == NULL)
		{
			iorq->next =
				(struct async_iorequest *) xmalloc(sizeof(struct async_iorequest));
			if (!iorq->next)
				return False;
			iorq->next->fd = 0;
			iorq->next->next = NULL;
		}
		iorq = iorq->next;
	}
	iorq->device = device;
	iorq->fd = file;
	iorq->id = id;
	iorq->major = major;
	iorq->length = length;
	iorq->partial_len = 0;
	iorq->fns = fns;
	iorq->timeout = total_timeout;
	iorq->itv_timeout = interval_timeout;
	iorq->buffer = buffer;
	iorq->offset = offset;
	return True;
}

static void
rdpdr_send_client_announce_reply(void)
{
	/* DR_CORE_CLIENT_ANNOUNCE_RSP */
	STREAM s;
	s = channel_init(rdpdr_channel, 12);
	out_uint16_le(s, RDPDR_CTYP_CORE);
	out_uint16_le(s, PAKID_CORE_CLIENTID_CONFIRM);
	out_uint16_le(s, 1);	/* VersionMajor, MUST be set to 0x1 */
	out_uint16_le(s, 5);	/* VersionMinor */
	out_uint32_be(s, g_client_id);	/* ClientID */
	s_mark_end(s);
	channel_send(s, rdpdr_channel);
}


static void
rdpdr_send_client_name_request(void)
{
	/* DR_CORE_CLIENT_NAME_REQ */
	STREAM s;
	uint32 hostlen;

	if (NULL == g_rdpdr_clientname)
	{
		g_rdpdr_clientname = g_hostname;
	}
	hostlen = (strlen(g_rdpdr_clientname) + 1) * 2;

	s = channel_init(rdpdr_channel, 16 + hostlen);
	out_uint16_le(s, RDPDR_CTYP_CORE);
	out_uint16_le(s, PAKID_CORE_CLIENT_NAME);
	out_uint32_le(s, 1);	/* UnicodeFlag */
	out_uint32_le(s, 0);	/* CodePage */
	out_uint32_le(s, hostlen);	/* ComputerNameLen */
	rdp_out_unistr(s, g_rdpdr_clientname, hostlen - 2);
	s_mark_end(s);
	channel_send(s, rdpdr_channel);
}

/* Returns the size of the payload of the announce packet */
static int
announcedata_size()
{
	int size, i;
	PRINTER *printerinfo;

	size = 8;		/* static announce size */
	size += g_num_devices * 0x14;

	for (i = 0; i < g_num_devices; i++)
	{
		if (g_rdpdr_device[i].device_type == DEVICE_TYPE_PRINTER)
		{
			printerinfo = (PRINTER *) g_rdpdr_device[i].pdevice_data;
			printerinfo->bloblen =
				printercache_load_blob(printerinfo->printer, &(printerinfo->blob));

			size += 0x18;
			size += 2 * strlen(printerinfo->driver) + 2;
			size += 2 * strlen(printerinfo->printer) + 2;
			size += printerinfo->bloblen;
		}
	}

	return size;
}

static void
rdpdr_send_client_device_list_announce(void)
{
	/* DR_CORE_CLIENT_ANNOUNCE_RSP */
	uint32 driverlen, printerlen, bloblen;
	int i;
	STREAM s;
	PRINTER *printerinfo;

	s = channel_init(rdpdr_channel, announcedata_size());
	out_uint16_le(s, RDPDR_CTYP_CORE);
	out_uint16_le(s, PAKID_CORE_DEVICE_LIST_ANNOUNCE);

	out_uint32_le(s, g_num_devices);

	for (i = 0; i < g_num_devices; i++)
	{
		out_uint32_le(s, g_rdpdr_device[i].device_type);
		out_uint32_le(s, i);	/* RDP Device ID */
		/* Is it possible to use share names longer than 8 chars?
		   /astrand */
		out_uint8p(s, g_rdpdr_device[i].name, 8);

		switch (g_rdpdr_device[i].device_type)
		{
			case DEVICE_TYPE_PRINTER:
				printerinfo = (PRINTER *) g_rdpdr_device[i].pdevice_data;

				driverlen = 2 * strlen(printerinfo->driver) + 2;
				printerlen = 2 * strlen(printerinfo->printer) + 2;
				bloblen = printerinfo->bloblen;

				out_uint32_le(s, 24 + driverlen + printerlen + bloblen);	/* length of extra info */
				out_uint32_le(s, printerinfo->default_printer ? 2 : 0);
				out_uint8s(s, 8);	/* unknown */
				out_uint32_le(s, driverlen);
				out_uint32_le(s, printerlen);
				out_uint32_le(s, bloblen);
				rdp_out_unistr(s, printerinfo->driver, driverlen - 2);
				rdp_out_unistr(s, printerinfo->printer, printerlen - 2);
				out_uint8a(s, printerinfo->blob, bloblen);

				if (printerinfo->blob)
					xfree(printerinfo->blob);	/* Blob is sent twice if reconnecting */
				break;
			default:
				out_uint32(s, 0);
		}
	}

	s_mark_end(s);
	channel_send(s, rdpdr_channel);
}

void
rdpdr_send_completion(uint32 device, uint32 id, uint32 status, uint32 result, uint8 * buffer,
		      uint32 length)
{
	STREAM s;

#ifdef WITH_SCARD
	scard_lock(SCARD_LOCK_RDPDR);
#endif
	s = channel_init(rdpdr_channel, 20 + length);
	out_uint16_le(s, RDPDR_CTYP_CORE);
	out_uint16_le(s, PAKID_CORE_DEVICE_IOCOMPLETION);
	out_uint32_le(s, device);
	out_uint32_le(s, id);
	out_uint32_le(s, status);
	out_uint32_le(s, result);
	out_uint8p(s, buffer, length);
	s_mark_end(s);
	/* JIF */
#ifdef WITH_DEBUG_RDP5
	printf("--> rdpdr_send_completion\n");
	/* hexdump(s->channel_hdr + 8, s->end - s->channel_hdr - 8); */
#endif
	channel_send(s, rdpdr_channel);
#ifdef WITH_SCARD
	scard_unlock(SCARD_LOCK_RDPDR);
#endif
}

static void
rdpdr_process_irp(STREAM s)
{
	uint32 result = 0,
		length = 0,
		desired_access = 0,
		request,
		file,
		info_level,
		buffer_len,
		id,
		major,
		minor,
		device,
		offset,
		bytes_in,
		bytes_out,
		error_mode,
		share_mode, disposition, total_timeout, interval_timeout, flags_and_attributes = 0;

	char *filename;
	uint32 filename_len;

	uint8 *buffer, *pst_buf;
	struct stream out;
	DEVICE_FNS *fns;
	RD_BOOL rw_blocking = True;
	RD_NTSTATUS status = RD_STATUS_INVALID_DEVICE_REQUEST;

	in_uint32_le(s, device);
	in_uint32_le(s, file);
	in_uint32_le(s, id);
	in_uint32_le(s, major);
	in_uint32_le(s, minor);

	filename = NULL;

	buffer_len = 0;
	buffer = (uint8 *) xmalloc(1024);
	buffer[0] = 0;

	if (device >= RDPDR_MAX_DEVICES)
	{
		error("invalid irp device 0x%lx file 0x%lx id 0x%lx major 0x%lx minor 0x%lx\n",
		      device, file, id, major, minor);
		xfree(buffer);
		return;
	}

	switch (g_rdpdr_device[device].device_type)
	{
		case DEVICE_TYPE_SERIAL:

			fns = &serial_fns;
			rw_blocking = False;
			break;

		case DEVICE_TYPE_PARALLEL:

			fns = &parallel_fns;
			rw_blocking = False;
			break;

		case DEVICE_TYPE_PRINTER:

			fns = &printer_fns;
			break;

		case DEVICE_TYPE_DISK:

			fns = &disk_fns;
			rw_blocking = False;
			break;

		case DEVICE_TYPE_SCARD:
#ifdef WITH_SCARD
			fns = &scard_fns;
			rw_blocking = False;
			break;
#endif
		default:

			error("IRP for bad device %ld\n", device);
			xfree(buffer);
			return;
	}

	switch (major)
	{
		case IRP_MJ_CREATE:

			in_uint32_be(s, desired_access);
			in_uint8s(s, 0x08);	/* unknown */
			in_uint32_le(s, error_mode);
			in_uint32_le(s, share_mode);
			in_uint32_le(s, disposition);
			in_uint32_le(s, flags_and_attributes);
			in_uint32_le(s, length);

			if (length && (length / 2) < 256)
			{
				rdp_in_unistr(s, length, &filename, &filename_len);
				if (filename)
					convert_to_unix_filename(filename);
			}

			if (!fns->create)
			{
				status = RD_STATUS_NOT_SUPPORTED;
				free(filename);
				break;
			}

			status = fns->create(device, desired_access, share_mode, disposition,
					     flags_and_attributes, filename, &result);

			free(filename);
			buffer_len = 1;
			break;

		case IRP_MJ_CLOSE:
			if (!fns->close)
			{
				status = RD_STATUS_NOT_SUPPORTED;
				break;
			}

			status = fns->close(file);
			break;

		case IRP_MJ_READ:

			if (!fns->read)
			{
				status = RD_STATUS_NOT_SUPPORTED;
				break;
			}

			in_uint32_le(s, length);
			in_uint32_le(s, offset);
#if WITH_DEBUG_RDP5
			DEBUG(("RDPDR IRP Read (length: %d, offset: %d)\n", length, offset));
#endif
			if (!rdpdr_handle_ok(device, file))
			{
				status = RD_STATUS_INVALID_HANDLE;
				break;
			}

			if (rw_blocking)	/* Complete read immediately */
			{
				buffer = (uint8 *) xrealloc((void *) buffer, length);
				if (!buffer)
				{
					status = RD_STATUS_CANCELLED;
					break;
				}
				status = fns->read(file, buffer, length, offset, &result);
				buffer_len = result;
				break;
			}

			/* Add request to table */
			pst_buf = (uint8 *) xmalloc(length);
			if (!pst_buf)
			{
				status = RD_STATUS_CANCELLED;
				break;
			}
			serial_get_timeout(file, length, &total_timeout, &interval_timeout);
			if (add_async_iorequest
			    (device, file, id, major, length, fns, total_timeout, interval_timeout,
			     pst_buf, offset))
			{
				status = RD_STATUS_PENDING;
				break;
			}

			status = RD_STATUS_CANCELLED;
			break;
		case IRP_MJ_WRITE:

			buffer_len = 1;

			if (!fns->write)
			{
				status = RD_STATUS_NOT_SUPPORTED;
				break;
			}

			in_uint32_le(s, length);
			in_uint32_le(s, offset);
			in_uint8s(s, 0x18);
#if WITH_DEBUG_RDP5
			DEBUG(("RDPDR IRP Write (length: %d)\n", result));
#endif
			if (!rdpdr_handle_ok(device, file))
			{
				status = RD_STATUS_INVALID_HANDLE;
				break;
			}

			if (rw_blocking)	/* Complete immediately */
			{
				status = fns->write(file, s->p, length, offset, &result);
				break;
			}

			/* Add to table */
			pst_buf = (uint8 *) xmalloc(length);
			if (!pst_buf)
			{
				status = RD_STATUS_CANCELLED;
				break;
			}

			in_uint8a(s, pst_buf, length);

			if (add_async_iorequest
			    (device, file, id, major, length, fns, 0, 0, pst_buf, offset))
			{
				status = RD_STATUS_PENDING;
				break;
			}

			status = RD_STATUS_CANCELLED;
			break;

		case IRP_MJ_QUERY_INFORMATION:

			if (g_rdpdr_device[device].device_type != DEVICE_TYPE_DISK)
			{
				status = RD_STATUS_INVALID_HANDLE;
				break;
			}
			in_uint32_le(s, info_level);

			out.data = out.p = buffer;
			out.size = sizeof(buffer);
			status = disk_query_information(file, info_level, &out);
			result = buffer_len = out.p - out.data;

			break;

		case IRP_MJ_SET_INFORMATION:

			if (g_rdpdr_device[device].device_type != DEVICE_TYPE_DISK)
			{
				status = RD_STATUS_INVALID_HANDLE;
				break;
			}

			in_uint32_le(s, info_level);

			out.data = out.p = buffer;
			out.size = sizeof(buffer);
			status = disk_set_information(file, info_level, s, &out);
			result = buffer_len = out.p - out.data;
			break;

		case IRP_MJ_QUERY_VOLUME_INFORMATION:

			if (g_rdpdr_device[device].device_type != DEVICE_TYPE_DISK)
			{
				status = RD_STATUS_INVALID_HANDLE;
				break;
			}

			in_uint32_le(s, info_level);

			out.data = out.p = buffer;
			out.size = sizeof(buffer);
			status = disk_query_volume_information(file, info_level, &out);
			result = buffer_len = out.p - out.data;
			break;

		case IRP_MJ_DIRECTORY_CONTROL:

			if (g_rdpdr_device[device].device_type != DEVICE_TYPE_DISK)
			{
				status = RD_STATUS_INVALID_HANDLE;
				break;
			}

			switch (minor)
			{
				case IRP_MN_QUERY_DIRECTORY:

					in_uint32_le(s, info_level);
					in_uint8s(s, 1);
					in_uint32_le(s, length);
					in_uint8s(s, 0x17);
					if (length && length < 2 * 255)
					{
						rdp_in_unistr(s, length, &filename, &filename_len);
						if (filename)
							convert_to_unix_filename(filename);
					}

					out.data = out.p = buffer;
					out.size = sizeof(buffer);
					status = disk_query_directory(file, info_level, filename,
								      &out);
					result = buffer_len = out.p - out.data;
					if (!buffer_len)
						buffer_len++;

					free(filename);
					break;

				case IRP_MN_NOTIFY_CHANGE_DIRECTORY:

					/* JIF
					   unimpl("IRP major=0x%x minor=0x%x: IRP_MN_NOTIFY_CHANGE_DIRECTORY\n", major, minor);  */

					in_uint32_le(s, info_level);	/* notify mask */

					status = disk_create_notify(file, info_level);
					result = 0;

					if (status == RD_STATUS_PENDING)
						add_async_iorequest(device, file, id, major, length,
								    fns, 0, 0, NULL, 0);
					break;

				default:

					status = RD_STATUS_INVALID_PARAMETER;
					/* JIF */
					unimpl("IRP major=0x%x minor=0x%x\n", major, minor);
			}
			break;

		case IRP_MJ_DEVICE_CONTROL:

			if (!fns->device_control)
			{
				status = RD_STATUS_NOT_SUPPORTED;
				break;
			}

			in_uint32_le(s, bytes_out);
			in_uint32_le(s, bytes_in);
			in_uint32_le(s, request);
			in_uint8s(s, 0x14);

			buffer = (uint8 *) xrealloc((void *) buffer, bytes_out + 0x14);
			if (!buffer)
			{
				status = RD_STATUS_CANCELLED;
				break;
			}

			out.data = out.p = buffer;
			out.size = sizeof(buffer);

#ifdef WITH_SCARD
			scardSetInfo(g_epoch, device, id, bytes_out + 0x14);
#endif
			status = fns->device_control(file, request, s, &out);
			result = buffer_len = out.p - out.data;

			/* Serial SERIAL_WAIT_ON_MASK */
			if (status == RD_STATUS_PENDING)
			{
				if (add_async_iorequest
				    (device, file, id, major, length, fns, 0, 0, NULL, 0))
				{
					status = RD_STATUS_PENDING;
					break;
				}
			}
#ifdef WITH_SCARD
			else if (status == (RD_STATUS_PENDING | 0xC0000000))
				status = RD_STATUS_PENDING;
#endif
			break;


		case IRP_MJ_LOCK_CONTROL:

			if (g_rdpdr_device[device].device_type != DEVICE_TYPE_DISK)
			{
				status = RD_STATUS_INVALID_HANDLE;
				break;
			}

			in_uint32_le(s, info_level);

			out.data = out.p = buffer;
			out.size = sizeof(buffer);
			/* FIXME: Perhaps consider actually *do*
			   something here :-) */
			status = RD_STATUS_SUCCESS;
			result = buffer_len = out.p - out.data;
			break;

		default:
			unimpl("IRP major=0x%x minor=0x%x\n", major, minor);
			break;
	}

	if (status != RD_STATUS_PENDING)
	{
		rdpdr_send_completion(device, id, status, result, buffer, buffer_len);
	}
	if (buffer)
		xfree(buffer);
	buffer = NULL;
}

static void
rdpdr_send_client_capability_response(void)
{
	/* DR_CORE_CAPABILITY_RSP */
	STREAM s;
	s = channel_init(rdpdr_channel, 0x50);
	out_uint16_le(s, RDPDR_CTYP_CORE);
	out_uint16_le(s, PAKID_CORE_CLIENT_CAPABILITY);
	out_uint32_le(s, 5);	/* count */
	out_uint16_le(s, 1);	/* first */
	out_uint16_le(s, 0x28);	/* length */
	out_uint32_le(s, 1);
	out_uint32_le(s, 2);
	out_uint16_le(s, 2);
	out_uint16_le(s, 5);
	out_uint16_le(s, 1);
	out_uint16_le(s, 5);
	out_uint16_le(s, 0xFFFF);
	out_uint16_le(s, 0);
	out_uint32_le(s, 0);
	out_uint32_le(s, 3);
	out_uint32_le(s, 0);
	out_uint32_le(s, 0);
	out_uint16_le(s, 2);	/* second */
	out_uint16_le(s, 8);	/* length */
	out_uint32_le(s, 1);
	out_uint16_le(s, 3);	/* third */
	out_uint16_le(s, 8);	/* length */
	out_uint32_le(s, 1);
	out_uint16_le(s, 4);	/* fourth */
	out_uint16_le(s, 8);	/* length */
	out_uint32_le(s, 1);
	out_uint16_le(s, 5);	/* fifth */
	out_uint16_le(s, 8);	/* length */
	out_uint32_le(s, 1);

	s_mark_end(s);
	channel_send(s, rdpdr_channel);
}

static void
rdpdr_process(STREAM s)
{
	uint32 handle;
	uint16 vmin;
	uint16 component;
	uint16 pakid;

#if WITH_DEBUG_RDP5
	printf("--- rdpdr_process ---\n");
	hexdump(s->p, s->end - s->p);
#endif

	in_uint16(s, component);
	in_uint16(s, pakid);

	if (component == RDPDR_CTYP_CORE)
	{
		switch (pakid)
		{
			case PAKID_CORE_DEVICE_IOREQUEST:
				rdpdr_process_irp(s);
				break;

			case PAKID_CORE_SERVER_ANNOUNCE:
				/* DR_CORE_SERVER_ANNOUNCE_REQ */
				in_uint8s(s, 2);	/* skip versionMajor */
				in_uint16_le(s, vmin);	/* VersionMinor */
				in_uint32_le(s, g_client_id);	/* ClientID */

				/* The RDP client is responsibility to provide a random client id
				   if server version is < 12 */
				if (vmin < 0x000c)
					g_client_id = 0x815ed39d;	/* IP address (use 127.0.0.1) 0x815ed39d */
				g_epoch++;

				rdpdr_send_client_announce_reply();
				rdpdr_send_client_name_request();
				break;

			case PAKID_CORE_CLIENTID_CONFIRM:
				rdpdr_send_client_device_list_announce();
				break;

			case PAKID_CORE_DEVICE_REPLY:
				in_uint32(s, handle);
#if WITH_DEBUG_RDP5
				DEBUG(("RDPDR: Server connected to resource %d\n", handle));
#endif
				break;

			case PAKID_CORE_SERVER_CAPABILITY:
				rdpdr_send_client_capability_response();
				break;

			default:
				unimpl("RDPDR pakid 0x%x of component 0x%x\n", pakid, component);
				break;

		}
	}
	else if (component == RDPDR_CTYP_PRN)
	{
		if (pakid == PAKID_PRN_CACHE_DATA)
			printercache_process(s);
	}
	else
		unimpl("RDPDR component 0x%x\n", component);
}

RD_BOOL
rdpdr_init()
{
	rdpdr_channel =
		channel_register("rdpdr",
				 CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_COMPRESS_RDP,
				 rdpdr_process);

	return (rdpdr_channel != NULL);
}

/* Add file descriptors of pending io request to select() */
void
rdpdr_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv, RD_BOOL * timeout)
{
	uint32 select_timeout = 0;	/* Timeout value to be used for select() (in millisecons). */
	struct async_iorequest *iorq;
	char c;

	iorq = g_iorequest;
	while (iorq != NULL)
	{
		if (iorq->fd != 0)
		{
			switch (iorq->major)
			{
				case IRP_MJ_READ:
					/* Is this FD valid? FDs will
					   be invalid when
					   reconnecting. FIXME: Real
					   support for reconnects. */

					FD_SET(iorq->fd, rfds);
					*n = MAX(*n, iorq->fd);

					/* Check if io request timeout is smaller than current (but not 0). */
					if (iorq->timeout
					    && (select_timeout == 0
						|| iorq->timeout < select_timeout))
					{
						/* Set new timeout */
						select_timeout = iorq->timeout;
						g_min_timeout_fd = iorq->fd;	/* Remember fd */
						tv->tv_sec = select_timeout / 1000;
						tv->tv_usec = (select_timeout % 1000) * 1000;
						*timeout = True;
						break;
					}
					if (iorq->itv_timeout && iorq->partial_len > 0
					    && (select_timeout == 0
						|| iorq->itv_timeout < select_timeout))
					{
						/* Set new timeout */
						select_timeout = iorq->itv_timeout;
						g_min_timeout_fd = iorq->fd;	/* Remember fd */
						tv->tv_sec = select_timeout / 1000;
						tv->tv_usec = (select_timeout % 1000) * 1000;
						*timeout = True;
						break;
					}
					break;

				case IRP_MJ_WRITE:
					/* FD still valid? See above. */
					if ((write(iorq->fd, &c, 0) != 0) && (errno == EBADF))
						break;

					FD_SET(iorq->fd, wfds);
					*n = MAX(*n, iorq->fd);
					break;

				case IRP_MJ_DEVICE_CONTROL:
					if (select_timeout > 5)
						select_timeout = 5;	/* serial event queue */
					break;

			}

		}

		iorq = iorq->next;
	}
}

struct async_iorequest *
rdpdr_remove_iorequest(struct async_iorequest *prev, struct async_iorequest *iorq)
{
	if (!iorq)
		return NULL;

	if (iorq->buffer)
		xfree(iorq->buffer);
	if (prev)
	{
		prev->next = iorq->next;
		xfree(iorq);
		iorq = prev->next;
	}
	else
	{
		/* Even if NULL */
		g_iorequest = iorq->next;
		xfree(iorq);
		iorq = NULL;
	}
	return iorq;
}

/* Check if select() returned with one of the rdpdr file descriptors, and complete io if it did */
static void
_rdpdr_check_fds(fd_set * rfds, fd_set * wfds, RD_BOOL timed_out)
{
	RD_NTSTATUS status;
	uint32 result = 0;
	DEVICE_FNS *fns;
	struct async_iorequest *iorq;
	struct async_iorequest *prev;
	uint32 req_size = 0;
	uint32 buffer_len;
	struct stream out;
	uint8 *buffer = NULL;


	if (timed_out)
	{
		/* check serial iv_timeout */

		iorq = g_iorequest;
		prev = NULL;
		while (iorq != NULL)
		{
			if (iorq->fd == g_min_timeout_fd)
			{
				if ((iorq->partial_len > 0) &&
				    (g_rdpdr_device[iorq->device].device_type ==
				     DEVICE_TYPE_SERIAL))
				{

					/* iv_timeout between 2 chars, send partial_len */
					/*printf("RDPDR: IVT total %u bytes read of %u\n", iorq->partial_len, iorq->length); */
					rdpdr_send_completion(iorq->device,
							      iorq->id, RD_STATUS_SUCCESS,
							      iorq->partial_len,
							      iorq->buffer, iorq->partial_len);
					iorq = rdpdr_remove_iorequest(prev, iorq);
					return;
				}
				else
				{
					break;
				}

			}
			else
			{
				break;
			}


			prev = iorq;
			if (iorq)
				iorq = iorq->next;

		}

		rdpdr_abort_io(g_min_timeout_fd, 0, RD_STATUS_TIMEOUT);
		return;
	}

	iorq = g_iorequest;
	prev = NULL;
	while (iorq != NULL)
	{
		if (iorq->fd != 0)
		{
			switch (iorq->major)
			{
				case IRP_MJ_READ:
					if (FD_ISSET(iorq->fd, rfds))
					{
						/* Read the data */
						fns = iorq->fns;

						req_size =
							(iorq->length - iorq->partial_len) >
							8192 ? 8192 : (iorq->length -
								       iorq->partial_len);
						/* never read larger chunks than 8k - chances are that it will block */
						status = fns->read(iorq->fd,
								   iorq->buffer + iorq->partial_len,
								   req_size, iorq->offset, &result);

						if ((long) result > 0)
						{
							iorq->partial_len += result;
							iorq->offset += result;
						}
#if WITH_DEBUG_RDP5
						DEBUG(("RDPDR: %d bytes of data read\n", result));
#endif
						/* only delete link if all data has been transfered */
						/* or if result was 0 and status success - EOF      */
						if ((iorq->partial_len == iorq->length) ||
						    (result == 0))
						{
#if WITH_DEBUG_RDP5
							DEBUG(("RDPDR: AIO total %u bytes read of %u\n", iorq->partial_len, iorq->length));
#endif
							rdpdr_send_completion(iorq->device,
									      iorq->id, status,
									      iorq->partial_len,
									      iorq->buffer,
									      iorq->partial_len);
							iorq = rdpdr_remove_iorequest(prev, iorq);
						}
					}
					break;
				case IRP_MJ_WRITE:
					if (FD_ISSET(iorq->fd, wfds))
					{
						/* Write data. */
						fns = iorq->fns;

						req_size =
							(iorq->length - iorq->partial_len) >
							8192 ? 8192 : (iorq->length -
								       iorq->partial_len);

						/* never write larger chunks than 8k - chances are that it will block */
						status = fns->write(iorq->fd,
								    iorq->buffer +
								    iorq->partial_len, req_size,
								    iorq->offset, &result);

						if ((long) result > 0)
						{
							iorq->partial_len += result;
							iorq->offset += result;
						}

#if WITH_DEBUG_RDP5
						DEBUG(("RDPDR: %d bytes of data written\n",
						       result));
#endif
						/* only delete link if all data has been transfered */
						/* or we couldn't write */
						if ((iorq->partial_len == iorq->length)
						    || (result == 0))
						{
#if WITH_DEBUG_RDP5
							DEBUG(("RDPDR: AIO total %u bytes written of %u\n", iorq->partial_len, iorq->length));
#endif
							rdpdr_send_completion(iorq->device,
									      iorq->id, status,
									      iorq->partial_len,
									      (uint8 *) "", 1);

							iorq = rdpdr_remove_iorequest(prev, iorq);
						}
					}
					break;
				case IRP_MJ_DEVICE_CONTROL:
					if (serial_get_event(iorq->fd, &result))
					{
						buffer = (uint8 *) xrealloc((void *) buffer, 0x14);
						out.data = out.p = buffer;
						out.size = sizeof(buffer);
						out_uint32_le(&out, result);
						result = buffer_len = out.p - out.data;
						status = RD_STATUS_SUCCESS;
						rdpdr_send_completion(iorq->device, iorq->id,
								      status, result, buffer,
								      buffer_len);
						xfree(buffer);
						iorq = rdpdr_remove_iorequest(prev, iorq);
					}

					break;
			}

		}
		prev = iorq;
		if (iorq)
			iorq = iorq->next;
	}

	/* Check notify */
	iorq = g_iorequest;
	prev = NULL;
	while (iorq != NULL)
	{
		if (iorq->fd != 0)
		{
			switch (iorq->major)
			{

				case IRP_MJ_DIRECTORY_CONTROL:
					if (g_rdpdr_device[iorq->device].device_type ==
					    DEVICE_TYPE_DISK)
					{

						if (g_notify_stamp)
						{
							g_notify_stamp = False;
							status = disk_check_notify(iorq->fd);
							if (status != RD_STATUS_PENDING)
							{
								rdpdr_send_completion(iorq->device,
										      iorq->id,
										      status, 0,
										      NULL, 0);
								iorq = rdpdr_remove_iorequest(prev,
											      iorq);
							}
						}
					}
					break;



			}
		}

		prev = iorq;
		if (iorq)
			iorq = iorq->next;
	}

}

void
rdpdr_check_fds(fd_set * rfds, fd_set * wfds, RD_BOOL timed_out)
{
	fd_set dummy;


	FD_ZERO(&dummy);


	/* fist check event queue only,
	   any serial wait event must be done before read block will be sent
	 */

	_rdpdr_check_fds(&dummy, &dummy, False);
	_rdpdr_check_fds(rfds, wfds, timed_out);
}


/* Abort a pending io request for a given handle and major */
RD_BOOL
rdpdr_abort_io(uint32 fd, uint32 major, RD_NTSTATUS status)
{
	uint32 result;
	struct async_iorequest *iorq;
	struct async_iorequest *prev;

	iorq = g_iorequest;
	prev = NULL;
	while (iorq != NULL)
	{
		/* Only remove from table when major is not set, or when correct major is supplied.
		   Abort read should not abort a write io request. */
		if ((iorq->fd == fd) && (major == 0 || iorq->major == major))
		{
			result = 0;
			rdpdr_send_completion(iorq->device, iorq->id, status, result, (uint8 *) "",
					      1);

			iorq = rdpdr_remove_iorequest(prev, iorq);
			return True;
		}

		prev = iorq;
		iorq = iorq->next;
	}

	return False;
}
