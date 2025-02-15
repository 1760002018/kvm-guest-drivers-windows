/*
* Copyright (C) 2018 Blu Tah, Inc.
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

#include "driver.h"
#include "piocrypt-public.h"

#ifndef NO_WPP
#include "piocrypt.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, PioCryptDeviceAdd)
#pragma alloc_text(PAGE, PioCryptDriverContextCleanup)
#pragma alloc_text(PAGE, PioCryptDevicePrepareHardware)
#endif

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    // Initialize the WPP tracing.
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    Trace(TRACE_LEVEL_VERBOSE, "[%s] -->", __FUNCTION__);

    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = PioCryptDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, PioCryptDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes,
        &config, WDF_NO_HANDLE);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfDriverCreate failed: status %X", __FUNCTION__, status);
        WPP_CLEANUP(DriverObject);
    }

    Trace(TRACE_LEVEL_VERBOSE, "[%s] -->", __FUNCTION__);

    return status;
}

NTSTATUS PioCryptDeviceAdd(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFQUEUE queue;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_INTERRUPT_CONFIG interruptConfig;
    PDEVICE_CONTEXT context;

    UNREFERENCED_PARAMETER(Driver);

    Trace(TRACE_LEVEL_VERBOSE, "[%s] -->", __FUNCTION__);

    PAGED_CODE();

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware = PioCryptDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = PioCryptDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = PioCryptDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = PioCryptDeviceD0Exit;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = PioCryptDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfDeviceCreate failed: status %X", __FUNCTION__, status);
        return status;
    }

    context = GetDeviceContext(device);

    RtlZeroMemory(context, sizeof(*context));
    InitializeListHead(&context->PendingBuffers);

    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, PioCryptInterruptIsr, PioCryptInterruptDpc);

    interruptConfig.EvtInterruptEnable = PioCryptInterruptEnable;
    interruptConfig.EvtInterruptDisable = PioCryptInterruptDisable;

    status = WdfInterruptCreate(device, &interruptConfig,
        WDF_NO_OBJECT_ATTRIBUTES, &context->WdfInterrupt);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfInterruptCreate failed: status %X", __FUNCTION__, status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes,
        &context->VirtQueueLock);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfSpinLockCreate failed: status %X", __FUNCTION__, status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device,
        &GUID_DEVINTERFACE_PIOCRYPT, NULL);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfDeviceCreateDeviceInterface failed: status %X", __FUNCTION__, status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = PioCryptIoControl;
    queueConfig.EvtIoStop = PioCryptIoStop;
    queueConfig.AllowZeroLengthRequests = FALSE;

    status = WdfIoQueueCreate(device, &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES, &queue);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfIoQueueCreate failed: status %X", __FUNCTION__, status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(device, queue, WdfRequestTypeDeviceControl);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfDeviceConfigureRequestDispatching failed: status %X", __FUNCTION__, status);
        return status;
    }

    Trace(TRACE_LEVEL_VERBOSE, "[%s] <--", __FUNCTION__);

    return status;
}

VOID PioCryptDriverContextCleanup(IN WDFOBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    PAGED_CODE();

    // Stop the WPP tracing.
    WPP_CLEANUP(WdfDriverWdmGetDriverObject(DriverObject));
}

NTSTATUS PioCryptInterruptEnable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE wdfDevice)
{
    PDEVICE_CONTEXT context = GetDeviceContext(wdfDevice);
    UNREFERENCED_PARAMETER(Interrupt);

    virtqueue_enable_cb(context->ControlQueue);
    virtqueue_kick(context->ControlQueue);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    return STATUS_SUCCESS;
}

NTSTATUS PioCryptInterruptDisable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE wdfDevice)
{
    PDEVICE_CONTEXT context = GetDeviceContext(wdfDevice);
    UNREFERENCED_PARAMETER(Interrupt);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    virtqueue_disable_cb(context->ControlQueue);

    return STATUS_SUCCESS;
}

BOOLEAN PioCryptInterruptIsr(IN WDFINTERRUPT Interrupt, IN ULONG MessageId)
{
    PDEVICE_CONTEXT context = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));
    WDF_INTERRUPT_INFO info;
    BOOLEAN processed;

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(context->WdfInterrupt, &info);

    processed = ((info.MessageSignaled && (MessageId == 0)) ||
        PhyzIOWdfGetISRStatus(&context->VDevice));
    
    if (processed)
    {
        WdfInterruptQueueDpcForIsr(Interrupt);
    }

    Trace(TRACE_LEVEL_VERBOSE, "[%s] %sprocessed", __FUNCTION__, processed ? "" : "not ");

    return processed;
}

VOID PioCryptInterruptDpc(IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT AssociatedObject)
{
    PDEVICE_CONTEXT context = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));
    UNREFERENCED_PARAMETER(AssociatedObject);
    UNREFERENCED_PARAMETER(context);
}

NTSTATUS PioCryptDevicePrepareHardware(IN WDFDEVICE Device,
    IN WDFCMRESLIST Resources,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Resources);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    PAGED_CODE();

    status = PhyzIOWdfInitialize(
        &context->VDevice,
        Device,
        ResourcesTranslated,
        NULL,
        PIO_CRYPT_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "PhyzIOWdfInitialize failed with %x\n", status);
    }

    return status;
}

NTSTATUS PioCryptDeviceReleaseHardware(IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    PhyzIOWdfShutdown(&context->VDevice);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    return STATUS_SUCCESS;
}

NTSTATUS PioCryptDeviceD0Entry(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE PrepiousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    PHYZIO_WDF_QUEUE_PARAM param;
    struct virtqueue *queues[1];

    Trace(TRACE_LEVEL_VERBOSE, "[%s] from D%d", __FUNCTION__, PrepiousState - WdfPowerDeviceD0);

    PAGED_CODE();

    if (NT_SUCCESS(status))
    {
        param.Interrupt = context->WdfInterrupt;
        status = PhyzIOWdfInitQueues(&context->VDevice, 1, queues, &param);
    }

    if (NT_SUCCESS(status))
    {
        context->ControlQueue = queues[0];
        PhyzIOWdfSetDriverOK(&context->VDevice);
    }
    else
    {
        PhyzIOWdfSetDriverFailed(&context->VDevice);
        Trace(TRACE_LEVEL_ERROR, "[%s] PhyzIOWdfInitQueues failed with %x\n", __FUNCTION__, status);
    }

    return status;
}

NTSTATUS PioCryptDeviceD0Exit(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE TargetState)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    Trace(TRACE_LEVEL_VERBOSE, "[%s] to D%d", __FUNCTION__, TargetState - WdfPowerDeviceD0);

    PAGED_CODE();

    PhyzIOWdfDestroyQueues(&context->VDevice);

    return STATUS_SUCCESS;
}

VOID PioCryptDeviceContextCleanup(IN WDFOBJECT DeviceObject)
{
    PDEVICE_CONTEXT context = GetDeviceContext(DeviceObject);
    BOOLEAN bHasBuffers = !IsListEmpty(&context->PendingBuffers);
    Trace(TRACE_LEVEL_VERBOSE, "[%s] %s", __FUNCTION__, bHasBuffers ? "Has unfreed buffers" : "");
    // TODO: free buffers allocated by the driver
}

VOID PioCryptIoControl
(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode)
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    UNREFERENCED_PARAMETER(Queue);
    Trace(TRACE_LEVEL_INFORMATION, "[%s] code %X, in %lld, out %lld",
        __FUNCTION__, IoControlCode, InputBufferLength, OutputBufferLength);
    if (status != STATUS_PENDING)
    {
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
    }
}

VOID PioCryptIoStop(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags)
{
    BOOLEAN bCancellable = (ActionFlags & WdfRequestStopRequestCancelable) != 0;
    UNREFERENCED_PARAMETER(Queue);

    Trace(TRACE_LEVEL_INFORMATION, "[%s] Req %p, action %X, the request is %scancellable",
        __FUNCTION__, Request, ActionFlags, bCancellable ? "" : "not ");

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        // the driver owns the request and it will not be able to process it
        Trace(TRACE_LEVEL_INFORMATION, "[%s] Req %p can't be suspended", __FUNCTION__, Request);
        if (!bCancellable || WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, STATUS_CANCELLED);
        }
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        Trace(TRACE_LEVEL_INFORMATION, "[%s] Req %p purged", __FUNCTION__, Request);
        if (!bCancellable || WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, STATUS_CANCELLED);
        }
    }
}
