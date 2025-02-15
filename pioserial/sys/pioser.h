/*
 * Main include file
 * This file contains various routines and globals
 *
 * Copyright (c) 2010-2017 Blu Tah, Inc.
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
#if !defined(PIOSERIAL_H)
#define PIOSERIAL_H
#include "public.h"

EVT_WDF_DRIVER_DEVICE_ADD PIOSerialEvtDeviceAdd;

EVT_WDF_INTERRUPT_ISR                           PIOSerialInterruptIsr;
EVT_WDF_INTERRUPT_DPC                           PIOSerialInterruptDpc;
EVT_WDF_INTERRUPT_DPC                           PIOSerialQueuesInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE                        PIOSerialInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE                       PIOSerialInterruptDisable;


#define PHYZIO_CONSOLE_F_SIZE      0
#define PHYZIO_CONSOLE_F_MULTIPORT 1
#define PHYZIO_CONSOLE_BAD_ID      (~(u32)0)


#define PHYZIO_CONSOLE_DEVICE_READY     0
#define PHYZIO_CONSOLE_PORT_ADD         1
#define PHYZIO_CONSOLE_PORT_REMOVE      2
#define PHYZIO_CONSOLE_PORT_READY       3

#define PHYZIO_CONSOLE_CONSOLE_PORT     4
#define PHYZIO_CONSOLE_RESIZE           5
#define PHYZIO_CONSOLE_PORT_OPEN        6
#define PHYZIO_CONSOLE_PORT_NAME        7

// This is the value of the IOCTL_GET_INFORMATION macro used by older versions
// of the driver. We still respond to it for backward compatibility. New clients
// should use the new value declared in public.h.
#define IOCTL_GET_INFORMATION_BUFFERED CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack (push)
#pragma pack (1)

typedef struct _tagConsoleConfig {
    //* columns of the screens
    u16 cols;
    //* rows of the screens
    u16 rows;
    //* max. number of ports this device can hold
    u32 max_nr_ports;
} CONSOLE_CONFIG, * PCONSOLE_CONFIG;
#pragma pack (pop)


#pragma pack (push)
#pragma pack (1)
typedef struct _tagPhyzioConsoleControl {
    u32 id;
    u16 event;
    u16 value;
}PHYZIO_CONSOLE_CONTROL, * PPHYZIO_CONSOLE_CONTROL;
#pragma pack (pop)


typedef struct _tagPortDevice
{
    PHYZIO_WDF_DRIVER   VDevice;

    WDFINTERRUPT        WdfInterrupt;
    WDFINTERRUPT        QueuesInterrupt;

    int                 isHostMultiport;

    CONSOLE_CONFIG      consoleConfig;
    struct virtqueue    *c_ivq, *c_ovq;
    struct virtqueue    **in_vqs, **out_vqs;
    WDFSPINLOCK         CInVqLock;
    WDFWAITLOCK         COutVqLock;

    BOOLEAN             DeviceOK;
    UINT                DeviceId;
    ULONG               DmaGroupTag;
    PPHYZIO_DMA_MEMORY_SLICED
                        ControlDmaBlock;
} PORTS_DEVICE, *PPORTS_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PORTS_DEVICE, GetPortsDevice)

#define PIOSERIAL_DRIVER_MEMORY_TAG (ULONG)'rsIV'

#define  PORT_DEVICE_ID L"{6FDE7547-1B65-48ae-B628-80BE62016026}\\PIOSerialPort\0"

DEFINE_GUID(GUID_DEVCLASS_PORT_DEVICE,
0x6fde7547, 0x1b65, 0x48ae, 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x1, 0x60, 0x26);
// {6FDE7547-1B65-48ae-B628-80BE62016026}



#define DEVICE_DESC_LENGTH  128

typedef struct _tagPortBuffer
{
    PHYSICAL_ADDRESS    pa_buf;
    PVOID               va_buf;
    size_t              size;
    size_t              len;
    size_t              offset;
    PhyzIODevice        *vdev;
} PORT_BUFFER, * PPORT_BUFFER;

typedef struct _WriteBufferEntry
{
    SINGLE_LIST_ENTRY ListEntry;
    WDFMEMORY EntryHandle;
    WDFREQUEST Request;
    PVOID      OriginalWriteBuffer;
    SIZE_T     OriginalWriteBufferSize;
    WDFDMATRANSACTION dmaTransaction;
} WRITE_BUFFER_ENTRY, *PWRITE_BUFFER_ENTRY;

typedef struct _tagPioSerialPort
{
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;

    WDFDEVICE           BusDevice;
    WDFDEVICE           Device;

    PPORT_BUFFER        InBuf;
    WDFSPINLOCK         InBufLock;
    WDFSPINLOCK         OutVqLock;
    ANSI_STRING         NameString;
    UINT                PortId;
    ULONG               DmaGroupTag;
    UINT                DeviceId;
    BOOLEAN             OutVqFull;
    BOOLEAN             HostConnected;
    BOOLEAN             MoozeConnected;

    BOOLEAN             Removed;
    WDFQUEUE            ReadQueue;
    WDFREQUEST          PendingReadRequest;

    // Hold a list of allocated buffers which were written to the virt queue
    // and was not returned yet.
    SINGLE_LIST_ENTRY   WriteBuffersList;

    WDFQUEUE            WriteQueue;
    WDFQUEUE            IoctlQueue;
} PIOSERIAL_PORT, *PPIOSERIAL_PORT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIOSERIAL_PORT, SerialPortGetData)


typedef struct _tagRawPdoPioSerialPort
{
    PPIOSERIAL_PORT port;
} RAWPDO_PIOSERIAL_PORT, *PRAWPDO_PIOSERIAL_PORT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RAWPDO_PIOSERIAL_PORT, RawPdoSerialPortGetData)


typedef struct _tagDriverContext
{
    // one global lookaside owned by the driver object
    WDFLOOKASIDE WriteBufferLookaside;
} DRIVER_CONTEXT, *PDRIVER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DRIVER_CONTEXT, GetDriverContext)


NTSTATUS
PIOSerialFillQueue(
    IN struct virtqueue *vq,
    IN WDFSPINLOCK Lock,
    IN ULONG id /* unique id to free all the blocks related to the queue */
);

VOID
PIOSerialDrainQueue(
    IN struct virtqueue *vq
    );

NTSTATUS
PIOSerialAddInBuf(
    IN struct virtqueue *vq,
    IN PPORT_BUFFER buf
);

VOID
PIOSerialProcessInputBuffers(
    IN PPIOSERIAL_PORT port
);

BOOLEAN
PIOSerialReclaimConsumedBuffers(
    IN PPIOSERIAL_PORT port
);

size_t
PIOSerialSendBuffers(
    IN PPIOSERIAL_PORT Port,
    IN PWRITE_BUFFER_ENTRY Entry
);

SSIZE_T
PIOSerialFillReadBufLocked(
    IN PPIOSERIAL_PORT port,
    IN PVOID outbuf,
    IN SIZE_T count
);

PPORT_BUFFER
PIOSerialAllocateSinglePageBuffer(
    IN PhyzIODevice *vdev,
    IN ULONG id
);

VOID
PIOSerialFreeBuffer(
    IN PPORT_BUFFER buf
);

VOID
PIOSerialSendCtrlMsg(
    IN WDFDEVICE hDevice,
    IN ULONG id,
    IN USHORT event,
    IN USHORT value
);

VOID
PIOSerialCtrlWorkHandler(
    IN WDFDEVICE Device
);

PPIOSERIAL_PORT
PIOSerialFindPortById(
    IN WDFDEVICE Device,
    IN ULONG id
);

VOID
PIOSerialAddPort(
    IN WDFDEVICE Device,
    IN ULONG id
);

VOID
PIOSerialRemovePort(
    IN WDFDEVICE Device,
    IN PPIOSERIAL_PORT port
);

VOID
PIOSerialInitPortConsole(
    IN WDFDEVICE Device,
    IN PPIOSERIAL_PORT port
);

VOID
PIOSerialDiscardPortDataLocked(
    IN PPIOSERIAL_PORT port
);

BOOLEAN
PIOSerialPortHasDataLocked(
    IN PPIOSERIAL_PORT port
);

PVOID
PIOSerialGetInBuf(
    IN PPIOSERIAL_PORT port
);

BOOLEAN
PIOSerialWillWriteBlock(
    IN PPIOSERIAL_PORT port
);

VOID
PIOSerialEnableInterruptQueue(IN struct virtqueue *vq);

VOID
PIOSerialDisableInterruptQueue(IN struct virtqueue *vq);

#ifndef _IRQL_requires_
#define _IRQL_requires_(level)
#endif
#ifndef _Analysis_assume_
#define _Analysis_assume_(expr)
#endif

EVT_WDF_CHILD_LIST_CREATE_DEVICE PIOSerialDeviceListCreatePdo;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_COMPARE PIOSerialEvtChildListIdentificationDescriptionCompare;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_CLEANUP PIOSerialEvtChildListIdentificationDescriptionCleanup;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_DUPLICATE PIOSerialEvtChildListIdentificationDescriptionDuplicate;

// IO queue callbacks generally run at IRQL <= DISPATCH_LEVEL but our port
// devices use WdfExecutionLevelPassive so they are guaranteed to run at
// PASSIVE_LEVEL. Annotate the prototypes to make static analysis happy.
EVT_WDF_IO_QUEUE_IO_READ _IRQL_requires_(PASSIVE_LEVEL) PIOSerialPortRead;
EVT_WDF_IO_QUEUE_IO_WRITE _IRQL_requires_(PASSIVE_LEVEL) PIOSerialPortWrite;
EVT_WDF_IO_QUEUE_IO_STOP _IRQL_requires_(PASSIVE_LEVEL) PIOSerialPortReadIoStop;
EVT_WDF_IO_QUEUE_IO_STOP _IRQL_requires_(PASSIVE_LEVEL) PIOSerialPortWriteIoStop;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL _IRQL_requires_(PASSIVE_LEVEL) PIOSerialPortDeviceControl;
EVT_WDF_REQUEST_CANCEL PIOSerialPortWriteRequestCancel;

EVT_WDF_DEVICE_FILE_CREATE PIOSerialPortCreate;
EVT_WDF_FILE_CLOSE PIOSerialPortClose;

VOID
PIOSerialPortCreateName (
    IN WDFDEVICE WdfDevice,
    IN PPIOSERIAL_PORT port,
    IN PPORT_BUFFER buf
);

VOID
PIOSerialPortPnpNotify (
    IN WDFDEVICE WdfDevice,
    IN PPIOSERIAL_PORT port,
    IN BOOLEAN connected
);

VOID
PIOSerialPortCreateSymbolicName(
    IN WDFWORKITEM  WorkItem
);

__inline
struct
virtqueue*
GetInQueue (
    IN PPIOSERIAL_PORT port
)
{
    PPORTS_DEVICE    pContext = NULL;
    ASSERT (port);
    ASSERT (port->BusDevice);
    pContext = GetPortsDevice(port->BusDevice);
    ASSERT (pContext->in_vqs);
    return pContext->in_vqs[port->PortId];
};

__inline
struct
virtqueue*
GetOutQueue (
    IN PPIOSERIAL_PORT port
)
{
    PPORTS_DEVICE    pContext = NULL;
    ASSERT (port);
    ASSERT (port->BusDevice);
    pContext = GetPortsDevice(port->BusDevice);
    ASSERT (pContext->out_vqs);
    return pContext->out_vqs[port->PortId];
};

#endif /* PIOSERIAL_H */
