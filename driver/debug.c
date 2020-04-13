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
    va_list Args;
    CHAR Buf[WNBD_LOG_BUFFER_SIZE];

    if (Level > WnbdLogLevel) {
        return;
    }

    Buf[0] = 0;
    va_start(Args, Format);
    RtlStringCbVPrintfA(Buf, sizeof(Buf), Format, Args);
    va_end(Args);

    if (!GlobalLogLevel) {
        DbgPrintEx(DPFLTR_SCSIMINIPORT_ID, Level, "%s:%lu %s\n", FuncName, Line, Buf);
    } else {
        DbgPrintEx(DPFLTR_SCSIMINIPORT_ID, GlobalLogLevel - 1, "%s:%lu %s\n", FuncName, Line, Buf);
    }
}
