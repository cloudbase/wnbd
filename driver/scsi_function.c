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
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = SRB_STATUS_NO_DEVICE;
    PWNBD_SCSI_DEVICE Device;

    if (SrbGetCdb(Srb)) {
        BYTE CdbValue = SrbGetCdb(Srb)->AsByte[0];

        WNBD_LOG_INFO(": Received %#02x command. SRB = 0x%p. CDB = 0x%x. PathId: %d TargetId: %d LUN: %d",
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
    AbortSubmittedRequests(Device);

    WnbdReleaseDevice(Device);
    SrbStatus = SRB_STATUS_SUCCESS;

Exit:
    WNBD_LOG_LOUD(": Exit");
    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdAbortFunction(_In_ PVOID DeviceExtension,
                  _In_ PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");

    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = DrainDeviceQueues(DeviceExtension, Srb);

    WNBD_LOG_LOUD(": Exit");

    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdResetLogicalUnitFunction(PVOID DeviceExtension,
                             PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");

    ASSERT(Srb);
    ASSERT(DeviceExtension);

    UCHAR SrbStatus = DrainDeviceQueues(DeviceExtension, Srb);

    WNBD_LOG_LOUD(": Exit");

    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdResetDeviceFunction(PVOID DeviceExtension,
                        PSCSI_REQUEST_BLOCK  Srb)
{
    WNBD_LOG_LOUD(": Enter");

    ASSERT(Srb);
    ASSERT(DeviceExtension);

    StorPortCompleteRequest(DeviceExtension,
                            Srb->PathId,
                            Srb->TargetId,
                            SP_UNTAGGED,
                            SRB_STATUS_TIMEOUT);

    WNBD_LOG_LOUD(": Exit");

    return SRB_STATUS_SUCCESS;
}

_Use_decl_annotations_
UCHAR
WnbdExecuteScsiFunction(PVOID DeviceExtension,
                        PSCSI_REQUEST_BLOCK Srb,
                        PBOOLEAN Complete)
{
    WNBD_LOG_LOUD(": Enter");

    ASSERT(DeviceExtension);
    ASSERT(Srb);
    ASSERT(Complete);

    NTSTATUS Status = STATUS_SUCCESS;
    UCHAR SrbStatus = SRB_STATUS_NO_DEVICE;
    PWNBD_SCSI_DEVICE Device;
    *Complete = TRUE;

    if (SrbGetCdb(Srb)) {
        BYTE CdbValue = SrbGetCdb(Srb)->AsByte[0];

        WNBD_LOG_INFO(": Received %#02x command. SRB = 0x%p. CDB = 0x%x. PathId: %d TargetId: %d LUN: %d",
            CdbValue, Srb, CdbValue, Srb->PathId, Srb->TargetId, Srb->Lun);
    }

    Device = WnbdFindDeviceByAddr(
        (PWNBD_EXTENSION)DeviceExtension, Srb->PathId, Srb->TargetId, Srb->Lun, TRUE);
    if (NULL == Device) {
        WNBD_LOG_INFO("Could not find device PathId: %d TargetId: %d LUN: %d",
                      Srb->PathId, Srb->TargetId, Srb->Lun);
        goto Exit;
    }
    if (Device->HardTerminateDevice) {
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

    WNBD_LOG_LOUD(": Exit");
    return SrbStatus;
}

_Use_decl_annotations_
UCHAR
WnbdPNPFunction(PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");

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
            DeviceCapabilitiesEx->SurpriseRemovalOK = 1;
            DeviceCapabilitiesEx->Removable = 1;

            SrbStatus = SRB_STATUS_SUCCESS;
        }
        break;

    default:
        WNBD_LOG_WARN("Untreated SCSI Request PNP Flag: %x", PNP->SrbPnPFlags);
        break;
    }

    WNBD_LOG_LOUD(": Exit with SrbStatus: %s",
                  WnbdToStringSrbStatus(SrbStatus));

    return SrbStatus;
}
