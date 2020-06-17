/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include <winioctl.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <setupapi.h>
#include <string.h>
#include <process.h>

/* WNBD Defines */
#include <userspace_shared.h>

INT
Syntax(void);

DWORD
WnbdUnmap(PCHAR instanceName);

DWORD
WnbdStats(PCHAR instanceName);

DWORD
WnbdMap(PCHAR InstanceName,
        PCHAR HostName,
        PCHAR PortName,
        PCHAR ExportName,
        UINT64 DiskSize,
        BOOLEAN Removable);

DWORD
WnbdList(PDISK_INFO_LIST* Output);

DWORD
WnbdSetDebug(UINT32 LogLevel);

#ifdef __cplusplus
}
#endif
