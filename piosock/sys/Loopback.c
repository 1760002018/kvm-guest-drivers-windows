/*
 * Loopback socket functions
 *
 * Copyright (c) 2020 Virtuozzo International GmbH
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
#include "Loopback.tmh"
#endif


_Requires_lock_not_held_(pSocket->RxLock)
static
NTSTATUS
PIOSockLoopbackAcceptEnqueue(
    IN PSOCKET_CONTEXT  pListenSocket,
    IN PSOCKET_CONTEXT  pConnectSocket
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

            pAcceptSocket->dst_cid = pContext->Config.mooze_cid;
            pAcceptSocket->dst_port = pConnectSocket->src_port;
            pAcceptSocket->peer_buf_alloc = pConnectSocket->buf_alloc;
            pAcceptSocket->peer_fwd_cnt = pConnectSocket->fwd_cnt;

            //link accepted socket to connecting one
            pAcceptSocket->LoopbackSocket = pConnectSocket->ThisSocket;
            WdfObjectReference(pAcceptSocket->LoopbackSocket);
            PIOSockSetFlag(pAcceptSocket, SOCK_LOOPBACK);

            PIOSockAcceptInitSocket(pAcceptSocket, pListenSocket);

            WdfRequestComplete(PendedRequest, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }
    }

    status = WdfMemoryCreateFromLookaside(pContext->AcceptMemoryList, &Memory);

    if (NT_SUCCESS(status))
    {
        PPIOSOCK_ACCEPT_ENTRY pAccept = WdfMemoryGetBuffer(Memory, NULL);

        pAccept->Memory = Memory;
        pAccept->ConnectSocket = pConnectSocket->ThisSocket;
        WdfObjectReference(pAccept->ConnectSocket);

        pAccept->dst_cid = pContext->Config.mooze_cid;
        pAccept->dst_port = pConnectSocket->src_port;
        pAccept->peer_buf_alloc = pConnectSocket->buf_alloc;
        pAccept->peer_fwd_cnt = pConnectSocket->fwd_cnt;

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

BOOLEAN
PIOSockLoopbackAcceptDequeue(
    IN PSOCKET_CONTEXT pAcceptSocket,
    IN PPIOSOCK_ACCEPT_ENTRY pAcceptEntry
)
{
    PSOCKET_CONTEXT pConnectSocket = GetSocketContext(pAcceptEntry->ConnectSocket);
    BOOLEAN bRes = TRUE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pConnectSocket->State == PIOSOCK_STATE_CONNECTING)
    {
        //link accepted socket to connecting one
        pAcceptSocket->LoopbackSocket = pAcceptEntry->ConnectSocket;//referenced on enqueue
        PIOSockSetFlag(pAcceptSocket, SOCK_LOOPBACK);
    }
    else
    {
        ASSERT(FALSE);
        //skip accept entry
        WdfObjectDereference(pAcceptEntry->ConnectSocket);
        bRes = FALSE;
    }

    return bRes;
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_not_held_(pDstSocket->StateLock)
__inline
LONG
PIOSockLoopbackTxSpaceUpdate(
    IN PSOCKET_CONTEXT pDstSocket,
    IN PSOCKET_CONTEXT pSrcSocket
)
{
    LONG uSpace;

    WdfSpinLockAcquire(pDstSocket->StateLock);

    pSrcSocket->last_fwd_cnt = pSrcSocket->fwd_cnt;

    pDstSocket->peer_buf_alloc = pSrcSocket->buf_alloc;
    pDstSocket->peer_fwd_cnt = pSrcSocket->fwd_cnt;
    uSpace = PIOSockTxHasSpace(pDstSocket);
    WdfSpinLockRelease(pDstSocket->StateLock);

    return uSpace;
}

static
NTSTATUS
PIOSockLoopbackConnect(
    PSOCKET_CONTEXT pSocket
)
{
    NTSTATUS        status;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PSOCKET_CONTEXT pListenSocket = PIOSockBoundFindByPort(pContext, pSocket->dst_port);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    ASSERT(PIOSockStateGet(pSocket) == PIOSOCK_STATE_CONNECTING);

    if (pListenSocket)
    {
        if(PIOSockStateGet(pListenSocket) == PIOSOCK_STATE_LISTEN)
        {
            //TODO: Increase buf_alloc for loopback?
            status = PIOSockLoopbackAcceptEnqueue(pListenSocket, pSocket);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "PIOSockLoopbackAcceptEnqueue failed: 0x%x\n", status);
            }
        }
        else
            status = STATUS_CONNECTION_RESET;
    }
    else
        status = STATUS_CONNECTION_RESET;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

_Requires_lock_not_held_(pSocket->StateLock)
static
NTSTATUS
PIOSockLoopbackHandleConnecting(
    IN PSOCKET_CONTEXT  pDestSocket,
    IN PSOCKET_CONTEXT  pSrcSocket,
    IN PHYZIO_VSOCK_OP  Op,
    IN BOOLEAN          bTxHasSpace
)
{
    WDFREQUEST  PendedRequest;
    NTSTATUS    status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = PIOSockPendedRequestGetLocked(pDestSocket, &PendedRequest);

    if (NT_SUCCESS(status))
    {
        if (PendedRequest == WDF_NO_HANDLE &&
            !PIOSockIsFlag(pDestSocket, SOCK_NON_BLOCK))
        {
            status = STATUS_CANCELLED;
        }
        else
        {
            switch (Op)
            {
            case PHYZIO_VSOCK_OP_RESPONSE:
                ASSERT(pSrcSocket);
                //link connecting socket to accepted one
                pDestSocket->LoopbackSocket = pSrcSocket->ThisSocket;
                WdfObjectReference(pDestSocket->LoopbackSocket);

                WdfSpinLockAcquire(pDestSocket->StateLock);
                PIOSockStateSet(pDestSocket, PIOSOCK_STATE_CONNECTED);
                PIOSockEventSetBit(pDestSocket, FD_CONNECT_BIT, STATUS_SUCCESS);
                if (bTxHasSpace)
                    PIOSockEventSetBit(pDestSocket, FD_WRITE_BIT, STATUS_SUCCESS);
                WdfSpinLockRelease(pDestSocket->StateLock);
                status = STATUS_SUCCESS;
                break;
            case PHYZIO_VSOCK_OP_INVALID:
                if (PendedRequest != WDF_NO_HANDLE)
                {
                    status = PIOSockPendedRequestSetLocked(pDestSocket, PendedRequest);
                    if (NT_SUCCESS(status))
                        PendedRequest = WDF_NO_HANDLE;
                }
                break;
            case PHYZIO_VSOCK_OP_RST:
                status = STATUS_CONNECTION_RESET;
                break;
            default:
                status = STATUS_CONNECTION_INVALID;
            }
        }
    }

    if (!NT_SUCCESS(status))
    {
        WdfSpinLockAcquire(pDestSocket->StateLock);
        PIOSockEventSetBit(pDestSocket, FD_CONNECT_BIT, status);
        PIOSockStateSet(pDestSocket, PIOSOCK_STATE_CLOSE);
        WdfSpinLockRelease(pDestSocket->StateLock);
        if (Op != PHYZIO_VSOCK_OP_RST)
            PIOSockSendReset(pDestSocket, FALSE);
    }

    if (PendedRequest)
        WdfRequestComplete(PendedRequest, status);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

static
NTSTATUS
PIOSockLoopbackHandleConnected(
    IN PSOCKET_CONTEXT  pDestSocket,
    IN PHYZIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN WDFREQUEST       Request OPTIONAL,
    IN ULONG            Length OPTIONAL,
    IN BOOLEAN          bTxHasSpace
)
{
    NTSTATUS    status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    switch (Op)
    {
    case PHYZIO_VSOCK_OP_RW:
        status = PIOSockRxRequestEnqueueCb(pDestSocket, Request, Length);
        if (NT_SUCCESS(status))
        {
            PIOSockEventSetBitLocked(pDestSocket, FD_READ_BIT, STATUS_SUCCESS);
            PIOSockReadDequeueCb(pDestSocket, NULL);
        }
        break;
    case PHYZIO_VSOCK_OP_CREDIT_UPDATE:
        if (bTxHasSpace)
        {
            PIOSockEventSetBitLocked(pDestSocket, FD_WRITE_BIT, STATUS_SUCCESS);
        }
        break;
    case PHYZIO_VSOCK_OP_SHUTDOWN:
        if (PIOSockShutdownFromPeer(pDestSocket,
            Flags & PHYZIO_VSOCK_SHUTDOWN_MASK) &&
            !PIOSockRxHasData(pDestSocket) &&
            !PIOSockIsDone(pDestSocket))
        {
            PIOSockSendReset(pDestSocket, FALSE);
            PIOSockDoClose(pDestSocket);
        }
        break;
    case PHYZIO_VSOCK_OP_RST:
        PIOSockDoClose(pDestSocket);
        PIOSockEventSetBitLocked(pDestSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

static
VOID
PIOSockLoopbackHandleDisconnecting(
    IN PSOCKET_CONTEXT  pDestSocket,
    IN PHYZIO_VSOCK_OP  Op
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (Op == PHYZIO_VSOCK_OP_RST)
    {
        PIOSockDoClose(pDestSocket);
        //         if (pDestSocket->PeerShutdown & PHYZIO_VSOCK_SHUTDOWN_MASK != PHYZIO_VSOCK_SHUTDOWN_MASK)
        //         {
        //             PIOSockEventSetBit(pDestSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
        //         }
    }
}

NTSTATUS
PIOSockLoopbackTxEnqueue(
    IN PSOCKET_CONTEXT  pSocket,
    IN PHYZIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN WDFREQUEST       Request OPTIONAL,
    IN ULONG            Length OPTIONAL
)
{
    NTSTATUS            status;
    PDEVICE_CONTEXT     pContext = GetDeviceContextFromSocket(pSocket);
    PSOCKET_CONTEXT     pLoopbackSocket = (pSocket->LoopbackSocket != WDF_NO_HANDLE) ?
        GetSocketContext(pSocket->LoopbackSocket) : NULL;
    ULONG               uCredit;
    BOOLEAN             bTxHasSpace;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (Request != WDF_NO_HANDLE)
    {
        ASSERT(Length);
        status = PIOSockStateValidate(pSocket, TRUE);
        if (status == STATUS_REMOTE_DISCONNECT)
            status = STATUS_LOCAL_DISCONNECT;

        if (!NT_SUCCESS(status))
        {

            TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "PIOSockStateValidate failed, status: 0x%x\n", status);
            return status;
        }
    }

    uCredit = PIOSockTxGetCredit(pSocket, Length);
    if (Length && !uCredit)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "PIOSockTxGetCredit failed\n");
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!pLoopbackSocket)
    {
        //only connect request allowed without LoopbackSocket
        if (Op == PHYZIO_VSOCK_OP_REQUEST)
        {
            status = PIOSockLoopbackConnect(pSocket);
        }
        else
        {
            ASSERT(FALSE);
            TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "no Loopback socket\n");
            status = STATUS_UNSUCCESSFUL;
        }
        return status;
    }

    bTxHasSpace = !!PIOSockLoopbackTxSpaceUpdate(pLoopbackSocket, pSocket);

    switch (pLoopbackSocket->State)
    {
    case PIOSOCK_STATE_CONNECTING:
        status = PIOSockLoopbackHandleConnecting(pLoopbackSocket, pSocket, Op, bTxHasSpace);
        break;

    case PIOSOCK_STATE_CONNECTED:
        status = PIOSockLoopbackHandleConnected(pLoopbackSocket, Op, Flags, Request, uCredit, bTxHasSpace);
        break;

    case PIOSOCK_STATE_CLOSING:
        PIOSockLoopbackHandleDisconnecting(pLoopbackSocket, Op);
        status = STATUS_SUCCESS;
        break;
    default:
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid socket state for Loopback Rx command\n");
        status = STATUS_CONNECTION_INVALID;
    }

    if (!NT_SUCCESS(status))
    {
        PIOSockTxPutCredit(pSocket, uCredit);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}
