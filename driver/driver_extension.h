/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef DRIVER_EXTENSION_H
#define DRIVER_EXTENSION_H 1

#include "common.h"

typedef struct _PGLOBAL_INFORMATION
{
    PVOID                   Handle;

    LONG                    ConnectionCount;
    LIST_ENTRY              ConnectionList;
    ERESOURCE               ConnectionMutex;

} GLOBAL_INFORMATION, *PGLOBAL_INFORMATION;

VOID
WnbdDeleteGlobalInformation(_Inout_ PVOID PGlobalInformation);
#pragma alloc_text (PAGE, WnbdDeleteGlobalInformation)

NTSTATUS
WnbdInitializeGlobalInformation(_In_ PVOID Handle,
                                _Out_ PVOID* PPGlobalInformation);

#endif
