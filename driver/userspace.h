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
#include "userspace_shared.h"
#include "rbd_protocol.h"

// TODO: make this configurable.
#define WNBD_MAX_IN_FLIGHT_REQUESTS 255
// For transfers larger than 32MB, we'll receive 0 sized buffers.
#define WNBD_MAX_TRANSFER_LENGTH 2 * 1024 * 1024
#define WNBD_PREALLOC_BUFF_SZ (WNBD_MAX_TRANSFER_LENGTH + sizeof(NBD_REQUEST))

typedef struct _USER_ENTRY {
    LIST_ENTRY                         ListEntry;
    struct _SCSI_DEVICE_INFORMATION*   ScsiInformation;
    ULONG                              BusIndex;
    ULONG                              TargetIndex;
    ULONG                              LunIndex;
    BOOLEAN                            Connected;
    UINT64                             DiskSize;
    UINT16                             BlockSize;
    UINT16                             NbdFlags;
    CONNECTION_INFO                    UserInformation;
} USER_ENTRY, *PUSER_ENTRY;

typedef struct _SCSI_DEVICE_INFORMATION
{
    PWNBD_SCSI_DEVICE           Device;
    PGLOBAL_INFORMATION         GlobalInformation;

    ULONG                       BusIndex;
    ULONG                       TargetIndex;
    ULONG                       LunIndex;
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

    KSEMAPHORE                  RequestSemaphore;

    KSEMAPHORE                  DeviceEvent;
    PVOID                       DeviceRequestThread;
    PVOID                       DeviceReplyThread;
    BOOLEAN                     HardTerminateDevice;
    BOOLEAN                     SoftTerminateDevice;

    WNBD_STATS                  Stats;
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
                   _In_ PCONNECTION_INFO Info,
                   _Maybenull_ PUSER_ENTRY* Entry);

NTSTATUS
WnbdCreateConnection(_In_ PGLOBAL_INFORMATION GInfo,
                     _In_ PCONNECTION_INFO Info);

NTSTATUS
WnbdDeleteConnectionEntry(_In_ PUSER_ENTRY Entry);

NTSTATUS
WnbdEnumerateActiveConnections(_In_ PGLOBAL_INFORMATION GInfo,
                               _In_ PIRP Irp);

NTSTATUS
WnbdDeleteConnection(_In_ PGLOBAL_INFORMATION GInfo,
                     _In_ PCONNECTION_INFO Info);

VOID
WnbdInitScsiIds();

#endif
