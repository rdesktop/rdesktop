/*
   rdesktop: A Remote Desktop Protocol client.
   Smart Card support
   Copyright (C) Alexi Volkov <alexi@myrealbox.com> 2006

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

#include <pthread.h>
#include "proto.h"

/*************************************************************************/
/* these are the additional types needed to split out 64-vs-32-bit APIs  */
/*                                                                       */

/* The point of all of this is to avoid patching the existing smartcard
 * infrastructure (PC/SC Lite, libmusclecard+libmusclepkcs11 or CoolKey, any
 * other apps linking against any of these) because the need for patches
 * spreads without limit. The alternative is to patch the heck out of rdesktop,
 * which is already being done anyway.
 *
 * - jared.jennings@eglin.af.mil, 2 Aug 2006
 */

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

/* A DWORD when dealing with the smartcard stuff. Could be 32 bits or 64. */
typedef DWORD MYPCSC_DWORD;
/* A DWORD when talking to the server. Must be exactly 32 bits all the time.*/
typedef uint32_t SERVER_DWORD;

typedef SCARDCONTEXT MYPCSC_SCARDCONTEXT;
typedef SCARDHANDLE MYPCSC_SCARDHANDLE;
typedef uint32_t SERVER_SCARDCONTEXT;
typedef uint32_t SERVER_SCARDHANDLE;

typedef SCARD_READERSTATE MYPCSC_SCARD_READERSTATE_A, *MYPCSC_LPSCARD_READERSTATE_A;

typedef struct
{
	const char *szReader;
	void *pvUserData;
	SERVER_DWORD dwCurrentState;
	SERVER_DWORD dwEventState;
	SERVER_DWORD cbAtr;
	unsigned char rgbAtr[36];
}
SERVER_SCARD_READERSTATE_A;

typedef SERVER_SCARD_READERSTATE_A *SERVER_LPSCARD_READERSTATE_A;

#define SERVER_SCARDSTATESIZE              (sizeof(SERVER_SCARD_READERSTATE_A) - sizeof(const char *) - sizeof(void *))
#define MYPCSC_SCARDSTATESIZE              (sizeof(MYPCSC_SCARD_READERSTATE_A) - sizeof(const char *) - sizeof(void *))

typedef struct _SERVER_SCARD_IO_REQUEST
{
	SERVER_DWORD dwProtocol;	/* Protocol identifier */
	SERVER_DWORD cbPciLength;	/* Protocol Control Inf Length */
}
SERVER_SCARD_IO_REQUEST, *SERVER_LPSCARD_IO_REQUEST;

typedef SCARD_IO_REQUEST MYPCSC_SCARD_IO_REQUEST;
typedef LPSCARD_IO_REQUEST MYPCSC_LPSCARD_IO_REQUEST;


/*                                                                       */
/*                                                                       */
/*************************************************************************/


#define	SC_TRUE                     1
#define SC_FALSE                    0

#define SC_ESTABLISH_CONTEXT        0x00090014	/* EstablishContext */
#define SC_RELEASE_CONTEXT          0x00090018	/* ReleaseContext */
#define SC_IS_VALID_CONTEXT         0x0009001C	/* IsValidContext */
#define SC_LIST_READER_GROUPS       0x00090020	/* ListReaderGroups */
#define SC_LIST_READERS             0x00090028	/* ListReadersA */
#define SC_INTRODUCE_READER_GROUP   0x00090050	/* IntroduceReaderGroup */
#define SC_FORGET_READER_GROUP      0x00090058	/* ForgetReader */
#define SC_INTRODUCE_READER         0x00090060	/* IntroduceReader */
#define SC_FORGET_READER            0x00090068	/* IntroduceReader */
#define SC_ADD_READER_TO_GROUP      0x00090070	/* AddReaderToGroup */
#define SC_REMOVE_READER_FROM_GROUP 0x00090078	/* RemoveReaderFromGroup */
#define SC_CONNECT                  0x000900AC	/* ConnectA */
#define SC_RECONNECT                0x000900B4	/* Reconnect */
#define SC_DISCONNECT               0x000900B8	/* Disconnect */
#define SC_GET_STATUS_CHANGE        0x000900A0	/* GetStatusChangeA */
#define SC_CANCEL                   0x000900A8	/* Cancel */
#define SC_BEGIN_TRANSACTION        0x000900BC	/* BeginTransaction */
#define SC_END_TRANSACTION          0x000900C0	/* EndTransaction */
#define SC_STATE		    0x000900C4	/* State */
#define SC_STATUS                   0x000900C8	/* StatusA */
#define SC_TRANSMIT                 0x000900D0	/* Transmit */
#define SC_CONTROL                  0x000900D4	/* Control */
#define SC_GETATTRIB                0x000900D8	/* GetAttrib */
#define SC_SETATTRIB                0x000900DC	/* SetAttrib */
#define SC_ACCESS_STARTED_EVENT	    0x000900E0	/* SCardAccessStartedEvent */
#define SC_LOCATE_CARDS_BY_ATR      0x000900E8	/* LocateCardsByATR */

/* #define INPUT_LINKED                0x00020000 */
#define INPUT_LINKED                0xFFFFFFFF

#define SC_THREAD_FUNCTION(f)       void *(*f)(void *)

extern RDPDR_DEVICE g_rdpdr_device[];

typedef struct _MEM_HANDLE
{
	struct _MEM_HANDLE *prevHandle;
	struct _MEM_HANDLE *nextHandle;
	int dataSize;
} MEM_HANDLE, *PMEM_HANDLE;

typedef struct _SCARD_ATRMASK_L
{
	unsigned int cbAtr;
	unsigned char rgbAtr[36];
	unsigned char rgbMask[36];
} SCARD_ATRMASK_L, *PSCARD_ATRMASK_L, *LPSCARD_ATRMASK_L;

typedef struct _TSCNameMapRec
{
	char alias[128];
	char name[128];
	char vendor[128];
} TSCNameMapRec, *PSCNameMapRec;

typedef struct _TSCHCardRec
{
	DWORD hCard;
	char *vendor;
	struct _TSCHCardRec *next;
	struct _TSCHCardRec *prev;
} TSCHCardRec, *PSCHCardRec;

typedef struct _TSCThreadData
{
	uint32 device;
	uint32 id;
	uint32 epoch;
	RD_NTHANDLE handle;
	uint32 request;
	STREAM in;
	STREAM out;
	PMEM_HANDLE memHandle;
	struct _TSCThreadData *next;
} TSCThreadData, *PSCThreadData;

typedef struct _TThreadListElement
{
	pthread_t thread;
	pthread_mutex_t busy;
	pthread_cond_t nodata;
	PSCThreadData data;
	struct _TThreadListElement *next;
} TThreadListElement, *PThreadListElement;
