#include "rdesktop.h"

FILE *printer_fp;

static NTSTATUS
printer_create(HANDLE *handle)
{
	printer_fp = popen("lpr", "w");
	*handle = 0;
	return STATUS_SUCCESS;
}

static NTSTATUS
printer_close(HANDLE handle)
{
	pclose(printer_fp);
	return STATUS_SUCCESS;
}

static NTSTATUS
printer_write(HANDLE handle, uint8 *data, uint32 length, uint32 *result)
{
	*result = fwrite(data, 1, length, printer_fp);
	return STATUS_SUCCESS;
}

DEVICE_FNS printer_fns =
{
	printer_create,
	printer_close,
	NULL, /* read */
	printer_write,
	NULL /* device_control */
};

