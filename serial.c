/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.

   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008

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
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <strings.h>
#include <sys/ioctl.h>

#ifdef HAVE_SYS_MODEM_H
#include <sys/modem.h>
#endif
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#ifdef HAVE_SYS_STRTIO_H
#include <sys/strtio.h>
#endif

#include "rdesktop.h"

#define FILE_DEVICE_SERIAL_PORT		0x1b

#define SERIAL_SET_BAUD_RATE		1
#define SERIAL_SET_QUEUE_SIZE		2
#define SERIAL_SET_LINE_CONTROL		3
#define SERIAL_SET_BREAK_ON		4
#define SERIAL_SET_BREAK_OFF		5
#define SERIAL_IMMEDIATE_CHAR		6
#define SERIAL_SET_TIMEOUTS		7
#define SERIAL_GET_TIMEOUTS		8
#define SERIAL_SET_DTR			9
#define SERIAL_CLR_DTR			10
#define SERIAL_RESET_DEVICE		11
#define SERIAL_SET_RTS			12
#define SERIAL_CLR_RTS			13
#define SERIAL_SET_XOFF			14
#define SERIAL_SET_XON			15
#define SERIAL_GET_WAIT_MASK		16
#define SERIAL_SET_WAIT_MASK		17
#define SERIAL_WAIT_ON_MASK		18
#define SERIAL_PURGE			19
#define SERIAL_GET_BAUD_RATE		20
#define SERIAL_GET_LINE_CONTROL		21
#define SERIAL_GET_CHARS		22
#define SERIAL_SET_CHARS		23
#define SERIAL_GET_HANDFLOW		24
#define SERIAL_SET_HANDFLOW		25
#define SERIAL_GET_MODEMSTATUS		26
#define SERIAL_GET_COMMSTATUS		27
#define SERIAL_XOFF_COUNTER		28
#define SERIAL_GET_PROPERTIES		29
#define SERIAL_GET_DTRRTS		30
#define SERIAL_LSRMST_INSERT		31
#define SERIAL_CONFIG_SIZE		32
#define SERIAL_GET_COMMCONFIG		33
#define SERIAL_SET_COMMCONFIG		34
#define SERIAL_GET_STATS		35
#define SERIAL_CLEAR_STATS		36
#define SERIAL_GET_MODEM_CONTROL	37
#define SERIAL_SET_MODEM_CONTROL	38
#define SERIAL_SET_FIFO_CONTROL		39

#define STOP_BITS_1			0
#define STOP_BITS_2			2

#define NO_PARITY			0
#define ODD_PARITY			1
#define EVEN_PARITY			2

#define SERIAL_PURGE_TXABORT 0x00000001
#define SERIAL_PURGE_RXABORT 0x00000002
#define SERIAL_PURGE_TXCLEAR 0x00000004
#define SERIAL_PURGE_RXCLEAR 0x00000008

/* SERIAL_WAIT_ON_MASK */
#define SERIAL_EV_RXCHAR           0x0001	/* Any Character received */
#define SERIAL_EV_RXFLAG           0x0002	/* Received certain character */
#define SERIAL_EV_TXEMPTY          0x0004	/* Transmit Queue Empty */
#define SERIAL_EV_CTS              0x0008	/* CTS changed state */
#define SERIAL_EV_DSR              0x0010	/* DSR changed state */
#define SERIAL_EV_RLSD             0x0020	/* RLSD changed state */
#define SERIAL_EV_BREAK            0x0040	/* BREAK received */
#define SERIAL_EV_ERR              0x0080	/* Line status error occurred */
#define SERIAL_EV_RING             0x0100	/* Ring signal detected */
#define SERIAL_EV_PERR             0x0200	/* Printer error occurred */
#define SERIAL_EV_RX80FULL         0x0400	/* Receive buffer is 80 percent full */
#define SERIAL_EV_EVENT1           0x0800	/* Provider specific event 1 */
#define SERIAL_EV_EVENT2           0x1000	/* Provider specific event 2 */

/* Modem Status */
#define SERIAL_MS_DTR 0x01
#define SERIAL_MS_RTS 0x02
#define SERIAL_MS_CTS 0x10
#define SERIAL_MS_DSR 0x20
#define SERIAL_MS_RNG 0x40
#define SERIAL_MS_CAR 0x80

/* Handflow */
#define SERIAL_DTR_CONTROL	0x01
#define SERIAL_CTS_HANDSHAKE	0x08
#define SERIAL_ERROR_ABORT	0x80000000

#define SERIAL_XON_HANDSHAKE	0x01
#define SERIAL_XOFF_HANDSHAKE	0x02
#define SERIAL_DSR_SENSITIVITY	0x40

#define SERIAL_CHAR_EOF		0
#define SERIAL_CHAR_ERROR	1
#define SERIAL_CHAR_BREAK	2
#define SERIAL_CHAR_EVENT	3
#define SERIAL_CHAR_XON		4
#define SERIAL_CHAR_XOFF	5

#ifndef CRTSCTS
#define CRTSCTS 0
#endif

/* FIONREAD should really do the same thing as TIOCINQ, where it is
 * not available */
#if !defined(TIOCINQ) && defined(FIONREAD)
#define TIOCINQ FIONREAD
#endif
#if !defined(TIOCOUTQ) && defined(FIONWRITE)
#define TIOCOUTQ FIONWRITE
#endif

extern RDPDR_DEVICE g_rdpdr_device[];

static SERIAL_DEVICE *
get_serial_info(RD_NTHANDLE handle)
{
	int index;

	for (index = 0; index < RDPDR_MAX_DEVICES; index++)
	{
		if (handle == g_rdpdr_device[index].handle)
			return (SERIAL_DEVICE *) g_rdpdr_device[index].pdevice_data;
	}
	return NULL;
}

static RD_BOOL
get_termios(SERIAL_DEVICE * pser_inf, RD_NTHANDLE serial_fd)
{
	speed_t speed;
	struct termios *ptermios;

	ptermios = pser_inf->ptermios;

	if (tcgetattr(serial_fd, ptermios) == -1)
		return False;

	speed = cfgetispeed(ptermios);
	switch (speed)
	{
#ifdef B75
		case B75:
			pser_inf->baud_rate = 75;
			break;
#endif
#ifdef B110
		case B110:
			pser_inf->baud_rate = 110;
			break;
#endif
#ifdef B134
		case B134:
			pser_inf->baud_rate = 134;
			break;
#endif
#ifdef B150
		case B150:
			pser_inf->baud_rate = 150;
			break;
#endif
#ifdef B300
		case B300:
			pser_inf->baud_rate = 300;
			break;
#endif
#ifdef B600
		case B600:
			pser_inf->baud_rate = 600;
			break;
#endif
#ifdef B1200
		case B1200:
			pser_inf->baud_rate = 1200;
			break;
#endif
#ifdef B1800
		case B1800:
			pser_inf->baud_rate = 1800;
			break;
#endif
#ifdef B2400
		case B2400:
			pser_inf->baud_rate = 2400;
			break;
#endif
#ifdef B4800
		case B4800:
			pser_inf->baud_rate = 4800;
			break;
#endif
#ifdef B9600
		case B9600:
			pser_inf->baud_rate = 9600;
			break;
#endif
#ifdef B19200
		case B19200:
			pser_inf->baud_rate = 19200;
			break;
#endif
#ifdef B38400
		case B38400:
			pser_inf->baud_rate = 38400;
			break;
#endif
#ifdef B57600
		case B57600:
			pser_inf->baud_rate = 57600;
			break;
#endif
#ifdef B115200
		case B115200:
			pser_inf->baud_rate = 115200;
			break;
#endif
#ifdef B230400
		case B230400:
			pser_inf->baud_rate = 230400;
			break;
#endif
#ifdef B460800
		case B460800:
			pser_inf->baud_rate = 460800;
			break;
#endif
		default:
			pser_inf->baud_rate = 9600;
			break;
	}

	speed = cfgetospeed(ptermios);
	pser_inf->dtr = (speed == B0) ? 0 : 1;

	pser_inf->stop_bits = (ptermios->c_cflag & CSTOPB) ? STOP_BITS_2 : STOP_BITS_1;
	pser_inf->parity =
		(ptermios->c_cflag & PARENB) ? ((ptermios->c_cflag & PARODD) ? ODD_PARITY :
						EVEN_PARITY) : NO_PARITY;
	switch (ptermios->c_cflag & CSIZE)
	{
		case CS5:
			pser_inf->word_length = 5;
			break;
		case CS6:
			pser_inf->word_length = 6;
			break;
		case CS7:
			pser_inf->word_length = 7;
			break;
		default:
			pser_inf->word_length = 8;
			break;
	}

	if (ptermios->c_cflag & CRTSCTS)
	{
		pser_inf->control = SERIAL_DTR_CONTROL | SERIAL_CTS_HANDSHAKE | SERIAL_ERROR_ABORT;
	}
	else
	{
		pser_inf->control = SERIAL_DTR_CONTROL | SERIAL_ERROR_ABORT;
	}

	pser_inf->xonoff = SERIAL_DSR_SENSITIVITY;
	if (ptermios->c_iflag & IXON)
		pser_inf->xonoff |= SERIAL_XON_HANDSHAKE;

	if (ptermios->c_iflag & IXOFF)
		pser_inf->xonoff |= SERIAL_XOFF_HANDSHAKE;

	pser_inf->chars[SERIAL_CHAR_XON] = ptermios->c_cc[VSTART];
	pser_inf->chars[SERIAL_CHAR_XOFF] = ptermios->c_cc[VSTOP];
	pser_inf->chars[SERIAL_CHAR_EOF] = ptermios->c_cc[VEOF];
	pser_inf->chars[SERIAL_CHAR_BREAK] = ptermios->c_cc[VINTR];
	pser_inf->chars[SERIAL_CHAR_ERROR] = ptermios->c_cc[VKILL];

	return True;
}

static void
set_termios(SERIAL_DEVICE * pser_inf, RD_NTHANDLE serial_fd)
{
	speed_t speed;

	struct termios *ptermios;

	ptermios = pser_inf->ptermios;


	switch (pser_inf->baud_rate)
	{
#ifdef B75
		case 75:
			speed = B75;
			break;
#endif
#ifdef B110
		case 110:
			speed = B110;
			break;
#endif
#ifdef B134
		case 134:
			speed = B134;
			break;
#endif
#ifdef B150
		case 150:
			speed = B150;
			break;
#endif
#ifdef B300
		case 300:
			speed = B300;
			break;
#endif
#ifdef B600
		case 600:
			speed = B600;
			break;
#endif
#ifdef B1200
		case 1200:
			speed = B1200;
			break;
#endif
#ifdef B1800
		case 1800:
			speed = B1800;
			break;
#endif
#ifdef B2400
		case 2400:
			speed = B2400;
			break;
#endif
#ifdef B4800
		case 4800:
			speed = B4800;
			break;
#endif
#ifdef B9600
		case 9600:
			speed = B9600;
			break;
#endif
#ifdef B19200
		case 19200:
			speed = B19200;
			break;
#endif
#ifdef B38400
		case 38400:
			speed = B38400;
			break;
#endif
#ifdef B57600
		case 57600:
			speed = B57600;
			break;
#endif
#ifdef B115200
		case 115200:
			speed = B115200;
			break;
#endif
#ifdef B230400
		case 230400:
			speed = B115200;
			break;
#endif
#ifdef B460800
		case 460800:
			speed = B115200;
			break;
#endif
		default:
			speed = B9600;
			break;
	}

#ifdef CBAUD
	ptermios->c_cflag &= ~CBAUD;
	ptermios->c_cflag |= speed;
#else
	/* on systems with separate ispeed and ospeed, we can remember the speed
	   in ispeed while changing DTR with ospeed */
	cfsetispeed(pser_inf->ptermios, speed);
	cfsetospeed(pser_inf->ptermios, pser_inf->dtr ? speed : 0);
#endif

	ptermios->c_cflag &= ~(CSTOPB | PARENB | PARODD | CSIZE | CRTSCTS);
	switch (pser_inf->stop_bits)
	{
		case STOP_BITS_2:
			ptermios->c_cflag |= CSTOPB;
			break;
		default:
			ptermios->c_cflag &= ~CSTOPB;
			break;
	}

	switch (pser_inf->parity)
	{
		case EVEN_PARITY:
			ptermios->c_cflag |= PARENB;
			break;
		case ODD_PARITY:
			ptermios->c_cflag |= PARENB | PARODD;
			break;
		case NO_PARITY:
			ptermios->c_cflag &= ~(PARENB | PARODD);
			break;
	}

	switch (pser_inf->word_length)
	{
		case 5:
			ptermios->c_cflag |= CS5;
			break;
		case 6:
			ptermios->c_cflag |= CS6;
			break;
		case 7:
			ptermios->c_cflag |= CS7;
			break;
		default:
			ptermios->c_cflag |= CS8;
			break;
	}

#if 0
	if (pser_inf->rts)
		ptermios->c_cflag |= CRTSCTS;
	else
		ptermios->c_cflag &= ~CRTSCTS;
#endif

	if (pser_inf->control & SERIAL_CTS_HANDSHAKE)
	{
		ptermios->c_cflag |= CRTSCTS;
	}
	else
	{
		ptermios->c_cflag &= ~CRTSCTS;
	}


	if (pser_inf->xonoff & SERIAL_XON_HANDSHAKE)
	{
		ptermios->c_iflag |= IXON | IMAXBEL;
	}
	if (pser_inf->xonoff & SERIAL_XOFF_HANDSHAKE)
	{
		ptermios->c_iflag |= IXOFF | IMAXBEL;
	}

	if ((pser_inf->xonoff & (SERIAL_XOFF_HANDSHAKE | SERIAL_XON_HANDSHAKE)) == 0)
	{
		ptermios->c_iflag &= ~IXON;
		ptermios->c_iflag &= ~IXOFF;
	}

	ptermios->c_cc[VSTART] = pser_inf->chars[SERIAL_CHAR_XON];
	ptermios->c_cc[VSTOP] = pser_inf->chars[SERIAL_CHAR_XOFF];
	ptermios->c_cc[VEOF] = pser_inf->chars[SERIAL_CHAR_EOF];
	ptermios->c_cc[VINTR] = pser_inf->chars[SERIAL_CHAR_BREAK];
	ptermios->c_cc[VKILL] = pser_inf->chars[SERIAL_CHAR_ERROR];

	tcsetattr(serial_fd, TCSANOW, ptermios);
}

/* Enumeration of devices from rdesktop.c        */
/* returns number of units found and initialized. */
/* optarg looks like ':com1=/dev/ttyS0'           */
/* when it arrives to this function.              */
/* :com1=/dev/ttyS0,com2=/dev/ttyS1 */
int
serial_enum_devices(uint32 * id, char *optarg)
{
	SERIAL_DEVICE *pser_inf;

	char *pos = optarg;
	char *pos2;
	int count = 0;

	/* skip the first colon */
	optarg++;
	while ((pos = next_arg(optarg, ',')) && *id < RDPDR_MAX_DEVICES)
	{
		/* Init data structures for device */
		pser_inf = (SERIAL_DEVICE *) xmalloc(sizeof(SERIAL_DEVICE));
		pser_inf->ptermios = (struct termios *) xmalloc(sizeof(struct termios));
		memset(pser_inf->ptermios, 0, sizeof(struct termios));
		pser_inf->pold_termios = (struct termios *) xmalloc(sizeof(struct termios));
		memset(pser_inf->pold_termios, 0, sizeof(struct termios));

		pos2 = next_arg(optarg, '=');
		strcpy(g_rdpdr_device[*id].name, optarg);

		toupper_str(g_rdpdr_device[*id].name);

		g_rdpdr_device[*id].local_path = xmalloc(strlen(pos2) + 1);
		strcpy(g_rdpdr_device[*id].local_path, pos2);

		logger(Core, Debug, "serial_enum_devices(), %s to %s", g_rdpdr_device[*id].name,
		       g_rdpdr_device[*id].local_path);

		/* set device type */
		g_rdpdr_device[*id].device_type = DEVICE_TYPE_SERIAL;
		g_rdpdr_device[*id].pdevice_data = (void *) pser_inf;
		count++;
		(*id)++;

		optarg = pos;
	}
	return count;
}

static RD_NTSTATUS
serial_create(uint32 device_id, uint32 access, uint32 share_mode, uint32 disposition,
	      uint32 flags_and_attributes, char *filename, RD_NTHANDLE * handle)
{
	UNUSED(access);
	UNUSED(share_mode);
	UNUSED(disposition);
	UNUSED(flags_and_attributes);
	UNUSED(filename);
	int serial_fd;
	SERIAL_DEVICE *pser_inf;

	pser_inf = (SERIAL_DEVICE *) g_rdpdr_device[device_id].pdevice_data;

	serial_fd = open(g_rdpdr_device[device_id].local_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (serial_fd == -1)
	{
		logger(Core, Error, "serial_create(), open '%s' failed: %s",
		       g_rdpdr_device[device_id].local_path, strerror(errno));
		return RD_STATUS_ACCESS_DENIED;
	}

	if (!get_termios(pser_inf, serial_fd))
	{
		logger(Core, Error, "serial_create(), access denied to %s",
		       g_rdpdr_device[device_id].name);
		fflush(stdout);
		return RD_STATUS_ACCESS_DENIED;
	}

	/* Store handle for later use */
	g_rdpdr_device[device_id].handle = serial_fd;

	/* some sane information */
	logger(Core, Debug,
	       "serial_create(), %s to %s, speed %u baud, stop bits %u, parity %u, word length %u bits, dtr %u, rts %u",
	       g_rdpdr_device[device_id].name, g_rdpdr_device[device_id].local_path,
	       pser_inf->baud_rate, pser_inf->stop_bits, pser_inf->parity, pser_inf->word_length,
	       pser_inf->dtr, pser_inf->rts);

	pser_inf->ptermios->c_iflag &=
		~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	pser_inf->ptermios->c_oflag &= ~OPOST;
	pser_inf->ptermios->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	pser_inf->ptermios->c_cflag &= ~(CSIZE | PARENB);
	pser_inf->ptermios->c_cflag |= CS8;

	tcsetattr(serial_fd, TCSANOW, pser_inf->ptermios);

	pser_inf->event_txempty = 0;
	pser_inf->event_cts = 0;
	pser_inf->event_dsr = 0;
	pser_inf->event_rlsd = 0;
	pser_inf->event_pending = 0;

	*handle = serial_fd;

	/* all read and writes should be non blocking */
	if (fcntl(*handle, F_SETFL, O_NONBLOCK) == -1)
		logger(Core, Error, "serial_create(), failed to set non blocking: %s",
		       strerror(errno));

	pser_inf->read_total_timeout_constant = 5;

	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
serial_close(RD_NTHANDLE handle)
{
	int i = get_device_index(handle);
	if (i >= 0)
		g_rdpdr_device[i].handle = 0;

	rdpdr_abort_io(handle, 0, RD_STATUS_TIMEOUT);
	close(handle);
	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
serial_read(RD_NTHANDLE handle, uint8 * data, uint32 length, uint64 offset, uint32 * result)
{
	UNUSED(offset);  /* Offset must always be zero according to MS-RDPESP */
	long timeout;
	SERIAL_DEVICE *pser_inf;
	struct termios *ptermios;
#ifdef TIOCINQ
	int bytes_inqueue;
#endif


	timeout = 90;
	pser_inf = get_serial_info(handle);
	ptermios = pser_inf->ptermios;

	/* Set timeouts kind of like the windows serial timeout parameters. Multiply timeout
	   with requested read size */
	if (pser_inf->read_total_timeout_multiplier | pser_inf->read_total_timeout_constant)
	{
		timeout =
			(pser_inf->read_total_timeout_multiplier * length +
			 pser_inf->read_total_timeout_constant + 99) / 100;
	}
	else if (pser_inf->read_interval_timeout)
	{
		timeout = (pser_inf->read_interval_timeout * length + 99) / 100;
	}

	/* If a timeout is set, do a blocking read, which times out after some time.
	   It will make rdesktop less responsive, but it will improve serial performance, by not
	   reading one character at a time. */
	if (timeout == 0)
	{
		ptermios->c_cc[VTIME] = 0;
		ptermios->c_cc[VMIN] = 0;
	}
	else
	{
		ptermios->c_cc[VTIME] = timeout;
		ptermios->c_cc[VMIN] = 1;
	}
	tcsetattr(handle, TCSANOW, ptermios);

#if defined(TIOCINQ)
	ioctl(handle, TIOCINQ, &bytes_inqueue);
	logger(Core, Debug, "serial_read(), inqueue: %d expected %d\n", bytes_inqueue, length);
#endif

	*result = read(handle, data, length);

	logger(Core, Debug, "serial_read(), %d bytes read", *result);

	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
serial_write(RD_NTHANDLE handle, uint8 * data, uint32 length, uint64 offset, uint32 * result)
{
	UNUSED(offset);  /* Offset must always be zero according to MS-RDPESP */
	SERIAL_DEVICE *pser_inf;

	pser_inf = get_serial_info(handle);

	*result = write(handle, data, length);

	if (*result > 0)
		pser_inf->event_txempty = *result;

	logger(Core, Debug, "serial_write(), %d bytes written", *result);

	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
serial_device_control(RD_NTHANDLE handle, uint32 request, STREAM in, STREAM out)
{
	int purge_mask;
	uint32 result, modemstate;
	uint8 immediate;
	SERIAL_DEVICE *pser_inf;

	if ((request >> 16) != FILE_DEVICE_SERIAL_PORT)
		return RD_STATUS_INVALID_PARAMETER;

	pser_inf = get_serial_info(handle);

	/* extract operation */
	request >>= 2;
	request &= 0xfff;

	switch (request)
	{
		case SERIAL_SET_BAUD_RATE:
			in_uint32_le(in, pser_inf->baud_rate);
			set_termios(pser_inf, handle);
			logger(Protocol, Debug, "serial_device_control(), set baud rate %d",
			       pser_inf->baud_rate);
			break;
		case SERIAL_GET_BAUD_RATE:
			out_uint32_le(out, pser_inf->baud_rate);
			logger(Protocol, Debug, "serial_device_control(), get baud rate %d",
			       pser_inf->baud_rate);
			break;
		case SERIAL_SET_QUEUE_SIZE:
			in_uint32_le(in, pser_inf->queue_in_size);
			in_uint32_le(in, pser_inf->queue_out_size);
			logger(Protocol, Debug,
			       "serial_device_control(), set queue size, in=%d out=%d",
			       pser_inf->queue_in_size, pser_inf->queue_out_size);
			break;
		case SERIAL_SET_LINE_CONTROL:
			in_uint8(in, pser_inf->stop_bits);
			in_uint8(in, pser_inf->parity);
			in_uint8(in, pser_inf->word_length);
			set_termios(pser_inf, handle);
			logger(Protocol, Debug,
			       "serial_device_control(), set line control, stop=%d, parity=%d, word=%d",
			       pser_inf->stop_bits, pser_inf->parity, pser_inf->word_length);
			break;
		case SERIAL_GET_LINE_CONTROL:
			logger(Protocol, Debug, "serial_device_control(), get line control");
			out_uint8(out, pser_inf->stop_bits);
			out_uint8(out, pser_inf->parity);
			out_uint8(out, pser_inf->word_length);
			break;
		case SERIAL_IMMEDIATE_CHAR:
			logger(Protocol, Debug, "serial_device_control(), immediate char");
			in_uint8(in, immediate);
			serial_write(handle, &immediate, 1, 0, &result);
			break;
		case SERIAL_CONFIG_SIZE:
			logger(Protocol, Debug, "serial_device_control(), config size");
			out_uint32_le(out, 0);
			break;
		case SERIAL_GET_CHARS:
			logger(Protocol, Debug, "serial_device_control(), get chars");
			out_uint8a(out, pser_inf->chars, 6);
			break;
		case SERIAL_SET_CHARS:
			logger(Protocol, Debug, "serial_device_control(), set chars");
			in_uint8a(in, pser_inf->chars, 6);
			set_termios(pser_inf, handle);
			break;
		case SERIAL_GET_HANDFLOW:
			logger(Protocol, Debug, "serial_device_control(), get handflow");
			get_termios(pser_inf, handle);
			out_uint32_le(out, pser_inf->control);
			out_uint32_le(out, pser_inf->xonoff);	/* Xon/Xoff */
			out_uint32_le(out, pser_inf->onlimit);
			out_uint32_le(out, pser_inf->offlimit);
			break;
		case SERIAL_SET_HANDFLOW:
			in_uint32_le(in, pser_inf->control);
			in_uint32_le(in, pser_inf->xonoff);
			in_uint32_le(in, pser_inf->onlimit);
			in_uint32_le(in, pser_inf->offlimit);
			logger(Protocol, Debug,
			       "serial_device_control(), set handflow, control=%x, xonoff=%x, onlimit=%x, offlimit=%x",
			       pser_inf->control, pser_inf->xonoff, pser_inf->onlimit,
			       pser_inf->offlimit);
			set_termios(pser_inf, handle);
			break;
		case SERIAL_SET_TIMEOUTS:
			in_uint32(in, pser_inf->read_interval_timeout);
			in_uint32(in, pser_inf->read_total_timeout_multiplier);
			in_uint32(in, pser_inf->read_total_timeout_constant);
			in_uint32(in, pser_inf->write_total_timeout_multiplier);
			in_uint32(in, pser_inf->write_total_timeout_constant);
			logger(Protocol, Debug,
			       "serial_device_control(), set timeouts, timeout=%d, multiplier=%d, constant=%d",
			       pser_inf->read_interval_timeout,
			       pser_inf->read_total_timeout_multiplier,
			       pser_inf->read_total_timeout_constant);
			break;
		case SERIAL_GET_TIMEOUTS:
			logger(Protocol, Debug,
			       "serial_device_control(), get timeouts, timeout=%d, multiplier=%d, constant=%d",
			       pser_inf->read_interval_timeout,
			       pser_inf->read_total_timeout_multiplier,
			       pser_inf->read_total_timeout_constant);

			out_uint32(out, pser_inf->read_interval_timeout);
			out_uint32(out, pser_inf->read_total_timeout_multiplier);
			out_uint32(out, pser_inf->read_total_timeout_constant);
			out_uint32(out, pser_inf->write_total_timeout_multiplier);
			out_uint32(out, pser_inf->write_total_timeout_constant);
			break;
		case SERIAL_GET_WAIT_MASK:
			logger(Protocol, Debug, "serial_device_control(), get wait mask, mask=0x%x",
			       pser_inf->wait_mask);
			out_uint32(out, pser_inf->wait_mask);
			break;
		case SERIAL_SET_WAIT_MASK:
			in_uint32(in, pser_inf->wait_mask);
			logger(Protocol, Debug, "serial_device_control(), set wait mask, mask=0x%x",
			       pser_inf->wait_mask);
			break;
		case SERIAL_SET_DTR:
			logger(Protocol, Debug, "serial_device_control(), set dtr");
			ioctl(handle, TIOCMGET, &result);
			result |= TIOCM_DTR;
			ioctl(handle, TIOCMSET, &result);
			pser_inf->dtr = 1;
			break;
		case SERIAL_CLR_DTR:
			logger(Protocol, Debug, "serial_device_control(), clear dtr");
			ioctl(handle, TIOCMGET, &result);
			result &= ~TIOCM_DTR;
			ioctl(handle, TIOCMSET, &result);
			pser_inf->dtr = 0;
			break;
		case SERIAL_SET_RTS:
			logger(Protocol, Debug, "serial_device_control(), set rts");
			ioctl(handle, TIOCMGET, &result);
			result |= TIOCM_RTS;
			ioctl(handle, TIOCMSET, &result);
			pser_inf->rts = 1;
			break;
		case SERIAL_CLR_RTS:
			logger(Protocol, Debug, "serial_device_control(), clear rts");
			ioctl(handle, TIOCMGET, &result);
			result &= ~TIOCM_RTS;
			ioctl(handle, TIOCMSET, &result);
			pser_inf->rts = 0;
			break;
		case SERIAL_GET_MODEMSTATUS:
			modemstate = 0;
#ifdef TIOCMGET
			ioctl(handle, TIOCMGET, &result);
			if (result & TIOCM_CTS)
				modemstate |= SERIAL_MS_CTS;
			if (result & TIOCM_DSR)
				modemstate |= SERIAL_MS_DSR;
			if (result & TIOCM_RNG)
				modemstate |= SERIAL_MS_RNG;
			if (result & TIOCM_CAR)
				modemstate |= SERIAL_MS_CAR;
			if (result & TIOCM_DTR)
				modemstate |= SERIAL_MS_DTR;
			if (result & TIOCM_RTS)
				modemstate |= SERIAL_MS_RTS;
#endif
			logger(Protocol, Debug,
			       "serial_device_control(), get modem status, state=0x%x", modemstate);
			out_uint32_le(out, modemstate);
			break;
		case SERIAL_GET_COMMSTATUS:
			out_uint32_le(out, 0);	/* Errors */
			out_uint32_le(out, 0);	/* Hold reasons */

			result = 0;
#ifdef TIOCINQ
			ioctl(handle, TIOCINQ, &result);
#endif
			out_uint32_le(out, result);	/* Amount in in queue */
			if (result)
				logger(Protocol, Debug,
				       "serial_device_control(), get comm status, %d bytes in input queue",
				       result);

			result = 0;
#ifdef TIOCOUTQ
			ioctl(handle, TIOCOUTQ, &result);
#endif
			out_uint32_le(out, result);	/* Amount in out queue */
			if (result)
				logger(Protocol, Debug,
				       "serial_device_control(), get comm status, %d bytes in output queue",
				       result);

			out_uint8(out, 0);	/* EofReceived */
			out_uint8(out, 0);	/* WaitForImmediate */
			break;
		case SERIAL_PURGE:
			in_uint32(in, purge_mask);
			logger(Protocol, Debug, "serial_device_control(), purge, mask=0x%x",
			       purge_mask);
			if ((purge_mask & SERIAL_PURGE_TXCLEAR)
			    && (purge_mask & SERIAL_PURGE_RXCLEAR))
				tcflush(handle, TCIOFLUSH);
			else if (purge_mask & SERIAL_PURGE_TXCLEAR)
				tcflush(handle, TCOFLUSH);
			else if (purge_mask & SERIAL_PURGE_RXCLEAR)
				tcflush(handle, TCIFLUSH);
			if (purge_mask & SERIAL_PURGE_TXABORT)
				rdpdr_abort_io(handle, 4, RD_STATUS_CANCELLED);
			if (purge_mask & SERIAL_PURGE_RXABORT)
				rdpdr_abort_io(handle, 3, RD_STATUS_CANCELLED);
			break;
		case SERIAL_WAIT_ON_MASK:
			logger(Protocol, Debug, "serial_device_control(), wait on mask, mask=0x%x",
			       pser_inf->wait_mask);
			pser_inf->event_pending = 1;
			if (serial_get_event(handle, &result))
			{
				logger(Protocol, Debug,
				       "serial_device_control(), wait end, event=0x%x", result);
				out_uint32_le(out, result);
				break;
			}
			return RD_STATUS_PENDING;
			break;
		case SERIAL_SET_BREAK_ON:
			logger(Protocol, Debug, "serial_device_control(), set break on");
			tcsendbreak(handle, 0);
			break;
		case SERIAL_RESET_DEVICE:
			logger(Protocol, Debug, "serial_device_control(), reset device");
			break;
		case SERIAL_SET_BREAK_OFF:
			logger(Protocol, Debug, "serial_device_control(), set break off");
			break;
		case SERIAL_SET_XOFF:
			logger(Protocol, Debug, "serial_device_control(), set xoff");
			break;
		case SERIAL_SET_XON:
			logger(Protocol, Debug, "serial_device_control(), set xon");
			tcflow(handle, TCION);
			break;
		default:
			logger(Protocol, Warning, "serial_device_control(), unhandled ioctl %d",
			       request);
			return RD_STATUS_INVALID_PARAMETER;
	}

	return RD_STATUS_SUCCESS;
}

RD_BOOL
serial_get_event(RD_NTHANDLE handle, uint32 * result)
{
	int index;
	SERIAL_DEVICE *pser_inf;
	int bytes;
	RD_BOOL ret = False;

	*result = 0;
	index = get_device_index(handle);
	if (index < 0)
		return False;

#ifdef TIOCINQ
	pser_inf = (SERIAL_DEVICE *) g_rdpdr_device[index].pdevice_data;

	ioctl(handle, TIOCINQ, &bytes);

	if (bytes > 0)
	{
		logger(Protocol, Debug, "serial_get_event(), %d bytes", bytes);
		if (bytes > pser_inf->event_rlsd)
		{
			pser_inf->event_rlsd = bytes;
			if (pser_inf->wait_mask & SERIAL_EV_RLSD)
			{
				logger(Protocol, Debug,
				       "serial_get_event(), SERIAL_EV_RLSD is set");
				*result |= SERIAL_EV_RLSD;
				ret = True;
			}

		}

		if ((bytes > 1) && (pser_inf->wait_mask & SERIAL_EV_RXFLAG))
		{
			logger(Protocol, Debug,
			       "serial_get_event(), SERIAL_EV_RXFLAG is set, %d bytes", bytes);
			*result |= SERIAL_EV_RXFLAG;
			ret = True;
		}
		if ((pser_inf->wait_mask & SERIAL_EV_RXCHAR))
		{
			logger(Protocol, Debug,
			       "serial_get_event(), SERIAL_EV_RXCHAR is set, %d bytes", bytes);
			*result |= SERIAL_EV_RXCHAR;
			ret = True;
		}

	}
	else
	{
		pser_inf->event_rlsd = 0;
	}
#endif

#ifdef TIOCOUTQ
	ioctl(handle, TIOCOUTQ, &bytes);
	if ((bytes == 0)
	    && (pser_inf->event_txempty > 0) && (pser_inf->wait_mask & SERIAL_EV_TXEMPTY))
	{
		logger(Protocol, Debug, "serial_get_event(), SERIAL_EV_TXEMPT is set");
		*result |= SERIAL_EV_TXEMPTY;
		ret = True;
	}
	pser_inf->event_txempty = bytes;
#endif

	ioctl(handle, TIOCMGET, &bytes);
	if ((bytes & TIOCM_DSR) != pser_inf->event_dsr)
	{
		pser_inf->event_dsr = bytes & TIOCM_DSR;
		if (pser_inf->wait_mask & SERIAL_EV_DSR)
		{
			logger(Protocol, Debug, "serial_get_event(), SERIAL_EV_DSR=%s",
			       (bytes & TIOCM_DSR) ? "ON" : "OFF");
			*result |= SERIAL_EV_DSR;
			ret = True;
		}
	}

	if ((bytes & TIOCM_CTS) != pser_inf->event_cts)
	{
		pser_inf->event_cts = bytes & TIOCM_CTS;
		if (pser_inf->wait_mask & SERIAL_EV_CTS)
		{
			logger(Protocol, Debug, "serial_get_event(), SERIAL_EV_CTS=%s",
			       (bytes & TIOCM_CTS) ? "ON" : "OFF");
			*result |= SERIAL_EV_CTS;
			ret = True;
		}
	}

	if (ret)
		pser_inf->event_pending = 0;

	return ret;
}

/* Read timeout for a given file descriptor (device) when adding FDs to select() */
RD_BOOL
serial_get_timeout(RD_NTHANDLE handle, uint32 length, uint32 * timeout, uint32 * itv_timeout)
{
	int index;
	SERIAL_DEVICE *pser_inf;

	index = get_device_index(handle);
	if (index < 0)
		return True;

	if (g_rdpdr_device[index].device_type != DEVICE_TYPE_SERIAL)
	{
		return False;
	}

	pser_inf = (SERIAL_DEVICE *) g_rdpdr_device[index].pdevice_data;

	*timeout =
		pser_inf->read_total_timeout_multiplier * length +
		pser_inf->read_total_timeout_constant;
	*itv_timeout = pser_inf->read_interval_timeout;
	return True;
}

DEVICE_FNS serial_fns = {
	serial_create,
	serial_close,
	serial_read,
	serial_write,
	serial_device_control
};
