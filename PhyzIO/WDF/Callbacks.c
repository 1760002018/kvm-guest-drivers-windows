/*
 * Implementation of phyzio_system_ops PhyzioLib callbacks
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

//#define LEGACY_DMA_SUPPORTED

static void *mem_alloc_contiguous_pages(void *context, size_t size)
{
    void *ret;
    PPHYZIO_WDF_DRIVER pWdfDriver = context;
    if (!pWdfDriver->bLegacyMode) {
        return PhyzIOWdfDeviceAllocDmaMemory(&pWdfDriver->PIODevice, size, 0);
    }
#if defined(LEGACY_DMA_SUPPORTED)
    PHYSICAL_ADDRESS HighestAcceptable;
    HighestAcceptable.QuadPart = 0xFFFFFFFFFF;
#if defined(NTDDI_WIN8) && (NTDDI_VERSION >= NTDDI_WIN8)
    {
        PHYSICAL_ADDRESS Zero = { 0 };
        ret = MmAllocateContiguousNodeMemory(
            size,
            Zero,
            HighestAcceptable,
            Zero,
            PAGE_READWRITE,
            MM_ANY_NODE_OK);
    }
#else
    ret = MmAllocateContiguousMemory(size, HighestAcceptable);
#endif
    if (ret != NULL) {
        RtlZeroMemory(ret, size);
    }
#else
    ret = NULL;
#endif
    return ret;
}

static void mem_free_contiguous_pages(void *context, void *virt)
{
    PPHYZIO_WDF_DRIVER pWdfDriver = context;
    if (!pWdfDriver->bLegacyMode) {
        PhyzIOWdfDeviceFreeDmaMemory(&pWdfDriver->PIODevice, virt);
        return;
    }

#if defined(LEGACY_DMA_SUPPORTED)
    MmFreeContiguousMemory(virt);
#endif
}

static ULONGLONG mem_get_physical_address(void *context, void *virt)
{
    PPHYZIO_WDF_DRIVER pWdfDriver = context;
    PHYSICAL_ADDRESS pa;
    if (!pWdfDriver->bLegacyMode) {
        pa = PhyzIOWdfDeviceGetPhysicalAddress(&pWdfDriver->PIODevice, virt);
    } else {
        pa.QuadPart = 0;
#if defined(LEGACY_DMA_SUPPORTED)
        pa = MmGetPhysicalAddress(virt);
        return pa.QuadPart;
#endif
    }
    if (!pa.QuadPart) {
        DPrintf(0, "%s WARNING: got zero physical address\n", __FUNCTION__);
    }
    return pa.QuadPart;
}

static void *mem_alloc_nonpaged_block(void *context, size_t size)
{
    PPHYZIO_WDF_DRIVER pWdfDriver = (PPHYZIO_WDF_DRIVER)context;

    PVOID addr = ExAllocatePoolWithTag(
        NonPagedPool,
        size,
        pWdfDriver->MemoryTag);
    if (addr) {
        RtlZeroMemory(addr, size);
    }
    return addr;
}

static void mem_free_nonpaged_block(void *context, void *addr)
{
    PPHYZIO_WDF_DRIVER pWdfDriver = (PPHYZIO_WDF_DRIVER)context;

    ExFreePoolWithTag(
        addr,
        pWdfDriver->MemoryTag);
}

static int pci_read_config_byte(void *context, int where, u8 *bVal)
{
    return PCIReadConfig((PPHYZIO_WDF_DRIVER)context, where, bVal, sizeof(*bVal));
}

static int pci_read_config_word(void *context, int where, u16 *wVal)
{
    return PCIReadConfig((PPHYZIO_WDF_DRIVER)context, where, wVal, sizeof(*wVal));
}

static int pci_read_config_dword(void *context, int where, u32 *dwVal)
{
    return PCIReadConfig((PPHYZIO_WDF_DRIVER)context, where, dwVal, sizeof(*dwVal));
}

static PPHYZIO_WDF_BAR find_bar(void *context, int bar)
{
    PPHYZIO_WDF_DRIVER pWdfDriver = (PPHYZIO_WDF_DRIVER)context;
    PSINGLE_LIST_ENTRY iter = &pWdfDriver->PCIBars;
    
    while (iter->Next != NULL) {
        PPHYZIO_WDF_BAR pBar = CONTAINING_RECORD(iter->Next, PHYZIO_WDF_BAR, ListEntry);
        if (pBar->iBar == bar) {
            return pBar;
        }
        iter = iter->Next;
    }
    return NULL;
}

static size_t pci_get_resource_len(void *context, int bar)
{
    PPHYZIO_WDF_BAR pBar = find_bar(context, bar);
    return (pBar ? pBar->uLength : 0);
}

static void *pci_map_address_range(void *context, int bar, size_t offset, size_t maxlen)
{
    PPHYZIO_WDF_BAR pBar = find_bar(context, bar);
    if (pBar) {
        if (pBar->pBase == NULL) {
            ASSERT(!pBar->bPortSpace);
#if defined(NTDDI_WINTHRESHOLD) && (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)
            pBar->pBase = MmMapIoSpaceEx(
                pBar->BasePA,
                pBar->uLength,
                PAGE_READWRITE | PAGE_NOCACHE);
#else
            pBar->pBase = MmMapIoSpace(pBar->BasePA, pBar->uLength, MmNonCached);
#endif
        }
        if (pBar->pBase != NULL && offset < pBar->uLength) {
            return (char *)pBar->pBase + offset;
        }
    }
    return NULL;
}

static u16 vdev_get_msix_vector(void *context, int queue)
{
    PPHYZIO_WDF_DRIVER pWdfDriver = (PPHYZIO_WDF_DRIVER)context;
    u16 vector = PHYZIO_MSI_NO_VECTOR;

    if (queue >= 0) {
        /* queue interrupt */
        if (pWdfDriver->pQueueParams != NULL) {
            vector = PCIGetMSIInterruptVector(pWdfDriver->pQueueParams[queue].Interrupt);
        }
    }
    else {
        /* on-device-config-change interrupt */
        vector = PCIGetMSIInterruptVector(pWdfDriver->ConfigInterrupt);
    }

    return vector;
}

static void vdev_sleep(void *context, unsigned int msecs)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    UNREFERENCED_PARAMETER(context);

    if (KeGetCurrentIrql() <= APC_LEVEL) {
        LARGE_INTEGER delay;
        delay.QuadPart = Int32x32To64(msecs, -10000);
        status = KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }

    if (!NT_SUCCESS(status)) {
        /* fall back to busy wait if we're not allowed to sleep */
        KeStallExecutionProcessor(1000 * msecs);
    }
}

extern u32 ReadPhyzIODeviceRegister(ULONG_PTR ulRegister);
extern void WritePhyzIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue);
extern u8 ReadPhyzIODeviceByte(ULONG_PTR ulRegister);
extern void WritePhyzIODeviceByte(ULONG_PTR ulRegister, u8 bValue);
extern u16 ReadPhyzIODeviceWord(ULONG_PTR ulRegister);
extern void WritePhyzIODeviceWord(ULONG_PTR ulRegister, u16 bValue);

PhyzIOSystemOps PhyzIOWdfSystemOps = {
    .vdev_read_byte = ReadPhyzIODeviceByte,
    .vdev_read_word = ReadPhyzIODeviceWord,
    .vdev_read_dword = ReadPhyzIODeviceRegister,
    .vdev_write_byte = WritePhyzIODeviceByte,
    .vdev_write_word = WritePhyzIODeviceWord,
    .vdev_write_dword = WritePhyzIODeviceRegister,
    .mem_alloc_contiguous_pages = mem_alloc_contiguous_pages,
    .mem_free_contiguous_pages = mem_free_contiguous_pages,
    .mem_get_physical_address = mem_get_physical_address,
    .mem_alloc_nonpaged_block = mem_alloc_nonpaged_block,
    .mem_free_nonpaged_block = mem_free_nonpaged_block,
    .pci_read_config_byte = pci_read_config_byte,
    .pci_read_config_word = pci_read_config_word,
    .pci_read_config_dword = pci_read_config_dword,
    .pci_get_resource_len = pci_get_resource_len,
    .pci_map_address_range = pci_map_address_range,
    .vdev_get_msix_vector = vdev_get_msix_vector,
    .vdev_sleep = vdev_sleep,
};
