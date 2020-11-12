/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <stdio.h>

#include "wnbd_log.h"
#include "wnbd.h"

VOID ConsoleLogger(
    WnbdLogLevel LogLevel,
    const char* Message,
    const char* FileName,
    UINT32 Line,
    const char* FunctionName)
{
    fprintf(stderr, "libwnbd.dll!%s %s %s\n",
            FunctionName, WnbdLogLevelToStr(LogLevel), Message);
}

static LogMessageFunc WnbdCurrLogger = ConsoleLogger;
static WnbdLogLevel WnbdCurrLogLevel = WnbdLogLevelWarning;

VOID LogMessage(WnbdLogLevel LogLevel,
                const char* FileName, UINT32 Line, const char* FunctionName,
                const char* Format, ...)
{
    LogMessageFunc CurrLogger = WnbdCurrLogger;
    WnbdLogLevel CurrLogLevel = WnbdCurrLogLevel;

    if (!CurrLogger || CurrLogLevel < LogLevel)
        return;

    va_list Args;
    va_start(Args, Format);

    size_t BufferLength = (size_t)_vscprintf(Format, Args) + 1;

    // TODO: consider enforcing WNBD_LOG_MESSAGE_MAX_SIZE and using a fixed
    // size buffer for performance reasons.
    char* Buff = (char*) malloc(BufferLength);
    if (!Buff) {
        va_end(Args);
        return;
    }

    vsnprintf_s(Buff, BufferLength, BufferLength - 1, Format, Args);
    va_end(Args);

    CurrLogger(LogLevel, Buff, FileName, Line, FunctionName);

    free(Buff);
}

VOID WnbdSetLogger(LogMessageFunc Logger)
{
    // Passing NULL should allow completely disabling the logger.
    WnbdCurrLogger = Logger;
}

VOID WnbdSetLogLevel(WnbdLogLevel LogLevel)
{
    WnbdCurrLogLevel = LogLevel;
}
