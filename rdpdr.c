#include "rdesktop.h"

#define IRP_MJ_CREATE		0x00
#define IRP_MJ_CLOSE		0x02
#define IRP_MJ_READ		0x03
#define IRP_MJ_WRITE		0x04
#define IRP_MJ_DEVICE_CONTROL	0x0e

extern char hostname[16];
extern DEVICE_FNS serial_fns;
extern DEVICE_FNS printer_fns;

static VCHANNEL *rdpdr_channel;

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

void
rdpdr_send_available(void)
{
	uint8 magic[4] = "rDAD";
	char *driver = "Digital turbo PrintServer 20";	/* Fairly generic PostScript driver */
	char *printer = "PostScript";
	uint32 driverlen = (strlen(driver) + 1) * 2;
	uint32 printerlen = (strlen(printer) + 1) * 2;
	STREAM s;

	s = channel_init(rdpdr_channel, 8 + 20);
	out_uint8a(s, magic, 4);
	out_uint32_le(s, 1);	/* Number of devices */

#if 1
	out_uint32_le(s, 0x1);	/* Device type 0x1 - serial */
	out_uint32_le(s, 0);	/* Handle */
	out_uint8p(s, "COM4", 4);
	out_uint8s(s, 4);	/* Pad to 8 */
	out_uint32(s, 0);
#endif
#if 0
	out_uint32_le(s, 0x2);	/* Device type 0x2 - parallel */
	out_uint32_le(s, 0);
	out_uint8p(s, "LPT2", 4);
	out_uint8s(s, 4);
	out_uint32(s, 0);
#endif
#if 1
	out_uint32_le(s, 0x4);	/* Device type 0x4 - printer */
	out_uint32_le(s, 1);
	out_uint8p(s, "PRN1", 4);
	out_uint8s(s, 4);
	out_uint32_le(s, 24 + driverlen + printerlen);	/* length of extra info */
	out_uint32_le(s, 2);	/* unknown */
	out_uint8s(s, 8);	/* unknown */
	out_uint32_le(s, driverlen);	/* length of driver name */
	out_uint32_le(s, printerlen);	/* length of printer name */
	out_uint32(s, 0);	/* unknown */
	rdp_out_unistr(s, driver, driverlen - 2);
	rdp_out_unistr(s, printer, printerlen - 2);
#endif
#if 0
	out_uint32_le(s, 0x8);	/* Device type 0x8 - disk */
	out_uint32_le(s, 0);
	out_uint8p(s, "Z:", 2);
	out_uint8s(s, 6);
	out_uint32(s, 0);
#endif
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
	hexdump(s->channel_hdr + 8, s->end - s->channel_hdr - 8);
	channel_send(s, rdpdr_channel);
}

static void
rdpdr_process_irp(STREAM s)
{
	uint32 device, file, id, major, minor;
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
	uint32 result = 0, length, request, bytes_in, bytes_out;
	uint8 buffer[256];
	uint32 buffer_len = 1;
	struct stream out;
	DEVICE_FNS *fns;

	in_uint32_le(s, device);
	in_uint32_le(s, file);
	in_uint32_le(s, id);
	in_uint32_le(s, major);
	in_uint32_le(s, minor);

	memset(buffer, 0, sizeof(buffer));

	/* FIXME: this should probably be a more dynamic mapping */
	switch (device)
	{
		case 0:
			fns = &serial_fns;
		case 1:
			fns = &printer_fns;
		default:
			error("IRP for bad device %ld\n", device);
			return;
	}

	switch (major)
	{
		case IRP_MJ_CREATE:
			if (fns->create)
				status = fns->create(&result);
			break;

		case IRP_MJ_CLOSE:
			if (fns->close)
				status = fns->close(file);
			break;

		case IRP_MJ_READ:
			if (fns->read)
			{
				if (length > sizeof(buffer))
					length = sizeof(buffer);
				status = fns->read(file, buffer, length, &result);
				buffer_len = result;
			}
			break;

		case IRP_MJ_WRITE:
			if (fns->write)
				status = fns->write(file, s->p, length, &result);
			break;

		case IRP_MJ_DEVICE_CONTROL:
			if (fns->device_control)
			{
				in_uint32_le(s, bytes_out);
				in_uint32_le(s, bytes_in);
				in_uint32_le(s, request);
				in_uint8s(s, 0x14);
				out.data = out.p = buffer;
				out.size = sizeof(buffer);
				status = fns->device_control(file, request, s, &out);
				result = buffer_len = out.p - out.data;
			}
			break;

		default:
			unimpl("IRP major=0x%x minor=0x%x\n", major, minor);
			break;
	}

	rdpdr_send_completion(device, id, status, result, buffer, buffer_len);
}

static void
rdpdr_process(STREAM s)
{
	uint32 handle;
	uint8 *magic;

	printf("rdpdr_process\n");
	hexdump(s->p, s->end - s->p);
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
			rdpdr_send_available();
			return;
		}
		else if ((magic[2] == 'C') && (magic[3] == 'C'))
		{
			/* connect from server */
			return;
		}
		else if ((magic[2] == 'r') && (magic[3] == 'd'))
		{
			/* connect to a specific resource */
			in_uint32(s, handle);
			printf("Server connected to resource %d\n", handle);
			return;
		}
	}
	unimpl("RDPDR packet type %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);
}

BOOL
rdpdr_init(void)
{
	rdpdr_channel =
		channel_register("rdpdr", CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_COMPRESS_RDP,
				 rdpdr_process);
	return (rdpdr_channel != NULL);
}
