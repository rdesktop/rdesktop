#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <strings.h>
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
#define SERIAL_EV_RXCHAR           0x0001	// Any Character received
#define SERIAL_EV_RXFLAG           0x0002	// Received certain character
#define SERIAL_EV_TXEMPTY          0x0004	// Transmitt Queue Empty
#define SERIAL_EV_CTS              0x0008	// CTS changed state
#define SERIAL_EV_DSR              0x0010	// DSR changed state
#define SERIAL_EV_RLSD             0x0020	// RLSD changed state
#define SERIAL_EV_BREAK            0x0040	// BREAK received
#define SERIAL_EV_ERR              0x0080	// Line status error occurred
#define SERIAL_EV_RING             0x0100	// Ring signal detected
#define SERIAL_EV_PERR             0x0200	// Printer error occured
#define SERIAL_EV_RX80FULL         0x0400	// Receive buffer is 80 percent full
#define SERIAL_EV_EVENT1           0x0800	// Provider specific event 1
#define SERIAL_EV_EVENT2           0x1000	// Provider specific event 2


extern RDPDR_DEVICE g_rdpdr_device[];

SERIAL_DEVICE *
get_serial_info(HANDLE handle)
{
	int index;

	for (index = 0; index < RDPDR_MAX_DEVICES; index++)
	{
		if (handle == g_rdpdr_device[index].handle)
			return (SERIAL_DEVICE *) g_rdpdr_device[index].pdevice_data;
	}
	return NULL;
}

BOOL
get_termios(SERIAL_DEVICE * pser_inf, HANDLE serial_fd)
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
		default:
			pser_inf->baud_rate = 0;
			break;
	}

	speed = cfgetospeed(ptermios);
	pser_inf->dtr = (speed == B0) ? 0 : 1;

	pser_inf->stop_bits = (ptermios->c_cflag & CSTOPB) ? STOP_BITS_2 : STOP_BITS_1;
	pser_inf->parity =
		(ptermios->
		 c_cflag & PARENB) ? ((ptermios->
				       c_cflag & PARODD) ? ODD_PARITY : EVEN_PARITY) : NO_PARITY;
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

	pser_inf->rts = (ptermios->c_cflag & CRTSCTS) ? 1 : 0;

	return True;
}

static void
set_termios(SERIAL_DEVICE * pser_inf, HANDLE serial_fd)
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
		default:
			speed = B0;
			break;
	}

	/* on systems with separate ispeed and ospeed, we can remember the speed
	   in ispeed while changing DTR with ospeed */
	cfsetispeed(pser_inf->ptermios, speed);
	cfsetospeed(pser_inf->ptermios, pser_inf->dtr ? speed : 0);

	ptermios->c_cflag &= ~(CSTOPB | PARENB | PARODD | CSIZE | CRTSCTS);
	switch (pser_inf->stop_bits)
	{
		case STOP_BITS_2:
			ptermios->c_cflag |= CSTOPB;
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

	if (pser_inf->rts)
		ptermios->c_cflag |= CRTSCTS;

	tcsetattr(serial_fd, TCSANOW, ptermios);
}

/* Enumeration of devices from rdesktop.c        */
/* returns numer of units found and initialized. */
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

	// skip the first colon
	optarg++;
	while ((pos = next_arg(optarg, ',')) && *id < RDPDR_MAX_DEVICES)
	{
		// Init data structures for device
		pser_inf = (SERIAL_DEVICE *) xmalloc(sizeof(SERIAL_DEVICE));
		pser_inf->ptermios = (struct termios *) xmalloc(sizeof(struct termios));
		pser_inf->pold_termios = (struct termios *) xmalloc(sizeof(struct termios));

		pos2 = next_arg(optarg, '=');
		strcpy(g_rdpdr_device[*id].name, optarg);

		toupper_str(g_rdpdr_device[*id].name);

		g_rdpdr_device[*id].local_path = xmalloc(strlen(pos2) + 1);
		strcpy(g_rdpdr_device[*id].local_path, pos2);
		printf("SERIAL %s to %s\n", g_rdpdr_device[*id].name,
		       g_rdpdr_device[*id].local_path);
		// set device type
		g_rdpdr_device[*id].device_type = DEVICE_TYPE_SERIAL;
		g_rdpdr_device[*id].pdevice_data = (void *) pser_inf;
		count++;
		(*id)++;

		optarg = pos;
	}
	return count;
}

NTSTATUS
serial_create(uint32 device_id, uint32 access, uint32 share_mode, uint32 disposition,
	      uint32 flags_and_attributes, char *filename, HANDLE * handle)
{
	HANDLE serial_fd;
	SERIAL_DEVICE *pser_inf;
	struct termios *ptermios;

	pser_inf = (SERIAL_DEVICE *) g_rdpdr_device[device_id].pdevice_data;
	ptermios = pser_inf->ptermios;
	serial_fd = open(g_rdpdr_device[device_id].local_path, O_RDWR | O_NOCTTY);

	if (serial_fd == -1)
	{
		perror("open");
		return STATUS_ACCESS_DENIED;
	}

	if (!get_termios(pser_inf, serial_fd))
	{
		printf("INFO: SERIAL %s access denied\n", g_rdpdr_device[device_id].name);
		fflush(stdout);
		return STATUS_ACCESS_DENIED;
	}

	// Store handle for later use
	g_rdpdr_device[device_id].handle = serial_fd;

	/* some sane information */
	printf("INFO: SERIAL %s to %s\nINFO: speed %u baud, stop bits %u, parity %u, word length %u bits, dtr %u, rts %u\n", g_rdpdr_device[device_id].name, g_rdpdr_device[device_id].local_path, pser_inf->baud_rate, pser_inf->stop_bits, pser_inf->parity, pser_inf->word_length, pser_inf->dtr, pser_inf->rts);

	printf("INFO: use stty to change settings\n");

/*	ptermios->c_cflag = B115200 | CRTSCTS | CS8 | CLOCAL | CREAD;
	ptermios->c_cflag |= CREAD;
	ptermios->c_lflag |= ICANON;
	ptermios->c_iflag = IGNPAR | ICRNL;

	tcsetattr(serial_fd, TCSANOW, ptermios);
*/

	*handle = serial_fd;

	/* all read and writes should be non blocking */
	if (fcntl(*handle, F_SETFL, O_NONBLOCK) == -1)
		perror("fcntl");

	return STATUS_SUCCESS;
}

static NTSTATUS
serial_close(HANDLE handle)
{
	int i = get_device_index(handle);
	if (i >= 0)
		g_rdpdr_device[i].handle = 0;
	close(handle);
	return STATUS_SUCCESS;
}

NTSTATUS
serial_read(HANDLE handle, uint8 * data, uint32 length, uint32 offset, uint32 * result)
{
	long timeout;
	SERIAL_DEVICE *pser_inf;
	struct termios *ptermios;

	timeout = 90;
	pser_inf = get_serial_info(handle);
	ptermios = pser_inf->ptermios;

	// Set timeouts kind of like the windows serial timeout parameters. Multiply timeout
	// with requested read size
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

	// If a timeout is set, do a blocking read, which times out after some time.
	// It will make rdesktop less responsive, but it will improve serial performance, by not
	// reading one character at a time.
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


	*result = read(handle, data, length);

	//hexdump(data, *read);

	return STATUS_SUCCESS;
}

NTSTATUS
serial_write(HANDLE handle, uint8 * data, uint32 length, uint32 offset, uint32 * result)
{
	*result = write(handle, data, length);
	return STATUS_SUCCESS;
}

static NTSTATUS
serial_device_control(HANDLE handle, uint32 request, STREAM in, STREAM out)
{
	int flush_mask, purge_mask;
	uint32 result;
	uint8 immediate;
	SERIAL_DEVICE *pser_inf;
	struct termios *ptermios;

	if ((request >> 16) != FILE_DEVICE_SERIAL_PORT)
		return STATUS_INVALID_PARAMETER;

	pser_inf = get_serial_info(handle);
	ptermios = pser_inf->ptermios;

	/* extract operation */
	request >>= 2;
	request &= 0xfff;

	printf("SERIAL IOCTL %d\n", request);

	switch (request)
	{
		case SERIAL_SET_BAUD_RATE:
			in_uint32_le(in, pser_inf->baud_rate);
			set_termios(pser_inf, handle);
			break;
		case SERIAL_GET_BAUD_RATE:
			out_uint32_le(out, pser_inf->baud_rate);
			break;
		case SERIAL_SET_QUEUE_SIZE:
			in_uint32_le(in, pser_inf->queue_in_size);
			in_uint32_le(in, pser_inf->queue_out_size);
			break;
		case SERIAL_SET_LINE_CONTROL:
			in_uint8(in, pser_inf->stop_bits);
			in_uint8(in, pser_inf->parity);
			in_uint8(in, pser_inf->word_length);
			set_termios(pser_inf, handle);
			break;
		case SERIAL_GET_LINE_CONTROL:
			out_uint8(out, pser_inf->stop_bits);
			out_uint8(out, pser_inf->parity);
			out_uint8(out, pser_inf->word_length);
			break;
		case SERIAL_IMMEDIATE_CHAR:
			in_uint8(in, immediate);
			serial_write(handle, &immediate, 1, 0, &result);
			break;
		case SERIAL_CONFIG_SIZE:
			out_uint32_le(out, 0);
			break;
		case SERIAL_GET_CHARS:
			out_uint8s(out, 6);
			break;
		case SERIAL_SET_CHARS:
			in_uint8s(in, 6);
			break;
		case SERIAL_GET_HANDFLOW:
			out_uint32_le(out, 0);
			out_uint32_le(out, 3);	/* Xon/Xoff */
			out_uint32_le(out, 0);
			out_uint32_le(out, 0);
			break;
		case SERIAL_SET_HANDFLOW:
			in_uint8s(in, 16);
			break;
		case SERIAL_SET_TIMEOUTS:
			in_uint8s(in, 20);
			break;
		case SERIAL_GET_TIMEOUTS:
			out_uint8s(out, 20);
			break;
		case SERIAL_GET_WAIT_MASK:
			out_uint32(out, pser_inf->wait_mask);
			break;
		case SERIAL_SET_WAIT_MASK:
			in_uint32(in, pser_inf->wait_mask);
			break;
		case SERIAL_SET_DTR:
			pser_inf->dtr = 1;
			set_termios(pser_inf, handle);
			break;
		case SERIAL_CLR_DTR:
			pser_inf->dtr = 0;
			set_termios(pser_inf, handle);
			break;
		case SERIAL_SET_RTS:
			pser_inf->rts = 1;
			set_termios(pser_inf, handle);
			break;
		case SERIAL_CLR_RTS:
			pser_inf->rts = 0;
			set_termios(pser_inf, handle);
			break;
		case SERIAL_GET_MODEMSTATUS:
			out_uint32_le(out, 0);	/* Errors */
			break;
		case SERIAL_GET_COMMSTATUS:
			out_uint32_le(out, 0);	/* Errors */
			out_uint32_le(out, 0);	/* Hold reasons */
			out_uint32_le(out, 0);	/* Amount in in queue */
			out_uint32_le(out, 0);	/* Amount in out queue */
			out_uint8(out, 0);	/* EofReceived */
			out_uint8(out, 0);	/* WaitForImmediate */
			break;
#if 0
		case SERIAL_PURGE:
			printf("SERIAL_PURGE\n");
			in_uint32(in, purge_mask);
			if (purge_mask & 0x04)
				flush_mask |= TCOFLUSH;
			if (purge_mask & 0x08)
				flush_mask |= TCIFLUSH;
			if (flush_mask != 0)
				tcflush(handle, flush_mask);
			if (purge_mask & 0x01)
				rdpdr_abort_io(handle, 4, STATUS_CANCELLED);
			if (purge_mask & 0x02)
				rdpdr_abort_io(handle, 3, STATUS_CANCELLED);
			break;
		case SERIAL_WAIT_ON_MASK:
			/* XXX implement me */
			out_uint32_le(out, pser_inf->wait_mask);
			break;
		case SERIAL_SET_BREAK_ON:
			tcsendbreak(serial_fd, 0);
			break;
		case SERIAL_RESET_DEVICE:
		case SERIAL_SET_BREAK_OFF:
		case SERIAL_SET_XOFF:
		case SERIAL_SET_XON:
			/* ignore */
			break;
#endif
		default:
			unimpl("SERIAL IOCTL %d\n", request);
			return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

/* Read timeout for a given file descripter (device) when adding fd's to select() */
BOOL
serial_get_timeout(HANDLE handle, uint32 length, uint32 * timeout, uint32 * itv_timeout)
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
