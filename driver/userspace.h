/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef USERSPACE_H
#define USERSPACE_H 1

#include "driver.h"
#include "driver_extension.h"
#include "scsi_driver_extensions.h"
#include "rbd_protocol.h"
#include "wnbd_ioctl.h"

// TODO: make this configurable. 1024 is the Storport default.
#define WNBD_MAX_IN_FLIGHT_REQUESTS 1024
#define WNBD_PREALLOC_BUFF_SZ (WNBD_DEFAULT_MAX_TRANSFER_LENGTH + sizeof(NBD_REQUEST))

// The connection id provided to the user is meant to be opaque. We're currently
// using the disk address, but that might change.
#define WNBD_CONNECTION_ID_FROM_ADDR(PathId, TargetId, Lun) \
    ((1 << 24 | (PathId) << 16) | ((TargetId) << 8) | (Lun))

typedef struct _USER_ENTRY {
    LIST_ENTRY                         ListEntry;
    struct _SCSI_DEVICE_INFORMATION*   ScsiInformation;
    USHORT                             BusIndex;
    USHORT                             TargetIndex;
    USHORT                             LunIndex;
    BOOLEAN                            Connected;
    WNBD_PROPERTIES                    Properties;
    WNBD_CONNECTION_ID                 ConnectionId;
} USER_ENTRY, *PUSER_ENTRY;

typedef struct _SCSI_DEVICE_INFORMATION
{
    PWNBD_SCSI_DEVICE           Device;
    PGLOBAL_INFORMATION         GlobalInformation;

    PINQUIRYDATA                InquiryData;

    PUSER_ENTRY                 UserEntry;
    INT                         Socket;
    INT                         SocketToClose;
    ERESOURCE                   SocketLock;

    // TODO: rename as PendingReqListHead
    LIST_ENTRY                  RequestListHead;
    KSPIN_LOCK                  RequestListLock;

    // TODO: rename as SubmittedReqListHead
    LIST_ENTRY                  ReplyListHead;
    KSPIN_LOCK                  ReplyListLock;

    KSEMAPHORE                  DeviceEvent;
    PVOID                       DeviceRequestThread;
    PVOID                       DeviceReplyThread;
    BOOLEAN                     HardTerminateDevice;
    BOOLEAN                     SoftTerminateDevice;
    KEVENT                      TerminateEvent;
    // The rundown protection provides device reference counting, preventing
    // it from being deallocated while still being accessed. This is
    // especially important for IO dispatching.
    EX_RUNDOWN_REF              RundownProtection;


    WNBD_DRV_STATS              Stats;
    PVOID                       ReadPreallocatedBuffer;
    ULONG                       ReadPreallocatedBufferLength;
    PVOID                       WritePreallocatedBuffer;
    ULONG                       WritePreallocatedBufferLength;
} SCSI_DEVICE_INFORMATION, *PSCSI_DEVICE_INFORMATION;

NTSTATUS
WnbdParseUserIOCTL(_In_ PVOID GlobalHandle,
                   _In_ PIRP Irp);

BOOLEAN
WnbdFindConnection(_In_ PGLOBAL_INFORMATION GInfo,
                   _In_ PCHAR InstanceName,
                   _Maybenull_ PUSER_ENTRY* Entry);

PUSER_ENTRY
WnbdFindConnectionEx(_In_ PGLOBAL_INFORMATION GInfo,
                     _In_ UINT64 ConnectionId);

NTSTATUS
WnbdCreateConnection(_In_ PGLOBAL_INFORMATION GInfo,
                     _In_ PWNBD_PROPERTIES Properties,
                     _In_ PWNBD_CONNECTION_INFO ConnectionInfo);

NTSTATUS
WnbdDeleteConnectionEntry(_In_ PUSER_ENTRY Entry);

NTSTATUS
WnbdEnumerateActiveConnections(_In_ PGLOBAL_INFORMATION GInfo,
                               _In_ PIRP Irp);

NTSTATUS
WnbdDeleteConnection(_In_ PGLOBAL_INFORMATION GInfo,
                     _In_ PCHAR InstanceName);

VOID
WnbdInitScsiIds();

#endif
