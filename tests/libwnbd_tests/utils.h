/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <string.h>

#include <wnbd.h>

// Converts a Windows error code to a string, including the error
// description.
std::string WinStrError(DWORD Err);

// Retrieves the disk path and waits for it to become available.
// Raises a runtime error upon failure.
std::string GetDiskPath(std::string InstanceName);

// Configures the specified disk as writable.
// Raises a runtime error upon failure.
void SetDiskWritable(std::string InstanceName);
void SetDiskWritable(HANDLE DiskHandle);

// Fetches the specified env variable, returning an empty string if
// missing.
// Raises a runtime error upon failure.
std::string GetEnv(std::string Name);
