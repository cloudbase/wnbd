/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "common.h"
#include "debug.h"
#include "scsi_driver_extensions.h"
#include "scsi_operation.h"
#include "scsi_trace.h"
#include "srb_helper.h"
#include "scsi_function.h"
#include "util.h"
#include "userspace.h"

UCHAR DrainDeviceQueues(PVOID DeviceExtension,
                        PVOID Srb,
                        BOOLEAN CheckStaleConn)

{
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = SRB_STATUS_NO_DEVICE;
    PWNBD_DISK_DEVICE Device;

    UCHAR PathId = SrbGetPathId(Srb);
    UCHAR TargetId = SrbGetTargetId(Srb);
    UCHAR Lun = SrbGetLun(Srb);

    PCDB Cdb = SrbGetCdb(Srb);
    if (Cdb) {
        BYTE CdbValue = Cdb->AsByte[0];

        WNBD_LOG_INFO("Received %#02x command. SRB = 0x%p. CDB = 0x%x. PathId: %d TargetId: %d LUN: %d",
            CdbValue, Srb, CdbValue, PathId, TargetId, Lun);
    }

    Device = WnbdFindDeviceByAddr(
        DeviceExtension, PathId, TargetId, Lun, TRUE);
    if (NULL == Device) {
        WNBD_LOG_INFO("Could not find device PathId: %d TargetId: %d LUN: %d",
            PathId, TargetId, Lun);
        goto Exit;
    }

    DrainDeviceQueue(Device, FALSE, CheckStaleConn);
    DrainDeviceQueue(Device, TRUE, CheckStaleConn);

    WnbdReleaseDevice(Device);
    SrbStatus = SRB_STATUS_SUCCESS;

Exit:
    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdAbortFunction(_In_ PVOID DeviceExtension,
                  _In_ PVOID Srb)
{
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = DrainDeviceQueues(DeviceExtension, Srb, TRUE);

    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdResetLogicalUnitFunction(PVOID DeviceExtension,
                             PVOID Srb)
{
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = DrainDeviceQueues(DeviceExtension, Srb, TRUE);

    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdResetDeviceFunction(PVOID DeviceExtension,
                        PVOID Srb)
{
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    StorPortCompleteRequest(DeviceExtension,
                            SrbGetPathId(Srb),
                            SrbGetTargetId(Srb),
                            SrbGetLun(Srb),
                            SRB_STATUS_TIMEOUT);

    return SRB_STATUS_SUCCESS;
}

_Use_decl_annotations_
UCHAR
WnbdExecuteScsiFunction(PVOID DeviceExtension,
                        PVOID Srb,
                        PBOOLEAN Complete)
{
    ASSERT(DeviceExtension);
    ASSERT(Srb);
    ASSERT(Complete);

    UCHAR PathId = SrbGetPathId(Srb);
    UCHAR TargetId = SrbGetTargetId(Srb);
    UCHAR Lun = SrbGetLun(Srb);

    NTSTATUS Status = STATUS_SUCCESS;
    UCHAR SrbStatus = SRB_STATUS_NO_DEVICE;
    PWNBD_DISK_DEVICE Device;
    *Complete = TRUE;

    PCDB Cdb = SrbGetCdb(Srb);
    if (Cdb) {
        BYTE CdbValue = Cdb->AsByte[0];

        WNBD_LOG_DEBUG("Received %#02x command. SRB = 0x%p. CDB = 0x%x. PathId: %d TargetId: %d LUN: %d",
            CdbValue, Srb, CdbValue, PathId, TargetId, Lun);
    }

    Device = WnbdFindDeviceByAddr(
        (PWNBD_EXTENSION)DeviceExtension, PathId, TargetId, Lun, TRUE);
    if (NULL == Device) {
        WNBD_LOG_DEBUG("Could not find device PathId: %d TargetId: %d LUN: %d",
                       PathId, TargetId, Lun);
        goto Exit;
    }
    if (Device->HardRemoveDevice) {
        WNBD_LOG_DEBUG("%p is marked for deletion. PathId = %d. TargetId = %d. LUN = %d",
                       Device, PathId, TargetId, Lun);
        goto Exit;
    }

    InterlockedIncrement64(&Device->Stats.OutstandingIOCount);
    Status = WnbdHandleSrbOperation((PWNBD_EXTENSION)DeviceExtension, Device, Srb);

    if(STATUS_PENDING == Status) {
        *Complete = FALSE;
        SrbStatus = SRB_STATUS_PENDING;
    } else {
        InterlockedDecrement64(&Device->Stats.OutstandingIOCount);
        SrbStatus = SrbGetSrbStatus(Srb);
    }

Exit:
    if (Device)
        WnbdReleaseDevice(Device);

    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdPNPFunction(PVOID Srb)
{
    ASSERT(Srb);

    STOR_PNP_ACTION PnPAction;
    ULONG SrbPnPFlags;

    PSRBEX_DATA_PNP SrbExPnp = (PSRBEX_DATA_PNP)SrbGetSrbExDataByType(
        (PSTORAGE_REQUEST_BLOCK) Srb,
        SrbExDataTypePnP);
    if (SrbExPnp) {
        SrbPnPFlags = SrbExPnp->SrbPnPFlags;
        PnPAction = SrbExPnp->PnPAction;
    } else {
        PSCSI_PNP_REQUEST_BLOCK SrbPnp = (PSCSI_PNP_REQUEST_BLOCK) Srb;
        SrbPnPFlags = SrbPnp->SrbPnPFlags;
        PnPAction = SrbPnp->PnPAction;
    }

    UCHAR SrbStatus = SRB_STATUS_INVALID_REQUEST;

    switch (PnPAction)
    {
    case StorQueryCapabilities:
        if (!(SrbPnPFlags & SRB_PNP_FLAGS_ADAPTER_REQUEST) &&
                SrbGetDataTransferLength(Srb) >=
                    sizeof(STOR_DEVICE_CAPABILITIES_EX))
        {
            PVOID DataBuffer = SrbGetDataBuffer(Srb);
            ASSERT(DataBuffer);

            PSTOR_DEVICE_CAPABILITIES_EX DeviceCapabilitiesEx = DataBuffer;
            RtlZeroMemory(
                DeviceCapabilitiesEx,
                sizeof(STOR_DEVICE_CAPABILITIES_EX));
            DeviceCapabilitiesEx->DefaultWriteCacheEnabled = 1;
            DeviceCapabilitiesEx->SilentInstall = 1;
            // We're disabling SurpriseRemovalOK in order to
            // receive device removal PnP events.
            DeviceCapabilitiesEx->SurpriseRemovalOK = 0;
            DeviceCapabilitiesEx->Removable = 1;
            DeviceCapabilitiesEx->EjectSupported = 1;

            SrbStatus = SRB_STATUS_SUCCESS;
        }
        break;

    default:
        WNBD_LOG_INFO("Untreated SCSI request. PnP action: %x, "
                      "PnP flag: %x", PnPAction, SrbPnPFlags);
        break;
    }

    WNBD_LOG_DEBUG("Exit with SrbStatus: %s",
                   WnbdToStringSrbStatus(SrbStatus));

    return SrbStatus;
}
