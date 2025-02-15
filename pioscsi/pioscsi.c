/*
 * This file contains pioscsi StorPort miniport driver
 *
 * Copyright (c) 2012-2017 Blu Tah, Inc.
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
#include "pioscsi.h"
#include "helper.h"
#include "pioscsidt.h"
#include "trace.h"

#if defined(EVENT_TRACING)
#include "pioscsi.tmh"
#endif


#define MS_SM_HBA_API
#include <hbapiwmi.h>

#include <hbaapi.h>
#include <ntddscsi.h>

#define PioScsiWmi_MofResourceName        L"MofResource"

#include "resources.h"
#include "..\Tools\vendor.ver"

#define PIOSCSI_SETUP_GUID_INDEX               0
#define PIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX   1
#define PIOSCSI_MS_PORT_INFORM_GUID_INDEX      2

BOOLEAN IsCrashDumpMode;

#if (NTDDI_VERSION > NTDDI_WIN7)
sp_DRIVER_INITIALIZE DriverEntry;
HW_INITIALIZE        PioScsiHwInitialize;
HW_BUILDIO           PioScsiBuildIo;
HW_STARTIO           PioScsiStartIo;
HW_FIND_ADAPTER      PioScsiFindAdapter;
HW_RESET_BUS         PioScsiResetBus;
HW_ADAPTER_CONTROL   PioScsiAdapterControl;
HW_INTERRUPT         PioScsiInterrupt;
HW_DPC_ROUTINE       PioScsiCompleteDpcRoutine;
HW_PASSIVE_INITIALIZE_ROUTINE         PioScsiIoPassiveInitializeRoutine;
HW_WORKITEM          PioScsiWorkItemCallback;
HW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE PioScsiMSInterrupt;
#endif


#ifdef EVENT_TRACING
PVOID TraceContext = NULL;
VOID WppCleanupRoutine(PVOID arg1) {
    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " WppCleanupRoutine\n");
    WPP_CLEANUP(NULL, TraceContext);
}
#endif

BOOLEAN
PioScsiHwInitialize(
    IN PVOID DeviceExtension
    );

BOOLEAN
PioScsiHwReinitialize(
    IN PVOID DeviceExtension
    );

BOOLEAN
PioScsiBuildIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

BOOLEAN
PioScsiStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

ULONG
PioScsiFindAdapter(
    IN PVOID DeviceExtension,
    IN PVOID HwContext,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN PBOOLEAN Again
    );

BOOLEAN
PioScsiResetBus(
    IN PVOID DeviceExtension,
    IN ULONG PathId
    );

SCSI_ADAPTER_CONTROL_STATUS
PioScsiAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters
    );

BOOLEAN
FORCEINLINE
PreProcessRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );

VOID
FORCEINLINE
PostProcessRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );

VOID
FORCEINLINE
CompleteRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );

VOID
FORCEINLINE
DispatchQueue(
    IN PVOID DeviceExtension,
    IN ULONG MessageID
    );

BOOLEAN
PioScsiInterrupt(
    IN PVOID DeviceExtension
    );

VOID
TransportReset(
    IN PVOID DeviceExtension,
    IN PPhyzIOSCSIEvent evt
    );

VOID
ParamChange(
    IN PVOID DeviceExtension,
    IN PPhyzIOSCSIEvent evt
    );

BOOLEAN
PioScsiMSInterrupt(
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    );

VOID
PioScsiWmiInitialize(
    IN PVOID  DeviceExtension
    );

VOID
PioScsiWmiSrb(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

VOID
PioScsiIoControl(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

BOOLEAN
PioScsiQueryWmiDataBlock(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG OutBufferSize,
    OUT PUCHAR Buffer
    );

UCHAR
PioScsiExecuteWmiMethod(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG MethodId,
    IN ULONG InBufferSize,
    IN ULONG OutBufferSize,
    IN OUT PUCHAR Buffer
    );

UCHAR
PioScsiQueryWmiRegInfo(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    OUT PWCHAR *MofResourceName
    );

VOID
PioScsiReadExtendedData(
    IN PVOID Context,
    OUT PUCHAR Buffer
   );

VOID
PioScsiSaveInquiryData(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

VOID
PioScsiPatchInquiryData(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

GUID PioScsiWmiExtendedInfoGuid = PioScsiWmi_ExtendedInfo_Guid;
GUID PioScsiWmiAdapterInformationQueryGuid = MS_SM_AdapterInformationQueryGuid;
GUID PioScsiWmiPortInformationMethodsGuid = MS_SM_PortInformationMethodsGuid;

SCSIWMIGUIDREGINFO PioScsiGuidList[] =
{
   { &PioScsiWmiExtendedInfoGuid,            1, 0 },
   { &PioScsiWmiAdapterInformationQueryGuid, 1, 0 },
   { &PioScsiWmiPortInformationMethodsGuid,  1, 0 },
};

#define PioScsiGuidCount (sizeof(PioScsiGuidList) / sizeof(SCSIWMIGUIDREGINFO))

void CopyUnicodeString(void* _pDest, const void* _pSrc, size_t _maxlength)
{
     PUSHORT _pDestTemp = _pDest;
     USHORT  _length = _maxlength - sizeof(USHORT);
     *_pDestTemp++ = _length;
     _length = (USHORT)min(wcslen(_pSrc)*sizeof(WCHAR), _length);
     memcpy(_pDestTemp, _pSrc, _length);
}

void CopyAnsiToUnicodeString(void* _pDest, const void* _pSrc, size_t _maxlength)
{
    PUSHORT _pDestTemp = _pDest;
    PWCHAR  dst;
    PCHAR   src = (PCHAR)_pSrc;
    USHORT  _length = _maxlength - sizeof(USHORT);
    *_pDestTemp++ = _length;
    dst = (PWCHAR)_pDestTemp;
    _length = (USHORT)min(strlen((const char*)_pSrc) * sizeof(WCHAR), _length);
    _length /= sizeof(WCHAR);
    while (_length) {
        *dst++ = *src++;
        --_length;
    };
}

USHORT CopyBufferToAnsiString(void* _pDest, const void* _pSrc, const char delimiter, size_t _maxlength)
{
    PCHAR  dst = (PCHAR)_pDest;
    PCHAR   src = (PCHAR)_pSrc;
    USHORT  _length = _maxlength;

    while (_length && (*src != ' ')) {
        *dst++ = *src++;
        --_length;
    };
    *dst = '\0';
    return _length;
}

#if (NTDDI_VERSION > NTDDI_WIN7)
BOOLEAN PioScsiReadRegistry(
    IN PVOID DeviceExtension
)
{
    BOOLEAN Ret = FALSE;
    ULONG Len = sizeof(ULONG);
    UCHAR* pBuf = NULL;
    PADAPTER_EXTENSION adaptExt;


    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    pBuf = StorPortAllocateRegistryBuffer(DeviceExtension, &Len);
    if (pBuf == NULL) {
        BtpwDbgPrint(TRACE_LEVEL_FATAL, "StorPortAllocateRegistryBuffer failed to allocate buffer\n");
        return FALSE;
    }

    memset(pBuf, 0, sizeof(ULONG));

    Ret = StorPortRegistryRead(DeviceExtension,
                               MAX_PH_BREAKS ,
                               1,
                               MINIPORT_REG_DWORD,
                               pBuf,
                               &Len);

    if ((Ret == FALSE) || (Len == 0)) {
        BtpwDbgPrint(TRACE_LEVEL_FATAL, "StorPortRegistryRead returned 0x%x, Len = %d\n", Ret, Len);
        return FALSE;
    }

    StorPortCopyMemory((PVOID)(&adaptExt->max_physical_breaks),
           (PVOID)pBuf,
           sizeof(ULONG));
    adaptExt->max_physical_breaks = min(
                                        max(SCSI_MINIMUM_PHYSICAL_BREAKS, adaptExt->max_physical_breaks),
                                        SCSI_MAXIMUM_PHYSICAL_BREAKS);

    StorPortFreeRegistryBuffer(DeviceExtension, pBuf );

    return TRUE;
}
#endif


ULONG
DriverEntry(
    IN PVOID  DriverObject,
    IN PVOID  RegistryPath
    )
{

    HW_INITIALIZATION_DATA hwInitData;
    ULONG                  initResult;

#ifdef EVENT_TRACING
    STORAGE_TRACE_INIT_INFO initInfo;
#else
#ifdef DBG
    InitializeDebugPrints((PDRIVER_OBJECT)DriverObject, (PUNICODE_STRING)RegistryPath);
#endif
#endif

    IsCrashDumpMode = FALSE;
    BtpwDbgPrint(TRACE_LEVEL_FATAL, " Pioscsi driver started...built on %s %s\n", __DATE__, __TIME__);
    if (RegistryPath == NULL) {
        IsCrashDumpMode = TRUE;
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " Crash dump mode\n");
    }

    RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    hwInitData.HwFindAdapter            = PioScsiFindAdapter;
    hwInitData.HwInitialize             = PioScsiHwInitialize;
    hwInitData.HwStartIo                = PioScsiStartIo;
    hwInitData.HwInterrupt              = PioScsiInterrupt;
    hwInitData.HwResetBus               = PioScsiResetBus;
    hwInitData.HwAdapterControl         = PioScsiAdapterControl;
    hwInitData.HwBuildIo                = PioScsiBuildIo;
    hwInitData.NeedPhysicalAddresses    = TRUE;
    hwInitData.TaggedQueuing            = TRUE;
    hwInitData.AutoRequestSense         = TRUE;
    hwInitData.MultipleRequestPerLu     = TRUE;

    hwInitData.DeviceExtensionSize      = sizeof(ADAPTER_EXTENSION);
    hwInitData.SrbExtensionSize         = sizeof(SRB_EXTENSION);

    hwInitData.AdapterInterfaceType     = PCIBus;

    /* Phyzio doesn't specify the number of BARs used by the device; it may
     * be one, it may be more. PCI_TYPE0_ADDRESSES, the theoretical maximum
     * on PCI, is a safe upper bound.
     */
    hwInitData.NumberOfAccessRanges     = PCI_TYPE0_ADDRESSES;
    hwInitData.MapBuffers               = STOR_MAP_NON_READ_WRITE_BUFFERS;

#if (NTDDI_VERSION > NTDDI_WIN7)
    /* Specify support/use SRB Extension for Windows 8 and up */
    hwInitData.SrbTypeFlags = SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK;
#endif

    initResult = StorPortInitialize(DriverObject,
                                    RegistryPath,
                                    &hwInitData,
                                    NULL);

#ifdef EVENT_TRACING
    TraceContext = NULL;

    memset(&initInfo, 0, sizeof(STORAGE_TRACE_INIT_INFO));
    initInfo.Size = sizeof(STORAGE_TRACE_INIT_INFO);
    initInfo.DriverObject = DriverObject;
    initInfo.NumErrorLogRecords = 5;
    initInfo.TraceCleanupRoutine = WppCleanupRoutine;
    initInfo.TraceContext = NULL;

    WPP_INIT_TRACING(DriverObject, RegistryPath, &initInfo);

    if (initInfo.TraceContext != NULL) {
        TraceContext = initInfo.TraceContext;
    }
#endif

    BtpwDbgPrint(TRACE_LEVEL_VERBOSE,
                 " Initialize returned 0x%x\n", initResult);

    return initResult;

}

ULONG
PioScsiFindAdapter(
    IN PVOID DeviceExtension,
    IN PVOID HwContext,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN PBOOLEAN Again
    )
{
    PADAPTER_EXTENSION adaptExt;
    PVOID              uncachedExtensionVa;
    USHORT             queueLength = 0;
    ULONG              Size;
    ULONG              HeapSize;
    ULONG              extensionSize;
    ULONG              index;
    ULONG              num_cpus;
    ULONG              max_cpus;
    ULONG              max_queues;

    UNREFERENCED_PARAMETER( HwContext );
    UNREFERENCED_PARAMETER( BusInformation );
    UNREFERENCED_PARAMETER( ArgumentString );
    UNREFERENCED_PARAMETER( Again );

ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    RtlZeroMemory(adaptExt, sizeof(ADAPTER_EXTENSION));

    adaptExt->dump_mode  = IsCrashDumpMode;
    adaptExt->hba_id     = HBA_ID;
    ConfigInfo->Master                      = TRUE;
    ConfigInfo->ScatterGather               = TRUE;
    ConfigInfo->DmaWidth                    = Width32Bits;
    ConfigInfo->Dma32BitAddresses           = TRUE;
#if (NTDDI_VERSION > NTDDI_WIN7)
    ConfigInfo->Dma64BitAddresses           = SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED;
#else
    ConfigInfo->Dma64BitAddresses           = TRUE;
#endif
    ConfigInfo->WmiDataProvider             = TRUE;
    ConfigInfo->AlignmentMask               = 0x3;
    ConfigInfo->MapBuffers                  = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel        = StorSynchronizeFullDuplex;
    ConfigInfo->HwMSInterruptRoutine        = PioScsiMSInterrupt;
    ConfigInfo->InterruptSynchronizationMode=InterruptSynchronizePerMessage;

    PioScsiWmiInitialize(DeviceExtension);

    if (!InitHW(DeviceExtension, ConfigInfo)) {
        BtpwDbgPrint(TRACE_LEVEL_FATAL, " Cannot initialize HardWare\n");
        return SP_RETURN_NOT_FOUND;
    }

    /* Set num_queues and seg_max to some sane values, to keep "Static Driver Verification" happy */
    adaptExt->scsi_config.num_queues = 1;
    adaptExt->scsi_config.seg_max = MAX_PHYS_SEGMENTS + 1;
    GetScsiConfig(DeviceExtension);
    SetMoozeFeatures(DeviceExtension);

    if(!adaptExt->dump_mode) {
        adaptExt->indirect = CHECKBIT(adaptExt->features, PHYZIO_RING_F_INDIRECT_DESC);
    }

    ConfigInfo->NumberOfBuses               = 1;//(UCHAR)adaptExt->num_queues;
    ConfigInfo->MaximumNumberOfTargets      = min((UCHAR)adaptExt->scsi_config.max_target, 255/*SCSI_MAXIMUM_TARGETS_PER_BUS*/);
    ConfigInfo->MaximumNumberOfLogicalUnits = min((UCHAR)adaptExt->scsi_config.max_lun, SCSI_MAXIMUM_LUNS_PER_TARGET);

    if(adaptExt->dump_mode) {
        ConfigInfo->NumberOfPhysicalBreaks  = SCSI_MINIMUM_PHYSICAL_BREAKS;
    } else {
        adaptExt->max_physical_breaks = MAX_PHYS_SEGMENTS;
#if (NTDDI_VERSION > NTDDI_WIN7)
        if (adaptExt->indirect) {
            PioScsiReadRegistry(DeviceExtension);
        }
#endif
        ConfigInfo->NumberOfPhysicalBreaks = adaptExt->max_physical_breaks + 1;
    }
    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " NumberOfPhysicalBreaks %d\n", ConfigInfo->NumberOfPhysicalBreaks);
    ConfigInfo->MaximumTransferLength = SP_UNINITIALIZED_VALUE;

#if (NTDDI_VERSION >= NTDDI_WIN7)
    num_cpus = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    max_cpus = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
    /* Set num_cpus and max_cpus to some sane values, to keep "Static Driver Verification" happy */
    num_cpus = max(1, num_cpus);
    max_cpus = max(1, max_cpus);
#else
    num_cpus = KeQueryActiveProcessorCount(NULL);
    max_cpus = KeQueryMaximumProcessorCount();
#endif
    adaptExt->num_queues = adaptExt->scsi_config.num_queues;
    if (adaptExt->dump_mode || !adaptExt->msix_enabled)
    {
        adaptExt->num_queues = 1;
    }
    else if (adaptExt->num_queues < num_cpus)
    {
//FIXME
        adaptExt->num_queues = 1;
    }
    else
    {
//FIXME
#if (NTDDI_VERSION > NTDDI_WIN7)
        adaptExt->num_queues = num_cpus;
#else
        adaptExt->num_queues = 1;
#endif
    }

    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " Queues %d CPUs %d\n", adaptExt->num_queues, num_cpus);

    /* Figure out the maximum number of queues we will ever need to set up. Note that this may
     * be higher than adaptExt->num_queues, because the driver may be reinitialized by calling
     * PioScsiFindAdapter again with more CPUs enabled. Unfortunately StorPortGetUncachedExtension
     * only allocates when called for the first time so we need to always use this upper bound.
     */
    if (adaptExt->dump_mode) {
        max_queues = adaptExt->num_queues;
    } else {
        max_queues = min(max_cpus, adaptExt->scsi_config.num_queues);
        if (adaptExt->num_queues > max_queues) {
            BtpwDbgPrint(TRACE_LEVEL_WARNING, " Multiqueue can only use at most one queue per cpu.");
            adaptExt->num_queues = max_queues;
        }
    }


    /* This function is our only chance to allocate memory for the driver; allocations are not
     * possible later on. Even worse, the only allocation mechanism guaranteed to work in all
     * cases is StorPortGetUncachedExtension, which gives us one block of physically contiguous
     * pages.
     *
     * Allocations that need to be page-aligned will be satisfied from this one block starting
     * at the first page-aligned offset, up to adaptExt->pageAllocationSize computed below. Other
     * allocations will be cache-line-aligned, of total size adaptExt->poolAllocationSize, also
     * computed below.
     */
    adaptExt->pageAllocationSize = 0;
    adaptExt->poolAllocationSize = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;
    Size = 0;
    for (index = PHYZIO_SCSI_CONTROL_QUEUE; index < max_queues + PHYZIO_SCSI_REQUEST_QUEUE_0; ++index) {
        phyzio_query_queue_allocation(&adaptExt->vdev, index, &queueLength, &Size, &HeapSize);
        if (Size == 0) {
            LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

            BtpwDbgPrint(TRACE_LEVEL_FATAL, " Virtual queue %d config failed.\n", index);
            return SP_RETURN_ERROR;
        }
        adaptExt->pageAllocationSize += ROUND_TO_PAGES(Size);
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(HeapSize);
    }
    if (!adaptExt->dump_mode) {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(SRB_EXTENSION));
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(PhyzIOSCSIEventNode) * 8);
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(STOR_DPC) * max_queues);
    }
    if (max_queues + PHYZIO_SCSI_REQUEST_QUEUE_0 > MAX_QUEUES_PER_DEVICE_DEFAULT)
    {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(
            (max_queues + PHYZIO_SCSI_REQUEST_QUEUE_0) * phyzio_get_queue_descriptor_size());
    }

    if(adaptExt->indirect) {
        adaptExt->queue_depth = queueLength;
    } else {
        adaptExt->queue_depth = queueLength / ConfigInfo->NumberOfPhysicalBreaks - 1;
    }
#if (NTDDI_VERSION > NTDDI_WIN7)
    ConfigInfo->MaxIOsPerLun = adaptExt->queue_depth * adaptExt->num_queues;
    ConfigInfo->InitialLunQueueDepth = ConfigInfo->MaxIOsPerLun;
    if (ConfigInfo->MaxIOsPerLun * ConfigInfo->MaximumNumberOfTargets > ConfigInfo->MaxNumberOfIO) {
        ConfigInfo->MaxNumberOfIO = ConfigInfo->MaxIOsPerLun * ConfigInfo->MaximumNumberOfTargets;
    }
#else
    // Prior to win8, lun queue depth must be at most 254.
    adaptExt->queue_depth = min(254, adaptExt->queue_depth);
#endif

    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth);

    extensionSize = PAGE_SIZE + adaptExt->pageAllocationSize + adaptExt->poolAllocationSize;
    uncachedExtensionVa = StorPortGetUncachedExtension(DeviceExtension, ConfigInfo, extensionSize);
    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " StorPortGetUncachedExtension uncachedExtensionVa = %p allocation size = %d\n", uncachedExtensionVa, extensionSize);
    if (!uncachedExtensionVa) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        BtpwDbgPrint(TRACE_LEVEL_FATAL, " Can't get uncached extension allocation size = %d\n", extensionSize);
        return SP_RETURN_ERROR;
    }

    /* At this point we have all the memory we're going to need. We lay it out as follows.
     * Note that StorPortGetUncachedExtension tends to return page-aligned memory so the
     * padding1 region will typically be empty and the size of padding2 equal to PAGE_SIZE.
     *
     * uncachedExtensionVa    pageAllocationVa         poolAllocationVa
     * +----------------------+------------------------+--------------------------+----------------------+
     * | \ \ \ \ \ \ \ \ \ \  |<= pageAllocationSize =>|<=  poolAllocationSize  =>| \ \ \ \ \ \ \ \ \ \  |
     * |  \ \  padding1 \ \ \ |                        |                          |  \ \  padding2 \ \ \ |
     * | \ \ \ \ \ \ \ \ \ \  |    page-aligned area   | pool area for cache-line | \ \ \ \ \ \ \ \ \ \  |
     * |  \ \ \ \ \ \ \ \ \ \ |                        | aligned allocations      |  \ \ \ \ \ \ \ \ \ \ |
     * +----------------------+------------------------+--------------------------+----------------------+
     * |<=====================================  extensionSize  =========================================>|
     */
    adaptExt->pageAllocationVa = (PVOID)(((ULONG_PTR)(uncachedExtensionVa) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    if (adaptExt->poolAllocationSize > 0) {
        adaptExt->poolAllocationVa = (PVOID)((ULONG_PTR)adaptExt->pageAllocationVa + adaptExt->pageAllocationSize);
    }
    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " Page-aligned area at %p, size = %d\n", adaptExt->pageAllocationVa, adaptExt->pageAllocationSize);
    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " Pool area at %p, size = %d\n", adaptExt->poolAllocationVa, adaptExt->poolAllocationSize);

#if (NTDDI_VERSION > NTDDI_WIN7)
    BtpwDbgPrint(TRACE_LEVEL_FATAL, " pmsg_affinity = %p\n",adaptExt->pmsg_affinity);
    if (!adaptExt->dump_mode && (adaptExt->num_queues > 1) && (adaptExt->pmsg_affinity == NULL)) {
        ULONG Status =
        StorPortAllocatePool(DeviceExtension,
                             sizeof(GROUP_AFFINITY) * (adaptExt->num_queues + 3),
                             PIOSCSI_POOL_TAG,
                             (PVOID*)&adaptExt->pmsg_affinity);
        BtpwDbgPrint(TRACE_LEVEL_FATAL, " pmsg_affinity = %p Status = %lu\n",adaptExt->pmsg_affinity, Status);
    }
#endif

EXIT_FN();
    return SP_RETURN_FOUND;
}

BOOLEAN
PioScsiPassiveInitializeRoutine(
    IN PVOID DeviceExtension
)
{
    ULONG index;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
ENTER_FN();

    for (index = 0; index < adaptExt->num_queues; ++index) {
        StorPortInitializeDpc(DeviceExtension,
            &adaptExt->dpc[index],
            PioScsiCompleteDpcRoutine);
    }
    adaptExt->dpc_ok = TRUE;
EXIT_FN();
    return TRUE;
}

static BOOLEAN InitializeVirtualQueues(PADAPTER_EXTENSION adaptExt, ULONG numQueues)
{
    NTSTATUS status;

    status = phyzio_find_queues(
        &adaptExt->vdev,
        numQueues,
        adaptExt->vq);
    if (!NT_SUCCESS(status)) {
        BtpwDbgPrint(TRACE_LEVEL_FATAL, " FAILED with status 0x%x\n", status);
        return FALSE;
    }

    return TRUE;
}

PVOID
PioScsiPoolAlloc(
    IN PVOID DeviceExtension,
    IN SIZE_T size
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PVOID ptr = (PVOID)((ULONG_PTR)adaptExt->poolAllocationVa + adaptExt->poolOffset);

    if ((adaptExt->poolOffset + size) <= adaptExt->poolAllocationSize) {
        size = ROUND_TO_CACHE_LINES(size);
        adaptExt->poolOffset += (ULONG)size;
        RtlZeroMemory(ptr, size);
        return ptr;
    } else {
        BtpwDbgPrint(TRACE_LEVEL_FATAL, " Out of memory %Id \n", size);
        return NULL;
    }
}

BOOLEAN
PioScsiHwInitialize(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONG              i;
    ULONG              index;

    PERF_CONFIGURATION_DATA perfData = { 0 };
    ULONG              status = STOR_STATUS_SUCCESS;
    MESSAGE_INTERRUPT_INFORMATION msi_info = { 0 };

ENTER_FN();

    adaptExt->msix_vectors = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;

    while(StorPortGetMSIInfo(DeviceExtension, adaptExt->msix_vectors, &msi_info) == STOR_STATUS_SUCCESS) {
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " MessageId = %x\n", msi_info.MessageId);
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " MessageData = %x\n", msi_info.MessageData);
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " InterruptVector = %x\n", msi_info.InterruptVector);
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " InterruptLevel = %x\n", msi_info.InterruptLevel);
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " InterruptMode = %s\n", msi_info.InterruptMode == LevelSensitive ? "LevelSensitive" : "Latched");
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " MessageAddress = %I64x\n\n", msi_info.MessageAddress.QuadPart);
        ++adaptExt->msix_vectors;
    }

    BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " Queues %d msix_vectors %d\n", adaptExt->num_queues, adaptExt->msix_vectors);
    if (adaptExt->num_queues > 1 &&
        ((adaptExt->num_queues + 3) > adaptExt->msix_vectors)) {
        //FIXME
        adaptExt->num_queues = 1;
    }

    if (!adaptExt->dump_mode && adaptExt->msix_vectors > 0) {
        if (adaptExt->msix_vectors >= adaptExt->num_queues + 3) {
            /* initialize queues with a MSI vector per queue */
            BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " Using a unique MSI vector per queue\n");
            adaptExt->msix_one_vector = FALSE;
        } else {
            /* if we don't have enough vectors, use one for all queues */
            BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " Using one MSI vector for all queues\n");
            adaptExt->msix_one_vector = TRUE;
        }
        if (!InitializeVirtualQueues(adaptExt, adaptExt->num_queues + PHYZIO_SCSI_REQUEST_QUEUE_0)) {
            return FALSE;
        }

#ifdef USE_CPU_TO_VQ_MAP
        if (!CHECKFLAG(adaptExt->perfFlags, STOR_PERF_ADV_CONFIG_LOCALITY)) {
            for (index = 0; index < adaptExt->num_queues; ++index) {
                adaptExt->cpu_to_vq_map[index] = (UCHAR)(index);
            }
        }
#endif // USE_CPU_TO_VQ_MAP
    }
    else
    {
        /* initialize queues with no MSI interrupts */
        adaptExt->msix_enabled = FALSE;
        if (!InitializeVirtualQueues(adaptExt, adaptExt->num_queues + PHYZIO_SCSI_REQUEST_QUEUE_0)) {
            return FALSE;
        }
    }

    for (index = 0; index < adaptExt->num_queues; ++index) {
          PREQUEST_LIST  element = &adaptExt->pending_list[index];
          InitializeListHead(&element->srb_list);
          KeInitializeSpinLock(&element->srb_list_lock);
    }

    if (!adaptExt->dump_mode) {
        /* we don't get another chance to call StorPortEnablePassiveInitialization and initialize
         * DPCs if the adapter is being restarted, so leave our datastructures alone on restart
         */
        if (adaptExt->dpc == NULL) {
            adaptExt->tmf_cmd.SrbExtension = (PSRB_EXTENSION)PioScsiPoolAlloc(DeviceExtension, sizeof(SRB_EXTENSION));
            adaptExt->events = (PPhyzIOSCSIEventNode)PioScsiPoolAlloc(DeviceExtension, sizeof(PhyzIOSCSIEventNode) * 8);
            adaptExt->dpc = (PSTOR_DPC)PioScsiPoolAlloc(DeviceExtension, sizeof(STOR_DPC) * adaptExt->num_queues);
        }
    }

    if (!adaptExt->dump_mode && CHECKBIT(adaptExt->features, PHYZIO_SCSI_F_HOTPLUG)) {
        PPhyzIOSCSIEventNode events = adaptExt->events;
        for (i = 0; i < 8; i++) {
           if (!KickEvent(DeviceExtension, (PVOID)(&events[i]))) {
                BtpwDbgPrint(TRACE_LEVEL_FATAL, " Cannot add event %d\n", i);
           }
        }
    }
    if (!adaptExt->dump_mode)
    {
        if ((adaptExt->num_queues > 1) && (adaptExt->perfFlags == 0)) {
#if 1
            perfData.Version = STOR_PERF_VERSION;
            perfData.Size = sizeof(PERF_CONFIGURATION_DATA);

            status = StorPortInitializePerfOpts(DeviceExtension, TRUE, &perfData);

            BtpwDbgPrint(TRACE_LEVEL_FATAL, " Current PerfOpts Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                        perfData.Version,
                        perfData.Flags,
                        perfData.ConcurrentChannels,
                        perfData.FirstRedirectionMessageNumber,
                        perfData.LastRedirectionMessageNumber);
            if ( (status == STOR_STATUS_SUCCESS) &&
                 (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION)) ) {
                    adaptExt->perfFlags = STOR_PERF_DPC_REDIRECTION;
                if (CHECKFLAG(perfData.Flags, STOR_PERF_INTERRUPT_MESSAGE_RANGES)) {
                    adaptExt->perfFlags |= STOR_PERF_INTERRUPT_MESSAGE_RANGES;
                    perfData.FirstRedirectionMessageNumber = 3;
                    perfData.LastRedirectionMessageNumber = perfData.FirstRedirectionMessageNumber + adaptExt->num_queues - 1;
                    if ((adaptExt->pmsg_affinity != NULL) && CHECKFLAG(perfData.Flags, STOR_PERF_ADV_CONFIG_LOCALITY)) {
                        RtlZeroMemory((PCHAR)adaptExt->pmsg_affinity, sizeof (GROUP_AFFINITY)* (adaptExt->num_queues + 3));
                        adaptExt->perfFlags |= STOR_PERF_ADV_CONFIG_LOCALITY;
                        perfData.MessageTargets = adaptExt->pmsg_affinity;
#if (NTDDI_VERSION > NTDDI_WIN7)
                        if (CHECKFLAG(perfData.Flags, STOR_PERF_CONCURRENT_CHANNELS)) {
                            adaptExt->perfFlags |= STOR_PERF_CONCURRENT_CHANNELS;
                            perfData.ConcurrentChannels = adaptExt->num_queues;
                        }
#endif
                    }
                }
#if (NTDDI_VERSION > NTDDI_WIN7)
                if (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION_CURRENT_CPU)) {
//                    adaptExt->perfFlags |= STOR_PERF_DPC_REDIRECTION_CURRENT_CPU;
                }
#endif
                if (CHECKFLAG(perfData.Flags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO)) {
//                    adaptExt->perfFlags |= STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO;
                }
                perfData.Flags = adaptExt->perfFlags;
                BtpwDbgPrint(TRACE_LEVEL_FATAL, "Applied PerfOpts Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                            perfData.Version,
                            perfData.Flags,
                            perfData.ConcurrentChannels,
                            perfData.FirstRedirectionMessageNumber,
                            perfData.LastRedirectionMessageNumber);
                status = StorPortInitializePerfOpts(DeviceExtension, FALSE, &perfData);
                if (status != STOR_STATUS_SUCCESS) {
                    adaptExt->perfFlags = 0;
                    BtpwDbgPrint(TRACE_LEVEL_ERROR, " StorPortInitializePerfOpts set failed with status = 0x%x\n", status);
                }
#ifdef USE_CPU_TO_VQ_MAP
                else if ((adaptExt->pmsg_affinity != NULL) && CHECKFLAG(perfData.Flags, STOR_PERF_ADV_CONFIG_LOCALITY)){
                    UCHAR msg = 0;
                    PGROUP_AFFINITY ga;
                    UCHAR cpu = 0;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " Perf Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                        perfData.Version, perfData.Flags, perfData.ConcurrentChannels, perfData.FirstRedirectionMessageNumber, perfData.LastRedirectionMessageNumber);
                    for (msg = 0; msg < adaptExt->num_queues + 3; msg++) {
                        ga = &adaptExt->pmsg_affinity[msg];
                        if ( ga->Mask > 0 && msg > 2) {
                            cpu = RtlFindLeastSignificantBit((ULONGLONG)ga->Mask);
                            adaptExt->cpu_to_vq_map[cpu] = MESSAGE_TO_QUEUE(msg) - PHYZIO_SCSI_REQUEST_QUEUE_0;
                            BtpwDbgPrint(TRACE_LEVEL_FATAL, " msg = %d, mask = 0x%lx group = %hu cpu = %hu vq = %hu\n", msg, ga->Mask, ga->Group, cpu, adaptExt->cpu_to_vq_map[cpu]);
                        }
                    }
                }
#endif // USE_CPU_TO_VQ_MAP
            }
            else {
                BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " StorPortInitializePerfOpts get failed with status = 0x%x\n", status);
            }
#endif
        }
        if (!adaptExt->dpc_ok && !StorPortEnablePassiveInitialization(DeviceExtension, PioScsiPassiveInitializeRoutine)) {
            BtpwDbgPrint(TRACE_LEVEL_FATAL, " StorPortEnablePassiveInitialization FAILED\n");
            return FALSE;
        }
    }

    phyzio_device_ready(&adaptExt->vdev);
EXIT_FN();
    return TRUE;
}

BOOLEAN
PioScsiHwReinitialize(
    IN PVOID DeviceExtension
    )
{
    /* The adapter is being restarted and we need to bring it back up without
     * running any passive-level code. Note that PioScsiFindAdapter is *not*
     * called on restart.
     */
    if (!InitPhyzIODevice(DeviceExtension)) {
        return FALSE;
    }
    SetMoozeFeatures(DeviceExtension);
    return PioScsiHwInitialize(DeviceExtension);
}

BOOLEAN
PioScsiStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN_SRB();
    if (PreProcessRequest(DeviceExtension, (PSRB_TYPE)Srb))
    {
        CompleteRequest(DeviceExtension, (PSRB_TYPE)Srb);
    }
    else
    {
        SendSRB(DeviceExtension, (PSRB_TYPE)Srb, FALSE, -1);
    }
EXIT_FN_SRB();
    return TRUE;
}

VOID
HandleResponse(
    IN PVOID DeviceExtension,
    IN PPhyzIOSCSICmd cmd
)
{
    PSRB_TYPE Srb = (PSRB_TYPE)(cmd->srb);
    PSRB_EXTENSION srbExt = SRB_EXTENSION(Srb);
    PhyzIOSCSICmdResp *resp = &cmd->resp.cmd;
    UCHAR senseInfoBufferLength = 0;
    PVOID senseInfoBuffer = NULL;
    UCHAR srbStatus = SRB_STATUS_SUCCESS;
    ULONG srbDataTransferLen = SRB_DATA_TRANSFER_LENGTH(Srb);

ENTER_FN();

    switch (resp->response) {
    case PHYZIO_SCSI_S_OK:
        SRB_SET_SCSI_STATUS(Srb, resp->status);
        srbStatus = (resp->status == SCSISTAT_GOOD) ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
        break;
    case PHYZIO_SCSI_S_UNDERRUN:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_UNDERRUN\n");
        srbStatus = SRB_STATUS_DATA_OVERRUN;
        break;
    case PHYZIO_SCSI_S_ABORTED:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_ABORTED\n");
        srbStatus = SRB_STATUS_ABORTED;
        break;
    case PHYZIO_SCSI_S_BAD_TARGET:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_BAD_TARGET\n");
        srbStatus = SRB_STATUS_INVALID_TARGET_ID;
        break;
    case PHYZIO_SCSI_S_RESET:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_RESET\n");
        srbStatus = SRB_STATUS_BUS_RESET;
        break;
    case PHYZIO_SCSI_S_BUSY:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_BUSY\n");
        srbStatus = SRB_STATUS_BUSY;
        break;
    case PHYZIO_SCSI_S_TRANSPORT_FAILURE:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_TRANSPORT_FAILURE\n");
        srbStatus = SRB_STATUS_ERROR;
        break;
    case PHYZIO_SCSI_S_TARGET_FAILURE:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_TARGET_FAILURE\n");
        srbStatus = SRB_STATUS_ERROR;
        break;
    case PHYZIO_SCSI_S_NEXUS_FAILURE:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_NEXUS_FAILURE\n");
        srbStatus = SRB_STATUS_ERROR;
        break;
    case PHYZIO_SCSI_S_FAILURE:
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " PHYZIO_SCSI_S_FAILURE\n");
        srbStatus = SRB_STATUS_ERROR;
        break;
    default:
        srbStatus = SRB_STATUS_ERROR;
        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " Unknown response %d\n", resp->response);
        break;
    }
    if (srbStatus == SRB_STATUS_SUCCESS &&
        resp->resid &&
        srbDataTransferLen > resp->resid)
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, srbDataTransferLen - resp->resid);
        srbStatus = SRB_STATUS_DATA_OVERRUN;
    }
    else if (srbStatus != SRB_STATUS_SUCCESS)
    {
        SRB_GET_SENSE_INFO(Srb, senseInfoBuffer, senseInfoBufferLength);
        if (senseInfoBufferLength >= FIELD_OFFSET(SENSE_DATA, CommandSpecificInformation)) {
            RtlCopyMemory(senseInfoBuffer, resp->sense,
                min(resp->sense_len, senseInfoBufferLength));
            if (srbStatus == SRB_STATUS_ERROR) {
                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }
        }
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, 0);
    }
    else if (srbExt && srbExt->Xfer && srbDataTransferLen > srbExt->Xfer)
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, srbExt->Xfer);
        srbStatus = SRB_STATUS_DATA_OVERRUN;
    }
    SRB_SET_SRB_STATUS(Srb, srbStatus);
    CompleteRequest(DeviceExtension, Srb);

EXIT_FN();
}

BOOLEAN
PioScsiInterrupt(
    IN PVOID DeviceExtension
    )
{
    PPhyzIOSCSICmd      cmd = NULL;
    PPhyzIOSCSIEventNode evtNode = NULL;
    unsigned int        len = 0;
    PADAPTER_EXTENSION  adaptExt = NULL;
    BOOLEAN             isInterruptServiced = FALSE;
    PSRB_TYPE           Srb = NULL;
    ULONG               intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " IRQL (%d)\n", KeGetCurrentIrql());
    intReason = phyzio_read_isr_status(&adaptExt->vdev);

    if (intReason == 1 || adaptExt->dump_mode) {
        isInterruptServiced = TRUE;

        if (adaptExt->tmf_infly) {
           while((cmd = (PPhyzIOSCSICmd)virtqueue_get_buf(adaptExt->vq[PHYZIO_SCSI_CONTROL_QUEUE], &len)) != NULL) {
              PhyzIOSCSICtrlTMFResp *resp;
              Srb = (PSRB_TYPE)cmd->srb;
              ASSERT(Srb == (PSRB_TYPE)&adaptExt->tmf_cmd.Srb);
              resp = &cmd->resp.tmf;
              switch(resp->response) {
              case PHYZIO_SCSI_S_OK:
              case PHYZIO_SCSI_S_FUNCTION_SUCCEEDED:
                 break;
              default:
                 BtpwDbgPrint(TRACE_LEVEL_ERROR, " unknown response %d\n", resp->response);
                 ASSERT(0);
                 break;
              }
              StorPortResume(DeviceExtension);
           }
           adaptExt->tmf_infly = FALSE;
        }
        while((evtNode = (PPhyzIOSCSIEventNode)virtqueue_get_buf(adaptExt->vq[PHYZIO_SCSI_EVENTS_QUEUE], &len)) != NULL) {
           PPhyzIOSCSIEvent evt = &evtNode->event;
           switch (evt->event) {
           case PHYZIO_SCSI_T_NO_EVENT:
              break;
           case PHYZIO_SCSI_T_TRANSPORT_RESET:
              TransportReset(DeviceExtension, evt);
              break;
           case PHYZIO_SCSI_T_PARAM_CHANGE:
              ParamChange(DeviceExtension, evt);
              break;
           default:
              BtpwDbgPrint(TRACE_LEVEL_ERROR, " Unsupport phyzio scsi event %x\n", evt->event);
              break;
           }
           SynchronizedKickEventRoutine(DeviceExtension, evtNode);
        }

        if (!adaptExt->dump_mode && adaptExt->dpc_ok)
        {
            StorPortIssueDpc(DeviceExtension,
                &adaptExt->dpc[0],
                ULongToPtr(QUEUE_TO_MESSAGE(PHYZIO_SCSI_REQUEST_QUEUE_0)),
                ULongToPtr(QUEUE_TO_MESSAGE(PHYZIO_SCSI_REQUEST_QUEUE_0)));
        }
        else
            ProcessQueue(DeviceExtension, QUEUE_TO_MESSAGE(PHYZIO_SCSI_REQUEST_QUEUE_0), TRUE);
    }

    BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " isInterruptServiced = %d\n", isInterruptServiced);
    return isInterruptServiced;
}

static BOOLEAN
PioScsiMSInterruptWorker(
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    )
{
    PPhyzIOSCSICmd      cmd;
    PPhyzIOSCSIEventNode evtNode;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    PSRB_TYPE           Srb = NULL;
    ULONG               intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    BtpwDbgPrint(TRACE_LEVEL_VERBOSE,
                 " MessageID 0x%x\n", MessageID);

    if (MessageID >= QUEUE_TO_MESSAGE(PHYZIO_SCSI_REQUEST_QUEUE_0))
    {
        DispatchQueue(DeviceExtension, MessageID);
        return TRUE;
    }
    if (MessageID == 0)
    {
       return TRUE;
    }
    if (MessageID == QUEUE_TO_MESSAGE(PHYZIO_SCSI_CONTROL_QUEUE))
    {
        if (adaptExt->tmf_infly)
        {
           while((cmd = (PPhyzIOSCSICmd)virtqueue_get_buf(adaptExt->vq[PHYZIO_SCSI_CONTROL_QUEUE], &len)) != NULL)
           {
              PhyzIOSCSICtrlTMFResp *resp;
              Srb = (PSRB_TYPE)(cmd->srb);
              ASSERT(Srb == (PSRB_TYPE)&adaptExt->tmf_cmd.Srb);
              resp = &cmd->resp.tmf;
              switch(resp->response) {
              case PHYZIO_SCSI_S_OK:
              case PHYZIO_SCSI_S_FUNCTION_SUCCEEDED:
                 break;
              default:
                 BtpwDbgPrint(TRACE_LEVEL_ERROR, " Unknown response %d\n", resp->response);
                 ASSERT(0);
                 break;
              }
              StorPortResume(DeviceExtension);
           }
           adaptExt->tmf_infly = FALSE;
        }
        return TRUE;
    }
    if (MessageID == QUEUE_TO_MESSAGE(PHYZIO_SCSI_EVENTS_QUEUE)) {
        while((evtNode = (PPhyzIOSCSIEventNode)virtqueue_get_buf(adaptExt->vq[PHYZIO_SCSI_EVENTS_QUEUE], &len)) != NULL) {
           PPhyzIOSCSIEvent evt = &evtNode->event;
           switch (evt->event) {
           case PHYZIO_SCSI_T_NO_EVENT:
              break;
           case PHYZIO_SCSI_T_TRANSPORT_RESET:
              TransportReset(DeviceExtension, evt);
              break;
           case PHYZIO_SCSI_T_PARAM_CHANGE:
              ParamChange(DeviceExtension, evt);
              break;
           default:
              BtpwDbgPrint(TRACE_LEVEL_ERROR, " Unsupport phyzio scsi event %x\n", evt->event);
              break;
           }
           SynchronizedKickEventRoutine(DeviceExtension, evtNode);
        }
        return TRUE;
    }
    return FALSE;
}

BOOLEAN
PioScsiMSInterrupt(
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    BOOLEAN isInterruptServiced = FALSE;
    ULONG i;

    if (!adaptExt->msix_one_vector) {
        /* Each queue has its own vector, this is the fast and common case */
        return PioScsiMSInterruptWorker(DeviceExtension, MessageID);
    }

    /* Fall back to checking all queues */
    for (i = 0; i < adaptExt->num_queues + PHYZIO_SCSI_REQUEST_QUEUE_0; i++) {
        if (virtqueue_has_buf(adaptExt->vq[i])) {
            isInterruptServiced |= PioScsiMSInterruptWorker(DeviceExtension, i + 1);
        }
    }
    return isInterruptServiced;
}

BOOLEAN
PioScsiResetBus(
    IN PVOID DeviceExtension,
    IN ULONG PathId
    )
{
    UNREFERENCED_PARAMETER( PathId );

    return DeviceReset(DeviceExtension);
}

SCSI_ADAPTER_CONTROL_STATUS
PioScsiAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters
    )
{
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST ControlTypeList;
    ULONG                             AdjustedMaxControlType;
    ULONG                             Index;
    PADAPTER_EXTENSION                adaptExt;
    SCSI_ADAPTER_CONTROL_STATUS       status = ScsiAdapterControlUnsuccessful;
    BOOLEAN SupportedControlTypes[5] = {TRUE, TRUE, TRUE, FALSE, FALSE};

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

ENTER_FN();

    switch (ControlType) {

    case ScsiQuerySupportedControlTypes: {
        BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " ScsiQuerySupportedControlTypes\n");
        ControlTypeList = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
        AdjustedMaxControlType =
            (ControlTypeList->MaxControlType < 5) ?
            ControlTypeList->MaxControlType :
            5;
        for (Index = 0; Index < AdjustedMaxControlType; Index++) {
            ControlTypeList->SupportedTypeList[Index] =
                SupportedControlTypes[Index];
        }
        status = ScsiAdapterControlSuccess;
        break;
    }
    case ScsiStopAdapter: {
        BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " ScsiStopAdapter\n");
        ShutDown(DeviceExtension);
        status = ScsiAdapterControlSuccess;
        break;
    }
    case ScsiRestartAdapter: {
        BtpwDbgPrint(TRACE_LEVEL_FATAL, " ScsiRestartAdapter\n");
        ShutDown(DeviceExtension);
        if (!PioScsiHwReinitialize(DeviceExtension))
        {
           BtpwDbgPrint(TRACE_LEVEL_FATAL, " Cannot reinitialize HW\n");
           break;
        }
        status = ScsiAdapterControlSuccess;
        break;
    }
    default:
        BtpwDbgPrint(TRACE_LEVEL_FATAL, " Unsupported ControlType %d\n", ControlType);
        break;
    }

EXIT_FN();
    return status;
}

BOOLEAN
PioScsiBuildIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PCDB                  cdb;
    ULONG                 i;
    ULONG                 fragLen;
    ULONG                 sgElement;
    ULONG                 sgMaxElements;
    PADAPTER_EXTENSION    adaptExt;
    PSRB_EXTENSION        srbExt;
    PSTOR_SCATTER_GATHER_LIST sgList;
    PhyzIOSCSICmd         *cmd;
    UCHAR                 TargetId;
    UCHAR                 Lun;
#ifdef USE_CPU_TO_VQ_MAP
#if (NTDDI_VERSION >= NTDDI_WIN7)
    PROCESSOR_NUMBER ProcNumber;
    ULONG processor = KeGetCurrentProcessorNumberEx(&ProcNumber);
    ULONG cpu = ProcNumber.Number;
#else
    ULONG cpu = KeGetCurrentProcessorNumber();
#endif
#endif

ENTER_FN_SRB();
    cdb      = SRB_CDB(Srb);
    srbExt   = SRB_EXTENSION(Srb);
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    TargetId = SRB_TARGET_ID(Srb);
    Lun      = SRB_LUN(Srb);

    if( (SRB_PATH_ID(Srb) > (UCHAR)adaptExt->num_queues) ||
        (TargetId >= adaptExt->scsi_config.max_target) ||
        (Lun >= adaptExt->scsi_config.max_lun) ) {
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_NO_DEVICE);
        StorPortNotification(RequestComplete,
                             DeviceExtension,
                             Srb);
        return FALSE;
    }

    BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " <--> %s (%d::%d::%d)\n", DbgGetScsiOpStr(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb));

    RtlZeroMemory(srbExt, sizeof(*srbExt));
    srbExt->Srb = Srb;
    srbExt->psgl = srbExt->pio_sg;
    srbExt->pdesc = srbExt->desc_alias;

    srbExt->allocated = 0;
#ifdef USE_CPU_TO_VQ_MAP
    srbExt->cpu = (UCHAR)cpu;
#endif
    cmd = &srbExt->cmd;
    cmd->srb = (PVOID)Srb;
    cmd->req.cmd.lun[0] = 1;
    cmd->req.cmd.lun[1] = TargetId;
    cmd->req.cmd.lun[2] = 0;
    cmd->req.cmd.lun[3] = Lun;
    cmd->req.cmd.tag = (ULONG_PTR)(Srb);
    cmd->req.cmd.task_attr = PHYZIO_SCSI_S_SIMPLE;
    cmd->req.cmd.prio = 0;
    cmd->req.cmd.crn = 0;
    if (cdb != NULL) {
        RtlCopyMemory(cmd->req.cmd.cdb, cdb, min(PHYZIO_SCSI_CDB_SIZE, SRB_CDB_LENGTH(Srb)));
    }

    sgElement = 0;
    srbExt->psgl[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->req.cmd, &fragLen);
    srbExt->psgl[sgElement].length   = sizeof(cmd->req.cmd);
    sgElement++;

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);
    if (sgList)
    {
        sgMaxElements = sgList->NumberOfElements;

#if (NTDDI_VERSION > NTDDI_WIN7)
        if (sgMaxElements > MAX_PHYS_SEGMENTS && adaptExt->indirect)
        {
            PHYSICAL_ADDRESS Low;
            PHYSICAL_ADDRESS High;
            PHYSICAL_ADDRESS Alighn;
            ULONG Status =   STATUS_SUCCESS;
            srbExt->allocated = sgMaxElements + 3;

            Status = StorPortAllocatePool(DeviceExtension,
                                    sizeof(PIO_SG) * (srbExt->allocated),
                                    PIOSCSI_POOL_TAG,
                                    (PVOID*)&srbExt->psgl);
            if (!NT_SUCCESS(Status)) {
                BtpwDbgPrint(TRACE_LEVEL_FATAL, " FAILED to allocate pool with status 0x%x\n", Status);
                return FALSE;
            }

            memcpy(srbExt->psgl, srbExt->pio_sg, sizeof(PIO_SG));
            Low.QuadPart = 0;
            High.QuadPart = (-1);
            Alighn.QuadPart = 0;
            Status = StorPortAllocateContiguousMemorySpecifyCacheNode(
                                    DeviceExtension,
                                    sizeof(VRING_DESC_ALIAS) * (srbExt->allocated),
                                    Low, High, Alighn,
                                    MmCached,
                                    MM_ANY_NODE_OK,
                                    (PVOID*)&srbExt->pdesc);
            if (!NT_SUCCESS(Status)) {
                BtpwDbgPrint(TRACE_LEVEL_FATAL, " FAILED to allocate contiguous memory with status 0x%x\n", Status);
                StorPortFreePool(DeviceExtension, srbExt->psgl);
                srbExt->allocated = 0;
                srbExt->psgl = srbExt->pio_sg;
                return FALSE;
            }
        }
#endif

        if((SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) == SRB_FLAGS_DATA_OUT) {
            for (i = 0; i < sgMaxElements; i++, sgElement++) {
                srbExt->psgl[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->psgl[sgElement].length = sgList->List[i].Length;
                srbExt->Xfer += sgList->List[i].Length;
            }
        }
    }
    srbExt->out = sgElement;
    srbExt->psgl[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->resp.cmd, &fragLen);
    srbExt->psgl[sgElement].length = sizeof(cmd->resp.cmd);
    sgElement++;
    if (sgList)
    {
        sgMaxElements = sgList->NumberOfElements;

        if((SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) != SRB_FLAGS_DATA_OUT) {
            for (i = 0; i < sgMaxElements; i++, sgElement++) {
                srbExt->psgl[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->psgl[sgElement].length = sgList->List[i].Length;
                srbExt->Xfer += sgList->List[i].Length;
            }
        }
    }
    srbExt->in = sgElement - srbExt->out;

EXIT_FN_SRB();
    return TRUE;
}


VOID
FORCEINLINE
DispatchQueue(
    IN PVOID DeviceExtension,
    IN ULONG MessageID
)
{
    PADAPTER_EXTENSION  adaptExt;
ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (!adaptExt->dump_mode && adaptExt->dpc_ok) {
        NT_ASSERT(MessageID >= QUEUE_TO_MESSAGE(PHYZIO_SCSI_REQUEST_QUEUE_0));
        StorPortIssueDpc(DeviceExtension,
            &adaptExt->dpc[MessageID - QUEUE_TO_MESSAGE(PHYZIO_SCSI_REQUEST_QUEUE_0)],
            ULongToPtr(MessageID),
            ULongToPtr(MessageID));
EXIT_FN();
        return;
    }
    ProcessQueue(DeviceExtension, MessageID, TRUE);
EXIT_FN();
}

VOID
ProcessQueue(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN BOOLEAN isr
)
{
    PPhyzIOSCSICmd      cmd;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    ULONG               index = MESSAGE_TO_QUEUE(MessageID) - PHYZIO_SCSI_REQUEST_QUEUE_0;
    STOR_LOCK_HANDLE    queueLock = { 0 };
    struct virtqueue    *vq;
    BOOLEAN             handleResponseInline;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    LIST_ENTRY          complete_list;
    PSRB_TYPE           Srb = NULL;
    PSRB_EXTENSION      srbExt = NULL;
ENTER_FN();
#ifdef USE_WORK_ITEM
    handleResponseInline = (adaptExt->num_queues == 1);
#else
    handleResponseInline = TRUE;
#endif
    vq = adaptExt->vq[PHYZIO_SCSI_REQUEST_QUEUE_0 + index];
    InitializeListHead(&complete_list);

    PioScsiVQLock(DeviceExtension, MessageID, &queueLock, isr);

    do {
        virtqueue_disable_cb(vq);
        while ((cmd = (PPhyzIOSCSICmd)virtqueue_get_buf(vq, &len)) != NULL) {
            if (handleResponseInline) {
                Srb = (PSRB_TYPE)(cmd->srb);
                srbExt = SRB_EXTENSION(Srb);
                InsertTailList(&complete_list, &srbExt->list_entry);
            }
#ifdef USE_WORK_ITEM
            else {
#if (NTDDI_VERSION > NTDDI_WIN7)
                PSRB_TYPE Srb = (PSRB_TYPE)(cmd->srb);
                PSRB_EXTENSION srbExt = SRB_EXTENSION(Srb);
                ULONG status = STOR_STATUS_SUCCESS;
                PSTOR_SLIST_ENTRY Result = NULL;
                PioScsiVQUnlock(DeviceExtension, MessageID, &queueLock, isr);
                srbExt->priv = (PVOID)cmd;
                status = StorPortInterlockedPushEntrySList(DeviceExtension, &adaptExt->srb_list[index], &srbExt->list_entry, &Result);
                if (status != STOR_STATUS_SUCCESS) {
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " StorPortInterlockedPushEntrySList failed with status 0x%x\n\n", status);
                }
                cnt++;
                PioScsiVQLock(DeviceExtension, MessageID, &queueLock, isr);
#else
                NT_ASSERT(0);
#endif
            }
#endif
        }
    } while (!virtqueue_enable_cb(vq));

    PioScsiVQUnlock(DeviceExtension, MessageID, &queueLock, isr);

    SendSRB(DeviceExtension, NULL, isr, MessageID);

    while (!IsListEmpty(&complete_list)) {
        srbExt = (PSRB_EXTENSION)RemoveHeadList(&complete_list);
        HandleResponse(DeviceExtension, &srbExt->cmd);
    }

#ifdef USE_WORK_ITEM
#if (NTDDI_VERSION > NTDDI_WIN7)
    if (cnt) {
       ULONG status = STOR_STATUS_SUCCESS;
       PVOID Worker = NULL;
       status = StorPortInitializeWorker(DeviceExtension, &Worker);
       if (status != STOR_STATUS_SUCCESS) {
          BtpwDbgPrint(TRACE_LEVEL_FATAL, " StorPortInitializeWorker failed with status 0x%x\n\n", status);
//FIXME   PioScsiWorkItemCallback
          return;
       }
       status = StorPortQueueWorkItem(DeviceExtension, &PioScsiWorkItemCallback, Worker, ULongToPtr(MessageID));
       if (status != STOR_STATUS_SUCCESS) {
          BtpwDbgPrint(TRACE_LEVEL_FATAL, " StorPortQueueWorkItem failed with status 0x%x\n\n", status);
//FIXME   PioScsiWorkItemCallback
       }
    }
#endif
#endif
EXIT_FN();
}

VOID
PioScsiCompleteDpcRoutine(
    IN PSTOR_DPC  Dpc,
    IN PVOID Context,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )
{
    ULONG MessageId;

ENTER_FN();
    MessageId = PtrToUlong(SystemArgument1);
    ProcessQueue(Context, MessageId, FALSE);
EXIT_FN();
}

BOOLEAN
FORCEINLINE
PreProcessRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PADAPTER_EXTENSION adaptExt;

ENTER_FN_SRB();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (SRB_FUNCTION(Srb)) {
        case SRB_FUNCTION_PNP:
        case SRB_FUNCTION_POWER:
        case SRB_FUNCTION_RESET_BUS:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
            return TRUE;
        }
        case SRB_FUNCTION_WMI:
            PioScsiWmiSrb(DeviceExtension, Srb);
            return TRUE;
        case SRB_FUNCTION_IO_CONTROL:
            PioScsiIoControl(DeviceExtension, Srb);
            return TRUE;
    }
EXIT_FN_SRB();
    return FALSE;
}

VOID
PostProcessRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PCDB                  cdb = NULL;
    PADAPTER_EXTENSION    adaptExt = NULL;
#if (NTDDI_VERSION > NTDDI_WIN7)
    PSRB_EXTENSION        srbExt = NULL;
#endif
ENTER_FN_SRB();
    if (SRB_FUNCTION(Srb) != SRB_FUNCTION_EXECUTE_SCSI) {
        return;
    }
    cdb      = SRB_CDB(Srb);
    if (!cdb)
        return;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (cdb->CDB6GENERIC.OperationCode)
    {
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_READ_CAPACITY16:
           break;
        case SCSIOP_INQUIRY:
           PioScsiSaveInquiryData(DeviceExtension, Srb);
           PioScsiPatchInquiryData(DeviceExtension, Srb);
           if (!StorPortSetDeviceQueueDepth( DeviceExtension, SRB_PATH_ID(Srb),
                                     SRB_TARGET_ID(Srb), SRB_LUN(Srb),
                                     adaptExt->queue_depth)) {
              BtpwDbgPrint(TRACE_LEVEL_ERROR, " StorPortSetDeviceQueueDepth(%p, %x) failed.\n",
                          DeviceExtension,
                          adaptExt->queue_depth);
           }
           break;
        default:
           break;

    }
#if (NTDDI_VERSION > NTDDI_WIN7)
    srbExt = SRB_EXTENSION(Srb);
    if (srbExt && (srbExt->allocated > 0)) {
        StorPortFreePool(DeviceExtension, srbExt->psgl);
        StorPortFreeContiguousMemorySpecifyCache(DeviceExtension, srbExt->pdesc, sizeof(VRING_DESC_ALIAS) * (srbExt->allocated), MmCached);
        srbExt->allocated = 0;
        srbExt->psgl = srbExt->pio_sg;
        srbExt->pdesc = srbExt->desc_alias;
    }
#endif
EXIT_FN_SRB();
}

VOID
CompleteRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
ENTER_FN_SRB();
    PostProcessRequest(DeviceExtension, Srb);
    StorPortNotification(RequestComplete,
                         DeviceExtension,
                         Srb);
EXIT_FN_SRB();
}

VOID
LogError(
    IN PVOID DeviceExtension,
    IN ULONG ErrorCode,
    IN ULONG UniqueId
    )
{
#if (NTDDI_VERSION > NTDDI_WIN7)
    STOR_LOG_EVENT_DETAILS logEvent;
    ULONG sz = 0;
    RtlZeroMemory( &logEvent, sizeof(logEvent) );
    logEvent.InterfaceRevision         = STOR_CURRENT_LOG_INTERFACE_REVISION;
    logEvent.Size                      = sizeof(logEvent);
    logEvent.EventAssociation          = StorEventAdapterAssociation;
    logEvent.StorportSpecificErrorCode = TRUE;
    logEvent.ErrorCode                 = ErrorCode;
    logEvent.DumpDataSize              = sizeof(UniqueId);
    logEvent.DumpData                  = &UniqueId;
    StorPortLogSystemEvent( DeviceExtension, &logEvent, &sz );
#else
    StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         ErrorCode,
                         UniqueId);
#endif
}

VOID
TransportReset(
    IN PVOID DeviceExtension,
    IN PPhyzIOSCSIEvent evt
    )
{
    UCHAR TargetId = evt->lun[1];
    UCHAR Lun = (evt->lun[2] << 8) | evt->lun[3];
ENTER_FN();

    switch (evt->reason)
    {
        case PHYZIO_SCSI_EVT_RESET_RESCAN:
           StorPortNotification( BusChangeDetected, DeviceExtension, 0);
           break;
        case PHYZIO_SCSI_EVT_RESET_REMOVED:
           StorPortNotification( BusChangeDetected, DeviceExtension, 0);
           break;
        default:
           BtpwDbgPrint(TRACE_LEVEL_VERBOSE, " <--> Unsupport phyzio scsi event reason 0x%x\n", evt->reason);
    }
EXIT_FN();
}

VOID
ParamChange(
    IN PVOID DeviceExtension,
    IN PPhyzIOSCSIEvent evt
    )
{
    UCHAR TargetId = evt->lun[1];
    UCHAR Lun = (evt->lun[2] << 8) | evt->lun[3];
    UCHAR AdditionalSenseCode = (UCHAR)(evt->reason & 255);
    UCHAR AdditionalSenseCodeQualifier = (UCHAR)(evt->reason >> 8);
ENTER_FN();

    if (AdditionalSenseCode == SCSI_ADSENSE_PARAMETERS_CHANGED &&
       (AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_PARAMETERS_CHANGED ||
        AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_MODE_PARAMETERS_CHANGED ||
        AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_CAPACITY_DATA_HAS_CHANGED))
    {
        StorPortNotification( BusChangeDetected, DeviceExtension, 0);
    }
EXIT_FN();
}

VOID
PioScsiWmiInitialize(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION    adaptExt;
    PSCSI_WMILIB_CONTEXT WmiLibContext;
ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    WmiLibContext = (PSCSI_WMILIB_CONTEXT)(&(adaptExt->WmiLibContext));

    WmiLibContext->GuidList = PioScsiGuidList;
    WmiLibContext->GuidCount = PioScsiGuidCount;
    WmiLibContext->QueryWmiRegInfo = PioScsiQueryWmiRegInfo;
    WmiLibContext->QueryWmiDataBlock = PioScsiQueryWmiDataBlock;
    WmiLibContext->SetWmiDataItem = NULL;
    WmiLibContext->SetWmiDataBlock = NULL;
    WmiLibContext->ExecuteWmiMethod = PioScsiExecuteWmiMethod;
    WmiLibContext->WmiFunctionControl = NULL;
EXIT_FN();
}

VOID
PioScsiWmiSrb(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{
    UCHAR status;
    SCSIWMI_REQUEST_CONTEXT requestContext = {0};
    ULONG retSize;
    PADAPTER_EXTENSION    adaptExt;
    PSRB_WMI_DATA pSrbWmi = SRB_WMI_DATA(Srb);

ENTER_FN_SRB();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    ASSERT(SRB_FUNCTION(Srb) == SRB_FUNCTION_WMI);
    ASSERT(SRB_LENGTH(Srb)  == sizeof(SCSI_WMI_REQUEST_BLOCK));
    ASSERT(SRB_DATA_TRANSFER_LENGTH(Srb) >= sizeof(ULONG));
    ASSERT(SRB_DATA_BUFFER(Srb));

    if (!pSrbWmi)
        return;
    if (!(pSrbWmi->WMIFlags & SRB_WMI_FLAGS_ADAPTER_REQUEST))
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, 0);
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
    }
    else
    {
        requestContext.UserContext = Srb;
        (VOID)ScsiPortWmiDispatchFunction(&adaptExt->WmiLibContext,
                                                pSrbWmi->WMISubFunction,
                                                DeviceExtension,
                                                &requestContext,
                                                pSrbWmi->DataPath,
                                                SRB_DATA_TRANSFER_LENGTH(Srb),
                                                SRB_DATA_BUFFER(Srb));

        retSize =  ScsiPortWmiGetReturnSize(&requestContext);
        status =  ScsiPortWmiGetReturnStatus(&requestContext);

        SRB_SET_DATA_TRANSFER_LENGTH(Srb, retSize);
        SRB_SET_SRB_STATUS(Srb, status);
    }

EXIT_FN_SRB();
}

VOID
PioScsiIoControl(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{
    PSRB_IO_CONTROL srbControl;
    PVOID           srbDataBuffer = SRB_DATA_BUFFER(Srb);
    PADAPTER_EXTENSION    adaptExt;

ENTER_FN_SRB();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    srbControl = (PSRB_IO_CONTROL)srbDataBuffer;

    switch (srbControl->ControlCode) {
        case IOCTL_SCSI_MINIPORT_NOT_QUORUM_CAPABLE:
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
            BtpwDbgPrint(TRACE_LEVEL_FATAL, " <--> Signature = %02x %02x %02x %02x %02x %02x %02x %02x\n",
                srbControl->Signature[0], srbControl->Signature[1], srbControl->Signature[2], srbControl->Signature[3],
                srbControl->Signature[4], srbControl->Signature[5], srbControl->Signature[6], srbControl->Signature[7]);
            BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " <--> IOCTL_SCSI_MINIPORT_NOT_QUORUM_CAPABLE\n");
            break;
        default:
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_INVALID_REQUEST);
            BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " <--> Unsupport control code 0x%x\n", srbControl->ControlCode);
            break;
    }
EXIT_FN_SRB();
}

UCHAR
ParseIdentificationDescr(
    IN PVOID  DeviceExtension,
    IN PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr,
    IN UCHAR PageLength
)
{
    PADAPTER_EXTENSION    adaptExt;
    UCHAR CodeSet = 0;
    UCHAR IdentifierType = 0;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ENTER_FN();
    if (IdentificationDescr) {
        CodeSet = IdentificationDescr->CodeSet;//(UCHAR)(((PCHAR)IdentificationDescr)[0]);
        IdentifierType = IdentificationDescr->IdentifierType;//(UCHAR)(((PCHAR)IdentificationDescr)[1]);
        switch (IdentifierType) {
        case PioscsiVpdIdentifierTypeVendorSpecific: {
            if (CodeSet == PioscsiVpdCodeSetAscii) {
                if (IdentificationDescr->IdentifierLength > 0 && adaptExt->ser_num == NULL) {
                    int ln = min(64, IdentificationDescr->IdentifierLength);
                    ULONG Status =
                        StorPortAllocatePool(DeviceExtension,
                            ln + 1,
                            PIOSCSI_POOL_TAG,
                            (PVOID*)&adaptExt->ser_num);
                    if (NT_SUCCESS(Status)) {
                        StorPortMoveMemory(adaptExt->ser_num, IdentificationDescr->Identifier, ln);
                        adaptExt->ser_num[ln] = '\0';
                        BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " serial number %s\n", adaptExt->ser_num);
                    }
                }
            }
        }
        break;
        case PioscsiVpdIdentifierTypeFCPHName: {
            if ((CodeSet == PioscsiVpdCodeSetBinary) && (IdentificationDescr->IdentifierLength == sizeof(ULONGLONG))) {
                REVERSE_BYTES_QUAD(&adaptExt->wwn, IdentificationDescr->Identifier);
                BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " wwn %llu\n", (ULONGLONG)adaptExt->wwn);
            }
        }
        break;
        case PioscsiVpdIdentifierTypeFCTargetPortPHName: {
            if ((CodeSet == PioscsiVpdCodeSetSASBinary) && (IdentificationDescr->IdentifierLength == sizeof(ULONGLONG))) {
                REVERSE_BYTES_QUAD(&adaptExt->port_wwn, IdentificationDescr->Identifier);
                BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " port wwn %llu\n", (ULONGLONG)adaptExt->port_wwn);
            }
        }
        break;
        case PioscsiVpdIdentifierTypeFCTargetPortRelativeTargetPort: {
            if ((CodeSet == PioscsiVpdCodeSetSASBinary) && (IdentificationDescr->IdentifierLength == sizeof(ULONG))) {
                REVERSE_BYTES(&adaptExt->port_idx, IdentificationDescr->Identifier);
                BtpwDbgPrint(TRACE_LEVEL_INFORMATION, " port index %lu\n", (ULONG)adaptExt->port_idx);
            }
        }
        break;
        default:
            BtpwDbgPrint(TRACE_LEVEL_ERROR, " Unsupported IdentifierType = %x!\n", IdentifierType);
            break;
        }
        return IdentificationDescr->IdentifierLength;
    }
    EXIT_FN();
    return 0;
}

VOID
PioScsiSaveInquiryData(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{
    PVOID           dataBuffer;
    PADAPTER_EXTENSION    adaptExt;
    PCDB cdb;
    ULONG dataLen;
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
ENTER_FN_SRB();

    if (!Srb)
        return;

    cdb  = SRB_CDB(Srb);

    if (!cdb)
        return;

    SRB_GET_SCSI_STATUS(Srb, SrbStatus);
    if (SrbStatus == SRB_STATUS_ERROR)
        return;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    dataBuffer = SRB_DATA_BUFFER(Srb);
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    if (cdb->CDB6INQUIRY3.EnableVitalProductData == 1) {
        switch (cdb->CDB6INQUIRY3.PageCode) {
            case VPD_SERIAL_NUMBER: {
                PVPD_SERIAL_NUMBER_PAGE SerialPage;
                SerialPage = (PVPD_SERIAL_NUMBER_PAGE)dataBuffer;
                BtpwDbgPrint(TRACE_LEVEL_FATAL, " VPD_SERIAL_NUMBER PageLength = %d\n", SerialPage->PageLength);
                if (SerialPage->PageLength > 0 && adaptExt->ser_num == NULL) {
                    int ln = min(64, SerialPage->PageLength);
                    ULONG Status =
                        StorPortAllocatePool(DeviceExtension,
                            ln + 1,
                            PIOSCSI_POOL_TAG,
                            (PVOID*)&adaptExt->ser_num);
                    if (NT_SUCCESS(Status)) {
                        StorPortMoveMemory(adaptExt->ser_num, SerialPage->SerialNumber, ln);
                        adaptExt->ser_num[ln] = '\0';
                        BtpwDbgPrint(TRACE_LEVEL_FATAL, " serial number %s\n", adaptExt->ser_num);
                    }
                }
            }
            break;
            case VPD_DEVICE_IDENTIFIERS: {
                PVPD_IDENTIFICATION_PAGE IdentificationPage;
                PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr;
                UCHAR PageLength = 0;
                IdentificationPage = (PVPD_IDENTIFICATION_PAGE)dataBuffer;
                PageLength = IdentificationPage->PageLength;
                if (PageLength >= sizeof(VPD_IDENTIFICATION_DESCRIPTOR)) {
                    UCHAR IdentifierLength = 0;
                    IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
                    do {
                        UCHAR offset = 0;
                        IdentifierLength = ParseIdentificationDescr(DeviceExtension, IdentificationDescr, PageLength);
                        offset = sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + IdentifierLength;
                        PageLength -= min(PageLength, offset);
                        IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)((ULONG_PTR)IdentificationDescr + offset);
                    } while (PageLength);
                }
            }
            break;
        }
    }
    else if (cdb->CDB6INQUIRY3.PageCode == VPD_SUPPORTED_PAGES) {
        PINQUIRYDATA InquiryData = (PINQUIRYDATA)dataBuffer;
        if (InquiryData && dataLen) {
            CopyBufferToAnsiString(adaptExt->ven_id, InquiryData->VendorId, ' ', sizeof(InquiryData->VendorId));
            CopyBufferToAnsiString(adaptExt->prod_id, InquiryData->ProductId, ' ', sizeof(InquiryData->ProductId));
            CopyBufferToAnsiString(adaptExt->rev_id, InquiryData->ProductRevisionLevel, ' ',sizeof(InquiryData->ProductRevisionLevel));
        }
    }
EXIT_FN_SRB();
}

VOID
PioScsiPatchInquiryData(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
)
{
    PVOID           dataBuffer;
    PADAPTER_EXTENSION    adaptExt;
    PCDB cdb;
    ULONG dataLen;
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    ENTER_FN_SRB();

    if (!Srb)
        return;

    cdb = SRB_CDB(Srb);

    if (!cdb)
        return;

    SRB_GET_SCSI_STATUS(Srb, SrbStatus);
    if (SrbStatus == SRB_STATUS_ERROR)
        return;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    dataBuffer = SRB_DATA_BUFFER(Srb);
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    if (cdb->CDB6INQUIRY3.EnableVitalProductData == 1) {
        switch (cdb->CDB6INQUIRY3.PageCode) {
            case VPD_DEVICE_IDENTIFIERS: {
                PVPD_IDENTIFICATION_PAGE IdentificationPage;
                PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr;
                UCHAR PageLength = 0;
                IdentificationPage = (PVPD_IDENTIFICATION_PAGE)dataBuffer;
                PageLength = IdentificationPage->PageLength;
                if (dataLen >= (sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + sizeof(VPD_IDENTIFICATION_PAGE) + 8) &&
                    PageLength <= sizeof(VPD_IDENTIFICATION_PAGE)) {
                    UCHAR IdentifierLength = 0;
                    IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
                    if (IdentificationDescr->IdentifierLength == 0)
                    {
                        IdentificationDescr->CodeSet = VpdCodeSetBinary;
                        IdentificationDescr->IdentifierType = VpdIdentifierTypeEUI64;
                        IdentificationDescr->IdentifierLength = 8;
                        IdentificationDescr->Identifier[0] = (adaptExt->system_io_bus_number >> 12) & 0xF;
                        IdentificationDescr->Identifier[1] = (adaptExt->system_io_bus_number >> 8) & 0xF;
                        IdentificationDescr->Identifier[2] = (adaptExt->system_io_bus_number >> 4) & 0xF;
                        IdentificationDescr->Identifier[3] = adaptExt->system_io_bus_number & 0xF;
                        IdentificationDescr->Identifier[4] = (adaptExt->slot_number >> 12) & 0xF;
                        IdentificationDescr->Identifier[5] = (adaptExt->slot_number >> 8) & 0xF;
                        IdentificationDescr->Identifier[6] = (adaptExt->slot_number >> 4) & 0xF;
                        IdentificationDescr->Identifier[7] = adaptExt->slot_number & 0xF;
                        IdentificationPage->PageLength = sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + IdentificationDescr->IdentifierLength;
                        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_IDENTIFICATION_PAGE) +
                            IdentificationPage->PageLength));
                    }
                }
            }
            break;
        }
    }
    EXIT_FN_SRB();
}

BOOLEAN
PioScsiQueryWmiDataBlock(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG OutBufferSize,
    OUT PUCHAR Buffer
    )
{
    ULONG size = 0;
    UCHAR status = SRB_STATUS_SUCCESS;
    PADAPTER_EXTENSION    adaptExt;

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)Context;

    UNREFERENCED_PARAMETER(InstanceIndex);

    switch (GuidIndex)
    {
        case PIOSCSI_SETUP_GUID_INDEX:
        {
            size = PioScsiExtendedInfo_SIZE;
            if (OutBufferSize < size)
            {
                status = SRB_STATUS_DATA_OVERRUN;
                break;
            }

            PioScsiReadExtendedData(Context,
                                     Buffer);
            *InstanceLengthArray = size;
            status = SRB_STATUS_SUCCESS;
        }
        break;
        case PIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX:
        {
            PMS_SM_AdapterInformationQuery pOutBfr = (PMS_SM_AdapterInformationQuery)Buffer;
            BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> PIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX\n");
            size = sizeof(MS_SM_AdapterInformationQuery);
            if (OutBufferSize < size)
            {
                status = SRB_STATUS_DATA_OVERRUN;
                break;
            }

            RtlZeroMemory(pOutBfr, size);
            pOutBfr->UniqueAdapterId = adaptExt->hba_id;
            pOutBfr->HBAStatus = HBA_STATUS_OK;
            pOutBfr->NumberOfPorts = 1;
            pOutBfr->VendorSpecificID = VENDORID | (PRODUCTID << 16);
            CopyUnicodeString(pOutBfr->Manufacturer, MANUFACTURER, sizeof(pOutBfr->Manufacturer));
            if (adaptExt->ser_num)
            {
                CopyAnsiToUnicodeString(pOutBfr->SerialNumber, adaptExt->ser_num, sizeof(pOutBfr->SerialNumber));
            }
            else
            {
                CopyUnicodeString(pOutBfr->SerialNumber, SERIALNUMBER, sizeof(pOutBfr->SerialNumber));
            }
            CopyUnicodeString(pOutBfr->Model, MODEL, sizeof(pOutBfr->Model));
            CopyUnicodeString(pOutBfr->ModelDescription, MODELDESCRIPTION, sizeof(pOutBfr->ModelDescription));
            CopyUnicodeString(pOutBfr->HardwareVersion, HARDWAREVERSION, sizeof(pOutBfr->ModelDescription));
            CopyUnicodeString(pOutBfr->DriverVersion, DRIVERVERSION, sizeof(pOutBfr->DriverVersion));
            CopyUnicodeString(pOutBfr->OptionROMVersion, OPTIONROMVERSION, sizeof(pOutBfr->OptionROMVersion));
            CopyAnsiToUnicodeString(pOutBfr->FirmwareVersion, adaptExt->rev_id, sizeof(pOutBfr->FirmwareVersion));
            CopyUnicodeString(pOutBfr->DriverName, DRIVERNAME, sizeof(pOutBfr->DriverName));
            CopyUnicodeString(pOutBfr->HBASymbolicName, HBASYMBOLICNAME, sizeof(pOutBfr->HBASymbolicName));
            CopyUnicodeString(pOutBfr->RedundantFirmwareVersion, REDUNDANTFIRMWAREVERSION, sizeof(pOutBfr->RedundantFirmwareVersion));
            CopyUnicodeString(pOutBfr->RedundantOptionROMVersion, REDUNDANTOPTIONROMVERSION, sizeof(pOutBfr->RedundantOptionROMVersion));
            CopyUnicodeString(pOutBfr->MfgDomain, MFRDOMAIN, sizeof(pOutBfr->MfgDomain));

            *InstanceLengthArray = size;
            status = SRB_STATUS_SUCCESS;
        }
        break;
        case PIOSCSI_MS_PORT_INFORM_GUID_INDEX:
        {
            size = sizeof(ULONG);
            if (OutBufferSize < size)
            {
                status = SRB_STATUS_DATA_OVERRUN;
                BtpwDbgPrint(TRACE_LEVEL_WARNING, " --> PIOSCSI_MS_PORT_INFORM_GUID_INDEX out buffer too small %d %d\n", OutBufferSize, size);
                break;
            }
            *InstanceLengthArray = size;
            status = SRB_STATUS_SUCCESS;
        }
        break;
        default:
        {
            status = SRB_STATUS_ERROR;
        }
    }

    ScsiPortWmiPostProcess(RequestContext,
                           status,
                           size);

EXIT_FN();
    return TRUE;
}

UCHAR
PioScsiExecuteWmiMethod(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG MethodId,
    IN ULONG InBufferSize,
    IN ULONG OutBufferSize,
    IN OUT PUCHAR Buffer
    )
{
    PADAPTER_EXTENSION      adaptExt = (PADAPTER_EXTENSION)Context;
    ULONG                   size = 0;
    UCHAR                   status = SRB_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(InstanceIndex);

    ENTER_FN();
    switch (GuidIndex)
    {
        case PIOSCSI_SETUP_GUID_INDEX:
        {
            BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> PIOSCSI_SETUP_GUID_INDEX ERROR\n");
        }
        break;
        case PIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX:
        {
            PMS_SM_AdapterInformationQuery pOutBfr = (PMS_SM_AdapterInformationQuery)Buffer;
            pOutBfr;
            BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> PIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX ERROR\n");
        }
        break;
        case PIOSCSI_MS_PORT_INFORM_GUID_INDEX:
        {
            switch (MethodId)
            {
                case SM_GetPortType:
                {
                    PSM_GetPortType_IN  pInBfr = (PSM_GetPortType_IN)Buffer;
                    PSM_GetPortType_OUT pOutBfr = (PSM_GetPortType_OUT)Buffer;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetPortType\n");
                    size = SM_GetPortType_OUT_SIZE;
                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }
                    if (InBufferSize < SM_GetPortType_IN_SIZE)
                    {
                        status = SRB_STATUS_ERROR;
                        break;
                    }
                    pOutBfr->HBAStatus = HBA_STATUS_OK;
                    pOutBfr->PortType = HBA_PORTTYPE_SASDEVICE;
                }
                break;
                case SM_GetAdapterPortAttributes:
                {
                    PSM_GetAdapterPortAttributes_IN  pInBfr = (PSM_GetAdapterPortAttributes_IN)Buffer;
                    PSM_GetAdapterPortAttributes_OUT pOutBfr = (PSM_GetAdapterPortAttributes_OUT)Buffer;
                    PMS_SMHBA_FC_Port pPortSpecificAttributes = NULL;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetAdapterPortAttributes\n");
                    size = FIELD_OFFSET(SM_GetAdapterPortAttributes_OUT, PortAttributes) + FIELD_OFFSET(MS_SMHBA_PORTATTRIBUTES, PortSpecificAttributes) + sizeof(MS_SMHBA_FC_Port);
                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }
                    if (InBufferSize < SM_GetAdapterPortAttributes_IN_SIZE)
                    {
                        status = SRB_STATUS_ERROR;
                        break;
                    }
                    pOutBfr->HBAStatus = HBA_STATUS_OK;
                    CopyUnicodeString(pOutBfr->PortAttributes.OSDeviceName, MODEL, sizeof(pOutBfr->PortAttributes.OSDeviceName));
                    pOutBfr->PortAttributes.PortState = HBA_PORTSTATE_ONLINE;
                    pOutBfr->PortAttributes.PortType = HBA_PORTTYPE_SASDEVICE;
                    pOutBfr->PortAttributes.PortSpecificAttributesSize = sizeof(MS_SMHBA_FC_Port);
                    pPortSpecificAttributes = (PMS_SMHBA_FC_Port) pOutBfr->PortAttributes.PortSpecificAttributes;
                    RtlZeroMemory(pPortSpecificAttributes, sizeof(MS_SMHBA_FC_Port));
                    RtlMoveMemory(pPortSpecificAttributes->NodeWWN, &adaptExt->wwn, sizeof(pPortSpecificAttributes->NodeWWN));
                    RtlMoveMemory(pPortSpecificAttributes->PortWWN, &adaptExt->port_wwn, sizeof(pPortSpecificAttributes->PortWWN));
                    pPortSpecificAttributes->FcId = 0;
                    pPortSpecificAttributes->PortSupportedClassofService = 0;
//FIXME report PortSupportedFc4Types PortActiveFc4Types FabricName;
                    pPortSpecificAttributes->NumberofDiscoveredPorts = 1;
                    pPortSpecificAttributes->NumberofPhys = 1;
                    CopyUnicodeString(pPortSpecificAttributes->PortSymbolicName, PORTSYMBOLICNAME, sizeof(pPortSpecificAttributes->PortSymbolicName));
                }
                break;
                case SM_GetDiscoveredPortAttributes:
                {
                    PSM_GetDiscoveredPortAttributes_IN  pInBfr = (PSM_GetDiscoveredPortAttributes_IN)Buffer;
                    PSM_GetDiscoveredPortAttributes_OUT pOutBfr = (PSM_GetDiscoveredPortAttributes_OUT)Buffer;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetDiscoveredPortAttributes\n");
                    size = SM_GetDiscoveredPortAttributes_OUT_SIZE;
                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }
                    if (InBufferSize < SM_GetDiscoveredPortAttributes_IN_SIZE)
                    {
                        status = SRB_STATUS_ERROR;
                        break;
                    }
                    pOutBfr->HBAStatus = HBA_STATUS_OK;
                    CopyUnicodeString(pOutBfr->PortAttributes.OSDeviceName, MODEL, sizeof(pOutBfr->PortAttributes.OSDeviceName));
                    pOutBfr->PortAttributes.PortState = HBA_PORTSTATE_ONLINE;
                    pOutBfr->PortAttributes.PortType = HBA_PORTTYPE_SASDEVICE;
                }
                break;
                case SM_GetPortAttributesByWWN:
                {
                    PSM_GetPortAttributesByWWN_IN  pInBfr = (PSM_GetPortAttributesByWWN_IN)Buffer;
                    PSM_GetPortAttributesByWWN_OUT pOutBfr = (PSM_GetPortAttributesByWWN_OUT)Buffer;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetPortAttributesByWWN\n");
                    size = SM_GetPortAttributesByWWN_OUT_SIZE;
                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }
                    if (InBufferSize < SM_GetPortAttributesByWWN_IN_SIZE)
                    {
                        status = SRB_STATUS_ERROR;
                        break;
                    }
                    pOutBfr->HBAStatus = HBA_STATUS_OK;
                    CopyUnicodeString(pOutBfr->PortAttributes.OSDeviceName, MODEL, sizeof(pOutBfr->PortAttributes.OSDeviceName));
                    pOutBfr->PortAttributes.PortState = HBA_PORTSTATE_ONLINE;
                    pOutBfr->PortAttributes.PortType = HBA_PORTTYPE_SASDEVICE;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetPortAttributesByWWN Not Implemented Yet\n");
                }
                break;
                case SM_GetProtocolStatistics:
                {
                    PSM_GetProtocolStatistics_IN  pInBfr = (PSM_GetProtocolStatistics_IN)Buffer;
                    PSM_GetProtocolStatistics_OUT pOutBfr = (PSM_GetProtocolStatistics_OUT)Buffer;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetProtocolStatistics\n");
                    size = SM_GetProtocolStatistics_OUT_SIZE;
                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }
                    if (InBufferSize < SM_GetProtocolStatistics_IN_SIZE)
                    {
                        status = SRB_STATUS_ERROR;
                        break;
                    }
                }
                break;
                case SM_GetPhyStatistics:
                {
                    PSM_GetPhyStatistics_IN  pInBfr = (PSM_GetPhyStatistics_IN)Buffer;
                    PSM_GetPhyStatistics_OUT pOutBfr = (PSM_GetPhyStatistics_OUT)Buffer;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetPhyStatistics\n");
                    size = FIELD_OFFSET(SM_GetPhyStatistics_OUT, PhyCounter) + sizeof(LONGLONG);
                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }
                    if (InBufferSize < SM_GetPhyStatistics_IN_SIZE)
                    {
                        status = SRB_STATUS_ERROR;
                        break;
                    }
                }
                break;
                case SM_GetFCPhyAttributes:
                {
                    PSM_GetFCPhyAttributes_IN  pInBfr = (PSM_GetFCPhyAttributes_IN)Buffer;
                    PSM_GetFCPhyAttributes_OUT pOutBfr = (PSM_GetFCPhyAttributes_OUT)Buffer;

                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetFCPhyAttributes\n");
                    size = SM_GetFCPhyAttributes_OUT_SIZE;

                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetFCPhyAttributes_IN_SIZE)
                    {
                        status = SRB_STATUS_ERROR;
                        break;
                    }
                }
                break;
                case SM_GetSASPhyAttributes:
                {
                    PSM_GetSASPhyAttributes_IN  pInBfr = (PSM_GetSASPhyAttributes_IN)Buffer;
                    PSM_GetSASPhyAttributes_OUT pOutBfr = (PSM_GetSASPhyAttributes_OUT)Buffer;
                    BtpwDbgPrint(TRACE_LEVEL_FATAL, " --> SM_GetSASPhyAttributes\n");
                    size = SM_GetSASPhyAttributes_OUT_SIZE;
                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }
                    if (InBufferSize < SM_GetSASPhyAttributes_IN_SIZE)
                    {
                        status = SRB_STATUS_ERROR;
                        break;
                    }
                }
                break;
                case SM_RefreshInformation:
                {
                }
                break;
                default:
                    status = SRB_STATUS_INVALID_REQUEST;
                    BtpwDbgPrint(TRACE_LEVEL_ERROR, " --> ERROR Unknown MethodId = %lu\n", MethodId);
                    break;
            }
        }
        break;
        default:
            status = SRB_STATUS_INVALID_REQUEST;
            BtpwDbgPrint(TRACE_LEVEL_ERROR, " --> PioScsiExecuteWmiMethod Unsupported GuidIndex = %lu\n", GuidIndex);
        break;
    }
    ScsiPortWmiPostProcess(RequestContext,
        status,
        size);

    EXIT_FN();
    return SRB_STATUS_SUCCESS;

}

UCHAR
PioScsiQueryWmiRegInfo(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    OUT PWCHAR *MofResourceName
    )
{
ENTER_FN();
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(RequestContext);

    *MofResourceName = PioScsiWmi_MofResourceName;
    return SRB_STATUS_SUCCESS;
}

VOID
PioScsiReadExtendedData(
IN PVOID Context,
OUT PUCHAR Buffer
)
{
    UCHAR numberOfBytes = sizeof(PioScsiExtendedInfo) - 1;
    PADAPTER_EXTENSION    adaptExt;
    PPioScsiExtendedInfo  extInfo;

ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)Context;
    extInfo = (PPioScsiExtendedInfo)Buffer;

    RtlZeroMemory(Buffer, numberOfBytes);

    extInfo->QueueDepth = (ULONG)adaptExt->queue_depth;
    extInfo->QueuesCount = (UCHAR)adaptExt->num_queues;
    extInfo->Indirect = CHECKBIT(adaptExt->features, PHYZIO_RING_F_INDIRECT_DESC);
    extInfo->EventIndex = CHECKBIT(adaptExt->features, PHYZIO_RING_F_EVENT_IDX);
    extInfo->RingPacked = CHECKBIT(adaptExt->features, PHYZIO_F_RING_PACKED);
    extInfo->DpcRedirection = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_DPC_REDIRECTION);
    extInfo->ConcurrentChannels = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_CONCURRENT_CHANNELS);
    extInfo->InterruptMsgRanges = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_INTERRUPT_MESSAGE_RANGES);
    extInfo->CompletionDuringStartIo = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO);
    extInfo->PhysicalBreaks = adaptExt->max_physical_breaks;

EXIT_FN();
}

#ifdef USE_WORK_ITEM
#if (NTDDI_VERSION > NTDDI_WIN7)
VOID
PioScsiWorkItemCallback(
    _In_ PVOID DeviceExtension,
    _In_opt_ PVOID Context,
    _In_ PVOID Worker
    )
{
    ULONG MessageId = PtrToUlong(Context);
    ULONG status = STOR_STATUS_SUCCESS;
    ULONG index = MESSAGE_TO_QUEUE(MessageId) - PHYZIO_SCSI_REQUEST_QUEUE_0;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSTOR_SLIST_ENTRY   listEntryRev, listEntry;
ENTER_FN();
    status = StorPortInterlockedFlushSList(DeviceExtension, &adaptExt->srb_list[index], &listEntryRev);
    if ((status == STOR_STATUS_SUCCESS) && (listEntryRev != NULL)) {
        KAFFINITY old_affinity, new_affinity;
        old_affinity = new_affinity = 0;
#if 1
        listEntry = listEntryRev;
#else
        listEntry = NULL;
        while (listEntryRev != NULL) {
            next = listEntryRev->Next;
            listEntryRev->Next = listEntry;
            listEntry = listEntryRev;
            listEntryRev = next;
        }
#endif
        while(listEntry)
        {
            PPhyzIOSCSICmd  cmd = NULL;
            PSRB_TYPE Srb = NULL;
            PSRB_EXTENSION srbExt = NULL;
            PSTOR_SLIST_ENTRY next = listEntry->Next;
            srbExt = CONTAINING_RECORD(listEntry,
                        SRB_EXTENSION, list_entry);

            ASSERT(srExt);
            Srb = (PSRB_TYPE)(srbExt->Srb);
            cmd = (PPhyzIOSCSICmd)srbExt->priv;
            ASSERT(cmd);
            if (new_affinity == 0) {
                new_affinity = ((KAFFINITY)1) << srbExt->cpu;
                old_affinity = KeSetSystemAffinityThreadEx(new_affinity);
            }
            HandleResponse(DeviceExtension, cmd);
            listEntry = next;
        }
        if (new_affinity != 0) {
            KeRevertToUserAffinityThreadEx(old_affinity);
        }
    }
    else if (status != STOR_STATUS_SUCCESS) {
       BtpwDbgPrint(TRACE_LEVEL_FATAL, " StorPortInterlockedPushEntrySList failed with status 0x%x\n\n", status);
    }

    status = StorPortFreeWorker(DeviceExtension, Worker);
    if (status != STOR_STATUS_SUCCESS) {
       BtpwDbgPrint(TRACE_LEVEL_FATAL, " StorPortFreeWorker failed with status 0x%x\n\n", status);
    }
EXIT_FN();
}
#endif
#endif
