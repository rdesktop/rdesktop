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

extern RDPDR_DEVICE    g_rdpdr_device[];

int serial_fd;
struct termios termios;

int dtr;
uint32 baud_rate;
uint32 queue_in_size, queue_out_size;
uint32 wait_mask;
uint8 stop_bits, parity, word_length;

SERIAL_DEVICE
*get_serial_info(HANDLE handle)
{
        int     index;

        for (index = 0; index < RDPDR_MAX_DEVICES; index++)
        {
                if (handle == g_rdpdr_device[index].handle)
                        return (SERIAL_DEVICE *) g_rdpdr_device[index].pdevice_data;
        }
        return NULL;
}

BOOL
get_termios(SERIAL_DEVICE *pser_inf, HANDLE serial_fd)
{
        speed_t         speed;
        struct termios  *ptermios;

	ptermios	= pser_inf->ptermios;

        if (tcgetattr(serial_fd, ptermios) == -1)
                return False;

        speed = cfgetispeed(ptermios);
        switch (speed)
        {
#ifdef B75
                case B75:       pser_inf->baud_rate = 75; break;
#endif
#ifdef B110
                case B110:      pser_inf->baud_rate = 110; break;
#endif
#ifdef B134
                case B134:      pser_inf->baud_rate = 134; break;
#endif
#ifdef B150
                case B150:      pser_inf->baud_rate = 150; break;
#endif
#ifdef B300
                case B300:      pser_inf->baud_rate = 300; break;
#endif
#ifdef B600
                case B600:      pser_inf->baud_rate = 600; break;
#endif
#ifdef B1200
                case B1200:     pser_inf->baud_rate = 1200; break;
#endif
#ifdef B1800
                case B1800:     pser_inf->baud_rate = 1800; break;
#endif
#ifdef B2400
                case B2400:     pser_inf->baud_rate = 2400; break;
#endif
#ifdef B4800
                case B4800:     pser_inf->baud_rate = 4800; break;
#endif
#ifdef B9600
                case B9600:     pser_inf->baud_rate = 9600; break;
#endif
#ifdef B19200
                case B19200:    pser_inf->baud_rate = 19200; break;
#endif
#ifdef B38400
                case B38400:    pser_inf->baud_rate = 38400; break;
#endif
#ifdef B57600
                case B57600:    pser_inf->baud_rate = 57600; break;
#endif
#ifdef B115200
                case B115200:   pser_inf->baud_rate = 115200; break;
#endif
                default:        pser_inf->baud_rate = 0; break;
        }

        speed = cfgetospeed(ptermios);
        pser_inf->dtr = (speed == B0) ? 0 : 1;

        pser_inf->stop_bits = (ptermios->c_cflag & CSTOPB) ? STOP_BITS_2 : STOP_BITS_1;
        pser_inf->parity = (ptermios->c_cflag & PARENB) ? ((ptermios->c_cflag & PARODD) ? ODD_PARITY : EVEN_PARITY) : NO_PARITY;
        switch (ptermios->c_cflag & CSIZE)
        {
                case CS5: pser_inf->word_length = 5; break;
                case CS6: pser_inf->word_length = 6; break;
                case CS7: pser_inf->word_length = 7; break;
                default:  pser_inf->word_length = 8; break;
        }

        return True;
}

static void
set_termios(void)
{
	speed_t speed;

	switch (baud_rate)
	{
#ifdef B75
		case 75:	speed = B75;break;
#endif
#ifdef B110
		case 110:	speed = B110;break;
#endif
#ifdef B134
		case 134:	speed = B134;break;
#endif
#ifdef B150
		case 150:	speed = B150;break;
#endif
#ifdef B300
		case 300:	speed = B300;break;
#endif
#ifdef B600
		case 600:	speed = B600;break;
#endif
#ifdef B1200
		case 1200:	speed = B1200;break;
#endif
#ifdef B1800
		case 1800:	speed = B1800;break;
#endif
#ifdef B2400
		case 2400:	speed = B2400;break;
#endif
#ifdef B4800
		case 4800:	speed = B4800;break;
#endif
#ifdef B9600
		case 9600:	speed = B9600;break;
#endif
#ifdef B19200
		case 19200:	speed = B19200;break;
#endif
#ifdef B38400
		case 38400:	speed = B38400;break;
#endif
#ifdef B57600
		case 57600:	speed = B57600;break;
#endif
#ifdef B115200
		case 115200:	speed = B115200;break;
#endif
		default:	speed = B0;break;
	}

	/* on systems with separate ispeed and ospeed, we can remember the speed
	   in ispeed while changing DTR with ospeed */
	cfsetispeed(&termios, speed);
	cfsetospeed(&termios, dtr ? speed : 0);

	termios.c_cflag &= ~(CSTOPB | PARENB | PARODD | CSIZE);
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
			termios.c_cflag |= PARENB | PARODD;
			break;
	}
	switch (word_length)
	{
		case 5:
			termios.c_cflag |= CS5;
			break;
		case 6:
			termios.c_cflag |= CS6;
			break;
		case 7:
			termios.c_cflag |= CS7;
			break;
		default:
			termios.c_cflag |= CS8;
			break;
	}

	tcsetattr(serial_fd, TCSANOW, &termios);
}

/* Enumeration of devices from rdesktop.c        */
/* returns numer of units found and initialized. */
/* optarg looks like ':com1=/dev/ttyS0'           */
/* when it arrives to this function.              */
/*  windev u*dev   baud, parity, stop bits, wordlength */
/* :com1=/dev/ttyS0:9600,0|1|2,0|2,5|6|7|8:dtr */
int
serial_enum_devices(int *id, char* optarg)
{
	SERIAL_DEVICE* pser_inf;

	int argcount=0;
	char* pos = optarg;
	char* pos2;
	char* pos3;

	if(*id<RDPDR_MAX_DEVICES){
		// Init data structures for device
		pser_inf = (SERIAL_DEVICE *) xmalloc(sizeof(SERIAL_DEVICE));
		pser_inf->ptermios = (struct termios *) xmalloc(sizeof(struct termios));
		pser_inf->pold_termios = (struct termios *) xmalloc(sizeof(struct termios));

		// skip the first colon
		optarg++;
		while( (pos = next_arg( optarg, ':')) ){

			switch(argcount){
				/* com1=/dev/ttyS0 */
				case 0:
					pos2 = next_arg(optarg,'=');
					if( !pos2 || *pos2 == (char)0x00 ){
						error("-r comport arguments should look like: -r comport:com1=/dev/ttyS0\n");
						return 0;
					}
					/* optarg = com1, pos2 = /dev/ttyS0 */
					strcpy(g_rdpdr_device[*id].name,optarg);

					toupper_str(g_rdpdr_device[*id].name);

					g_rdpdr_device[*id].local_path = xmalloc( strlen(pos2) + 1 );
					strcpy(g_rdpdr_device[*id].local_path,pos2);
					break;
				/* 9600,0|1|2,O|2,5|6|7|8 */
				/* TODO: values should be set in serial_create()... ??? */
				case 1:
					pos2 = next_arg(optarg,',');
					/*optarg=9600*/
					pser_inf->baud_rate = atoi(optarg);
					if( !pos2 || *pos2 == (char)0x00 )
						break;
					pos3 = next_arg(pos2,',');
					/* pos2 = 0|1|2 */
					pser_inf->parity = atoi(pos2);
					/* pos3 = 0|2,5|6|7|8*/
					pos2 = next_arg(pos3,',');
					if( !pos3 || *pos3 == (char)0x00 )
						break;
				        pser_inf->stop_bits = atoi(pos3);
					/* pos2 = 5|6|7|8 */
					if( !pos2 || *pos2 == (char)0x00 )
						break;
					pser_inf->word_length = atoi(pos2);
					break;
				default:
					if( (*optarg != (char)0x00) && (strcmp( optarg, "dtr" ) == 0) ){
						pser_inf->dtr = 1;
					}
					/* TODO: add more switches here, like xon, xoff. they will be separated by colon
					if( (*optarg != (char)0x00) && (strcmp( optarg, "xon" ) == 0) ){
					}
					*/
					break;
			}
			argcount++;
			optarg=pos;
		}

		printf("SERIAL %s to %s", g_rdpdr_device[*id].name, g_rdpdr_device[*id].local_path );
		if( pser_inf->baud_rate != 0 ){
			printf(" with baud: %u, parity: %u, stop bits: %u word length: %u", pser_inf->baud_rate, pser_inf->parity, pser_inf->stop_bits, pser_inf->word_length );
			if( pser_inf->dtr )
				printf( " dtr set\n");
			else
				printf( "\n" );
		}else
			printf("\n");

		// set device type
		g_rdpdr_device[*id].device_type = DEVICE_TYPE_SERIAL;
		g_rdpdr_device[*id].pdevice_data = (void *) pser_inf;
		(*id)++;

		return 1;
	}
	return 0;
}

NTSTATUS
serial_create(uint32 device_id, uint32 access, uint32 share_mode, uint32 disposition, uint32 flags_and_attributes, char *filename, HANDLE *handle)
{
        HANDLE          serial_fd;
        SERIAL_DEVICE   *pser_inf;
        struct termios  *ptermios;
	SERIAL_DEVICE   tmp_inf;

        pser_inf        = (SERIAL_DEVICE *) g_rdpdr_device[device_id].pdevice_data;
        ptermios        = pser_inf->ptermios;
        serial_fd       = open(g_rdpdr_device[device_id].local_path, O_RDWR | O_NOCTTY);

        if (serial_fd == -1)
                return STATUS_ACCESS_DENIED;

	// before we clog the user inserted args store them locally
	//
	memcpy(&tmp_inf,pser_inf, sizeof(pser_inf) );

        if (!get_termios(pser_inf, serial_fd))
                return STATUS_ACCESS_DENIED;

        // Store handle for later use
        g_rdpdr_device[device_id].handle = serial_fd;
        tcgetattr(serial_fd, pser_inf->pold_termios);  // Backup original settings

        // Initial configuration.
        bzero(ptermios, sizeof(ptermios));
        ptermios->c_cflag = B9600 | CRTSCTS | CS8 | CLOCAL | CREAD;
        ptermios->c_iflag = IGNPAR;
        ptermios->c_oflag = 0;
        ptermios->c_lflag = 0; //non-canonical, no echo
        ptermios->c_cc[VTIME] = 0;
        tcsetattr(serial_fd, TCSANOW, ptermios);

	// overload with user settings
	// -- if there are any
	if( tmp_inf.baud_rate != 0 ){
		dtr = tmp_inf.dtr;
		baud_rate = tmp_inf.baud_rate;
		parity = tmp_inf.parity;
		stop_bits = tmp_inf.stop_bits;
		word_length = tmp_inf.word_length;
		set_termios();
	}

	*handle = serial_fd;
        return STATUS_SUCCESS;
}

static NTSTATUS
serial_close(HANDLE handle)
{
	close(serial_fd);
	return STATUS_SUCCESS;
}

NTSTATUS
serial_read(HANDLE handle, uint8 *data, uint32 length, uint32 offset, uint32 *result)
{
        long            timeout;
        SERIAL_DEVICE   *pser_inf;
        struct termios  *ptermios;

        timeout         = 0;
        pser_inf        = get_serial_info(handle);
        ptermios        = pser_inf->ptermios;

        // Set timeouts kind of like the windows serial timeout parameters. Multiply timeout
        // with requested read size
        if (pser_inf->read_total_timeout_multiplier | pser_inf->read_total_timeout_constant)
        {
                timeout = (pser_inf->read_total_timeout_multiplier * length + pser_inf->read_total_timeout_constant + 99) / 100;
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
        return STATUS_SUCCESS;
}

NTSTATUS
serial_write(HANDLE handle, uint8 *data, uint32 length, uint32 offset, uint32 *result)
{
        *result = write(handle, data, length);
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

                        printf("SERIAL_PURGE\n");
                        in_uint32(in, purge_mask);
                        if (purge_mask & 0x04) flush_mask |= TCOFLUSH;
                        if (purge_mask & 0x08) flush_mask |= TCIFLUSH;
                        if (flush_mask != 0) tcflush(handle, flush_mask);
                        if (purge_mask & 0x01) rdpdr_abort_io(handle, 4, STATUS_CANCELLED);
                        if (purge_mask & 0x02) rdpdr_abort_io(handle, 3, STATUS_CANCELLED);
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

/* Read timeout for a given file descripter (device) when adding fd's to select() */
BOOL
serial_get_timeout(uint32 handle, uint32 length, uint32 *timeout, uint32 *itv_timeout)
{
        int             index;
        SERIAL_DEVICE   *pser_inf;

        index = get_device_index(handle);

        if (g_rdpdr_device[index].device_type != DEVICE_TYPE_SERIAL)
        {
                return False;
        }

        pser_inf = (SERIAL_DEVICE *) g_rdpdr_device[index].pdevice_data;

        *timeout = pser_inf->read_total_timeout_multiplier * length + pser_inf->read_total_timeout_constant;
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
