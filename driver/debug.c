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

#define WNBD_LOG_BUFFER_SIZE 512

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

    DbgPrintEx(DPFLTR_SCSIMINIPORT_ID, Level, "%s:%lu %s\n", FuncName, Line, Buf);
    WnbdWppTrace(Level, "%s:%lu %s\n", FuncName, Line, Buf);
}
