/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "debug.h"
#include "driver.h"
#include "options.h"
#include "srb_helper.h"
#include "scsi_driver_extensions.h"
#include "scsi_function.h"
#include "scsi_trace.h"
#include "util.h"
#include "userspace.h"

PWNBD_EXTENSION GlobalExt;

VOID
WnbdScsiAdapterSupportControlTypes(PSCSI_SUPPORTED_CONTROL_TYPE_LIST List)
{
    ASSERT(List);

    ULONG Iterator;

    for (Iterator = 0; Iterator < List->MaxControlType; Iterator++) {
        switch (Iterator) {
        case ScsiQuerySupportedControlTypes:
        case ScsiStopAdapter:
        case ScsiRestartAdapter:
        case ScsiSetBootConfig:
        case ScsiSetRunningConfig:
            List->SupportedTypeList[Iterator] = TRUE;
            break;
        default:
            break;
        }
    }
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_adapter_control
 */
_Use_decl_annotations_
SCSI_ADAPTER_CONTROL_STATUS
WnbdHwAdapterControl(PVOID DeviceExtension,
                     SCSI_ADAPTER_CONTROL_TYPE ControlType,
                     PVOID Parameters)
{
    UNREFERENCED_PARAMETER(DeviceExtension);

    WNBD_LOG_DEBUG(
        "Received control type: %s (%d)",
        WnbdToStringScsiAdapterCtrlType(ControlType),
        ControlType);
    switch (ControlType) {
    case ScsiQuerySupportedControlTypes:
        WnbdScsiAdapterSupportControlTypes(
            (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters);
        break;
    default:
        break;
    }

    return ScsiAdapterControlSuccess;
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_find_adapter
 */
_Use_decl_annotations_
ULONG
WnbdHwFindAdapter(PVOID DeviceExtension,
                  PVOID HwContext,
                  PVOID BusInformation,
                  PVOID LowerDevice,
                  PCHAR ArgumentString,
                  PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                  PBOOLEAN Again)
{
    _IRQL_limited_to_(PASSIVE_LEVEL);
    UNREFERENCED_PARAMETER(Again);
    UNREFERENCED_PARAMETER(ArgumentString);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(LowerDevice);
    PWNBD_EXTENSION Ext = (PWNBD_EXTENSION) DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    /*
     * https://docs.microsoft.com/en-us/previous-versions/windows/hardware/drivers/ff563901(v%3Dvs.85)
     */
    // We're receiving 0 lengths for SCSIOP_READ|SCSIOP_WRITE when setting
    // MaximumTransferLength to SP_UNINITIALIZED_VALUE. Keeping transfer lengths
    // smaller than 32MB avoids this issue.
    ConfigInfo->MaximumTransferLength = WNBD_DEFAULT_MAX_TRANSFER_LENGTH;

    DWORD MaxIOReqPerAdapter =
        (DWORD) WnbdDriverOptions[OptMaxIOReqPerAdapter].Value.Data.AsInt64;
    if (0 < MaxIOReqPerAdapter &&
            MaxIOReqPerAdapter <= WNBD_ABS_MAX_IO_REQ_PER_ADAPTER) {
        WNBD_LOG_INFO("Configured maximum number of requests per adapter: %d",
                      MaxIOReqPerAdapter);
        ConfigInfo->MaxNumberOfIO = MaxIOReqPerAdapter;
    } else {
        WNBD_LOG_WARN("Unsupported maximum number of requests per adapter: %d. "
                      "Minimum: 1. Maximum: %d. "
                      "Falling back to default value: %d.",
                      MaxIOReqPerAdapter,
                      WNBD_ABS_MAX_IO_REQ_PER_ADAPTER,
                      WNBD_DEFAULT_MAX_IO_REQ_PER_ADAPTER);
        ConfigInfo->MaxNumberOfIO = WNBD_DEFAULT_MAX_IO_REQ_PER_ADAPTER;
    }

    DWORD MaxIOReqPerLun =
        (DWORD) WnbdDriverOptions[OptMaxIOReqPerLun].Value.Data.AsInt64;
    if (0 < MaxIOReqPerLun &&
            MaxIOReqPerLun <= WNBD_ABS_MAX_IO_REQ_PER_LUN) {
        WNBD_LOG_INFO("Configured maximum number of requests per lun: %d",
                      MaxIOReqPerLun);
        ConfigInfo->MaxIOsPerLun = MaxIOReqPerLun;
    } else {
        WNBD_LOG_WARN("Unsupported maximum number of requests per lun: %d. "
                      "Minimum: 1. Maximum: %d. "
                      "Falling back to default value: %d.",
                      MaxIOReqPerLun,
                      WNBD_ABS_MAX_IO_REQ_PER_LUN,
                      WNBD_DEFAULT_MAX_IO_REQ_PER_LUN);
        ConfigInfo->MaxIOsPerLun = WNBD_DEFAULT_MAX_IO_REQ_PER_LUN;
    }

    ConfigInfo->InitialLunQueueDepth = ConfigInfo->MaxIOsPerLun;

    ConfigInfo->NumberOfPhysicalBreaks = SP_UNINITIALIZED_VALUE;
    ConfigInfo->AlignmentMask = FILE_BYTE_ALIGNMENT;
    ConfigInfo->NumberOfBuses = WNBD_MAX_BUSES_PER_ADAPTER;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Dma64BitAddresses =
        SCSI_DMA64_MINIPORT_FULL64BIT_NO_BOUNDARY_REQ_SUPPORTED;
    ConfigInfo->Master = TRUE;
    ConfigInfo->CachesData = TRUE;
    ConfigInfo->MaximumNumberOfTargets = WNBD_MAX_TARGETS_PER_BUS;
    ConfigInfo->MaximumNumberOfLogicalUnits = WNBD_MAX_LUNS_PER_TARGET;
    ConfigInfo->WmiDataProvider = FALSE;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->VirtualDevice = TRUE;

    Status = ExInitializeResourceLite(&Ext->DeviceCreationLock);
    if (!NT_SUCCESS(Status)) {
        WNBD_LOG_ERROR("Error initializing resource DeviceCreationLock. Failed with NTSTATUS: 0x%x.", Status);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Clean;
    }

    /*
     * Initialize basic fields of the device global extension
     */
    InitializeListHead(&Ext->DeviceList);       
    KeInitializeSpinLock(&Ext->DeviceListLock);
    KeInitializeEvent(&Ext->GlobalDeviceRemovalEvent, SynchronizationEvent, FALSE);
    ExInitializeRundownProtection(&Ext->RundownProtection);
    GlobalExt = Ext;

    /*
     * Setup user-space communication device
     */
    Status = IoRegisterDeviceInterface((PDEVICE_OBJECT)HwContext,
        &WNBD_GUID, NULL, &Ext->DeviceInterface);

    if (!NT_SUCCESS(Status)) {
        WNBD_LOG_ERROR("Error calling IoRegisterDeviceInterface. Failed with NTSTATUS: 0x%x.", Status);
        goto CleanLock;
    }

    WnbdInitScsiIds();

    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    WNBD_LOG_DEBUG("Exit SP_RETURN_FOUND");
    return SP_RETURN_FOUND;
Exit:
    RtlFreeUnicodeString(&Ext->DeviceInterface);
CleanLock:
    ExDeleteResourceLite(&Ext->DeviceCreationLock);
Clean:
    WNBD_LOG_DEBUG("Exit SP_RETURN_NOT_FOUND");
    return SP_RETURN_NOT_FOUND;
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_free_adapter_resources
 */
_Use_decl_annotations_
VOID
WnbdHwFreeAdapterResources(_In_ PVOID DeviceExtension)
{
    ASSERT(DeviceExtension);
    ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    PWNBD_EXTENSION	Ext = (PWNBD_EXTENSION) DeviceExtension;

    WnbdCleanupAllDevices(Ext);
    ExDeleteResourceLite(&Ext->DeviceCreationLock);
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_initialize
 */
_Use_decl_annotations_
BOOLEAN
WnbdHwInitialize(PVOID DeviceExtension)
{
    ASSERT(DeviceExtension);
    PWNBD_EXTENSION	Ext = (PWNBD_EXTENSION) DeviceExtension;

    NTSTATUS Status = IoSetDeviceInterfaceState(&Ext->DeviceInterface, TRUE);
    if (!NT_SUCCESS(Status)) {
        WNBD_LOG_ERROR("Error calling IoSetDeviceInterfaceState 0x%x.", Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_process_service_request
 */
_Use_decl_annotations_
VOID
WnbdHwProcessServiceRequest(PVOID DeviceExtension,
                            PVOID Irp)
{
    ASSERT(DeviceExtension);
    ASSERT(Irp);

    PIO_STACK_LOCATION IoLocation = IoGetCurrentIrpStackLocation((PIRP)Irp);
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    if (IRP_MJ_DEVICE_CONTROL == IoLocation->MajorFunction) {
        Status = WnbdParseUserIOCTL((PWNBD_EXTENSION) DeviceExtension,
                                    (PIRP)Irp);
    }

    if (STATUS_PENDING != Status) {
        ((PIRP)Irp)->IoStatus.Status = Status;
        WNBD_LOG_DEBUG("Calling StorPortCompleteServiceIrp");
        StorPortCompleteServiceIrp(DeviceExtension, Irp);
    } else {
        WNBD_LOG_DEBUG("Pending HwProcessServiceRequest");
    }
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_reset_bus
 */
_Use_decl_annotations_
BOOLEAN
WnbdHwResetBus(PVOID DeviceExtension,
               ULONG PathId)
{
    _IRQL_limited_to_(DISPATCH_LEVEL);
    StorPortCompleteRequest(DeviceExtension, (UCHAR)PathId,
                            SP_UNTAGGED, SP_UNTAGGED, SRB_STATUS_BUS_RESET);

    return TRUE;
}

UCHAR
WnbdFirmwareInfo(PVOID Srb)
{
    PSRB_IO_CONTROL SrbControl;
    PFIRMWARE_REQUEST_BLOCK Request;
    PSTORAGE_FIRMWARE_INFO_V2 Info;

    SrbControl = (PSRB_IO_CONTROL)SrbGetDataBuffer(Srb);

    Request = (PFIRMWARE_REQUEST_BLOCK)(SrbControl + 1);

    if (sizeof(STORAGE_FIRMWARE_INFO_V2) > Request->DataBufferLength) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        SrbSetSrbStatus(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        goto exit;
    }

    Info = (PSTORAGE_FIRMWARE_INFO_V2)((PUCHAR)SrbControl + Request->DataBufferOffset);

    if ((STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2 != Info->Version ) ||
        (sizeof(STORAGE_FIRMWARE_INFO_V2) > Info->Size)) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        SrbSetSrbStatus(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        goto exit;
    }

    RtlZeroMemory((PCHAR)Info, Request->DataBufferLength);

    /* Just fill data needed for WLK Firmware update test */
    Info->Version = STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2;
    Info->Size = sizeof(STORAGE_FIRMWARE_INFO_V2);
    Info->UpgradeSupport = TRUE;
    Info->SlotCount = 1;
    Info->ActiveSlot = 0;
    Info->PendingActivateSlot = STORAGE_FIRMWARE_INFO_INVALID_SLOT;
    Info->FirmwareShared = FALSE;
    Info->ImagePayloadAlignment = 4096;
    Info->ImagePayloadMaxSize = 8096;

    if ((sizeof(STORAGE_FIRMWARE_INFO_V2) + sizeof(STORAGE_FIRMWARE_SLOT_INFO_V2))
        <= Request->DataBufferLength) {
        Info->Slot[0].SlotNumber = 0;
        Info->Slot[0].ReadOnly = FALSE;
        StorPortCopyMemory(&Info->Slot[0].Revision, "01234567", 8);
        SrbControl->ReturnCode = FIRMWARE_STATUS_SUCCESS;
        SrbSetSrbStatus(Srb, SRB_STATUS_SUCCESS);
    } else {
        Request->DataBufferLength = sizeof(STORAGE_FIRMWARE_INFO_V2) + sizeof(STORAGE_FIRMWARE_SLOT_INFO_V2);
        SrbControl->ReturnCode = FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL;
        SrbSetSrbStatus(Srb, SRB_STATUS_SUCCESS);
    }

exit:
    return SrbGetSrbStatus(Srb);
}

UCHAR
WnbdFirmareIOCTL(PVOID Srb)
{
    ULONG                   BufferLength = 0;
    PSRB_IO_CONTROL         SrbControl;
    PFIRMWARE_REQUEST_BLOCK Request;

    SrbControl = (PSRB_IO_CONTROL)SrbGetDataBuffer(Srb);
    BufferLength = SrbGetDataTransferLength(Srb);

    if ((sizeof(SRB_IO_CONTROL) + sizeof(FIRMWARE_REQUEST_BLOCK))
        >BufferLength) {
        SrbSetSrbStatus(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        goto exit;
    }

    Request = (PFIRMWARE_REQUEST_BLOCK)(SrbControl + 1);

    if (((ULONGLONG)Request->DataBufferOffset + Request->DataBufferLength)
        >(ULONGLONG)(BufferLength)) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        SrbSetSrbStatus(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        goto exit;
    }

    if (FIRMWARE_REQUEST_BLOCK_STRUCTURE_VERSION > Request->Version) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        SrbSetSrbStatus(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        goto exit;
    }

    if (ALIGN_UP(sizeof(SRB_IO_CONTROL) + sizeof(FIRMWARE_REQUEST_BLOCK), PVOID)
        > Request->DataBufferOffset) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        SrbSetSrbStatus(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        goto exit;
    }

    switch (Request->Function) {

    case FIRMWARE_FUNCTION_GET_INFO:
        WnbdFirmwareInfo(Srb);
        break;

    case FIRMWARE_FUNCTION_DOWNLOAD:
    case FIRMWARE_FUNCTION_ACTIVATE:
    default:
        SrbControl->ReturnCode = FIRMWARE_STATUS_SUCCESS;
        SrbSetSrbStatus(Srb, SRB_STATUS_SUCCESS);
        break;

    }

exit:
    return SrbGetSrbStatus(Srb);
}

UCHAR
WnbdProcessSrbIOCTL(PVOID Srb)
{
    PSRB_IO_CONTROL SrbControl = (PSRB_IO_CONTROL)SrbGetDataBuffer(Srb);

    switch (SrbControl->ControlCode) {
        case IOCTL_SCSI_MINIPORT_FIRMWARE:
        {
            WnbdFirmareIOCTL(Srb);
        }
        break;

        default:
            SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
            SrbSetSrbStatus(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
            break;
    }

    return SrbGetSrbStatus(Srb);
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_startio
 */
_Use_decl_annotations_
BOOLEAN
WnbdHwStartIo(PVOID DeviceExtension, PVOID Srb)
{
    _IRQL_limited_to_(DISPATCH_LEVEL);
    UCHAR SrbStatus = SRB_STATUS_INVALID_REQUEST;
    BOOLEAN Complete = TRUE;
    ASSERT(DeviceExtension);
    PWNBD_EXTENSION Ext = (PWNBD_EXTENSION)DeviceExtension;
    ULONG SrbFunction = SrbGetSrbFunction(Srb);

    WNBD_LOG_DEBUG("WnbdHwStartIo Processing SRB Function = 0x%x(%s)",
                   SrbFunction, WnbdToStringSrbFunction(SrbFunction));

    switch (SrbFunction) {
    case SRB_FUNCTION_EXECUTE_SCSI:
        SrbStatus = WnbdExecuteScsiFunction(Ext, Srb, &Complete);
        break;

    case SRB_FUNCTION_RESET_LOGICAL_UNIT:
        SrbStatus = WnbdResetLogicalUnitFunction(Ext, Srb);
        break;

    case SRB_FUNCTION_RESET_DEVICE:
        SrbStatus = WnbdResetDeviceFunction(Ext, Srb);
        break;

    case SRB_FUNCTION_ABORT_COMMAND:
        SrbStatus = WnbdAbortFunction(Ext, Srb);
        break;

    case SRB_FUNCTION_PNP:
        SrbStatus = WnbdPNPFunction(Srb);
        break;

    case SRB_FUNCTION_IO_CONTROL: {
        SrbStatus = WnbdProcessSrbIOCTL(Srb);
        break;
    }

    case SRB_FUNCTION_FLUSH:
    case SRB_FUNCTION_SHUTDOWN:
        /* Set to NOOP for virtual devices */
        SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case SRB_FUNCTION_WMI:
    case SRB_FUNCTION_RESET_BUS:
    default:
        WNBD_LOG_INFO("Unknown SRB Function = 0x%x(%s)",
                       SrbFunction, WnbdToStringSrbFunction(SrbFunction));
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;

    }

    /*
     * If the operation is not pending notify the Storport of completion
     */
    if (Complete) {
        WNBD_LOG_DEBUG("RequestComplete of %s status: 0x%x(%s)",
                       WnbdToStringSrbFunction(SrbFunction),
                       SrbStatus,
                       WnbdToStringSrbStatus(SrbStatus));
        SrbSetSrbStatus(Srb, SrbStatus);
        StorPortNotification(RequestComplete, DeviceExtension, Srb);
    }

    return TRUE;
}
