/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef SCSI_OPERATION_H
#define SCSI_OPERATION_H 1

#include "common.h"

NTSTATUS
WnbdHandleSrbOperation(_In_ PVOID DeviceExtension,
                       _In_ PVOID ScsiDeviceExtension,
                       _In_ PSCSI_REQUEST_BLOCK Srb);
#endif
