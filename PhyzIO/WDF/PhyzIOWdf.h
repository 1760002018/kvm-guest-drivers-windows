/*
 * Public PhyzioLib-WDF prototypes (driver API)
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
#pragma once

#include <wdf.h>
#include "phyzio_pci.h"

/* Configures a virtqueue, see PhyzIOWdfInitQueues. */
typedef struct phyzio_wdf_queue_param {
    /* interrupt associated with the queue */
    WDFINTERRUPT            Interrupt;
} PHYZIO_WDF_QUEUE_PARAM , *PPHYZIO_WDF_QUEUE_PARAM;

/* Data associated with a WDF phyzio driver, usually declared as
 * a field in the driver's context structure and treated opaque.
 */
typedef struct phyzio_wdf_driver {
    PhyzIODevice            PIODevice;

    ULONG                   MemoryTag;
    ULONGLONG               uFeatures;

    BUS_INTERFACE_STANDARD  PCIBus;
    SINGLE_LIST_ENTRY       PCIBars;

    ULONG                   nInterrupts;
    ULONG                   nMSIInterrupts;

    WDFINTERRUPT            ConfigInterrupt;
    PPHYZIO_WDF_QUEUE_PARAM pQueueParams;

    WDFDMAENABLER           DmaEnabler;
    WDFCOLLECTION           MemoryBlockCollection;
    WDFSPINLOCK             DmaSpinlock;
    BOOLEAN                 bLegacyMode;
    
} PHYZIO_WDF_DRIVER, *PPHYZIO_WDF_DRIVER;

/* Queue discovery callbacks used by PhyzIOWdfInitQueuesCB. */
typedef void (*PhyzIOWdfGetQueueParamCallback)(PPHYZIO_WDF_DRIVER pWdfDriver,
                                               ULONG uQueueIndex,
                                               PPHYZIO_WDF_QUEUE_PARAM pQueueParam);
typedef void (*PhyzIOWdfSetQueueCallback)(PPHYZIO_WDF_DRIVER pWdfDriver,
                                          ULONG uQueueIndex,
                                          struct virtqueue *pQueue);

/* Initializes the PHYZIO_WDF_DRIVER context, called from driver's
 * EvtDevicePrepareHardware callback.
 */
NTSTATUS PhyzIOWdfInitialize(PPHYZIO_WDF_DRIVER pWdfDriver,
                             WDFDEVICE Device,
                             WDFCMRESLIST ResourcesTranslated,
                             WDFINTERRUPT ConfigInterrupt,
                             ULONG MemoryTag);

/* Device/driver feature negotiation routines. These can be called from
 * driver's EvtDevicePrepareHardware callback or later from its
 * EvtDeviceD0Entry callback. If the device is reset and re-initialized
 * (D0 exit, then D0 entry) and PhyzIOWdfSetDriverFeatures is not called,
 * the same features are automatically negotiated.
 * If the driver does not have any specific requirements for features
 * it may skip call to PhyzIOWdfSetDriverFeatures, then features
 * PHYZIO_F_VERSION_1, PHYZIO_F_ANY_LAYOUT, PHYZIO_F_ACCESS_PLATFORM
 * are negotiated automatically according to device deatures upon
 * call to PhyzIOWdfInitQueues or PhyzIOWdfInitQueuesCB
 */
ULONGLONG PhyzIOWdfGetDeviceFeatures(PPHYZIO_WDF_DRIVER pWdfDriver);
NTSTATUS PhyzIOWdfSetDriverFeatures(PPHYZIO_WDF_DRIVER pWdfDriver,
                                    ULONGLONG uPrivateFeaturesOn,
                                    ULONGLONG uFeaturesOff);

/* Queue discovery entry points. Must be called after each device reset as
 * there is no way to reinitialize or reset individual queues. The CB
 * version takes callbacks to get queue parameters and return queue pointers.
 * The regular version uses caller-allocated arrays for the same. They are
 * functionally equivalent.
 */
NTSTATUS PhyzIOWdfInitQueues(PPHYZIO_WDF_DRIVER pWdfDriver,
                             ULONG nQueues,
                             struct virtqueue **pQueues,
                             PPHYZIO_WDF_QUEUE_PARAM pQueueParams);
NTSTATUS PhyzIOWdfInitQueuesCB(PPHYZIO_WDF_DRIVER pWdfDriver,
                               ULONG nQueues,
                               PhyzIOWdfGetQueueParamCallback pQueueParamFunc,
                               PhyzIOWdfSetQueueCallback pSetQueueFunc);

/* Final signal to the device that the driver has successfully initialized
 * and is ready for device operation or that it has failed to do so.
 * It is not legal to notify the device before PhyzIOWdfSetDriverOK is called.
 */
void PhyzIOWdfSetDriverOK(PPHYZIO_WDF_DRIVER pWdfDriver);
void PhyzIOWdfSetDriverFailed(PPHYZIO_WDF_DRIVER pWdfDriver);

/* Resets the device and destroys virtqueue data structures. To be called
 * from driver's EvtDeviceD0Exit callback.
 */
NTSTATUS PhyzIOWdfDestroyQueues(PPHYZIO_WDF_DRIVER pWdfDriver);

/* Destroys the PHYZIO_WDF_DRIVER context and deallocates all resources.
 * To be called from driver's EvtDeviceReleaseHardware callback.
 */
NTSTATUS PhyzIOWdfShutdown(PPHYZIO_WDF_DRIVER pWdfDriver);

/* Returns the contents of the ISR status field and acknowledges the
 * interrupt. Called from driver's ISR if traditional IRQ interrupts
 * are used.
 */
UCHAR PhyzIOWdfGetISRStatus(PPHYZIO_WDF_DRIVER pWdfDriver);

/* Device config space access routines. Follow specific device documentation
 * for rules on when and how these can be called. If interrupt on device
 * config change is desired, a valid WDFINTERRUPT should be passed to
 * PhyzIOWdfInitialize.
 */
void PhyzIOWdfDeviceGet(PPHYZIO_WDF_DRIVER pWdfDriver,
                        ULONG offset,
                        PVOID buf,
                        ULONG len);
void PhyzIOWdfDeviceSet(PPHYZIO_WDF_DRIVER pWdfDriver,
                        ULONG offset,
                        CONST PVOID buf,
                        ULONG len);

/* DMA memory allocations */

/* PASSIVE, optional groupTag for PhyzIOWdfDeviceFreeDmaMemoryByTag
 * returns NULL on DISPATCH
 */
void *PhyzIOWdfDeviceAllocDmaMemory(PhyzIODevice *vdev, size_t size, ULONG groupTag);
PHYSICAL_ADDRESS PhyzIOWdfDeviceGetPhysicalAddress(PhyzIODevice *vdev, void *va);
/* PASSIVE, va must be exact address of the block
 *  does not free the block on DISPATCH
 */
void PhyzIOWdfDeviceFreeDmaMemory(PhyzIODevice *vdev, void *va);
/* PASSIVE, suitable for D0 exit */
void PhyzIOWdfDeviceFreeDmaMemoryByTag(PhyzIODevice *vdev, ULONG groupTag);

typedef struct phyzio_dma_transaction_params
{
    /* IN */
    PVOID param1; /* scratch field to be used by the callback */
    PVOID param2;
    WDFREQUEST req; /* NULL or Write request */
    PVOID buffer;   /* NULL or buffer with data to be sent */
    ULONG size;     /* amount of data to be copied from buffer */
    ULONG allocationTag; /* used for reallocation */
    /* callback */
    WDFDMATRANSACTION transaction;
    PSCATTER_GATHER_LIST sgList;
} PHYZIO_DMA_TRANSACTION_PARAMS, *PPHYZIO_DMA_TRANSACTION_PARAMS;

typedef BOOLEAN (*PhyzIOWdfDmaTransactionCallback)(PPHYZIO_DMA_TRANSACTION_PARAMS);

/* if PhyzIOWdfDeviceDmaTxAsync returns FALSE, the callback was not and will not be called
 * The callback will be called synchronously or asynchronously, so do not call this
 *   API under spinlock. There are 2 options:
 * 1. req != NULL (size is ignored) -> the transaction will use IN buffer of Write Irp
 *    The request should be non-cancellable all the way
 * 2. req = NULL, buffer and size provided, the buffer will be reallocated and the callback
 *    will receive SG of copied data (originally provided buffer is not used for DMA)
 * If the callback wants to return FALSE (too many elements in SG or whatever), call
 *    PhyzIOWdfDeviceDmaTxComplete, then complete the request (if req != NULL), then return FALSE
 * If the callback returns TRUE, call PhyzIOWdfDeviceDmaTxComplete later from InterruptDpc
 *      then complete the request.
 * If this API is used with req != NULL and with cancellable request, the problem will happen
 *      with cancelled request which SG is enqueued in TX virtq
 * IRQL: <= DISPATCH, callback is called on DISPATCH
 */
BOOLEAN PhyzIOWdfDeviceDmaTxAsync(PhyzIODevice *vdev,
                                 PPHYZIO_DMA_TRANSACTION_PARAMS params,
                                 PhyzIOWdfDmaTransactionCallback);
/* <= DISPATCH transaction = PHYZIO_DMA_TRANSACTION_PARAMS.transaction */
void PhyzIOWdfDeviceDmaTxComplete(PhyzIODevice *vdev, WDFDMATRANSACTION transaction);

typedef struct phyzio_dma_memory_sliced
{
    PVOID                (*get_slice)(struct phyzio_dma_memory_sliced *, PHYSICAL_ADDRESS *ppa);
    void                 (*return_slice)(struct phyzio_dma_memory_sliced *, PVOID va);
    void                 (*destroy)(struct phyzio_dma_memory_sliced *);
    /* private area */
    PHYSICAL_ADDRESS     pa;
    PPHYZIO_WDF_DRIVER   drv;
    PVOID                va;
    RTL_BITMAP           bitmap;
    ULONG                slice;
    ULONG                bitmap_buffer[1];
}PHYZIO_DMA_MEMORY_SLICED, *PPHYZIO_DMA_MEMORY_SLICED;

PPHYZIO_DMA_MEMORY_SLICED PhyzIOWdfDeviceAllocDmaMemorySliced(PhyzIODevice *vdev, size_t blockSize, ULONG sliceSize);
