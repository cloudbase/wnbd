/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "debug.h"
#include "driver.h"
#include "driver_extension.h"
#include "scsi_driver_extensions.h"
#include "scsi_function.h"
#include "scsi_trace.h"
#include "util.h"
#include "userspace.h"

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
    WNBD_LOG_LOUD(": Enter");

    SCSI_ADAPTER_CONTROL_STATUS Status = ScsiAdapterControlSuccess;
    ASSERT(DeviceExtension);
    PWNBD_EXTENSION Ext = (PWNBD_EXTENSION) DeviceExtension;

    Ext->ScsiAdapterControlState = ControlType;

    switch (ControlType) {
    case ScsiQuerySupportedControlTypes:
        WNBD_LOG_INFO("ScsiQuerySupportedControlTypes");
        WnbdScsiAdapterSupportControlTypes((PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters);
        break;

    case ScsiStopAdapter:
        WNBD_LOG_INFO("ScsiStopAdapter");
        break;

    case ScsiRestartAdapter:
        WNBD_LOG_INFO("ScsiRestartAdapter");
        break;

    case ScsiSetBootConfig:
        WNBD_LOG_INFO("ScsiSetBootConfig");
        break;

    case ScsiSetRunningConfig:
        WNBD_LOG_INFO("ScsiSetRunningConfig");
        break;

    default:
        break;
    }

    WNBD_LOG_LOUD(": Exit");

    return Status;
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
    HANDLE DeviceCleanerHandle;

    /*
     * https://docs.microsoft.com/en-us/previous-versions/windows/hardware/drivers/ff563901(v%3Dvs.85)
     */
    ConfigInfo->MaximumTransferLength = SP_UNINITIALIZED_VALUE;
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

    Status = ExInitializeResourceLite(&Ext->DeviceResourceLock);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanThread;
    }

    /*
     * Initialize basic fields of the device global extension
     */

    InitializeListHead(&Ext->DeviceList);       
    KeInitializeSpinLock(&Ext->DeviceListLock);
    KeInitializeEvent(&Ext->DeviceCleanerEvent, SynchronizationEvent, FALSE);

    /*
     * Initialize out device thread cleaner
     */
    Status = PsCreateSystemThread(&DeviceCleanerHandle,
                                  (ACCESS_MASK)0L, NULL, NULL, NULL,
                                  WnbdDeviceCleanerThread, Ext);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanThread;
    }
    Status = ObReferenceObjectByHandle(DeviceCleanerHandle, THREAD_ALL_ACCESS, NULL,
        KernelMode, &Ext->DeviceCleaner, NULL);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanThread;
    }

    /*
     * Setup user-space communication device
     */
    Status = IoRegisterDeviceInterface((PDEVICE_OBJECT)HwContext,
        &WNBD_GUID, NULL, &Ext->DeviceInterface);

    if (!NT_SUCCESS(Status)) {
        WNBD_LOG_ERROR(": Error calling IoRegisterDeviceInterface. Failed with NTSTATUS: %x.", Status);
        goto CleanAdapter;
    }

    Status = WnbdInitializeGlobalInformation(DeviceExtension, &Ext->GlobalInformation);

    if (!NT_SUCCESS(Status)) {
        goto Clean;
    }

    WNBD_LOG_LOUD(": Exit SP_RETURN_FOUND");
    return SP_RETURN_FOUND;
Clean:
    RtlFreeUnicodeString(&Ext->DeviceInterface);
CleanAdapter:
    Ext->StopDeviceCleaner = TRUE;
    KeSetEvent(&Ext->DeviceCleanerEvent, IO_NO_INCREMENT, FALSE);
CleanThread:
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

    Ext->StopDeviceCleaner = TRUE;
    KeSetEvent(&Ext->DeviceCleanerEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(Ext->DeviceCleaner, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(Ext->DeviceCleaner);
    ExDeleteResourceLite(&Ext->DeviceResourceLock);

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
    PWNBD_EXTENSION	Ext = (PWNBD_EXTENSION) DeviceExtension;

    if (IRP_MJ_DEVICE_CONTROL == IoLocation->MajorFunction) {
        Status = WnbdParseUserIOCTL(Ext->GlobalInformation, (PIRP)Irp);
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

    case SRB_FUNCTION_FLUSH:
    case SRB_FUNCTION_SHUTDOWN:
    case SRB_FUNCTION_IO_CONTROL:
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
        WNBD_LOG_LOUD("RequestComplete of %p status: 0x%x(%s)",
                      Srb, SrbStatus, WnbdToStringSrbStatus(SrbStatus));
        Srb->SrbStatus = SrbStatus;
        StorPortNotification(RequestComplete, DeviceExtension, Srb);
    }

    WNBD_LOG_LOUD(": Exit");

    return TRUE;
}
