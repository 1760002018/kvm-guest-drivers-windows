/*
 * This file contains various phyzio queue related routines.
 *
 * Copyright (c) 2008-2017 Blu Tah, Inc.
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@blutah.com>
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
#include "phyzio_stor_hw_helper.h"
#include"phyzio_stor_utils.h"
#include <limits.h>

#if defined(EVENT_TRACING)
#include "phyzio_stor_hw_helper.tmh"
#endif



#define SET_VA_PA() { ULONG len; va = adaptExt->indirect ? srbExt->desc : NULL; \
                      pa = va ? StorPortGetPhysicalAddress(DeviceExtension, NULL, va, &len).QuadPart : 0; \
                    }

BOOLEAN
BtpwDoFlush(
    PVOID DeviceExtension,
    PSRB_TYPE Srb,
    BOOLEAN resend,
    BOOLEAN bIsr
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION      srbExt   = SRB_EXTENSION(Srb);
    ULONG               fragLen = 0UL;
    PVOID               va = NULL;
    ULONGLONG           pa = 0ULL;

    ULONG               QueueNumber = 0;
    ULONG               OldIrql = 0;
    ULONG               MessageId = 0;
    BOOLEAN             result = FALSE;
    bool                notify = FALSE;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    ULONG               status = STOR_STATUS_SUCCESS;
    struct virtqueue    *vq = NULL;

    SET_VA_PA();

    if (resend) {
        MessageId = srbExt->MessageID;
        QueueNumber = MessageId - 1;
    } else if (adaptExt->num_queues > 1) {
        STARTIO_PERFORMANCE_PARAMETERS param;
        param.Size = sizeof(STARTIO_PERFORMANCE_PARAMETERS);
        status = StorPortGetStartIoPerfParams(DeviceExtension, (PSCSI_REQUEST_BLOCK)Srb, &param);
        if (status == STOR_STATUS_SUCCESS && param.MessageNumber != 0) {
           MessageId = param.MessageNumber;
           QueueNumber = MessageId - 1;
           BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " srb %p, QueueNumber %lu, MessageNumber %lu, ChannelNumber %lu.\n",
                        Srb, QueueNumber, param.MessageNumber, param.ChannelNumber);
        }
        else {
           BtpwDbgPrint(TRACE_LEVEL_ERROR, " StorPortGetStartIoPerfParams failed. srb %p status 0x%x.\n",
                        Srb, status);
           QueueNumber = 0;
           MessageId = 1;
        }
    } else {
        QueueNumber = 0;
        MessageId = 1;
    }

    srbExt->MessageID = MessageId;
    vq = adaptExt->vq[QueueNumber];

    srbExt->vbr.out_hdr.sector = 0;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (struct request *)Srb;
    srbExt->vbr.out_hdr.type   = PHYZIO_BLK_T_FLUSH;
    srbExt->out                = 1;
    srbExt->in                 = 1;

    srbExt->sg[0].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &fragLen);
    srbExt->sg[0].length   = sizeof(srbExt->vbr.out_hdr);
    srbExt->sg[1].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &fragLen);
    srbExt->sg[1].length   = sizeof(srbExt->vbr.status);

    PioStorVQLock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (virtqueue_add_buf(vq,
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr, va, pa) >= 0) {
        notify = virtqueue_kick_prepare(vq);
        PioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        result = TRUE;
#ifdef DBG
        InterlockedIncrement((LONG volatile*)&adaptExt->inqueue_cnt);
#endif
    }
    else {
        PioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        BtpwDbgPrint(TRACE_LEVEL_ERROR, " Can not add packet to queue %d.\n", QueueNumber);
        StorPortBusy(DeviceExtension, 2);
    }
    if (notify) {
        virtqueue_notify(vq);
    }

    return result;
}

BOOLEAN
BtpwDoReadWrite(PVOID DeviceExtension,
                PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION      srbExt   = SRB_EXTENSION(Srb);
    PVOID               va = NULL;
    ULONGLONG           pa = 0ULL;

    ULONG               QueueNumber = 0;
    ULONG               OldIrql = 0;
    ULONG               MessageId = 0;
    BOOLEAN             result = FALSE;
    bool                notify = FALSE;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    ULONG               status = STOR_STATUS_SUCCESS;
    struct virtqueue    *vq = NULL;

    SET_VA_PA();

    if (adaptExt->num_queues > 1) {
        STARTIO_PERFORMANCE_PARAMETERS param;
        param.Size = sizeof(STARTIO_PERFORMANCE_PARAMETERS);
        status = StorPortGetStartIoPerfParams(DeviceExtension, (PSCSI_REQUEST_BLOCK)Srb, &param);
        if (status == STOR_STATUS_SUCCESS && param.MessageNumber != 0) {
           MessageId = param.MessageNumber;
           QueueNumber = MessageId - 1;
           BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " srb %p, QueueNumber %lu, MessageNumber %lu, ChannelNumber %lu.\n",
                        Srb, QueueNumber, param.MessageNumber, param.ChannelNumber);
        }
        else {
           BtpwDbgPrint(TRACE_LEVEL_ERROR, " StorPortGetStartIoPerfParams failed srb %p status 0x%x.\n",
                        Srb, status);
           QueueNumber = 0;
           MessageId = 1;
        }
    }
    else {
        QueueNumber = 0;
        MessageId = 1;
    }

    srbExt->MessageID = MessageId;
    vq = adaptExt->vq[QueueNumber];
    BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " QueueNumber 0x%x vq = %p\n", QueueNumber, vq);

    PioStorVQLock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (virtqueue_add_buf(vq,
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr, va, pa) >= 0) {
        notify = virtqueue_kick_prepare(vq);
        PioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
#ifdef DBG
        InterlockedIncrement((LONG volatile*)&adaptExt->inqueue_cnt);
#endif
        result = TRUE;
    }
    else {
        PioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        BtpwDbgPrint(TRACE_LEVEL_ERROR, " Can not add packet to queue %d.\n", QueueNumber);
        StorPortBusy(DeviceExtension, 2);
    }
    if (notify) {
        virtqueue_notify(vq);
    }

#if (NTDDI_VERSION > NTDDI_WIN7)
    if (adaptExt->num_queues > 1) {
        if (CHECKFLAG(adaptExt->perfFlags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO)) {
           PioStorCompleteRequest(DeviceExtension, MessageId, FALSE);
        }
    }
#endif
    return result;
}

#if (NTDDI_VERSION > NTDDI_WIN7)
BOOLEAN
BtpwDoUnMap(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION      srbExt   = SRB_EXTENSION(Srb);

    PUNMAP_LIST_HEADER  unmapList = NULL;
    USHORT              blockDescrDataLength = 0;

    PVOID               srbDataBuffer = SRB_DATA_BUFFER(Srb);
    ULONG               srbDataBufferLength = SRB_DATA_TRANSFER_LENGTH(Srb);

    ULONG                 i = 0;
    PUNMAP_BLOCK_DESCRIPTOR BlockDescriptors = NULL;
    USHORT BlockDescrCount = 0;
    ULONG               fragLen = 0UL;


    PVOID               va = NULL;
    ULONGLONG           pa = 0ULL;

    ULONG               QueueNumber = 0;
    ULONG               OldIrql = 0;
    ULONG               MessageId = 0;
    BOOLEAN             result = FALSE;
    BOOLEAN             notify = FALSE;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    ULONG               status = STOR_STATUS_SUCCESS;
    struct virtqueue    *vq = NULL;

    SET_VA_PA();

    unmapList = (PUNMAP_LIST_HEADER)srbDataBuffer;

    if (unmapList == NULL) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return FALSE;
    }

    REVERSE_BYTES_SHORT(&blockDescrDataLength, unmapList->BlockDescrDataLength);

    if ( !(CHECKBIT(adaptExt->features, PHYZIO_BLK_F_DISCARD)) ||
         (srbDataBufferLength < (ULONG)(blockDescrDataLength + 8)) ) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return FALSE;
    }

    BlockDescriptors = (PUNMAP_BLOCK_DESCRIPTOR)((PCHAR)srbDataBuffer + 8);
    BlockDescrCount = blockDescrDataLength / sizeof(UNMAP_BLOCK_DESCRIPTOR);
    for (i = 0; i < BlockDescrCount; i++) {
        ULONGLONG       blockDescrStartingLba;
        ULONG           blockDescrLbaCount;
        REVERSE_BYTES_QUAD(&blockDescrStartingLba, BlockDescriptors[i].StartingLba);
        REVERSE_BYTES(&blockDescrLbaCount, BlockDescriptors[i].LbaCount);
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, "Count %d BlockDescrCount = %d blockDescrStartingLba = %llu blockDescrLbaCount = %lu\n",
                     i, BlockDescrCount, blockDescrStartingLba, blockDescrLbaCount);
        adaptExt->blk_discard[i].sector = blockDescrStartingLba * (adaptExt->info.blk_size / SECTOR_SIZE);
        adaptExt->blk_discard[i].num_sectors = blockDescrLbaCount * (adaptExt->info.blk_size / SECTOR_SIZE);
        adaptExt->blk_discard[i].flags = 0;
    }

    srbExt->vbr.out_hdr.sector = 0;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (struct request *)Srb;
    srbExt->vbr.out_hdr.type   = PHYZIO_BLK_T_DISCARD | PHYZIO_BLK_T_OUT;
    srbExt->out                = 2;
    srbExt->in                 = 1;

    srbExt->sg[0].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &fragLen);
    srbExt->sg[0].length   = sizeof(srbExt->vbr.out_hdr);
    srbExt->sg[1].physAddr = MmGetPhysicalAddress(&adaptExt->blk_discard[0]);
    srbExt->sg[1].length   = sizeof(blk_discard_write_zeroes) * BlockDescrCount;
    srbExt->sg[2].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &fragLen);
    srbExt->sg[2].length   = sizeof(srbExt->vbr.status);

    if (adaptExt->num_queues > 1) {
        STARTIO_PERFORMANCE_PARAMETERS param;
        param.Size = sizeof(STARTIO_PERFORMANCE_PARAMETERS);
        status = StorPortGetStartIoPerfParams(DeviceExtension, (PSCSI_REQUEST_BLOCK)Srb, &param);
        if (status == STOR_STATUS_SUCCESS && param.MessageNumber != 0) {
           MessageId = param.MessageNumber;
           QueueNumber = MessageId - 1;
           BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " srb %p, QueueNumber %lu, MessageNumber %lu, ChannelNumber %lu.\n",
                        Srb, QueueNumber, param.MessageNumber, param.ChannelNumber);
        }
        else {
           BtpwDbgPrint(TRACE_LEVEL_ERROR, " StorPortGetStartIoPerfParams failed srb %p status 0x%x.\n",
                        Srb, status);
           QueueNumber = 0;
           MessageId = 1;
        }
    }
    else {
        QueueNumber = 0;
        MessageId = 1;
    }

    srbExt->MessageID = MessageId;
    vq = adaptExt->vq[QueueNumber];
    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " QueueNumber 0x%x vq = %p type = %d\n", QueueNumber, vq, srbExt->vbr.out_hdr.type);

    PioStorVQLock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (virtqueue_add_buf(vq,
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr, va, pa) >= 0) {
        notify = virtqueue_kick_prepare(vq);
        PioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
#ifdef DBG
        InterlockedIncrement((LONG volatile*)&adaptExt->inqueue_cnt);
#endif
        result = TRUE;
    }
    else {
        PioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        BtpwDbgPrint(TRACE_LEVEL_ERROR, " Can not add packet to queue %d.\n", QueueNumber);
        StorPortBusy(DeviceExtension, 2);
    }
    if (notify) {
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " %s virtqueue_notify %d.\n", __FUNCTION__,  QueueNumber);
        virtqueue_notify(vq);
    }
    return result;
}

#endif

BOOLEAN
BtpwGetSerialNumber(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
)
{
    ULONG               QueueNumber = 0;
    ULONG               OldIrql = 0;
    ULONG               MessageId = 1;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    struct virtqueue    *vq = NULL;
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION      srbExt   = SRB_EXTENSION(Srb);
    ULONG               status = STOR_STATUS_SUCCESS;
    PVOID               va = NULL;
    ULONGLONG           pa = 0ULL;
    BOOLEAN             result = FALSE;
    BOOLEAN             notify = FALSE;
    ULONG               fragLen = 0UL;

    SET_VA_PA();

    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " srbExt %p.\n", srbExt);

    if (adaptExt->num_queues > 1) {
        STARTIO_PERFORMANCE_PARAMETERS param;
        param.Size = sizeof(STARTIO_PERFORMANCE_PARAMETERS);
        status = StorPortGetStartIoPerfParams(DeviceExtension, (PSCSI_REQUEST_BLOCK)Srb, &param);
        if (status == STOR_STATUS_SUCCESS && param.MessageNumber != 0) {
           MessageId = param.MessageNumber;
           QueueNumber = MessageId - 1;
           BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " srb %p, QueueNumber %lu, MessageNumber %lu, ChannelNumber %lu.\n",
                        Srb, QueueNumber, param.MessageNumber, param.ChannelNumber);
        }
        else {
           BtpwDbgPrint(TRACE_LEVEL_ERROR, " StorPortGetStartIoPerfParams failed srb %p status 0x%x.\n",
                        Srb, status);
        }
    }

    vq = adaptExt->vq[QueueNumber];

    srbExt->MessageID = MessageId;
    vq = adaptExt->vq[QueueNumber];

    srbExt->vbr.out_hdr.sector = 0;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (struct request *)Srb;
    srbExt->vbr.out_hdr.type   = PHYZIO_BLK_T_GET_ID | PHYZIO_BLK_T_IN;
    srbExt->out                = 1;
    srbExt->in                 = 2;

    srbExt->sg[0].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &fragLen);
    srbExt->sg[0].length   = sizeof(srbExt->vbr.out_hdr);
    srbExt->sg[1].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &adaptExt->sn[0], &fragLen);
    srbExt->sg[1].length   = sizeof(adaptExt->sn);
    srbExt->sg[2].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &fragLen);
    srbExt->sg[2].length   = sizeof(srbExt->vbr.status);

    PioStorVQLock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (virtqueue_add_buf(vq,
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr, va, pa) >= 0) {
        notify = virtqueue_kick_prepare(vq);
        PioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        result = TRUE;
#ifdef DBG
        InterlockedIncrement((LONG volatile*)&adaptExt->inqueue_cnt);
#endif
    }
    else {
        PioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        BtpwDbgPrint(TRACE_LEVEL_ERROR, " Can not add packet to queue %d.\n", QueueNumber);
        StorPortBusy(DeviceExtension, 2);
    }
    if (notify) {
        virtqueue_notify(vq);
    }

    return result;
}


VOID
BtpwShutDown(
    IN PVOID DeviceExtension
    )
{
    ULONG index;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    phyzio_device_reset(&adaptExt->vdev);
    phyzio_delete_queues(&adaptExt->vdev);
    for (index = 0; index < adaptExt->num_queues; ++index) {
        adaptExt->vq[index] = NULL;
    }
    phyzio_device_shutdown(&adaptExt->vdev);
}

ULONGLONG
BtpwGetLba(
    IN PVOID DeviceExtension,
    IN PCDB Cdb
    )
{

    EIGHT_BYTE lba;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    lba.AsULongLong = 0;

    switch (Cdb->CDB6GENERIC.OperationCode) {

        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_WRITE_VERIFY:
        case SCSIOP_VERIFY:
#if (NTDDI_VERSION > NTDDI_WIN7)
        case SCSIOP_UNMAP:
#endif
        {
            lba.Byte0 = Cdb->CDB10.LogicalBlockByte3;
            lba.Byte1 = Cdb->CDB10.LogicalBlockByte2;
            lba.Byte2 = Cdb->CDB10.LogicalBlockByte1;
            lba.Byte3 = Cdb->CDB10.LogicalBlockByte0;
        }
        break;
        case SCSIOP_READ6:
        case SCSIOP_WRITE6: {
            lba.Byte0 = Cdb->CDB6READWRITE.LogicalBlockMsb1;
            lba.Byte1 = Cdb->CDB6READWRITE.LogicalBlockMsb0;
            lba.Byte2 = Cdb->CDB6READWRITE.LogicalBlockLsb;
        }
        break;
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
        case SCSIOP_WRITE_VERIFY12: {
            REVERSE_BYTES(&lba, &Cdb->CDB12.LogicalBlock[0]);
        }
        break;
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
        case SCSIOP_READ_CAPACITY16:
        case SCSIOP_WRITE_VERIFY16:
        case SCSIOP_VERIFY16: {
            REVERSE_BYTES_QUAD(&lba, &Cdb->CDB16.LogicalBlock[0]);
        }
        break;
        default: {
            ASSERT(FALSE);
            return (ULONGLONG)-1;
        }
    }
    return (lba.AsULongLong * (adaptExt->info.blk_size / SECTOR_SIZE));
}

ULONG
BtpwGetSectors(
    IN PVOID DeviceExtension,
    IN PCDB Cdb
)
{
    FOUR_BYTE sector;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    sector.AsULong = 0UL;

    switch (Cdb->CDB6GENERIC.OperationCode) {

        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_WRITE_VERIFY:
        case SCSIOP_VERIFY:{
            sector.Byte0 = Cdb->CDB10.TransferBlocksLsb;
            sector.Byte1 = Cdb->CDB10.TransferBlocksMsb;
        }
        break;
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
        case SCSIOP_READ_CAPACITY16:
        case SCSIOP_WRITE_VERIFY16:
        case SCSIOP_VERIFY16: {
            REVERSE_BYTES(&sector, &Cdb->CDB16.TransferLength[0]);
        }
        break;
        default: {
            ASSERT(FALSE);
            return (ULONGLONG)-1;
        }
    }
    return (sector.AsULong * (adaptExt->info.blk_size / SECTOR_SIZE));
}


VOID
BtpwGetDiskGeometry(
    IN PVOID DeviceExtension
)
{
    u64                cap;
    u32                v;
    struct phyzio_blk_geometry vgeo;

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    adaptExt->features = phyzio_get_features(&adaptExt->vdev);

    if (CHECKBIT(adaptExt->features, PHYZIO_BLK_F_BARRIER)) {
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_BLK_F_BARRIER\n");
    }

    if (CHECKBIT(adaptExt->features, PHYZIO_BLK_F_RO)) {
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_BLK_F_RO\n");
    }

    if (CHECKBIT(adaptExt->features, PHYZIO_BLK_F_SIZE_MAX)) {
        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, size_max),
                          &v, sizeof(v));
        adaptExt->info.size_max = v;
    } else {
        adaptExt->info.size_max = SECTOR_SIZE;
    }

    if (CHECKBIT(adaptExt->features, PHYZIO_BLK_F_SEG_MAX)) {
        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, seg_max),
                          &v, sizeof(v));
        adaptExt->info.seg_max = v;
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_BLK_F_SEG_MAX = %d\n", adaptExt->info.seg_max);
    }

    if (CHECKBIT(adaptExt->features, PHYZIO_BLK_F_BLK_SIZE)) {
        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, blk_size),
                          &v, sizeof(v));
        adaptExt->info.blk_size = v;
    } else {
        adaptExt->info.blk_size = SECTOR_SIZE;
    }
    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_BLK_F_BLK_SIZE = %d\n", adaptExt->info.blk_size);

    if (CHECKBIT(adaptExt->features, PHYZIO_BLK_F_GEOMETRY)) {
        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, geometry),
                          &vgeo, sizeof(vgeo));
        adaptExt->info.geometry.cylinders= vgeo.cylinders;
        adaptExt->info.geometry.heads    = vgeo.heads;
        adaptExt->info.geometry.sectors  = vgeo.sectors;
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_BLK_F_GEOMETRY. cylinders = %d  heads = %d  sectors = %d\n", adaptExt->info.geometry.cylinders, adaptExt->info.geometry.heads, adaptExt->info.geometry.sectors);
    }

    phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, capacity),
                      &cap, sizeof(cap));
    adaptExt->info.capacity = cap;
    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " device capacity = %08I64X\n", adaptExt->info.capacity);


    if(CHECKBIT(adaptExt->features, PHYZIO_BLK_F_TOPOLOGY)) {
        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, physical_block_exp),
                          &adaptExt->info.physical_block_exp, sizeof(adaptExt->info.physical_block_exp));
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " physical_block_exp = %d\n", adaptExt->info.physical_block_exp);

        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, alignment_offset),
                          &adaptExt->info.alignment_offset, sizeof(adaptExt->info.alignment_offset));
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " alignment_offset = %d\n", adaptExt->info.alignment_offset);

        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, min_io_size),
                          &adaptExt->info.min_io_size, sizeof(adaptExt->info.min_io_size));
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " min_io_size = %d\n", adaptExt->info.min_io_size);

        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, opt_io_size),
                          &adaptExt->info.opt_io_size, sizeof(adaptExt->info.opt_io_size));
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " opt_io_size = %d\n", adaptExt->info.opt_io_size);
    }

    if(CHECKBIT(adaptExt->features, PHYZIO_BLK_F_DISCARD)) {
        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, discard_sector_alignment),
                          &v, sizeof(v));
        adaptExt->info.discard_sector_alignment = v ? v << SECTOR_SHIFT : 0;
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " discard_sector_alignment = %d\n", adaptExt->info.discard_sector_alignment);

        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, max_discard_sectors),
                          &v, sizeof(v));
        adaptExt->info.max_discard_sectors = v ? v : UINT_MAX;
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " max_discard_sectors = %d\n", adaptExt->info.max_discard_sectors);

        phyzio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, max_discard_seg),
                          &v, sizeof(v));
        adaptExt->info.max_discard_seg = (v < MAX_DISCARD_SEGMENTS) ? v : MAX_DISCARD_SEGMENTS -1;
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " max_discard_seg = %d\n", adaptExt->info.max_discard_seg);
    }
}

VOID
PioStorVQLock(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN OUT PSTOR_LOCK_HANDLE LockHandle,
    IN BOOLEAN isr
    )
{
    PADAPTER_EXTENSION  adaptExt;
    BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " ---> MessageID = %d isr = %d\n",  MessageID, isr);

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    if (!isr) {
        if (adaptExt->msix_enabled) {
            if (adaptExt->num_queues > 1) {

                NT_ASSERT(MessageID > 0);
                NT_ASSERT(MessageID <= adaptExt->num_queues);
                StorPortAcquireSpinLock(DeviceExtension, DpcLock, &adaptExt->dpc[MessageID - 1], LockHandle);
            }
            else {
                ULONG oldIrql = 0;
                StorPortAcquireMSISpinLock(DeviceExtension, (adaptExt->msix_one_vector ? 0 : MessageID), &oldIrql);
                LockHandle->Context.OldIrql = (KIRQL)oldIrql;
            }
        }
        else {
            StorPortAcquireSpinLock(DeviceExtension, InterruptLock, NULL, LockHandle);
        }
    }
    BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " <--- MessageID = %d\n", MessageID);
}

VOID
PioStorVQUnlock(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN PSTOR_LOCK_HANDLE LockHandle,
    IN BOOLEAN isr
    )
{
    PADAPTER_EXTENSION  adaptExt;
    BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " ---> MessageID = %d isr = %d\n", MessageID, isr);
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (!isr) {
        if (adaptExt->msix_enabled) {
            if (adaptExt->num_queues > 1) {
                StorPortReleaseSpinLock(DeviceExtension, LockHandle);
            }
            else {
                StorPortReleaseMSISpinLock(DeviceExtension, (adaptExt->msix_one_vector ? 0 : MessageID), LockHandle->Context.OldIrql);
            }
        }
        else {
            StorPortReleaseSpinLock(DeviceExtension, LockHandle);
        }
    }
    BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " <--- MessageID = %d\n", MessageID);
}
