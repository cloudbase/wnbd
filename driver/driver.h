/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef DRIVER_H
#define DRIVER_H 1

#include "version.h"

// Maximum number of scsi targets per bus
#define MAX_NUMBER_OF_SCSI_TARGETS       128
// Maximum number of luns per target
#define MAX_NUMBER_OF_SCSI_LOGICAL_UNITS 1
#define MAX_NUMBER_OF_SCSI_BUSES         1
// The maximum number of disks per WNBD adapter
#define MAX_NUMBER_OF_DISKS (MAX_NUMBER_OF_SCSI_LOGICAL_UNITS * \
                             MAX_NUMBER_OF_SCSI_TARGETS * \
                             MAX_NUMBER_OF_SCSI_BUSES)

#define WNBD_INQUIRY_VENDOR_ID           "WNBD"
#define WNBD_INQUIRY_PRODUCT_ID          "WNBD_DISK"
// Placeholder
#define WNBD_INQUIRY_VENDOR_SPECIFIC     ""

#endif
