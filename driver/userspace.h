/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef USERSPACE_H
#define USERSPACE_H 1

#include "driver.h"
#include "nbd_protocol.h"
#include "wnbd_ioctl.h"
#include "scsi_driver_extensions.h"

// TODO: make this configurable. 1024 is the Storport default.
#define WNBD_MAX_IN_FLIGHT_REQUESTS 1024
#define WNBD_PREALLOC_BUFF_SZ (WNBD_DEFAULT_MAX_TRANSFER_LENGTH + sizeof(NBD_REQUEST))

// The connection id provided to the user is meant to be opaque. We're currently
// using the disk address, but that might change.
#define WNBD_CONNECTION_ID_FROM_ADDR(PathId, TargetId, Lun) \
    ((1 << 24 | (PathId) << 16) | ((TargetId) << 8) | (Lun))

NTSTATUS
WnbdParseUserIOCTL(_In_ PWNBD_EXTENSION DeviceExtension,
                   _In_ PIRP Irp);

NTSTATUS
WnbdCreateConnection(_In_ PWNBD_EXTENSION DeviceExtension,
                     _In_ PWNBD_PROPERTIES Properties,
                     _In_ PWNBD_CONNECTION_INFO ConnectionInfo);

NTSTATUS
WnbdEnumerateActiveConnections(_In_ PWNBD_EXTENSION DeviceExtension,
                               _In_ PIRP Irp);

NTSTATUS
WnbdDeleteConnection(_In_ PWNBD_EXTENSION DeviceExtension,
                     _In_ PCHAR InstanceName);

VOID
WnbdInitScsiIds();

VOID
WnbdDeviceMonitorThread(_In_ PVOID Context);

#endif
