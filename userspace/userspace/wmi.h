/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once
#include <comutil.h>
#pragma comment(lib, "comsuppd.lib")
#pragma comment(lib, "comsuppwd.lib")
#define _WIN32_DCOM
#include <wbemcli.h>
#pragma comment(lib, "wbemuuid.lib")

#include <string>
#include <vector>
#include <iostream>

struct DiskInfo
{
    std::wstring deviceId;
    std::wstring freeSpace;
    uint32_t Index;
};

bool ReleaseWMI();
bool InitWMI();
std::wstring GetProperty(IWbemClassObject* pclsObj, const std::wstring& property);
UINT32 GetPropertyInt(IWbemClassObject* pclsObj, const std::wstring& property);
bool QueryWMI(_bstr_t Query, std::vector<DiskInfo>& disks);
