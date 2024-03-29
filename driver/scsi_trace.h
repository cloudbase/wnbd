/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef SCSI_TRACE_H
#define SCSI_TRACE_H 1

#include "common.h"

PCHAR
WnbdToStringSrbFunction(_In_ ULONG SrbFunction);

PCHAR
WnbdToStringSrbStatus(_In_ UCHAR SrbStatus);

PCHAR
WnbdToStringPnpMinorFunction(_In_ UCHAR PnpMinorFunction);

PCHAR
WnbdToStringScsiAdapterCtrlType(_In_ UCHAR ControlType);

#endif
