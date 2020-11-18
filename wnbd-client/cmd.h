/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

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

#include <string>

#include <wnbd.h>

#define WNBD_CLI_OWNER_NAME "wnbd-client"

void
PrintSyntax();

DWORD
CmdUnmap(
    std::string InstanceName,
    BOOLEAN HardRemove,
    BOOLEAN NoHardDisonnectFallback,
    DWORD SoftDisconnectTimeout,
    DWORD SoftDisconnectRetryInterval);

DWORD
CmdStats(std::string InstanceName);

DWORD
CmdMap(
    std::string InstanceName,
    std::string HostName,
    DWORD PortNumber,
    std::string ExportName,
    UINT64 DiskSize,
    UINT32 BlockSize,
    BOOLEAN MustNegotiate,
    BOOLEAN ReadOnly);

DWORD
CmdList();

DWORD
CmdShow(std::string InstanceName);

DWORD
CmdVersion();

DWORD
CmdGetOpt(std::string Name, BOOLEAN Persistent);

DWORD
CmdSetOpt(std::string Name, std::string Value, BOOLEAN Persistent);

DWORD
CmdResetOpt(std::string Name, BOOLEAN Persistent);

DWORD
CmdListOpt(BOOLEAN Persistent);

DWORD
CmdUninstall();

DWORD
CmdInstall(std::string FileName);
