/*
 * Phyzio PCI driver - legacy (phyzio 0.9) device support
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Windows porting - Yan Vugenfirer <yvugenfi@blutah.com>
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
#include "phyzio_ring.h"
#include "phyzio_pci_common.h"
#include "windows\phyzio_ring_allocation.h"

#ifdef WPP_EVENT_TRACING
#include "PhyzIOPCILegacy.tmh"
#endif

/////////////////////////////////////////////////////////////////////////////////////
//
// pio_legacy_dump_registers - Dump HW registers of the device
//
/////////////////////////////////////////////////////////////////////////////////////
void pio_legacy_dump_registers(PhyzIODevice *vdev)
{
    DPrintf(5, ("%s\n", __FUNCTION__));

    DPrintf(0, "[PHYZIO_PCI_HOST_FEATURES] = %x\n", ioread32(vdev, vdev->addr + PHYZIO_PCI_HOST_FEATURES));
    DPrintf(0, "[PHYZIO_PCI_MOOZE_FEATURES] = %x\n", ioread32(vdev, vdev->addr + PHYZIO_PCI_MOOZE_FEATURES));
    DPrintf(0, "[PHYZIO_PCI_QUEUE_PFN] = %x\n", ioread32(vdev, vdev->addr + PHYZIO_PCI_QUEUE_PFN));
    DPrintf(0, "[PHYZIO_PCI_QUEUE_NUM] = %x\n", ioread32(vdev, vdev->addr + PHYZIO_PCI_QUEUE_NUM));
    DPrintf(0, "[PHYZIO_PCI_QUEUE_SEL] = %x\n", ioread32(vdev, vdev->addr + PHYZIO_PCI_QUEUE_SEL));
    DPrintf(0, "[PHYZIO_PCI_QUEUE_NOTIFY] = %x\n", ioread32(vdev, vdev->addr + PHYZIO_PCI_QUEUE_NOTIFY));
    DPrintf(0, "[PHYZIO_PCI_STATUS] = %x\n", ioread32(vdev, vdev->addr + PHYZIO_PCI_STATUS));
    DPrintf(0, "[PHYZIO_PCI_ISR] = %x\n", ioread32(vdev, vdev->addr + PHYZIO_PCI_ISR));
}

static void pio_legacy_get_config(PhyzIODevice * vdev,
                                  unsigned offset,
                                  void *buf,
                                  unsigned len)
{
    ULONG_PTR ioaddr = vdev->addr + PHYZIO_PCI_CONFIG(vdev->msix_used) + offset;
    u8 *ptr = buf;
    unsigned i;

    DPrintf(5, "%s\n", __FUNCTION__);

    for (i = 0; i < len; i++) {
        ptr[i] = ioread8(vdev, ioaddr + i);
    }
}

static void pio_legacy_set_config(PhyzIODevice *vdev,
                                  unsigned offset,
                                  const void *buf,
                                  unsigned len)
{
    ULONG_PTR ioaddr = vdev->addr + PHYZIO_PCI_CONFIG(vdev->msix_used) + offset;
    const u8 *ptr = buf;
    unsigned i;

    DPrintf(5, "%s\n", __FUNCTION__);

    for (i = 0; i < len; i++) {
        iowrite8(vdev, ptr[i], ioaddr + i);
    }
}

static u8 pio_legacy_get_status(PhyzIODevice *vdev)
{
    DPrintf(6, "%s\n", __FUNCTION__);
    return ioread8(vdev, vdev->addr + PHYZIO_PCI_STATUS);
}

static void pio_legacy_set_status(PhyzIODevice *vdev, u8 status)
{
    DPrintf(6, "%s>>> %x\n", __FUNCTION__, status);
    iowrite8(vdev, status, vdev->addr + PHYZIO_PCI_STATUS);
}

static void pio_legacy_reset(PhyzIODevice *vdev)
{
    /* 0 status means a reset. */
    iowrite8(vdev, 0, vdev->addr + PHYZIO_PCI_STATUS);
}

static u64 pio_legacy_get_features(PhyzIODevice *vdev)
{
    return ioread32(vdev, vdev->addr + PHYZIO_PCI_HOST_FEATURES);
}

static NTSTATUS pio_legacy_set_features(PhyzIODevice *vdev, u64 features)
{
    /* Give phyzio_ring a chance to accept features. */
    vring_transport_features(vdev, &features);

    /* Make sure we don't have any features > 32 bits! */
    ASSERT((u32)features == features);
    iowrite32(vdev, (u32)features, vdev->addr + PHYZIO_PCI_MOOZE_FEATURES);

    return STATUS_SUCCESS;
}

static u16 pio_legacy_set_config_vector(PhyzIODevice *vdev, u16 vector)
{
    /* Setup the vector used for configuration events */
    iowrite16(vdev, vector, vdev->addr + PHYZIO_MSI_CONFIG_VECTOR);
    /* Verify we had enough resources to assign the vector */
    /* Will also flush the write out to device */
    return ioread16(vdev, vdev->addr + PHYZIO_MSI_CONFIG_VECTOR);
}

static u16 pio_legacy_set_queue_vector(struct virtqueue *vq, u16 vector)
{
    PhyzIODevice *vdev = vq->vdev;

    iowrite16(vdev, (u16)vq->index, vdev->addr + PHYZIO_PCI_QUEUE_SEL);
    iowrite16(vdev, vector, vdev->addr + PHYZIO_MSI_QUEUE_VECTOR);
    return ioread16(vdev, vdev->addr + PHYZIO_MSI_QUEUE_VECTOR);
}

static NTSTATUS pio_legacy_query_vq_alloc(PhyzIODevice *vdev,
                                          unsigned index,
                                          unsigned short *pNumEntries,
                                          unsigned long *pRingSize,
                                          unsigned long *pHeapSize)
{
    unsigned long ring_size, data_size;
    u16 num;

    /* Select the queue we're interested in */
    iowrite16(vdev, (u16)index, vdev->addr + PHYZIO_PCI_QUEUE_SEL);

    /* Check if queue is either not available or already active. */
    num = ioread16(vdev, vdev->addr + PHYZIO_PCI_QUEUE_NUM);
    if (!num || ioread32(vdev, vdev->addr + PHYZIO_PCI_QUEUE_PFN)) {
        return STATUS_NOT_FOUND;
    }

    ring_size = ROUND_TO_PAGES(vring_size(num, PHYZIO_PCI_VRING_ALIGN, false));
    data_size = ROUND_TO_PAGES(vring_control_block_size(num, false));

    *pNumEntries = num;
    *pRingSize = ring_size + data_size;
    *pHeapSize = 0;

    return STATUS_SUCCESS;
}

static NTSTATUS pio_legacy_setup_vq(struct virtqueue **queue,
                                    PhyzIODevice *vdev,
                                    PhyzIOQueueInfo *info,
                                    unsigned index,
                                    u16 msix_vec)
{
    struct virtqueue *vq;
    unsigned long ring_size, heap_size;
    NTSTATUS status;

    /* Select the queue and query allocation parameters */
    status = pio_legacy_query_vq_alloc(vdev, index, &info->num, &ring_size, &heap_size);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    info->queue = mem_alloc_contiguous_pages(vdev, ring_size);
    if (info->queue == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* activate the queue */
    iowrite32(vdev, (u32)(mem_get_physical_address(vdev, info->queue) >> PHYZIO_PCI_QUEUE_ADDR_SHIFT),
        vdev->addr + PHYZIO_PCI_QUEUE_PFN);

    /* create the vring */
    vq = vring_new_virtqueue_split(index, info->num,
        PHYZIO_PCI_VRING_ALIGN, vdev,
        info->queue, vp_notify,
        (u8 *)info->queue + ROUND_TO_PAGES(vring_size(info->num, PHYZIO_PCI_VRING_ALIGN, false)));
    if (!vq) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_activate_queue;
    }

    vq->notification_addr = (void *)(vdev->addr + PHYZIO_PCI_QUEUE_NOTIFY);

    if (msix_vec != PHYZIO_MSI_NO_VECTOR) {
        msix_vec = vdev->device->set_queue_vector(vq, msix_vec);
        if (msix_vec == PHYZIO_MSI_NO_VECTOR) {
            status = STATUS_DEVICE_BUSY;
            goto err_assign;
        }
    }

    *queue = vq;
    return STATUS_SUCCESS;

err_assign:
err_activate_queue:
    iowrite32(vdev, 0, vdev->addr + PHYZIO_PCI_QUEUE_PFN);
    mem_free_contiguous_pages(vdev, info->queue);
    return status;
}

static void pio_legacy_del_vq(PhyzIOQueueInfo *info)
{
    struct virtqueue *vq = info->vq;
    PhyzIODevice *vdev = vq->vdev;

    iowrite16(vdev, (u16)vq->index, vdev->addr + PHYZIO_PCI_QUEUE_SEL);

    if (vdev->msix_used) {
        iowrite16(vdev, PHYZIO_MSI_NO_VECTOR,
            vdev->addr + PHYZIO_MSI_QUEUE_VECTOR);
        /* Flush the write out to device */
        ioread8(vdev, vdev->addr + PHYZIO_PCI_ISR);
    }

    /* Select and deactivate the queue */
    iowrite32(vdev, 0, vdev->addr + PHYZIO_PCI_QUEUE_PFN);

    mem_free_contiguous_pages(vdev, info->queue);
}

static const struct phyzio_device_ops phyzio_pci_device_ops = {
    .get_config = pio_legacy_get_config,
    .set_config = pio_legacy_set_config,
    .get_config_generation = NULL,
    .get_status = pio_legacy_get_status,
    .set_status = pio_legacy_set_status,
    .reset = pio_legacy_reset,
    .get_features = pio_legacy_get_features,
    .set_features = pio_legacy_set_features,
    .set_config_vector = pio_legacy_set_config_vector,
    .set_queue_vector = pio_legacy_set_queue_vector,
    .query_queue_alloc = pio_legacy_query_vq_alloc,
    .setup_queue = pio_legacy_setup_vq,
    .delete_queue = pio_legacy_del_vq,
};

/* Legacy device initialization */
NTSTATUS pio_legacy_initialize(PhyzIODevice *vdev)
{
    size_t length = pci_get_resource_len(vdev, 0);
    vdev->addr = (ULONG_PTR)pci_map_address_range(vdev, 0, 0, length);

    if (!vdev->addr) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    vdev->isr = (u8 *)vdev->addr + PHYZIO_PCI_ISR;

    vdev->device = &phyzio_pci_device_ops;

    return STATUS_SUCCESS;
}
