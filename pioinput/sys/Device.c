/*
 * Device related functions
 *
 * Copyright (c) 2016-2017 Blu Tah, Inc.
 *
 * Author(s):
 *  Ladi Prosek <lprosek@blutah.com>
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
#include "pioinput.h"

#if defined(EVENT_TRACING)
#include "Device.tmh"
#endif

EVT_WDF_DEVICE_PREPARE_HARDWARE     PIOInputEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     PIOInputEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             PIOInputEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              PIOInputEvtDeviceD0Exit;

static NTSTATUS PIOInputInitInterruptHandling(IN WDFDEVICE hDevice);
static NTSTATUS PIOInputInitAllQueues(IN WDFOBJECT hDevice);
static VOID PIOInputShutDownAllQueues(IN WDFOBJECT WdfDevice);
static NTSTATUS PIOInputCreateChildPdo(IN WDFDEVICE hDevice);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PIOInputEvtDeviceAdd)
#pragma alloc_text (PAGE, PIOInputEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, PIOInputEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, PIOInputCreateChildPdo)
#pragma alloc_text (PAGE, PIOInputEvtDeviceD0Exit)
#endif

static
NTSTATUS
PIOInputInitInterruptHandling(
    IN WDFDEVICE hDevice)
{
    WDF_INTERRUPT_CONFIG interruptConfig;
    NTSTATUS             status = STATUS_SUCCESS;
    PINPUT_DEVICE        pContext = GetDeviceContext(hDevice);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create control queue interrupt: %x\n", status);
        return status;
    }

    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
        PIOInputInterruptIsr, PIOInputQueuesInterruptDpc);

    interruptConfig.EvtInterruptEnable = PIOInputInterruptEnable;
    interruptConfig.EvtInterruptDisable = PIOInputInterruptDisable;

    status = WdfInterruptCreate(hDevice, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->QueuesInterrupt);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create general queue interrupt: %x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
PIOInputEvtDeviceAdd(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES        Attributes;
    WDFDEVICE                    hDevice;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    PINPUT_DEVICE                pContext = NULL;
    WDF_IO_QUEUE_CONFIG          queueConfig;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDevicePrepareHardware = PIOInputEvtDevicePrepareHardware;
    PnpPowerCallbacks.EvtDeviceReleaseHardware = PIOInputEvtDeviceReleaseHardware;
    PnpPowerCallbacks.EvtDeviceD0Entry = PIOInputEvtDeviceD0Entry;
    PnpPowerCallbacks.EvtDeviceD0Exit = PIOInputEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, INPUT_DEVICE);
    Attributes.SynchronizationScope = WdfSynchronizationScopeDevice;
    status = WdfDeviceCreate(&DeviceInit, &Attributes, &hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreate failed - 0x%x\n", status);
        return status;
    }

    status = PIOInputInitInterruptHandling(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "PIOInputInitInterruptHandling failed - 0x%x\n", status);
    }

    status = WdfDeviceCreateDeviceInterface(
        hDevice,
        &GUID_PIOINPUT_CONTROLLER,
        NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreateDeviceInterface failed - 0x%x\n", status);
        return status;
    }

    pContext = GetDeviceContext(hDevice);

    pContext->EventQMemBlock = pContext->StatusQMemBlock = NULL;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchParallel);

    queueConfig.EvtIoInternalDeviceControl = EvtIoDeviceControl;

    status = WdfIoQueueCreate(
        hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->IoctlQueue);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed - 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);

    status = WdfIoQueueCreate(
        hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->HidQueue);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed - 0x%x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = hDevice;
    status = WdfSpinLockCreate(
        &Attributes,
        &pContext->EventQLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed - 0x%x\n", status);
        return status;
    }
    status = WdfSpinLockCreate(
        &Attributes,
        &pContext->StatusQLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed - 0x%x\n", status);
        return status;
    }

    RtlZeroMemory(&pContext->HidDeviceAttributes, sizeof(HID_DEVICE_ATTRIBUTES));
    pContext->HidDeviceAttributes.Size = sizeof(HID_DEVICE_ATTRIBUTES);
    pContext->HidDeviceAttributes.VendorID = HIDMINI_VID;
    pContext->HidDeviceAttributes.ProductID = HIDMINI_PID;
    pContext->HidDeviceAttributes.VersionNumber = HIDMINI_VERSION;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

static void PIOInputFreeMemBlocks(PINPUT_DEVICE pContext)
{
    if (pContext->EventQMemBlock)
    {
        pContext->EventQMemBlock->destroy(pContext->EventQMemBlock);
        pContext->EventQMemBlock = NULL;
    }
    if (pContext->StatusQMemBlock)
    {
        pContext->StatusQMemBlock->destroy(pContext->StatusQMemBlock);
        pContext->StatusQMemBlock = NULL;
    }
}

NTSTATUS
PIOInputEvtDevicePrepareHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesRaw,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PINPUT_DEVICE pContext = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    status = PhyzIOWdfInitialize(
        &pContext->VDevice,
        Device,
        ResourcesTranslated,
        NULL,
        PIOINPUT_DRIVER_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "PhyzIOWdfInitialize failed with %x\n", status);
        return status;
    }

    pContext->EventQMemBlock = PhyzIOWdfDeviceAllocDmaMemorySliced(
        &pContext->VDevice.PIODevice, PAGE_SIZE, sizeof(PHYZIO_INPUT_EVENT));
    pContext->StatusQMemBlock = PhyzIOWdfDeviceAllocDmaMemorySliced(
        &pContext->VDevice.PIODevice, PAGE_SIZE, sizeof(PHYZIO_INPUT_EVENT_WITH_REQUEST));

    if (!pContext->EventQMemBlock || !pContext->StatusQMemBlock) {
        PIOInputFreeMemBlocks(pContext);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Figure out what kind of input device this is and build a
    // corresponding HID report descriptor.
    status = PIOInputBuildReportDescriptor(pContext);

    if (NT_SUCCESS(status) && !pContext->bChildPdoCreated)
    {
        // Create a child PDO with an instance path based on the
        // HID report descriptor (hash). This is to make sure that
        // the devnode won't be reused when a different phyzio
        // input device is plugged into the same PCI slot.
        // piohidkmdf.sys (build by the hidpassthrough project) is
        // the FDO for the child, passing IOCTL IRPs back to us.
        status = PIOInputCreateChildPdo(Device);
        if (NT_SUCCESS(status))
        {
            pContext->bChildPdoCreated = TRUE;
        }
    }

    if (!NT_SUCCESS(status))
    {
        PIOInputFreeMemBlocks(pContext);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

static NTSTATUS
PIOInputCreateChildPdo(
    IN WDFDEVICE hDevice)
{
    PINPUT_DEVICE pContext = GetDeviceContext(hDevice);
    PWDFDEVICE_INIT pDeviceInit = NULL;
    PPDO_EXTENSION PdoExtension;
    WDFDEVICE hChild;
    WDF_OBJECT_ATTRIBUTES pdoAttributes;
    WDF_DEVICE_PNP_CAPABILITIES PnpCaps;
    NTSTATUS status = STATUS_SUCCESS;

    DECLARE_CONST_UNICODE_STRING(deviceLocation, L"PIOINPUT");
    DECLARE_CONST_UNICODE_STRING(deviceId, L"PIOINPUT\\REV_01");
    DECLARE_UNICODE_STRING_SIZE(buffer, 32);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    pDeviceInit = WdfPdoInitAllocate(hDevice);
    if (pDeviceInit == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    status = WdfPdoInitAssignDeviceID(pDeviceInit, &deviceId);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }
    status = WdfPdoInitAddHardwareID(pDeviceInit, &deviceId);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }
    status = WdfPdoInitAddCompatibleID(pDeviceInit, &deviceId);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }

    status = RtlUnicodeStringPrintf(
        &buffer,
        L"%08I64x",
        pContext->HidReportDescriptorHash);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }
    status = WdfPdoInitAssignInstanceID(pDeviceInit, &buffer);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttributes, PDO_EXTENSION);
    status = WdfDeviceCreate(
        &pDeviceInit,
        &pdoAttributes,
        &hChild);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }
    pDeviceInit = NULL;

    // hide the child from Device Manager
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&PnpCaps);
    PnpCaps.NoDisplayInUI = WdfTrue;
    PnpCaps.UniqueID = WdfFalse;
    WdfDeviceSetPnpCapabilities(hChild, &PnpCaps);

    // initialize the PDO extension
    PdoExtension = PdoGetExtension(hChild);
    RtlZeroMemory(PdoExtension, sizeof(PDO_EXTENSION));
    PdoExtension->Version = PDO_EXTENSION_VERSION;
    PdoExtension->BusFdo = WdfDeviceWdmGetDeviceObject(hDevice);

    // add the child
    status = WdfFdoAddStaticChild(hDevice, hChild);
    if (!NT_SUCCESS(status))
    {
        WdfObjectDelete(hChild);
    }

Exit:
    if (pDeviceInit != NULL)
    {
        WdfDeviceInitFree(pDeviceInit);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
PIOInputEvtDeviceReleaseHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PINPUT_DEVICE pContext = GetDeviceContext(Device);
    PSINGLE_LIST_ENTRY entry;
    ULONG i;

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    PhyzIOWdfShutdown(&pContext->VDevice);

    for (i = 0; i < pContext->uNumOfClasses; i++)
    {
        PINPUT_CLASS_COMMON pClass = pContext->InputClasses[i];
        if (pClass->CleanupFunc)
        {
            pClass->CleanupFunc(pClass);
        }
        PIOInputFree(&pClass->pHidReport);
        PIOInputFree(&pClass);
    }
    pContext->uNumOfClasses = 0;

    PIOInputFree(&pContext->HidReportDescriptor);

    PIOInputFreeMemBlocks(pContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

static
NTSTATUS
PIOInputInitAllQueues(
    IN WDFOBJECT Device)
{
    NTSTATUS status = STATUS_SUCCESS;
    PINPUT_DEVICE pContext = GetDeviceContext(Device);

    struct virtqueue *vqs[2];
    PHYZIO_WDF_QUEUE_PARAM params[2];

    // event
    params[0].Interrupt = pContext->QueuesInterrupt;

    // status
    params[1].Interrupt = pContext->QueuesInterrupt;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    status = PhyzIOWdfInitQueues(&pContext->VDevice, 2, vqs, params);
    if (NT_SUCCESS(status))
    {
        pContext->EventQ = vqs[0];
        pContext->StatusQ = vqs[1];
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "PhyzIOWdfInitQueues returned %x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID
PIOInputShutDownAllQueues(IN WDFOBJECT WdfDevice)
{
    PINPUT_DEVICE pContext = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    PhyzIOWdfDestroyQueues(&pContext->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
PIOInputFillEventQueue(PINPUT_DEVICE pContext)
{
    NTSTATUS status = STATUS_SUCCESS;
    PPHYZIO_INPUT_EVENT buf = NULL;
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

    for (;;)
    {
        PHYSICAL_ADDRESS pa;
        buf = pContext->EventQMemBlock->get_slice(pContext->EventQMemBlock, &pa);
        if (buf == NULL)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "PHYZIO_INPUT_EVENT alloc failed\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        WdfSpinLockAcquire(pContext->EventQLock);
        status = PIOInputAddInBuf(pContext->EventQ, buf, pa);
        WdfSpinLockRelease(pContext->EventQLock);
        if (!NT_SUCCESS(status))
        {
            pContext->EventQMemBlock->return_slice(pContext->EventQMemBlock, buf);
            break;
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

static NTSTATUS
PIOInputAddBuf(
    IN struct virtqueue *vq,
    IN PPHYZIO_INPUT_EVENT buf,
    IN PHYSICAL_ADDRESS pa,
    IN BOOLEAN out)
{
    NTSTATUS  status = STATUS_SUCCESS;
    struct PhyzIOBufferDescriptor sg;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s  buf = %p, pa %I64x\n", __FUNCTION__, buf, pa.QuadPart);
    if (buf == NULL)
    {
        ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (vq == NULL)
    {
        ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    sg.physAddr = pa;
    sg.length = sizeof(PHYZIO_INPUT_EVENT);

    if (0 > virtqueue_add_buf(vq, &sg, (out ? 1 : 0), (out ? 0 : 1), buf, NULL, 0))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "<-- %s cannot add_buf\n", __FUNCTION__);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    virtqueue_kick(vq);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
PIOInputAddInBuf(
    IN struct virtqueue *vq,
    IN PPHYZIO_INPUT_EVENT buf,
    IN PHYSICAL_ADDRESS pa)
{
    return PIOInputAddBuf(vq, buf, pa, FALSE);
}

NTSTATUS
PIOInputAddOutBuf(
    IN struct virtqueue *vq,
    IN PPHYZIO_INPUT_EVENT buf,
    IN PHYSICAL_ADDRESS pa)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_QUEUEING, "%s %p\n", __FUNCTION__, buf);
    return PIOInputAddBuf(vq, buf, pa, TRUE);
}

NTSTATUS
PIOInputEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PrepiousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PINPUT_DEVICE pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PrepiousState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    status = PIOInputInitAllQueues(Device);
    if (NT_SUCCESS(status))
    {
        PhyzIOWdfSetDriverOK(&pContext->VDevice);
        PIOInputFillEventQueue(pContext);
    }
    else
    {
        PhyzIOWdfSetDriverFailed(&pContext->VDevice);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
PIOInputEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState)
{
    PINPUT_DEVICE pContext = GetDeviceContext(Device);
    PPHYZIO_INPUT_EVENT buf;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s TargetState: %d\n",
        __FUNCTION__, TargetState);

    PAGED_CODE();

    // reset the device to make sure it's not processing the event queue anymore
    phyzio_device_reset(&pContext->VDevice.PIODevice);

    // now with the queue stopped, free the buffers we've pushed to it
    if (pContext->EventQ)
    {
        while (buf = (PPHYZIO_INPUT_EVENT)virtqueue_detach_unused_buf(pContext->EventQ))
        {
            pContext->EventQMemBlock->return_slice(pContext->EventQMemBlock, buf);
        }
    }
    PIOInputShutDownAllQueues(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}
