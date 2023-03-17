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
#define WNBD_MAX_IN_FLIGHT_REQUESTS 2048
// TODO: make this configuragble. 255 is the Storport default.
#define WNBD_MAX_IO_REQ_PER_LUN 512
#define WNBD_PREALLOC_BUFF_SZ (WNBD_DEFAULT_MAX_TRANSFER_LENGTH + sizeof(NBD_REQUEST))

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

NTSTATUS
WnbdSetDiskSize(_In_ PWNBD_EXTENSION DeviceExtension,
                _In_ WNBD_CONNECTION_ID ConnectionId,
                _In_ UINT64 BlockCount);

VOID
WnbdInitScsiIds();

VOID
WnbdDeviceMonitorThread(_In_ PVOID Context);

#endif
