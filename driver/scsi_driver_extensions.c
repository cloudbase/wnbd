/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "debug.h"
#include "driver.h"
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
    WNBD_LOG_LOUD(": Enter");
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

    WNBD_LOG_LOUD(": Exit");
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

    WNBD_LOG_INFO(
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
    WNBD_LOG_LOUD(": Enter");
    PWNBD_EXTENSION Ext = (PWNBD_EXTENSION) DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    /*
     * https://docs.microsoft.com/en-us/previous-versions/windows/hardware/drivers/ff563901(v%3Dvs.85)
     */
    // We're receiving 0 lengths for SCSIOP_READ|SCSIOP_WRITE when setting
    // MaximumTransferLength to SP_UNINITIALIZED_VALUE. Keeping transfer lengths
    // smaller than 32MB avoids this issue.
    ConfigInfo->MaximumTransferLength = WNBD_DEFAULT_MAX_TRANSFER_LENGTH;
    ConfigInfo->MaxNumberOfIO = WNBD_MAX_IN_FLIGHT_REQUESTS;
    ConfigInfo->NumberOfPhysicalBreaks = SP_UNINITIALIZED_VALUE;
    ConfigInfo->AlignmentMask = FILE_BYTE_ALIGNMENT;
    ConfigInfo->NumberOfBuses = MAX_NUMBER_OF_SCSI_BUSES;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->CachesData = TRUE;
    ConfigInfo->MaximumNumberOfTargets = MAX_NUMBER_OF_SCSI_TARGETS;
    ConfigInfo->MaximumNumberOfLogicalUnits = MAX_NUMBER_OF_SCSI_LOGICAL_UNITS;
    ConfigInfo->WmiDataProvider = FALSE;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->VirtualDevice = TRUE;

    Status = ExInitializeResourceLite(&Ext->DeviceCreationLock);
    if (!NT_SUCCESS(Status)) {
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
        WNBD_LOG_ERROR(": Error calling IoRegisterDeviceInterface. Failed with NTSTATUS: %x.", Status);
        goto CleanLock;
    }

    WnbdInitScsiIds();

    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    WNBD_LOG_LOUD(": Exit SP_RETURN_FOUND");
    return SP_RETURN_FOUND;
Exit:
    RtlFreeUnicodeString(&Ext->DeviceInterface);
CleanLock:
    ExDeleteResourceLite(&Ext->DeviceCreationLock);
Clean:
    WNBD_LOG_ERROR(": Failing with SP_RETURN_NOT_FOUND");
    WNBD_LOG_LOUD(": Exit");
    return SP_RETURN_NOT_FOUND;
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_free_adapter_resources
 */
_Use_decl_annotations_
VOID
WnbdHwFreeAdapterResources(_In_ PVOID DeviceExtension)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    PWNBD_EXTENSION	Ext = (PWNBD_EXTENSION) DeviceExtension;

    WnbdCleanupAllDevices(Ext);
    ExDeleteResourceLite(&Ext->DeviceCreationLock);

    WNBD_LOG_LOUD(": Exit");
}

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_initialize
 */
_Use_decl_annotations_
BOOLEAN
WnbdHwInitialize(PVOID DeviceExtension)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    PWNBD_EXTENSION	Ext = (PWNBD_EXTENSION) DeviceExtension;

    NTSTATUS Status = IoSetDeviceInterfaceState(&Ext->DeviceInterface, TRUE);
    if (!NT_SUCCESS(Status)) {
        WNBD_LOG_ERROR(": Error calling IoSetDeviceInterfaceState %x.", Status);
        return FALSE;
    }

    WNBD_LOG_LOUD(": Exit");
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
    WNBD_LOG_LOUD(": Enter");
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
        WNBD_LOG_LOUD("Calling StorPortCompleteServiceIrp");
        StorPortCompleteServiceIrp(DeviceExtension, Irp);
    } else {
        WNBD_LOG_LOUD("Pending HwProcessServiceRequest");
    }
    WNBD_LOG_LOUD(": Exit");
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
    WNBD_LOG_LOUD(": Enter");

    StorPortCompleteRequest(DeviceExtension, (UCHAR)PathId,
                            SP_UNTAGGED, SP_UNTAGGED, SRB_STATUS_BUS_RESET);

    WNBD_LOG_LOUD(": Exit");
    return TRUE;
}

UCHAR
WnbdFirmwareInfo(PSCSI_REQUEST_BLOCK Srb)
{
    PSRB_IO_CONTROL SrbControl;
    PFIRMWARE_REQUEST_BLOCK Request;
    PSTORAGE_FIRMWARE_INFO_V2 Info;

    SrbControl = (PSRB_IO_CONTROL)SrbGetDataBuffer(Srb);

    Request = (PFIRMWARE_REQUEST_BLOCK)(SrbControl + 1);

    if (sizeof(STORAGE_FIRMWARE_INFO_V2) > Request->DataBufferLength) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        goto exit;
    }

    Info = (PSTORAGE_FIRMWARE_INFO_V2)((PUCHAR)SrbControl + Request->DataBufferOffset);

    if ((STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2 != Info->Version ) ||
        (sizeof(STORAGE_FIRMWARE_INFO_V2) > Info->Size)) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
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
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    } else {
        Request->DataBufferLength = sizeof(STORAGE_FIRMWARE_INFO_V2) + sizeof(STORAGE_FIRMWARE_SLOT_INFO_V2);
        SrbControl->ReturnCode = FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

exit:
    return Srb->SrbStatus;
}

UCHAR
WnbdFirmareIOCTL(PSCSI_REQUEST_BLOCK Srb)
{
    ULONG                   BufferLength = 0;
    PSRB_IO_CONTROL         SrbControl;
    PFIRMWARE_REQUEST_BLOCK Request;

    SrbControl = (PSRB_IO_CONTROL)SrbGetDataBuffer(Srb);
    BufferLength = SrbGetDataTransferLength(Srb);

    if ((sizeof(SRB_IO_CONTROL) + sizeof(FIRMWARE_REQUEST_BLOCK))
        >BufferLength) {
        Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        goto exit;
    }

    Request = (PFIRMWARE_REQUEST_BLOCK)(SrbControl + 1);

    if (((ULONGLONG)Request->DataBufferOffset + Request->DataBufferLength)
        >(ULONGLONG)(BufferLength)) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        goto exit;
    }

    if (FIRMWARE_REQUEST_BLOCK_STRUCTURE_VERSION > Request->Version) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        goto exit;
    }

    if (ALIGN_UP(sizeof(SRB_IO_CONTROL) + sizeof(FIRMWARE_REQUEST_BLOCK), PVOID)
        > Request->DataBufferOffset) {
        SrbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
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
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    }

exit:
    return Srb->SrbStatus;
}

UCHAR
WnbdProcessSrbIOCTL(PSCSI_REQUEST_BLOCK Srb)
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
            Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
            break;
    }

    return Srb->SrbStatus;
}



/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_startio
 */
_Use_decl_annotations_
BOOLEAN
WnbdHwStartIo(PVOID DeviceExtension,
              PSCSI_REQUEST_BLOCK  Srb)
{
    _IRQL_limited_to_(DISPATCH_LEVEL);
    WNBD_LOG_LOUD(": Enter");
    UCHAR SrbStatus = SRB_STATUS_INVALID_REQUEST;
    BOOLEAN Complete = TRUE;
    ASSERT(DeviceExtension);
    PWNBD_EXTENSION Ext = (PWNBD_EXTENSION)DeviceExtension;

    WNBD_LOG_INFO("WnbdHwStartIo Processing SRB Function = 0x%x(%s)",
                  Srb->Function, WnbdToStringSrbFunction(Srb->Function));

    switch (Srb->Function) {
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
        WNBD_LOG_ERROR("Unknown SRB Function = 0x%x(%s)",
                       Srb->Function, WnbdToStringSrbFunction(Srb->Function));
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;

    }

    /*
     * If the operation is not pending notify the Storport of completion
     */
    if (Complete) {
        WNBD_LOG_LOUD("RequestComplete of %s status: 0x%x(%s)",
                      WnbdToStringSrbFunction(Srb->Function),
                      SrbStatus,
                      WnbdToStringSrbStatus(SrbStatus));
        Srb->SrbStatus = SrbStatus;
        StorPortNotification(RequestComplete, DeviceExtension, Srb);
    }

    WNBD_LOG_LOUD(": Exit");

    return TRUE;
}
