/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef SCSI_OPERATION_H
#define SCSI_OPERATION_H 1

#include "common.h"
#include "userspace.h"

NTSTATUS
WnbdHandleSrbOperation(_In_ PWNBD_EXTENSION DeviceExtension,
                       _In_ PWNBD_SCSI_DEVICE ScsiDeviceExtension,
                       _In_ PSCSI_REQUEST_BLOCK Srb);
#endif
