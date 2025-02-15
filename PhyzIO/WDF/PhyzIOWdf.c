/*
 * Implementation of PhyzioLib-WDF driver API
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
#include "osdep.h"
#include "phyzio_pci.h"
#include "PhyzIOWdf.h"
#include "private.h"
#include <wdmguid.h>

extern PhyzIOSystemOps PhyzIOWdfSystemOps;

NTSTATUS PhyzIOWdfInitialize(PPHYZIO_WDF_DRIVER pWdfDriver,
                             WDFDEVICE Device,
                             WDFCMRESLIST ResourcesTranslated,
                             WDFINTERRUPT ConfigInterrupt,
                             ULONG MemoryTag)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_DMA_ENABLER_CONFIG dmaEnablerConfig;
    WDF_OBJECT_ATTRIBUTES  attributes;

    RtlZeroMemory(pWdfDriver, sizeof(*pWdfDriver));
    pWdfDriver->MemoryTag = MemoryTag;
    pWdfDriver->bLegacyMode = FALSE;

    /* get the PCI bus interface */
    status = WdfFdoQueryForInterface(
        Device,
        &GUID_BUS_INTERFACE_STANDARD,
        (PINTERFACE)&pWdfDriver->PCIBus,
        sizeof(pWdfDriver->PCIBus),
        1 /* version */,
        NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* register config interrupt */
    status = PCIRegisterInterrupt(ConfigInterrupt);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* set up resources */
    status = PCIAllocBars(ResourcesTranslated, pWdfDriver);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* set max transfer size to 256M, should be enough for any purpose */
    /* number of SG fragments is unlimited */
    WDF_DMA_ENABLER_CONFIG_INIT(&dmaEnablerConfig, WdfDmaProfileScatterGather64Duplex, 0xFFFFFFF);
    status = WdfDmaEnablerCreate(Device, &dmaEnablerConfig, WDF_NO_OBJECT_ATTRIBUTES, &pWdfDriver->DmaEnabler);
    if (NT_SUCCESS(status)) {
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        DPrintf(0, "%s DMA enabler ready (alignment %d), pWdfDriver %p\n", __FUNCTION__,
            WdfDeviceGetAlignmentRequirement(Device) + 1, pWdfDriver);
        attributes.ParentObject = Device;
        status = WdfCollectionCreate(&attributes, &pWdfDriver->MemoryBlockCollection);
    }

    if (NT_SUCCESS(status)) {
        status = WdfSpinLockCreate(&attributes, &pWdfDriver->DmaSpinlock);
    }

    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* initialize the underlying PhyzIODevice */
    status = phyzio_device_initialize(
        &pWdfDriver->PIODevice,
        &PhyzIOWdfSystemOps,
        pWdfDriver,
        pWdfDriver->nMSIInterrupts > 0);
    if (!NT_SUCCESS(status)) {
        PCIFreeBars(pWdfDriver);
    }

    pWdfDriver->ConfigInterrupt = ConfigInterrupt;

    return status;
}

ULONGLONG PhyzIOWdfGetDeviceFeatures(PPHYZIO_WDF_DRIVER pWdfDriver)
{
    return phyzio_get_features(&pWdfDriver->PIODevice);
}

NTSTATUS PhyzIOWdfSetDriverFeatures(PPHYZIO_WDF_DRIVER pWdfDriver,
                                    ULONGLONG uPrivateFeaturesOn,
                                    ULONGLONG uFeaturesOff)
{
    ULONGLONG uFeatures = 0, uDeviceFeatures = PhyzIOWdfGetDeviceFeatures(pWdfDriver);
    char drvTag[sizeof(pWdfDriver->MemoryTag) + 1];

    drvTag[sizeof(pWdfDriver->MemoryTag)] = 0;
    RtlCopyMemory(drvTag, &pWdfDriver->MemoryTag, sizeof(pWdfDriver->MemoryTag));

    if (phyzio_is_feature_enabled(uDeviceFeatures, PHYZIO_F_VERSION_1)) {
        phyzio_feature_enable(uFeatures, PHYZIO_F_VERSION_1);
    }
    if (phyzio_is_feature_enabled(uDeviceFeatures, PHYZIO_F_ANY_LAYOUT)) {
        phyzio_feature_enable(uFeatures, PHYZIO_F_ANY_LAYOUT);
    }
    if (phyzio_is_feature_enabled(uDeviceFeatures, PHYZIO_F_ACCESS_PLATFORM)) {
        phyzio_feature_enable(uFeatures, PHYZIO_F_ACCESS_PLATFORM);
    }

    if ((uDeviceFeatures & uPrivateFeaturesOn) != uPrivateFeaturesOn) {
        DPrintf(0, "%s(%s) FAILED features %I64X != %I64X\n", __FUNCTION__,
            drvTag, uPrivateFeaturesOn, (uDeviceFeatures & uPrivateFeaturesOn));
        return STATUS_INVALID_PARAMETER;
    }

    uFeatures |= uPrivateFeaturesOn;
    uFeatures &= ~uFeaturesOff;

    /* make sure that we always follow the status bit-setting protocol */
    u8 status = phyzio_get_status(&pWdfDriver->PIODevice);
    if (!(status & PHYZIO_CONFIG_S_ACKNOWLEDGE)) {
        phyzio_add_status(&pWdfDriver->PIODevice, PHYZIO_CONFIG_S_ACKNOWLEDGE);
    }
    if (!(status & PHYZIO_CONFIG_S_DRIVER)) {
        phyzio_add_status(&pWdfDriver->PIODevice, PHYZIO_CONFIG_S_DRIVER);
    }

    /* cache driver features in case we need to replay this in PhyzIOWdfInitQueues */
    pWdfDriver->uFeatures = uFeatures;
    return phyzio_set_features(&pWdfDriver->PIODevice, uFeatures);
}

static NTSTATUS PhyzIOWdfFinalizeFeatures(PPHYZIO_WDF_DRIVER pWdfDriver)
{
    NTSTATUS status = STATUS_SUCCESS;

    if (!pWdfDriver->uFeatures) {
        /* specific driver does not have any special features requirements */
        status = PhyzIOWdfSetDriverFeatures(pWdfDriver, 0, 0);
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }

    u8 dev_status = phyzio_get_status(&pWdfDriver->PIODevice);
    if (!(dev_status & PHYZIO_CONFIG_S_ACKNOWLEDGE)) {
        phyzio_add_status(&pWdfDriver->PIODevice, PHYZIO_CONFIG_S_ACKNOWLEDGE);
    }
    if (!(dev_status & PHYZIO_CONFIG_S_DRIVER)) {
        phyzio_add_status(&pWdfDriver->PIODevice, PHYZIO_CONFIG_S_DRIVER);
    }
    if (!(dev_status & PHYZIO_CONFIG_S_FEATURES_OK)) {
        status = phyzio_set_features(&pWdfDriver->PIODevice, pWdfDriver->uFeatures);
    }

    return status;
}

NTSTATUS PhyzIOWdfInitQueues(PPHYZIO_WDF_DRIVER pWdfDriver,
                             ULONG nQueues,
                             struct virtqueue **pQueues,
                             PPHYZIO_WDF_QUEUE_PARAM pQueueParams)
{
    NTSTATUS status;
    ULONG i;

    /* make sure that we always follow the status bit-setting protocol */
    status = PhyzIOWdfFinalizeFeatures(pWdfDriver);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* register queue interrupts */
    for (i = 0; i < nQueues; i++) {
        status = PCIRegisterInterrupt(pQueueParams[i].Interrupt);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    /* find and initialize queues */
    pWdfDriver->pQueueParams = pQueueParams;
    status = phyzio_find_queues(
        &pWdfDriver->PIODevice,
        nQueues,
        pQueues);
    pWdfDriver->pQueueParams = NULL;

    return status;
}

NTSTATUS PhyzIOWdfInitQueuesCB(PPHYZIO_WDF_DRIVER pWdfDriver,
                               ULONG nQueues,
                               PhyzIOWdfGetQueueParamCallback pQueueParamFunc,
                               PhyzIOWdfSetQueueCallback pSetQueueFunc)
{
    PHYZIO_WDF_QUEUE_PARAM QueueParam;
    struct virtqueue *vq;
    NTSTATUS status;
    u16 msix_vec;
    ULONG i;

    /* make sure that we always follow the status bit-setting protocol */
    status = PhyzIOWdfFinalizeFeatures(pWdfDriver);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* let PhyzioLib know how many queues we'll need */
    status = phyzio_reserve_queue_memory(&pWdfDriver->PIODevice, nQueues);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* set up the device config vector */
    msix_vec = PCIGetMSIInterruptVector(pWdfDriver->ConfigInterrupt);
    if (msix_vec != PHYZIO_MSI_NO_VECTOR) {
        if (phyzio_set_config_vector(&pWdfDriver->PIODevice, msix_vec) != msix_vec) {
            return STATUS_DEVICE_BUSY;
        }
    }

    /* find and initialize queues */
    for (i = 0; i < nQueues; i++) {
        status = phyzio_find_queue(&pWdfDriver->PIODevice, i, &vq);
        if (!NT_SUCCESS(status)) {
            break;
        }

        /* set the desired queue vector */
        QueueParam.Interrupt = NULL;

        pQueueParamFunc(pWdfDriver, i, &QueueParam);

        status = PCIRegisterInterrupt(QueueParam.Interrupt);
        if (!NT_SUCCESS(status)) {
            break;
        }

        msix_vec = PCIGetMSIInterruptVector(QueueParam.Interrupt);
        if (msix_vec != PHYZIO_MSI_NO_VECTOR) {
            if (phyzio_set_queue_vector(vq, msix_vec) != msix_vec) {
                status = STATUS_DEVICE_BUSY;
                break;
            }
        }

        /* pass the virtqueue pointer to the caller */
        pSetQueueFunc(pWdfDriver, i, vq);
    }

    if (!NT_SUCCESS(status)) {
        phyzio_delete_queues(&pWdfDriver->PIODevice);
    }
    return status;
}

void PhyzIOWdfSetDriverOK(PPHYZIO_WDF_DRIVER pWdfDriver)
{
    phyzio_device_ready(&pWdfDriver->PIODevice);
}

void PhyzIOWdfSetDriverFailed(PPHYZIO_WDF_DRIVER pWdfDriver)
{
    phyzio_add_status(&pWdfDriver->PIODevice, PHYZIO_CONFIG_S_FAILED);
}

NTSTATUS PhyzIOWdfShutdown(PPHYZIO_WDF_DRIVER pWdfDriver)
{
    phyzio_device_shutdown(&pWdfDriver->PIODevice);

    PCIFreeBars(pWdfDriver);

    return STATUS_SUCCESS;
}

NTSTATUS PhyzIOWdfDestroyQueues(PPHYZIO_WDF_DRIVER pWdfDriver)
{
    phyzio_device_reset(&pWdfDriver->PIODevice);
    phyzio_delete_queues(&pWdfDriver->PIODevice);

    return STATUS_SUCCESS;
}

void PhyzIOWdfDeviceGet(PPHYZIO_WDF_DRIVER pWdfDriver,
                        ULONG offset,
                        PVOID buf,
                        ULONG len)
{
    phyzio_get_config(
        &pWdfDriver->PIODevice,
        offset,
        buf,
        len);
}

void PhyzIOWdfDeviceSet(PPHYZIO_WDF_DRIVER pWdfDriver,
                        ULONG offset,
                        CONST PVOID buf,
                        ULONG len)
{
    phyzio_set_config(
        &pWdfDriver->PIODevice,
        offset,
        buf,
        len);
}

UCHAR PhyzIOWdfGetISRStatus(PPHYZIO_WDF_DRIVER pWdfDriver)
{
    return phyzio_read_isr_status(&pWdfDriver->PIODevice);
}
