/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include "common.h"
#include "wnbd_ioctl.h"

static inline UCHAR WnbdOptRegType(WnbdOptValType Type) {
    switch(Type) {
    case WnbdOptBool:
    case WnbdOptInt64:
        return REG_QWORD;
    case WnbdOptWstr:
        return REG_SZ;
    default:
        return REG_NONE;
    }
}

static inline DWORD WnbdOptRegSize(WnbdOptValType Type) {
    switch(Type) {
    case WnbdOptBool:
    case WnbdOptInt64:
        return 8;
    case WnbdOptWstr:
        return WNBD_MAX_NAME_LENGTH;
    default:
        return 0;
    }
}

typedef enum {
    OptLogLevel,
    OptNewMappingsAllowed,
} WNBD_OPT_KEY;

extern WNBD_OPTION WnbdDriverOptions[];

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS WnbdGetPersistentOpt(
    PWCHAR Name,
    PWNBD_OPTION_VALUE Value);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS WnbdGetDrvOpt(
    PWCHAR Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS WnbdSetDrvOpt(
    PWCHAR Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS WnbdResetDrvOpt(
    PWCHAR Name,
    BOOLEAN Persistent);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS WnbdReloadPersistentOptions();

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS WnbdListDrvOpt(
    PWNBD_OPTION_LIST OptionList,
    PULONG BufferSize,
    BOOLEAN Persistent);

// Validate the option value and perform
// required conversions.
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS WnbdProcessOptionValue(
    PWNBD_OPTION Option,
    PWNBD_OPTION_VALUE Value);
