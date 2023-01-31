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

extern HANDLE GlobalDrvRegHandle;

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

    RtlZeroMemory(&Value->Data, sizeof(Value->Data));

    BYTE Buff[
        sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
        (WNBD_MAX_NAME_LENGTH * sizeof(WCHAR))] = { 0 };
    PKEY_VALUE_PARTIAL_INFORMATION ValueInformation = (
        PKEY_VALUE_PARTIAL_INFORMATION) Buff;

    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(Name);
    ULONG RequiredBufferSize = 0;
    NTSTATUS Status = ZwQueryValueKey(
        GlobalDrvRegHandle, &KeyName, KeyValuePartialInformation,
        ValueInformation,
        sizeof(Buff), &RequiredBufferSize);
    if (Status) {
        WNBD_LOG_WARN("Couldn't retrieve registry key. Status: %d", Status);
        return Status;
    }

    if (WnbdOptRegType(Option->Type) != ValueInformation->Type) {
        WNBD_LOG_WARN(
            "Registry value type mismatch. Expecting: %d, retrieved: %d",
            Option->Type, ValueInformation->Type);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    switch(Option->Type) {
    case WnbdOptBool:
        // We're storing bool and int64 values as QWORD
        if (ValueInformation->DataLength != sizeof(UINT64)) {
            WNBD_LOG_WARN(
                "Registry value size mismatch. Expecting: %d, actual: %d",
                sizeof(UINT64), ValueInformation->DataLength);
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        Value->Data.AsBool = !!*(PUINT64)ValueInformation->Data;
        break;
    case WnbdOptInt64:
        if (ValueInformation->DataLength != sizeof(UINT64)) {
            WNBD_LOG_WARN(
                "Registry value size mismatch. Expecting: %d, actual: %d",
                sizeof(UINT64), ValueInformation->DataLength);
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        Value->Data.AsInt64 = *(PUINT64)ValueInformation->Data;
        break;
    case WnbdOptWstr:
        if (ValueInformation->DataLength > sizeof(Value->Data.AsWstr)) {
            WNBD_LOG_WARN(
                "Registry value size overflow. Maximum allowed: %d, actual: %d",
                sizeof(Value->Data.AsWstr), ValueInformation->DataLength);
            return STATUS_BUFFER_OVERFLOW;
        }
        RtlCopyMemory(Value->Data.AsWstr, ValueInformation->Data,
                      ValueInformation->DataLength);
        break;
    default:
        WNBD_LOG_WARN("Unsupported option type: %d", Option->Type);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

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
        UNICODE_STRING KeyName = RTL_CONSTANT_STRING(Name);
        Status = ZwSetValueKey(
            GlobalDrvRegHandle,
            &KeyName,
            0,
            WnbdOptRegType(Option->Type),
            (PVOID)&Value->Data,
            WnbdOptRegSize(Option->Type));
        if (Status) {
            WNBD_LOG_ERROR("Couln't set registry key. Status: %d", Status);
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
        UNICODE_STRING KeyName = RTL_CONSTANT_STRING(Name);
        NTSTATUS Status = ZwDeleteValueKey(
            GlobalDrvRegHandle,
            &KeyName);
        if (Status) {
            WNBD_LOG_ERROR("Couln't remove registry key. Status: %d", Status);
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
