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

#pragma comment(lib, "Setupapi.lib")

std::wstring to_wstring(std::string str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> strconverter;
    return strconverter.from_bytes(str);
}

void PrintSyntax()
{
    fprintf(stderr, "Syntax:\n");
    fprintf(stderr, "wnbd-client -v\n");
    fprintf(stderr, "wnbd-client map  <InstanceName> <HostName> "
                    "<PortName> <ExportName> [<SkipNBDNegotiation> "
                    "<ReadOnly> <DiskSize> <BlockSize>]\n");
    fprintf(stderr, "wnbd-client unmap <InstanceName> [HardRemove]\n");
    fprintf(stderr, "wnbd-client list \n");
    fprintf(stderr, "wnbd-client set-debug <DebugMode>\n");
    fprintf(stderr, "wnbd-client stats <InstanceName>\n");
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

// We're doing this check quite often, so this little helper comes in handy.
void CheckOpenFailed(DWORD Status)
{
    if (Status == ERROR_OPEN_FAILED) {
        fprintf(stderr,
                "Could not open WNBD device. Make sure that the driver "
                "is installed.\n");
    }
}

DWORD CmdMap(
    PCHAR InstanceName,
    PCHAR HostName,
    DWORD PortNumber,
    PCHAR ExportName,
    UINT64 DiskSize,
    UINT32 BlockSize,
    BOOLEAN SkipNegotiation,
    BOOLEAN ReadOnly)
{
    if (!PortNumber) {
        fprintf(stderr, "Missing NBD server port number.\n");
    }
    if (SkipNegotiation && !(BlockSize && DiskSize)) {
        fprintf(stderr,
                "The disk size and block size must be provided when "
                "skipping NBD negotiation.\n");
    }

    WNBD_PROPERTIES Props = { 0 };
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenDevice(&WnbdDriverHandle);
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
    Status = WnbdIoctlCreate(WnbdDriverHandle, &Props, &ConnectionInfo);
    if (Status) {
        fprintf(stderr, "Could not create mapping.\n");
        PrintFormattedError(Status);
    }

    CloseHandle(WnbdDriverHandle);
    return Status;
}


DWORD CmdUnmap(PCHAR InstanceName, BOOLEAN HardRemove)
{
    DWORD Status = WnbdRemoveEx(InstanceName, HardRemove);
    if (Status) {
        CheckOpenFailed(Status);
        fprintf(stderr, "Could not disconnect WNBD device.\n");
        PrintFormattedError(Status);
    }
    return Status;
}

DWORD CmdStats(PCHAR InstanceName)
{
    WNBD_DRV_STATS Stats = {0};
    DWORD Status = WnbdGetDriverStats(InstanceName, &Stats);
    if (Status) {
        CheckOpenFailed(Status);
        fprintf(stderr, "Could not get IO stats.\n");
        PrintFormattedError(Status);
        return Status;
    }

    printf("Device stats:\n");
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
        CheckOpenFailed(Status);
        fprintf(stderr, "Could not get connection list.\n");
        PrintFormattedError(Status);

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

    // This must be called only once.
    HRESULT hres = WnbdCoInitializeBasic();
    if (FAILED(hres)) {
        fprintf(stderr, "Failed to initialize COM. HRESULT: 0x%x.\n", hres);
        free(ConnList);
        return HRESULT_CODE(hres);
    }

    DWORD Status = 0;
    printf("%-10s  %-10s  %-5s  %-15s  %s\n", "Pid", "DiskNumber", "Nbd", "Owner", "InstanceName");
    for (ULONG index = 0; index < ConnList->Count; index++) {
        std::wstring SerialNumberW = to_wstring(
            ConnList->Connections[index].Properties.SerialNumber);
        DWORD DiskNumber = -1;
        hres = WnbdGetDiskNumberBySerialNumber(
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

DWORD CmdRaiseLogLevel(UINT32 LogLevel)
{
    DWORD Status = WnbdRaiseLogLevel(LogLevel);
    if (Status) {
        CheckOpenFailed(Status);
        fprintf(stderr, "Could not get connection list.\n");
        PrintFormattedError(Status);
    }
    return Status;
}

DWORD CmdVersion() {
    printf("wnbd-client.exe: %s\n", WNBD_VERSION_STR);

    WNBD_VERSION Version = { 0 };
    WnbdGetLibVersion(&Version);
    printf("libwnbd.dll: %s\n", Version.Description);

    Version = { 0 };
    DWORD Status = WnbdGetDriverVersion(&Version);
    if (Status) {
        fprintf(stderr, "Could not get WNBD driver version.\n");
        PrintFormattedError(Status);
    }
    else {
        printf("wnbd.sys: %s\n", Version.Description);
    }

    return Status;
}
