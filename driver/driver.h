/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef DRIVER_H
#define DRIVER_H 1

#include "version.h"

// Maximum number of scsi targets per bus
#define WNBD_MAX_TARGETS_PER_BUS      2
// Maximum number of luns per target
#define WNBD_MAX_LUNS_PER_TARGET      255
// Maximum number of buses per target
#define WNBD_MAX_BUSES_PER_ADAPTER    1
// The maximum number of disks per WNBD adapter
#define WNBD_MAX_NUMBER_OF_DISKS \
    (WNBD_MAX_LUNS_PER_TARGET * \
     WNBD_MAX_TARGETS_PER_BUS * \
     WNBD_MAX_BUSES_PER_ADAPTER)

#define WNBD_INQUIRY_VENDOR_ID           "WNBD"
#define WNBD_INQUIRY_PRODUCT_ID          "WNBD_DISK"
// Placeholder
#define WNBD_INQUIRY_VENDOR_SPECIFIC     ""

#endif
