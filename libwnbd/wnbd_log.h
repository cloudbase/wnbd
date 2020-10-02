/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include "wnbd.h"

#ifdef __cplusplus
extern "C" {
#endif

VOID ConsoleLogger(
    WnbdLogLevel LogLevel,
    const char* Message,
    const char* FileName,
    UINT32 Line,
    const char* FunctionName);

VOID LogMessage(
    WnbdLogLevel LogLevel,
    const char* FileName,
    UINT32 Line,
    const char* FunctionName,
    const char* Format, ...);

#define LogCritical(Format, ...) \
    LogMessage(WnbdLogLevelCritical, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogError(Format, ...) \
    LogMessage(WnbdLogLevelError, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogWarning(Format, ...) \
    LogMessage(WnbdLogLevelWarning, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogInfo(Format, ...) \
    LogMessage(WnbdLogLevelInfo, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogDebug(Format, ...) \
    LogMessage(WnbdLogLevelDebug, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogTrace(Format, ...) \
    LogMessage(WnbdLogLevelTrace, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
