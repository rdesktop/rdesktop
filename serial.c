#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
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

int serial_fd;
struct termios termios;

int dtr;
uint32 baud_rate;
uint32 queue_in_size, queue_out_size;
uint32 wait_mask;
uint8 stop_bits, parity, word_length;

static BOOL
get_termios(void)
{
	speed_t speed;

	if (tcgetattr(serial_fd, &termios) == -1)
		return False;

	speed = cfgetispeed(&termios);
	switch (speed)
	{
		case B75:	baud_rate = 75; break;
		case B110:	baud_rate = 110; break;
		case B134:	baud_rate = 134; break;
		case B150:	baud_rate = 150; break;
		case B300:	baud_rate = 300; break;
		case B600:	baud_rate = 600; break;
		case B1200:	baud_rate = 1200; break;
		case B1800:	baud_rate = 1800; break;
		case B2400:	baud_rate = 2400; break;
		case B4800:	baud_rate = 4800; break;
		case B9600:	baud_rate = 9600; break;
		case B19200:	baud_rate = 19200; break;
		case B38400:	baud_rate = 38400; break;
		case B57600:	baud_rate = 57600; break;
		case B115200:	baud_rate = 115200; break;
		default:	baud_rate = 0; break;
	}

	speed = cfgetospeed(&termios);
	dtr = (speed == B0) ? 0 : 1;

	stop_bits = (termios.c_cflag & CSTOPB) ? STOP_BITS_2 : STOP_BITS_1;
	parity = (termios.c_cflag & PARENB) ? ((termios.c_cflag & PARODD) ? ODD_PARITY : EVEN_PARITY) : NO_PARITY;
	switch (termios.c_cflag & CSIZE)
	{
		case CS5: word_length = 5; break;
		case CS6: word_length = 6; break;
		case CS7: word_length = 7; break;
		default:  word_length = 8; break;
	}

	return True;
}

static void
set_termios(void)
{
	speed_t speed;

	switch (baud_rate)
	{
		case 75:	speed = B75; break;
		case 110:	speed = B110; break;
		case 134:	speed = B134; break;
		case 150:	speed = B150; break;
		case 300:	speed = B300; break;
		case 600:	speed = B600; break;
		case 1200:	speed = B1200; break;
		case 1800:	speed = B1800; break;
		case 2400:	speed = B2400; break;
		case 4800:	speed = B4800; break;
		case 9600:	speed = B9600; break;
		case 19200:	speed = B19200; break;
		case 38400:	speed = B38400; break;
		case 57600:	speed = B57600; break;
		case 115200:	speed = B115200; break;
		default:	speed = B0; break;
	}

	/* on systems with separate ispeed and ospeed, we can remember the speed
	   in ispeed while changing DTR with ospeed */
	cfsetispeed(&termios, speed);
	cfsetospeed(&termios, dtr ? speed : 0);

	termios.c_cflag &= ~(CSTOPB|PARENB|PARODD|CSIZE);
	switch (stop_bits)
	{
		case STOP_BITS_2:
			termios.c_cflag |= CSTOPB;
			break;
	}
	switch (parity)
	{
		case EVEN_PARITY:
			termios.c_cflag |= PARENB;
			break;
		case ODD_PARITY:
			termios.c_cflag |= PARENB|PARODD;
			break;
	}
	switch (word_length)
	{
		case 5:  termios.c_cflag |= CS5; break;
		case 6:  termios.c_cflag |= CS6; break;
		case 7:  termios.c_cflag |= CS7; break;
		default: termios.c_cflag |= CS8; break;
	}

	tcsetattr(serial_fd, TCSANOW, &termios);
}

static NTSTATUS
serial_create(HANDLE *handle)
{
	/* XXX do we have to handle concurrent open attempts? */
	serial_fd = open("/dev/ttyS0", O_RDWR);
	if (serial_fd == -1)
		return STATUS_ACCESS_DENIED;

	if (!get_termios())
		return STATUS_ACCESS_DENIED;

	*handle = 0;
	return STATUS_SUCCESS;
}

static NTSTATUS
serial_close(HANDLE handle)
{
	close(serial_fd);
	return STATUS_SUCCESS;
}

static NTSTATUS
serial_read(HANDLE handle, uint8 *data, uint32 length, uint32 *result)
{
	*result = read(serial_fd, data, length);
	return STATUS_SUCCESS;
}

static NTSTATUS
serial_write(HANDLE handle, uint8 *data, uint32 length, uint32 *result)
{
	*result = write(serial_fd, data, length);
	return STATUS_SUCCESS;
}

static NTSTATUS
serial_device_control(HANDLE handle, uint32 request, STREAM in, STREAM out)
{
	uint32 result;
	uint8 immediate;

	if ((request >> 16) != FILE_DEVICE_SERIAL_PORT)
		return STATUS_INVALID_PARAMETER;

	/* extract operation */
	request >>= 2;
	request &= 0xfff;

	printf("SERIAL IOCTL %d\n", request);

	switch (request)
	{
		case SERIAL_SET_BAUD_RATE:
			in_uint32_le(in, baud_rate);
			set_termios();
			break;
		case SERIAL_GET_BAUD_RATE:
			out_uint32_le(out, baud_rate);
			break;
		case SERIAL_SET_QUEUE_SIZE:
			in_uint32_le(in, queue_in_size);
			in_uint32_le(in, queue_out_size);
			break;
		case SERIAL_SET_LINE_CONTROL:
			in_uint8(in, stop_bits);
			in_uint8(in, parity);
			in_uint8(in, word_length);
			set_termios();
			break;
		case SERIAL_GET_LINE_CONTROL:
			out_uint8(out, stop_bits);
			out_uint8(out, parity);
			out_uint8(out, word_length);
			break;
		case SERIAL_IMMEDIATE_CHAR:
			in_uint8(in, immediate);
			serial_write(handle, &immediate, 1, &result);
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
			out_uint32_le(out, 3); /* Xon/Xoff */
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
			out_uint32(out, wait_mask);
			break;
		case SERIAL_SET_WAIT_MASK:
			in_uint32(in, wait_mask);
			break;
		case SERIAL_SET_DTR:
			dtr = 1;
			set_termios();
			break;
		case SERIAL_CLR_DTR:
			dtr = 0;
			set_termios();
			break;
#if 0
		case SERIAL_WAIT_ON_MASK:
			/* XXX implement me */
			break;
		case SERIAL_SET_BREAK_ON:
			tcsendbreak(serial_fd, 0);
			break;
		case SERIAL_PURGE:
			in_uint32(purge_mask);
			/* tcflush */
			break;
		case SERIAL_RESET_DEVICE:
		case SERIAL_SET_BREAK_OFF:
		case SERIAL_SET_RTS:
		case SERIAL_CLR_RTS:
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

DEVICE_FNS serial_fns =
{
	serial_create,
	serial_close,
	serial_read,
	serial_write,
	serial_device_control
};

