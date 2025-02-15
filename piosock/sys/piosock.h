/*
 * Main include file
 * This file contains various routines and globals
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#if !defined(PIOSOCK_H)
#define PIOSOCK_H
#include "public.h"

#define PIOSOCK_DRIVER_MEMORY_TAG (ULONG)'cosV'

#pragma pack (push)
#pragma pack (1)

typedef enum _PHYZIO_VSOCK_OP {
    PHYZIO_VSOCK_OP_INVALID = 0,

    /* Connect operations */
    PHYZIO_VSOCK_OP_REQUEST = 1,
    PHYZIO_VSOCK_OP_RESPONSE = 2,
    PHYZIO_VSOCK_OP_RST = 3,
    PHYZIO_VSOCK_OP_SHUTDOWN = 4,

    /* To send payload */
    PHYZIO_VSOCK_OP_RW = 5,

    /* Tell the peer our credit info */
    PHYZIO_VSOCK_OP_CREDIT_UPDATE = 6,
    /* Request the peer to send the credit info to us */
    PHYZIO_VSOCK_OP_CREDIT_REQUEST = 7,
}PHYZIO_VSOCK_OP;

/* PHYZIO_VSOCK_OP_SHUTDOWN flags values */
enum phyzio_vsock_shutdown {
    PHYZIO_VSOCK_SHUTDOWN_RCV = 1,
    PHYZIO_VSOCK_SHUTDOWN_SEND = 2,
    PHYZIO_VSOCK_SHUTDOWN_MASK = PHYZIO_VSOCK_SHUTDOWN_RCV | PHYZIO_VSOCK_SHUTDOWN_SEND,
};

typedef struct _PHYZIO_VSOCK_HDR {
    ULONG64 src_cid;
    ULONG64 dst_cid;
    ULONG32 src_port;
    ULONG32 dst_port;
    ULONG32 len;
    USHORT  type;
    USHORT  op;
    ULONG32 flags;
    ULONG32 buf_alloc;
    ULONG32 fwd_cnt;
}PHYZIO_VSOCK_HDR, *PPHYZIO_VSOCK_HDR;

typedef enum _PHYZIO_VSOCK_EVENT_ID {
    PHYZIO_VSOCK_EVENT_TRANSPORT_RESET = 0,
}PHYZIO_VSOCK_EVENT_ID;

typedef struct _PHYZIO_VSOCK_EVENT {
    ULONG32 id;
}PHYZIO_VSOCK_EVENT, *PPHYZIO_VSOCK_EVENT;

#pragma pack (pop)

typedef struct virtqueue PIOSOCK_VQ, *PPIOSOCK_VQ;
typedef struct PhyzIOBufferDescriptor PIOSOCK_SG_DESC, *PPIOSOCK_SG_DESC;

#define PIOSOCK_VQ_RX  0
#define PIOSOCK_VQ_TX  1
#define PIOSOCK_VQ_EVT 2
#define PIOSOCK_VQ_MAX 3

#define PHYZIO_VSOCK_DEFAULT_RX_BUF_SIZE	(1024 * 4)
#define PHYZIO_VSOCK_MAX_PKT_BUF_SIZE		(1024 * 64)

#define VSOCK_CLOSE_TIMEOUT                 SEC_TO_NANO(8)
#define VSOCK_DEFAULT_CONNECT_TIMEOUT       SEC_TO_NANO(2)
#define VSOCK_DEFAULT_BUFFER_SIZE           (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MAX_SIZE       (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MIN_SIZE       128

#define PHYZIO_VSOCK_MAX_EVENTS 8

#define LAST_RESERVED_PORT  1023
#define MAX_PORT_RETRIES    24
//////////////////////////////////////////////////////////////////////////
#define PIOSOCK_TIMER_TOLERANCE MSEC_TO_NANO(50)
typedef struct _PIOSOCK_TIMER
{
    WDFTIMER    Timer;
    LONGLONG    StartTime; //ticks when timer started
    LONGLONG    Timeout;   //timeout in 100ns
    ULONG       StartRefs;
}PIOSOCK_TIMER,*PPIOSOCK_TIMER;

#define PIOSOCK_DEVICE_NAME L"\\Device\\Piosock"

typedef struct _DEVICE_CONTEXT {

    PHYZIO_WDF_DRIVER           VDevice;

    WDFDEVICE                   ThisDevice;

    //Recv packets
    WDFQUEUE                    ReadQueue;

    WDFSPINLOCK                 RxLock;
    _Guarded_by_(RxLock) PPIOSOCK_VQ                 RxVq;
    PVOID                       RxPktVA;        //contiguous array of PIOSOCK_RX_PKT
    PHYSICAL_ADDRESS            RxPktPA;
    _Guarded_by_(RxLock) SINGLE_LIST_ENTRY           RxPktList;      //postponed requests
    ULONG                       RxPktNum;
    ULONG                       RxCbBuffersNum;
    WDFLOOKASIDE                RxCbBufferMemoryList;
    _Guarded_by_(RxLock) SINGLE_LIST_ENTRY           RxCbBuffers;    //list or Rx buffers

    //Send packets
    WDFQUEUE                    WriteQueue;

    WDFSPINLOCK                 TxLock;
    _Guarded_by_(TxLock) PPIOSOCK_VQ                 TxVq;
    _Guarded_by_(TxLock) PPHYZIO_DMA_MEMORY_SLICED   TxPktSliced;
    ULONG                       TxPktNum;       //Num of slices in TxPktSliced
    _Guarded_by_(TxLock) LONG                        TxQueuedReply;
    _Guarded_by_(TxLock) LIST_ENTRY                  TxList;
    _Guarded_by_(TxLock) PIOSOCK_TIMER               TxTimer;
    WDFLOOKASIDE                TxMemoryList;

    WDFLOOKASIDE                AcceptMemoryList;

    //Events
    PPIOSOCK_VQ                 EvtVq;
    PPHYZIO_VSOCK_EVENT         EvtVA;
    PHYSICAL_ADDRESS            EvtPA;
    ULONG                       EvtRstOccured;

    WDFSPINLOCK                 BoundLock;
    _Guarded_by_(BoundLock) WDFCOLLECTION               BoundList;

    WDFSPINLOCK                 ConnectedLock;
    _Guarded_by_(ConnectedLock) WDFCOLLECTION               ConnectedList;

    WDFWAITLOCK                 SelectLock;
    _Guarded_by_(SelectLock) LIST_ENTRY                  SelectList;
    _Interlocked_ volatile LONG               SelectInProgress;
    WDFWORKITEM                 SelectWorkitem;
    _Guarded_by_(SelectLock) PIOSOCK_TIMER               SelectTimer;

    WDFQUEUE                    IoCtlQueue;

    WDFINTERRUPT                WdfInterrupt;

    _Interlocked_ volatile LONG             SocketId; //for debug

    PHYZIO_VSOCK_CONFIG         Config;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

typedef enum _PIOSOCK_STATE
{
    PIOSOCK_STATE_CLOSE = 0,
    PIOSOCK_STATE_CONNECTING = 1,
    PIOSOCK_STATE_CONNECTED = 2,
    PIOSOCK_STATE_CLOSING = 3,
    PIOSOCK_STATE_LISTEN = 4,
}PIOSOCK_STATE;

#define SOCK_CONTROL    0x01
#define SOCK_BOUND      0x02
#define SOCK_LINGER     0x04
#define SOCK_NON_BLOCK  0x08
#define SOCK_LOOPBACK   0x10

typedef struct _PIOSOCK_ACCEPT_ENTRY
{
    LIST_ENTRY      ListEntry;
    WDFMEMORY       Memory;
    WDFFILEOBJECT   ConnectSocket;
    ULONG32         dst_cid;
    ULONG32         dst_port;
    ULONG32         peer_buf_alloc;
    ULONG32         peer_fwd_cnt;
}PIOSOCK_ACCEPT_ENTRY, *PPIOSOCK_ACCEPT_ENTRY;

typedef struct _SOCKET_CONTEXT {

    WDFFILEOBJECT   ThisSocket;

    _Interlocked_ volatile LONG             Flags;
    LONG            SocketId; //for debug

    PHYZIO_VSOCK_TYPE  type;

    ULONG32         dst_cid;
    ULONG32         src_port;
    ULONG32         dst_port;

    WDFSPINLOCK     StateLock;
    _Interlocked_ volatile PIOSOCK_STATE    State;
    LONGLONG        ConnectTimeout;
    ULONG           SendTimeout;
    ULONG           RecvTimeout;
    ULONG32         BufferMinSize;
    ULONG32         BufferMaxSize;
    ULONG32         PeerShutdown;
    ULONG32         Shutdown;

    WDFTIMER        ConnectTimer;
    _Guarded_by_(RxLock) LONGLONG        DueTime;
    KEVENT          CloseEvent;

    _Guarded_by_(StateLock) PKEVENT         EventObject;
    _Guarded_by_(StateLock) ULONG           EventsMask;
    _Guarded_by_(StateLock) ULONG           Events;
    _Guarded_by_(StateLock) NTSTATUS        EventsStatus[FD_MAX_EVENTS];

    WDFSPINLOCK     RxLock;         //accept list lock for listen socket
    _Guarded_by_(RxLock) LIST_ENTRY      RxCbList;
    _Guarded_by_(RxLock) PCHAR           RxCbReadPtr;    //read ptr in first CB
    _Guarded_by_(RxLock) ULONG           RxCbReadLen;    //remaining bytes in first CB
    _Guarded_by_(RxLock) volatile ULONG           RxBytes;        //used bytes in rx buffer
    _Guarded_by_(RxLock) ULONG           RxBuffers;      //used rx buffers (for debug)

    WDFQUEUE        ReadQueue;
    _Guarded_by_(RxLock) PCHAR           ReadRequestPtr;
    _Guarded_by_(RxLock) ULONG           ReadRequestFree;
    _Guarded_by_(RxLock) ULONG           ReadRequestLength;
    _Guarded_by_(RxLock) ULONG           ReadRequestFlags;
    _Guarded_by_(RxLock) PIOSOCK_TIMER   ReadTimer;

    _Guarded_by_(RxLock) WDFREQUEST      PendedRequest;

    _Guarded_by_(RxLock) LIST_ENTRY      AcceptList;
    LONG            Backlog;
    _Interlocked_ volatile LONG   AcceptPended;

    ULONG32         buf_alloc;
    _Guarded_by_(RxLock) ULONG32         fwd_cnt;
    ULONG32         last_fwd_cnt;

    _Guarded_by_(StateLock) ULONG32         peer_buf_alloc;
    _Guarded_by_(StateLock) ULONG32         peer_fwd_cnt;
    _Guarded_by_(StateLock) ULONG32         tx_cnt;

    USHORT          LingerTime;
    WDFFILEOBJECT   LoopbackSocket;

    volatile LONG   SelectRefs[FDSET_MAX];
} SOCKET_CONTEXT, *PSOCKET_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOCKET_CONTEXT, GetSocketContext);

#define PIOSockIsFlag(s,f) ((s)->Flags & (f))
#define PIOSockSetFlag(s,f) (InterlockedOr(&(s)->Flags, (f)) & (f))
#define PIOSockResetFlag(s,f) (InterlockedAnd(&(s)->Flags, ~(f)) & (f))

#define GetSocketContextFromRequest(r) GetSocketContext(WdfRequestGetFileObject((r)))

#define GetDeviceContextFromRequest(r) GetDeviceContext(WdfFileObjectGetDevice(WdfRequestGetFileObject((r))))

#define GetDeviceContextFromSocket(s) GetDeviceContext(WdfFileObjectGetDevice((s)->ThisSocket))

#define IsControlRequest(r) PIOSockIsFlag(GetSocketContextFromRequest(r), SOCK_CONTROL)

#define IsLoopbackSocket(s) (PIOSockIsFlag(s,SOCK_LOOPBACK))

//////////////////////////////////////////////////////////////////////////
//Device functions

EVT_WDF_DRIVER_DEVICE_ADD   PIOSockEvtDeviceAdd;

NTSTATUS
PIOSockInterruptInit(
    IN WDFDEVICE hDevice
);

//////////////////////////////////////////////////////////////////////////
//Socket functions
EVT_WDF_DEVICE_FILE_CREATE  PIOSockCreateStub;
EVT_WDF_FILE_CLOSE          PIOSockClose;

NTSTATUS
PIOSockDeviceControl(
    IN WDFREQUEST Request,
    IN ULONG      IoControlCode,
    IN OUT size_t *pLength
);

WDFFILEOBJECT
PIOSockGetSocketFromHandle(
    IN PDEVICE_CONTEXT pContext,
    IN ULONGLONG       uSocket,
    IN BOOLEAN         bIs32BitProcess
);

VOID
PIOSockHandleTransportReset(
    IN PDEVICE_CONTEXT pContext
);

NTSTATUS
PIOSockBoundListInit(
    IN WDFDEVICE hDevice
);

NTSTATUS
PIOSockBoundAdd(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32         svm_port
);

PSOCKET_CONTEXT
PIOSockBoundFindByPort(
    IN PDEVICE_CONTEXT pContext,
    IN ULONG32         ulSrcPort
);

PSOCKET_CONTEXT
PIOSockBoundFindByPortUnlocked(
    IN PDEVICE_CONTEXT pContext,
    IN ULONG32         ulSrcPort
);

PSOCKET_CONTEXT
PIOSockBoundFindByFile(
    IN PDEVICE_CONTEXT pContext,
    IN PFILE_OBJECT pFileObject
);

NTSTATUS
PIOSockConnectedListInit(
    IN WDFDEVICE hDevice
);

PSOCKET_CONTEXT
PIOSockConnectedFindByRxPkt(
    IN PDEVICE_CONTEXT      pContext,
    IN PPHYZIO_VSOCK_HDR    pPkt
);

_Requires_lock_held_(pSocket->StateLock)
PIOSOCK_STATE
PIOSockStateSet(
    IN PSOCKET_CONTEXT pSocket,
    IN PIOSOCK_STATE   NewState
);

_Requires_lock_not_held_(pSocket->StateLock)
PIOSOCK_STATE
PIOSockStateSetLocked(
    IN PSOCKET_CONTEXT pSocket,
    IN PIOSOCK_STATE   NewState
);

#define PIOSockStateGet(s) ((s)->State)

_Requires_lock_not_held_(pSocket->StateLock)
BOOLEAN
PIOSockShutdownFromPeer(
    PSOCKET_CONTEXT pSocket,
    ULONG uFlags
);

VOID
PIOSockDoClose(
    PSOCKET_CONTEXT pSocket
);

__inline
BOOLEAN
PIOSockIsDone(
    PSOCKET_CONTEXT pSocket
)
{
    return !!KeReadStateEvent(&pSocket->CloseEvent);
}

_Requires_lock_held_(pSocket->RxLock)
NTSTATUS
PIOSockPendedRequestSet(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request
);

_Requires_lock_not_held_(pSocket->RxLock)
NTSTATUS
PIOSockPendedRequestSetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request
);

_Requires_lock_held_(pSocket->RxLock)
NTSTATUS
PIOSockPendedRequestGet(
    IN  PSOCKET_CONTEXT pSocket,
    OUT WDFREQUEST      *Request
);

_Requires_lock_not_held_(pSocket->RxLock)
NTSTATUS
PIOSockPendedRequestGetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    OUT WDFREQUEST      *Request
);

NTSTATUS
PIOSockAcceptInitSocket(
	PSOCKET_CONTEXT pAcceptSocket,
	PSOCKET_CONTEXT pListenSocket
);

_Requires_lock_not_held_(pListenSocket->RxLock)
NTSTATUS
PIOSockAcceptEnqueuePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PPHYZIO_VSOCK_HDR    pPkt
);

_Requires_lock_not_held_(pListenSocket->RxLock)
VOID
PIOSockAcceptRemovePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PPHYZIO_VSOCK_HDR    pPkt
);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
PIOSockSelectRun(
    IN PSOCKET_CONTEXT pSocket
);

/*
 * WinSock 2 extension -- bit values and indices for FD_XXX network events
 */
#define FD_READ_BIT      0
#define FD_READ          (1 << FD_READ_BIT)
#define FD_WRITE_BIT     1
#define FD_WRITE         (1 << FD_WRITE_BIT)
#define FD_OOB_BIT       2
#define FD_OOB           (1 << FD_OOB_BIT)
#define FD_ACCEPT_BIT    3
#define FD_ACCEPT        (1 << FD_ACCEPT_BIT)
#define FD_CONNECT_BIT   4
#define FD_CONNECT       (1 << FD_CONNECT_BIT)
#define FD_CLOSE_BIT     5
#define FD_CLOSE         (1 << FD_CLOSE_BIT)
#define FD_QOS_BIT       6
#define FD_QOS           (1 << FD_QOS_BIT)
#define FD_GROUP_QOS_BIT 7
#define FD_GROUP_QOS     (1 << FD_GROUP_QOS_BIT)
#define FD_ROUTING_INTERFACE_CHANGE_BIT 8
#define FD_ROUTING_INTERFACE_CHANGE     (1 << FD_ROUTING_INTERFACE_CHANGE_BIT)
#define FD_ADDRESS_LIST_CHANGE_BIT 9
#define FD_ADDRESS_LIST_CHANGE     (1 << FD_ADDRESS_LIST_CHANGE_BIT)

_Requires_lock_not_held_(pSocket->StateLock)
VOID
PIOSockEventSetBit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uSetBit,
    IN NTSTATUS Status
);

_Requires_lock_not_held_(pSocket->StateLock)
VOID
PIOSockEventSetBitLocked(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uSetBit,
    IN NTSTATUS Status
);

_Requires_lock_not_held_(pSocket->StateLock)
VOID
PIOSockEventClearBit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uClearBit
);

//////////////////////////////////////////////////////////////////////////
//Tx functions

NTSTATUS
PIOSockWriteQueueInit(
    IN WDFDEVICE hDevice
);

NTSTATUS
PIOSockTxVqInit(
    IN PDEVICE_CONTEXT pContext
);

VOID
PIOSockTxVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pContext->TxLock)
VOID
PIOSockTxVqProcess(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pSocket->StateLock)
NTSTATUS
PIOSockStateValidate(
    PSOCKET_CONTEXT pSocket,
    BOOLEAN         bTx
);

_Requires_lock_not_held_(pContext->TxLock)
NTSTATUS
PIOSockTxEnqueue(
    IN PSOCKET_CONTEXT  pSocket,
    IN PHYZIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN BOOLEAN          Reply,
    IN WDFREQUEST       Request OPTIONAL
);

#define PIOSockSendCreditUpdate(s) PIOSockTxEnqueue(s, PHYZIO_VSOCK_OP_CREDIT_UPDATE, 0, FALSE, WDF_NO_HANDLE)

#define PIOSockSendConnect(s) PIOSockTxEnqueue(s, PHYZIO_VSOCK_OP_REQUEST, 0, FALSE, WDF_NO_HANDLE)

#define PIOSockSendShutdown(s, f) PIOSockTxEnqueue(s, PHYZIO_VSOCK_OP_SHUTDOWN, f, FALSE, WDF_NO_HANDLE)

#define PIOSockSendWrite(s, rq) PIOSockTxEnqueue(s, PHYZIO_VSOCK_OP_RW, 0, FALSE, rq)

#define PIOSockSendReset(s, r) PIOSockTxEnqueue(s, PHYZIO_VSOCK_OP_RST, 0, r, WDF_NO_HANDLE)

#define PIOSockSendResponse(s) PIOSockTxEnqueue(s, PHYZIO_VSOCK_OP_RESPONSE, 0, TRUE, WDF_NO_HANDLE)

_Requires_lock_not_held_(pContext->TxLock)
NTSTATUS
PIOSockSendResetNoSock(
    IN PDEVICE_CONTEXT pContext,
    IN PPHYZIO_VSOCK_HDR pHeader
);

_Requires_lock_not_held_(pSocket->StateLock)
__inline
ULONG32
PIOSockTxGetCredit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    ULONG32 uRet;

    WdfSpinLockAcquire(pSocket->StateLock);
    uRet = pSocket->peer_buf_alloc - (pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    if (uRet > uCredit)
        uRet = uCredit;
    pSocket->tx_cnt += uRet;
    WdfSpinLockRelease(pSocket->StateLock);

    return uRet;
}

_Requires_lock_not_held_(pSocket->StateLock)
__inline
VOID
PIOSockTxPutCredit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->tx_cnt -= uCredit;
    WdfSpinLockRelease(pSocket->StateLock);
}

_Requires_lock_held_(pSocket->StateLock)
__inline
LONG
PIOSockTxHasSpace(
    IN PSOCKET_CONTEXT pSocket
)
{
    LONG lBytes = (LONG)pSocket->peer_buf_alloc - (pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    if (lBytes < 0)
        lBytes = 0;
    return lBytes;
}

_Requires_lock_not_held_(pSocket->StateLock)
__inline
LONG
PIOSockTxSpaceUpdate(
    IN PSOCKET_CONTEXT pSocket,
    IN PPHYZIO_VSOCK_HDR pPkt
)
{
    LONG uSpace;

    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->peer_buf_alloc = pPkt->buf_alloc;
    pSocket->peer_fwd_cnt = pPkt->fwd_cnt;
    uSpace = PIOSockTxHasSpace(pSocket);
    WdfSpinLockRelease(pSocket->StateLock);

    return uSpace;
}

__inline
BOOLEAN
PIOSockTxMoreReplies(
    IN PDEVICE_CONTEXT  pContext
)
{
    return pContext->TxQueuedReply < (LONG)pContext->RxPktNum;
}

_Requires_lock_not_held_(pContext->TxLock)
VOID
PIOSockTxCancel(
    PDEVICE_CONTEXT pContext,
    PSOCKET_CONTEXT pSocket,
    NTSTATUS        Status
);

//////////////////////////////////////////////////////////////////////////
//Rx functions

NTSTATUS
PIOSockRxVqInit(
    IN PDEVICE_CONTEXT pContext
);

VOID
PIOSockRxVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pSocket->StateLock)
__inline
VOID
PIOSockRxIncTxPkt(
    IN PSOCKET_CONTEXT pSocket,
    IN OUT PPHYZIO_VSOCK_HDR pPkt
)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->last_fwd_cnt = pSocket->fwd_cnt;
    pPkt->fwd_cnt = pSocket->fwd_cnt;
    pPkt->buf_alloc = pSocket->buf_alloc;
    WdfSpinLockRelease(pSocket->StateLock);
}

_Requires_lock_not_held_(pContext->RxLock)
VOID
PIOSockRxVqProcess(
    IN PDEVICE_CONTEXT pContext
);

NTSTATUS
PIOSockRxRequestEnqueueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request,
    IN ULONG            Length
);

__inline
ULONG
PIOSockRxHasData(
    IN PSOCKET_CONTEXT pSocket
)
{
    return pSocket->RxBytes;
}

NTSTATUS
PIOSockReadQueueInit(
    IN WDFDEVICE hDevice
);

NTSTATUS
PIOSockReadSocketQueueInit(
    IN PSOCKET_CONTEXT pSocket
);

_Requires_lock_not_held_(pSocket->RxLock)
VOID
PIOSockReadDequeueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       ReadRequest OPTIONAL
);

_Requires_lock_not_held_(pSocket->RxLock)
VOID
PIOSockReadCleanupCb(
    IN PSOCKET_CONTEXT pSocket
);

NTSTATUS
PIOSockReadWithFlags(
    IN WDFREQUEST Request
);

//////////////////////////////////////////////////////////////////////////
//Event functions
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PIOSockEvtVqInit(
    IN PDEVICE_CONTEXT pContext
);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
PIOSockEvtVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
PIOSockEvtVqProcess(
    IN PDEVICE_CONTEXT pContext
);

//////////////////////////////////////////////////////////////////////////
BOOLEAN
PIOSockLoopbackAcceptDequeue(
    IN PSOCKET_CONTEXT pAcceptSocket,
    IN PPIOSOCK_ACCEPT_ENTRY pAcceptEntry
);

NTSTATUS
PIOSockLoopbackTxEnqueue(
    IN PSOCKET_CONTEXT  pSocket,
    IN PHYZIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN WDFREQUEST       Request OPTIONAL,
    IN ULONG            Length OPTIONAL
);

//////////////////////////////////////////////////////////////////////////
__inline
NTSTATUS
PIOSockTimerCreate(
    IN PPIOSOCK_TIMER   pTimer,
    IN WDFOBJECT        ParentObject,
    IN PFN_WDF_TIMER    EvtTimerFunc
)
{
    WDF_OBJECT_ATTRIBUTES   Attributes;
    WDF_TIMER_CONFIG        timerConfig;

    pTimer->Timeout = 0;
    pTimer->StartTime = 0;
    pTimer->StartRefs = 0;

    WDF_TIMER_CONFIG_INIT(&timerConfig, EvtTimerFunc);

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = ParentObject;

    return WdfTimerCreate(&timerConfig, &Attributes, &pTimer->Timer);
}

VOID
PIOSockTimerStart(
    IN PPIOSOCK_TIMER   pTimer,
    IN LONGLONG         Timeout
);

__inline
VOID
PIOSockTimerSet(
    IN PPIOSOCK_TIMER   pTimer,
    IN LONGLONG         Timeout
)
{
    LARGE_INTEGER liTicks;

    if (!Timeout || Timeout == LONGLONG_MAX)
    {
        ASSERT(!pTimer->StartRefs);
        pTimer->StartTime = 0;
        pTimer->Timeout = 0;
        return;
    }

    ASSERT(Timeout > PIOSOCK_TIMER_TOLERANCE);
    if (Timeout <= PIOSOCK_TIMER_TOLERANCE)
        Timeout = PIOSOCK_TIMER_TOLERANCE + 1;

    KeQueryTickCount(&liTicks);

    pTimer->StartTime = liTicks.QuadPart;
    pTimer->Timeout = Timeout;
    WdfTimerStart(pTimer->Timer, -Timeout);
}

__inline
VOID
PIOSockTimerCancel(
    IN PPIOSOCK_TIMER pTimer
)
{
    WdfTimerStop(pTimer->Timer, FALSE);
    pTimer->Timeout = 0;
    pTimer->StartTime = 0;
}

__inline
VOID
PIOSockTimerDeref(
    IN PPIOSOCK_TIMER   pTimer,
    IN BOOLEAN          bStop
)
{
    ASSERT(pTimer->StartRefs);
    if (--pTimer->StartRefs == 0 && bStop)
        PIOSockTimerCancel(pTimer);
}

__inline
LONGLONG
PIOSockTimerPassed(
    IN PPIOSOCK_TIMER pTimer
)
{
    LARGE_INTEGER liTicks;

    KeQueryTickCount(&liTicks);

    return (liTicks.QuadPart - pTimer->StartTime) * KeQueryTimeIncrement();
}
//////////////////////////////////////////////////////////////////////////

#endif /* PIOSOCK_H */
