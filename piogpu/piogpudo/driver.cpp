/*
 * Copyright (C) 2019-2020 Blu Tah, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@blutah.com>
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

#include "driver.h"
#include "piogpudo.h"
#include "helper.h"
#include "baseobj.h"

#if !DBG
#include "driver.tmh"
#endif

#pragma code_seg(push)
#pragma code_seg("INIT")

int nDebugLevel;
int phyzioDebugLevel;
int bDebugPrint;
int bBreakAlways;

tDebugPrintFunc PhyzioDebugPrintProc;

#ifdef DBG
void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, IN PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    bDebugPrint = 0;
    phyzioDebugLevel = 0;
    nDebugLevel = TRACE_LEVEL_NONE;
    bBreakAlways = 0;

    bDebugPrint = 1;
    phyzioDebugLevel = 0x5;
    bBreakAlways = 1;
    nDebugLevel = TRACE_LEVEL_FATAL;
#if defined(COM_DEBUG)
    PhyzioDebugPrintProc = DebugPrintFuncSerial;
#elif defined(PRINT_DEBUG)
    PhyzioDebugPrintProc = DebugPrintFuncKdPrint;
#endif
}
#endif


extern "C"
NTSTATUS
DriverEntry(
    _In_  DRIVER_OBJECT*  pDriverObject,
    _In_  UNICODE_STRING* pRegistryPath)
{
    PAGED_CODE();
    WPP_INIT_TRACING(pDriverObject, pRegistryPath);

    //    PioGpuDbgBreak();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> KMDOD build on on %s %s\n", __DATE__, __TIME__));

    KMDDOD_INITIALIZATION_DATA InitialData = { 0 };

    InitialData.Version = DXGKDDI_INTERFACE_VERSION_WIN8;//DXGKDDI_INTERFACE_VERSION;

    InitialData.DxgkDdiAddDevice = PioGpuDodAddDevice;
    InitialData.DxgkDdiStartDevice = PioGpuDodStartDevice;
    InitialData.DxgkDdiStopDevice = PioGpuDodStopDevice;
    InitialData.DxgkDdiResetDevice = PioGpuDodResetDevice;
    InitialData.DxgkDdiRemoveDevice = PioGpuDodRemoveDevice;
    InitialData.DxgkDdiDispatchIoRequest = PioGpuDodDispatchIoRequest;
    InitialData.DxgkDdiInterruptRoutine = PioGpuDodInterruptRoutine;
    InitialData.DxgkDdiDpcRoutine = PioGpuDodDpcRoutine;
    InitialData.DxgkDdiQueryChildRelations = PioGpuDodQueryChildRelations;
    InitialData.DxgkDdiQueryChildStatus = PioGpuDodQueryChildStatus;
    InitialData.DxgkDdiQueryDeviceDescriptor = PioGpuDodQueryDeviceDescriptor;
    InitialData.DxgkDdiSetPowerState = PioGpuDodSetPowerState;
    InitialData.DxgkDdiUnload = PioGpuDodUnload;
    InitialData.DxgkDdiQueryAdapterInfo = PioGpuDodQueryAdapterInfo;
    InitialData.DxgkDdiSetPointerPosition = PioGpuDodSetPointerPosition;
    InitialData.DxgkDdiSetPointerShape = PioGpuDodSetPointerShape;
    InitialData.DxgkDdiIsSupportedVidPn = PioGpuDodIsSupportedVidPn;
    InitialData.DxgkDdiRecommendFunctionalVidPn = PioGpuDodRecommendFunctionalVidPn;
    InitialData.DxgkDdiEnumVidPnCofuncModality = PioGpuDodEnumVidPnCofuncModality;
    InitialData.DxgkDdiSetVidPnSourceVisibility = PioGpuDodSetVidPnSourceVisibility;
    InitialData.DxgkDdiCommitVidPn = PioGpuDodCommitVidPn;
    InitialData.DxgkDdiUpdateActiveVidPnPresentPath = PioGpuDodUpdateActiveVidPnPresentPath;
    InitialData.DxgkDdiRecommendMonitorModes = PioGpuDodRecommendMonitorModes;
    InitialData.DxgkDdiQueryVidPnHWCapability = PioGpuDodQueryVidPnHWCapability;
    InitialData.DxgkDdiPresentDisplayOnly = PioGpuDodPresentDisplayOnly;
    InitialData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = PioGpuDodStopDeviceAndReleasePostDisplayOwnership;
    InitialData.DxgkDdiSystemDisplayEnable = PioGpuDodSystemDisplayEnable;
    InitialData.DxgkDdiSystemDisplayWrite = PioGpuDodSystemDisplayWrite;

    NTSTATUS Status = DxgkInitializeDisplayOnlyDriver(pDriverObject, pRegistryPath, &InitialData);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkInitializeDisplayOnlyDriver failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}
// END: Init Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg("PAGE")

//
// PnP DDIs
//

VOID
PioGpuDodUnload(VOID)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> %s\n", __FUNCTION__));
    WPP_CLEANUP(NULL);
}

NTSTATUS
PioGpuDodAddDevice(
    _In_ DEVICE_OBJECT* pPhysicalDeviceObject,
    _Outptr_ PVOID*  ppDeviceContext)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if ((pPhysicalDeviceObject == NULL) ||
        (ppDeviceContext == NULL))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("One of pPhysicalDeviceObject (%p), ppDeviceContext (%p) is NULL",
            pPhysicalDeviceObject, ppDeviceContext));
        return STATUS_INVALID_PARAMETER;
    }
    *ppDeviceContext = NULL;

    PioGpuDod* pPioGpuDod = new(NonPagedPoolNx) PioGpuDod(pPhysicalDeviceObject);
    if (pPioGpuDod == NULL)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPioGpuDod failed to be allocated"));
        return STATUS_NO_MEMORY;
    }

    *ppDeviceContext = pPioGpuDod;

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s ppDeviceContext = %p\n", __FUNCTION__, pPioGpuDod));
    return STATUS_SUCCESS;
}

NTSTATUS
PioGpuDodRemoveDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, pDeviceContext));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);

    if (pPioGpuDod)
    {
        delete pPioGpuDod;
    }

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS
PioGpuDodStartDevice(
    _In_  VOID*              pDeviceContext,
    _In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    return pPioGpuDod->StartDevice(pDxgkStartInfo, pDxgkInterface, pNumberOfViews, pNumberOfChildren);
}

NTSTATUS
PioGpuDodStopDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    return pPioGpuDod->StopDevice();
}


NTSTATUS
PioGpuDodDispatchIoRequest(
    _In_  VOID*                 pDeviceContext,
    _In_  ULONG                 VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PioGpuDod (0x%I64x) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->DispatchIoRequest(VidPnSourceId, pVideoRequestPacket);
}

NTSTATUS
PioGpuDodSetPowerState(
    _In_  VOID*              pDeviceContext,
    _In_  ULONG              HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    if (!pPioGpuDod->IsDriverActive())
    {
        return STATUS_SUCCESS;
    }
    return pPioGpuDod->SetPowerState(HardwareUid, DevicePowerState, ActionType);
}

NTSTATUS
PioGpuDodQueryChildRelations(
    _In_  VOID*              pDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_  ULONG              ChildRelationsSize)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    return pPioGpuDod->QueryChildRelations(pChildRelations, ChildRelationsSize);
}

NTSTATUS
PioGpuDodQueryChildStatus(
    _In_    VOID*            pDeviceContext,
    _Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN          NonDestructiveOnly)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    return pPioGpuDod->QueryChildStatus(pChildStatus, NonDestructiveOnly);
}

NTSTATUS
PioGpuDodQueryDeviceDescriptor(
    _In_  VOID*                     pDeviceContext,
    _In_  ULONG                     ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    if (!pPioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("PIOGPU (%p) is being called when not active!", pPioGpuDod));
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->QueryDeviceDescriptor(ChildUid, pDeviceDescriptor);
}


NTSTATUS
APIENTRY
PioGpuDodQueryAdapterInfo(
    _In_ CONST HANDLE                    hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    return pPioGpuDod->QueryAdapterInfo(pQueryAdapterInfo);
}

NTSTATUS
APIENTRY
PioGpuDodSetPointerPosition(
    _In_ CONST HANDLE                      hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("PioGpu (%p) is being called when not active!", pPioGpuDod));
        PioGpuDbgBreak();
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->SetPointerPosition(pSetPointerPosition);
}

NTSTATUS
APIENTRY
PioGpuDodSetPointerShape(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s PioGpu (%p) is being called when not active!\n", __FUNCTION__, pPioGpuDod));
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->SetPointerShape(pSetPointerShape);
}

NTSTATUS
APIENTRY
PioGpuDodPresentDisplayOnly(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->PresentDisplayOnly(pPresentDisplayOnly);
}

NTSTATUS
APIENTRY
PioGpuDodStopDeviceAndReleasePostDisplayOwnership(
    _In_  VOID*                          pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    if (pPioGpuDod)
    {
        status = pPioGpuDod->StopDeviceAndReleasePostDisplayOwnership(TargetId, DisplayInfo);
    }
    return status;
}

NTSTATUS
APIENTRY
PioGpuDodIsSupportedVidPn(
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("PIOGPU (%p) is being called when not active!", pPioGpuDod));
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->IsSupportedVidPn(pIsSupportedVidPn);
}

NTSTATUS
APIENTRY
PioGpuDodRecommendFunctionalVidPn(
    _In_ CONST HANDLE                                  hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->RecommendFunctionalVidPn(pRecommendFunctionalVidPn);
}

NTSTATUS
APIENTRY
PioGpuDodRecommendVidPnTopology(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->RecommendVidPnTopology(pRecommendVidPnTopology);
}

NTSTATUS
APIENTRY
PioGpuDodRecommendMonitorModes(
    _In_ CONST HANDLE                                hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->RecommendMonitorModes(pRecommendMonitorModes);
}

NTSTATUS
APIENTRY
PioGpuDodEnumVidPnCofuncModality(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->EnumVidPnCofuncModality(pEnumCofuncModality);
}

NTSTATUS
APIENTRY
PioGpuDodSetVidPnSourceVisibility(
    _In_ CONST HANDLE                            hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->SetVidPnSourceVisibility(pSetVidPnSourceVisibility);
}

NTSTATUS
APIENTRY
PioGpuDodCommitVidPn(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->CommitVidPn(pCommitVidPn);
}

NTSTATUS
APIENTRY
PioGpuDodUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                                      hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->UpdateActiveVidPnPresentPath(pUpdateActiveVidPnPresentPath);
}

NTSTATUS
APIENTRY
PioGpuDodQueryVidPnHWCapability(
    _In_ CONST HANDLE                       hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps)
{
    PAGED_CODE();
    PIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(hAdapter);
    if (!pPioGpuDod->IsDriverActive())
    {
        PIOGPU_LOG_ASSERTION1("PIOGPU (%p) is being called when not active!", pPioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pPioGpuDod->QueryVidPnHWCapability(pVidPnHWCaps);
}

//END: Paged Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg()
// BEGIN: Non-Paged Code

VOID
PioGpuDodDpcRoutine(
    _In_  VOID* pDeviceContext)
{
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    if (!pPioGpuDod->IsHardwareInit())
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("PioGpu (%p) is being called when not active!", pPioGpuDod));
        return;
    }
    pPioGpuDod->DpcRoutine();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN
PioGpuDodInterruptRoutine(
    _In_  VOID* pDeviceContext,
    _In_  ULONG MessageNumber)
{
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    return pPioGpuDod->InterruptRoutine(MessageNumber);
}

VOID
PioGpuDodResetDevice(
    _In_  VOID* pDeviceContext)
{
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    pPioGpuDod->ResetDevice();
}

NTSTATUS
APIENTRY
PioGpuDodSystemDisplayEnable(
    _In_  VOID* pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat)
{
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    return pPioGpuDod->SystemDisplayEnable(TargetId, Flags, Width, Height, ColorFormat);
}

VOID
APIENTRY
PioGpuDodSystemDisplayWrite(
    _In_  VOID* pDeviceContext,
    _In_  VOID* Source,
    _In_  UINT  SourceWidth,
    _In_  UINT  SourceHeight,
    _In_  UINT  SourceStride,
    _In_  UINT  PositionX,
    _In_  UINT  PositionY)
{
    PIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    PioGpuDod* pPioGpuDod = reinterpret_cast<PioGpuDod*>(pDeviceContext);
    pPioGpuDod->SystemDisplayWrite(Source, SourceWidth, SourceHeight, SourceStride, PositionX, PositionY);
}

#if defined(DBG)

#if defined(COM_DEBUG)

#define BTPW_DEBUG_PORT     ((PUCHAR)0x3F8)
#define TEMP_BUFFER_SIZE    256

void DebugPrintFuncSerial(const char *format, ...)
{
    char buf[TEMP_BUFFER_SIZE];
    NTSTATUS status;
    size_t len;
    va_list list;
    va_start(list, format);
    status = RtlStringCbVPrintfA(buf, sizeof(buf), format, list);
    if (status == STATUS_SUCCESS)
    {
        len = strlen(buf);
    }
    else
    {
        len = 2;
        buf[0] = 'O';
        buf[1] = '\n';
    }
    if (len)
    {
        WRITE_PORT_BUFFER_UCHAR(BTPW_DEBUG_PORT, (PUCHAR)buf, (ULONG)len);
        WRITE_PORT_UCHAR(BTPW_DEBUG_PORT, '\r');
    }
    va_end(list);
}
#endif

#if defined(PRINT_DEBUG)
void DebugPrintFuncKdPrint(const char *format, ...)
{
    va_list list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
    va_end(list);
}
#endif

#endif
#pragma code_seg(pop) // End Non-Paged Code

