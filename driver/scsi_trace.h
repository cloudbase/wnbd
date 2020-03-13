/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef SCSI_TRACE_H
#define SCSI_TRACE_H 1

#include "common.h"

PCHAR
WnbdToStringSrbFunction(_In_ UCHAR SrbFunction);

PCHAR
WnbdToStringSrbCdbOperation(_In_ UCHAR SrbOperation);

PCHAR
WnbdToStringSrbStatus(_In_ UCHAR SrbStatus);

#endif
