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
#include "wnbd.h"

#define WNBD_CLI_OWNER_NAME "wnbd-client"

void
PrintSyntax();

DWORD
CmdUnmap(
    PCSTR InstanceName,
    BOOLEAN HardRemove,
    BOOLEAN NoHardDisonnectFallback,
    DWORD SoftDisconnectTimeout,
    DWORD SoftDisconnectRetryInterval);

DWORD
CmdStats(PCSTR InstanceName);

DWORD
CmdMap(
    PCSTR InstanceName,
    PCSTR HostName,
    DWORD PortNumber,
    PCSTR ExportName,
    UINT64 DiskSize,
    UINT32 BlockSize,
    BOOLEAN MustNegotiate,
    BOOLEAN ReadOnly);

DWORD
CmdList();

DWORD
CmdShow(PCSTR InstanceName);

DWORD
CmdVersion();

DWORD
CmdGetOpt(const char* Name, BOOLEAN Persistent);

DWORD
CmdSetOpt(const char* Name, const char* Value, BOOLEAN Persistent);

DWORD
CmdResetOpt(const char* Name, BOOLEAN Persistent);

DWORD
CmdListOpt(BOOLEAN Persistent);

#ifdef __cplusplus
}
#endif
