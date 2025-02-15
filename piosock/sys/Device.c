/*
 * Placeholder for the device related functions
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
#include "Device.tmh"
#endif

EVT_WDF_DEVICE_PREPARE_HARDWARE     PIOSockEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     PIOSockEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             PIOSockEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              PIOSockEvtDeviceD0Exit;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED PIOSockEvtDeviceD0EntryPostInterruptsEnabled;

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL  PIOSockEvtIoDeviceControl;
EVT_WDF_REQUEST_CANCEL              PIOSockSelectCancel;
EVT_WDF_WORKITEM                    PIOSockSelectWorkitem;
EVT_WDF_TIMER                       PIOSockSelectTimerFunc;

VOID
PIOSockQueuesCleanup(
    IN WDFDEVICE hDevice
);

NTSTATUS
PIOSockQueuesInit(
    IN WDFDEVICE hDevice
);

NTSTATUS
PIOSockDeviceGetConfig(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
PIOSockDeviceGetAf(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

typedef struct _PIOSOCK_SELECT_HANDLE
{
    ULONGLONG       hSocket;
    WDFFILEOBJECT   Socket;
}PIOSOCK_SELECT_HANDLE, *PPIOSOCK_SELECT_HANDLE;

typedef struct _PIOSOCK_SELECT_PKT
{
    LIST_ENTRY              ListEntry;
    LONGLONG                Timeout;
    PPHYZIO_VSOCK_SELECT    pSelect;
    ULONG                   FdCount[FDSET_MAX];
    NTSTATUS                Status;
    PIOSOCK_SELECT_HANDLE   Fds[FD_SETSIZE];
}PIOSOCK_SELECT_PKT, *PPIOSOCK_SELECT_PKT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIOSOCK_SELECT_PKT, GetSelectContext);

NTSTATUS
PIOSockSelectInit(
    IN PDEVICE_CONTEXT pContext
);

VOID
PIOSockSelectCleanupFds(
    IN PPIOSOCK_SELECT_PKT      pPkt,
    IN PHYZIO_VSOCK_FDSET_TYPE  iFdSet,
    IN ULONG                    uStartIndex
);

BOOLEAN
PIOSockSelectCheckPkt(
    IN PPIOSOCK_SELECT_PKT  pPkt
);

BOOLEAN
PIOSockSelectCopyFds(
    IN PDEVICE_CONTEXT          pContext,
    IN BOOLEAN                  bIs32BitProcess,
    IN PPHYZIO_VSOCK_SELECT     pSelect,
    IN PPIOSOCK_SELECT_PKT      pPkt,
    IN PHYZIO_VSOCK_FDSET_TYPE  iFdSet,
    IN ULONG                    uStartIndex
);

NTSTATUS
PIOSockSelect(
    IN WDFREQUEST Request,
    IN OUT size_t *pLength
);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PIOSockEvtDeviceAdd)
#pragma alloc_text (PAGE, PIOSockQueuesCleanup)
#pragma alloc_text (PAGE, PIOSockQueuesInit)

#pragma alloc_text (PAGE, PIOSockEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, PIOSockEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, PIOSockEvtDeviceD0Entry)
#pragma alloc_text (PAGE, PIOSockEvtDeviceD0Exit)
#pragma alloc_text (PAGE, PIOSockEvtDeviceD0EntryPostInterruptsEnabled)

#pragma alloc_text (PAGE, PIOSockDeviceGetConfig)
#pragma alloc_text (PAGE, PIOSockDeviceGetAf)
#pragma alloc_text (PAGE, PIOSockEvtIoDeviceControl)
#pragma alloc_text (PAGE, PIOSockSelectInit)
#pragma alloc_text (PAGE, PIOSockSelectCleanupFds)
#pragma alloc_text (PAGE, PIOSockSelectCheckPkt)
#pragma alloc_text (PAGE, PIOSockSelectWorkitem)
#pragma alloc_text (PAGE, PIOSockSelectCopyFds)
#pragma alloc_text (PAGE, PIOSockSelect)
#endif

static
VOID
PIOSockQueuesCleanup(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    NTSTATUS status = STATUS_SUCCESS;

    ULONG uBufferSize;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (pContext->RxVq)
        PIOSockRxVqCleanup(pContext);

    if (pContext->TxVq)
        PIOSockTxVqCleanup(pContext);

    if (pContext->EvtVq)
        PIOSockEvtVqCleanup(pContext);

    PhyzIOWdfDestroyQueues(&pContext->VDevice);
}

static
NTSTATUS
PIOSockQueuesInit(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    NTSTATUS status = STATUS_SUCCESS;
    PHYZIO_WDF_QUEUE_PARAM params[PIOSOCK_VQ_MAX];
    PPIOSOCK_VQ vqs[PIOSOCK_VQ_MAX];

    ULONG uBufferSize;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    // rx
    params[PIOSOCK_VQ_RX].Interrupt = pContext->WdfInterrupt;
    // tx
    params[PIOSOCK_VQ_TX].Interrupt = pContext->WdfInterrupt;
    // event
    params[PIOSOCK_VQ_EVT].Interrupt = pContext->WdfInterrupt;

    status = PhyzIOWdfInitQueues(&pContext->VDevice, PIOSOCK_VQ_MAX, vqs, params);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PhyzIOWdfInitQueues failed: 0x%x\n", status);
        return status;
    }

    pContext->RxVq = vqs[PIOSOCK_VQ_RX];
    status = PIOSockRxVqInit(pContext);
    if (NT_SUCCESS(status))
    {
        pContext->TxVq = vqs[PIOSOCK_VQ_TX];
        status = PIOSockTxVqInit(pContext);
        if (NT_SUCCESS(status))
        {
            pContext->EvtVq = vqs[PIOSOCK_VQ_EVT];
            status = PIOSockEvtVqInit(pContext);
            if (!NT_SUCCESS(status))
                pContext->EvtVq = NULL;
        }
        else
            pContext->TxVq = NULL;
    }
    else
        pContext->RxVq = NULL;

    if (!NT_SUCCESS(status))
        PIOSockQueuesCleanup(hDevice);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);

    return status;
}

//////////////////////////////////////////////////////////////////////////
NTSTATUS
PIOSockEvtDeviceAdd(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES        Attributes;
    WDFDEVICE                    hDevice;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    PDEVICE_CONTEXT              pContext = NULL;
    WDF_FILEOBJECT_CONFIG        fileConfig;
    WDF_IO_QUEUE_CONFIG          queueConfig;
    WDF_WORKITEM_CONFIG          wrkConfig;

    DECLARE_CONST_UNICODE_STRING(usDeviceName, PIOSOCK_DEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(usDosDeviceName, PIOSOCK_SYMLINK_NAME);

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    // Configure Pnp/power callbacks
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDevicePrepareHardware = PIOSockEvtDevicePrepareHardware;
    PnpPowerCallbacks.EvtDeviceReleaseHardware = PIOSockEvtDeviceReleaseHardware;
    PnpPowerCallbacks.EvtDeviceD0Entry         = PIOSockEvtDeviceD0Entry;
    PnpPowerCallbacks.EvtDeviceD0Exit          = PIOSockEvtDeviceD0Exit;
    PnpPowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = PIOSockEvtDeviceD0EntryPostInterruptsEnabled;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

    // Set DirectIO mode
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

    // Set device name (for kernel mode clients)
    status = WdfDeviceInitAssignName(DeviceInit, &usDeviceName);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceInitAssignName failed - 0x%x\n", status);
        return status;
    }

    // Set device access (for user mode clients)
    status = WdfDeviceInitAssignSDDLString(DeviceInit, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceInitAssignSDDLString failed - 0x%x\n", status);
        return status;
    }

    // Configure file object callbacks
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        PIOSockCreateStub,
        PIOSockClose,
        WDF_NO_EVENT_CALLBACK // Cleanup
    );
    fileConfig.FileObjectClass = WdfFileObjectWdfCanUseFsContext;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SOCKET_CONTEXT);

    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &Attributes);

    // Create device
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &Attributes, &hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreate failed - 0x%x\n", status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(
        hDevice,
        &GUID_DEVINTERFACE_PIOSOCK,
        NULL
    );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreateDeviceInterface failed - 0x%x\n", status);
        return status;
    }

    status = WdfDeviceCreateSymbolicLink(
        hDevice,
        &usDosDeviceName
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreateSymbolicLink failed - 0x%x\n", status);
        return status;
    }

    status = PIOSockBoundListInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "PIOSockBoundListInit failed - 0x%x\n", status);
        return status;
    }

    status = PIOSockConnectedListInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "PIOSockConnectedListInit failed - 0x%x\n", status);
        return status;
    }

    pContext = GetDeviceContext(hDevice);

    pContext->ThisDevice = hDevice;

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = hDevice;

    status = WdfLookasideListCreate(&Attributes,
        sizeof(PIOSOCK_ACCEPT_ENTRY), NonPagedPoolNx,
        &Attributes, PIOSOCK_DRIVER_MEMORY_TAG,
        &pContext->AcceptMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfLookasideListCreate failed: 0x%x\n", status);
        return status;
    }

    // Create sequential queue for IoCtl requests
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchParallel
    );
    queueConfig.EvtIoDeviceControl = PIOSockEvtIoDeviceControl;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->IoCtlQueue
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfIoQueueCreate failed (IoCtrl Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice,
        pContext->IoCtlQueue,
        WdfRequestTypeDeviceControl);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfDeviceConfigureRequestDispatching failed (IoCtrl Queue): 0x%x\n", status);
        return status;
    }

    // Create parallel queue for Write requests
    status = PIOSockWriteQueueInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "PIOSockWriteQueueInit failed (Write Queue): 0x%x\n", status);
        return status;
    }

    // Create parallel queue for Read requests
    status = PIOSockReadQueueInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "PIOSockReadQueueInit failed (Read Queue): 0x%x\n", status);
        return status;
    }

    status = PIOSockSelectInit(pContext);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "PIOSockSelectInit failed: 0x%x\n", status);
        return status;
    }

    status = PIOSockInterruptInit(hDevice);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "PIOSockInterruptInit failed - 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

static
NTSTATUS
PIOSockEvtDevicePrepareHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesRaw,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;
    UINT nr_ports;
    u64 u64HostFeatures;
    u64 u64MoozeFeatures = 0;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    status = PhyzIOWdfInitialize(
        &pContext->VDevice,
        Device,
        ResourcesTranslated,
        NULL,
        PIOSOCK_DRIVER_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PhyzIOWdfInitialize failed with 0x%x\n", status);
        return status;
    }

    u64HostFeatures = PhyzIOWdfGetDeviceFeatures(&pContext->VDevice);

    if (phyzio_is_feature_enabled(u64HostFeatures, PHYZIO_RING_F_INDIRECT_DESC))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Enable indirect feature.\n");

        phyzio_feature_enable(u64MoozeFeatures, PHYZIO_RING_F_INDIRECT_DESC);
    }

    status = PhyzIOWdfSetDriverFeatures(&pContext->VDevice, u64MoozeFeatures, 0);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PhyzIOWdfSetDriverFeatures failed: 0x%x\n", status);
        PhyzIOWdfSetDriverFailed(&pContext->VDevice);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

static
NTSTATUS
PIOSockEvtDeviceReleaseHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    PhyzIOWdfShutdown(&pContext->VDevice);

    return STATUS_SUCCESS;
}

static
NTSTATUS
PIOSockEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PrepiousState
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PrepiousState);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    status = PIOSockQueuesInit(Device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "PIOSockQueuesInit failed: 0x%x\n", status);
        return status;
    }

    PhyzIOWdfDeviceGet(&pContext->VDevice,
        0,
        &pContext->Config,
        sizeof(pContext->Config));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
        "mooze_cid %lld\n", pContext->Config.mooze_cid);

    return status;
}

static
NTSTATUS
PIOSockEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s TargetState: %d\n",
        __FUNCTION__, TargetState);

    PIOSockQueuesCleanup(Device);

    return STATUS_SUCCESS;
}

static
NTSTATUS
PIOSockEvtDeviceD0EntryPostInterruptsEnabled(
    IN  WDFDEVICE WdfDevice,
    IN  WDF_POWER_DEVICE_STATE PrepiousState
    )
{
    PDEVICE_CONTEXT    pContext = GetDeviceContext(WdfDevice);
    UNREFERENCED_PARAMETER(PrepiousState);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Setting PHYZIO_CONFIG_S_DRIVER_OK flag\n");
    PhyzIOWdfSetDriverOK(&pContext->VDevice);

    ASSERT(pContext->RxVq && pContext->EvtVq);

    return STATUS_SUCCESS;
}

static
NTSTATUS
PIOSockDeviceGetConfig(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PPHYZIO_VSOCK_CONFIG    pConfig = NULL;
    NTSTATUS                status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(PHYZIO_VSOCK_CONFIG), (PVOID*)&pConfig, pLength);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
            "WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
    _Analysis_assume_(*pLength >= sizeof(PHYZIO_VSOCK_CONFIG));

    *pConfig = GetDeviceContextFromRequest(Request)->Config;
    *pLength = sizeof(*pConfig);

    return STATUS_SUCCESS;
}

static
NTSTATUS
PIOSockDeviceGetAf(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PULONG                  pulAF = NULL;
    NTSTATUS                status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pulAF), (PVOID*)&pulAF, pLength);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
            "WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
    _Analysis_assume_(*pLength >= sizeof(*pulAF));

    *pulAF = AF_VSOCK;
    *pLength = sizeof(*pulAF);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, AF: %d\n", __FUNCTION__, *pulAF);

    return STATUS_SUCCESS;
}

static
VOID
PIOSockEvtIoDeviceControl(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     OutputBufferLength,
    IN size_t     InputBufferLength,
    IN ULONG      IoControlCode
)
{
    size_t          Length = 0;
    NTSTATUS        status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    switch (IoControlCode)
    {
    case IOCTL_GET_CONFIG:
        status = PIOSockDeviceGetConfig(Request, &Length);
        break;

    case IOCTL_SELECT:
        status = PIOSockSelect(Request, &Length);
        break;

    case IOCTL_GET_AF:
        status = PIOSockDeviceGetAf(Request, &Length);
        break;

    default:
        if (IsControlRequest(Request))
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid socket type\n");
            status = STATUS_NOT_SOCKET;
        }
        else
        {
            status = PIOSockDeviceControl(
                Request,
                IoControlCode,
                &Length);
        }
    }

    if (status != STATUS_PENDING)
        WdfRequestCompleteWithInformation(Request, status, Length);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
}

//////////////////////////////////////////////////////////////////////////
static
NTSTATUS
PIOSockSelectInit(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS                status;
    WDF_OBJECT_ATTRIBUTES   Attributes;
    WDF_WORKITEM_CONFIG     wrkConfig;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    InitializeListHead(&pContext->SelectList);
    pContext->SelectInProgress = 0;

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pContext->ThisDevice;

    status = WdfWaitLockCreate(&Attributes, &pContext->SelectLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT,
            "WdfWaitLockCreate failed (Select): 0x%x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pContext->ThisDevice;

    WDF_WORKITEM_CONFIG_INIT(&wrkConfig, PIOSockSelectWorkitem);
    status = WdfWorkItemCreate(&wrkConfig, &Attributes, &pContext->SelectWorkitem);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT,
            "WdfWorkItemCreate failed (Select): 0x%x\n", status);
        return status;
    }

    PIOSockTimerCreate(&pContext->SelectTimer, pContext->ThisDevice, PIOSockSelectTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT,
            "PIOSockTimerCreate failed (Select): 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);

    return status;
}

static
VOID
PIOSockSelectCleanupFds(
    IN PPIOSOCK_SELECT_PKT      pPkt,
    IN PHYZIO_VSOCK_FDSET_TYPE  iFdSet,
    IN ULONG                    uStartIndex

)
{
    ULONG i;
    PPIOSOCK_SELECT_HANDLE  pHandleSet = &pPkt->Fds[uStartIndex];

    PAGED_CODE();

    for (i = 0; i < pPkt->FdCount[iFdSet]; ++i)
    {
        ASSERT(pHandleSet[i].Socket);

        InterlockedDecrement(&GetSocketContext(pHandleSet[i].Socket)->SelectRefs[iFdSet]); //dereference socket
        WdfObjectDereference(pHandleSet[i].Socket);
    }

    pPkt->FdCount[iFdSet] = 0;
}

__inline
VOID
PIOSockSelectCleanupPkt(
    IN PPIOSOCK_SELECT_PKT pPkt
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s, status: 0x%08x\n", __FUNCTION__, pPkt->Status);

    PIOSockSelectCleanupFds(pPkt, FDSET_READ, 0);
    PIOSockSelectCleanupFds(pPkt, FDSET_WRITE, pPkt->FdCount[FDSET_READ]);
    PIOSockSelectCleanupFds(pPkt, FDSET_EXCPT, pPkt->FdCount[FDSET_READ] + pPkt->FdCount[FDSET_WRITE]);
}

static
VOID
PIOSockSelectTimerFunc(
    IN WDFTIMER Timer
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfTimerGetParentObject(Timer));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    if (InterlockedIncrement(&pContext->SelectInProgress) == 1)
    {
        WdfWorkItemEnqueue(pContext->SelectWorkitem);
    }
}

static
BOOLEAN
PIOSockSelectCheckPkt(
    IN PPIOSOCK_SELECT_PKT  pPkt
)
{
    ULONG i;
    PPIOSOCK_SELECT_HANDLE  pHandleSet;
    PPHYZIO_VSOCK_FD_SET    pFds;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    pFds = &pPkt->pSelect->Fdss[FDSET_READ];
    pHandleSet = pPkt->Fds;

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_READ]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if (pSocket->Events & (FD_ACCEPT_BIT | FD_READ_BIT | FD_CLOSE_BIT))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
    }

    pFds = &pPkt->pSelect->Fdss[FDSET_WRITE];
    pHandleSet = &pPkt->Fds[pPkt->FdCount[FDSET_READ]];

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_WRITE]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if (pSocket->Events & FD_WRITE_BIT ||
            (pSocket->Events & FD_CONNECT_BIT) && NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT]))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
    }

    pFds = &pPkt->pSelect->Fdss[FDSET_EXCPT];
    pHandleSet = &pPkt->Fds[pPkt->FdCount[FDSET_READ] + pPkt->FdCount[FDSET_WRITE]];

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_EXCPT]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if ((pSocket->Events & FD_CONNECT_BIT) &&
            !NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT]))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
    }

    return pPkt->pSelect->Fdss[FDSET_READ].fd_count ||
        pPkt->pSelect->Fdss[FDSET_WRITE].fd_count ||
        pPkt->pSelect->Fdss[FDSET_EXCPT].fd_count;
}

static
VOID
PIOSockSelectCancel(
    IN WDFREQUEST Request
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    if (InterlockedIncrement(&pContext->SelectInProgress) == 1)
    {
        WdfWorkItemEnqueue(pContext->SelectWorkitem);
    }
}

static
VOID
PIOSockSelectWorkitem(
    IN WDFWORKITEM Workitem
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfWorkItemGetParentObject(Workitem));

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    do
    {
        LIST_ENTRY  CompletionList;
        WDFREQUEST  Request;
        LONGLONG    TimePassed, Timeout = LONGLONG_MAX;
        PLIST_ENTRY CurrentItem;
        BOOLEAN     bRemove;

        InterlockedExchange(&pContext->SelectInProgress, 1);

        InitializeListHead(&CompletionList);

        WdfWaitLockAcquire(pContext->SelectLock, NULL);

        TimePassed = PIOSockTimerPassed(&pContext->SelectTimer);

        for (CurrentItem = pContext->SelectList.Flink;
            CurrentItem != &pContext->SelectList;
            CurrentItem = CurrentItem->Flink)
        {
            PPIOSOCK_SELECT_PKT pPkt = CONTAINING_RECORD(CurrentItem, PIOSOCK_SELECT_PKT, ListEntry);
            WDFREQUEST Request = WdfObjectContextGetObject(pPkt);
            NTSTATUS status = WdfRequestUnmarkCancelable(Request);

            ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

            bRemove = FALSE;

            if (status == STATUS_CANCELLED)
            {
                bRemove = TRUE;
                pPkt->Status = STATUS_CANCELLED;
            }

            if (PIOSockSelectCheckPkt(pPkt))
            {
                bRemove = TRUE;
                pPkt->Status = STATUS_SUCCESS;
            }
            else if (pPkt->Timeout)
            {
                if (pPkt->Timeout <= TimePassed + PIOSOCK_TIMER_TOLERANCE)
                {
                    bRemove = TRUE;
                    pPkt->Status = STATUS_TIMEOUT;
                }
                else
                {
                    pPkt->Timeout -= TimePassed;

                    if (pPkt->Timeout < Timeout)
                        Timeout = pPkt->Timeout;
                }
            }

            if (!bRemove)
            {
                status = WdfRequestMarkCancelableEx(Request, PIOSockSelectCancel);

                ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

                if (status == STATUS_CANCELLED)
                {
                    bRemove = TRUE;
                    pPkt->Status = STATUS_CANCELLED;
                }
            }

            if (bRemove)
            {
                CurrentItem = pPkt->ListEntry.Blink;
                RemoveEntryList(&pPkt->ListEntry);
                InsertTailList(&CompletionList, &pPkt->ListEntry);
                if (pPkt->Timeout)
                    PIOSockTimerDeref(&pContext->SelectTimer, TRUE);
            }
        }

        PIOSockTimerSet(&pContext->SelectTimer, Timeout);

        WdfWaitLockRelease(pContext->SelectLock);

        while (!IsListEmpty(&CompletionList))
        {
            PPIOSOCK_SELECT_PKT pPkt = CONTAINING_RECORD(RemoveHeadList(&CompletionList), PIOSOCK_SELECT_PKT, ListEntry);

            PIOSockSelectCleanupPkt(pPkt);
            WdfRequestComplete(WdfObjectContextGetObject(pPkt), pPkt->Status);
        }

    } while (InterlockedCompareExchange(&pContext->SelectInProgress, 0, 1) != 1);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);
}

VOID
PIOSockSelectRun(
    IN PSOCKET_CONTEXT pSocket
)
{
    BOOLEAN bRun = FALSE;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    if (pSocket->SelectRefs[FDSET_READ])
    {
        bRun |= pSocket->Events & (FD_ACCEPT_BIT | FD_READ_BIT | FD_CLOSE_BIT);
    }

    if (pSocket->SelectRefs[FDSET_WRITE])
    {
        bRun |= pSocket->Events & FD_WRITE_BIT ||
            (pSocket->Events & FD_CONNECT_BIT) && NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT]);
    }

    if (pSocket->SelectRefs[FDSET_EXCPT])
    {
        bRun |= (pSocket->Events & FD_CONNECT_BIT) &&
            !NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT]);
    }

    if (bRun && InterlockedIncrement(&pContext->SelectInProgress) == 1)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "Enqueue workitem\n");
        WdfWorkItemEnqueue(pContext->SelectWorkitem);
    }

}

static
BOOLEAN
PIOSockSelectCopyFds(
    IN PDEVICE_CONTEXT          pContext,
    IN BOOLEAN                  bIs32BitProcess,
    IN PPHYZIO_VSOCK_SELECT     pSelect,
    IN PPIOSOCK_SELECT_PKT      pPkt,
    IN PHYZIO_VSOCK_FDSET_TYPE  iFdSet,
    IN ULONG                    uStartIndex
)
{
    ULONG                   i;
    PPIOSOCK_SELECT_HANDLE  pHandleSet = &pPkt->Fds[uStartIndex];

    PAGED_CODE();

    pPkt->FdCount[iFdSet] = 0;
    for (i = 0; i < pSelect->Fdss[iFdSet].fd_count; ++i)
    {
        ULONGLONG hSocket = pSelect->Fdss[iFdSet].fd_array[i];
        if (hSocket)
        {
            WDFFILEOBJECT Socket = PIOSockGetSocketFromHandle(pContext, hSocket, bIs32BitProcess);
            if (Socket != WDF_NO_HANDLE)
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(Socket);

                pHandleSet[i].hSocket = hSocket;
                pHandleSet[i].Socket = Socket;
                InterlockedIncrement(&pSocket->SelectRefs[iFdSet]); //reference socket
            }
            else
                break;
        }
        else
            break;
    }

    pPkt->FdCount[iFdSet] = i;

    return i == pSelect->Fdss[iFdSet].fd_count;
}

static
NTSTATUS
PIOSockSelect(
    IN WDFREQUEST Request,
    IN OUT size_t *pLength
)
{
    PDEVICE_CONTEXT         pContext = GetDeviceContextFromRequest(Request);
    PPHYZIO_VSOCK_SELECT    pSelect;
    SIZE_T                  stSelectLen;
    NTSTATUS                status;
    BOOLEAN                 bIs32BitProcess = FALSE;
    WDF_OBJECT_ATTRIBUTES   Attributes;
    PPIOSOCK_SELECT_PKT     pPkt = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    *pLength = 0;

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pSelect), &pSelect, &stSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stSelectLen >= sizeof(*pSelect));

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pSelect), &pSelect, &stSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stSelectLen >= sizeof(*pSelect));

    if (FD_SETSIZE < pSelect->Fdss[FDSET_READ].fd_count +
        pSelect->Fdss[FDSET_WRITE].fd_count +
        pSelect->Fdss[FDSET_EXCPT].fd_count)
    {
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN64
    bIs32BitProcess = WdfRequestIsFrom32BitProcess(Request);
#endif //_WIN64

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &Attributes,
        PIOSOCK_SELECT_PKT
    );

    status = WdfObjectAllocateContext(Request, &Attributes, &pPkt);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return status;
    }

    if (PIOSockSelectCopyFds(pContext, bIs32BitProcess, pSelect, pPkt, FDSET_READ, 0) &&
        PIOSockSelectCopyFds(pContext, bIs32BitProcess, pSelect, pPkt, FDSET_WRITE, pPkt->FdCount[FDSET_READ]) &&
        PIOSockSelectCopyFds(pContext, bIs32BitProcess, pSelect, pPkt, FDSET_EXCPT,
            pPkt->FdCount[FDSET_READ] + pPkt->FdCount[FDSET_WRITE]))
    {
        pPkt->Status = status = PIOSockSelectCheckPkt(pPkt) ? STATUS_SUCCESS : STATUS_PENDING;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SELECT, "PIOSockSelectCopyFds failed\n");
        status = STATUS_INVALID_HANDLE;
    }

    if (status == STATUS_SUCCESS)
        *pLength = sizeof(*pSelect);

    if (status != STATUS_PENDING)
        PIOSockSelectCleanupPkt(pPkt);
    else
    {
        WdfWaitLockAcquire(pContext->SelectLock, NULL);

        status = WdfRequestMarkCancelableEx(Request, PIOSockSelectCancel);

        ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

        if (NT_SUCCESS(status))
        {
            status = STATUS_PENDING;

            InsertTailList(&pContext->SelectList, &pPkt->ListEntry);
            pPkt->Timeout = pSelect->Timeout;

            if (pPkt->Timeout)
                PIOSockTimerStart(&pContext->SelectTimer, pPkt->Timeout);
        }

        WdfWaitLockRelease(pContext->SelectLock);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);

    return status;
}
