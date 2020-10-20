/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "cmd.h"
#include "wnbd.h"
#include "nbd_protocol.h"
#include "version.h"

#include <string>
#include <codecvt>
#include <locale>
#include <sstream>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "CfgMgr32.lib")

std::wstring to_wstring(std::string str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> strconverter;
    return strconverter.from_bytes(str);
}

std::string to_string(std::wstring wstr)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> strconverter;
    return strconverter.to_bytes(wstr);
}

std::string OptValToString(PWNBD_OPTION_VALUE Value)
{
    std::ostringstream stream;
    switch(Value->Type) {
    case WnbdOptBool:
        stream << (Value->Data.AsBool ? "true" : "false");
        break;
    case WnbdOptInt64:
        stream << Value->Data.AsInt64;
        break;
    case WnbdOptWstr:
        stream << to_string(Value->Data.AsWstr);
        break;
    default:
        stream << "UNKNOWN TYPE (" << Value->Type << ")";
        break;
    }

    return stream.str();
}

void PrintFormattedError(DWORD Error)
{
    LPVOID LpMsgBuf;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        Error,
        0,
        (LPTSTR)&LpMsgBuf,
        0,
        NULL
    );

    fprintf(stderr, "Error code: %d. Error message: %s\n",
            Error, (LPTSTR)LpMsgBuf);

    LocalFree(LpMsgBuf);
}

void PrintLastError()
{
    PrintFormattedError(GetLastError());
}

DWORD CmdMap(
    PCSTR InstanceName,
    PCSTR HostName,
    DWORD PortNumber,
    PCSTR ExportName,
    UINT64 DiskSize,
    UINT32 BlockSize,
    BOOLEAN SkipNegotiation,
    BOOLEAN ReadOnly)
{
    if (!PortNumber) {
        fprintf(stderr, "Missing NBD server port number.\n");
        return ERROR_INVALID_PARAMETER;
    }
    if (SkipNegotiation && !(BlockSize && DiskSize)) {
        fprintf(stderr,
                "The disk size and block size must be provided when "
                "skipping NBD negotiation.\n");
        return ERROR_INVALID_PARAMETER;
    }
    if (!strlen(ExportName)) {
        ExportName = InstanceName;
    }
    if (!strlen(InstanceName)) {
        fprintf(stderr, "Missing instace name.\n");
        return ERROR_INVALID_PARAMETER;
    }
    if (!strlen(HostName)) {
        fprintf(stderr, "Missing NBD hostname.\n");
        return ERROR_INVALID_PARAMETER;
    }

    WNBD_PROPERTIES Props = { 0 };
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&WnbdDriverHandle);
    if (Status) {
        fprintf(
            stderr,
            "Could not open WNBD device. Make sure that the driver "
            "is installed.\n");
        return Status;
    }

    memcpy(&Props.InstanceName, InstanceName,
        min(strlen(InstanceName) + 1, WNBD_MAX_NAME_LENGTH));
    memcpy(&Props.SerialNumber, InstanceName,
        min(strlen(InstanceName) + 1, WNBD_MAX_NAME_LENGTH));
    memcpy(&Props.Owner, WNBD_CLI_OWNER_NAME,
        strlen(WNBD_CLI_OWNER_NAME));

    memcpy(&Props.NbdProperties.Hostname, HostName,
        min(strlen(HostName) + 1, WNBD_MAX_NAME_LENGTH));
    memcpy(&Props.NbdProperties.ExportName, ExportName,
        min(strlen(ExportName) + 1, WNBD_MAX_NAME_LENGTH));
    Props.NbdProperties.PortNumber = PortNumber;
    Props.NbdProperties.Flags.SkipNegotiation = SkipNegotiation;

    Props.Flags.UseNbd = TRUE;
    Props.Flags.ReadOnly = ReadOnly;

    Props.Pid = _getpid();
    Props.BlockSize = BlockSize;
    Props.BlockCount = BlockSize ? DiskSize / BlockSize : 0;

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    Status = WnbdIoctlCreate(WnbdDriverHandle, &Props, &ConnectionInfo, NULL);

    CloseHandle(WnbdDriverHandle);
    return Status;
}

DWORD CmdUnmap(PCSTR InstanceName, BOOLEAN HardRemove)
{
    WNBD_REMOVE_OPTIONS RemoveOptions = {0};
    RemoveOptions.Flags.HardRemove = HardRemove;

    // TODO: make those configurable. We should use named arguments first.
    RemoveOptions.Flags.HardRemoveFallback = TRUE;
    RemoveOptions.SoftRemoveTimeoutMs = WNBD_DEFAULT_RM_TIMEOUT_MS;
    RemoveOptions.SoftRemoveRetryIntervalMs = WNBD_DEFAULT_RM_RETRY_INTERVAL_MS;

    DWORD Status = WnbdRemoveEx(InstanceName, &RemoveOptions);
    return Status;
}

DWORD CmdStats(PCSTR InstanceName)
{
    WNBD_DRV_STATS Stats = {0};
    DWORD Status = WnbdGetDriverStats(InstanceName, &Stats);
    if (Status) {
        return Status;
    }

    printf("Disk stats:\n");
    printf("TotalReceivedIORequests: %llu\n", Stats.TotalReceivedIORequests);
    printf("TotalSubmittedIORequests: %llu\n", Stats.TotalSubmittedIORequests);
    printf("TotalReceivedIOReplies: %llu\n", Stats.TotalReceivedIOReplies);
    printf("UnsubmittedIORequests: %llu\n", Stats.UnsubmittedIORequests);
    printf("PendingSubmittedIORequests: %llu\n", Stats.PendingSubmittedIORequests);
    printf("AbortedSubmittedIORequests: %llu\n", Stats.AbortedSubmittedIORequests);
    printf("AbortedUnsubmittedIORequests: %llu\n", Stats.AbortedUnsubmittedIORequests);
    printf("CompletedAbortedIORequests: %llu\n", Stats.CompletedAbortedIORequests);
    printf("OutstandingIOCount: %llu\n", Stats.OutstandingIOCount);
    return Status;
}

DWORD GetList(PWNBD_CONNECTION_LIST* ConnectionList)
{
    DWORD CurrentBufferSize = 0;
    DWORD BufferSize = 0;
    DWORD Status = 0;
    PWNBD_CONNECTION_LIST TempList = NULL;

    // We're using a loop because other connections may show up by the time
    // we retry.
    do {
        if (TempList)
            free(TempList);

        if (BufferSize) {
            TempList = (PWNBD_CONNECTION_LIST) calloc(1, BufferSize);
            if (!TempList) {
                fprintf(stderr, "Could not allocate %d bytes.\n", BufferSize);
                Status = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
        }

        CurrentBufferSize = BufferSize;
        // If the buffer is too small, the return value is 0 and "BufferSize"
        // will contain the required size. This is counterintuitive, but
        // Windows drivers can't return a buffer as well as a non-zero status.
        Status = WnbdList(TempList, &BufferSize);
        if (Status)
            break;
    } while (CurrentBufferSize < BufferSize);

    if (Status) {
        if (TempList)
            free(TempList);
    }
    else {
        *ConnectionList = TempList;
    }
    return Status;
}

// TODO: add CmdShow
DWORD CmdList()
{
    PWNBD_CONNECTION_LIST ConnList = NULL;
    DWORD err = GetList(&ConnList);
    if (err) {
        return err;
    }

    DWORD Status = 0;
    printf("%-10s  %-10s  %-5s  %-15s  %s\n", "Pid", "DiskNumber", "Nbd", "Owner", "InstanceName");
    for (ULONG index = 0; index < ConnList->Count; index++) {
        std::wstring SerialNumberW = to_wstring(
            ConnList->Connections[index].Properties.SerialNumber);
        DWORD DiskNumber = -1;
        HRESULT hres = WnbdGetDiskNumberBySerialNumber(
            SerialNumberW.c_str(), &DiskNumber);
        if (FAILED(hres)) {
            fprintf(stderr,
                    "Warning: Could not retrieve disk number for serial '%ls'. "
                    "HRESULT: 0x%x.\n", SerialNumberW.c_str(), hres);
            Status = HRESULT_CODE(hres);
        }
        printf("%-10d  %-10d  %-5s  %-15s  %s\n",
               ConnList->Connections[index].Properties.Pid,
               DiskNumber,
               ConnList->Connections[index].Properties.Flags.UseNbd ? "true" : "false",
               ConnList->Connections[index].Properties.Owner,
               ConnList->Connections[index].Properties.InstanceName);
    }
    free(ConnList);
    return Status;
}

DWORD CmdVersion() {
    printf("wnbd-client.exe: %s\n", WNBD_VERSION_STR);

    WNBD_VERSION Version = { 0 };
    WnbdGetLibVersion(&Version);
    printf("libwnbd.dll: %s\n", Version.Description);

    Version = { 0 };
    DWORD Status = WnbdGetDriverVersion(&Version);
    if (!Status) {
        printf("wnbd.sys: %s\n", Version.Description);
    }

    return Status;
}

DWORD
CmdGetOpt(const char* Name, BOOLEAN Persistent)
{
    WNBD_OPTION_VALUE Value;
    DWORD Status = WnbdGetDrvOpt(Name, &Value, Persistent);
    if (Status) {
        return Status;
    }

    printf("Name: %s\n", Name);
    printf("Value: %s\n", OptValToString(&Value).c_str());
    return Status;
}

DWORD
CmdSetOpt(const char* Name, const char* Value, BOOLEAN Persistent)
{
    WNBD_OPTION_VALUE OptValue = { WnbdOptWstr };
    to_wstring(Value).copy((PWCHAR)&OptValue.Data.AsWstr, WNBD_MAX_NAME_LENGTH);
    return WnbdSetDrvOpt(Name, &OptValue, Persistent);
}

DWORD
CmdResetOpt(const char* Name, BOOLEAN Persistent)
{
    return WnbdResetDrvOpt(Name, Persistent);
}

DWORD GetOptList(PWNBD_OPTION_LIST* OptionList, BOOLEAN Persistent)
{
    DWORD CurrentBufferSize = 0;
    DWORD BufferSize = 0;
    DWORD Status = 0;
    PWNBD_OPTION_LIST TempList = NULL;

    do {
        if (TempList)
            free(TempList);

        if (BufferSize) {
            TempList = (PWNBD_OPTION_LIST) calloc(1, BufferSize);
            if (!TempList) {
                fprintf(stderr, "Could not allocate %d bytes.\n", BufferSize);
                Status = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
        }

        CurrentBufferSize = BufferSize;
        // If the buffer is too small, the return value is 0 and "BufferSize"
        // will contain the required size.
        Status = WnbdListDrvOpt(TempList, &BufferSize, Persistent);
        if (Status)
            break;
    } while (CurrentBufferSize < BufferSize);

    if (Status) {
        if (TempList)
            free(TempList);
    }
    else {
        *OptionList = TempList;
    }
    return Status;
}

DWORD
CmdListOpt(BOOLEAN Persistent)
{
    PWNBD_OPTION_LIST OptList = NULL;
    DWORD Status = GetOptList(&OptList, Persistent);
    if (Status) {
        return Status;
    }

    for (ULONG index = 0; index < OptList->Count; index++) {
        PWNBD_OPTION Option = &OptList->Options[index];
        printf(
            "%s: %s (Default: %s)\n",
            to_string(Option->Name).c_str(),
            OptValToString(&Option->Value).c_str(),
            OptValToString(&Option->Default).c_str());
    }

    free(OptList);
    return 0;
}
