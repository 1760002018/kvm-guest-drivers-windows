/*
 * Placeholder for the Recv path functions
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
#include "Rx.tmh"
#endif

#define PIOSOCK_DMA_RX_PAGES    1           //contiguous buffer

#define PIOSOCK_BYTES_TO_MERGE  128         //max bytes to merge with prev buffer

#define PIOSOCK_CB_ENTRIES(n) ((n)+(n>>1))  //default chained buffer queue size

 //Chained Buffer entry
typedef struct _PIOSOCK_RX_CB
{
    union {
        SINGLE_LIST_ENTRY   FreeListEntry;
        LIST_ENTRY          ListEntry;      //Request buffer list
    };

    PVOID               BufferVA;   //common buffer of PHYZIO_VSOCK_DEFAULT_RX_BUF_SIZE bytes
    PHYSICAL_ADDRESS    BufferPA;   //common buffer PA

    ULONG               DataLen;    //Valid data len (pkt.header.len)
    WDFREQUEST          Request;    //Write request for loopback
    WDFMEMORY           Memory;
}PIOSOCK_RX_CB, *PPIOSOCK_RX_CB;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIOSOCK_RX_CB, GetRequestRxCb);

typedef struct _PIOSOCK_RX_PKT
{
    PHYZIO_VSOCK_HDR    Header;
    PPIOSOCK_RX_CB      Buffer;     //Chained buffer
    union {
        BYTE IndirectDescs[SIZE_OF_SINGLE_INDIRECT_DESC * (1 + PIOSOCK_DMA_RX_PAGES)]; //Header + buffer
        SINGLE_LIST_ENTRY   RxPktListEntry;
        LIST_ENTRY          CompletionListEntry;
    };
}PIOSOCK_RX_PKT, *PPIOSOCK_RX_PKT;

typedef union _PIOSOCK_RX_CONTEXT {
    struct
    {
        LONGLONG        Timeout; //100ns
        ULONG           Counter;
    };
    struct
    {
        LIST_ENTRY ListEntry;
        WDFREQUEST ThisRequest;
    };
}PIOSOCK_RX_CONTEXT, *PPIOSOCK_RX_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIOSOCK_RX_CONTEXT, GetRequestRxContext);

BOOLEAN
PIOSockRxCbInit(
    IN PDEVICE_CONTEXT  pContext
);

BOOLEAN
PIOSockRxCbAdd(
    IN PDEVICE_CONTEXT pContext
);

VOID
PIOSockRxCbCleanup(
    IN PDEVICE_CONTEXT pContext
);

EVT_WDF_IO_QUEUE_IO_READ    PIOSockRead;
EVT_WDF_IO_QUEUE_IO_DEFAULT PIOSockReadSocketIoDefault;
EVT_WDF_IO_QUEUE_IO_STOP    PIOSockReadSocketIoStop;
EVT_WDF_REQUEST_CANCEL      PIOSockRxRequestCancelCb;
EVT_WDF_TIMER               PIOSockReadTimerFunc;


#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PIOSockRxCbInit)
#pragma alloc_text (PAGE, PIOSockRxCbAdd)
#pragma alloc_text (PAGE, PIOSockRxCbCleanup)
#pragma alloc_text (PAGE, PIOSockRxVqInit)
#pragma alloc_text (PAGE, PIOSockReadQueueInit)
#pragma alloc_text (PAGE, PIOSockReadSocketQueueInit)
#endif

//////////////////////////////////////////////////////////////////////////
_Requires_lock_held_(pContext->RxLock)
__inline
VOID
PIOSockRxCbPush(
    PDEVICE_CONTEXT pContext,
    PPIOSOCK_RX_CB pCb
)
{
    PushEntryList(&pContext->RxCbBuffers, &pCb->FreeListEntry);
}

_Requires_lock_not_held_(pContext->RxLock)
__inline
VOID
PIOSockRxCbPushLocked(
    PDEVICE_CONTEXT pContext,
    PPIOSOCK_RX_CB pCb
)
{
    WdfSpinLockAcquire(pContext->RxLock);
    PIOSockRxCbPush(pContext, pCb);
    WdfSpinLockRelease(pContext->RxLock);
}

_Requires_lock_held_(pContext->RxLock)
__inline
PPIOSOCK_RX_CB
PIOSockRxCbPop(
    IN PDEVICE_CONTEXT pContext
)
{
    PSINGLE_LIST_ENTRY pListEntry = PopEntryList(&pContext->RxCbBuffers);

    if (pListEntry)
    {
        return CONTAINING_RECORD(pListEntry, PIOSOCK_RX_CB, FreeListEntry);
    }

    return NULL;
}

__inline
VOID
PIOSockRxCbFree(
    IN PDEVICE_CONTEXT pContext,
    IN PPIOSOCK_RX_CB pCb
)
{
    ASSERT(pCb && pCb->BufferVA);

    ASSERT(pCb->BufferPA.QuadPart && pCb->Request == WDF_NO_HANDLE);

    if (pCb->BufferPA.QuadPart)
        PhyzIOWdfDeviceFreeDmaMemory(&pContext->VDevice.PIODevice, pCb->BufferVA);

    WdfObjectDelete(pCb->Memory);
}

static
BOOLEAN
PIOSockRxCbAdd(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS        status;
    PPIOSOCK_RX_CB  pCb;
    WDFMEMORY       Memory;
    BOOLEAN         bRes = FALSE;

    PAGED_CODE();

    status = WdfMemoryCreateFromLookaside(pContext->RxCbBufferMemoryList, &Memory);
    if (NT_SUCCESS(status))
    {
        pCb = WdfMemoryGetBuffer(Memory, NULL);

        RtlZeroMemory(pCb, sizeof(*pCb));

        pCb->Memory = Memory;
        pCb->BufferVA = PhyzIOWdfDeviceAllocDmaMemory(&pContext->VDevice.PIODevice,
            PHYZIO_VSOCK_DEFAULT_RX_BUF_SIZE, PIOSOCK_DRIVER_MEMORY_TAG);

        if (!pCb->BufferVA)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                "PhyzIOWdfDeviceAllocDmaMemory(%u bytes for Rx buffer) failed\n", PHYZIO_VSOCK_DEFAULT_RX_BUF_SIZE);
            WdfObjectDelete(pCb->Memory);
        }
        else
        {
            pCb->BufferPA = PhyzIOWdfDeviceGetPhysicalAddress(&pContext->VDevice.PIODevice,
                pCb->BufferVA);

            ASSERT(pCb->BufferPA.QuadPart);

            if (!pCb->BufferPA.QuadPart)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PhyzIOWdfDeviceGetPhysicalAddress failed\n");
                PIOSockRxCbFree(pContext, pCb);
            }
            else
            {
                //no need to lock, init call
                PIOSockRxCbPush(pContext, pCb);
                bRes = TRUE;
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
    }

    return bRes;
}

static
PPIOSOCK_RX_CB
PIOSockRxCbEntryForRequest(
    IN PDEVICE_CONTEXT  pContext,
    IN WDFREQUEST       Request,
    IN ULONG            Length
)
{
    NTSTATUS                status;
    WDF_OBJECT_ATTRIBUTES   attributes;
    PPIOSOCK_RX_CB          pCb = NULL;

    ASSERT(Request != WDF_NO_HANDLE);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        PIOSOCK_RX_CB
    );
    status = WdfObjectAllocateContext(
        Request,
        &attributes,
        &pCb
    );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return NULL;
    }

    pCb->Memory = WDF_NO_HANDLE;
    pCb->BufferPA.QuadPart = 0;
    InitializeListHead(&pCb->ListEntry);

    status = WdfRequestRetrieveInputBuffer(Request, 0, &pCb->BufferVA, NULL);
    if (NT_SUCCESS(status))
    {
        pCb->Request = Request;
        pCb->DataLen = (ULONG)Length;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        pCb = NULL;
    }

    return pCb;
}

static
BOOLEAN
PIOSockRxCbInit(
    IN PDEVICE_CONTEXT  pContext
)
{
    ULONG i;
    BOOLEAN bRes = TRUE;
    WDF_OBJECT_ATTRIBUTES lockAttributes, memAttributes;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pContext->RxCbBuffersNum = PIOSOCK_CB_ENTRIES(pContext->RxPktNum);

    if (pContext->RxCbBuffersNum < pContext->RxPktNum)
        pContext->RxCbBuffersNum = pContext->RxPktNum;

    WDF_OBJECT_ATTRIBUTES_INIT(&memAttributes);
    memAttributes.ParentObject = pContext->ThisDevice;

    status = WdfLookasideListCreate(&memAttributes,
        sizeof(PIOSOCK_RX_CB), NonPagedPoolNx,
        &memAttributes, PIOSOCK_DRIVER_MEMORY_TAG,
        &pContext->RxCbBufferMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfLookasideListCreate failed: 0x%x\n", status);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ,
        "Initialize chained buffer with %u entries\n", pContext->RxCbBuffersNum);

    pContext->RxCbBuffers.Next = NULL;

    for (i = 0; i < pContext->RxCbBuffersNum; ++i)
    {
        if (!PIOSockRxCbAdd(pContext))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                "PIOSockRxCbAdd failed, cleanup chained buffer\n");
            bRes = FALSE;
            break;
        }
    }

    if (!bRes)
        PIOSockRxCbCleanup(pContext);

    return bRes;
}

static
VOID
PIOSockRxCbCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PPIOSOCK_RX_CB pCb;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    //no need to lock
    while (pCb = PIOSockRxCbPop(pContext))
    {
        PIOSockRxCbFree(pContext, pCb);
    }

}

//////////////////////////////////////////////////////////////////////////
__inline
VOID
PIOSockRxPktCleanup(
    IN PDEVICE_CONTEXT pContext,
    IN PPIOSOCK_RX_PKT pPkt
)
{
    ASSERT(pPkt);

    if (pPkt->Buffer)
    {
        PIOSockRxCbPush(pContext, pPkt->Buffer);
        pPkt->Buffer = NULL;
    }
}

C_ASSERT((PIOSOCK_DMA_RX_PAGES + 1) == 2);

_Requires_lock_held_(pContext->RxLock)
static
BOOLEAN
PIOSockRxPktInsert(
    IN PDEVICE_CONTEXT pContext,
    IN PPIOSOCK_RX_PKT pPkt
)
{
    BOOLEAN bRes = TRUE;
    PIOSOCK_SG_DESC sg[PIOSOCK_DMA_RX_PAGES + 1];
    PHYSICAL_ADDRESS pPKtPA;
    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (!pPkt->Buffer)
    {
        pPkt->Buffer = PIOSockRxCbPop(pContext);
        if (!pPkt->Buffer)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "PIOSockRxCbPop returns NULL\n");
            return FALSE;
        }
    }

    ASSERT(pPkt->Buffer->Request == WDF_NO_HANDLE);

    pPKtPA.QuadPart = pContext->RxPktPA.QuadPart +
        (ULONGLONG)((PCHAR)pPkt - (PCHAR)pContext->RxPktVA);

    sg[0].length = sizeof(PHYZIO_VSOCK_HDR);
    sg[0].physAddr.QuadPart = pPKtPA.QuadPart + FIELD_OFFSET(PIOSOCK_RX_PKT, Header);

    sg[1].length = PHYZIO_VSOCK_DEFAULT_RX_BUF_SIZE;
    sg[1].physAddr.QuadPart = pPkt->Buffer->BufferPA.QuadPart;

    ret = virtqueue_add_buf(pContext->RxVq, sg, 0, 2, pPkt, &pPkt->IndirectDescs,
        pPKtPA.QuadPart + FIELD_OFFSET(PIOSOCK_RX_PKT, IndirectDescs));

    ASSERT(ret >= 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Error adding buffer to Rx queue (ret = %d)\n", ret);
        PIOSockRxPktCleanup(pContext, pPkt);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return TRUE;
}

_Requires_lock_held_(pContext->RxLock)
__inline
VOID
PIOSockRxPktListProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PSINGLE_LIST_ENTRY pListEntry;

    while (pListEntry = PopEntryList(&pContext->RxPktList))
    {
        PPIOSOCK_RX_PKT pPkt = CONTAINING_RECORD(pListEntry, PIOSOCK_RX_PKT, RxPktListEntry);

        if (!PIOSockRxPktInsert(pContext, pPkt))
        {
            PushEntryList(&pContext->RxPktList, &pPkt->RxPktListEntry);
            break;
        }
    }
}

_Requires_lock_not_held_(pContext->RxLock)
__inline
VOID
PIOSockRxPktInsertOrPostpone(
    IN PDEVICE_CONTEXT pContext,
    IN PPIOSOCK_RX_PKT pPkt
)
{
    bool bNotify = false;
    WdfSpinLockAcquire(pContext->RxLock);
    if (!PIOSockRxPktInsert(pContext, pPkt))
    {
        //postpone packet
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "Postpone packet\n");
        PushEntryList(&pContext->RxPktList, &pPkt->RxPktListEntry);
    }
    else
    {
        PIOSockRxPktListProcess(pContext);
        bNotify = virtqueue_kick_prepare(pContext->RxVq);
    }
    WdfSpinLockRelease(pContext->RxLock);

    if (bNotify)
        virtqueue_notify(pContext->RxVq);
}

_Requires_lock_held_(pSocket->RxLock)
__inline
BOOLEAN
PIOSockRxPktInc(
    IN PSOCKET_CONTEXT  pSocket,
    IN ULONG            uPktLen
)
{
    if (pSocket->RxBytes + uPktLen > pSocket->buf_alloc)
        return FALSE;

    pSocket->RxBytes += uPktLen;
    return TRUE;

}

_Requires_lock_held_(pSocket->RxLock)
__inline
VOID
PIOSockRxPktDec(
    IN PSOCKET_CONTEXT  pSocket,
    IN ULONG            uPktLen
)
{
    pSocket->RxBytes -= uPktLen;
    pSocket->fwd_cnt += uPktLen;
}

VOID
PIOSockRxVqCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PPIOSOCK_RX_PKT pPkt;
    PSINGLE_LIST_ENTRY pListEntry;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->RxVq && pContext->RxPktVA);

    //drain queue
    WdfSpinLockAcquire(pContext->RxLock);
    while (pPkt = (PPIOSOCK_RX_PKT)virtqueue_detach_unused_buf(pContext->RxVq))
    {
        PIOSockRxPktCleanup(pContext, pPkt);
    }


    while (pListEntry = PopEntryList(&pContext->RxPktList))
    {
        PIOSockRxPktCleanup(pContext, CONTAINING_RECORD(pListEntry, PIOSOCK_RX_PKT, RxPktListEntry));
    }
    WdfSpinLockRelease(pContext->RxLock);

    PIOSockRxCbCleanup(pContext);

    if (pContext->RxPktVA)
    {
        PhyzIOWdfDeviceFreeDmaMemory(&pContext->VDevice.PIODevice, pContext->RxPktVA);
        pContext->RxPktVA = NULL;
    }

    pContext->RxVq = NULL;
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
PIOSockRxVqInit(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    USHORT uNumEntries;
    ULONG uRingSize, uHeapSize, uBufferSize;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    pContext->RxPktList.Next = NULL;

    status = phyzio_query_queue_allocation(&pContext->VDevice.PIODevice, PIOSOCK_VQ_RX,
        &uNumEntries, &uRingSize, &uHeapSize);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "phyzio_query_queue_allocation(PIOSOCK_VQ_RX) failed\n");
        pContext->RxVq = NULL;
        return status;
    }

    pContext->RxPktNum = uNumEntries;

    uBufferSize = sizeof(PIOSOCK_RX_PKT) * uNumEntries;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Allocating common buffer of %u bytes for %u Rx packets\n",
        uBufferSize, uNumEntries);

    pContext->RxPktVA = (PPIOSOCK_RX_PKT)PhyzIOWdfDeviceAllocDmaMemory(&pContext->VDevice.PIODevice,
        uBufferSize, PIOSOCK_DRIVER_MEMORY_TAG);

    ASSERT(pContext->RxPktVA);
    if (!pContext->RxPktVA)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "PhyzIOWdfDeviceAllocDmaMemory(%u bytes for RxPackets) failed\n", uBufferSize);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else if (PIOSockRxCbInit(pContext))
    {
        ULONG i;
        PPIOSOCK_RX_PKT RxPktVA = (PPIOSOCK_RX_PKT)pContext->RxPktVA;

        pContext->RxPktPA = PhyzIOWdfDeviceGetPhysicalAddress(&pContext->VDevice.PIODevice, pContext->RxPktVA);
        ASSERT(pContext->RxPktPA.QuadPart);

        //fill queue, no lock
        for (i = 0; i < uNumEntries; i++)
        {
            if (!PIOSockRxPktInsert(pContext, &RxPktVA[i]))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PIOSockRxPktInsert[%u] failed\n", i);
                status = STATUS_UNSUCCESSFUL;
                break;
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PIOSockRxCbInit failed\n");
        status = STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(status))
    {
        PIOSockRxVqCleanup(pContext);
    }

    return status;
}

_Requires_lock_not_held_(pSocket->StateLock)
static
VOID
PIOSockRxPktHandleConnecting(
    IN PSOCKET_CONTEXT  pSocket,
    IN PPIOSOCK_RX_PKT  pPkt,
    IN BOOLEAN          bTxHasSpace
)
{
    WDFREQUEST  PendedRequest;
    NTSTATUS    status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = PIOSockPendedRequestGetLocked(pSocket, &PendedRequest);

    if (NT_SUCCESS(status))
    {
        if (PendedRequest == WDF_NO_HANDLE &&
            !PIOSockIsFlag(pSocket, SOCK_NON_BLOCK))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "No PendedRequest found\n");
            status = STATUS_CANCELLED;
        }
        else
        {
            switch (pPkt->Header.op)
            {
            case PHYZIO_VSOCK_OP_RESPONSE:
                WdfSpinLockAcquire(pSocket->StateLock);
                PIOSockStateSet(pSocket, PIOSOCK_STATE_CONNECTED);
                PIOSockEventSetBit(pSocket, FD_CONNECT_BIT, STATUS_SUCCESS);
                if (bTxHasSpace)
                    PIOSockEventSetBit(pSocket, FD_WRITE_BIT, STATUS_SUCCESS);
                WdfSpinLockRelease(pSocket->StateLock);
                status = STATUS_SUCCESS;
                break;
            case PHYZIO_VSOCK_OP_INVALID:
                if (PendedRequest != WDF_NO_HANDLE)
                {
                    status = PIOSockPendedRequestSetLocked(pSocket, PendedRequest);
                    if (NT_SUCCESS(status))
                        PendedRequest = WDF_NO_HANDLE;
                }
                break;
            case PHYZIO_VSOCK_OP_RST:
                status = STATUS_CONNECTION_REFUSED;
                break;
            default:
                status = STATUS_CONNECTION_INVALID;
            }
        }
    }

    if (!NT_SUCCESS(status))
    {
        WdfSpinLockAcquire(pSocket->StateLock);
        PIOSockEventSetBit(pSocket, FD_CONNECT_BIT, status);
        PIOSockStateSet(pSocket, PIOSOCK_STATE_CLOSE);
        WdfSpinLockRelease(pSocket->StateLock);
        if (pPkt->Header.op != PHYZIO_VSOCK_OP_RST)
            PIOSockSendReset(pSocket, TRUE);
    }

    if (PendedRequest)
    {
        WdfTimerStop(pSocket->ConnectTimer, FALSE);
        WdfRequestComplete(PendedRequest, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

_Requires_lock_not_held_(pSocket->RxLock)
static
BOOLEAN
PIOSockRxPktEnqueueCb(
    IN PSOCKET_CONTEXT pSocket,
    IN PPIOSOCK_RX_PKT pPkt
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PPIOSOCK_RX_CB pCurrentCb = NULL;
    ULONG BufferFree, PktLen;
    BOOLEAN bRes = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    ASSERT(pPkt && pPkt->Buffer && pPkt->Header.len);

    PktLen = pPkt->Header.len;

    //Merge buffers
    WdfSpinLockAcquire(pSocket->RxLock);
    if (!IsListEmpty(&pSocket->RxCbList) && PktLen <= PIOSOCK_BYTES_TO_MERGE)
    {
        pCurrentCb = CONTAINING_RECORD(pSocket->RxCbList.Blink, PIOSOCK_RX_CB, ListEntry);

        BufferFree = PHYZIO_VSOCK_DEFAULT_RX_BUF_SIZE - pCurrentCb->DataLen;

        if (BufferFree >= PktLen)
        {
            if (PIOSockRxPktInc(pSocket, PktLen))
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "RxCb merged: %d + %d bytes\n", pCurrentCb->DataLen, PktLen);
                memcpy((PCHAR)pCurrentCb->BufferVA + pCurrentCb->DataLen, pPkt->Buffer->BufferVA, PktLen);
                pCurrentCb->DataLen += PktLen;
            }
            else
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
            }
            //just leave buffer with pkt
            WdfSpinLockRelease(pSocket->RxLock);
            return FALSE;
        }
    }
    else
    {
        bRes = TRUE;    //Scan read queue
    }

    //Enqueue buffer
    if (PIOSockRxPktInc(pSocket, PktLen))
    {
        pPkt->Buffer->DataLen = PktLen;
        InsertTailList(&pSocket->RxCbList, &pPkt->Buffer->ListEntry);
        pSocket->RxBuffers++;
        pPkt->Buffer = NULL; //remove buffer from pkt
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "RxCb enqueued: %d bytes\n", PktLen);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
        bRes = FALSE;
    }

    WdfSpinLockRelease(pSocket->RxLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return bRes;
}

_Requires_lock_not_held_(pSocket->RxLock)
static
VOID
PIOSockRxRequestCancelCb(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PPIOSOCK_RX_CB  pCb = GetRequestRxCb(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pSocket->RxLock);
    RemoveEntryList(&pCb->ListEntry);
    WdfSpinLockRelease(pSocket->RxLock);

    WdfRequestComplete(Request, STATUS_CANCELLED);
}

_Requires_lock_not_held_(pSocket->RxLock)
NTSTATUS
PIOSockRxRequestEnqueueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request,
    IN ULONG            Length
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PPIOSOCK_RX_CB  pCurrentCb = NULL;
    ULONG           BufferFree;
    NTSTATUS        status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pCurrentCb = PIOSockRxCbEntryForRequest(pContext, Request, Length);
    if (!pCurrentCb)
        return STATUS_INSUFFICIENT_RESOURCES;


    WdfSpinLockAcquire(pSocket->RxLock);

    //Enqueue buffer
    if (PIOSockRxPktInc(pSocket, pCurrentCb->DataLen))
    {
        status = WdfRequestMarkCancelableEx(Request, PIOSockRxRequestCancelCb);
        if (NT_SUCCESS(status))
        {
            InsertTailList(&pSocket->RxCbList, &pCurrentCb->ListEntry);
            pSocket->RxBuffers++;
            status = STATUS_SUCCESS;
        }
        else
        {
            ASSERT(status == STATUS_CANCELLED);
            TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request canceled: 0x%x\n", status);
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
        status = STATUS_BUFFER_TOO_SMALL;
    }

    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

static
VOID
PIOSockRxPktHandleConnected(
    IN PSOCKET_CONTEXT  pSocket,
    IN PPIOSOCK_RX_PKT  pPkt,
    IN BOOLEAN          bTxHasSpace
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    switch (pPkt->Header.op)
    {
    case PHYZIO_VSOCK_OP_RW:
        if (PIOSockRxPktEnqueueCb(pSocket, pPkt))
        {
            PIOSockEventSetBitLocked(pSocket, FD_READ_BIT, STATUS_SUCCESS);
            PIOSockReadDequeueCb(pSocket, WDF_NO_HANDLE);
        }
        break;
    case PHYZIO_VSOCK_OP_CREDIT_UPDATE:
        if (bTxHasSpace)
        {
            PIOSockEventSetBitLocked(pSocket, FD_WRITE_BIT, STATUS_SUCCESS);
        }
        break;
    case PHYZIO_VSOCK_OP_SHUTDOWN:
        if (PIOSockShutdownFromPeer(pSocket,
            pPkt->Header.flags & PHYZIO_VSOCK_SHUTDOWN_MASK) &&
            !PIOSockRxHasData(pSocket) &&
            !PIOSockIsDone(pSocket))
        {
            PIOSockSendReset(pSocket, FALSE);
            PIOSockDoClose(pSocket);
        }
        break;
    case PHYZIO_VSOCK_OP_RST:
        PIOSockDoClose(pSocket);
        PIOSockEventSetBitLocked(pSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

static
VOID
PIOSockRxPktHandleDisconnecting(
    IN PSOCKET_CONTEXT pSocket,
    IN PPIOSOCK_RX_PKT pPkt
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pPkt->Header.op == PHYZIO_VSOCK_OP_RST)
    {
        PIOSockDoClose(pSocket);

//         if (pSocket->PeerShutdown & PHYZIO_VSOCK_SHUTDOWN_MASK != PHYZIO_VSOCK_SHUTDOWN_MASK)
//         {
//             PIOSockEventSetBit(pSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
//         }
    }
}

static
VOID
PIOSockRxPktHandleListen(
    IN PSOCKET_CONTEXT pSocket,
    IN PPIOSOCK_RX_PKT pPkt
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    NTSTATUS    status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pPkt->Header.op == PHYZIO_VSOCK_OP_RST)
    {
        //remove pended accept
        PIOSockAcceptRemovePkt(pSocket, &pPkt->Header);
    }
    else if (pPkt->Header.op != PHYZIO_VSOCK_OP_REQUEST)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid packet: %u\n", pPkt->Header.op);
        PIOSockSendResetNoSock(pContext, &pPkt->Header);
        return;
    }

    status = PIOSockAcceptEnqueuePkt(pSocket, &pPkt->Header);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "PIOSockAcceptEnqueuePkt failed: 0x%x\n", status);
        PIOSockSendResetNoSock(pContext, &pPkt->Header);
    }
}

VOID
PIOSockRxVqProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PPIOSOCK_RX_PKT     pPkt;
    UINT                len;
    LIST_ENTRY          CompletionList;
    PSINGLE_LIST_ENTRY  pCurrentEntry;
    PSOCKET_CONTEXT     pSocket = NULL;
    BOOLEAN             bStop = FALSE;

    NTSTATUS            status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->RxLock);
    do
    {
        virtqueue_disable_cb(pContext->RxVq);

        while (TRUE)
        {
            if (!PIOSockTxMoreReplies(pContext)) {
                /* Stop rx until the device processes already
                 * pending replies.  Leave rx virtqueue
                 * callbacks disabled.
                 */
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "Stop rx\n");

                bStop = TRUE;
                break;
            }

            pPkt = (PPIOSOCK_RX_PKT)virtqueue_get_buf(pContext->RxVq, &len);
            if (!pPkt)
                break;

            /* Drop short/long packets */

            if (len < sizeof(pPkt->Header) ||
                len > sizeof(pPkt->Header) + pPkt->Header.len)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Short/Long packet (%d)\n", len);
                PIOSockRxPktInsert(pContext, pPkt);
                continue;
            }

            ASSERT(pPkt->Header.len == len - sizeof(pPkt->Header));

            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Recv packet %!Op! (%d:%d <-- %d:%d), len: %d, flags: %d, buf_alloc: %d, fwd_cnt: %d\n",
                pPkt->Header.op,
                (ULONG)pPkt->Header.dst_cid, pPkt->Header.dst_port,
                (ULONG)pPkt->Header.src_cid, pPkt->Header.src_port,
                pPkt->Header.len, pPkt->Header.flags, pPkt->Header.buf_alloc, pPkt->Header.fwd_cnt);

            //"complete" buffers later
            InsertTailList(&CompletionList, &pPkt->CompletionListEntry);
        }
    } while (!virtqueue_enable_cb(pContext->RxVq) && !bStop);

    WdfSpinLockRelease(pContext->RxLock);

    //complete buffers
    while (!IsListEmpty(&CompletionList))
    {
        BOOLEAN bTxHasSpace;

        pPkt = CONTAINING_RECORD(RemoveHeadList(&CompletionList), PIOSOCK_RX_PKT, CompletionListEntry);

        //find socket
        pSocket = PIOSockConnectedFindByRxPkt(pContext, &pPkt->Header);
        if (!pSocket)
        {
            //no connected socket for incoming packet
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "No connected socket found\n");
            pSocket = PIOSockBoundFindByPort(pContext, pPkt->Header.dst_port);
        }

        if (pSocket && pSocket->type != pPkt->Header.type)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid socket %d type\n", pSocket->SocketId);
            pSocket = NULL;
        }
        if (!pSocket)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Socket for packet is not exists\n");
            PIOSockSendResetNoSock(pContext, &pPkt->Header);
            PIOSockRxPktInsertOrPostpone(pContext, pPkt);
            continue;
        }


        ASSERT(pContext->Config.mooze_cid == (ULONG32)pPkt->Header.dst_cid);

        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Socket %d found, state %!State!\n",
            pSocket->SocketId, PIOSockStateGet(pSocket));

        bTxHasSpace = !!PIOSockTxSpaceUpdate(pSocket, &pPkt->Header);

        switch (PIOSockStateGet(pSocket))
        {
        case PIOSOCK_STATE_CONNECTING:
            PIOSockRxPktHandleConnecting(pSocket, pPkt, bTxHasSpace);
            break;
        case PIOSOCK_STATE_CONNECTED:
            PIOSockRxPktHandleConnected(pSocket, pPkt, bTxHasSpace);
            break;
        case PIOSOCK_STATE_CLOSING:
            PIOSockRxPktHandleDisconnecting(pSocket, pPkt);
            break;
        case PIOSOCK_STATE_LISTEN:
            PIOSockRxPktHandleListen(pSocket, pPkt);
            break;
        default:
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET,
                "Invalid socket %d state for Rx packet %d\n", pSocket->SocketId, pPkt->Header.op);
        }

        //reinsert handled packet
        PIOSockRxPktInsertOrPostpone(pContext, pPkt);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_not_held_(pSocket->RxLock)
static
NTSTATUS
PIOSockReadForward(
    IN PSOCKET_CONTEXT pSocket,
    IN WDFREQUEST Request
)
{
    NTSTATUS status;
    BOOLEAN bTimer = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (!PIOSockIsFlag(pSocket, SOCK_NON_BLOCK) &&
        pSocket->RecvTimeout != LONG_MAX)
    {
        PPIOSOCK_RX_CONTEXT pRequest;
        WDF_OBJECT_ATTRIBUTES attributes;

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
            &attributes,
            PIOSOCK_RX_CONTEXT
        );
        status = WdfObjectAllocateContext(
            Request,
            &attributes,
            &pRequest
        );
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfObjectAllocateContext failed: 0x%x\n", status);
            return status;
        }

        pRequest->Timeout = WDF_ABS_TIMEOUT_IN_MS(pSocket->RecvTimeout);
        pRequest->Counter = 0;

        WdfSpinLockAcquire(pSocket->RxLock);
        PIOSockTimerStart(&pSocket->ReadTimer, pRequest->Timeout);
        WdfSpinLockRelease(pSocket->RxLock);

        bTimer = TRUE;
    }

    PIOSockEventClearBit(pSocket, FD_READ_BIT);

    status = WdfRequestForwardToIoQueue(Request, pSocket->ReadQueue);
    if (!NT_SUCCESS(status))
    {

        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestForwardToIoQueue failed: 0x%x\n", status);
        if (bTimer)
        {
            WdfSpinLockAcquire(pSocket->RxLock);
            PIOSockTimerDeref(&pSocket->ReadTimer, TRUE);
            WdfSpinLockRelease(pSocket->RxLock);
        }
    }

    return status;
}

static
VOID
PIOSockRead(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);
    //check Request
    if (PIOSockIsFlag(pSocket, SOCK_CONTROL))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Invalid socket %d for read\n", pSocket->SocketId);
        WdfRequestComplete(Request, STATUS_NOT_SOCKET);
        return;
    }

    status = PIOSockReadForward(pSocket, Request);

    if (!NT_SUCCESS(status))
        WdfRequestComplete(Request, status);
}

NTSTATUS
PIOSockReadWithFlags(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    NTSTATUS status;
    PPHYZIO_VSOCK_READ_PARAMS pReadParams;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    //validate request
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pReadParams), &pReadParams, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
            "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
    }
    else if (pReadParams->Flags & ~(MSG_PEEK | MSG_WAITALL))
    {

        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ,
            "Unsupported flags: 0x%x\n", pReadParams->Flags & ~(MSG_PEEK | MSG_WAITALL));
        status = STATUS_NOT_SUPPORTED;
    }
    else if ((pReadParams->Flags & (MSG_PEEK | MSG_WAITALL)) == (MSG_PEEK | MSG_WAITALL))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ,
            "Incompatible flags: 0x%x\n", MSG_PEEK | MSG_WAITALL);
        status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        PVOID pBuffer;
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &pBuffer, NULL);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        }
        else
            WdfRequestSetInformation(Request, pReadParams->Flags);
    }

    if (NT_SUCCESS(status))
    {
        if (NT_SUCCESS(PIOSockReadForward(pSocket, Request)))
            status = STATUS_PENDING;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

NTSTATUS
PIOSockReadQueueInit(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT              pContext = GetDeviceContext(hDevice);
    WDF_IO_QUEUE_CONFIG          queueConfig;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = pContext->ThisDevice;

    status = WdfSpinLockCreate(
        &attributes,
        &pContext->RxLock
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfSpinLockCreate failed: 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchParallel
    );

    queueConfig.EvtIoRead = PIOSockRead;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->ReadQueue
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfIoQueueCreate failed (Read Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice,
        pContext->ReadQueue,
        WdfRequestTypeRead);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfDeviceConfigureRequestDispatching failed (Read Queue): 0x%x\n", status);
        return status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
PIOSockReadGetRequestParameters(
    IN WDFREQUEST       Request,
    OUT PVOID           *pBuffer,
    OUT ULONG           *pLength,
    OUT ULONG           *pFlags
)
{
    NTSTATUS        status;
    WDF_REQUEST_PARAMETERS  parameters;
    size_t  stLength;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_REQUEST_PARAMETERS_INIT(&parameters);
    WdfRequestGetParameters(Request, &parameters);

    if (parameters.Type == WdfRequestTypeDeviceControl)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "Control request\n");

        *pFlags = (ULONG)WdfRequestGetInformation(Request);
        WdfRequestSetInformation(Request, 0);
    }
    else
        *pFlags = 0;

    status = WdfRequestRetrieveOutputBuffer(Request, 0, pBuffer, &stLength);
    if (!NT_SUCCESS(status))
    {
        ASSERT(FALSE);
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
    }
    else
    {
        *pLength = (ULONG)stLength;
    }

    return status;
}

VOID
PIOSockReadDequeueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       ReadRequest OPTIONAL
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    NTSTATUS        status;
    ULONG           ReadRequestFlags = 0;
    PCHAR           ReadRequestPtr = NULL;
    ULONG           ReadRequestFree, ReadRequestLength;
    PPIOSOCK_RX_CB  pCurrentCb;
    LIST_ENTRY      LoopbackList, *pCurrentItem;
    ULONG           FreeSpace;
    BOOLEAN         bSetBit, bStop = FALSE, bPend = FALSE;
    PPIOSOCK_RX_CONTEXT pRequest = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pSocket->RxLock);

    if (ReadRequest != WDF_NO_HANDLE)
    {
        pRequest = GetRequestRxContext(ReadRequest);

        if (pRequest && pRequest->Timeout)
        {
            PIOSockTimerDeref(&pSocket->ReadTimer, TRUE);
            pRequest->Timeout -= PIOSockTimerPassed(&pSocket->ReadTimer);
        }
    }

    if (PIOSockRxHasData(pSocket) ||
        PIOSockIsFlag(pSocket, SOCK_NON_BLOCK) ||
        PIOSockStateGet(pSocket) != PIOSOCK_STATE_CONNECTED)
    {
        if (ReadRequest != WDF_NO_HANDLE)
        {
            PIOSockEventClearBit(pSocket, FD_READ_BIT);

            status = PIOSockReadGetRequestParameters(
                ReadRequest,
                &ReadRequestPtr,
                &ReadRequestLength,
                &ReadRequestFlags);

            ReadRequestFree = ReadRequestLength;
        }
        else
        {
            status = PIOSockPendedRequestGet(pSocket, &ReadRequest);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Pended Read request canceled\n");
                status = STATUS_SUCCESS; //do not complete canceled request
                ASSERT(ReadRequest == WDF_NO_HANDLE);
            }
            else if (ReadRequest != WDF_NO_HANDLE)
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Pended Read request found\n");
                pRequest = GetRequestRxContext(ReadRequest);

                if (pRequest && pRequest->Timeout)
                {
                    PIOSockTimerDeref(&pSocket->ReadTimer, TRUE);
                    pRequest->Timeout -= PIOSockTimerPassed(&pSocket->ReadTimer);
                }

                ReadRequestPtr = pSocket->ReadRequestPtr;
                ReadRequestFree = pSocket->ReadRequestFree;
                ReadRequestLength = pSocket->ReadRequestLength;
                ReadRequestFlags = pSocket->ReadRequestFlags;
            }
        }

        if (ReadRequest == WDF_NO_HANDLE || !NT_SUCCESS(status))
        {
            bStop = TRUE;
        }
        else if (!PIOSockRxHasData(pSocket))
        {
            //complete request
            bStop = TRUE;

            status = PIOSockStateValidate(pSocket, FALSE);

            if (NT_SUCCESS(status))
            {
                if (PIOSockIsFlag(pSocket, SOCK_NON_BLOCK))
                {
                    status = STATUS_CANT_WAIT;
                }
                else
                {
                    ASSERT(FALSE);
                    bPend = TRUE;
                }
            }
            else if (status == STATUS_REMOTE_DISCONNECT)
                status = STATUS_SUCCESS; //return zero bytes on peer shutdown
        }
    }
    else
    {
        bPend = TRUE;
    }

    if (bPend)
    {
        bStop = TRUE;

        ASSERT(ReadRequest != WDF_NO_HANDLE);
        if (ReadRequest != WDF_NO_HANDLE)
        {
            if (!pRequest || pRequest->Timeout > PIOSOCK_TIMER_TOLERANCE)
            {
                status = PIOSockReadGetRequestParameters(
                    ReadRequest,
                    &pSocket->ReadRequestPtr,
                    &pSocket->ReadRequestLength,
                    &pSocket->ReadRequestFlags);

                if (NT_SUCCESS(status))
                {
                    pSocket->ReadRequestFree = pSocket->ReadRequestLength;
                    status = PIOSockPendedRequestSet(pSocket, ReadRequest);
                    if (NT_SUCCESS(status))
                    {
                        if(pRequest)
                            PIOSockTimerStart(&pSocket->ReadTimer, pRequest->Timeout);
                        ReadRequest = WDF_NO_HANDLE;
                    }
                }
            }
            else if (pRequest)
                status = STATUS_TIMEOUT;
        }
    }

    if (ReadRequest == WDF_NO_HANDLE || bStop)
    {
        WdfSpinLockRelease(pSocket->RxLock);

        if (ReadRequest != WDF_NO_HANDLE)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Complete request without CB dequeue: 0x%08x\n", status);
            WdfRequestComplete(ReadRequest, status);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "No read request available\n");
        }
        return;
    }

    ASSERT(ReadRequestPtr);

    InitializeListHead(&LoopbackList);

    //process chained buffer
    for (pCurrentItem = pSocket->RxCbList.Flink;
        pCurrentItem != &pSocket->RxCbList;
        pCurrentItem = pCurrentItem->Flink)
    {
        //peek the first buffer
        pCurrentCb = CONTAINING_RECORD(pCurrentItem, PIOSOCK_RX_CB, ListEntry);

        if (pCurrentCb->Request != WDF_NO_HANDLE) //only loopback buffers have request assigned
        {
            status = WdfRequestUnmarkCancelable(pCurrentCb->Request);

            //STATUS_CANCELLED means cancellation is in progress,
            //cancel routine will remove from list and complete current request,
            //we should just skip it
            if (!NT_SUCCESS(status))
            {
                ASSERT(status == STATUS_CANCELLED);
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request is canceling: 0x%x\n", status);

                pSocket->RxCbReadPtr = NULL;
                pSocket->RxCbReadLen = 0;
                continue;
            }
        }

        //set socket read data pointer
        if (!pSocket->RxCbReadPtr)
        {
            pSocket->RxCbReadPtr = pCurrentCb->BufferVA;
            pSocket->RxCbReadLen = pCurrentCb->DataLen;
        }

        //can we copy the whole CB?
        if (ReadRequestFree >= pSocket->RxCbReadLen)
        {
            memcpy(ReadRequestPtr, pSocket->RxCbReadPtr, pSocket->RxCbReadLen);

            //update request buffer data ptr
            ReadRequestPtr += pSocket->RxCbReadLen;
            ReadRequestFree -= pSocket->RxCbReadLen;

            if (!(ReadRequestFlags & MSG_PEEK))
            {
                PLIST_ENTRY pPrevItem = pCurrentItem->Blink;

                PIOSockRxPktDec(pSocket, pCurrentCb->DataLen);

                RemoveEntryList(pCurrentItem);
                pCurrentItem = pPrevItem;

                if (pCurrentCb->Request != WDF_NO_HANDLE)
                    InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); //complete loopback requests later
                else
                    PIOSockRxCbPushLocked(pContext, pCurrentCb);
            }

            pSocket->RxCbReadPtr = NULL;
            pSocket->RxCbReadLen = 0;
        }
        else //request buffer is not big enough
        {
            memcpy(ReadRequestPtr, pSocket->RxCbReadPtr, ReadRequestFree);

            ReadRequestFree = 0;

            if (!(ReadRequestFlags & MSG_PEEK))
            {
                //update current CB data ptr
                pSocket->RxCbReadPtr += ReadRequestLength;
                pSocket->RxCbReadLen -= ReadRequestLength;
            }

            if (pCurrentCb->Request != WDF_NO_HANDLE)
            {
                //Postpone incomplete loopback request.
                status = WdfRequestMarkCancelableEx(pCurrentCb->Request, PIOSockRxRequestCancelCb);

                //WdfRequestMarkCancelableEx returns STATUS_CANCELLED if request has Canceled bit set
                //(was canceled after Unmark and before this call). In this case caller has to complete
                //request.
                //WARNING! SDV marks pCurrentCb->Request as INVALID despite of return status, but request still VALID
                //if status == STATUS_CANCELLED
                if (!NT_SUCCESS(status))
                {
                    ASSERT(status == STATUS_CANCELLED);
                    TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request is canceled: 0x%x\n", status);

                    RemoveEntryList(pCurrentItem);
                    pSocket->RxCbReadPtr = NULL;
                    pSocket->RxCbReadLen = 0;

                    pCurrentCb->DataLen = 0;
                    InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); //complete canceled loopback request later
                }
            }
            break;
        }
    }

    if (!ReadRequestFree || ReadRequestFlags & MSG_PEEK)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "Should complete read request\n");
    }
    else if (ReadRequestLength == ReadRequestFree || ReadRequestFlags & MSG_WAITALL)
    {
        if (!pRequest || pRequest->Timeout > PIOSOCK_TIMER_TOLERANCE)
        {
            //pend request
            ASSERT(!(ReadRequestFlags & MSG_PEEK));
            pSocket->ReadRequestPtr = ReadRequestPtr;
            pSocket->ReadRequestFree = ReadRequestFree;
            pSocket->ReadRequestLength = ReadRequestLength;
            pSocket->ReadRequestFlags = ReadRequestFlags;

            status = PIOSockPendedRequestSet(pSocket, ReadRequest);
            if (!NT_SUCCESS(status))
            {
                ASSERT(status == STATUS_CANCELLED);
                if (status == STATUS_CANCELLED)
                    ReadRequest = WDF_NO_HANDLE; //already completed

                ReadRequestLength = ReadRequestFree = 0;
            }
            else
            {
                ReadRequest = WDF_NO_HANDLE;
                if(pRequest)
                    PIOSockTimerStart(&pSocket->ReadTimer, pRequest->Timeout);
            }
        }
        else if (pRequest)
        {
            status = STATUS_TIMEOUT;
            ReadRequestLength = ReadRequestFree = 0;
        }
    }

    bSetBit = (ReadRequest != WDF_NO_HANDLE && PIOSockRxHasData(pSocket));
    FreeSpace = pSocket->buf_alloc - (pSocket->fwd_cnt - pSocket->last_fwd_cnt);

    WdfSpinLockRelease(pSocket->RxLock);

    if (bSetBit)
        PIOSockEventSetBit(pSocket, FD_READ_BIT, STATUS_SUCCESS);

    if (FreeSpace < PHYZIO_VSOCK_MAX_PKT_BUF_SIZE)
        PIOSockSendCreditUpdate(pSocket);

    if (ReadRequest != WDF_NO_HANDLE)
    {
        ASSERT(pSocket->PendedRequest == WDF_NO_HANDLE);

        WdfRequestCompleteWithInformation(ReadRequest, status, ReadRequestLength - ReadRequestFree);
    }

    //complete loopback requests (succeed and canceled)
    while (!IsListEmpty(&LoopbackList))
    {
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&LoopbackList), PIOSOCK_RX_CB, ListEntry);

        //NOTE! SDV thinks we are completing INVALID request marked as cancelable,
        //but request is appeared in this list only if WdfRequestMarkCancelableEx failed
        WdfRequestCompleteWithInformation(pCurrentCb->Request,
            pCurrentCb->DataLen ? STATUS_SUCCESS : STATUS_CANCELLED,
            pCurrentCb->DataLen);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

VOID
PIOSockReadCleanupCb(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PPIOSOCK_RX_CB  pCurrentCb;
    LIST_ENTRY      LoopbackList;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    InitializeListHead(&LoopbackList);

    WdfSpinLockAcquire(pSocket->RxLock);

    pSocket->RxCbReadLen = 0;
    pSocket->RxCbReadPtr = NULL;

    //process chained buffer
    while (!IsListEmpty(&pSocket->RxCbList))
    {
        //peek the first buffer
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&pSocket->RxCbList), PIOSOCK_RX_CB, ListEntry);

        if (pCurrentCb->Request)
        {
            NTSTATUS status = WdfRequestUnmarkCancelable(pCurrentCb->Request);
            if (!NT_SUCCESS(status))
            {
                ASSERT(status == STATUS_CANCELLED);
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request canceled\n");
            }
            else
                InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); //complete loopback requests later
        }
        else
            PIOSockRxCbPushLocked(pContext, pCurrentCb);
    }

    WdfSpinLockRelease(pSocket->RxLock);

    //complete loopback
    while (!IsListEmpty(&LoopbackList))
    {
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&LoopbackList), PIOSOCK_RX_CB, ListEntry);
        WdfRequestComplete(pCurrentCb->Request, STATUS_CANCELLED);
    }

}

static
VOID
PIOSockReadSocketIoDefault(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);
    PIOSockReadDequeueCb(pSocket, Request);
}

static
VOID
PIOSockReadSocketIoStop(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s, ActionFlags: %x\n", __FUNCTION__, ActionFlags);

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        WdfRequestStopAcknowledge(Request, FALSE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {

        if (ActionFlags & WdfRequestStopRequestCancelable)
        {
            PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);

            WdfSpinLockAcquire(pSocket->RxLock);
            if (pSocket->PendedRequest == Request)
            {
                pSocket->PendedRequest = WDF_NO_HANDLE;
                WdfObjectDereference(Request);
            }
            WdfSpinLockRelease(pSocket->RxLock);

            if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
            {
                WdfRequestComplete(Request, STATUS_CANCELLED);
            }
        }
    }

}

NTSTATUS
PIOSockReadSocketQueueInit(
    IN PSOCKET_CONTEXT pSocket
)
{
    WDFDEVICE               hDevice = WdfFileObjectGetDevice(pSocket->ThisSocket);
    PDEVICE_CONTEXT         pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES   queueAttributes;
    WDF_IO_QUEUE_CONFIG     queueConfig;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchSequential
    );

    queueConfig.EvtIoDefault = PIOSockReadSocketIoDefault;
    queueConfig.EvtIoStop = PIOSockReadSocketIoStop;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ParentObject = pSocket->ThisSocket;

    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        &queueAttributes,
        &pSocket->ReadQueue
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfIoQueueCreate failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    PIOSockTimerCreate(&pSocket->ReadTimer, pSocket->ThisSocket, PIOSockReadTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "PIOSockTimerCreate failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
VOID
PIOSockReadTimerFunc(
    WDFTIMER Timer
)
{
    static ULONG ulCounter;
    PSOCKET_CONTEXT pSocket = GetSocketContext(WdfTimerGetParentObject(Timer));
    LONGLONG Timeout = LONGLONG_MAX;
    WDFREQUEST PrevTagRequest = WDF_NO_HANDLE, TagRequest = WDF_NO_HANDLE, Request;
    NTSTATUS status;
    LIST_ENTRY CompletionList;
    PPIOSOCK_RX_CONTEXT pRequest;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pSocket->RxLock);

    ++ulCounter;

    status = PIOSockPendedRequestGet(pSocket, &Request);
    if (NT_SUCCESS(status) && Request != WDF_NO_HANDLE)
    {
        pRequest = GetRequestRxContext(Request);

        if (pRequest)
        {
            if (pRequest->Timeout > pSocket->ReadTimer.Timeout + PIOSOCK_TIMER_TOLERANCE)
            {
                pRequest->Timeout -= pSocket->ReadTimer.Timeout;

                if (pRequest->Timeout < Timeout)
                    Timeout = pRequest->Timeout;

                status = PIOSockPendedRequestSet(pSocket, Request);
            }
            else
            {
                status = STATUS_UNSUCCESSFUL;
            }

            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "Complete expired pended Rx request %p\n", Request);

                InsertTailList(&CompletionList, &pRequest->ListEntry);
                pRequest->ThisRequest = Request;
                PIOSockTimerDeref(&pSocket->ReadTimer, FALSE);
            }
        }
    }

    do
    {
        status = WdfIoQueueFindRequest(pSocket->ReadQueue, PrevTagRequest, WDF_NO_HANDLE, NULL, &TagRequest);

        if (PrevTagRequest != WDF_NO_HANDLE)
        {
            WdfObjectDereference(PrevTagRequest);
        }

        if (NT_SUCCESS(status))
        {
            pRequest = GetRequestRxContext(TagRequest);

            if (pRequest && pRequest->Timeout && pRequest->Counter < ulCounter)
            {
                if (pRequest->Timeout <= pSocket->ReadTimer.Timeout + PIOSOCK_TIMER_TOLERANCE)
                {
                    status = WdfIoQueueRetrieveFoundRequest(pSocket->ReadQueue, TagRequest, &Request);

                    WdfObjectDereference(TagRequest);

                    if (status == STATUS_NOT_FOUND)
                    {
                        TagRequest = PrevTagRequest = WDF_NO_HANDLE;
                        status = STATUS_SUCCESS;
                    }
                    else if (!NT_SUCCESS(status))
                    {
                        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfIoQueueRetrieveFoundRequest failed: 0x%08x\n", status);
                        break;
                    }
                    else
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "Complete expired queued Rx request %p\n", Request);

                        InsertTailList(&CompletionList, &pRequest->ListEntry);
                        pRequest->ThisRequest = Request;
                        PIOSockTimerDeref(&pSocket->ReadTimer, FALSE);
                    }
                }
                else
                {
                    pRequest->Counter = ulCounter;
                    pRequest->Timeout -= pSocket->ReadTimer.Timeout;

                    if (pRequest->Timeout < Timeout)
                        Timeout = pRequest->Timeout;

                    PrevTagRequest = TagRequest;
                }
            }
        }
        else if (status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        else if (status == STATUS_NOT_FOUND)
        {
            TagRequest = PrevTagRequest = WDF_NO_HANDLE;
            status = STATUS_SUCCESS;
        }
    } while (NT_SUCCESS(status));

    PIOSockTimerSet(&pSocket->ReadTimer, Timeout);

    WdfSpinLockRelease(pSocket->RxLock);

    while (!IsListEmpty(&CompletionList))
    {
        pRequest = CONTAINING_RECORD(RemoveHeadList(&CompletionList), PIOSOCK_RX_CONTEXT, ListEntry);
        WdfRequestComplete(pRequest->ThisRequest, STATUS_TIMEOUT);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
}