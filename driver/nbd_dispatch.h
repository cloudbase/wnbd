/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "scsi_driver_extensions.h"
#include "nbd_protocol.h"

VOID
NbdDeviceRequestThread(_In_ PVOID Context);
#pragma alloc_text (PAGE, NbdDeviceRequestThread)
VOID
NbdDeviceReplyThread(_In_ PVOID Context);
#pragma alloc_text (PAGE, NbdDeviceReplyThread)
VOID
NbdProcessDeviceThreadReplies(_In_ PWNBD_SCSI_DEVICE Device);
