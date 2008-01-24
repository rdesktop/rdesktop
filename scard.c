/*
   rdesktop: A Remote Desktop Protocol client.
   Smart Card support
   Copyright (C) Alexi Volkov <alexi@myrealbox.com> 2006

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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/types.h>
#include <time.h>
#ifndef MAKE_PROTO
#ifdef __APPLE__
#include <PCSC/wintypes.h>
#include <PCSC/pcsclite.h>
#include <PCSC/winscard.h>
#else
#include <wintypes.h>
#include <pcsclite.h>
#include <winscard.h>
#endif /* PCSC_OSX */
#include "rdesktop.h"
#include "scard.h"

/* variable segment */

#define SCARD_MAX_MEM 102400
#define SCARD_AUTOALLOCATE -1
#define	OUT_STREAM_SIZE	4096

#ifdef B_ENDIAN
#define swap32(x)	((((x) & 0xff) << 24) | (((x) & 0xff00) << 8) |	\
			(((x) & 0xff0000) >> 8) | (((x) & 0xff000000) >> 24))

#define	swap16(x)	((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8))
#else
#define	swap32(x)	(x)
#define	swap16(x)	(x)
#endif

static pthread_mutex_t **scard_mutex = NULL;

static uint32 curDevice = 0, curId = 0, curBytesOut = 0;
static PSCNameMapRec nameMapList = NULL;
static int nameMapCount = 0;

static pthread_t queueHandler;
static pthread_mutex_t queueAccess;
static pthread_cond_t queueEmpty;
static pthread_mutex_t hcardAccess;

static PMEM_HANDLE threadListHandle = NULL;
static PThreadListElement threadList = NULL;


static PSCThreadData queueFirst = NULL, queueLast = NULL;
static int threadCount = 0;

static PSCHCardRec hcardFirst = NULL;

static void *queue_handler_function(void *data);

/* code segment */

#endif /* MAKE_PROTO */
void
scardSetInfo(uint32 device, uint32 id, uint32 bytes_out)
{
	curDevice = device;
	curId = id;
	curBytesOut = bytes_out;
}

#ifndef MAKE_PROTO

static RD_NTSTATUS
scard_create(uint32 device_id, uint32 accessmask, uint32 sharemode, uint32 create_disposition,
	     uint32 flags_and_attributes, char *filename, RD_NTHANDLE * phandle)
{
	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
scard_close(RD_NTHANDLE handle)
{
	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
scard_read(RD_NTHANDLE handle, uint8 * data, uint32 length, uint32 offset, uint32 * result)
{
	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
scard_write(RD_NTHANDLE handle, uint8 * data, uint32 length, uint32 offset, uint32 * result)
{
	return RD_STATUS_SUCCESS;
}
#endif /* MAKE_PROTO */

/* Enumeration of devices from rdesktop.c        */
/* returns numer of units found and initialized. */
/* optarg looks like ':"ReaderName=ReaderAlias"' */
/* when it arrives to this function.             */

int
scard_enum_devices(uint32 * id, char *optarg)
{
	char *name = optarg + 1;
	char *alias;
	int count = 0;
	PSCNameMapRec tmpMap;

	MYPCSC_DWORD rv;
	SCARDCONTEXT hContext;

	/* code segment  */
	rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &hContext);
	if (rv != SCARD_S_SUCCESS)
	{
		error("scard_enum_devices: PCSC service not available\n");
		return 0;
	}
	else
		rv = SCardReleaseContext(hContext);

	count = 0;

	if (0 != pthread_mutex_init(&queueAccess, NULL))
	{
		error("scard_enum_devices: Can't initialize queue access mutex\n");
		return 0;
	}

	if (0 != pthread_cond_init(&queueEmpty, NULL))
	{
		error("scard_enum_devices: Can't initialize queue control cv\n");
		return 0;
	}

	if (0 != pthread_mutex_init(&hcardAccess, NULL))
	{
		error("scard_enum_devices: Can't initialize hcard list access mutex\n");
		return 0;
	}

	if (0 !=
	    pthread_create(&queueHandler, NULL, (void *(*)(void *)) queue_handler_function, NULL))
	{
		error("scard_enum_devices: Can't create queue handling Thread\n");
		return 0;
	}

	strncpy(g_rdpdr_device[*id].name, "SCARD\0\0\0", 8);
	toupper_str(g_rdpdr_device[*id].name);
	g_rdpdr_device[*id].local_path = "/dev/scard";
	g_rdpdr_device[*id].pdevice_data = NULL;
	g_rdpdr_device[*id].handle = 0;
	g_rdpdr_device[*id].device_type = DEVICE_TYPE_SCARD;
	count++;
	(*id)++;

	if (*optarg == ':')
	{
		while ((optarg = next_arg(name, ',')) && *id < RDPDR_MAX_DEVICES)
		{
			int len;
			char *vendor = NULL;
			alias = next_arg(name, '=');
			vendor = next_arg(alias, ';');

			if (strlen(name) > 0)
			{
				if (!strlen(alias))
				{
					alias = name;
					vendor = "\0";
				}

				printf("Static/aliased Device:\n");
				printf("  Lin name: [%s]\n", name);
				printf("  Win name: [%s]\n", alias);
				printf("  Vendor  : [%s]\n", vendor);
				nameMapCount++;

				if (nameMapList == NULL)
					nameMapList = xmalloc(nameMapCount * sizeof(TSCNameMapRec));
				else
					nameMapList =
						xrealloc(nameMapList,
							 nameMapCount * sizeof(TSCNameMapRec));

				tmpMap = nameMapList + nameMapCount - 1;

				len = strlen(alias);
				strncpy(tmpMap->alias, alias, (len > 127) ? (127) : (len));
				len = strlen(name);
				strncpy(tmpMap->name, name, (len > 127) ? (127) : (len));

				if (vendor)
				{
					len = strlen(vendor);
					if (len > 0)
					{
						memset(tmpMap->vendor, 0, 128);
						strncpy(tmpMap->vendor, vendor,
							(len > 127) ? (127) : (len));
					}
					else
						tmpMap->vendor[0] = '\0';
				}
				else
					tmpMap->vendor[0] = '\0';
			}
			name = optarg;
		}
	}

	return count;
}

#ifndef MAKE_PROTO
/* ---------------------------------- */

/* These two functions depend heavily on the actual implementation of the smart
 * card handle in PC/SC Lite 1.3.1. Here are the salient bits:
 *
 * From winscard.c:331, in SCardConnect:
 *         *phCard = RFCreateReaderHandle(rContext);
 *
 * RFCreateReaderHandle (readerfactory.c:1161) creates a random short (16-bit
 * integer) and makes sure it's unique. Then it adds it to
 * rContext->dwIdentity.
 *
 * From readerfactory.c:173, in RFAddReader:
 *         (sReadersContexts[dwContext])->dwIdentity =
 *               (dwContext + 1) << (sizeof(DWORD) / 2) * 8;
 *
 * dwContext must be less than PCSCLITE_MAX_READERS_CONTEXTS, which is defined
 * to be 16 in the 1.3.1 release. 
 *
 * The use of "(sizeof(DWORD) / 2) * 8" is what makes conversion necessary in
 * order to use 64-bit card handles when talking to PC/SC Lite, and 32-bit card
 * handles when talking with the server, without losing any data: a card handle
 * made by a 32-bit PC/SC Lite looks like 0x00014d32, where the 4d32 is the
 * random 16 bits, 01 is the reader context index + 1, and it's left-shifted by 
 * 16 bits (sizeof(DWORD) == 4, divided by 2 is 2, times 8 is 16.) But a 64-bit
 * PC/SC Lite makes a card handle that looks like 0x0000000100004d32. The
 * reader context index+1 is left-shifted 32 bits because sizeof(DWORD) is 8,
 * not 4. This means the handle won't fit in 32 bits. (The multiplication by 8
 * is because sizeofs are in bytes, but saying how many places to left-shift is
 * speaking in bits.)
 *
 * So then. Maximum value of dwContext+1 is 17; we'll say this fits in a byte
 * to be loose and have plenty of room. This is then left-shifted by
 * sizeof(DWORD) / 2 * 8 - which in this file is sizeof(MYPCSC_DWORD) / 2 * 8.
 *
 * At any rate, if we take the handle as passed from PC/SC Lite, right-shift by
 * sizeof(MYPCSC_DWORD) / 2, left-shift by sizeof(SERVER_DWORD) / 2, and add
 * the lower two bytes of the value (the random number), we can fit all the
 * information into 32 bits without losing any. Of course, any time we want to
 * hand that back to PC/SC Lite, we'll have to expand it again. (And if
 * sizeof(MYPCSC_DWORD) == sizeof(SERVER_DWORD), we're essentially doing
 * nothing, which will not break anything.)
 *
 *
 * - jared.jennings@eglin.af.mil, 2 Aug 2006
 */


static MYPCSC_SCARDHANDLE
scHandleToMyPCSC(SERVER_SCARDHANDLE server)
{
	return (((MYPCSC_SCARDHANDLE) server >> (sizeof(SERVER_DWORD) * 8 / 2) & 0xffff)
		<< (sizeof(MYPCSC_DWORD) * 8 / 2)) + (server & 0xffff);
}

static SERVER_SCARDHANDLE
scHandleToServer(MYPCSC_SCARDHANDLE mypcsc)
{
	return ((mypcsc >> (sizeof(MYPCSC_DWORD) * 8 / 2) & 0xffff)
		<< (sizeof(SERVER_DWORD) * 8 / 2)) + (mypcsc & 0xffff);
}

/* ---------------------------------- */

static void *
SC_xmalloc(PMEM_HANDLE * memHandle, unsigned int size)
{
	PMEM_HANDLE handle = NULL;
	if (size > 0 && memHandle)
	{
		handle = xmalloc(size + sizeof(MEM_HANDLE));
		if (handle)
		{
			handle->prevHandle = NULL;
			handle->nextHandle = NULL;
			handle->dataSize = size;
			if (*memHandle)
			{
				handle->prevHandle = *memHandle;
				(*memHandle)->nextHandle = handle;
			}
			*memHandle = handle;
			return handle + 1;
		}
		else
			return NULL;
	}
	else
		return NULL;
}

static void
SC_xfree(PMEM_HANDLE * handle, void *memptr)
{
	if (memptr != NULL)
	{
		PMEM_HANDLE lcHandle = (PMEM_HANDLE) memptr - 1;
		if (lcHandle->dataSize > 0)
		{
			memset(memptr, 0, lcHandle->dataSize);
			if (lcHandle->nextHandle)
				lcHandle->nextHandle->prevHandle = lcHandle->prevHandle;
			if (lcHandle->prevHandle)
				lcHandle->prevHandle->nextHandle = lcHandle->nextHandle;
			if (*handle == lcHandle)
			{
				if (lcHandle->prevHandle)
					*handle = lcHandle->prevHandle;
				else
					*handle = lcHandle->nextHandle;
			}
			xfree(lcHandle);
		}
	}
}

static void
SC_xfreeallmemory(PMEM_HANDLE * handle)
{
	if (handle && (*handle))
	{
		if ((*handle)->prevHandle)
		{
			(*handle)->prevHandle->nextHandle = NULL;
			SC_xfreeallmemory(&((*handle)->prevHandle));
		}
		if ((*handle)->nextHandle)
		{
			(*handle)->nextHandle->prevHandle = NULL;
			SC_xfreeallmemory(&((*handle)->nextHandle));
		}
		memset(*handle, 0, (*handle)->dataSize + sizeof(MEM_HANDLE));
		xfree(*handle);
		*handle = NULL;
	}
}

/* ---------------------------------- */

static char *
getName(char *alias)
{
	int i;
	PSCNameMapRec tmpMap;
	for (i = 0, tmpMap = nameMapList; i < nameMapCount; i++, tmpMap++)
	{
		if (strcmp(tmpMap->alias, alias) == 0)
			return tmpMap->name;
	}
	return alias;
}

static char *
getVendor(char *name)
{
	int i;
	PSCNameMapRec tmpMap;
	for (i = 0, tmpMap = nameMapList; i < nameMapCount; i++, tmpMap++)
	{
		if (strcmp(tmpMap->name, name) == 0)
			return tmpMap->vendor;
	}
	return NULL;
}


static char *
getAlias(char *name)
{
	int i;
	PSCNameMapRec tmpMap;
	for (i = 0, tmpMap = nameMapList; i < nameMapCount; i++, tmpMap++)
	{
		if (strcmp(tmpMap->name, name) == 0)
			return tmpMap->alias;
	}
	return name;
}

static int
hasAlias(char *name)
{
	int i;
	PSCNameMapRec tmpMap;
	for (i = 0, tmpMap = nameMapList; i < nameMapCount; i++, tmpMap++)
	{
		if (strcmp(tmpMap->name, name) == 0)
			return 1;
	}
	return 0;
}

static void
inRepos(STREAM in, unsigned int read)
{
	SERVER_DWORD add = 4 - read % 4;
	if (add < 4 && add > 0)
	{
		in_uint8s(in, add);
	}
}

static void
outRepos(STREAM out, unsigned int written)
{
	SERVER_DWORD add = (4 - written % 4) % 4;
	if (add > 0)
	{
		out_uint8s(out, add);
	}
}


static void
outBufferStartWithLimit(STREAM out, int length, int highLimit)
{
	int header = (length < 0) ? (0) : ((length > highLimit) ? (highLimit) : (length));
	out_uint32_le(out, header);
	out_uint32_le(out, 0x00000001);	/* Magic DWORD - any non zero */
}


static void
outBufferStart(STREAM out, int length)
{
	outBufferStartWithLimit(out, length, 0x7FFFFFFF);
}

static void
outBufferFinishWithLimit(STREAM out, char *buffer, unsigned int length, unsigned int highLimit)
{
	int header = (length < 0) ? (0) : ((length > highLimit) ? (highLimit) : (length));
	out_uint32_le(out, header);

	if (length <= 0)
	{
		out_uint32_le(out, 0x00000000);
	}
	else
	{
		if (header < length)
			length = header;
		out_uint8p(out, buffer, length);
		outRepos(out, length);
	}
}

static void
outBufferFinish(STREAM out, char *buffer, unsigned int length)
{
	outBufferFinishWithLimit(out, buffer, length, 0x7FFFFFFF);
}

static void
outForceAlignment(STREAM out, unsigned int seed)
{
	SERVER_DWORD add = (seed - (out->p - out->data) % seed) % seed;
	if (add > 0)
		out_uint8s(out, add);
}

static unsigned int
inString(PMEM_HANDLE * handle, STREAM in, char **destination, SERVER_DWORD dataLength, RD_BOOL wide)
{
	unsigned int Result = (wide) ? (2 * dataLength) : (dataLength);
	PMEM_HANDLE lcHandle = NULL;
	char *buffer = SC_xmalloc(&lcHandle, Result + 2);
	char *reader;

	/* code segment */

	if (wide)
	{
		int i;
		in_uint8a(in, buffer, 2 * dataLength);
		for (i = 0; i < dataLength; i++)
			if ((buffer[2 * i] < 0) || (buffer[2 * i + 1] != 0))
				buffer[i] = '?';
			else
				buffer[i] = buffer[2 * i];
	}
	else
	{
		in_uint8a(in, buffer, dataLength);
	}

	buffer[dataLength] = '\0';
	reader = getName(buffer);
	*destination = SC_xmalloc(handle, strlen(reader) + 1);
	strcpy(*destination, reader);

	SC_xfreeallmemory(&lcHandle);
	return Result;
}

static unsigned int
outString(STREAM out, char *source, RD_BOOL wide)
{
	PMEM_HANDLE lcHandle = NULL;
	char *reader = getAlias(source);
	unsigned int dataLength = strlen(reader) + 1;
	unsigned int Result = (wide) ? (2 * dataLength) : (dataLength);

	/* code segment */

	if (wide)
	{
		int i;
		char *buffer = SC_xmalloc(&lcHandle, Result);

		for (i = 0; i < dataLength; i++)
		{
			if (source[i] < 0)
				buffer[2 * i] = '?';
			else
				buffer[2 * i] = reader[i];
			buffer[2 * i + 1] = '\0';
		}
		out_uint8p(out, buffer, 2 * dataLength);
	}
	else
	{
		out_uint8p(out, reader, dataLength);
	}

	SC_xfreeallmemory(&lcHandle);
	return Result;
}

static void
inReaderName(PMEM_HANDLE * handle, STREAM in, char **destination, RD_BOOL wide)
{
	SERVER_DWORD dataLength;
	in->p += 0x08;
	in_uint32_le(in, dataLength);
	inRepos(in, inString(handle, in, destination, dataLength, wide));
}

static void
inSkipLinked(STREAM in)
{
	SERVER_DWORD len;
	in_uint32_le(in, len);
	if (len > 0)
	{
		in_uint8s(in, len);
		inRepos(in, len);
	}
}

/* ---------------------------------- */
/*  Smart Card processing functions:  */
/* ---------------------------------- */

static MYPCSC_DWORD
SC_returnCode(MYPCSC_DWORD rc, PMEM_HANDLE * handle, STREAM in, STREAM out)
{
	SC_xfreeallmemory(handle);
	out_uint8s(out, 256);
	return rc;
}

static MYPCSC_DWORD
SC_returnNoMemoryError(PMEM_HANDLE * handle, STREAM in, STREAM out)
{
	return SC_returnCode(SCARD_E_NO_MEMORY, handle, in, out);
}

static MYPCSC_DWORD
TS_SCardEstablishContext(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	MYPCSC_SCARDCONTEXT hContext;
	/* code segment  */

	DEBUG_SCARD(("SCARD: SCardEstablishContext()\n"));
	rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &hContext);
	if (rv)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success (context: 0x%08lx)\n", hContext));
	}

	out_uint32_le(out, 0x00000004);
	out_uint32_le(out, (SERVER_DWORD) hContext);	/* must not be 0 (Seems to be pointer), don't know what is this (I use hContext as value) */
	/* i hope it's not a pointer because i just downcasted it - jlj */
	out_uint32_le(out, 0x00000004);
	out_uint32_le(out, (SERVER_DWORD) hContext);
	return rv;
}

static MYPCSC_DWORD
TS_SCardReleaseContext(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;

	in->p += 0x1C;
	in_uint32_le(in, hContext);
	DEBUG_SCARD(("SCARD: SCardReleaseContext(context: 0x%08x)\n", (unsigned) hContext));
	rv = SCardReleaseContext((MYPCSC_SCARDCONTEXT) hContext);

	if (rv)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
	}

	return rv;
}

static MYPCSC_DWORD
TS_SCardIsValidContext(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;
	char *readers;
	DWORD readerCount = 1024;
	PMEM_HANDLE lcHandle = NULL;

	in->p += 0x1C;
	in_uint32_le(in, hContext);
	DEBUG_SCARD(("SCARD: SCardIsValidContext(context: 0x%08x)\n", (unsigned) hContext));
	/* There is no realization of SCardIsValidContext in PC/SC Lite so we call SCardListReaders */

	readers = SC_xmalloc(&lcHandle, 1024);
	if (!readers)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	rv = SCardListReaders((MYPCSC_SCARDCONTEXT) hContext, NULL, readers, &readerCount);

	if (rv)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
		rv = SCARD_E_INVALID_HANDLE;
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
	}

	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}


static MYPCSC_DWORD
TS_SCardListReaders(STREAM in, STREAM out, RD_BOOL wide)
{
#define readerArraySize 1024
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;
	SERVER_DWORD dataLength;
	MYPCSC_DWORD cchReaders = readerArraySize;
	unsigned char *plen1, *plen2, *pend;
	char *readers, *cur;
	PMEM_HANDLE lcHandle = NULL;

	in->p += 0x2C;
	in_uint32_le(in, hContext);
	DEBUG_SCARD(("SCARD: SCardListReaders(context: 0x%08x)\n", (unsigned) hContext));
	plen1 = out->p;
	out_uint32_le(out, 0x00000000);	/* Temp value for data length as 0x0 */
	out_uint32_le(out, 0x01760650);
	plen2 = out->p;
	out_uint32_le(out, 0x00000000);	/* Temp value for data length as 0x0 */

	dataLength = 0;
	readers = SC_xmalloc(&lcHandle, readerArraySize);
	if (!readers)
		return SC_returnNoMemoryError(&lcHandle, in, out);


	readers[0] = '\0';
	readers[1] = '\0';
	rv = SCardListReaders((MYPCSC_SCARDCONTEXT) hContext, NULL, readers, &cchReaders);
	cur = readers;
	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		int i;
		PSCNameMapRec tmpMap;
		DEBUG_SCARD(("SCARD: -> Success\n"));
		for (i = 0, tmpMap = nameMapList; i < nameMapCount; i++, tmpMap++)
		{
			dataLength += outString(out, tmpMap->alias, wide);
		}

		int lenSC = strlen(cur);
		if (lenSC == 0)
			dataLength += outString(out, "\0", wide);
		else
			while (lenSC > 0)
			{
				if (!hasAlias(cur))
				{
					DEBUG_SCARD(("SCARD:    \"%s\"\n", cur));
					dataLength += outString(out, cur, wide);
				}
				cur = (void *) ((unsigned char *) cur + lenSC + 1);
				lenSC = strlen(cur);
			}
	}

	dataLength += outString(out, "\0", wide);
	outRepos(out, dataLength);

	pend = out->p;
	out->p = plen1;
	out_uint32_le(out, dataLength);
	out->p = plen2;
	out_uint32_le(out, dataLength);
	out->p = pend;

	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}


static MYPCSC_DWORD
TS_SCardConnect(STREAM in, STREAM out, RD_BOOL wide)
{
	MYPCSC_DWORD rv;
	SCARDCONTEXT hContext;
	char *szReader;
	SERVER_DWORD dwShareMode;
	SERVER_DWORD dwPreferredProtocol;
	MYPCSC_SCARDHANDLE myHCard;
	SERVER_SCARDHANDLE hCard;

	MYPCSC_DWORD dwActiveProtocol;
	PMEM_HANDLE lcHandle = NULL;

	in->p += 0x1C;
	in_uint32_le(in, dwShareMode);
	in_uint32_le(in, dwPreferredProtocol);
	inReaderName(&lcHandle, in, &szReader, wide);
	in->p += 0x04;
	in_uint32_le(in, hContext);
	DEBUG_SCARD(("SCARD: SCardConnect(context: 0x%08x, share: 0x%08x, proto: 0x%08x, reader: \"%s\")\n", (unsigned) hContext, (unsigned) dwShareMode, (unsigned) dwPreferredProtocol, szReader ? szReader : "NULL"));
	rv = SCardConnect(hContext, szReader, (MYPCSC_DWORD) dwShareMode,
			  (MYPCSC_DWORD) dwPreferredProtocol, &myHCard, &dwActiveProtocol);
	hCard = scHandleToServer(myHCard);
	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		char *szVendor = getVendor(szReader);
		DEBUG_SCARD(("SCARD: -> Success (hcard: 0x%08x [0x%08lx])\n",
			     (unsigned) hCard, (unsigned long) myHCard));
		if (szVendor && (strlen(szVendor) > 0))
		{
			DEBUG_SCARD(("SCARD: Set Attribute ATTR_VENDOR_NAME\n"));
			pthread_mutex_lock(&hcardAccess);
			PSCHCardRec hcard = xmalloc(sizeof(TSCHCardRec));
			if (hcard)
			{
				hcard->hCard = hCard;
				hcard->vendor = szVendor;
				hcard->next = NULL;
				hcard->prev = NULL;

				if (hcardFirst)
				{
					hcardFirst->prev = hcard;
					hcard->next = hcardFirst;
				}
				hcardFirst = hcard;
			}
			pthread_mutex_unlock(&hcardAccess);
		}
	}

	out_uint32_le(out, 0x00000000);
	out_uint32_le(out, 0x00000000);
	out_uint32_le(out, 0x00000004);
	out_uint32_le(out, 0x016Cff34);
	/* if the active protocol > 4 billion, this is trouble. odds are low */
	out_uint32_le(out, (SERVER_DWORD) dwActiveProtocol);
	out_uint32_le(out, 0x00000004);
	out_uint32_le(out, hCard);

	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}

static MYPCSC_DWORD
TS_SCardReconnect(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SCARDCONTEXT hContext;
	SERVER_SCARDHANDLE hCard;
	MYPCSC_SCARDHANDLE myHCard;
	SERVER_DWORD dwShareMode;
	SERVER_DWORD dwPreferredProtocol;
	SERVER_DWORD dwInitialization;
	MYPCSC_DWORD dwActiveProtocol;

	in->p += 0x20;
	in_uint32_le(in, dwShareMode);
	in_uint32_le(in, dwPreferredProtocol);
	in_uint32_le(in, dwInitialization);
	in->p += 0x04;
	in_uint32_le(in, hContext);
	in->p += 0x04;
	in_uint32_le(in, hCard);
	myHCard = scHandleToMyPCSC(hCard);
	DEBUG_SCARD(("SCARD: SCardReconnect(context: 0x%08x, hcard: 0x%08x [0x%08lx], share: 0x%08x, proto: 0x%08x, init: 0x%08x)\n", (unsigned) hContext, (unsigned) hCard, (unsigned long) myHCard, (unsigned) dwShareMode, (unsigned) dwPreferredProtocol, (unsigned) dwInitialization));
	rv = SCardReconnect(myHCard, (MYPCSC_DWORD) dwShareMode, (MYPCSC_DWORD) dwPreferredProtocol,
			    (MYPCSC_DWORD) dwInitialization, &dwActiveProtocol);
	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success (proto: 0x%08x)\n", (unsigned) dwActiveProtocol));
	}

	outForceAlignment(out, 8);
	out_uint32_le(out, (SERVER_DWORD) dwActiveProtocol);
	return rv;
}

static MYPCSC_DWORD
TS_SCardDisconnect(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;
	SERVER_SCARDHANDLE hCard;
	MYPCSC_SCARDHANDLE myHCard;
	SERVER_DWORD dwDisposition;

	in->p += 0x20;
	in_uint32_le(in, dwDisposition);
	in->p += 0x04;
	in_uint32_le(in, hContext);
	in->p += 0x04;
	in_uint32_le(in, hCard);

	DEBUG_SCARD(("SCARD: SCardDisconnect(context: 0x%08x, hcard: 0x%08x, disposition: 0x%08x)\n", (unsigned) hContext, (unsigned) hCard, (unsigned) dwDisposition));

	pthread_mutex_lock(&hcardAccess);
	PSCHCardRec hcard = hcardFirst;
	while (hcard)
	{
		if (hcard->hCard == hCard)
		{
			if (hcard->prev)
				hcard->prev->next = hcard->next;
			if (hcard->next)
				hcard->next->prev = hcard->prev;
			if (hcardFirst == hcard)
				hcardFirst = hcard->next;
			xfree(hcard);
			break;
		}
		hcard = hcard->next;
	}
	pthread_mutex_unlock(&hcardAccess);

	myHCard = scHandleToMyPCSC(hCard);
	rv = SCardDisconnect(myHCard, (MYPCSC_DWORD) dwDisposition);

	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
	}

	outForceAlignment(out, 8);
	return rv;
}

static int
needStatusRecheck(MYPCSC_DWORD rv, MYPCSC_LPSCARD_READERSTATE_A rsArray, SERVER_DWORD dwCount)
{
	int i, recall = 0;
	if (rv == SCARD_S_SUCCESS)
	{
		MYPCSC_LPSCARD_READERSTATE_A cur;
		for (i = 0, cur = rsArray; i < dwCount; i++, cur++)
		{
			if (cur->dwEventState & SCARD_STATE_UNKNOWN)
			{
				cur->dwCurrentState = cur->dwEventState;
				recall++;
			}
		}
	}
	return recall;
}

static RD_BOOL
mappedStatus(MYPCSC_DWORD code)
{
	code >>= 16;
	code &= 0x0000FFFF;
	return (code % 2);
}

static MYPCSC_DWORD
incStatus(MYPCSC_DWORD code, RD_BOOL mapped)
{
	if (mapped || (code & SCARD_STATE_CHANGED))
	{
		MYPCSC_DWORD count = (code >> 16) & 0x0000FFFF;
		count++;
		if (mapped && !(count % 2))
			count++;
		return (code & 0x0000FFFF) | (count << 16);
	}
	else
		return code;
}

static void
copyReaderState_MyPCSCToServer(MYPCSC_LPSCARD_READERSTATE_A src, SERVER_LPSCARD_READERSTATE_A dst,
			       MYPCSC_DWORD readerCount)
{
	MYPCSC_LPSCARD_READERSTATE_A srcIter;
	SERVER_LPSCARD_READERSTATE_A dstIter;
	MYPCSC_DWORD i;

	for (i = 0, srcIter = src, dstIter = dst; i < readerCount; i++, srcIter++, dstIter++)
	{
		dstIter->szReader = srcIter->szReader;
		dstIter->pvUserData = srcIter->pvUserData;
		dstIter->dwCurrentState = srcIter->dwCurrentState;
		dstIter->dwEventState = srcIter->dwEventState;
		dstIter->cbAtr = srcIter->cbAtr;
		memcpy(dstIter->rgbAtr, srcIter->rgbAtr, MAX_ATR_SIZE * sizeof(unsigned char));
	}
}

static void
copyReaderState_ServerToMyPCSC(SERVER_LPSCARD_READERSTATE_A src, MYPCSC_LPSCARD_READERSTATE_A dst,
			       SERVER_DWORD readerCount)
{
	SERVER_LPSCARD_READERSTATE_A srcIter;
	MYPCSC_LPSCARD_READERSTATE_A dstIter;
	SERVER_DWORD i;

	for (i = 0, srcIter = src, dstIter = dst; i < readerCount; i++, srcIter++, dstIter++)
	{
		dstIter->szReader = srcIter->szReader;
		dstIter->pvUserData = srcIter->pvUserData;
		dstIter->dwCurrentState = srcIter->dwCurrentState;
		dstIter->dwEventState = srcIter->dwEventState;
		dstIter->cbAtr = srcIter->cbAtr;
		memcpy(dstIter->rgbAtr, srcIter->rgbAtr, MAX_ATR_SIZE * sizeof(unsigned char));
	}
}


static MYPCSC_DWORD
TS_SCardGetStatusChange(STREAM in, STREAM out, RD_BOOL wide)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;
	SERVER_DWORD dwTimeout;
	SERVER_DWORD dwCount;
	SERVER_LPSCARD_READERSTATE_A rsArray, cur;
	SERVER_DWORD *stateArray = NULL, *curState;
	MYPCSC_LPSCARD_READERSTATE_A myRsArray;
	long i;
	PMEM_HANDLE lcHandle = NULL;
#if 0
	RD_BOOL mapped = False;
#endif

	in->p += 0x18;
	in_uint32_le(in, dwTimeout);
	in_uint32_le(in, dwCount);
	in->p += 0x08;
	in_uint32_le(in, hContext);
	in->p += 0x04;

	DEBUG_SCARD(("SCARD: SCardGetStatusChange(context: 0x%08x, timeout: 0x%08x, count: %d)\n",
		     (unsigned) hContext, (unsigned) dwTimeout, (int) dwCount));

	if (dwCount > 0)
	{
		rsArray = SC_xmalloc(&lcHandle, dwCount * sizeof(SERVER_SCARD_READERSTATE_A));
		if (!rsArray)
			return SC_returnNoMemoryError(&lcHandle, in, out);
		memset(rsArray, 0, dwCount * sizeof(SERVER_SCARD_READERSTATE_A));
		stateArray = SC_xmalloc(&lcHandle, dwCount * sizeof(MYPCSC_DWORD));
		if (!stateArray)
			return SC_returnNoMemoryError(&lcHandle, in, out);
		/* skip two pointers at beginning of struct */
		for (i = 0, cur = (SERVER_LPSCARD_READERSTATE_A) ((unsigned char **) rsArray + 2);
		     i < dwCount; i++, cur++)
		{
			in->p += 0x04;
			in_uint8a(in, cur, SERVER_SCARDSTATESIZE);
		}

		for (i = 0, cur = rsArray, curState = stateArray;
		     i < dwCount; i++, cur++, curState++)
		{
			SERVER_DWORD dataLength;

			/* Do endian swaps... */
			cur->dwCurrentState = swap32(cur->dwCurrentState);
			cur->dwEventState = swap32(cur->dwEventState);
			cur->cbAtr = swap32(cur->cbAtr);

			/* reset Current state hign bytes; */
			*curState = cur->dwCurrentState;
			cur->dwCurrentState &= 0x0000FFFF;
			cur->dwEventState &= 0x0000FFFF;

#if 0
			if (cur->dwCurrentState == (SCARD_STATE_CHANGED | SCARD_STATE_PRESENT))
			{
				cur->dwCurrentState = 0x00000000;
				mapped = True;
			}

			if (mappedStatus(*curState))
			{
				cur->dwCurrentState &= ~SCARD_STATE_INUSE;
				cur->dwEventState &= ~SCARD_STATE_INUSE;

				if (cur->dwCurrentState & SCARD_STATE_EMPTY)
				{
					cur->dwCurrentState &= ~SCARD_STATE_EMPTY;
					cur->dwCurrentState |= SCARD_STATE_UNKNOWN;
				}
			}
#endif

			in->p += 0x08;
			in_uint32_le(in, dataLength);
			inRepos(in,
				inString(&lcHandle, in, (char **) &(cur->szReader), dataLength,
					 wide));

			if (strcmp(cur->szReader, "\\\\?PnP?\\Notification") == 0)
				cur->dwCurrentState |= SCARD_STATE_IGNORE;

			DEBUG_SCARD(("SCARD:    \"%s\"\n", cur->szReader ? cur->szReader : "NULL"));
			DEBUG_SCARD(("SCARD:        user: 0x%08x, state: 0x%08x, event: 0x%08x\n",
				     (unsigned) cur->pvUserData, (unsigned) cur->dwCurrentState,
				     (unsigned) cur->dwEventState));
			DEBUG_SCARD(("SCARD:            current state: 0x%08x\n",
				     (unsigned) *curState));
		}
	}
	else
	{
		rsArray = NULL;
		stateArray = NULL;
	}

	myRsArray = SC_xmalloc(&lcHandle, dwCount * sizeof(MYPCSC_SCARD_READERSTATE_A));
	if (!rsArray)
		return SC_returnNoMemoryError(&lcHandle, in, out);
	memset(myRsArray, 0, dwCount * sizeof(SERVER_SCARD_READERSTATE_A));
	copyReaderState_ServerToMyPCSC(rsArray, myRsArray, (SERVER_DWORD) dwCount);

	rv = SCardGetStatusChange((MYPCSC_SCARDCONTEXT) hContext, (MYPCSC_DWORD) dwTimeout,
				  myRsArray, (MYPCSC_DWORD) dwCount);
	copyReaderState_MyPCSCToServer(myRsArray, rsArray, (MYPCSC_DWORD) dwCount);

	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
	}

	out_uint32_le(out, dwCount);
	out_uint32_le(out, 0x00084dd8);
	out_uint32_le(out, dwCount);

	for (i = 0, cur = rsArray, curState = stateArray; i < dwCount; i++, cur++, curState++)
	{

		cur->dwCurrentState = (*curState);
		cur->dwEventState |= (*curState) & 0xFFFF0000;

#if 0
		if (mapped && (cur->dwCurrentState & SCARD_STATE_PRESENT)
		    && (cur->dwCurrentState & SCARD_STATE_CHANGED)
		    && (cur->dwEventState & SCARD_STATE_PRESENT)
		    && (cur->dwEventState & SCARD_STATE_CHANGED))
		{
			cur->dwEventState |= SCARD_STATE_INUSE;
		}
		else if (cur->dwEventState & SCARD_STATE_UNKNOWN)
		{
			cur->dwEventState &= ~SCARD_STATE_UNKNOWN;
			cur->dwEventState |= SCARD_STATE_EMPTY;
			mapped = True;
		}
		else if ((!mapped) && (cur->dwEventState & SCARD_STATE_INUSE))
		{
			mapped = True;
			cur->dwEventState &= ~SCARD_STATE_INUSE;
		}

		cur->dwEventState = incStatus(cur->dwEventState, mapped);
#endif
		cur->dwEventState = incStatus(cur->dwEventState, False);

		DEBUG_SCARD(("SCARD:    \"%s\"\n", cur->szReader ? cur->szReader : "NULL"));
		DEBUG_SCARD(("SCARD:        user: 0x%08x, state: 0x%08x, event: 0x%08x\n",
			     (unsigned) cur->pvUserData, (unsigned) cur->dwCurrentState,
			     (unsigned) cur->dwEventState));

		/* Do endian swaps... */
		cur->dwCurrentState = swap32(cur->dwCurrentState);
		cur->dwEventState = swap32(cur->dwEventState);
		cur->cbAtr = swap32(cur->cbAtr);

		out_uint8p(out, (void *) ((unsigned char **) cur + 2),
			   sizeof(SERVER_SCARD_READERSTATE_A) - 2 * sizeof(unsigned char *));
	}
	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}

static MYPCSC_DWORD
TS_SCardCancel(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;

	in->p += 0x1C;
	in_uint32_le(in, hContext);
	DEBUG_SCARD(("SCARD: SCardCancel(context: 0x%08x)\n", (unsigned) hContext));
	rv = SCardCancel((MYPCSC_SCARDCONTEXT) hContext);
	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
	}
	outForceAlignment(out, 8);
	return rv;
}

static MYPCSC_DWORD
TS_SCardLocateCardsByATR(STREAM in, STREAM out, RD_BOOL wide)
{
	int i, j, k;
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;
	/* The SCARD_ATRMASK_L struct doesn't contain any longs or DWORDs -
	   no need to split into SERVER_ and MYPCSC_ */
	LPSCARD_ATRMASK_L pAtrMasks, cur;
	SERVER_DWORD atrMaskCount = 0;
	SERVER_DWORD readerCount = 0;
	SERVER_LPSCARD_READERSTATE_A rsArray, ResArray, rsCur;
	MYPCSC_LPSCARD_READERSTATE_A myRsArray;
	PMEM_HANDLE lcHandle = NULL;

	in->p += 0x2C;
	in_uint32_le(in, hContext);
	in_uint32_le(in, atrMaskCount);
	pAtrMasks = SC_xmalloc(&lcHandle, atrMaskCount * sizeof(SCARD_ATRMASK_L));
	if (!pAtrMasks)
		return SC_returnNoMemoryError(&lcHandle, in, out);
	in_uint8a(in, pAtrMasks, atrMaskCount * sizeof(SCARD_ATRMASK_L));

	in_uint32_le(in, readerCount);
	rsArray = SC_xmalloc(&lcHandle, readerCount * sizeof(SCARD_READERSTATE_A));
	if (!rsArray)
		return SC_returnNoMemoryError(&lcHandle, in, out);
	memset(rsArray, 0, readerCount * sizeof(SCARD_READERSTATE_A));

	DEBUG_SCARD(("SCARD: SCardLocateCardsByATR(context: 0x%08x, atrs: %d, readers: %d)\n",
		     (unsigned) hContext, (int) atrMaskCount, (int) readerCount));

	for (i = 0, cur = pAtrMasks; i < atrMaskCount; i++, cur++)
	{
		cur->cbAtr = swap32(cur->cbAtr);

		DEBUG_SCARD(("SCARD:    ATR: "));
		for (j = 0; j < pAtrMasks->cbAtr; j++)
		{
		DEBUG_SCARD(("%02x%c",
				     (unsigned) (unsigned char) cur->rgbAtr[j],
				     (j == pAtrMasks->cbAtr - 1) ? ' ' : ':'))}
		DEBUG_SCARD(("\n"));
		DEBUG_SCARD(("SCARD:         "));
		for (j = 0; j < pAtrMasks->cbAtr; j++)
		{
		DEBUG_SCARD(("%02x%c",
				     (unsigned) (unsigned char) cur->rgbMask[j],
				     (j == pAtrMasks->cbAtr - 1) ? ' ' : ':'))}
		DEBUG_SCARD(("\n"));
	}

	for (i = 0, rsCur = (SERVER_LPSCARD_READERSTATE_A) ((unsigned char **) rsArray + 2);
	     i < readerCount; i++, rsCur++)
	{
		in_uint8s(in, 4);
		in_uint8a(in, rsCur, SERVER_SCARDSTATESIZE);
	}

	ResArray = SC_xmalloc(&lcHandle, readerCount * sizeof(SERVER_SCARD_READERSTATE_A));
	if (!ResArray)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	for (i = 0, rsCur = rsArray; i < readerCount; i++, rsCur++)
	{
		/* Do endian swaps... */
		rsCur->dwCurrentState = swap32(rsCur->dwCurrentState);
		rsCur->dwEventState = swap32(rsCur->dwEventState);
		rsCur->cbAtr = swap32(rsCur->cbAtr);

		inReaderName(&lcHandle, in, (char **) &rsCur->szReader, wide);
		DEBUG_SCARD(("SCARD:    \"%s\"\n", rsCur->szReader ? rsCur->szReader : "NULL"));
		DEBUG_SCARD(("SCARD:        user: 0x%08x, state: 0x%08x, event: 0x%08x\n",
			     (unsigned) rsCur->pvUserData, (unsigned) rsCur->dwCurrentState,
			     (unsigned) rsCur->dwEventState));
	}
	memcpy(ResArray, rsArray, readerCount * sizeof(SERVER_SCARD_READERSTATE_A));

	/* FIXME segfault here. */
	myRsArray = SC_xmalloc(&lcHandle, readerCount * sizeof(MYPCSC_SCARD_READERSTATE_A));
	if (!myRsArray)
		return SC_returnNoMemoryError(&lcHandle, in, out);
	copyReaderState_ServerToMyPCSC(rsArray, myRsArray, readerCount);
	rv = SCardGetStatusChange((MYPCSC_SCARDCONTEXT) hContext, 0x00000001, myRsArray,
				  readerCount);
	copyReaderState_MyPCSCToServer(myRsArray, rsArray, readerCount);
	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
		cur = pAtrMasks;
		for (i = 0, cur = pAtrMasks; i < atrMaskCount; i++, cur++)
		{
			for (j = 0, rsCur = rsArray; j < readerCount; j++, rsCur++)
			{
				RD_BOOL equal = 1;
				for (k = 0; k < cur->cbAtr; k++)
				{
					if ((cur->rgbAtr[k] & cur->rgbMask[k]) !=
					    (rsCur->rgbAtr[k] & cur->rgbMask[k]))
					{
						equal = 0;
						break;
					}
				}
				if (equal)
				{
					rsCur->dwEventState |= 0x00000040;	/* SCARD_STATE_ATRMATCH 0x00000040 */
					memcpy(ResArray + j, rsCur, sizeof(SCARD_READERSTATE_A));
					DEBUG_SCARD(("SCARD:    \"%s\"\n",
						     rsCur->szReader ? rsCur->szReader : "NULL"));
					DEBUG_SCARD(("SCARD:        user: 0x%08x, state: 0x%08x, event: 0x%08x\n", (unsigned) rsCur->pvUserData, (unsigned) rsCur->dwCurrentState, (unsigned) rsCur->dwEventState));
				}
			}
		}
	}

	out_uint32_le(out, readerCount);
	out_uint32_le(out, 0x00084dd8);
	out_uint32_le(out, readerCount);

	for (i = 0, rsCur = ResArray; i < readerCount; i++, rsCur++)
	{
		/* Do endian swaps... */
		rsCur->dwCurrentState = swap32(rsCur->dwCurrentState);
		rsCur->dwEventState = swap32(rsCur->dwEventState);
		rsCur->cbAtr = swap32(rsCur->cbAtr);

		out_uint8p(out, (void *) ((unsigned char **) rsCur + 2),
			   sizeof(SCARD_READERSTATE_A) - 2 * sizeof(unsigned char *));
	}

	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}

static DWORD
TS_SCardBeginTransaction(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hCard;
	MYPCSC_SCARDCONTEXT myHCard;

	in->p += 0x30;
	in_uint32_le(in, hCard);
	myHCard = scHandleToMyPCSC(hCard);
	DEBUG_SCARD(("SCARD: SCardBeginTransaction(hcard: 0x%08x [0x%08lx])\n",
		     (unsigned) hCard, (unsigned long) myHCard));
	rv = SCardBeginTransaction(myHCard);
	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
	}
	outForceAlignment(out, 8);
	return rv;
}

static DWORD
TS_SCardEndTransaction(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hCard;
	MYPCSC_SCARDCONTEXT myHCard;
	SERVER_DWORD dwDisposition = 0;

	in->p += 0x20;
	in_uint32_le(in, dwDisposition);
	in->p += 0x0C;
	in_uint32_le(in, hCard);
	myHCard = scHandleToMyPCSC(hCard);

	DEBUG_SCARD(("[hCard = 0x%.8x]\n", (unsigned int) hCard));
	DEBUG_SCARD(("[myHCard = 0x%016lx]\n", (unsigned long) myHCard));
	DEBUG_SCARD(("[dwDisposition = 0x%.8x]\n", (unsigned int) dwDisposition));

	DEBUG_SCARD(("SCARD: SCardEndTransaction(hcard: 0x%08x [0x%08lx], disposition: 0x%08x)\n",
		     (unsigned) hCard, (unsigned long) myHCard, (unsigned) dwDisposition));
	rv = SCardEndTransaction(myHCard, (MYPCSC_DWORD) dwDisposition);
	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
	}
	outForceAlignment(out, 8);
	return rv;
}


static void
copyIORequest_MyPCSCToServer(MYPCSC_LPSCARD_IO_REQUEST src, SERVER_LPSCARD_IO_REQUEST dst)
{
	unsigned char *srcBytes, *dstBytes;
	size_t bytesToCopy = src->cbPciLength - sizeof(MYPCSC_SCARD_IO_REQUEST);
	srcBytes = ((unsigned char *) src + sizeof(MYPCSC_SCARD_IO_REQUEST));
	dstBytes = ((unsigned char *) dst + sizeof(SERVER_SCARD_IO_REQUEST));
	dst->dwProtocol = swap32((uint32_t) src->dwProtocol);
	dst->cbPciLength = swap32((uint32_t) src->cbPciLength
				  - sizeof(MYPCSC_SCARD_IO_REQUEST) +
				  sizeof(SERVER_SCARD_IO_REQUEST));
	memcpy(dstBytes, srcBytes, bytesToCopy);
}

static void
copyIORequest_ServerToMyPCSC(SERVER_LPSCARD_IO_REQUEST src, MYPCSC_LPSCARD_IO_REQUEST dst)
{
	unsigned char *srcBytes, *dstBytes;
	size_t bytesToCopy = src->cbPciLength - sizeof(SERVER_SCARD_IO_REQUEST);
	srcBytes = ((unsigned char *) src + sizeof(SERVER_SCARD_IO_REQUEST));
	dstBytes = ((unsigned char *) dst + sizeof(MYPCSC_SCARD_IO_REQUEST));
	dst->dwProtocol = swap32(src->dwProtocol);
	dst->cbPciLength = src->cbPciLength	/* already correct endian */
		- sizeof(SERVER_SCARD_IO_REQUEST) + sizeof(MYPCSC_SCARD_IO_REQUEST);
	memcpy(dstBytes, srcBytes, bytesToCopy);
}


static DWORD
TS_SCardTransmit(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_DWORD map[7], linkedLen;
	void *tmp;
	SERVER_SCARDCONTEXT hCard;
	MYPCSC_SCARDCONTEXT myHCard;
	SERVER_LPSCARD_IO_REQUEST pioSendPci, pioRecvPci;
	MYPCSC_LPSCARD_IO_REQUEST myPioSendPci, myPioRecvPci;
	unsigned char *sendBuf = NULL, *recvBuf = NULL;
	SERVER_DWORD cbSendLength, cbRecvLength;
	MYPCSC_DWORD myCbRecvLength;
	PMEM_HANDLE lcHandle = NULL;

	in->p += 0x14;
	in_uint32_le(in, map[0]);
	in->p += 0x04;
	in_uint32_le(in, map[1]);
	pioSendPci = SC_xmalloc(&lcHandle, sizeof(SERVER_SCARD_IO_REQUEST));
	if (!pioSendPci)
		return SC_returnNoMemoryError(&lcHandle, in, out);
	in_uint8a(in, pioSendPci, sizeof(SERVER_SCARD_IO_REQUEST));
	in_uint32_le(in, map[2]);
	in_uint32_le(in, cbSendLength);
	in_uint32_le(in, map[3]);
	in_uint32_le(in, map[4]);
	in_uint32_le(in, map[5]);
	in_uint32_le(in, cbRecvLength);
	if (map[0] & INPUT_LINKED)
		inSkipLinked(in);

	in->p += 0x04;
	in_uint32_le(in, hCard);
	myHCard = scHandleToMyPCSC(hCard);

	if (map[2] & INPUT_LINKED)
	{
		in_uint32_le(in, linkedLen);
		pioSendPci->cbPciLength = linkedLen + sizeof(SERVER_SCARD_IO_REQUEST);
		tmp = SC_xmalloc(&lcHandle, pioSendPci->cbPciLength);
		if (!tmp)
			return SC_returnNoMemoryError(&lcHandle, in, out);
		in_uint8a(in, (void *) ((unsigned char *) tmp + sizeof(SERVER_SCARD_IO_REQUEST)),
			  linkedLen);
		memcpy(tmp, pioSendPci, sizeof(SERVER_SCARD_IO_REQUEST));
		SC_xfree(&lcHandle, pioSendPci);
		pioSendPci = tmp;
		tmp = NULL;
	}
	else
		pioSendPci->cbPciLength = sizeof(SERVER_SCARD_IO_REQUEST);

	if (map[3] & INPUT_LINKED)
	{
		in_uint32_le(in, linkedLen);
		sendBuf = SC_xmalloc(&lcHandle, linkedLen);
		if (!sendBuf)
			return SC_returnNoMemoryError(&lcHandle, in, out);
		in_uint8a(in, sendBuf, linkedLen);
		inRepos(in, linkedLen);
	}
	else
		sendBuf = NULL;

	if (cbRecvLength)
	{
		recvBuf = SC_xmalloc(&lcHandle, cbRecvLength);
		if (!recvBuf)
			return SC_returnNoMemoryError(&lcHandle, in, out);
	}

	if (map[4] & INPUT_LINKED)
	{
		pioRecvPci = SC_xmalloc(&lcHandle, sizeof(SERVER_SCARD_IO_REQUEST));
		if (!pioRecvPci)
			return SC_returnNoMemoryError(&lcHandle, in, out);
		in_uint8a(in, pioRecvPci, sizeof(SERVER_SCARD_IO_REQUEST));
		in_uint32_le(in, map[6]);
		if (map[6] & INPUT_LINKED)
		{
			in_uint32_le(in, linkedLen);
			pioRecvPci->cbPciLength = linkedLen + sizeof(SERVER_SCARD_IO_REQUEST);
			tmp = SC_xmalloc(&lcHandle, pioRecvPci->cbPciLength);
			if (!tmp)
				return SC_returnNoMemoryError(&lcHandle, in, out);
			in_uint8a(in,
				  (void *) ((unsigned char *) tmp +
					    sizeof(SERVER_SCARD_IO_REQUEST)), linkedLen);
			memcpy(tmp, pioRecvPci, sizeof(SERVER_SCARD_IO_REQUEST));
			SC_xfree(&lcHandle, pioRecvPci);
			pioRecvPci = tmp;
			tmp = NULL;
		}
		else
			pioRecvPci->cbPciLength = sizeof(SERVER_SCARD_IO_REQUEST);
	}
	else
		pioRecvPci = NULL;

	DEBUG_SCARD(("SCARD: SCardTransmit(hcard: 0x%08x [0x%08lx], send: %d bytes, recv: %d bytes)\n", (unsigned) hCard, (unsigned long) myHCard, (int) cbSendLength, (int) cbRecvLength));

	myCbRecvLength = cbRecvLength;
	myPioSendPci = SC_xmalloc(&lcHandle,
				  sizeof(MYPCSC_SCARD_IO_REQUEST)
				  + pioSendPci->cbPciLength - sizeof(SERVER_SCARD_IO_REQUEST));
	if (!myPioSendPci)
		return SC_returnNoMemoryError(&lcHandle, in, out);
	copyIORequest_ServerToMyPCSC(pioSendPci, myPioSendPci);
	/* always a send, not always a recv */
	if (pioRecvPci)
	{
		myPioRecvPci = SC_xmalloc(&lcHandle,
					  sizeof(MYPCSC_SCARD_IO_REQUEST)
					  + pioRecvPci->cbPciLength
					  - sizeof(SERVER_SCARD_IO_REQUEST));
		if (!myPioRecvPci)
			return SC_returnNoMemoryError(&lcHandle, in, out);
		copyIORequest_ServerToMyPCSC(pioRecvPci, myPioRecvPci);
	}
	else
	{
		myPioRecvPci = NULL;
	}
	rv = SCardTransmit(myHCard, myPioSendPci, sendBuf, (MYPCSC_DWORD) cbSendLength,
			   myPioRecvPci, recvBuf, &myCbRecvLength);
	cbRecvLength = myCbRecvLength;

	/* FIXME: handle responses with length > 448 bytes */
	if (cbRecvLength > 448)
	{
		warning("Card response limited from %d to 448 bytes!\n", cbRecvLength);
		DEBUG_SCARD(("SCARD:    Truncated %d to %d\n", (unsigned int) cbRecvLength, 448));
		cbRecvLength = 448;
	}

	if (pioRecvPci)
	{
		/*
		 * pscs-lite mishandles this structure in some cases.
		 * make sure we only copy it if it is valid.
		 */
		if (myPioRecvPci->cbPciLength >= sizeof(MYPCSC_SCARD_IO_REQUEST))
			copyIORequest_MyPCSCToServer(myPioRecvPci, pioRecvPci);
	}

	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success (%d bytes)\n", (int) cbRecvLength));
#if 0
		if ((pioRecvPci != NULL) && (mypioRecvPci->cbPciLength > 0))
		{
			out_uint32_le(out, (DWORD) pioRecvPci);	/* if not NULL, this 4 bytes indicates that pioRecvPci is present */
		}
		else
#endif
			out_uint32_le(out, 0);	/* pioRecvPci 0x00; */

		outBufferStart(out, cbRecvLength);	/* start of recvBuf output */

#if 0
		if ((pioRecvPci) && (mypioRecvPci->cbPciLength > 0))
		{
			out_uint32_le(out, mypioRecvPci->dwProtocol);
			int len = mypioRecvPci->cbPciLength - sizeof(mypioRecvPci);
			outBufferStartWithLimit(out, len, 12);
			outBufferFinishWithLimit(out,
						 (char *) ((DWORD) pioRecvPci + sizeof(pioRecvPci)),
						 len, 12);
		}
#endif

		outBufferFinish(out, (char *) recvBuf, cbRecvLength);
	}
	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}

static MYPCSC_DWORD
TS_SCardStatus(STREAM in, STREAM out, RD_BOOL wide)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hCard;
	MYPCSC_SCARDCONTEXT myHCard;
	SERVER_DWORD dwState = 0, dwProtocol = 0, dwReaderLen, dwAtrLen;
	MYPCSC_DWORD state, protocol, readerLen, atrLen;
	SERVER_DWORD dataLength;
	PMEM_HANDLE lcHandle = NULL;
	char *readerName;
	unsigned char *atr;

	in->p += 0x24;
	in_uint32_le(in, dwReaderLen);
	in_uint32_le(in, dwAtrLen);
	in->p += 0x0C;
	in_uint32_le(in, hCard);
	in->p += 0x04;
	myHCard = scHandleToMyPCSC(hCard);

	DEBUG_SCARD(("SCARD: SCardStatus(hcard: 0x%08x [0x%08lx], reader len: %d bytes, atr len: %d bytes)\n", (unsigned) hCard, (unsigned long) myHCard, (int) dwReaderLen, (int) dwAtrLen));

	if (dwReaderLen <= 0 || dwReaderLen == SCARD_AUTOALLOCATE || dwReaderLen > SCARD_MAX_MEM)
		dwReaderLen = SCARD_MAX_MEM;
	if (dwAtrLen <= 0 || dwAtrLen == SCARD_AUTOALLOCATE || dwAtrLen > SCARD_MAX_MEM)
		dwAtrLen = SCARD_MAX_MEM;

#if 1
	/*
	 * Active client sometimes sends a readerlen *just* big enough
	 * SCardStatus doesn't seem to like this. This is a workaround,
	 * aka hack!
	 */
	dwReaderLen = 200;
#endif

	readerName = SC_xmalloc(&lcHandle, dwReaderLen + 2);
	if (!readerName)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	atr = SC_xmalloc(&lcHandle, dwAtrLen + 1);
	if (!atr)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	state = dwState;
	protocol = dwProtocol;
	readerLen = dwReaderLen;
	atrLen = dwAtrLen;
	rv = SCardStatus(myHCard, readerName, &readerLen, &state, &protocol, atr, &atrLen);
	dwAtrLen = atrLen;
	dwReaderLen = readerLen;
	dwProtocol = protocol;
	dwState = state;


	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
		return SC_returnCode(rv, &lcHandle, in, out);
	}
	else
	{
		int i;

		DEBUG_SCARD(("SCARD: -> Success (state: 0x%08x, proto: 0x%08x)\n",
			     (unsigned) dwState, (unsigned) dwProtocol));
		DEBUG_SCARD(("SCARD:        Reader: \"%s\"\n", readerName ? readerName : "NULL"));
		DEBUG_SCARD(("SCARD:        ATR: "));
		for (i = 0; i < dwAtrLen; i++)
		{
			DEBUG_SCARD(("%02x%c", atr[i], (i == dwAtrLen - 1) ? ' ' : ':'));
		}
		DEBUG_SCARD(("\n"));

		if (dwState & (SCARD_SPECIFIC | SCARD_NEGOTIABLE))
			dwState = 0x00000006;
		else
#if 0
		if (dwState & SCARD_SPECIFIC)
			dwState = 0x00000006;
		else if (dwState & SCARD_NEGOTIABLE)
			dwState = 0x00000005;
		else
#endif
		if (dwState & SCARD_POWERED)
			dwState = 0x00000004;
		else if (dwState & SCARD_SWALLOWED)
			dwState = 0x00000003;
		else if (dwState & SCARD_PRESENT)
			dwState = 0x00000002;
		else if (dwState & SCARD_ABSENT)
			dwState = 0x00000001;
		else
			dwState = 0x00000000;

		void *p_len1 = out->p;
		out_uint32_le(out, dwReaderLen);
		out_uint32_le(out, 0x00020000);
		out_uint32_le(out, dwState);
		out_uint32_le(out, dwProtocol);
		out_uint8p(out, atr, dwAtrLen);
		if (dwAtrLen < 32)
		{
			out_uint8s(out, 32 - dwAtrLen);
		}
		out_uint32_le(out, dwAtrLen);

		void *p_len2 = out->p;
		out_uint32_le(out, dwReaderLen);
		dataLength = outString(out, readerName, wide);
		dataLength += outString(out, "\0", wide);
		outRepos(out, dataLength);
		void *psave = out->p;
		out->p = p_len1;
		out_uint32_le(out, dataLength);
		out->p = p_len2;
		out_uint32_le(out, dataLength);
		out->p = psave;
	}
	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}

static MYPCSC_DWORD
TS_SCardState(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hCard;
	MYPCSC_SCARDCONTEXT myHCard;
	SERVER_DWORD dwState = 0, dwProtocol = 0, dwReaderLen, dwAtrLen;
	MYPCSC_DWORD state, protocol, readerLen, atrLen;
	PMEM_HANDLE lcHandle = NULL;
	char *readerName;
	unsigned char *atr;

	in->p += 0x24;
	in_uint32_le(in, dwAtrLen);
	in->p += 0x0C;
	in_uint32_le(in, hCard);
	in->p += 0x04;
	myHCard = scHandleToMyPCSC(hCard);

	DEBUG_SCARD(("SCARD: SCardState(hcard: 0x%08x [0x%08lx], atr len: %d bytes)\n",
		     (unsigned) hCard, (unsigned long) myHCard, (int) dwAtrLen));

	dwReaderLen = SCARD_MAX_MEM;
	if (dwAtrLen <= 0 || dwAtrLen == SCARD_AUTOALLOCATE || dwAtrLen > SCARD_MAX_MEM)
		dwAtrLen = SCARD_MAX_MEM;

	readerName = SC_xmalloc(&lcHandle, dwReaderLen + 2);
	if (!readerName)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	atr = SC_xmalloc(&lcHandle, dwAtrLen + 1);
	if (!atr)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	state = dwState;
	protocol = dwProtocol;
	readerLen = dwReaderLen;
	atrLen = dwAtrLen;
	rv = SCardStatus(myHCard, readerName, &readerLen, &state, &protocol, atr, &atrLen);
	dwAtrLen = atrLen;
	dwReaderLen = readerLen;
	dwProtocol = protocol;
	dwState = state;

	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
		return SC_returnCode(rv, &lcHandle, in, out);
	}
	else
	{
		int i;

		DEBUG_SCARD(("SCARD: -> Success (state: 0x%08x, proto: 0x%08x)\n",
			     (unsigned) dwState, (unsigned) dwProtocol));
		DEBUG_SCARD(("SCARD:        ATR: "));
		for (i = 0; i < dwAtrLen; i++)
		{
			DEBUG_SCARD(("%02x%c", atr[i], (i == dwAtrLen - 1) ? ' ' : ':'));
		}
		DEBUG_SCARD(("\n"));

		if (dwState & (SCARD_SPECIFIC | SCARD_NEGOTIABLE))
			dwState = 0x00000006;
		else
#if 0
		if (dwState & SCARD_SPECIFIC)
			dwState = 0x00000006;
		else if (dwState & SCARD_NEGOTIABLE)
			dwState = 0x00000005;
		else
#endif
		if (dwState & SCARD_POWERED)
			dwState = 0x00000004;
		else if (dwState & SCARD_SWALLOWED)
			dwState = 0x00000003;
		else if (dwState & SCARD_PRESENT)
			dwState = 0x00000002;
		else if (dwState & SCARD_ABSENT)
			dwState = 0x00000001;
		else
			dwState = 0x00000000;

		out_uint32_le(out, dwState);
		out_uint32_le(out, dwProtocol);
		out_uint32_le(out, dwAtrLen);
		out_uint32_le(out, 0x00000001);
		out_uint32_le(out, dwAtrLen);
		out_uint8p(out, atr, dwAtrLen);
		outRepos(out, dwAtrLen);
	}
	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}



#ifndef WITH_PCSC120

static MYPCSC_DWORD
TS_SCardListReaderGroups(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;
	SERVER_DWORD dwGroups;
	MYPCSC_DWORD groups;
	char *szGroups;
	PMEM_HANDLE lcHandle = NULL;

	in->p += 0x20;
	in_uint32_le(in, dwGroups);
	in->p += 0x04;
	in_uint32_le(in, hContext);

	DEBUG_SCARD(("SCARD: SCardListReaderGroups(context: 0x%08x, groups: %d)\n",
		     (unsigned) hContext, (int) dwGroups));

	if (dwGroups <= 0 || dwGroups == SCARD_AUTOALLOCATE || dwGroups > SCARD_MAX_MEM)
		dwGroups = SCARD_MAX_MEM;

	szGroups = SC_xmalloc(&lcHandle, dwGroups);
	if (!szGroups)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	groups = dwGroups;
	rv = SCardListReaderGroups((MYPCSC_SCARDCONTEXT) hContext, szGroups, &groups);
	dwGroups = groups;

	if (rv)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
		return SC_returnCode(rv, &lcHandle, in, out);
	}
	else
	{
		int i;
		char *cur;

		DEBUG_SCARD(("SCARD: -> Success\n"));
		for (i = 0, cur = szGroups; i < dwGroups; i++, cur += strlen(cur) + 1)
		{
			DEBUG_SCARD(("SCARD:    %s\n", cur));
		}
	}


	out_uint32_le(out, dwGroups);
	out_uint32_le(out, 0x00200000);
	out_uint32_le(out, dwGroups);
	out_uint8a(out, szGroups, dwGroups);
	outRepos(out, dwGroups);
	out_uint32_le(out, 0x00000000);

	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}

static MYPCSC_DWORD
TS_SCardGetAttrib(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hCard;
	MYPCSC_SCARDCONTEXT myHCard;
	SERVER_DWORD dwAttrId, dwAttrLen;
	MYPCSC_DWORD attrLen;
	unsigned char *pbAttr;
	PMEM_HANDLE lcHandle = NULL;

	in->p += 0x20;
	in_uint32_le(in, dwAttrId);
	in->p += 0x04;
	in_uint32_le(in, dwAttrLen);
	in->p += 0x0C;
	in_uint32_le(in, hCard);
	myHCard = scHandleToMyPCSC(hCard);

	dwAttrId = dwAttrId & 0x0000FFFF;

	DEBUG_SCARD(("SCARD: SCardGetAttrib(hcard: 0x%08x [0x%08lx], attrib: 0x%08x (%d bytes))\n",
		     (unsigned) hCard, (unsigned long) myHCard,
		     (unsigned) dwAttrId, (int) dwAttrLen));

	if (dwAttrLen > MAX_BUFFER_SIZE)
		dwAttrLen = MAX_BUFFER_SIZE;


	if (dwAttrLen > SCARD_AUTOALLOCATE)
		pbAttr = NULL;
	else if ((dwAttrLen < 0) || (dwAttrLen > SCARD_MAX_MEM))
	{
		dwAttrLen = SCARD_AUTOALLOCATE;
		pbAttr = NULL;
	}
	else
	{
		pbAttr = SC_xmalloc(&lcHandle, dwAttrLen);
		if (!pbAttr)
			return SC_returnNoMemoryError(&lcHandle, in, out);
	}

	attrLen = dwAttrLen;
	rv = SCardGetAttrib(myHCard, (MYPCSC_DWORD) dwAttrId, pbAttr, &attrLen);
	dwAttrLen = attrLen;

	if (dwAttrId == 0x00000100 && rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD:    Faking attribute ATTR_VENDOR_NAME\n"));
		pthread_mutex_lock(&hcardAccess);
		PSCHCardRec hcard = hcardFirst;
		while (hcard)
		{
			if (hcard->hCard == hCard)
			{
				dwAttrLen = strlen(hcard->vendor);
				memcpy(pbAttr, hcard->vendor, dwAttrLen);
				rv = SCARD_S_SUCCESS;
				break;
			}
			hcard = hcard->next;
		}
		pthread_mutex_unlock(&hcardAccess);
		DEBUG_SCARD(("[0x%.8x]\n", (unsigned int) rv));
	}

	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
		return SC_returnCode(rv, &lcHandle, in, out);
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success (%d bytes)\n", (int) dwAttrLen));

		out_uint32_le(out, dwAttrLen);
		out_uint32_le(out, 0x00000200);
		out_uint32_le(out, dwAttrLen);
		if (!pbAttr)
		{
			out_uint8s(out, dwAttrLen);
		}
		else
		{
			out_uint8p(out, pbAttr, dwAttrLen);
		}
		outRepos(out, dwAttrLen);
		out_uint32_le(out, 0x00000000);
	}
	outForceAlignment(out, 8);
	return rv;
}

static MYPCSC_DWORD
TS_SCardSetAttrib(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hCard;
	MYPCSC_SCARDCONTEXT myHCard;
	SERVER_DWORD dwAttrId;
	SERVER_DWORD dwAttrLen;
	unsigned char *pbAttr;
	PMEM_HANDLE lcHandle = NULL;

	in->p += 0x20;
	in_uint32_le(in, dwAttrId);
	in->p += 0x04;
	in_uint32_le(in, dwAttrLen);
	in->p += 0x0C;
	in_uint32_le(in, hCard);
	myHCard = scHandleToMyPCSC(hCard);

	dwAttrId = dwAttrId & 0x0000FFFF;

	DEBUG_SCARD(("SCARD: SCardSetAttrib(hcard: 0x%08x [0x%08lx], attrib: 0x%08x (%d bytes))\n",
		     (unsigned) hCard, (unsigned long) myHCard,
		     (unsigned) dwAttrId, (int) dwAttrLen));

	if (dwAttrLen > MAX_BUFFER_SIZE)
		dwAttrLen = MAX_BUFFER_SIZE;

	pbAttr = SC_xmalloc(&lcHandle, dwAttrLen);
	if (!pbAttr)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	in_uint8a(in, pbAttr, dwAttrLen);
	rv = SCardSetAttrib(myHCard, (MYPCSC_DWORD) dwAttrId, pbAttr, (MYPCSC_DWORD) dwAttrLen);

	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success\n"));
	}

	out_uint32_le(out, 0x00000000);
	out_uint32_le(out, 0x00000200);
	out_uint32_le(out, 0x00000000);
	out_uint32_le(out, 0x00000000);
	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}

#endif

static MYPCSC_DWORD
TS_SCardControl(STREAM in, STREAM out)
{
	MYPCSC_DWORD rv;
	SERVER_SCARDCONTEXT hContext;
	SERVER_SCARDHANDLE hCard;
	MYPCSC_SCARDHANDLE myHCard;
	SERVER_DWORD map[3];
	SERVER_DWORD dwControlCode;
	unsigned char *pInBuffer, *pOutBuffer;
	SERVER_DWORD nInBufferSize, nOutBufferSize, nOutBufferRealSize, nBytesReturned;
	MYPCSC_DWORD sc_nBytesReturned;
	PMEM_HANDLE lcHandle = NULL;

	pInBuffer = NULL;
	pOutBuffer = NULL;

	in->p += 0x14;
	in_uint32_le(in, map[0]);
	in->p += 0x04;
	in_uint32_le(in, map[1]);
	in_uint32_le(in, dwControlCode);
	in_uint32_le(in, nInBufferSize);
	in_uint32_le(in, map[2]);
	in->p += 0x04;
	in_uint32_le(in, nOutBufferSize);
	in->p += 0x04;
	in_uint32_le(in, hContext);
	in->p += 0x04;
	in_uint32_le(in, hCard);
	if (map[2] & INPUT_LINKED)
	{
		/* read real input size */
		in_uint32_le(in, nInBufferSize);
		pInBuffer = SC_xmalloc(&lcHandle, nInBufferSize);
		if (!pInBuffer)
			return SC_returnNoMemoryError(&lcHandle, in, out);
		in_uint8a(in, pInBuffer, nInBufferSize);
	}

#if 0
	if (nOutBufferSize > 0)
	{
		nOutBufferRealSize = nOutBufferSize;
	}
	else
#endif
		nOutBufferRealSize = 1024;

	nBytesReturned = nOutBufferRealSize;

	nBytesReturned = nOutBufferRealSize;
	pOutBuffer = SC_xmalloc(&lcHandle, nOutBufferRealSize);
	if (!pOutBuffer)
		return SC_returnNoMemoryError(&lcHandle, in, out);

	DEBUG_SCARD(("SCARD: SCardControl(context: 0x%08x, hcard: 0x%08x, code: 0x%08x, in: %d bytes, out: %d bytes)\n", (unsigned) hContext, (unsigned) hCard, (unsigned) dwControlCode, (int) nInBufferSize, (int) nOutBufferSize));

	sc_nBytesReturned = nBytesReturned;
	myHCard = scHandleToMyPCSC(hCard);
#ifdef WITH_PCSC120
	rv = SCardControl(myHCard, pInBuffer, (MYPCSC_DWORD) nInBufferSize, pOutBuffer,
			  &sc_nBytesReturned);
#else
	rv = SCardControl(myHCard, (MYPCSC_DWORD) dwControlCode, pInBuffer,
			  (MYPCSC_DWORD) nInBufferSize, pOutBuffer,
			  (MYPCSC_DWORD) nOutBufferRealSize, &sc_nBytesReturned);
#endif
	nBytesReturned = sc_nBytesReturned;

	if (rv != SCARD_S_SUCCESS)
	{
		DEBUG_SCARD(("SCARD: -> Failure: %s (0x%08x)\n",
			     pcsc_stringify_error(rv), (unsigned int) rv));
	}
	else
	{
		DEBUG_SCARD(("SCARD: -> Success (out: %d bytes)\n", (int) nBytesReturned));
	}

	out_uint32_le(out, nBytesReturned);
	out_uint32_le(out, 0x00000004);
	out_uint32_le(out, nBytesReturned);
	if (nBytesReturned > 0)
	{
		out_uint8p(out, pOutBuffer, nBytesReturned);
		outRepos(out, nBytesReturned);
	}

	outForceAlignment(out, 8);
	SC_xfreeallmemory(&lcHandle);
	return rv;
}

static MYPCSC_DWORD
TS_SCardAccessStartedEvent(STREAM in, STREAM out)
{
	DEBUG_SCARD(("SCARD: SCardAccessStartedEvent()\n"));
	out_uint8s(out, 8);
	return SCARD_S_SUCCESS;
}


static RD_NTSTATUS
scard_device_control(RD_NTHANDLE handle, uint32 request, STREAM in, STREAM out)
{
	SERVER_DWORD Result = 0x00000000;
	unsigned char *psize, *pend, *pStatusCode;
	SERVER_DWORD addToEnd = 0;

	/* Processing request */

	out_uint32_le(out, 0x00081001);	/* Header lines */
	out_uint32_le(out, 0xCCCCCCCC);
	psize = out->p;
	out_uint32_le(out, 0x00000000);	/* Size of data portion */
	out_uint32_le(out, 0x00000000);	/* Zero bytes (may be usefull) */
	pStatusCode = out->p;
	out_uint32_le(out, 0x00000000);	/* Status Code */

	switch (request)
	{
			/* SCardEstablishContext */
		case SC_ESTABLISH_CONTEXT:
			{
				Result = (SERVER_DWORD) TS_SCardEstablishContext(in, out);
				break;
			}
			/* SCardReleaseContext */
		case SC_RELEASE_CONTEXT:
			{
				Result = (SERVER_DWORD) TS_SCardReleaseContext(in, out);
				break;
			}
			/* SCardIsValidContext */
		case SC_IS_VALID_CONTEXT:
			{
				Result = (SERVER_DWORD) TS_SCardIsValidContext(in, out);
				break;
			}
			/* SCardListReaders */
		case SC_LIST_READERS:	/* SCardListReadersA */
		case SC_LIST_READERS + 4:	/* SCardListReadersW */
			{
				RD_BOOL wide = request != SC_LIST_READERS;
				Result = (SERVER_DWORD) TS_SCardListReaders(in, out, wide);
				break;
			}
			/* ScardConnect */
		case SC_CONNECT:	/* ScardConnectA */
		case SC_CONNECT + 4:	/* SCardConnectW */
			{
				RD_BOOL wide = request != SC_CONNECT;
				Result = (SERVER_DWORD) TS_SCardConnect(in, out, wide);
				break;
			}
			/* ScardReconnect */
		case SC_RECONNECT:
			{
				Result = (SERVER_DWORD) TS_SCardReconnect(in, out);
				break;
			}
			/* ScardDisconnect */
		case SC_DISCONNECT:
			{
				Result = (SERVER_DWORD) TS_SCardDisconnect(in, out);
				break;
			}
			/* ScardGetStatusChange */
		case SC_GET_STATUS_CHANGE:	/* SCardGetStatusChangeA */
		case SC_GET_STATUS_CHANGE + 4:	/* SCardGetStatusChangeW */
			{
				RD_BOOL wide = request != SC_GET_STATUS_CHANGE;
				Result = (SERVER_DWORD) TS_SCardGetStatusChange(in, out, wide);
				break;
			}
			/* SCardCancel */
		case SC_CANCEL:
			{
				Result = (SERVER_DWORD) TS_SCardCancel(in, out);
				break;
			}
			/* SCardLocateCardsByATR */
		case SC_LOCATE_CARDS_BY_ATR:	/* SCardLocateCardsByATRA */
		case SC_LOCATE_CARDS_BY_ATR + 4:	/* SCardLocateCardsByATRW */
			{
				RD_BOOL wide = request != SC_LOCATE_CARDS_BY_ATR;
				Result = (SERVER_DWORD) TS_SCardLocateCardsByATR(in, out, wide);
				break;
			}
			/* SCardBeginTransaction */
		case SC_BEGIN_TRANSACTION:
			{
				Result = (SERVER_DWORD) TS_SCardBeginTransaction(in, out);
				break;
			}
			/* SCardBeginTransaction */
		case SC_END_TRANSACTION:
			{
				Result = (SERVER_DWORD) TS_SCardEndTransaction(in, out);
				break;
			}
			/* ScardTransmit */
		case SC_TRANSMIT:
			{
				Result = (SERVER_DWORD) TS_SCardTransmit(in, out);
				break;
			}
			/* SCardControl */
		case SC_CONTROL:
			{
				Result = (SERVER_DWORD) TS_SCardControl(in, out);
				break;
			}
			/* SCardGetAttrib */
#ifndef WITH_PCSC120
		case SC_GETATTRIB:
			{
				Result = (SERVER_DWORD) TS_SCardGetAttrib(in, out);
				break;
			}
#endif
		case SC_ACCESS_STARTED_EVENT:
			{
				Result = (SERVER_DWORD) TS_SCardAccessStartedEvent(in, out);
				break;
			}
		case SC_STATUS:	/* SCardStatusA */
		case SC_STATUS + 4:	/* SCardStatusW */
			{
				RD_BOOL wide = request != SC_STATUS;
				Result = (SERVER_DWORD) TS_SCardStatus(in, out, wide);
				break;
			}
		case SC_STATE:	/* SCardState */
			{
				Result = (SERVER_DWORD) TS_SCardState(in, out);
				break;
			}
		default:
			{
				warning("SCARD: Unknown function %d\n", (int) request);
				Result = 0x80100014;
				out_uint8s(out, 256);
				break;
			}
	}

#if 0
	out_uint32_le(out, 0x00000000);
#endif
	/* Setting modified variables */
	pend = out->p;
	/* setting data size */
	out->p = psize;
	out_uint32_le(out, pend - psize - 16);
	/* setting status code */
	out->p = pStatusCode;
	out_uint32_le(out, Result);
	/* finish */
	out->p = pend;

	addToEnd = (pend - pStatusCode) % 16;
	if (addToEnd < 16 && addToEnd > 0)
	{
		out_uint8s(out, addToEnd);
	}

	return RD_STATUS_SUCCESS;
}

/* Thread functions */

static STREAM
duplicateStream(PMEM_HANDLE * handle, STREAM s, uint32 buffer_size, RD_BOOL isInputStream)
{
	STREAM d = SC_xmalloc(handle, sizeof(struct stream));
	if (d != NULL)
	{
		if (isInputStream)
			d->size = (size_t) (s->end) - (size_t) (s->data);
		else if (buffer_size < s->size)
			d->size = s->size;
		else
			d->size = buffer_size;

		d->data = SC_xmalloc(handle, d->size);

		d->end = (void *) ((size_t) (d->data) + (size_t) (s->end) - (size_t) (s->data));
		d->p = (void *) ((size_t) (d->data) + (size_t) (s->p) - (size_t) (s->data));
		d->iso_hdr =
			(void *) ((size_t) (d->data) + (size_t) (s->iso_hdr) - (size_t) (s->data));
		d->mcs_hdr =
			(void *) ((size_t) (d->data) + (size_t) (s->mcs_hdr) - (size_t) (s->data));
		d->sec_hdr =
			(void *) ((size_t) (d->data) + (size_t) (s->sec_hdr) - (size_t) (s->data));
		d->sec_hdr =
			(void *) ((size_t) (d->data) + (size_t) (s->sec_hdr) - (size_t) (s->data));
		d->rdp_hdr =
			(void *) ((size_t) (d->data) + (size_t) (s->rdp_hdr) - (size_t) (s->data));
		d->channel_hdr =
			(void *) ((size_t) (d->data) + (size_t) (s->channel_hdr) -
				  (size_t) (s->data));
		if (isInputStream)
			memcpy(d->data, s->data, (size_t) (s->end) - (size_t) (s->data));
		else
			memcpy(d->data, s->data, (size_t) (s->p) - (size_t) (s->data));
	}
	return d;
}

static void
freeStream(PMEM_HANDLE * handle, STREAM s)
{
	if (s != NULL)
	{
		if (s->data != NULL)
			SC_xfree(handle, s->data);
		SC_xfree(handle, s);
	}
}

static PSCThreadData
SC_addToQueue(RD_NTHANDLE handle, uint32 request, STREAM in, STREAM out)
{
	PMEM_HANDLE lcHandle = NULL;
	PSCThreadData data = SC_xmalloc(&lcHandle, sizeof(TSCThreadData));

	if (!data)
		return NULL;
	else
	{
		data->memHandle = lcHandle;
		data->device = curDevice;
		data->id = curId;
		data->handle = handle;
		data->request = request;
		data->in = duplicateStream(&(data->memHandle), in, 0, SC_TRUE);
		if (data->in == NULL)
		{
			SC_xfreeallmemory(&(data->memHandle));
			return NULL;
		}
		data->out =
			duplicateStream(&(data->memHandle), out, OUT_STREAM_SIZE + curBytesOut,
					SC_FALSE);
		if (data->out == NULL)
		{
			SC_xfreeallmemory(&(data->memHandle));
			return NULL;
		}
		data->next = NULL;

		pthread_mutex_lock(&queueAccess);

		if (queueLast)
			queueLast->next = data;
		queueLast = data;
		if (!queueFirst)
			queueFirst = data;

		pthread_cond_broadcast(&queueEmpty);
		pthread_mutex_unlock(&queueAccess);
	}
	return data;
}

static void
SC_destroyThreadData(PSCThreadData data)
{
	if (data)
	{
		PMEM_HANDLE handle = data->memHandle;
		SC_xfreeallmemory(&handle);
	}
}

static PSCThreadData
SC_getNextInQueue()
{
	PSCThreadData Result = NULL;

	pthread_mutex_lock(&queueAccess);

	while (queueFirst == NULL)
		pthread_cond_wait(&queueEmpty, &queueAccess);

	Result = queueFirst;
	queueFirst = queueFirst->next;
	if (!queueFirst)
	{
		queueLast = NULL;
	}
	Result->next = NULL;

	pthread_mutex_unlock(&queueAccess);

	return Result;
}

static void
SC_deviceControl(PSCThreadData data)
{
	size_t buffer_len = 0;
	scard_device_control(data->handle, data->request, data->in, data->out);
	buffer_len = (size_t) data->out->p - (size_t) data->out->data;
	rdpdr_send_completion(data->device, data->id, 0, buffer_len, data->out->data, buffer_len);
	SC_destroyThreadData(data);
}


static void *
thread_function(PThreadListElement listElement)
{
	pthread_mutex_lock(&listElement->busy);
	while (1)
	{
		while (listElement->data == NULL)
			pthread_cond_wait(&listElement->nodata, &listElement->busy);

		SC_deviceControl(listElement->data);
		listElement->data = NULL;
	}
	pthread_mutex_unlock(&listElement->busy);

	pthread_exit(NULL);
	return NULL;
}

static void
SC_handleRequest(PSCThreadData data)
{
	int Result = 0;
	PThreadListElement cur;

	for (cur = threadList; cur != NULL; cur = cur->next)
	{
		if (cur->data == NULL)
		{
			pthread_mutex_lock(&cur->busy);
			/* double check with lock held.... */
			if (cur->data != NULL)
			{
				pthread_mutex_unlock(&cur->busy);
				continue;
			}

			/* Wake up thread */
			cur->data = data;
			pthread_cond_broadcast(&cur->nodata);
			pthread_mutex_unlock(&cur->busy);
			return;
		}
	}

	cur = SC_xmalloc(&threadListHandle, sizeof(TThreadListElement));
	if (!cur)
		return;

	threadCount++;

	pthread_mutex_init(&cur->busy, NULL);
	pthread_cond_init(&cur->nodata, NULL);
	cur->data = data;

	Result = pthread_create(&cur->thread, NULL, (void *(*)(void *)) thread_function, cur);
	if (0 != Result)
	{
		error("[THREAD CREATE ERROR 0x%.8x]\n", Result);
		SC_xfree(&threadListHandle, cur);
		SC_destroyThreadData(data);
		data = NULL;
	}
	cur->next = threadList;
	threadList = cur;
}

static void *
queue_handler_function(void *data)
{
	PSCThreadData cur_data = NULL;
	while (1)
	{
		cur_data = SC_getNextInQueue();
		switch (cur_data->request)
		{
			case SC_ESTABLISH_CONTEXT:
			case SC_RELEASE_CONTEXT:
				{
					SC_deviceControl(cur_data);
					break;
				}
			default:
				{
					SC_handleRequest(cur_data);
					break;
				}
		}
	}
	return NULL;
}

static RD_NTSTATUS
thread_wrapper(RD_NTHANDLE handle, uint32 request, STREAM in, STREAM out)
{
	if (SC_addToQueue(handle, request, in, out))
		return RD_STATUS_PENDING | 0xC0000000;
	else
		return RD_STATUS_NO_SUCH_FILE;
}

DEVICE_FNS scard_fns = {
	scard_create,
	scard_close,
	scard_read,
	scard_write,
	thread_wrapper
};
#endif /* MAKE_PROTO */

void
scard_lock(int lock)
{
	if (!scard_mutex)
	{
		int i;

		scard_mutex =
			(pthread_mutex_t **) xmalloc(sizeof(pthread_mutex_t *) * SCARD_LOCK_LAST);

		for (i = 0; i < SCARD_LOCK_LAST; i++)
		{
			scard_mutex[i] = NULL;
		}
	}

	if (!scard_mutex[lock])
	{
		scard_mutex[lock] = (pthread_mutex_t *) xmalloc(sizeof(pthread_mutex_t));
		pthread_mutex_init(scard_mutex[lock], NULL);
	}

	pthread_mutex_lock(scard_mutex[lock]);
}

void
scard_unlock(int lock)
{
	pthread_mutex_unlock(scard_mutex[lock]);
}
