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

typedef struct _USER_ENTRY {
    LIST_ENTRY                         ListEntry;
    struct _SCSI_DEVICE_INFORMATION*   ScsiInformation;
    ULONG                              BusIndex;
    ULONG                              TargetIndex;
    ULONG                              LunIndex;
    BOOLEAN                            Connected;
    UINT64                             DiskSize;
    UINT16                             BlockSize;
    USER_IN                            UserInformation;
} USER_ENTRY, *PUSER_ENTRY;

typedef struct _SCSI_DEVICE_INFORMATION
{
    PVOID                       Device;
    PGLOBAL_INFORMATION         GlobalInformation;

    ULONG                       BusIndex;
    ULONG                       TargetIndex;
    ULONG                       LunIndex;
    PINQUIRYDATA                InquiryData;

    PUSER_ENTRY                 UserEntry;
    INT                         Socket;

    LIST_ENTRY                  ListHead;
    KSPIN_LOCK                  ListLock;

    KEVENT                      DeviceEvent;
    PVOID                       DeviceThread;
    BOOLEAN                     HardTerminateDevice;
    BOOLEAN                     SoftTerminateDevice;
} SCSI_DEVICE_INFORMATION, *PSCSI_DEVICE_INFORMATION;

NTSTATUS
WnbdParseUserIOCTL(_In_ PVOID GlobalHandle,
                   _In_ PIRP Irp);

BOOLEAN
WnbdFindConnection(_In_ PGLOBAL_INFORMATION GInfo,
                   _In_ PUSER_IN Info,
                   _Maybenull_ PUSER_ENTRY* Entry);

NTSTATUS
WnbdCreateConnection(_In_ PGLOBAL_INFORMATION GInfo,
                     _In_ PUSER_IN Info);

NTSTATUS
WnbdDeleteConnectionEntry(_In_ PUSER_ENTRY Entry);

NTSTATUS
WnbdEnumerateActiveConnections(_In_ PGLOBAL_INFORMATION GInfo,
                               _In_ PIRP Irp);

VOID
WnbdInitScsiIds();

#endif
