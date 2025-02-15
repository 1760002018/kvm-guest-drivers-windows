/*
 * Socket object functions
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

#include "precomp.h"
#include "piosock.h"

#if defined(EVENT_TRACING)
#include "Socket.tmh"
#endif

NTSTATUS
PIOSockBind(
    IN WDFREQUEST   Request
);

NTSTATUS
PIOSockConnect(
    IN WDFREQUEST   Request
);

NTSTATUS
PIOSockShutdown(
    IN WDFREQUEST   Request
);

NTSTATUS
PIOSockListen(
    IN WDFREQUEST   Request
);

NTSTATUS
PIOSockEnumNetEvents(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
PIOSockEventSelect(
    IN WDFREQUEST   Request
);

NTSTATUS
PIOSockGetPeerName(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
PIOSockGetSockName(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
PIOSockGetSockOpt(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
PIOSockSetSockOpt(
    IN WDFREQUEST   Request
);

NTSTATUS
PIOSockIoctl(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

typedef union _PIOSOCK_PENDED_CONTEXT {
    WDFFILEOBJECT ParentSocket;
}PIOSOCK_PENDED_CONTEXT, *PPIOSOCK_PENDED_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIOSOCK_PENDED_CONTEXT, GetRequestPendedContext);

NTSTATUS
PIOSockAccept(
    IN HANDLE       hListenSocket,
    IN WDFREQUEST   Request
);

NTSTATUS
PIOSockCreate(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
);

EVT_WDF_REQUEST_CANCEL PIOSockPendedRequestCancel;
EVT_WDF_TIMER          PIOSockConnectTimerFunc;


#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PIOSockBoundListInit)
#pragma alloc_text (PAGE, PIOSockConnectedListInit)

#pragma alloc_text (PAGE, PIOSockCreateStub)
#pragma alloc_text (PAGE, PIOSockClose)

#pragma alloc_text (PAGE, PIOSockBind)
#pragma alloc_text (PAGE, PIOSockConnect)
#pragma alloc_text (PAGE, PIOSockShutdown)
#pragma alloc_text (PAGE, PIOSockListen)
#pragma alloc_text (PAGE, PIOSockEnumNetEvents)
#pragma alloc_text (PAGE, PIOSockEventSelect)
#pragma alloc_text (PAGE, PIOSockGetPeerName)
#pragma alloc_text (PAGE, PIOSockGetSockName)
#pragma alloc_text (PAGE, PIOSockGetSockOpt)
#pragma alloc_text (PAGE, PIOSockSetSockOpt)
#pragma alloc_text (PAGE, PIOSockIoctl)
#pragma alloc_text (PAGE, PIOSockAccept)
#pragma alloc_text (PAGE, PIOSockCreate)

#pragma alloc_text (PAGE, PIOSockDeviceControl)
#endif

//////////////////////////////////////////////////////////////////////////
NTSTATUS
PIOSockBoundListInit(
    IN WDFDEVICE hDevice
)
{
    NTSTATUS                status;
    PDEVICE_CONTEXT         pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES   collectionAttributes;

    PAGED_CODE();

    // Create collection for socket objects
    WDF_OBJECT_ATTRIBUTES_INIT(&collectionAttributes);
    collectionAttributes.ParentObject = hDevice;

    status = WdfCollectionCreate(&collectionAttributes, &pContext->BoundList);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfCollectionCreate failed - 0x%x\n", status);
    }
    else
    {
        WDF_OBJECT_ATTRIBUTES   lockAttributes;

        WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
        lockAttributes.ParentObject = hDevice;
        status = WdfSpinLockCreate(&lockAttributes, &pContext->BoundLock);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfSpinLockCreate failed - 0x%x\n", status);
        }
    }
    return status;
}

NTSTATUS
PIOSockBoundAdd(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32         svm_port
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    static ULONG32  uSvmPort;
    ULONG           uSeed = (ULONG)(ULONG_PTR)pSocket;
    NTSTATUS        status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (svm_port == VMADDR_PORT_ANY)
    {
        BOOLEAN bFound = FALSE;
        ULONG i;

        for (i = 0; i < MAX_PORT_RETRIES; ++i)
        {
            if (!uSvmPort)
                uSvmPort = RtlRandomEx((PULONG)&uSeed);

            if (uSvmPort == VMADDR_PORT_ANY)
            {
                uSvmPort = 0;
                continue;
            }

            if (uSvmPort <= LAST_RESERVED_PORT)
                uSvmPort += LAST_RESERVED_PORT + 1;

            svm_port = uSvmPort++;

            WdfSpinLockAcquire(pContext->BoundLock);
            if (!PIOSockBoundFindByPortUnlocked(pContext, svm_port))
            {
                bFound = TRUE;
                pSocket->src_port = svm_port;
                status = WdfCollectionAdd(pContext->BoundList, pSocket->ThisSocket);
            }
            WdfSpinLockRelease(pContext->BoundLock);

            if (bFound)
                break;
        }

        if (!bFound)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "No ports available\n");
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        WdfSpinLockAcquire(pContext->BoundLock);
        if (PIOSockBoundFindByPortUnlocked(pContext, svm_port))
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Local port %u is already in use\n", svm_port);
            status = STATUS_ADDRESS_ALREADY_ASSOCIATED;
        }
        else
        {
            pSocket->src_port = svm_port;
            status = WdfCollectionAdd(pContext->BoundList, pSocket->ThisSocket);
        }
        WdfSpinLockRelease(pContext->BoundLock);
    }

    if (NT_SUCCESS(status))
        PIOSockSetFlag(pSocket, SOCK_BOUND);

    return status;
}

__inline
VOID
PIOSockBoundRemove(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pContext->BoundLock);
    if (PIOSockResetFlag(pSocket, SOCK_BOUND))
    {
        WdfCollectionRemove(pContext->BoundList, pSocket->ThisSocket);
    }
    WdfSpinLockRelease(pContext->BoundLock);
}

typedef
BOOLEAN
(*PSOCKET_CALLBACK)(
    IN PSOCKET_CONTEXT pSocket,
    IN PVOID pCallbackContext
    );

PSOCKET_CONTEXT
PIOSockBoundEnumUnlocked(
    IN PDEVICE_CONTEXT  pContext,
    IN PSOCKET_CALLBACK pEnumCallback,
    IN PVOID            pCallbackContext
)
{
    PSOCKET_CONTEXT pSocket = NULL;
    ULONG i, ItemCount;

    ItemCount = WdfCollectionGetCount(pContext->BoundList);
    for (i = 0; i < ItemCount; ++i)
    {
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(WdfCollectionGetItem(pContext->BoundList, i));

        if (pEnumCallback(pCurrentSocket, pCallbackContext))
        {
            pSocket = pCurrentSocket;
            break;
        }
    }

    return pSocket;
}

PSOCKET_CONTEXT
PIOSockBoundEnum(
    IN PDEVICE_CONTEXT  pContext,
    IN PSOCKET_CALLBACK pEnumCallback,
    IN PVOID            pCallbackContext
)
{
    PSOCKET_CONTEXT pSocket;

    WdfSpinLockAcquire(pContext->BoundLock);
    pSocket = PIOSockBoundEnumUnlocked(pContext, pEnumCallback, pCallbackContext);
    WdfSpinLockRelease(pContext->BoundLock);

    return pSocket;
}

static
BOOLEAN
PIOSockBoundFindByPortCallback(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVOID            pCallbackContext
)
{
    return pSocket->src_port == (ULONG32)(ULONG_PTR)pCallbackContext;
}

PSOCKET_CONTEXT
PIOSockBoundFindByPort(
    IN PDEVICE_CONTEXT pContext,
    IN ULONG32         ulSrcPort
)
{
    return PIOSockBoundEnum(pContext, PIOSockBoundFindByPortCallback, (PVOID)ulSrcPort);
}

PSOCKET_CONTEXT
PIOSockBoundFindByPortUnlocked(
    IN PDEVICE_CONTEXT pContext,
    IN ULONG32         ulSrcPort
)
{
    return PIOSockBoundEnumUnlocked(pContext, PIOSockBoundFindByPortCallback, (PVOID)ulSrcPort);
}

static
BOOLEAN
PIOSockFindByFileCallback(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVOID            pCallbackContext
)
{
    return WdfFileObjectWdmGetFileObject(pSocket->ThisSocket) ==
        (PFILE_OBJECT)pCallbackContext;
}

PSOCKET_CONTEXT
PIOSockBoundFindByFile(
    IN PDEVICE_CONTEXT pContext,
    IN PFILE_OBJECT pFileObject
)
{
    return PIOSockBoundEnum(pContext, PIOSockFindByFileCallback, pFileObject);
}

NTSTATUS
PIOSockConnectedListInit(
    IN WDFDEVICE hDevice
)
{
    NTSTATUS                status;
    PDEVICE_CONTEXT         pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES   collectionAttributes;

    PAGED_CODE();

    // Create collection for socket objects
    WDF_OBJECT_ATTRIBUTES_INIT(&collectionAttributes);
    collectionAttributes.ParentObject = hDevice;

    status = WdfCollectionCreate(&collectionAttributes, &pContext->ConnectedList);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfCollectionCreate failed - 0x%x\n", status);
    }
    else
    {
        WDF_OBJECT_ATTRIBUTES   lockAttributes;

        WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
        lockAttributes.ParentObject = hDevice;
        status = WdfSpinLockCreate(&lockAttributes, &pContext->ConnectedLock);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfSpinLockCreate failed - 0x%x\n", status);
        }
    }

    return status;
}

__inline
NTSTATUS
PIOSockConnectedAdd(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));
    NTSTATUS status;

    WdfSpinLockAcquire(pContext->ConnectedLock);
    status = WdfCollectionAdd(pContext->ConnectedList, pSocket->ThisSocket);
    WdfSpinLockRelease(pContext->ConnectedLock);
    return status;
}

__inline
VOID
PIOSockConnectedRemove(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));

    WdfSpinLockAcquire(pContext->ConnectedLock);
    WdfCollectionRemove(pContext->ConnectedList, pSocket->ThisSocket);
    WdfSpinLockRelease(pContext->ConnectedLock);
}

PSOCKET_CONTEXT
PIOSockConnectedEnum(
    IN PDEVICE_CONTEXT  pContext,
    IN PSOCKET_CALLBACK pEnumCallback,
    IN PVOID            pCallbackContext
)
{
    PSOCKET_CONTEXT pSocket = NULL;
    ULONG i, ItemCount;

    WdfSpinLockAcquire(pContext->ConnectedLock);
    ItemCount = WdfCollectionGetCount(pContext->ConnectedList);
    for (i = 0; i < ItemCount; ++i)
    {
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(WdfCollectionGetItem(pContext->ConnectedList, i));

        if (pEnumCallback(pCurrentSocket, pCallbackContext))
        {
            pSocket = pCurrentSocket;
            break;
        }
    }
    WdfSpinLockRelease(pContext->ConnectedLock);

    return pSocket;
}

static
BOOLEAN
PIOSockConnectedFindByRxPktCallback(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVOID            pCallbackContext
)
{
    PPHYZIO_VSOCK_HDR    pPkt = (PPHYZIO_VSOCK_HDR)pCallbackContext;
    return (pPkt->src_cid == pSocket->dst_cid &&
        pPkt->src_port == pSocket->dst_port &&
        pPkt->dst_port == pSocket->src_port);
}

PSOCKET_CONTEXT
PIOSockConnectedFindByRxPkt(
    IN PDEVICE_CONTEXT      pContext,
    IN PPHYZIO_VSOCK_HDR    pPkt
)
{
    return PIOSockConnectedEnum(pContext, PIOSockConnectedFindByRxPktCallback, pPkt);
}

PSOCKET_CONTEXT
PIOSockConnectedFindByFile(
    IN PDEVICE_CONTEXT  pContext,
    IN PFILE_OBJECT     pFileObject
)
{
    return PIOSockConnectedEnum(pContext, PIOSockFindByFileCallback, pFileObject);
}

PIOSOCK_STATE
PIOSockStateSet(
    IN PSOCKET_CONTEXT pSocket,
    IN PIOSOCK_STATE   NewState
)
{
    PIOSOCK_STATE PrevState;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Socket %d, %!State! --> %!State!\n",
        pSocket->SocketId, pSocket->State, NewState);

    PrevState = InterlockedExchange((PLONG)&pSocket->State, NewState);

    //add/remove socket to/from connected list
    if (PrevState != NewState && !IsLoopbackSocket(pSocket))
    {
        if (NewState == PIOSOCK_STATE_CLOSE &&
            (PrevState == PIOSOCK_STATE_CONNECTED ||
            PrevState == PIOSOCK_STATE_CLOSING))
        {
            PIOSockConnectedRemove(pSocket);
        }
        else if (NewState == PIOSOCK_STATE_CONNECTED)
        {
            ASSERT(PrevState == PIOSOCK_STATE_CONNECTING || PrevState == PIOSOCK_STATE_CLOSE);
            if (!NT_SUCCESS(PIOSockConnectedAdd(pSocket)))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "PIOSockConnectedAdd failed\n");
//                InterlockedExchange((PLONG)&pSocket->State, PIOSOCK_STATE_CLOSE);
            }
        }
    }

    return PrevState;
}

PIOSOCK_STATE
PIOSockStateSetLocked(
    IN PSOCKET_CONTEXT pSocket,
    IN PIOSOCK_STATE   NewState
)
{
    PIOSOCK_STATE PrevState;

    WdfSpinLockAcquire(pSocket->StateLock);
    PrevState = PIOSockStateSet(pSocket, NewState);
    WdfSpinLockRelease(pSocket->StateLock);

    return PrevState;
}

__inline
BOOLEAN
PIOSockIsBound(
    IN PSOCKET_CONTEXT pSocket
)
{
    return pSocket->src_port != VMADDR_PORT_ANY;
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_not_held_(pSocket->RxLock)
static
VOID
PIOSockPendedRequestCancel(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    WDFFILEOBJECT ParentSocket = WDF_NO_HANDLE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, Request: %p\n", __FUNCTION__, Request);

    if (PIOSockStateGet(pSocket) == PIOSOCK_STATE_CONNECTING)
        WdfTimerStop(pSocket->ConnectTimer, FALSE);
    else
    {
        PPIOSOCK_PENDED_CONTEXT pRequest = GetRequestPendedContext(Request);
        if (pRequest)
            ParentSocket = pRequest->ParentSocket;
        if (ParentSocket != WDF_NO_HANDLE)
            pSocket = GetSocketContext(ParentSocket);
    }

    ASSERT(pSocket && pSocket->PendedRequest == Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "Socket %d\n", pSocket->SocketId);

    WdfSpinLockAcquire(pSocket->RxLock);
    WdfObjectDereference(Request);
    pSocket->PendedRequest = WDF_NO_HANDLE;
    WdfSpinLockRelease(pSocket->RxLock);

    WdfRequestComplete(Request, STATUS_CANCELLED);
}

NTSTATUS
PIOSockPendedRequestSet(
    IN PSOCKET_CONTEXT pSocket,
    IN WDFREQUEST Request
)
{
    NTSTATUS status;
    PSOCKET_CONTEXT pRequestSocket = GetSocketContextFromRequest(Request);
    PPIOSOCK_PENDED_CONTEXT pRequest = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, Socket %d, Request %p\n", __FUNCTION__, pSocket->SocketId, Request);

    ASSERT(pSocket && pRequestSocket);
    ASSERT(pSocket->PendedRequest == WDF_NO_HANDLE);

    //store parent socket handle
    if (pRequestSocket != pSocket)
    {
        WDF_OBJECT_ATTRIBUTES attributes;

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
            &attributes,
            PIOSOCK_PENDED_CONTEXT
        );

        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "Linking pended request to parent socket\n");

        status = WdfObjectAllocateContext(
            Request,
            &attributes,
            &pRequest
        );

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "WdfObjectAllocateContext failed: 0x%x\n", status);
            return status;
        }

        ASSERT(pRequest->ParentSocket == WDF_NO_HANDLE || pRequest->ParentSocket == pSocket->ThisSocket);
        pRequest->ParentSocket = pSocket->ThisSocket;
    }

    WdfObjectReference(Request);
    pSocket->PendedRequest = Request;

    status = WdfRequestMarkCancelableEx(pSocket->PendedRequest, PIOSockPendedRequestCancel);
    if (!NT_SUCCESS(status))
    {
        ASSERT(status == STATUS_CANCELLED);
        pSocket->PendedRequest = WDF_NO_HANDLE;
        WdfObjectDereference(Request);

        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Pended request canceled: 0x%x\n", status);
    }
    return status;
}

NTSTATUS
PIOSockPendedRequestSetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request
)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = PIOSockPendedRequestSet(pSocket, Request);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

NTSTATUS
PIOSockPendedRequestGet(
    IN  PSOCKET_CONTEXT pSocket,
    OUT WDFREQUEST *Request
)
{
    NTSTATUS    status = STATUS_SUCCESS;;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, Socket %d\n", __FUNCTION__, pSocket->SocketId);

    *Request = pSocket->PendedRequest;
    if (*Request != WDF_NO_HANDLE)
    {
        status = WdfRequestUnmarkCancelable(*Request);
        ASSERT(NT_SUCCESS(status));
        if (!NT_SUCCESS(status))
        {
            ASSERT(status == STATUS_CANCELLED && pSocket->PendedRequest == WDF_NO_HANDLE);
            *Request = WDF_NO_HANDLE; //do not complete canceled request
            status = STATUS_CANCELLED;
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Pended request canceled\n");
        }
        else
        {
            ASSERT(GetRequestPendedContext(*Request) == NULL || GetRequestPendedContext(*Request)->ParentSocket);

            WdfObjectDereference(*Request);
            pSocket->PendedRequest = WDF_NO_HANDLE;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, pended request: %p\n", __FUNCTION__, *Request);

    return status;
}

NTSTATUS
PIOSockPendedRequestGetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    OUT WDFREQUEST      *Request
)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = PIOSockPendedRequestGet(pSocket, Request);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

//////////////////////////////////////////////////////////////////////////
NTSTATUS
PIOSockAcceptEnqueuePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PPHYZIO_VSOCK_HDR    pPkt
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pListenSocket);
    NTSTATUS        status;
    WDFMEMORY       Memory;
    LONG            lAcceptPended;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    lAcceptPended = InterlockedIncrement(&pListenSocket->AcceptPended);
    if (lAcceptPended > pListenSocket->Backlog)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Accept list is full\n");
        InterlockedDecrement(&pListenSocket->AcceptPended);
        return STATUS_CONNECTION_RESET;
    }

    //accepted list is empty
    if (lAcceptPended == 1)
    {
        WDFREQUEST PendedRequest;

        PIOSockPendedRequestGetLocked(pListenSocket, &PendedRequest);
        if (PendedRequest)
        {
            PSOCKET_CONTEXT pAcceptSocket = GetSocketContextFromRequest(PendedRequest);

            InterlockedDecrement(&pListenSocket->AcceptPended);

            pAcceptSocket->dst_cid = (ULONG32)pPkt->src_cid;
            pAcceptSocket->dst_port = pPkt->src_port;
            pAcceptSocket->peer_buf_alloc = pPkt->src_port;
            pAcceptSocket->peer_fwd_cnt = pPkt->fwd_cnt;

            PIOSockAcceptInitSocket(pAcceptSocket, pListenSocket);

            WdfRequestComplete(PendedRequest, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }
    }

    status = WdfMemoryCreateFromLookaside(pContext->AcceptMemoryList,&Memory);

    if (NT_SUCCESS(status))
    {
        PPIOSOCK_ACCEPT_ENTRY pAccept = WdfMemoryGetBuffer(Memory, NULL);

        pAccept->Memory = Memory;
        pAccept->ConnectSocket = WDF_NO_HANDLE;

        pAccept->dst_cid = (ULONG32)pPkt->src_cid;
        pAccept->dst_port = pPkt->src_port;
        pAccept->peer_buf_alloc = pPkt->src_port;
        pAccept->peer_fwd_cnt = pPkt->fwd_cnt;

        WdfSpinLockAcquire(pListenSocket->RxLock);
        InsertTailList(&pListenSocket->AcceptList, &pAccept->ListEntry);
        WdfSpinLockRelease(pListenSocket->RxLock);
        PIOSockEventSetBit(pListenSocket, FD_ACCEPT_BIT, STATUS_SUCCESS);
        status = STATUS_PENDING;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
        InterlockedDecrement(&pListenSocket->AcceptPended);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID
PIOSockAcceptRemovePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PPHYZIO_VSOCK_HDR    pPkt
)
{
    PLIST_ENTRY pCurrentEntry;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pListenSocket->RxLock);
    for (pCurrentEntry = pListenSocket->AcceptList.Flink;
        pCurrentEntry != &pListenSocket->AcceptList;
        pCurrentEntry = pCurrentEntry->Flink)
    {
        PPIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(pCurrentEntry, PIOSOCK_ACCEPT_ENTRY, ListEntry);

        if (pAccept->dst_cid == pPkt->src_cid &&
            pAccept->dst_port == pPkt->src_port)
        {
            ASSERT(pAccept->ConnectSocket == WDF_NO_HANDLE);
            RemoveEntryList(pCurrentEntry);
            InterlockedDecrement(&pListenSocket->AcceptPended);
            WdfObjectDelete(pAccept->Memory);
            break;
        }
    }
    WdfSpinLockRelease(pListenSocket->RxLock);

}

NTSTATUS
PIOSockAcceptInitSocket(
    PSOCKET_CONTEXT pAcceptSocket,
    PSOCKET_CONTEXT pListenSocket
)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    pAcceptSocket->type = pListenSocket->type;

    pAcceptSocket->src_port = pListenSocket->src_port;

    pAcceptSocket->ConnectTimeout = pListenSocket->ConnectTimeout;
    pAcceptSocket->BufferMinSize = pListenSocket->BufferMinSize;
    pAcceptSocket->BufferMaxSize = pListenSocket->BufferMaxSize;

    pAcceptSocket->buf_alloc = pListenSocket->buf_alloc;

    PIOSockStateSetLocked(pAcceptSocket, PIOSOCK_STATE_CONNECTED);
    status = PIOSockSendResponse(pAcceptSocket);
    PIOSockEventSetBit(pListenSocket, FD_ACCEPT_BIT, status);

    return status;
}

_Requires_lock_not_held_(pListenSocket->RxLock)
static
BOOLEAN
PIOSockAcceptDequeue(
    IN PSOCKET_CONTEXT  pListenSocket,
    IN PSOCKET_CONTEXT  pAcceptSocket,
    OUT PBOOLEAN        pbSetBit
)
{
    BOOLEAN bAccepted = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    do
    {
        PLIST_ENTRY pListEntry;

        WdfSpinLockAcquire(pListenSocket->RxLock);
        pListEntry = IsListEmpty(&pListenSocket->AcceptList) ? NULL
            : RemoveHeadList(&pListenSocket->AcceptList);
        WdfSpinLockRelease(pListenSocket->RxLock);

        *pbSetBit = FALSE;
        if (pListEntry)
        {
            PPIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(pListEntry, PIOSOCK_ACCEPT_ENTRY, ListEntry);
            LONG lAcceptPended = InterlockedDecrement(&pListenSocket->AcceptPended);

            //loopback connect
            if (pAccept->ConnectSocket != WDF_NO_HANDLE)
            {
                if (!PIOSockLoopbackAcceptDequeue(pAcceptSocket, pAccept))
                {
                    WdfObjectDelete(pAccept->Memory);
                    continue;
                }
            }

            *pbSetBit = !!lAcceptPended;

            pAcceptSocket->dst_cid = pAccept->dst_cid;
            pAcceptSocket->dst_port = pAccept->dst_port;
            pAcceptSocket->peer_buf_alloc = pAccept->peer_buf_alloc;
            pAcceptSocket->peer_fwd_cnt = pAccept->peer_fwd_cnt;

            PIOSockAcceptInitSocket(pAcceptSocket, pListenSocket);

            WdfObjectDelete(pAccept->Memory);
            bAccepted = TRUE;
        }
        else
            break;
    } while (!bAccepted);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, accepted: %d\n", __FUNCTION__, bAccepted);

    return bAccepted;
}

_Requires_lock_not_held_(pListenSocket->RxLock)
static
VOID
PIOSockAcceptCleanup(
    IN PSOCKET_CONTEXT pListenSocket
)
{
    WDFREQUEST PendedRequest;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pListenSocket->RxLock);
    while (!IsListEmpty(&pListenSocket->AcceptList))
    {
        PPIOSOCK_ACCEPT_ENTRY pAccept =
            CONTAINING_RECORD(RemoveHeadList(&pListenSocket->AcceptList), PIOSOCK_ACCEPT_ENTRY, ListEntry);

        if (pAccept->ConnectSocket != WDF_NO_HANDLE)
        {
            WdfObjectDereference(pAccept->ConnectSocket);
        }

        WdfObjectDelete(pAccept->Memory);
    }
    pListenSocket->AcceptPended = 0;

    WdfSpinLockRelease(pListenSocket->RxLock);
}

static
NTSTATUS
PIOSockAccept(
    IN HANDLE       hListenSocket,
    IN WDFREQUEST   Request
)
{
    NTSTATUS        status;
    PSOCKET_CONTEXT pAcceptSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pAcceptSocket);
    PFILE_OBJECT    pFileObj;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = ObReferenceObjectByHandle(hListenSocket, FILE_READ_ACCESS, *IoFileObjectType,
        WdfRequestGetRequestorMode(Request), (PVOID)&pFileObj, NULL);

    if (NT_SUCCESS(status))
    {
        PSOCKET_CONTEXT pListenSocket = PIOSockBoundFindByFile(pContext, pFileObj);

        ObDereferenceObject(pFileObj);

        if (!pListenSocket)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Listen socket not found\n");
            status = STATUS_NOT_SOCKET;
        }
        else if (PIOSockStateGet(pListenSocket) != PIOSOCK_STATE_LISTEN)
        {
            status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            BOOLEAN bDequeued = FALSE, bSetBit = FALSE;

            if (PIOSockIsFlag(pListenSocket, SOCK_NON_BLOCK))
                PIOSockSetFlag(pAcceptSocket, SOCK_NON_BLOCK);

            PIOSockEventClearBit(pListenSocket, FD_ACCEPT_BIT);

            if (!PIOSockAcceptDequeue(pListenSocket, pAcceptSocket, &bSetBit))
            {
                if (PIOSockIsFlag(pListenSocket, SOCK_NON_BLOCK))
                    status = STATUS_CANT_WAIT;
                else
                {
                    if (pListenSocket->PendedRequest == WDF_NO_HANDLE)
                        status = PIOSockPendedRequestSetLocked(pListenSocket, Request);
                    else
                        status = STATUS_DEVICE_BUSY;

                    if (NT_SUCCESS(status))
                        status = STATUS_PENDING;
                }
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "ObReferenceObjectByHandle failed: 0x%x\n", status);
        status = STATUS_NOT_SOCKET;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

static
NTSTATUS
PIOSockCreate(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
)
{
    PDEVICE_CONTEXT         pContext = GetDeviceContext(WdfDevice);
    PSOCKET_CONTEXT         pSocket;
    NTSTATUS                status = STATUS_SUCCESS;
    WDF_REQUEST_PARAMETERS  parameters;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);

    ASSERT(FileObject);
    if (WDF_NO_HANDLE == FileObject)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "NULL FileObject\n");
        return STATUS_UNSUCCESSFUL;
    }

    pSocket = GetSocketContext(FileObject);
    pSocket->ThisSocket = FileObject;
    pSocket->SocketId = InterlockedIncrement(&pContext->SocketId);

    WDF_REQUEST_PARAMETERS_INIT(&parameters);

    //check EA presents
    WdfRequestGetParameters(Request, &parameters);

    if (parameters.Parameters.Create.EaLength)
    {
        PFILE_FULL_EA_INFORMATION EaBuffer=
            (PFILE_FULL_EA_INFORMATION)WdfRequestWdmGetIrp(Request)->AssociatedIrp.SystemBuffer;

        ASSERT(EaBuffer);

        if (EaBuffer->EaValueLength >= sizeof(PHYZIO_VSOCK_PARAMS))
        {
            WDF_OBJECT_ATTRIBUTES   Attributes;
            HANDLE                  hListenSocket;
            PPHYZIO_VSOCK_PARAMS    pParams = (PPHYZIO_VSOCK_PARAMS)((PCHAR)EaBuffer +
                (FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + 1 + EaBuffer->EaNameLength));

            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Socket %d (%p) initializing\n",
                pSocket->SocketId, pSocket->ThisSocket);

            WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
            Attributes.ParentObject = FileObject;
            status = WdfSpinLockCreate(&Attributes, &pSocket->StateLock);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "WdfSpinLockCreate failed (StateLock) - 0x%x\n", status);
                return status;
            }

            WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
            Attributes.ParentObject = FileObject;
            status = WdfSpinLockCreate(&Attributes, &pSocket->RxLock);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "WdfSpinLockCreate failed (RxLock) - 0x%x\n", status);
                return status;
            }

            KeInitializeEvent(&pSocket->CloseEvent, NotificationEvent, FALSE);

            pSocket->src_port = VMADDR_PORT_ANY;//set unbound state
            pSocket->dst_port = VMADDR_PORT_ANY;
            pSocket->dst_cid = VMADDR_CID_ANY;

            pSocket->State = PIOSOCK_STATE_CLOSE;
            pSocket->PendedRequest = WDF_NO_HANDLE;
            pSocket->LoopbackSocket = WDF_NO_HANDLE;

            status = PIOSockReadSocketQueueInit(pSocket);
            if (!NT_SUCCESS(status))
            {
                return status;
            }

            InitializeListHead(&pSocket->RxCbList);
            pSocket->RxBytes = 0;

#ifdef _WIN64
            if (WdfRequestIsFrom32BitProcess(Request))
            {
                hListenSocket = Handle32ToHandle((void * POINTER_32)(ULONG)pParams->Socket);
            }
            else
#endif //_WIN64
            {
                hListenSocket = (HANDLE)pParams->Socket;
            }

            //find listen socket
            if (hListenSocket)
            {
                status = PIOSockAccept(hListenSocket, Request);
            }
            else
            {
                if (pParams->Type != PHYZIO_VSOCK_TYPE_STREAM)
                {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "Unsupported socket type: %u\n", pSocket->type);
                    status = STATUS_NOT_SUPPORTED;
                }
                else
                {
                    pSocket->type = pParams->Type;

                    pSocket->ConnectTimeout = VSOCK_DEFAULT_CONNECT_TIMEOUT;
                    pSocket->BufferMinSize = VSOCK_DEFAULT_BUFFER_MIN_SIZE;
                    pSocket->BufferMaxSize = VSOCK_DEFAULT_BUFFER_MAX_SIZE;
                    pSocket->SendTimeout = LONG_MAX;
                    pSocket->RecvTimeout = LONG_MAX;

                    pSocket->buf_alloc = VSOCK_DEFAULT_BUFFER_SIZE;
                }
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "Invalid EA length\n");
            status = STATUS_INVALID_PARAMETER;
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Control socket %d (%p) initializing\n",
            pSocket->SocketId, pSocket->ThisSocket);

        //Socket for select and config retrieve
        PIOSockSetFlag(pSocket, SOCK_CONTROL);
        status = STATUS_SUCCESS;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE,"<-- %s\n", __FUNCTION__);
    return status;
}

VOID
PIOSockCreateStub(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
)
{
    NTSTATUS status = PIOSockCreate(WdfDevice, Request, FileObject);

    PAGED_CODE();

    if (status != STATUS_PENDING)
        WdfRequestComplete(Request, status);
}

VOID
PIOSockDoClose(
    PSOCKET_CONTEXT pSocket
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    pSocket->PeerShutdown = PHYZIO_VSOCK_SHUTDOWN_MASK;

    if (!PIOSockRxHasData(pSocket))
    {
        PIOSockStateSet(pSocket, PIOSOCK_STATE_CLOSING);
    }

    KeSetEvent(&pSocket->CloseEvent, IO_NO_INCREMENT, FALSE);
}

VOID
PIOSockClose(
    IN WDFFILEOBJECT FileObject
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContext(FileObject);
    WDFREQUEST Request;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "--> %s\n", __FUNCTION__);

    if (PIOSockIsFlag(pSocket, SOCK_CONTROL))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Control socket %d closed\n", pSocket->SocketId);
        return;
    }

    PIOSockBoundRemove(pSocket);

    if (PIOSockStateGet(pSocket) == PIOSOCK_STATE_CONNECTED ||
        PIOSockStateGet(pSocket) == PIOSOCK_STATE_CLOSING)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "Socket %d, peershutdown: %x\n", pSocket->SocketId, pSocket->PeerShutdown);

        /* Already received SHUTDOWN from peer, reply with RST */
        if ((pSocket->PeerShutdown & PHYZIO_VSOCK_SHUTDOWN_MASK) == PHYZIO_VSOCK_SHUTDOWN_MASK)
        {
            if(PIOSockStateGet(pSocket) != PIOSOCK_STATE_CLOSING)
                PIOSockSendReset(pSocket, FALSE);
        }
        else
        {
            LARGE_INTEGER liTimeout;

            if ((pSocket->Shutdown & PHYZIO_VSOCK_SHUTDOWN_MASK) != PHYZIO_VSOCK_SHUTDOWN_MASK)
            {
                PIOSockSendShutdown(pSocket, PHYZIO_VSOCK_SHUTDOWN_MASK);
            }

            liTimeout.QuadPart = -(VSOCK_CLOSE_TIMEOUT + (PIOSockIsFlag(pSocket, SOCK_LINGER) ?
                WDF_ABS_TIMEOUT_IN_SEC(pSocket->LingerTime) : 0));

            if (KeWaitForSingleObject(&pSocket->CloseEvent, Executive, KernelMode, FALSE, &liTimeout) == STATUS_TIMEOUT)
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_CREATE_CLOSE, "Socket %d, close timeout expires\n", pSocket->SocketId);

                PIOSockSendReset(pSocket, FALSE);
                PIOSockDoClose(pSocket);
            }
        }
    }

    PIOSockStateSet(pSocket, PIOSOCK_STATE_CLOSE);

    PIOSockPendedRequestGetLocked(pSocket, &Request);
    if (Request != WDF_NO_HANDLE)
    {
        WdfRequestComplete(Request, STATUS_CANCELLED);
    }
    if (pSocket->EventObject)
    {
        ObDereferenceObject(pSocket->EventObject);
        pSocket->EventObject = NULL;
    }

    if (pSocket->LoopbackSocket != WDF_NO_HANDLE)
        WdfObjectDereference(pSocket->LoopbackSocket);

    if (PIOSockStateGet(pSocket) == PIOSOCK_STATE_LISTEN)
        PIOSockAcceptCleanup(pSocket);
    else
        PIOSockReadCleanupCb(pSocket);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Socket %d closed\n", pSocket->SocketId);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
}

__inline
BOOLEAN
PIOSockIsMoozeAddrValid(
    IN PSOCKADDR_VM    pAddr
)
{
    return (pAddr->svm_family == AF_VSOCK) &&
        (pAddr->svm_cid != VMADDR_CID_HYPERVISOR) &&
        (pAddr->svm_cid != VMADDR_CID_RESERVED) &&
        (pAddr->svm_cid != VMADDR_CID_HOST);
}

__inline
BOOLEAN
PIOSockIsHostAddrValid(
    IN PSOCKADDR_VM    pAddr,
    IN ULONG32         ulSrcCid
)
{
    return (pAddr->svm_family == AF_VSOCK) &&
        (pAddr->svm_cid == VMADDR_CID_HYPERVISOR ||
            pAddr->svm_cid == VMADDR_CID_HOST ||
            pAddr->svm_cid == ulSrcCid);
}

static
NTSTATUS
PIOSockBind(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;
    static ULONG32  uSvmPort;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if (!PIOSockIsMoozeAddrValid(pAddr))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid addr to bind, cid: %u, port: %u\n", pAddr->svm_cid, pAddr->svm_port);
        return STATUS_INVALID_ADDRESS_COMPONENT;
    }

    if (pAddr->svm_cid != (ULONG32)pContext->Config.mooze_cid &&
        pAddr->svm_cid != VMADDR_CID_ANY)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid cid: %u\n", pAddr->svm_cid);
        return STATUS_INVALID_ADDRESS_COMPONENT;
    }

    if (PIOSockIsBound(pSocket))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Socket %d already bound\n", pSocket->SocketId);
        return STATUS_INVALID_PARAMETER;
    }

    if (PIOSockStateGet(pSocket) != PIOSOCK_STATE_CLOSE)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid socket state: %u\n", PIOSockStateGet(pSocket));
        return STATUS_UNSUCCESSFUL;
    }

    status = PIOSockBoundAdd(pSocket, pAddr->svm_port);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "PIOSockBoundAdd failed: 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);

    return status;
}

static
NTSTATUS
PIOSockConnect(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if(!PIOSockIsHostAddrValid(pAddr, (ULONG32)pContext->Config.mooze_cid))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid addr to connect, cid: %u, port: %u\n", pAddr->svm_cid, pAddr->svm_port);
        return STATUS_INVALID_ADDRESS_COMPONENT;
    }

    switch (PIOSockStateGet(pSocket))
    {
    case PIOSOCK_STATE_CONNECTING:
        status = STATUS_CONNECTION_ESTABLISHING;
        break;
    case PIOSOCK_STATE_CONNECTED:
    case PIOSOCK_STATE_CLOSING:
        status = STATUS_CONNECTION_ACTIVE;
        break;
    case PIOSOCK_STATE_LISTEN:
        status = STATUS_INVALID_PARAMETER;
        break;
    default:
        status = STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid socket state: %u\n", pSocket->State);
        return status;
    }

    if (!pSocket->ConnectTimer)
    {
        WDF_OBJECT_ATTRIBUTES   Attributes;
        WDF_TIMER_CONFIG        timerConfig;

        WDF_TIMER_CONFIG_INIT(&timerConfig, PIOSockConnectTimerFunc);

        WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
        Attributes.ParentObject = pSocket->ThisSocket;

        status = WdfTimerCreate(&timerConfig, &Attributes, &pSocket->ConnectTimer);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET,
                "WdfTimerCreate failed: 0x%x\n", status);
            return status;
        }
    }

    if (!PIOSockIsBound(pSocket))
    {
        //autobind
        status = PIOSockBoundAdd(pSocket, VMADDR_PORT_ANY);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "PIOSockBoundAdd failed: 0x%x\n", status);
            return status;
        }
    }

    PIOSockEventClearBit(pSocket, FD_CONNECT_BIT);

    pSocket->dst_cid = pAddr->svm_cid;
    pSocket->dst_port = pAddr->svm_port;

    //no lock required
    PIOSockStateSet(pSocket, PIOSOCK_STATE_CONNECTING);

    if (!PIOSockIsFlag(pSocket, SOCK_NON_BLOCK))
    {
        status = PIOSockPendedRequestSetLocked(pSocket, Request);
        if (!NT_SUCCESS(status))
        {
            PIOSockStateSet(pSocket, PIOSOCK_STATE_CLOSE);
            return status;
        }
        WdfTimerStart(pSocket->ConnectTimer, -pSocket->ConnectTimeout);
    }

    if (pSocket->dst_cid == (ULONG32)pContext->Config.mooze_cid)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "Connect to local socket (loopback)\n");
        PIOSockSetFlag(pSocket, SOCK_LOOPBACK);
    }
    else
        PIOSockResetFlag(pSocket, SOCK_LOOPBACK);

    status = PIOSockSendConnect(pSocket);

    if (!NT_SUCCESS(status))
    {
        //no lock required
        PIOSockStateSet(pSocket, PIOSOCK_STATE_CLOSE);
        if (!PIOSockIsFlag(pSocket, SOCK_NON_BLOCK))
        {
            WdfTimerStop(pSocket->ConnectTimer, TRUE);
            if (!NT_SUCCESS(PIOSockPendedRequestGetLocked(pSocket, &Request)))
            {
                status = STATUS_PENDING; //do not complete canceled request
            }
        }
    }
    else
        status = PIOSockIsFlag(pSocket, SOCK_NON_BLOCK) ? STATUS_CANT_WAIT : STATUS_PENDING;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

BOOLEAN
PIOSockShutdownFromPeer(
    PSOCKET_CONTEXT pSocket,
    ULONG uFlags
)
{
    BOOLEAN bRes;
    ULONG32 uDrain;

    WdfSpinLockAcquire(pSocket->StateLock);
    uDrain = ~pSocket->PeerShutdown & uFlags;
    pSocket->PeerShutdown |= uFlags;
    bRes = (pSocket->PeerShutdown & PHYZIO_VSOCK_SHUTDOWN_MASK) == PHYZIO_VSOCK_SHUTDOWN_MASK;
    WdfSpinLockRelease(pSocket->StateLock);

//     if (uDrain & PHYZIO_VSOCK_SHUTDOWN_SEND)
//         PIOSockReadDequeueCb(pSocket, WDF_NO_HANDLE);
//     if (uDrain & PHYZIO_VSOCK_SHUTDOWN_RCV)
//     {
//         //TODO: dequeue requests for current socket only
//     }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, bRes: %u\n", __FUNCTION__, bRes);

    return bRes;
}

_Requires_lock_not_held_(pSocket->StateLock)
static
NTSTATUS
PIOSockStateValidateShutdown(
    PSOCKET_CONTEXT pSocket,
    ULONG32 uHow
)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->StateLock);

    if ((pSocket->Shutdown & uHow) == uHow)
    {
        status = STATUS_LOCAL_DISCONNECT;
    }
    else if(pSocket->PeerShutdown & PHYZIO_VSOCK_SHUTDOWN_MASK)
    {
        status = STATUS_SUCCESS;
    }
    else if (PIOSockStateGet(pSocket) == PIOSOCK_STATE_CLOSING)
    {
        status = STATUS_CONNECTION_RESET;
    }
    else if (PIOSockStateGet(pSocket) != PIOSOCK_STATE_CONNECTED)
    {
        status = STATUS_CONNECTION_INVALID;
    }
    else
        status = STATUS_SUCCESS;

    WdfSpinLockRelease(pSocket->StateLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

static
NTSTATUS
PIOSockShutdown(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    int             *pHow;
    SIZE_T          stHowLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pHow), &pHow, &stHowLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stHowLen >= sizeof(*pHow));

    /* User level uses SD_RECEIVE (0) and SD_SEND (1), but the kernel uses
    * PHYZIO_VSOCK_SHUTDOWN_RCV (1) and PHYZIO_VSOCK_SHUTDOWN_SEND (2),
    * so we must increment mode here like the other address families do.
    * Note also that the increment makes SD_BOTH (2) into
    * RCV_SHUTDOWN | SEND_SHUTDOWN (3), which is what we want.
    */

    (*pHow)++;
    if ((*pHow & ~PHYZIO_VSOCK_SHUTDOWN_MASK) || !*pHow)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid shutdown flags: %u\n", *pHow);
        return STATUS_INVALID_PARAMETER;
    }

    status = PIOSockStateValidateShutdown(pSocket, *pHow);

    if (NT_SUCCESS(status))
    {
        pSocket->Shutdown |= (ULONG32)*pHow;
        status = PIOSockSendShutdown(pSocket, *pHow);
    }
    else if (status == STATUS_LOCAL_DISCONNECT)
    {
        status = STATUS_SUCCESS;
    }

    return status;
}

static
NTSTATUS
PIOSockListen(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    int             *pBacklog;
    SIZE_T          stBacklogLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pBacklog), &pBacklog, &stBacklogLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stBacklogLen >= sizeof(*pBacklog));

    if (PIOSockStateGet(pSocket) == PIOSOCK_STATE_CLOSE)
    {
        if (PIOSockIsBound(pSocket))
        {
            pSocket->Backlog = *pBacklog;
            InitializeListHead(&pSocket->AcceptList);
            //no lock required
            PIOSockStateSet(pSocket, PIOSOCK_STATE_LISTEN);
        }
        else
            status = STATUS_INVALID_PARAMETER;
    }
    else
        status = STATUS_CONNECTION_ACTIVE;

    return status;
}
__inline
PKEVENT
PIOSockGetEventFromHandle(
    IN HANDLE hEvent
)
{
    PKEVENT pEvent = NULL;

    ObReferenceObjectByHandle(hEvent, STANDARD_RIGHTS_REQUIRED, *ExEventObjectType,
        KernelMode, (PVOID)&pEvent, NULL);

    return pEvent;
}

_Requires_lock_not_held_(pSocket->StateLock)
static
VOID
PIOSockEventUnregister(
    IN PSOCKET_CONTEXT  pSocket,
    IN PPHYZIO_VSOCK_NETWORK_EVENTS  lpNetworkEvents,
    IN PKEVENT          pEvent
)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    if (pEvent)
        pSocket->EventObject = NULL;
    lpNetworkEvents->NetworkEvents = pSocket->Events & pSocket->EventsMask;
    RtlCopyMemory(lpNetworkEvents->Status, pSocket->EventsStatus, sizeof(pSocket->EventsStatus));
    pSocket->Events = 0;
    WdfSpinLockRelease(pSocket->StateLock);

    if (pEvent)
    {
        KeClearEvent(pEvent);
        ObDereferenceObject(pEvent);
    }
}

static
NTSTATUS
PIOSockEnumNetEvents(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT     pSocket = GetSocketContextFromRequest(Request);
    PULONGLONG          pEventObject;
    SIZE_T              stEventObjectLen;
    PPHYZIO_VSOCK_NETWORK_EVENTS  lpNetworkEvents;
    SIZE_T              stNetworkEventsLen;
    NTSTATUS            status;
    HANDLE              hEvent = NULL;
    PKEVENT             pEvent = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pEventObject), &pEventObject, &stEventObjectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stEventObjectLen >= sizeof(*pEventObject));

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*lpNetworkEvents), &lpNetworkEvents, &stNetworkEventsLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
    _Analysis_assume_(stNetworkEventsLen >= sizeof(*lpNetworkEvents));

#ifdef _WIN64
    if (WdfRequestIsFrom32BitProcess(Request))
    {
        hEvent = Handle32ToHandle((void * POINTER_32)(ULONG)*pEventObject);
    }
    else
#endif //_WIN64
    {
        hEvent = (HANDLE)(ULONG_PTR)*pEventObject;
    }

    if (hEvent)
    {
        pEvent = PIOSockGetEventFromHandle(hEvent);
        if (!pEvent)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "PIOSockGetEventFromHandle failed\n");
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (pEvent && pEvent != pSocket->EventObject)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid event handle\n");
        return STATUS_INVALID_PARAMETER;
    }

    PIOSockEventUnregister(pSocket, lpNetworkEvents, pEvent);

    *pLength = sizeof(*lpNetworkEvents);

    return STATUS_SUCCESS;
}

__inline
NTSTATUS
PIOSockSetNonBlocking(
    IN PSOCKET_CONTEXT pSocket,
    IN BOOLEAN bNonBlocking
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, bNonBlocking: %u\n", __FUNCTION__, bNonBlocking);

    if (!PIOSockSetFlag(pSocket, SOCK_NON_BLOCK))
    {
        PIOSockReadDequeueCb(pSocket, WDF_NO_HANDLE);
        //TODO: process blocked Tx if any
    }

    return STATUS_SUCCESS;
}

_Requires_lock_not_held_(pSocket->StateLock)
static
VOID
PIOSockEventRegister(
    IN PSOCKET_CONTEXT  pSocket,
    IN LONG             lNetworkEvents,
    IN PKEVENT          pEvent
)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->EventsMask = lNetworkEvents;
    if (pSocket->EventObject)
        ObDereferenceObject(pSocket->EventObject);
    pSocket->Events = 0;
    pSocket->EventObject = pEvent;
    WdfSpinLockRelease(pSocket->StateLock);
}

static
NTSTATUS
PIOSockEventSelect(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT             pSocket = GetSocketContextFromRequest(Request);
    PPHYZIO_VSOCK_EVENT_SELECT  pEventSelect;
    SIZE_T                      stEventSelectLen;
    NTSTATUS                    status;
    PKEVENT                     pEvent = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pEventSelect), &pEventSelect, &stEventSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stEventSelectLen >= sizeof(*pEventSelect));

    if (pEventSelect->lNetworkEvents)
    {
        HANDLE  hEvent;

#ifdef _WIN64
        if (WdfRequestIsFrom32BitProcess(Request))
        {
            hEvent = Handle32ToHandle((void * POINTER_32)pEventSelect->hEventObject);
        }
        else
#endif //_WIN64
        {
            hEvent = (HANDLE)pEventSelect->hEventObject;
        }

        if (!hEvent)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid hEventObject\n");
            return STATUS_INVALID_PARAMETER;
        }

        pEvent = PIOSockGetEventFromHandle(hEvent);
        if (!pEvent)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "PIOSockGetEventFromHandle failed\n");
            return STATUS_INVALID_PARAMETER;
        }
    }

    PIOSockSetNonBlocking(pSocket, TRUE);

    PIOSockEventRegister(pSocket, pEventSelect->lNetworkEvents, pEvent);

    return STATUS_SUCCESS;
}

VOID
PIOSockEventSetBit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uSetBit,
    IN NTSTATUS Status
)
{
    ULONG uEvent = (1 << uSetBit);
    BOOLEAN bSetEvent;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, uSetBit: 0x%x\n", __FUNCTION__, uSetBit);

    bSetEvent = !!(~pSocket->Events & uEvent & pSocket->EventsMask);

    pSocket->Events |= uEvent;
    pSocket->EventsStatus[uSetBit] = Status;

    PIOSockSelectRun(pSocket);

    if (bSetEvent && pSocket->EventObject)
        KeSetEvent(pSocket->EventObject, IO_NO_INCREMENT, FALSE);
}

VOID
PIOSockEventSetBitLocked(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uSetBit,
    IN NTSTATUS Status
)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    PIOSockEventSetBit(pSocket, uSetBit, Status);
    WdfSpinLockRelease(pSocket->StateLock);
}

VOID
PIOSockEventClearBit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uClearBit
)
{
    ULONG uEvent = (1 << uClearBit);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, uClearBit: 0x%x\n", __FUNCTION__, uClearBit);

    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->Events &= ~uEvent;
    WdfSpinLockRelease(pSocket->StateLock);
}

static
NTSTATUS
PIOSockGetPeerName(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if (PIOSockStateGet(pSocket) == PIOSOCK_STATE_CONNECTED)
    {
        RtlZeroBytes(pAddr, sizeof(*pAddr));
        pAddr->svm_family = AF_VSOCK;
        pAddr->svm_cid = pSocket->dst_cid;
        pAddr->svm_port = pSocket->dst_port;
        *pLength = sizeof(*pAddr);
    }
    else
        status = STATUS_INVALID_DEVICE_STATE;

    return STATUS_SUCCESS;
}

static
NTSTATUS
PIOSockGetSockName(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if (PIOSockIsBound(pSocket))
    {
        RtlZeroBytes(pAddr, sizeof(*pAddr));
        pAddr->svm_family = AF_VSOCK;
        pAddr->svm_cid = (ULONG32)pContext->Config.mooze_cid;
        pAddr->svm_port = pSocket->src_port;
        *pLength = sizeof(*pAddr);
    }
    else
        status = STATUS_INVALID_DEVICE_STATE;

    return STATUS_SUCCESS;
}
/*
 * Structure used for manipulating linger option.
 */
typedef struct  _LINGER {
    USHORT l_onoff;                /* option on/off */
    USHORT l_linger;               /* linger time */
}LINGER, *PLINGER;

#define SO_ACCEPTCONN   0x0002          /* socket has had listen() */
#define SO_LINGER       0x0080          /* linger on close if data present */

#define SOCK_STREAM     1

/*
 * Structure used in select() call, taken from the BSD file sys/time.h.
 */
struct timeval {
    long    tv_sec;         /* seconds */
    long    tv_usec;        /* and microseconds */
};

#define USEC_PER_SEC    1000000L

static
NTSTATUS
PIOSockGetSockOpt(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT     pSocket = GetSocketContextFromRequest(Request);
    PPHYZIO_VSOCK_OPT   pOpt;
    SIZE_T              stOptLen;
    PVOID               pOptVal;
    NTSTATUS            status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pOpt), &pOpt, &stOptLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stOptLen >= sizeof(*pOpt));

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pOpt), &pOpt, &stOptLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stOptLen >= sizeof(*pOpt));

    if (!pOpt->optlen || !pOpt->optval)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid *optval\n");
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN64
    if (WdfRequestIsFrom32BitProcess(Request))
    {
        pOptVal = Ptr32ToPtr((void * POINTER_32)(ULONG)pOpt->optval);
    }
    else
#endif //_WIN64
    {
        pOptVal = (PVOID)pOpt->optval;
    }

    if (WdfRequestGetRequestorMode(Request) == UserMode)
    {
        WDFMEMORY   Memory;
        status = WdfRequestProbeAndLockUserBufferForWrite(Request, pOptVal, pOpt->optlen, &Memory);

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestProbeAndLockUserBufferForWrite failed: 0x%x\n", status);
            return status;
        }
        pOptVal = WdfMemoryGetBuffer(Memory, NULL);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "OptName: 0x%04x\n", pOpt->optname);

    switch (pOpt->optname)
    {
    case SO_ACCEPTCONN:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = (PIOSockStateGet(pSocket) == PIOSOCK_STATE_LISTEN);
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_TYPE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = SOCK_STREAM;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_LINGER:
        if (pOpt->optlen < sizeof(LINGER))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        ((PLINGER)pOptVal)->l_onoff = PIOSockIsFlag(pSocket, SOCK_LINGER);
        ((PLINGER)pOptVal)->l_linger = pSocket->LingerTime;

        pOpt->optlen = sizeof(LINGER);
        break;

    case SO_SNDTIMEO:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = pSocket->SendTimeout;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_RCVTIMEO:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = pSocket->RecvTimeout;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_VM_SOCKETS_BUFFER_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = pSocket->buf_alloc;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = pSocket->BufferMaxSize;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = pSocket->BufferMinSize;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_VM_SOCKETS_CONNECT_TIMEOUT:
        if (pOpt->optlen < sizeof(struct timeval))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        else
        {
            struct timeval* tv = (struct timeval*)pOptVal;

            tv->tv_sec = (long)NANO_TO_SEC(pSocket->ConnectTimeout);
            tv->tv_usec = (long)NANO_TO_USEC(pSocket->ConnectTimeout - SEC_TO_NANO((LONGLONG)tv->tv_sec));

            pOpt->optlen = sizeof(*tv);
        }
        break;
    default:
        status = STATUS_NOT_SUPPORTED;
    }

    if (NT_SUCCESS(status))
        *pLength = sizeof(*pOpt);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

__inline
VOID
PIOSockSetLinger(
    IN PSOCKET_CONTEXT pSocket,
    IN PLINGER         pLinger
)
{
    if (pLinger->l_onoff)
    {
        pSocket->LingerTime = pLinger->l_linger;
        PIOSockSetFlag(pSocket, SOCK_LINGER);
    }
    else
        PIOSockResetFlag(pSocket, SOCK_LINGER);
}

static
VOID
PIOSockRxUpdateBufferSize(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG           uBufferSize
)
{
    if (uBufferSize > pSocket->BufferMaxSize)
        uBufferSize = pSocket->BufferMaxSize;

    if (uBufferSize < pSocket->BufferMinSize)
        uBufferSize = pSocket->BufferMinSize;

    if (uBufferSize != pSocket->buf_alloc)
    {
        pSocket->buf_alloc = uBufferSize;
        PIOSockSendCreditUpdate(pSocket);
    }
}

static
NTSTATUS
PIOSockSetSockOpt(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT     pSocket = GetSocketContextFromRequest(Request);
    PPHYZIO_VSOCK_OPT   pOpt;
    SIZE_T              stOptLen;
    PVOID               pOptVal;
    NTSTATUS            status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pOpt), &pOpt, &stOptLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stOptLen >= sizeof(*pOpt));

    if (!pOpt->optlen || !pOpt->optval)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid *optval\n");
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN64
    if (WdfRequestIsFrom32BitProcess(Request))
    {
        pOptVal = Ptr32ToPtr((void * POINTER_32)(ULONG)pOpt->optval);
    }
    else
#endif //_WIN64
    {
        pOptVal = (PVOID)pOpt->optval;
    }

    if (WdfRequestGetRequestorMode(Request) == UserMode)
    {
        WDFMEMORY   Memory;
        status = WdfRequestProbeAndLockUserBufferForRead(Request, pOptVal, pOpt->optlen, &Memory);

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestProbeAndLockUserBufferForRead failed: 0x%x\n", status);
            return status;
        }
        pOptVal = WdfMemoryGetBuffer(Memory, NULL);

    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "OptName: 0x%04x\n", pOpt->optname);

    switch (pOpt->optname)
    {
    case SO_LINGER:
        if (pOpt->optlen < sizeof(LINGER))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        PIOSockSetLinger(pSocket, (PLINGER)pOptVal);
        break;

    case SO_SNDTIMEO:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        pSocket->SendTimeout = *(PULONG)pOptVal;
        break;

    case SO_RCVTIMEO:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        pSocket->RecvTimeout = *(PULONG)pOptVal;
        break;

    case SO_VM_SOCKETS_BUFFER_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        PIOSockRxUpdateBufferSize(pSocket, *(PULONG)pOptVal);
        break;

    case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        pSocket->BufferMaxSize = *(PULONG)pOptVal;
        PIOSockRxUpdateBufferSize(pSocket, pSocket->buf_alloc);
        break;

    case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        pSocket->BufferMinSize = *(PULONG)pOptVal;
        PIOSockRxUpdateBufferSize(pSocket, pSocket->buf_alloc);
        break;

    case SO_VM_SOCKETS_CONNECT_TIMEOUT:
        if (pOpt->optlen < sizeof(struct timeval))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        else
        {
            struct timeval *tv = (struct timeval*)pOptVal;
            LONGLONG llTimeout = 0;

            if (tv->tv_sec >= 0 && tv->tv_usec < USEC_PER_SEC &&
                tv->tv_sec < (LONG_MAX / 1000 - 1))
            {
                llTimeout = SEC_TO_NANO(tv->tv_sec) + USEC_TO_NANO(tv->tv_usec);
            }

            if (!llTimeout)
                llTimeout = VSOCK_DEFAULT_CONNECT_TIMEOUT;

            pSocket->ConnectTimeout = llTimeout;
        }
        break;

    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

#define IOCPARM_MASK    0x7f            /* parameters must be < 128 bytes */
#define IOC_VOID        0x20000000      /* no parameters */
#define IOC_OUT         0x40000000      /* copy out parameters */
#define IOC_IN          0x80000000      /* copy in parameters */

#define _IOR(x,y,t)     (IOC_OUT|(((long)sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y))
#define _IOW(x,y,t)     (IOC_IN|(((long)sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y))

#define FIONREAD    _IOR('f', 127, ULONG) /* get # bytes to read */
#define FIONBIO     _IOW('f', 126, ULONG) /* set/clear non-blocking i/o */

#define IOC_WS2                       0x08000000

#define _WSAIO(x,y)                   (IOC_VOID|(x)|(y))
#define _WSAIOR(x,y)                  (IOC_OUT|(x)|(y))

#define SIO_ADDRESS_LIST_QUERY        _WSAIOR(IOC_WS2,22)
#define SIO_ADDRESS_LIST_CHANGE       _WSAIO(IOC_WS2,23)

static
NTSTATUS
PIOSockIoctl(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT     pSocket = GetSocketContextFromRequest(Request);
    PPHYZIO_VSOCK_IOCTL_IN pInParams;
    SIZE_T              stInParamsLen;
    PVOID               lpvOutBuffer = NULL;
    SIZE_T              stOutBufferLen = 0;
    NTSTATUS            status;
    PVOID               lpvInBuffer = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pInParams), &pInParams, &stInParamsLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stInParamsLen >= sizeof(*pInParams));

    if (pInParams->dwIoControlCode == FIONREAD ||
        pInParams->dwIoControlCode == SIO_ADDRESS_LIST_QUERY)
    {
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &lpvOutBuffer, &stOutBufferLen);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
            return status;
        }
    }

    if (pInParams->cbInBuffer)
    {
#ifdef _WIN64
        if (WdfRequestIsFrom32BitProcess(Request))
        {
            lpvInBuffer = Ptr32ToPtr((void * POINTER_32)(ULONG)pInParams->lpvInBuffer);
        }
        else
#endif //_WIN64
        {
            lpvInBuffer = (PVOID)pInParams->lpvInBuffer;
        }

        if (lpvInBuffer)
        {
            if (WdfRequestGetRequestorMode(Request) == UserMode)
            {
                WDFMEMORY   Memory;
                status = WdfRequestProbeAndLockUserBufferForRead(Request, lpvInBuffer, pInParams->cbInBuffer, &Memory);

                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestProbeAndLockUserBufferForRead failed: 0x%x\n", status);
                    return status;
                }
                lpvInBuffer = WdfMemoryGetBuffer(Memory, NULL);

            }
        }
        else
            status = STATUS_INVALID_PARAMETER;
    }

    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "dwIoControlCode: 0x%08x\n", pInParams->dwIoControlCode);

        switch (pInParams->dwIoControlCode)
        {
        case FIONREAD:
            if (stOutBufferLen >= sizeof(ULONG))
            {
                *(PULONG)lpvOutBuffer = PIOSockRxHasData(pSocket);
                *pLength = sizeof(ULONG);
                status = STATUS_SUCCESS;
            }
            else
                status = STATUS_INVALID_PARAMETER;
            break;

        case FIONBIO:
            if (pInParams->cbInBuffer >= sizeof(ULONG))
            {
                status = PIOSockSetNonBlocking(pSocket, !!(*(PULONG)lpvInBuffer));
            }
            else
                status = STATUS_INVALID_PARAMETER;
            break;

            //TODO: implement address list change
        case SIO_ADDRESS_LIST_QUERY:
        case SIO_ADDRESS_LIST_CHANGE:
        default:
            status = STATUS_NOT_SUPPORTED;
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

NTSTATUS
PIOSockDeviceControl(
    IN WDFREQUEST Request,
    IN ULONG      IoControlCode,
    IN OUT size_t *pLength
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, code: 0x%08x, socket %d\n",
        __FUNCTION__, IoControlCode, GetSocketContextFromRequest(Request)->SocketId);

    *pLength = 0;

    switch (IoControlCode)
    {
    case IOCTL_SOCKET_BIND:
        status = PIOSockBind(Request);
        break;
    case IOCTL_SOCKET_CONNECT:
        status = PIOSockConnect(Request);
        break;
    case IOCTL_SOCKET_READ:
        status = PIOSockReadWithFlags(Request);
        break;
    case IOCTL_SOCKET_SHUTDOWN:
        status = PIOSockShutdown(Request);
        break;
    case IOCTL_SOCKET_LISTEN:
        status = PIOSockListen(Request);
        break;
    case IOCTL_SOCKET_ENUM_NET_EVENTS:
        status = PIOSockEnumNetEvents(Request, pLength);
        break;
    case IOCTL_SOCKET_EVENT_SELECT:
        status = PIOSockEventSelect(Request);
        break;
    case IOCTL_SOCKET_GET_PEER_NAME:
        status = PIOSockGetPeerName(Request, pLength);
        break;
    case IOCTL_SOCKET_GET_SOCK_NAME:
        status = PIOSockGetSockName(Request, pLength);
        break;
    case IOCTL_SOCKET_GET_SOCK_OPT:
        status = PIOSockGetSockOpt(Request, pLength);
        break;
    case IOCTL_SOCKET_SET_SOCK_OPT:
        status = PIOSockSetSockOpt(Request);
        break;
    case IOCTL_SOCKET_IOCTL:
        status = PIOSockIoctl(Request, pLength);
        break;
    default:
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "Invalid socket ioctl\n");
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

NTSTATUS
PIOSockStateValidate(
    PSOCKET_CONTEXT pSocket,
    BOOLEAN         bTx
)
{
    NTSTATUS status;
    BOOLEAN bIsShutdown, bIsPeerShutdown;

    WdfSpinLockAcquire(pSocket->StateLock);

    if (bTx)
    {

        bIsShutdown = pSocket->Shutdown & PHYZIO_VSOCK_SHUTDOWN_SEND;
        bIsPeerShutdown = pSocket->PeerShutdown & PHYZIO_VSOCK_SHUTDOWN_RCV;
    }
    else
    {
        bIsShutdown = pSocket->Shutdown & PHYZIO_VSOCK_SHUTDOWN_RCV;
        bIsPeerShutdown = pSocket->PeerShutdown & PHYZIO_VSOCK_SHUTDOWN_SEND;
    }

    if (bIsShutdown)
    {
        status = STATUS_LOCAL_DISCONNECT;
    }
    else if (bIsPeerShutdown)
    {
        status = STATUS_REMOTE_DISCONNECT;
    }
    else if (PIOSockStateGet(pSocket) == PIOSOCK_STATE_CLOSING)
    {
        status = STATUS_CONNECTION_RESET;
    }
    else if (PIOSockStateGet(pSocket) != PIOSOCK_STATE_CONNECTED)
    {
        status = STATUS_CONNECTION_INVALID;
    }
    else
        status = STATUS_SUCCESS;

    WdfSpinLockRelease(pSocket->StateLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

WDFFILEOBJECT
PIOSockGetSocketFromHandle(
    IN PDEVICE_CONTEXT pContext,
    IN ULONGLONG       uSocket,
    IN BOOLEAN         bIs32BitProcess
)
{
    NTSTATUS        status;
    PFILE_OBJECT    pFileObj;
    HANDLE          hSocket;


#ifdef _WIN64
    if (bIs32BitProcess)
    {
        hSocket = Handle32ToHandle((void * POINTER_32)(ULONG)uSocket);
    }
    else
#endif //_WIN64
    {
        hSocket = (HANDLE)uSocket;
    }

    status = ObReferenceObjectByHandle(hSocket, STANDARD_RIGHTS_REQUIRED, *IoFileObjectType,
        KernelMode, (PVOID)&pFileObj, NULL);

    if (NT_SUCCESS(status))
    {
        PSOCKET_CONTEXT pSocket = PIOSockConnectedFindByFile(pContext, pFileObj);
        if (!pSocket)
        {
            pSocket = PIOSockBoundFindByFile(pContext, pFileObj);
        }

        ObDereferenceObject(pFileObj);

        if (pSocket)
        {
            WdfObjectReference(pSocket->ThisSocket);
            return pSocket->ThisSocket;
        }
    }

    return WDF_NO_HANDLE;
}

VOID
PIOSockHandleTransportReset(
    IN PDEVICE_CONTEXT pContext
)
{
    WDFFILEOBJECT Socket;
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    while (WDF_NO_HANDLE != (Socket = WdfCollectionGetFirstItem(pContext->ConnectedList)))
    {
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(Socket);

        ASSERT(PIOSockStateGet(pCurrentSocket) == PIOSOCK_STATE_CONNECTED);

        PIOSockStateSet(pCurrentSocket, PIOSOCK_STATE_CLOSE);
        PIOSockEventSetBit(pCurrentSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////
VOID
PIOSockConnectTimerFunc(
    WDFTIMER Timer
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContext(WdfTimerGetParentObject(Timer));
    WDFREQUEST PendedRequest;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    ASSERT(PIOSockStateGet(pSocket) == PIOSOCK_STATE_CONNECTING);

    if (NT_SUCCESS(PIOSockPendedRequestGetLocked(pSocket, &PendedRequest)) &&
        PendedRequest != WDF_NO_HANDLE)
    {
        WdfRequestComplete(PendedRequest, STATUS_TIMEOUT);
//        PIOSockTxCancel(GetDeviceContextFromSocket(pSocket), pSocket, STATUS_TIMEOUT);
    }
}