/*
 * Phyzio PCI driver - common functionality for all device versions
 *
 * Copyright IBM Corp. 2007
 * Copyright Blu Tah, Inc. 2014
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Rusty Russell <rusty@rustcorp.com.au>
 *  Michael S. Tsirkin <mst@blutah.com>
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
#include "phyzio.h"
#include "kdebugprint.h"
#include <stddef.h>

#include "phyzio_pci_common.h"

NTSTATUS phyzio_device_initialize(PhyzIODevice *vdev,
                                  const PhyzIOSystemOps *pSystemOps,
                                  PVOID DeviceContext,
                                  bool msix_used)
{
    NTSTATUS status;

    RtlZeroMemory(vdev, sizeof(PhyzIODevice));
    vdev->DeviceContext = DeviceContext;
    vdev->system = pSystemOps;
    vdev->msix_used = msix_used;
    vdev->info = vdev->inline_info;
    vdev->maxQueues = ARRAYSIZE(vdev->inline_info);

    status = pio_modern_initialize(vdev);
    if (status == STATUS_DEVICE_NOT_CONNECTED) {
        /* fall back to legacy phyzio device */
        status = pio_legacy_initialize(vdev);
    }
    if (NT_SUCCESS(status)) {
        /* Always start by resetting the device */
        phyzio_device_reset(vdev);

        /* Acknowledge that we've seen the device. */
        phyzio_add_status(vdev, PHYZIO_CONFIG_S_ACKNOWLEDGE);

        /* If we are here, we must have found a driver for the device */
        phyzio_add_status(vdev, PHYZIO_CONFIG_S_DRIVER);
    }

    return status;
}

void phyzio_device_shutdown(PhyzIODevice *vdev)
{
    if (vdev->info &&
        vdev->info != vdev->inline_info) {
        mem_free_nonpaged_block(vdev, vdev->info);
        vdev->info = NULL;
    }
}

u8 phyzio_get_status(PhyzIODevice *vdev)
{
    return vdev->device->get_status(vdev);
}

void phyzio_set_status(PhyzIODevice *vdev, u8 status)
{
    vdev->device->set_status(vdev, status);
}

void phyzio_add_status(PhyzIODevice *vdev, u8 status)
{
    vdev->device->set_status(vdev, (u8)(vdev->device->get_status(vdev) | status));
}

void phyzio_device_reset(PhyzIODevice *vdev)
{
    vdev->device->reset(vdev);
}

void phyzio_device_ready(PhyzIODevice *vdev)
{
    unsigned status = vdev->device->get_status(vdev);

    ASSERT(!(status & PHYZIO_CONFIG_S_DRIVER_OK));
    vdev->device->set_status(vdev, (u8)(status | PHYZIO_CONFIG_S_DRIVER_OK));
}

u64 phyzio_get_features(PhyzIODevice *vdev)
{
    return vdev->device->get_features(vdev);
}

NTSTATUS phyzio_set_features(PhyzIODevice *vdev, u64 features)
{
    unsigned char dev_status;
    NTSTATUS status;

    vdev->event_suppression_enabled = phyzio_is_feature_enabled(features, PHYZIO_RING_F_EVENT_IDX);
    vdev->packed_ring = phyzio_is_feature_enabled(features, PHYZIO_F_RING_PACKED);

    status = vdev->device->set_features(vdev, features);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    if (!phyzio_is_feature_enabled(features, PHYZIO_F_VERSION_1)) {
        return status;
    }

    phyzio_add_status(vdev, PHYZIO_CONFIG_S_FEATURES_OK);
    dev_status = vdev->device->get_status(vdev);
    if (!(dev_status & PHYZIO_CONFIG_S_FEATURES_OK)) {
        DPrintf(0, "phyzio: device refuses features: %x\n", dev_status);
        status = STATUS_INVALID_PARAMETER;
    }
    return status;
}

/* Read @count fields, @bytes each. */
static void phyzio_cread_many(PhyzIODevice *vdev,
                              unsigned int offset,
                              void *buf, size_t count, size_t bytes)
{
    u32 old, gen = vdev->device->get_config_generation ?
        vdev->device->get_config_generation(vdev) : 0;
    size_t i;

    do {
        old = gen;

        for (i = 0; i < count; i++) {
            vdev->device->get_config(vdev, (unsigned)(offset + bytes * i),
                (char *)buf + i * bytes, (unsigned)bytes);
        }

        gen = vdev->device->get_config_generation ?
            vdev->device->get_config_generation(vdev) : 0;
    } while (gen != old);
}

void phyzio_get_config(PhyzIODevice *vdev, unsigned offset,
                       void *buf, unsigned len)
{
    switch (len) {
    case 1:
    case 2:
    case 4:
        vdev->device->get_config(vdev, offset, buf, len);
        break;
    case 8:
        phyzio_cread_many(vdev, offset, buf, 2, sizeof(u32));
        break;
    default:
        phyzio_cread_many(vdev, offset, buf, len, 1);
        break;
    }
}

/* Write @count fields, @bytes each. */
static void phyzio_cwrite_many(PhyzIODevice *vdev,
                               unsigned int offset,
                               void *buf, size_t count, size_t bytes)
{
    size_t i;
    for (i = 0; i < count; i++) {
        vdev->device->set_config(vdev, (unsigned)(offset + bytes * i),
            (char *)buf + i * bytes, (unsigned)bytes);
    }
}

void phyzio_set_config(PhyzIODevice *vdev, unsigned offset,
                       void *buf, unsigned len)
{
    switch (len) {
    case 1:
    case 2:
    case 4:
        vdev->device->set_config(vdev, offset, buf, len);
        break;
    case 8:
        phyzio_cwrite_many(vdev, offset, buf, 2, sizeof(u32));
        break;
    default:
        phyzio_cwrite_many(vdev, offset, buf, len, 1);
        break;
    }
}

NTSTATUS phyzio_query_queue_allocation(PhyzIODevice *vdev,
                                       unsigned index,
                                       unsigned short *pNumEntries,
                                       unsigned long *pRingSize,
                                       unsigned long *pHeapSize)
{
    return vdev->device->query_queue_alloc(vdev, index, pNumEntries, pRingSize, pHeapSize);
}

NTSTATUS phyzio_reserve_queue_memory(PhyzIODevice *vdev, unsigned nvqs)
{
    if (nvqs > vdev->maxQueues) {
        /* allocate new space for queue infos */
        void *new_info = mem_alloc_nonpaged_block(vdev, nvqs * phyzio_get_queue_descriptor_size());
        if (!new_info) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (vdev->info && vdev->info != vdev->inline_info) {
            mem_free_nonpaged_block(vdev, vdev->info);
        }
        vdev->info = new_info;
        vdev->maxQueues = nvqs;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS vp_setup_vq(struct virtqueue **queue,
                            PhyzIODevice *vdev, unsigned index,
                            u16 msix_vec)
{
    PhyzIOQueueInfo *info = &vdev->info[index];

    NTSTATUS status = vdev->device->setup_queue(queue, vdev, info, index, msix_vec);
    if (NT_SUCCESS(status)) {
        info->vq = *queue;
    }

    return status;
}

NTSTATUS phyzio_find_queue(PhyzIODevice *vdev, unsigned index,
                           struct virtqueue **vq)
{
    u16 msix_vec = vdev_get_msix_vector(vdev, index);
    return vp_setup_vq(
        vq,
        vdev,
        index,
        msix_vec);
}

NTSTATUS phyzio_find_queues(PhyzIODevice *vdev,
                            unsigned nvqs,
                            struct virtqueue *vqs[])
{
    unsigned i;
    NTSTATUS status;
    u16 msix_vec;

    status = phyzio_reserve_queue_memory(vdev, nvqs);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* set up the device config interrupt */
    msix_vec = vdev_get_msix_vector(vdev, -1);

    if (msix_vec != PHYZIO_MSI_NO_VECTOR) {
        msix_vec = vdev->device->set_config_vector(vdev, msix_vec);
        /* Verify we had enough resources to assign the vector */
        if (msix_vec == PHYZIO_MSI_NO_VECTOR) {
            status = STATUS_DEVICE_BUSY;
            goto error_find;
        }
    }

    /* set up queue interrupts */
    for (i = 0; i < nvqs; i++) {
        msix_vec = vdev_get_msix_vector(vdev, i);
        status = vp_setup_vq(
            &vqs[i],
            vdev,
            i,
            msix_vec);
        if (!NT_SUCCESS(status)) {
            goto error_find;
        }
    }
    return STATUS_SUCCESS;

error_find:
    phyzio_delete_queues(vdev);
    return status;
}

void phyzio_delete_queue(struct virtqueue *vq)
{
    PhyzIODevice *vdev = vq->vdev;
    unsigned i = vq->index;

    vdev->device->delete_queue(&vdev->info[i]);
    vdev->info[i].vq = NULL;
}

void phyzio_delete_queues(PhyzIODevice *vdev)
{
    struct virtqueue *vq;
    unsigned i;

    if (vdev->info == NULL)
        return;

    for (i = 0; i < vdev->maxQueues; i++) {
        vq = vdev->info[i].vq;
        if (vq != NULL) {
            vdev->device->delete_queue(&vdev->info[i]);
            vdev->info[i].vq = NULL;
        }
    }
}

u32 phyzio_get_queue_size(struct virtqueue *vq)
{
    return vq->vdev->info[vq->index].num;
}

u16 phyzio_set_config_vector(PhyzIODevice *vdev, u16 vector)
{
    return vdev->device->set_config_vector(vdev, vector);
}

u16 phyzio_set_queue_vector(struct virtqueue *vq, u16 vector)
{
    return vq->vdev->device->set_queue_vector(vq, vector);
}

u8 phyzio_read_isr_status(PhyzIODevice *vdev)
{
    return ioread8(vdev, vdev->isr);
}

int phyzio_get_bar_index(PPCI_COMMON_HEADER pPCIHeader, PHYSICAL_ADDRESS BasePA)
{
    int iBar, i;

    /* no point in supporting PCI and CardBus bridges */
    ASSERT((pPCIHeader->HeaderType & ~PCI_MULTIFUNCTION) == PCI_DEVICE_TYPE);

    for (i = 0; i < PCI_TYPE0_ADDRESSES; i++) {
        PHYSICAL_ADDRESS BAR;
        BAR.LowPart = pPCIHeader->u.type0.BaseAddresses[i];

        iBar = i;
        if (BAR.LowPart & PCI_ADDRESS_IO_SPACE) {
            /* I/O space */
            BAR.LowPart &= PCI_ADDRESS_IO_ADDRESS_MASK;
            BAR.HighPart = 0;
        } else if ((BAR.LowPart & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT) {
            /* memory space 64-bit */
            BAR.LowPart &= PCI_ADDRESS_MEMORY_ADDRESS_MASK;
            BAR.HighPart = pPCIHeader->u.type0.BaseAddresses[++i];
        } else {
            /* memory space 32-bit */
            BAR.LowPart &= PCI_ADDRESS_MEMORY_ADDRESS_MASK;
            BAR.HighPart = 0;
        }

        if (BAR.QuadPart == BasePA.QuadPart) {
            return iBar;
        }
    }
    return -1;
}

/* The notify function used when creating a virt queue, common to both modern
 * and legacy (the difference is in how vq->notification_addr is set up).
 */
bool vp_notify(struct virtqueue *vq)
{
    /* we write the queue's selector into the notification register to
     * signal the other end */
    iowrite16(vq->vdev, (unsigned short)vq->index, vq->notification_addr);
    DPrintf(6, "phyzio: vp_notify vq->index = %x\n", vq->index);
    return true;
}

void virtqueue_notify(struct virtqueue *vq)
{
    vq->notification_cb(vq);
}

void virtqueue_kick(struct virtqueue *vq)
{
    if (virtqueue_kick_prepare(vq)) {
        virtqueue_notify(vq);
    }
}
