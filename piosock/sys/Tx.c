/*
 * Placeholder for the Send path functions
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
#include "Tx.tmh"
#endif

EVT_WDF_IO_QUEUE_IO_WRITE   PIOSockWrite;
EVT_WDF_IO_QUEUE_IO_STOP    PIOSockWriteIoStop;
EVT_WDF_REQUEST_CANCEL      PIOSockTxEnqueueCancel;
EVT_WDF_TIMER               PIOSockTxTimerFunc;


#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PIOSockTxVqInit)
#pragma alloc_text (PAGE, PIOSockTxVqCleanup)
#pragma alloc_text (PAGE, PIOSockWriteQueueInit)
#endif

#define PIOSOCK_DMA_TX_PAGES BYTES_TO_PAGES(PHYZIO_VSOCK_MAX_PKT_BUF_SIZE)

typedef struct _PIOSOCK_TX_PKT
{
    PHYZIO_VSOCK_HDR Header;
    PHYSICAL_ADDRESS PhysAddr; //packet addr
    WDFDMATRANSACTION Transaction;
    union
    {
        BYTE IndirectDescs[SIZE_OF_SINGLE_INDIRECT_DESC * (1 + PIOSOCK_DMA_TX_PAGES)]; //Header + sglist
        struct
        {
            LIST_ENTRY ListEntry;
            WDFREQUEST Request;
        };
    };
}PIOSOCK_TX_PKT, *PPIOSOCK_TX_PKT;

typedef struct _PIOSOCK_TX_ENTRY {
    LIST_ENTRY      ListEntry;
    WDFMEMORY       Memory;
    WDFREQUEST      Request;
    WDFFILEOBJECT   Socket;

    ULONG64         dst_cid;
    ULONG32         src_port;
    ULONG32         dst_port;

    ULONG32         len;
    USHORT          op;
    BOOLEAN         reply;
    ULONG32         flags;
    USHORT          type;

    LONGLONG        Timeout; //100ns
}PIOSOCK_TX_ENTRY, *PPIOSOCK_TX_ENTRY;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIOSOCK_TX_ENTRY, GetRequestTxContext);

_Requires_lock_not_held_(pContext->TxLock)
VOID
PIOSockTxDequeue(
    PDEVICE_CONTEXT pContext
);

//////////////////////////////////////////////////////////////////////////

VOID
PIOSockTxVqCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->TxVq);
    if (pContext->TxPktSliced)
    {
        pContext->TxPktSliced->destroy(pContext->TxPktSliced);
        pContext->TxPktSliced = NULL;
        pContext->TxPktNum = 0;
    }
    pContext->TxVq = NULL;
}

NTSTATUS
PIOSockTxVqInit(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    USHORT uNumEntries;
    ULONG uRingSize, uHeapSize, uBufferSize;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    status = phyzio_query_queue_allocation(&pContext->VDevice.PIODevice, PIOSOCK_VQ_TX, &uNumEntries, &uRingSize, &uHeapSize);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "phyzio_query_queue_allocation(PIOSOCK_VQ_TX) failed\n");
        pContext->TxVq = NULL;
        return status;
    }

    uBufferSize = sizeof(PIOSOCK_TX_PKT) * uNumEntries;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Allocating sliced buffer of %u bytes for %u Tx packets\n",
        uBufferSize, uNumEntries);

    pContext->TxPktSliced = PhyzIOWdfDeviceAllocDmaMemorySliced(&pContext->VDevice.PIODevice,
        uBufferSize, sizeof(PIOSOCK_TX_PKT));

    ASSERT(pContext->TxPktSliced);
    if (!pContext->TxPktSliced)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "PhyzIOWdfDeviceAllocDmaMemorySliced(%u butes for TxPackets) failed\n", uBufferSize);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    pContext->TxPktNum = uNumEntries;

    return status;
}

_Requires_lock_held_(pContext->TxLock)
static
PPIOSOCK_TX_PKT
PIOSockTxPktAlloc(
    IN PDEVICE_CONTEXT pContext,
    IN PPIOSOCK_TX_ENTRY pTxEntry
)
{
    PHYSICAL_ADDRESS PA;
    PPIOSOCK_TX_PKT pPkt;

    ASSERT(pContext->TxPktSliced);
    pPkt = pContext->TxPktSliced->get_slice(pContext->TxPktSliced, &PA);
    if (pPkt)
    {
        pPkt->PhysAddr = PA;
        pPkt->Transaction = WDF_NO_HANDLE;
        if (pTxEntry->Socket != WDF_NO_HANDLE)
        {
            PIOSockRxIncTxPkt(GetSocketContext(pTxEntry->Socket), &pPkt->Header);
        }
        pPkt->Header.src_cid = pContext->Config.mooze_cid;
        pPkt->Header.dst_cid = pTxEntry->dst_cid;
        pPkt->Header.src_port = pTxEntry->src_port;
        pPkt->Header.dst_port = pTxEntry->dst_port;
        pPkt->Header.len = pTxEntry->len;
        pPkt->Header.type = pTxEntry->type;
        pPkt->Header.op = pTxEntry->op;
        pPkt->Header.flags = pTxEntry->flags;
    }
    return pPkt;
}

_Requires_lock_held_(pContext->TxLock)
__inline
VOID
PIOSockTxPktFree(
    IN PDEVICE_CONTEXT pContext,
    IN PPIOSOCK_TX_PKT pPkt
)
{
    pContext->TxPktSliced->return_slice(pContext->TxPktSliced, pPkt);
}

_Requires_lock_held_(pContext->TxLock)
static
BOOLEAN
PIOSockTxPktInsert(
    IN PDEVICE_CONTEXT pContext,
    IN PPIOSOCK_TX_PKT pPkt,
    IN PPHYZIO_DMA_TRANSACTION_PARAMS pParams OPTIONAL
)
{
    PIOSOCK_SG_DESC sg[PIOSOCK_DMA_TX_PAGES + 1];
    ULONG uElements = 1, uPktLen = 0;
    PVOID va_indirect = NULL;
    ULONGLONG phys_indirect = 0;
    PSCATTER_GATHER_LIST SgList = NULL;

    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (pParams)
    {
        ASSERT(pParams->transaction);
        pPkt->Transaction = pParams->transaction;
        SgList = pParams->sgList;
    }

    sg[0].length = sizeof(PHYZIO_VSOCK_HDR);
    sg[0].physAddr.QuadPart = pPkt->PhysAddr.QuadPart + FIELD_OFFSET(PIOSOCK_TX_PKT, Header);

    if (SgList)
    {
        ULONG i;

        ASSERT(SgList->NumberOfElements <= PIOSOCK_DMA_TX_PAGES);
        for (i = 0; i < SgList->NumberOfElements; i++)
        {
            sg[i + 1].length = SgList->Elements[i].Length;
            sg[i + 1].physAddr = SgList->Elements[i].Address;

            uPktLen += SgList->Elements[i].Length;
            if (uPktLen >= pPkt->Header.len)
            {
                sg[++i].length -= uPktLen - pPkt->Header.len;
                break;
            }
        }
        uElements += i;
    }

    if (uElements > 1)
    {
        va_indirect = &pPkt->IndirectDescs;
        phys_indirect = pPkt->PhysAddr.QuadPart + FIELD_OFFSET(PIOSOCK_TX_PKT, IndirectDescs);
    }


    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "Send packet %!Op! (%d:%d --> %d:%d), len: %d, flags: %d, buf_alloc: %d, fwd_cnt: %d\n",
        pPkt->Header.op,
        (ULONG)pPkt->Header.src_cid, pPkt->Header.src_port,
        (ULONG)pPkt->Header.dst_cid, pPkt->Header.dst_port,
        pPkt->Header.len, pPkt->Header.flags, pPkt->Header.buf_alloc, pPkt->Header.fwd_cnt);

    ret = virtqueue_add_buf(pContext->TxVq, sg, uElements, 0, pPkt, va_indirect, phys_indirect);

    ASSERT(ret >= 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Error adding buffer to queue (ret = %d)\n", ret);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return TRUE;
}

VOID
PIOSockTxVqProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PPIOSOCK_TX_PKT pPkt;
    UINT len;
    LIST_ENTRY CompletionList, *CurrentItem;
    NTSTATUS status;
    WDFREQUEST Request;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->TxLock);
    do
    {
        virtqueue_disable_cb(pContext->TxVq);

        while ((pPkt = (PPIOSOCK_TX_PKT)virtqueue_get_buf(pContext->TxVq, &len)) != NULL)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "Free packet %!Op! (%d:%d --> %d:%d)\n",
                pPkt->Header.op, (ULONG)pPkt->Header.src_cid, pPkt->Header.src_port,
                (ULONG)pPkt->Header.dst_cid, pPkt->Header.dst_port);

            if (pPkt->Transaction != WDF_NO_HANDLE)
            {
                pPkt->Request = WdfDmaTransactionGetRequest(pPkt->Transaction);

                //postpone to complete transaction
                InsertTailList(&CompletionList, &pPkt->ListEntry);
            }
            else
            {
                //just free packet
                pPkt->Request = WDF_NO_HANDLE;
                PIOSockTxPktFree(pContext, pPkt);
            }
        }
    } while (!virtqueue_enable_cb(pContext->TxVq));

    WdfSpinLockRelease(pContext->TxLock);

    for (CurrentItem = CompletionList.Flink;
        CurrentItem != &CompletionList;
        CurrentItem = CurrentItem->Flink)
    {
        pPkt = CONTAINING_RECORD(CurrentItem, PIOSOCK_TX_PKT, ListEntry);

        PhyzIOWdfDeviceDmaTxComplete(&pContext->VDevice.PIODevice, pPkt->Transaction);
        if (pPkt->Request != WDF_NO_HANDLE)
            WdfRequestCompleteWithInformation(pPkt->Request, STATUS_SUCCESS, pPkt->Header.len);
    };

    //cleanup pkt locked
    WdfSpinLockAcquire(pContext->TxLock);
    while (!IsListEmpty(&CompletionList))
    {
        PIOSockTxPktFree(pContext, CONTAINING_RECORD(RemoveHeadList(&CompletionList), PIOSOCK_TX_PKT, ListEntry));
    };
    WdfSpinLockRelease(pContext->TxLock);

    PIOSockTxDequeue(pContext);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static
BOOLEAN
PIOSockTxDequeueCallback(
    IN PPHYZIO_DMA_TRANSACTION_PARAMS pParams
)
{
    PPIOSOCK_TX_PKT pPkt = pParams->param1;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket((PSOCKET_CONTEXT)pParams->param2);
    BOOLEAN         bRes;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pContext->TxLock);
    bRes = PIOSockTxPktInsert(pContext, pPkt, pParams);
    if (!bRes)
        PIOSockTxPktFree(pContext, pPkt);
    WdfSpinLockRelease(pContext->TxLock);

    if (!bRes)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PIOSockTxPktInsert failed\n");

        PhyzIOWdfDeviceDmaTxComplete(&pContext->VDevice.PIODevice, pParams->transaction);
        WdfRequestComplete(pParams->req, STATUS_INSUFFICIENT_RESOURCES);
    }
    else
    {
        virtqueue_kick(pContext->TxVq);
    }

    return bRes;
}

static
VOID
PIOSockTxDequeue(
    PDEVICE_CONTEXT pContext
)
{
    static volatile LONG    lInProgress;
    BOOLEAN                 bKick = FALSE, bReply, bRestartRx = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (InterlockedCompareExchange(&lInProgress, 1, 0) == 1)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "Another instance of PIOSockTxDequeue already running, stop tx dequeue\n");
        return; //one running instance allowed
    }

    WdfSpinLockAcquire(pContext->TxLock);

    while (!IsListEmpty(&pContext->TxList))
    {
        PPIOSOCK_TX_ENTRY   pTxEntry = CONTAINING_RECORD(pContext->TxList.Flink,
            PIOSOCK_TX_ENTRY, ListEntry);
        PPIOSOCK_TX_PKT     pPkt = PIOSockTxPktAlloc(pContext, pTxEntry);
        NTSTATUS            status;

        //can't allocate packet, stop dequeue
        if (!pPkt)
            break;

        RemoveHeadList(&pContext->TxList);

        bReply = pTxEntry->reply;

        if (pTxEntry->Request)
        {
            ASSERT(pTxEntry->Socket != WDF_NO_HANDLE);
            ASSERT(pTxEntry->len && !bReply);
            status = WdfRequestUnmarkCancelable(pTxEntry->Request);

            if (NT_SUCCESS(status))
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(pTxEntry->Socket);
                PHYZIO_DMA_TRANSACTION_PARAMS params = { 0 };

                if (pTxEntry->Timeout)
                    PIOSockTimerDeref(&pContext->TxTimer, TRUE);

                WdfSpinLockRelease(pContext->TxLock);

                status = PIOSockStateValidate(pSocket, TRUE);
                if (status == STATUS_REMOTE_DISCONNECT)
                    status = STATUS_LOCAL_DISCONNECT;

                if (NT_SUCCESS(status))
                {
                    params.req = pTxEntry->Request;

                    params.param1 = pPkt;
                    params.param2 = pSocket;

                    //create transaction
                    if (!PhyzIOWdfDeviceDmaTxAsync(&pContext->VDevice.PIODevice, &params, PIOSockTxDequeueCallback))
                    {
                        if (params.transaction)
                            PhyzIOWdfDeviceDmaTxComplete(&pContext->VDevice.PIODevice, params.transaction);
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    }
                }

                if (!NT_SUCCESS(status))
                    WdfRequestComplete(pTxEntry->Request, status);

                WdfSpinLockAcquire(pContext->TxLock);

                if (!NT_SUCCESS(status))
                    PIOSockTxPktFree(pContext, pPkt);
            }
            else
            {
                ASSERT(status == STATUS_CANCELLED);
                TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Write request canceled\n");
            }
        }
        else
        {
            ASSERT(pTxEntry->Memory != WDF_NO_HANDLE);
            if (PIOSockTxPktInsert(pContext, pPkt, NULL))
            {
                bKick = TRUE;
                WdfObjectDelete(pTxEntry->Memory);
            }
            else
            {
                ASSERT(FALSE);
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PIOSockTxPktInsert failed\n");
                InsertHeadList(&pContext->TxList, &pTxEntry->ListEntry);
                PIOSockTxPktFree(pContext, pPkt);
                break;
            }

            if (bReply)
            {
                LONG lVal = --pContext->TxQueuedReply;

                /* Do we now have resources to resume rx processing? */
                bRestartRx = (lVal + 1 == pContext->RxPktNum);
            }
        }
    }

    InterlockedExchange(&lInProgress, 0);

    WdfSpinLockRelease(pContext->TxLock);

    if (bKick)
        virtqueue_kick(pContext->TxVq);

    if (bRestartRx)
        PIOSockRxVqProcess(pContext);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

VOID
PIOSockTxCancel(
    PDEVICE_CONTEXT pContext,
    PSOCKET_CONTEXT pSocket,
    NTSTATUS        Status
)
{
    LONG lCnt = 0;
    PLIST_ENTRY CurrentEntry;
    LIST_ENTRY  CompletionList;
    BOOLEAN     bProcessVq = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->TxLock);

    for (CurrentEntry = pContext->TxList.Flink;
        CurrentEntry != &pContext->TxList;
        CurrentEntry = CurrentEntry->Flink)
    {
        PPIOSOCK_TX_ENTRY   pTxEntry = CONTAINING_RECORD(CurrentEntry,
            PIOSOCK_TX_ENTRY, ListEntry);

        if (pTxEntry->Socket == pSocket->ThisSocket)
        {
            CurrentEntry = CurrentEntry->Blink;

            RemoveEntryList(&pTxEntry->ListEntry);
            InsertTailList(&CompletionList, &pTxEntry->ListEntry); //complete later

            if (pTxEntry->Timeout)
                PIOSockTimerDeref(&pContext->TxTimer, TRUE);

            if (pTxEntry->reply)
                ++lCnt;
        }
    }

    if (lCnt)
    {
        pContext->TxQueuedReply -= lCnt;
        if (pContext->TxQueuedReply + lCnt >= (LONG)pContext->RxPktNum &&
            pContext->TxQueuedReply < (LONG)pContext->RxPktNum)
            bProcessVq = TRUE;
    }

    WdfSpinLockRelease(pContext->TxLock);

    while (!IsListEmpty(&CompletionList))
    {
        PPIOSOCK_TX_ENTRY   pTxEntry = CONTAINING_RECORD(RemoveHeadList(&CompletionList),
            PIOSOCK_TX_ENTRY, ListEntry);

        if (pTxEntry->Request)
            WdfRequestComplete(pTxEntry->Request, Status);

        WdfObjectDelete(pTxEntry->Memory);
    }

    if (bProcessVq)
    {
        PIOSockRxVqProcess(pContext);
    }
}

//////////////////////////////////////////////////////////////////////////
static
VOID
PIOSockTxEnqueueCancel(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PPIOSOCK_TX_ENTRY pTxEntry = GetRequestTxContext(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pContext->TxLock);
    RemoveEntryList(&pTxEntry->ListEntry);
    PIOSockTxPutCredit(pSocket, pTxEntry->len);

    if (pTxEntry->Timeout)
        PIOSockTimerDeref(&pContext->TxTimer, TRUE);

    WdfSpinLockRelease(pContext->TxLock);

    WdfRequestComplete(Request, STATUS_CANCELLED);
}

NTSTATUS
PIOSockTxEnqueue(
    IN PSOCKET_CONTEXT  pSocket,
    IN PHYZIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN BOOLEAN          Reply,
    IN WDFREQUEST       Request OPTIONAL
)
{
    NTSTATUS            status;
    PDEVICE_CONTEXT     pContext = GetDeviceContextFromSocket(pSocket);
    PPIOSOCK_TX_ENTRY   pTxEntry = NULL;
    ULONG               uLen;
    WDFMEMORY           Memory = WDF_NO_HANDLE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (IsLoopbackSocket(pSocket))
        return PIOSockLoopbackTxEnqueue(pSocket, Op, Flags, Request,
        (Request == WDF_NO_HANDLE) ? 0 : GetRequestTxContext(Request)->len);

    if (Request == WDF_NO_HANDLE)
    {

        status = WdfMemoryCreateFromLookaside(pContext->TxMemoryList, &Memory);
        if (NT_SUCCESS(status))
        {
            pTxEntry = WdfMemoryGetBuffer(Memory, NULL);
            pTxEntry->Memory = Memory;
            pTxEntry->Request = WDF_NO_HANDLE;
            pTxEntry->len = 0;
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
        }
    }
    else
    {
        status = PIOSockStateValidate(pSocket, TRUE);
        if (status == STATUS_REMOTE_DISCONNECT)
            status = STATUS_LOCAL_DISCONNECT;

        if (NT_SUCCESS(status))
        {
            pTxEntry = GetRequestTxContext(Request);
            pTxEntry->Request = Request;
            pTxEntry->Memory = WDF_NO_HANDLE;
        }
    }

    if (!NT_SUCCESS(status))
        return status;

    ASSERT(pTxEntry);
    pTxEntry->Socket = pSocket->ThisSocket;
    pTxEntry->src_port = pSocket->src_port;
    pTxEntry->dst_cid = pSocket->dst_cid;
    pTxEntry->dst_port = pSocket->dst_port;
    pTxEntry->type = (USHORT)pSocket->type;
    pTxEntry->op = Op;
    pTxEntry->reply = Reply;
    pTxEntry->flags = Flags;
    pTxEntry->Timeout = 0;

    uLen = PIOSockTxGetCredit(pSocket, pTxEntry->len);
    if (pTxEntry->len && !uLen)
    {
        ASSERT(pTxEntry->Request);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "No free space on peer\n");

        return STATUS_BUFFER_TOO_SMALL;
    }

    WdfSpinLockAcquire(pContext->TxLock);

    if (Request != WDF_NO_HANDLE)
    {
        pTxEntry->len = uLen;
        status = WdfRequestMarkCancelableEx(Request, PIOSockTxEnqueueCancel);
        if (!NT_SUCCESS(status))
        {
            ASSERT(status == STATUS_CANCELLED);
            TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Write request canceled: 0x%x\n", status);

            PIOSockTxPutCredit(pSocket, pTxEntry->len);
            WdfSpinLockRelease(pContext->TxLock);
            return status; //caller completes failed requests
        }

        if (pSocket->SendTimeout != LONG_MAX)
        {
            pTxEntry->Timeout = WDF_ABS_TIMEOUT_IN_MS(pSocket->SendTimeout);
            PIOSockTimerStart(&pContext->TxTimer, pTxEntry->Timeout);
        }
    }

    if (pTxEntry->reply)
        pContext->TxQueuedReply++;

    InsertTailList(&pContext->TxList, &pTxEntry->ListEntry);
    WdfSpinLockRelease(pContext->TxLock);

    PIOSockTxVqProcess(pContext);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return status;
}

//////////////////////////////////////////////////////////////////////////
static
VOID
PIOSockWrite(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{
    PDEVICE_CONTEXT         pContext = GetDeviceContext(WdfIoQueueGetDevice(Queue));
    NTSTATUS                status;
    WDF_OBJECT_ATTRIBUTES   attributes;
    PSOCKET_CONTEXT         pSocket = GetSocketContextFromRequest(Request);
    PPIOSOCK_TX_ENTRY       pRequest;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (IsControlRequest(Request))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Invalid socket %d for write\n", pSocket->SocketId);

        WdfRequestComplete(Request, STATUS_NOT_SOCKET);
        return;
    }

    if (Length > PHYZIO_VSOCK_MAX_PKT_BUF_SIZE)
        Length = PHYZIO_VSOCK_MAX_PKT_BUF_SIZE;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        PIOSOCK_TX_ENTRY
    );
    status = WdfObjectAllocateContext(
        Request,
        &attributes,
        &pRequest
    );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfObjectAllocateContext failed: 0x%x\n", status);

        WdfRequestComplete(Request, status);
        return;
    }
    else
    {
        pRequest->len = (ULONG32)Length;
    }

    status = PIOSockSendWrite(pSocket, Request);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "PIOSockSendWrite failed for socket %d: 0x%x\n",
            pSocket->SocketId, status);
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

static
VOID
PIOSockWriteIoStop(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags
)
{
    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        WdfRequestStopAcknowledge(Request, FALSE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        if (ActionFlags & WdfRequestStopRequestCancelable)
        {
            if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
            {
                WdfRequestComplete(Request, STATUS_CANCELLED);
            }
        }
    }
}

NTSTATUS
PIOSockWriteQueueInit(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT              pContext = GetDeviceContext(hDevice);
    WDF_IO_QUEUE_CONFIG          queueConfig;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES lockAttributes, memAttributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = pContext->ThisDevice;

    status = WdfSpinLockCreate(
        &lockAttributes,
        &pContext->TxLock
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfSpinLockCreate failed: 0x%x\n", status);
        return FALSE;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&memAttributes);
    memAttributes.ParentObject = pContext->ThisDevice;

    status = WdfLookasideListCreate(&memAttributes,
        sizeof(PIOSOCK_TX_ENTRY), NonPagedPoolNx,
        &memAttributes, PIOSOCK_DRIVER_MEMORY_TAG,
        &pContext->TxMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfLookasideListCreate failed: 0x%x\n", status);
        return status;
    }

    InitializeListHead(&pContext->TxList);

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchParallel
    );

    queueConfig.EvtIoWrite = PIOSockWrite;
    queueConfig.EvtIoStop = PIOSockWriteIoStop;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->WriteQueue
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "WdfIoQueueCreate failed (Write Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice,
        pContext->WriteQueue,
        WdfRequestTypeWrite);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "WdfDeviceConfigureRequestDispatching failed (Write Queue): 0x%x\n", status);
        return status;
    }

    PIOSockTimerCreate(&pContext->TxTimer, pContext->ThisDevice, PIOSockTxTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "PIOSockTimerCreate failed (Write Queue): 0x%x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
NTSTATUS
PIOSockSendResetNoSock(
    IN PDEVICE_CONTEXT pContext,
    IN PPHYZIO_VSOCK_HDR pHeader
)
{
    PPIOSOCK_TX_ENTRY   pTxEntry;
    NTSTATUS            status;
    WDFMEMORY           Memory;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    /* Send RST only if the original pkt is not a RST pkt */
    if (pHeader->op == PHYZIO_VSOCK_OP_RST)
        return STATUS_SUCCESS;

    status = WdfMemoryCreateFromLookaside(pContext->TxMemoryList, &Memory);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
        return status;
    }

    pTxEntry = WdfMemoryGetBuffer(Memory, NULL);
    pTxEntry->Memory = Memory;
    pTxEntry->Request = WDF_NO_HANDLE;
    pTxEntry->len = 0;

    pTxEntry->src_port = pHeader->dst_port;
    pTxEntry->dst_cid = pHeader->src_cid;
    pTxEntry->dst_port = pHeader->src_port;
    pTxEntry->type = pHeader->type;

    pTxEntry->Socket = WDF_NO_HANDLE;
    pTxEntry->op = PHYZIO_VSOCK_OP_RST;
    pTxEntry->reply = FALSE;
    pTxEntry->flags = 0;

    WdfSpinLockAcquire(pContext->TxLock);
    InsertTailList(&pContext->TxList, &pTxEntry->ListEntry);
    WdfSpinLockRelease(pContext->TxLock);

    PIOSockTxVqProcess(pContext);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}

//////////////////////////////////////////////////////////////////////////
VOID
PIOSockTxTimerFunc(
    WDFTIMER Timer
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfTimerGetParentObject(Timer));
    PLIST_ENTRY CurrentEntry;
    LONGLONG Timeout = LONGLONG_MAX;
    LIST_ENTRY CompletionList;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->TxLock);

    for (CurrentEntry = pContext->TxList.Flink;
        CurrentEntry != &pContext->TxList;
        CurrentEntry = CurrentEntry->Flink)
    {
        PPIOSOCK_TX_ENTRY pTxEntry = CONTAINING_RECORD(CurrentEntry,
            PIOSOCK_TX_ENTRY, ListEntry);

        if (pTxEntry->Timeout)
        {
            if (pTxEntry->Timeout <= pContext->TxTimer.Timeout + PIOSOCK_TIMER_TOLERANCE)
            {
                CurrentEntry = CurrentEntry->Blink;
                RemoveEntryList(&pTxEntry->ListEntry);

                if (pTxEntry->Request)
                {
                    NTSTATUS status = WdfRequestUnmarkCancelable(pTxEntry->Request);
                    if (NT_SUCCESS(status))
                    {
                        InsertTailList(&CompletionList, &pTxEntry->ListEntry);
                        PIOSockTimerDeref(&pContext->TxTimer, FALSE);
                    }
                    else
                    {
                        ASSERT(status == STATUS_CANCELLED);
                        TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Write request canceled\n");
                    }
                }
                else
                {
                    ASSERT(pTxEntry->Memory);
                    WdfObjectDelete(pTxEntry->Memory);
                    PIOSockTimerDeref(&pContext->TxTimer, FALSE);
                }
            }
            else
            {
                pTxEntry->Timeout -= pContext->TxTimer.Timeout;

                if (pTxEntry->Timeout < Timeout)
                    Timeout = pTxEntry->Timeout;
            }
        }
    }

    PIOSockTimerSet(&pContext->TxTimer, Timeout);

    WdfSpinLockRelease(pContext->TxLock);

    while (!IsListEmpty(&CompletionList))
    {
        PPIOSOCK_TX_ENTRY pTxEntry = CONTAINING_RECORD(RemoveHeadList(&CompletionList),
            PIOSOCK_TX_ENTRY, ListEntry);

        WdfRequestComplete(pTxEntry->Request, STATUS_TIMEOUT);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}