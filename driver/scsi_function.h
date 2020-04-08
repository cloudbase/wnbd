/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef SCSI_FUNCTION_H
#define SCSI_FUNCTION_H 1

#include "common.h"

UCHAR
WnbdAbortFunction(_In_ PVOID DeviceExtension,
                  _In_ PSCSI_REQUEST_BLOCK Srb);

UCHAR
WnbdResetLogicalUnitFunction(_In_ PVOID DeviceExtension,
                             _In_ PSCSI_REQUEST_BLOCK Srb);

UCHAR
WnbdResetDeviceFunction(_In_ PVOID DeviceExtension,
                        _In_ PSCSI_REQUEST_BLOCK Srb);

UCHAR
WnbdExecuteScsiFunction(_In_ PVOID DeviceExtension,
                        _In_ PSCSI_REQUEST_BLOCK Srb,
                        _Inout_ PBOOLEAN Complete);

UCHAR
WnbdPNPFunction(_In_ PSCSI_REQUEST_BLOCK Srb);

#endif
