#ifndef WNBD_DISPATCH_H
#define WNBD_DISPATCH_H 1

#include "common.h"
#include "userspace.h"

// TODO: consider moving this to util.h
NTSTATUS LockUsermodeBuffer(
    PVOID Buffer, UINT32 BufferSize, BOOLEAN Writeable,
    PVOID* OutBuffer, PMDL* OutMdl, BOOLEAN* Locked);

NTSTATUS WnbdDispatchRequest(
    PIRP Irp,
    PWNBD_SCSI_DEVICE Device,
    PWNBD_IOCTL_FETCH_REQ_COMMAND Command);

NTSTATUS WnbdHandleResponse(
    PIRP Irp,
    PWNBD_SCSI_DEVICE Device,
    PWNBD_IOCTL_SEND_RSP_COMMAND Command);

#endif // WNBD_DISPATCH_H
