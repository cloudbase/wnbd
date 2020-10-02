/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <string>

std::wstring to_wstring(const char* str);
std::string to_string(std::wstring wstr);
std::string win32_strerror(int err);
