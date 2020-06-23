/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <ksocket.h>
#include "common.h"
#include "debug.h"
#include "driver_extension.h"
#include "userspace.h"

extern PGLOBAL_INFORMATION GlobalInformation = NULL;

_Use_decl_annotations_
NTSTATUS
WnbdInitializeGlobalInformation(PVOID Handle,
                                PVOID* PPGlobalInformation)
{
    WNBD_LOG_LOUD(": Enter");
    NTSTATUS Status = STATUS_SUCCESS;
    *PPGlobalInformation = NULL;

    PGLOBAL_INFORMATION Info = (PGLOBAL_INFORMATION)
        ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(GLOBAL_INFORMATION), 'DBNg');

    if(!Info) {
        WNBD_LOG_ERROR(": Error allocating global information");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Info, sizeof(GLOBAL_INFORMATION));

    Info->Handle = Handle;

    InitializeListHead(&Info->ConnectionList);
    if (!NT_SUCCESS(ExInitializeResourceLite(&Info->ConnectionMutex))) {
        WNBD_LOG_ERROR(": Error allocating Info->ConnectionMutex");
        ExFreePool(Info);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *PPGlobalInformation = Info;

    GlobalInformation = Info;

    WnbdInitScsiIds();

    WNBD_LOG_LOUD(": Exit.");

    return Status;
}

_Use_decl_annotations_
VOID
WnbdDeleteGlobalInformation(PVOID PGlobalInformation)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(PGlobalInformation);
    PAGED_CODE();

    PGLOBAL_INFORMATION Info = (PGLOBAL_INFORMATION) PGlobalInformation;

    if (Info) {
        ExDeleteResourceLite(&Info->ConnectionMutex);
        ExFreePool(Info);
        KsInitialize();
        KsDestroy();
        Info = NULL;
    }

    WNBD_LOG_LOUD(": Exit");
}
