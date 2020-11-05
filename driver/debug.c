/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <ntstatus.h>
#include <ntstrsafe.h>
#include <wdm.h>

#include "debug.h"
#include "options.h"
#include "events.h"

#define WNBD_LOG_BUFFER_SIZE 512

VOID EtwPrint(UINT32 Level,
              PCHAR FuncName,
              UINT32 Line,
              PCHAR Buf)
{
    BOOLEAN Enabled = WnbdDriverOptions[OptEtwLoggingEnabled].Value.Data.AsBool;
    if (!Enabled) {
        return;
    }
    switch (Level) {
    case WNBD_LVL_ERROR:
        EventWriteErrorEvent(NULL, FuncName, Line, Buf);
        break;
    case WNBD_LVL_WARN:
        EventWriteWarningEvent(NULL, FuncName, Line, Buf);
        break;
    default:
        EventWriteInformationalEvent(NULL, FuncName, Line, Buf);
        break;
    }
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

    UINT64 WnbdLogLevel = WnbdDriverOptions[OptLogLevel].Value.Data.AsInt64;
    if (Level > WnbdLogLevel) {
        return;
    }

    Buf[0] = 0;
    va_start(Args, Format);
    RtlStringCbVPrintfA(Buf, sizeof(Buf), Format, Args);
    va_end(Args);

    /* Log via ETW */
    EtwPrint(Level, FuncName, Line, Buf);

    /* DbgPrint logging */
    if (WnbdDriverOptions[OptDbgPrintEnabled].Value.Data.AsBool) {
        DbgPrintEx(DPFLTR_SCSIMINIPORT_ID, Level, "%s:%lu %s\n", FuncName, Line, Buf);
    }

    /* Log via WPP */
    if (WnbdDriverOptions[OptWppLoggingEnabled].Value.Data.AsBool) {
        WnbdWppTrace(Level, "%s:%lu %s\n", FuncName, Line, Buf);
    }
}
