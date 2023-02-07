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
    ConfigInfo->MaxNumberOfIO = WNBD_MAX_IN_FLIGHT_REQUESTS;
    ConfigInfo->NumberOfPhysicalBreaks = SP_UNINITIALIZED_VALUE;
    ConfigInfo->AlignmentMask = FILE_BYTE_ALIGNMENT;
    ConfigInfo->NumberOfBuses = WNBD_MAX_BUSES_PER_ADAPTER;
    ConfigInfo->ScatterGather = TRUE;
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

/*
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_startio
 */
_Use_decl_annotations_
BOOLEAN
WnbdHwStartIo(PVOID DeviceExtension,
              PSCSI_REQUEST_BLOCK  Srb)
{
    _IRQL_limited_to_(DISPATCH_LEVEL);
    UCHAR SrbStatus = SRB_STATUS_INVALID_REQUEST;
    BOOLEAN Complete = TRUE;
    ASSERT(DeviceExtension);
    PWNBD_EXTENSION Ext = (PWNBD_EXTENSION)DeviceExtension;

    WNBD_LOG_DEBUG("WnbdHwStartIo Processing SRB Function = 0x%x(%s)",
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

    case SRB_FUNCTION_FLUSH:
    case SRB_FUNCTION_SHUTDOWN:
        /* Set to NOOP for virtual devices */
        SrbStatus = SRB_STATUS_SUCCESS;
        break;

    case SRB_FUNCTION_WMI:
    case SRB_FUNCTION_RESET_BUS:
    default:
        WNBD_LOG_INFO("Unknown SRB Function = 0x%x(%s)",
                       Srb->Function, WnbdToStringSrbFunction(Srb->Function));
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;

    }

    /*
     * If the operation is not pending notify the Storport of completion
     */
    if (Complete) {
        WNBD_LOG_DEBUG("RequestComplete of %s status: 0x%x(%s)",
                       WnbdToStringSrbFunction(Srb->Function),
                       SrbStatus,
                       WnbdToStringSrbStatus(SrbStatus));
        Srb->SrbStatus = SrbStatus;
        StorPortNotification(RequestComplete, DeviceExtension, Srb);
    }

    return TRUE;
}
