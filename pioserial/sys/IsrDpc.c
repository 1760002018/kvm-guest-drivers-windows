#include "precomp.h"
#include "pioser.h"

#if defined(EVENT_TRACING)
#include "IsrDpc.tmh"
#endif

BOOLEAN
PIOSerialInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG MessageID)
{
    PPORTS_DEVICE pContext = GetPortsDevice(WdfInterruptGetDevice(Interrupt));
    WDF_INTERRUPT_INFO info;
    BOOLEAN serviced;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Interrupt, &info);

    // Schedule a DPC if the device is using message-signaled interrupts, or
    // if the device ISR status is enabled.
    if (info.MessageSignaled || PhyzIOWdfGetISRStatus(&pContext->VDevice))
    {
        WdfInterruptQueueDpcForIsr(Interrupt);
        serviced = TRUE;
    }
    else
    {
        serviced = FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "<-- %s Serviced: %d\n", __FUNCTION__, serviced);

    return serviced;
}

VOID
PIOSerialInterruptDpc(
    IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT AssociatedObject)
{
    WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
    PPORTS_DEVICE Context = GetPortsDevice(Device);
    WDF_INTERRUPT_INFO info;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    PIOSerialCtrlWorkHandler(Device);

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Context->QueuesInterrupt, &info);

    // Using the queues' DPC if only one interrupt is available.
    if (info.Vector == 0)
    {
        PIOSerialQueuesInterruptDpc(Interrupt, AssociatedObject);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

VOID PIOSerialQueuesInterruptDpc(IN WDFINTERRUPT Interrupt,
                                 IN WDFOBJECT AssociatedObject)
{
    NTSTATUS status;
    WDFCHILDLIST PortList;
    WDFDEVICE Device;
    WDF_CHILD_LIST_ITERATOR iterator;
    WDF_CHILD_RETRIEVE_INFO PortInfo;
    PIOSERIAL_PORT VirtPort;
    PIOSERIAL_PORT *Port;

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    PortList = WdfFdoGetDefaultChildList(WdfInterruptGetDevice(Interrupt));
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator, WdfRetrievePresentChildren);

    WdfChildListBeginIteration(PortList, &iterator);
    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO_INIT(&PortInfo, &VirtPort.Header);
        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&VirtPort.Header,
            sizeof(VirtPort));

        status = WdfChildListRetrieveNextDevice(PortList, &iterator, &Device,
            &PortInfo);

        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }

        Port = RawPdoSerialPortGetData(Device)->port;

        // handle the read queue
        PIOSerialProcessInputBuffers(Port);

        // handle the write queue
        PIOSerialReclaimConsumedBuffers(Port);
    }
    WdfChildListEndIteration(PortList, &iterator);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

static
VOID
PIOSerialEnableInterrupt(PPORTS_DEVICE pContext)
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if(!pContext)
        return;

    if(pContext->c_ivq)
    {
        virtqueue_enable_cb(pContext->c_ivq);
        virtqueue_kick(pContext->c_ivq);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s enable\n", __FUNCTION__);
}

static
VOID
PIOSerialDisableInterrupt(PPORTS_DEVICE pContext)
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if(!pContext)
        return;

    if(pContext->c_ivq)
    {
        virtqueue_disable_cb(pContext->c_ivq);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s disable\n", __FUNCTION__);
}


NTSTATUS
PIOSerialInterruptEnable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    PIOSerialEnableInterrupt(GetPortsDevice(WdfInterruptGetDevice(Interrupt)));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
PIOSerialInterruptDisable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    PIOSerialDisableInterrupt(GetPortsDevice(WdfInterruptGetDevice(Interrupt)));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
PIOSerialEnableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if(!vq)
        return;

    virtqueue_enable_cb(vq);
    virtqueue_kick(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

VOID
PIOSerialDisableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if(!vq)
        return;

    virtqueue_disable_cb(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

