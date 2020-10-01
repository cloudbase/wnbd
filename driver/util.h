/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef UTIL_H
#define UTIL_H 1

#include "common.h"
#include "scsi_driver_extensions.h"

VOID
DrainDeviceQueue(_In_ PWNBD_SCSI_DEVICE Device,
                 _In_ BOOLEAN SubmittedRequests);
VOID
AbortSubmittedRequests(_In_ PWNBD_SCSI_DEVICE Device);
VOID
CompleteRequest(_In_ PWNBD_SCSI_DEVICE Device,
                _In_ PSRB_QUEUE_ELEMENT Element,
                _In_ BOOLEAN FreeElement);

VOID
WnbdCleanupAllDevices(_In_ PWNBD_EXTENSION DeviceExtension);

// Increments the device rundown protection reference count, preventing
// it from being cleaned up.
BOOLEAN
WnbdAcquireDevice(_In_ PWNBD_SCSI_DEVICE Device);
// Decrements the reference count. All "WnbdAcquireDevice" calls must
// be paired with a "WnbdReleaseDevice" call.
VOID
WnbdReleaseDevice(_In_ PWNBD_SCSI_DEVICE Device);
// Signals the device cleanup thread, setting the "*TerminateDevice" flags
// to avoid further processing.
VOID
WnbdDisconnectAsync(PWNBD_SCSI_DEVICE Device);
// The specified device must be acquired. It will be released by
// WnbdDisconnectSync.
VOID
WnbdDisconnectSync(_In_ PWNBD_SCSI_DEVICE Device);

// Device returned by WnbdFindDevice* functions must be subsequently
// relased using WnbdReleaseDevice, if "Acquire" is set.
// Unacquired device pointers must not be dereferenced.
PWNBD_SCSI_DEVICE
WnbdFindDeviceByAddr(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ BOOLEAN Acquire);
PWNBD_SCSI_DEVICE
WnbdFindDeviceByConnId(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ UINT64 ConnectionId,
    _In_ BOOLEAN Acquire);
PWNBD_SCSI_DEVICE
WnbdFindDeviceByInstanceName(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ PCHAR InstanceName,
    _In_ BOOLEAN Acquire);

BOOLEAN
IsReadSrb(_In_ PSCSI_REQUEST_BLOCK Srb);

VOID DisconnectSocket(_In_ PWNBD_SCSI_DEVICE Device);
VOID CloseSocket(_In_ PWNBD_SCSI_DEVICE Device);
int ScsiOpToNbdReqType(_In_ int ScsiOp);
BOOLEAN ValidateScsiRequest(
    _In_ PWNBD_SCSI_DEVICE Device,
    _In_ PSRB_QUEUE_ELEMENT Element);

#define LIST_FORALL_SAFE(_headPtr, _itemPtr, _nextPtr)                \
    for (_itemPtr = (_headPtr)->Flink, _nextPtr = (_itemPtr)->Flink;  \
         _itemPtr != _headPtr;                                        \
         _itemPtr = _nextPtr, _nextPtr = (_itemPtr)->Flink)

#endif

UCHAR SetSrbStatus(PVOID Srb, PWNBD_STATUS Status);

static inline int
ScsiOpToWnbdReqType(int ScsiOp)
{
    switch (ScsiOp) {
    case SCSIOP_READ6:
    case SCSIOP_READ:
    case SCSIOP_READ12:
    case SCSIOP_READ16:
        return WnbdReqTypeRead;
    case SCSIOP_WRITE6:
    case SCSIOP_WRITE:
    case SCSIOP_WRITE12:
    case SCSIOP_WRITE16:
        return WnbdReqTypeWrite;
    case SCSIOP_UNMAP:
        return WnbdReqTypeUnmap;
    case SCSIOP_SYNCHRONIZE_CACHE:
    case SCSIOP_SYNCHRONIZE_CACHE16:
        return WnbdReqTypeFlush;
    default:
        return WnbdReqTypeUnknown;
    }
}
