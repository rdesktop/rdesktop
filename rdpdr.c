#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>		/* opendir, closedir, readdir */
#include <time.h>
#include "rdesktop.h"

#define IRP_MJ_CREATE		0x00
#define IRP_MJ_CLOSE		0x02
#define IRP_MJ_READ		0x03
#define IRP_MJ_WRITE		0x04
#define IRP_MJ_DEVICE_CONTROL	0x0e

#define IRP_MJ_CREATE			0x00
#define IRP_MJ_CLOSE			0x02
#define IRP_MJ_READ			0x03
#define IRP_MJ_WRITE			0x04
#define	IRP_MJ_QUERY_INFORMATION	0x05
#define IRP_MJ_SET_INFORMATION		0x06
#define IRP_MJ_QUERY_VOLUME_INFORMATION	0x0a
#define IRP_MJ_DIRECTORY_CONTROL	0x0c
#define IRP_MJ_DEVICE_CONTROL		0x0e

#define IRP_MN_QUERY_DIRECTORY          0x01
#define IRP_MN_NOTIFY_CHANGE_DIRECTORY  0x02

extern char hostname[16];
extern DEVICE_FNS serial_fns;
extern DEVICE_FNS printer_fns;
extern DEVICE_FNS parallel_fns;
extern DEVICE_FNS disk_fns;
extern FILEINFO g_fileinfo[];

static VCHANNEL *rdpdr_channel;

/* If select() times out, the request for the device with handle g_min_timeout_fd is aborted */
HANDLE g_min_timeout_fd;
uint32 g_num_devices;

/* Table with information about rdpdr devices */
RDPDR_DEVICE g_rdpdr_device[RDPDR_MAX_DEVICES];

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
get_device_index(HANDLE handle)
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

BOOL
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
BOOL
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
		// create new element if needed
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

void
rdpdr_send_connect(void)
{
	uint8 magic[4] = "rDCC";
	STREAM s;

	s = channel_init(rdpdr_channel, 12);
	out_uint8a(s, magic, 4);
	out_uint16_le(s, 1);	/* unknown */
	out_uint16_le(s, 5);
	out_uint32_be(s, 0x815ed39d);	/* IP address (use 127.0.0.1) 0x815ed39d */
	s_mark_end(s);
	channel_send(s, rdpdr_channel);
}


void
rdpdr_send_name(void)
{
	uint8 magic[4] = "rDNC";
	uint32 hostlen = (strlen(hostname) + 1) * 2;
	STREAM s;

	s = channel_init(rdpdr_channel, 16 + hostlen);
	out_uint8a(s, magic, 4);
	out_uint16_le(s, 0x63);	/* unknown */
	out_uint16_le(s, 0x72);
	out_uint32(s, 0);
	out_uint32_le(s, hostlen);
	rdp_out_unistr(s, hostname, hostlen - 2);
	s_mark_end(s);
	channel_send(s, rdpdr_channel);
}

/* Returns the size of the payload of the announce packet */
int
announcedata_size()
{
	int size, i;
	PRINTER *printerinfo;

	size = 8;		//static announce size
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

void
rdpdr_send_available(void)
{

	uint8 magic[4] = "rDAD";
	uint32 driverlen, printerlen, bloblen;
	int i;
	STREAM s;
	PRINTER *printerinfo;

	s = channel_init(rdpdr_channel, announcedata_size());
	out_uint8a(s, magic, 4);
	out_uint32_le(s, g_num_devices);

	for (i = 0; i < g_num_devices; i++)
	{
		out_uint32_le(s, g_rdpdr_device[i].device_type);
		out_uint32_le(s, i);	/* RDP Device ID */
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
#if 0
	out_uint32_le(s, 0x20);	/* Device type 0x20 - smart card */
	out_uint32_le(s, 0);
	out_uint8p(s, "SCARD", 5);
	out_uint8s(s, 3);
	out_uint32(s, 0);
#endif

	s_mark_end(s);
	channel_send(s, rdpdr_channel);
}

void
rdpdr_send_completion(uint32 device, uint32 id, uint32 status, uint32 result, uint8 * buffer,
		      uint32 length)
{
	uint8 magic[4] = "rDCI";
	STREAM s;

	s = channel_init(rdpdr_channel, 20 + length);
	out_uint8a(s, magic, 4);
	out_uint32_le(s, device);
	out_uint32_le(s, id);
	out_uint32_le(s, status);
	out_uint32_le(s, result);
	out_uint8p(s, buffer, length);
	s_mark_end(s);
	/* JIF
	   hexdump(s->channel_hdr + 8, s->end - s->channel_hdr - 8); */
	channel_send(s, rdpdr_channel);
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

	char filename[256];
	uint8 *buffer, *pst_buf;
	struct stream out;
	DEVICE_FNS *fns;
	BOOL rw_blocking = True;
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	in_uint32_le(s, device);
	in_uint32_le(s, file);
	in_uint32_le(s, id);
	in_uint32_le(s, major);
	in_uint32_le(s, minor);

	buffer_len = 0;
	buffer = (uint8 *) xmalloc(1024);
	buffer[0] = 0;

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
		default:

			error("IRP for bad device %ld\n", device);
			return;
	}

	switch (major)
	{
		case IRP_MJ_CREATE:

			in_uint32_be(s, desired_access);
			in_uint8s(s, 0x08);	// unknown
			in_uint32_le(s, error_mode);
			in_uint32_le(s, share_mode);
			in_uint32_le(s, disposition);
			in_uint32_le(s, flags_and_attributes);
			in_uint32_le(s, length);

			if (length && (length / 2) < 256)
			{
				rdp_in_unistr(s, filename, length);
				convert_to_unix_filename(filename);
			}
			else
			{
				filename[0] = 0;
			}

			if (!fns->create)
			{
				status = STATUS_NOT_SUPPORTED;
				break;
			}

			status = fns->create(device, desired_access, share_mode, disposition,
					     flags_and_attributes, filename, &result);
			buffer_len = 1;
			break;

		case IRP_MJ_CLOSE:
			if (!fns->close)
			{
				status = STATUS_NOT_SUPPORTED;
				break;
			}

			status = fns->close(file);
			break;

		case IRP_MJ_READ:

			if (!fns->read)
			{
				status = STATUS_NOT_SUPPORTED;
				break;
			}

			in_uint32_le(s, length);
			in_uint32_le(s, offset);
#if WITH_DEBUG_RDP5
			DEBUG(("RDPDR IRP Read (length: %d, offset: %d)\n", length, offset));
#endif
			if (!rdpdr_handle_ok(device, file))
			{
				status = STATUS_INVALID_HANDLE;
				break;
			}

			if (rw_blocking)	// Complete read immediately
			{
				buffer = (uint8 *) xrealloc((void *) buffer, length);
				if (!buffer)
				{
					status = STATUS_CANCELLED;
					break;
				}
				status = fns->read(file, buffer, length, offset, &result);
				buffer_len = result;
				break;
			}

			// Add request to table
			pst_buf = (uint8 *) xmalloc(length);
			if (!pst_buf)
			{
				status = STATUS_CANCELLED;
				break;
			}
			serial_get_timeout(file, length, &total_timeout, &interval_timeout);
			if (add_async_iorequest
			    (device, file, id, major, length, fns, total_timeout, interval_timeout,
			     pst_buf, offset))
			{
				status = STATUS_PENDING;
				break;
			}

			status = STATUS_CANCELLED;
			break;
		case IRP_MJ_WRITE:

			buffer_len = 1;

			if (!fns->write)
			{
				status = STATUS_NOT_SUPPORTED;
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
				status = STATUS_INVALID_HANDLE;
				break;
			}

			if (rw_blocking)	// Complete immediately
			{
				status = fns->write(file, s->p, length, offset, &result);
				break;
			}

			// Add to table
			pst_buf = (uint8 *) xmalloc(length);
			if (!pst_buf)
			{
				status = STATUS_CANCELLED;
				break;
			}

			in_uint8a(s, pst_buf, length);

			if (add_async_iorequest
			    (device, file, id, major, length, fns, 0, 0, pst_buf, offset))
			{
				status = STATUS_PENDING;
				break;
			}

			status = STATUS_CANCELLED;
			break;

		case IRP_MJ_QUERY_INFORMATION:

			if (g_rdpdr_device[device].device_type != DEVICE_TYPE_DISK)
			{
				status = STATUS_INVALID_HANDLE;
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
				status = STATUS_INVALID_HANDLE;
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
				status = STATUS_INVALID_HANDLE;
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
				status = STATUS_INVALID_HANDLE;
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
						rdp_in_unistr(s, filename, length);
						convert_to_unix_filename(filename);
					}
					else
					{
						filename[0] = 0;
					}
					out.data = out.p = buffer;
					out.size = sizeof(buffer);
					status = disk_query_directory(file, info_level, filename,
								      &out);
					result = buffer_len = out.p - out.data;
					if (!buffer_len)
						buffer_len++;
					break;

				case IRP_MN_NOTIFY_CHANGE_DIRECTORY:

					/* JIF
					   unimpl("IRP major=0x%x minor=0x%x: IRP_MN_NOTIFY_CHANGE_DIRECTORY\n", major, minor); */
					status = STATUS_PENDING;	// Don't send completion packet
					break;

				default:

					status = STATUS_INVALID_PARAMETER;
					/* JIF
					   unimpl("IRP major=0x%x minor=0x%x\n", major, minor); */
			}
			break;

		case IRP_MJ_DEVICE_CONTROL:

			if (!fns->device_control)
			{
				status = STATUS_NOT_SUPPORTED;
				break;
			}

			in_uint32_le(s, bytes_out);
			in_uint32_le(s, bytes_in);
			in_uint32_le(s, request);
			in_uint8s(s, 0x14);

			buffer = (uint8 *) xrealloc((void *) buffer, bytes_out + 0x14);
			if (!buffer)
			{
				status = STATUS_CANCELLED;
				break;
			}

			out.data = out.p = buffer;
			out.size = sizeof(buffer);
			status = fns->device_control(file, request, s, &out);
			result = buffer_len = out.p - out.data;
			break;

		default:
			unimpl("IRP major=0x%x minor=0x%x\n", major, minor);
			break;
	}

	if (status != STATUS_PENDING)
	{
		rdpdr_send_completion(device, id, status, result, buffer, buffer_len);
	}
	if (buffer)
		xfree(buffer);
	buffer = NULL;
}

void
rdpdr_send_clientcapabilty(void)
{
	uint8 magic[4] = "rDPC";
	STREAM s;

	s = channel_init(rdpdr_channel, 0x50);
	out_uint8a(s, magic, 4);
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
	uint8 *magic;

#if WITH_DEBUG_RDP5
	printf("--- rdpdr_process ---\n");
	hexdump(s->p, s->end - s->p);
#endif
	in_uint8p(s, magic, 4);

	if ((magic[0] == 'r') && (magic[1] == 'D'))
	{
		if ((magic[2] == 'R') && (magic[3] == 'I'))
		{
			rdpdr_process_irp(s);
			return;
		}
		if ((magic[2] == 'n') && (magic[3] == 'I'))
		{
			rdpdr_send_connect();
			rdpdr_send_name();
			return;
		}
		if ((magic[2] == 'C') && (magic[3] == 'C'))
		{
			/* connect from server */
			rdpdr_send_clientcapabilty();
			rdpdr_send_available();
			return;
		}
		if ((magic[2] == 'r') && (magic[3] == 'd'))
		{
			/* connect to a specific resource */
			in_uint32(s, handle);
#if WITH_DEBUG_RDP5
			DEBUG(("RDPDR: Server connected to resource %d\n", handle));
#endif
			return;
		}
		if ((magic[2] == 'P') && (magic[3] == 'S'))
		{
			/* server capability */
			return;
		}
	}
	if ((magic[0] == 'R') && (magic[1] == 'P'))
	{
		if ((magic[2] == 'C') && (magic[3] == 'P'))
		{
			printercache_process(s);
			return;
		}
	}
	unimpl("RDPDR packet type %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);
}

BOOL
rdpdr_init()
{
	if (g_num_devices > 0)
	{
		rdpdr_channel =
			channel_register("rdpdr",
					 CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_COMPRESS_RDP,
					 rdpdr_process);
	}

	return (rdpdr_channel != NULL);
}

/* Add file descriptors of pending io request to select() */
void
rdpdr_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv, BOOL * timeout)
{
	uint32 select_timeout = 0;	// Timeout value to be used for select() (in millisecons).
	struct async_iorequest *iorq;

	iorq = g_iorequest;
	while (iorq != NULL)
	{
		if (iorq->fd != 0)
		{
			switch (iorq->major)
			{
				case IRP_MJ_READ:

					FD_SET(iorq->fd, rfds);

					// Check if io request timeout is smaller than current (but not 0).
					if (iorq->timeout
					    && (select_timeout == 0
						|| iorq->timeout < select_timeout))
					{
						// Set new timeout
						select_timeout = iorq->timeout;
						g_min_timeout_fd = iorq->fd;	/* Remember fd */
						tv->tv_sec = select_timeout / 1000;
						tv->tv_usec = (select_timeout % 1000) * 1000;
						*timeout = True;
					}
					break;

				case IRP_MJ_WRITE:
					FD_SET(iorq->fd, wfds);
					break;

			}
			*n = MAX(*n, iorq->fd);
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
		// Even if NULL
		g_iorequest = iorq->next;
		xfree(iorq);
		iorq = NULL;
	}
	return iorq;
}

/* Check if select() returned with one of the rdpdr file descriptors, and complete io if it did */
void
rdpdr_check_fds(fd_set * rfds, fd_set * wfds, BOOL timed_out)
{
	NTSTATUS status;
	uint32 result = 0;
	DEVICE_FNS *fns;
	struct async_iorequest *iorq;
	struct async_iorequest *prev;
	uint32 req_size = 0;

	if (timed_out)
	{
		rdpdr_abort_io(g_min_timeout_fd, 0, STATUS_TIMEOUT);
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

						if (result > 0)
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

						if (result > 0)
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
			}

		}
		prev = iorq;
		if (iorq)
			iorq = iorq->next;
	}

}

/* Abort a pending io request for a given handle and major */
BOOL
rdpdr_abort_io(uint32 fd, uint32 major, NTSTATUS status)
{
	uint32 result;
	struct async_iorequest *iorq;
	struct async_iorequest *prev;

	iorq = g_iorequest;
	prev = NULL;
	while (iorq != NULL)
	{
		// Only remove from table when major is not set, or when correct major is supplied.
		// Abort read should not abort a write io request.
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
