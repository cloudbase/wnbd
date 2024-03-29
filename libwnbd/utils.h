/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <string>
#include <guiddef.h>
#include <optional>

std::wstring to_wstring(const std::string& str);
std::string to_string(const std::wstring& str);
std::string win32_strerror(int err);

std::string guid_to_string(GUID guid);

std::optional<_OSVERSIONINFOEXW> GetWinVersion();
bool CheckWindowsVersion(DWORD Major, DWORD Minor, DWORD BuildNumber);
bool EnsureWindowsVersionSupported();
