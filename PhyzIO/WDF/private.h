/*
 * Private PhyzioLib-WDF prototypes
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

#include <Ntddk.h>
#include <wdf.h>
#include "kdebugprint.h"

typedef struct phyzio_wdf_bar {
    SINGLE_LIST_ENTRY ListEntry;

    int               iBar;
    PHYSICAL_ADDRESS  BasePA;
    ULONG             uLength;
    PVOID             pBase;
    bool              bPortSpace;
} PHYZIO_WDF_BAR, *PPHYZIO_WDF_BAR;

typedef struct phyzio_wdf_interrupt_context {
    /* This is a workaround for a WDF bug where on resource rebalance
     * it does not preserve the MessageNumber field of its internal
     * data structures describing interrupts. As a result, we fail to
     * report the right MSI message number to the phyzio device when
     * re-initializing it and it may stop working.
     */
    USHORT            uMessageNumber;
    bool              bMessageNumberSet;
} PHYZIO_WDF_INTERRUPT_CONTEXT, *PPHYZIO_WDF_INTERRUPT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PHYZIO_WDF_INTERRUPT_CONTEXT, GetInterruptContext)

NTSTATUS PCIAllocBars(WDFCMRESLIST ResourcesTranslated,
                      PPHYZIO_WDF_DRIVER pWdfDriver);

void PCIFreeBars(PPHYZIO_WDF_DRIVER pWdfDriver);

int PCIReadConfig(PPHYZIO_WDF_DRIVER pWdfDriver,
                  int where,
                  void *buffer,
                  size_t length);

NTSTATUS PCIRegisterInterrupt(WDFINTERRUPT Interrupt);

u16 PCIGetMSIInterruptVector(WDFINTERRUPT Interrupt);

typedef struct phyzio_wdf_memory_block_context {
    PVOID               pVirtualAddress;
    PHYSICAL_ADDRESS    PhysicalAddress;
    WDFCOMMONBUFFER     WdfBuffer;
    size_t              Length;
    ULONG               groupTag;
    BOOLEAN             bToBeDeleted;
} PHYZIO_WDF_MEMORY_BLOCK_CONTEXT, *PPHYZIO_WDF_MEMORY_BLOCK_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PHYZIO_WDF_MEMORY_BLOCK_CONTEXT, GetMemoryBlockContext)

typedef struct phyzio_wdf_dma_transaction_context {
    PHYZIO_DMA_TRANSACTION_PARAMS   parameters;
    PhyzIOWdfDmaTransactionCallback callback;
    PMDL                            mdl;
    PVOID                           buffer;
    LONG                            refCount;
} PHYZIO_WDF_DMA_TRANSACTION_CONTEXT, *PPHYZIO_WDF_DMA_TRANSACTION_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PHYZIO_WDF_DMA_TRANSACTION_CONTEXT, GetDmaTransactionContext)
