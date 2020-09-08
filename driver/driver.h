/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef DRIVER_H
#define DRIVER_H 1

#define MAX_NUMBER_OF_SCSI_TARGETS       128
#define MAX_NUMBER_OF_SCSI_LOGICAL_UNITS 1
#define MAX_NUMBER_OF_SCSI_BUSES         1

// TODO: replace those placeholders
#define WNBD_INQUIRY_VENDOR_ID           "WNBD Disk"
#define WNBD_INQUIRY_PRODUCT_ID          "WNBD_DISK_ID"
#define WNBD_INQUIRY_PRODUCT_REVISION    "V0.1"
#define WNBD_INQUIRY_VENDOR_SPECIFIC     "WNBD_DISK_SPECIFIC_VENDOR_STRING"

BOOLEAN
WNBDReadRegistryValue(_In_ PUNICODE_STRING RegistryPath,
                      _In_ PWSTR Key,
                      _In_ ULONG Type,
                      _Out_ PVOID Value);

#endif
