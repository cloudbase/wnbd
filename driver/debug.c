/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <ntstatus.h>
#include <ntstrsafe.h>
#include <wdm.h>

#include "debug.h"

#define WNBD_LOG_BUFFER_SIZE 512
#define WNBD_DBG_DEFAULT     WNBD_DBG_INFO

UINT32  WnbdLogLevel = WNBD_DBG_DEFAULT;
extern UINT32 GlobalLogLevel;

_Use_decl_annotations_
VOID
WnbdSetLogLevel(UINT32 Level)
{
    // FIXME: This will actually override the log level of every log record.
    // It's ok when you want to get debug messages, but can be confusing if you want
    // to avoid info messages. If we set it to "1", all log messages become error messages.
    // If we set it to "3", all messages become INFO messages.
    GlobalLogLevel = Level;
}

_Use_decl_annotations_
VOID
WnbdLog(UINT32 Level,
        PCHAR FuncName,
        UINT32 Line,
        PCHAR Format,
        ...)
{
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(FuncName);
    UNREFERENCED_PARAMETER(Line);
    UNREFERENCED_PARAMETER(Format);

    va_list Args;
    CHAR Buf[WNBD_LOG_BUFFER_SIZE];

    if(GlobalLogLevel) {
        Level = GlobalLogLevel - 1;
    }

    if (Level > WnbdLogLevel) {
        return;
    }

    Buf[0] = 0;
    va_start(Args, Format);
    RtlStringCbVPrintfA(Buf, sizeof(Buf), Format, Args);
    va_end(Args);

    DbgPrintEx(DPFLTR_SCSIMINIPORT_ID, Level, "%s:%lu %s\n", FuncName, Line, Buf);
}
