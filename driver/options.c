/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "options.h"
#include "debug.h"
#include "util.h"
#include "wnbd_ioctl.h"

#pragma warning(push)
#pragma warning(disable:4204)

extern UNICODE_STRING GlobalRegistryPath;

#define WNBD_DEF_OPT(OptName, TypeSuffix, DefaultVal) \
    {.Name = OptName, \
     .Type = WnbdOpt ## TypeSuffix, \
     .Default = { .Type = WnbdOpt ## TypeSuffix, \
                  .Data.As ## TypeSuffix = DefaultVal}, \
     .Value = { .Type = WnbdOpt ## TypeSuffix, \
                .Data.As ## TypeSuffix = DefaultVal}, \
    }

// Make sure to update WNBD_OPT_KEY whenever adding or removing options.
// It provides simple and fast access to the options without having to
// introduce other structures. Also, it would be nice to keep the
// options sorted.
WNBD_OPTION WnbdDriverOptions[] = {
    WNBD_DEF_OPT(L"LogLevel", Int64, WNBD_LVL_WARN),
    WNBD_DEF_OPT(L"NewMappingsAllowed", Bool, TRUE),
    WNBD_DEF_OPT(L"EtwLoggingEnabled", Bool, TRUE),
    WNBD_DEF_OPT(L"WppLoggingEnabled", Bool, FALSE),
    WNBD_DEF_OPT(L"DbgPrintEnabled", Bool, TRUE),
};
DWORD WnbdOptionsCount = sizeof(WnbdDriverOptions) / sizeof(WNBD_OPTION);

PWNBD_OPTION WnbdFindOpt(PWCHAR Name)
{
    for (DWORD i = 0; i < WnbdOptionsCount; i++) {
        if (!_wcsicmp(Name, WnbdDriverOptions[i].Name)) {
            return &WnbdDriverOptions[i];
        }
    }
    return NULL;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
ReadRegistryValue(
    PWSTR Key,
    ULONG Type,
    PVOID Value)
{
    ASSERT(Key);
    ASSERT(Value);

    RTL_QUERY_REGISTRY_TABLE Table[2];
    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags =
        RTL_QUERY_REGISTRY_DIRECT |
        RTL_QUERY_REGISTRY_REQUIRED |
        RTL_QUERY_REGISTRY_TYPECHECK;
    Table[0].Name = Key;
    Table[0].EntryContext = Value;
    Table[0].DefaultType = Type;

    NTSTATUS Status = RtlQueryRegistryValues(
        RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
        GlobalRegistryPath.Buffer, Table, 0, 0);

    WNBD_LOG_DEBUG("Exit: 0x%x", Status);
    return Status;
}

_Use_decl_annotations_
NTSTATUS WnbdGetPersistentOpt(
    PWCHAR Name,
    PWNBD_OPTION_VALUE Value)
{
    PWNBD_OPTION Option = WnbdFindOpt(Name); 
    if (!Option) {
        WNBD_LOG_WARN("Could not find option: %ls.", Name);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    PVOID Buff = NULL;
    RtlZeroMemory(&Value->Data, sizeof(Value->Data));
    UNICODE_STRING StringBuff = {
        0,
        sizeof(Value->Data.AsWstr) - sizeof(WCHAR),
        (PWCHAR) &Value->Data.AsWstr};
    switch(Option->Type) {
    case WnbdOptBool:
    case WnbdOptInt64:
        Buff = &Value->Data;
        // We have to explicitly specify the data size using the
        // first 4 bytes of the buffer when using data types larger
        // than 4 bytes such as QWORD.
        Value->Data.AsInt64 = (UINT64)-1 * WnbdOptRegSize(Option->Type);
        break;
    case WnbdOptWstr:
        Buff = &StringBuff;
        break;
    default:
        WNBD_LOG_WARN("Unsupported option type: %d", Option->Type);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    NTSTATUS Status = ReadRegistryValue(
        Name,
        WnbdOptRegType(Option->Type) << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT,
        Buff);
    if (!Status) {
        Value->Type = Option->Type;
    }
    return Status;
}

_Use_decl_annotations_
NTSTATUS WnbdGetDrvOpt(
    PWCHAR Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent)
{
    if (Persistent) {
        return WnbdGetPersistentOpt(Name, Value);
    }

    PWNBD_OPTION Option = WnbdFindOpt(Name);
    if (!Option) {
        WNBD_LOG_WARN("Could not find option: %ls.", Name);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    *Value = Option->Value;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WnbdProcessOptionValue(
    PWNBD_OPTION Option,
    PWNBD_OPTION_VALUE Value)
{
    if (Value->Type == WnbdOptWstr) {
        // Ensure that the string is NULL terminated.
        Value->Data.AsWstr[WNBD_MAX_NAME_LENGTH - 1] = L'\0';
    }

    NTSTATUS Status = STATUS_SUCCESS;
    if (Value->Type != Option->Type) {
        // We'll try to convert strings, rejecting other
        // type mismatches.
        if (Value->Type != WnbdOptWstr) {
            return STATUS_OBJECT_TYPE_MISMATCH;
        }

        WNBD_OPTION_VALUE ConvertedValue = { .Type = Option->Type };
        UNICODE_STRING StringBuff = {
            0,
            sizeof(Option->Value),
            (PWCHAR) &Value->Data.AsWstr};
        switch (Option->Type) {
        case WnbdOptBool:
            Status = WstrToBool(Value->Data.AsWstr, &ConvertedValue.Data.AsBool);
            break;
        case WnbdOptInt64:
            Status = RtlUnicodeStringToInt64(
                &StringBuff, 0,
                &ConvertedValue.Data.AsInt64, NULL);
            break;
        default:
            WNBD_LOG_WARN("Unsupported option type: %d.", Option->Type);
            Status = STATUS_OBJECT_TYPE_MISMATCH;
            break;
        }
        if (!Status) {
            *Value = ConvertedValue;
        }
    }
    return Status;
}

_Use_decl_annotations_
NTSTATUS WnbdSetDrvOpt(
    PWCHAR Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent)
{
    PWNBD_OPTION Option = WnbdFindOpt(Name);
    if (!Option) {
        WNBD_LOG_WARN("Could not find option: %ls.", Name);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    NTSTATUS Status = WnbdProcessOptionValue(Option, Value);
    if (Status) {
        return Status;
    }

    // We'll set the persistent value first. If that fails,
    // we won't set the runtime value.
    if (Persistent) {
        Status = RtlWriteRegistryValue(
            RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
            GlobalRegistryPath.Buffer, Name,
            WnbdOptRegType(Option->Type),
            (PVOID)&Value->Data,
            WnbdOptRegSize(Option->Type));
        if (Status) {
            return Status;
        }
    }

    Option->Value = *Value;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WnbdResetDrvOpt(
    PWCHAR Name,
    BOOLEAN Persistent)
{
    PWNBD_OPTION Option = WnbdFindOpt(Name);
    if (!Option) {
        WNBD_LOG_WARN("Could not find opton: %ls.", Name);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (Persistent) {
        NTSTATUS Status = RtlDeleteRegistryValue(
            RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
            GlobalRegistryPath.Buffer, Name);
        if (Status) {
            return Status;
        }
    }

    Option->Value = Option->Default;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WnbdReloadPersistentOptions()
{
    DWORD Status = STATUS_SUCCESS;

    for (DWORD i = 0; i < WnbdOptionsCount; i++) {
        PWNBD_OPTION Option = &WnbdDriverOptions[i];
        WNBD_OPTION_VALUE PersistentValue = { 0 };
        Status = WnbdGetPersistentOpt(Option->Name, &PersistentValue);
        if (Status) {
            if (Status != STATUS_OBJECT_NAME_NOT_FOUND) {
                WNBD_LOG_WARN("Could not load option %ls. Error: 0x%x",
                              Option->Name, Status);
            }
            continue;
        }
        Status = WnbdProcessOptionValue(Option, &PersistentValue);
        if (Status) {
            WNBD_LOG_WARN("Could not process option %ls. Error: 0x%x",
                          Option->Name, Status);
            continue;
        }

        Option->Value = PersistentValue;
    }

    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdListDrvOpt(
    PWNBD_OPTION_LIST OptionList,
    PULONG BufferSize,
    BOOLEAN Persistent)
{
    ASSERT(OptionList);

    DWORD RequiredBuffSize =
        sizeof(WNBD_OPTION) * WnbdOptionsCount + sizeof(WNBD_OPTION_LIST);
    if (*BufferSize < RequiredBuffSize) {
        *BufferSize = RequiredBuffSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    OptionList->Count = 0;
    for (DWORD i = 0; i < WnbdOptionsCount; i++) {
        // When persistent options are requested, we'll only retrieve
        // the ones that are currently set.
        if (Persistent) {
            WNBD_OPTION_VALUE PersistentValue = { 0 };
            DWORD Status = WnbdGetPersistentOpt(
                WnbdDriverOptions[i].Name,
                &PersistentValue);
            if (!Status) {
                OptionList->Options[OptionList->Count] = WnbdDriverOptions[i];
                OptionList->Options[OptionList->Count].Value = PersistentValue;
                OptionList->Count += 1;
            }
        }
        else {
            OptionList->Options[i] = WnbdDriverOptions[i];
            OptionList->Count += 1;
        }
    }

    return STATUS_SUCCESS;
}

#pragma warning(pop)