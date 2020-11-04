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
                        PSCSI_REQUEST_BLOCK Srb)

{
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = SRB_STATUS_NO_DEVICE;
    PWNBD_DISK_DEVICE Device;

    if (SrbGetCdb(Srb)) {
        BYTE CdbValue = SrbGetCdb(Srb)->AsByte[0];

        WNBD_LOG_INFO("Received %#02x command. SRB = 0x%p. CDB = 0x%x. PathId: %d TargetId: %d LUN: %d",
            CdbValue, Srb, CdbValue, Srb->PathId, Srb->TargetId, Srb->Lun);
    }

    Device = WnbdFindDeviceByAddr(
        DeviceExtension, Srb->PathId, Srb->TargetId, Srb->Lun, TRUE);
    if (NULL == Device) {
        WNBD_LOG_INFO("Could not find device PathId: %d TargetId: %d LUN: %d",
            Srb->PathId, Srb->TargetId, Srb->Lun);
        goto Exit;
    }

    DrainDeviceQueue(Device, FALSE);
    if (Device->Properties.Flags.UseNbd) {
        // NBD replies don't include the IO size so we'll have to keep
        // this data around.
        AbortSubmittedRequests(Device);
    }
    else {
        DrainDeviceQueue(Device, TRUE);
    }

    WnbdReleaseDevice(Device);
    SrbStatus = SRB_STATUS_SUCCESS;

Exit:
    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdAbortFunction(_In_ PVOID DeviceExtension,
                  _In_ PSCSI_REQUEST_BLOCK Srb)
{
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = DrainDeviceQueues(DeviceExtension, Srb);

    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdResetLogicalUnitFunction(PVOID DeviceExtension,
                             PSCSI_REQUEST_BLOCK Srb)
{
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = DrainDeviceQueues(DeviceExtension, Srb);

    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdResetDeviceFunction(PVOID DeviceExtension,
                        PSCSI_REQUEST_BLOCK  Srb)
{
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    StorPortCompleteRequest(DeviceExtension,
                            Srb->PathId,
                            Srb->TargetId,
                            SP_UNTAGGED,
                            SRB_STATUS_TIMEOUT);

    return SRB_STATUS_SUCCESS;
}

_Use_decl_annotations_
UCHAR
WnbdExecuteScsiFunction(PVOID DeviceExtension,
                        PSCSI_REQUEST_BLOCK Srb,
                        PBOOLEAN Complete)
{
    ASSERT(DeviceExtension);
    ASSERT(Srb);
    ASSERT(Complete);

    NTSTATUS Status = STATUS_SUCCESS;
    UCHAR SrbStatus = SRB_STATUS_NO_DEVICE;
    PWNBD_DISK_DEVICE Device;
    *Complete = TRUE;

    if (SrbGetCdb(Srb)) {
        BYTE CdbValue = SrbGetCdb(Srb)->AsByte[0];

        WNBD_LOG_LOUD("Received %#02x command. SRB = 0x%p. CDB = 0x%x. PathId: %d TargetId: %d LUN: %d",
            CdbValue, Srb, CdbValue, Srb->PathId, Srb->TargetId, Srb->Lun);
    }

    Device = WnbdFindDeviceByAddr(
        (PWNBD_EXTENSION)DeviceExtension, Srb->PathId, Srb->TargetId, Srb->Lun, TRUE);
    if (NULL == Device) {
        WNBD_LOG_INFO("Could not find device PathId: %d TargetId: %d LUN: %d",
                      Srb->PathId, Srb->TargetId, Srb->Lun);
        goto Exit;
    }
    if (Device->HardRemoveDevice) {
        WNBD_LOG_WARN("%p is marked for deletion. PathId = %d. TargetId = %d. LUN = %d",
                      Device, Srb->PathId, Srb->TargetId, Srb->Lun);
        goto Exit;
    }

    InterlockedIncrement64(&Device->Stats.OutstandingIOCount);
    Status = WnbdHandleSrbOperation((PWNBD_EXTENSION)DeviceExtension, Device, Srb);

    if(STATUS_PENDING == Status) {
        *Complete = FALSE;
        SrbStatus = SRB_STATUS_PENDING;
    } else {
        InterlockedDecrement64(&Device->Stats.OutstandingIOCount);
        SrbStatus = Srb->SrbStatus;
    }

Exit:
    if (Device)
        WnbdReleaseDevice(Device);

    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdPNPFunction(PSCSI_REQUEST_BLOCK Srb)
{
    ASSERT(Srb);
    PSCSI_PNP_REQUEST_BLOCK PNP = (PSCSI_PNP_REQUEST_BLOCK) Srb;
    UCHAR SrbStatus = SRB_STATUS_INVALID_REQUEST;

    switch (PNP->PnPAction)
    {
    case StorQueryCapabilities:
        if (!(PNP->SrbPnPFlags & SRB_PNP_FLAGS_ADAPTER_REQUEST)) {
            PVOID DataBuffer = SrbGetDataBuffer(Srb);
            ASSERT(DataBuffer);

            PSTOR_DEVICE_CAPABILITIES_EX DeviceCapabilitiesEx = DataBuffer;
            // TODO: check why zero-ing the entire structure leads to a crash
            // on WS 2016.
            RtlZeroMemory(DeviceCapabilitiesEx, sizeof(PSTOR_DEVICE_CAPABILITIES_EX));
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
        WNBD_LOG_WARN("Untreated SCSI request. PnP action: %x, "
                      "PnP flag: %x", PNP->PnPAction, PNP->SrbPnPFlags);
        break;
    }

    WNBD_LOG_LOUD("Exit with SrbStatus: %s",
                  WnbdToStringSrbStatus(SrbStatus));

    return SrbStatus;
}
