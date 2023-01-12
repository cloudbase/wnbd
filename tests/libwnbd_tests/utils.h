/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <string.h>

#include <wnbd.h>

#define EVENTUALLY(expression, retry_attempts, retry_interval_ms)   \
{                                                                   \
    static_assert(retry_attempts > 0);                              \
    static_assert(retry_interval_ms > 0);                           \
    int _retry_attempts = retry_attempts;                           \
    bool ok = false;                                                \
    while (_retry_attempts--) {                                     \
        if (expression) {                                           \
            ok = true;                                              \
        }                                                           \
        else {                                                      \
            Sleep(retry_interval_ms);                               \
        }                                                           \
    }                                                               \
    if (!ok)                                                        \
        GTEST_FATAL_FAILURE_("Expression mismatch: "#expression);   \
}

static const uint64_t DefaultBlockCount = 1 << 20;
static const uint64_t DefaultBlockSize = 512;

std::string GetNewInstanceName();

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

// Returns a string containing the hex values of the byte array
// received as parameter.
std::string ByteArrayToHex(BYTE* arr, int length);
