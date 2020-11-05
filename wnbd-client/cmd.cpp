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
#include <locale>
#include <sstream>
#include <iostream>
#include <iomanip>

#include <boost/locale/encoding_utf.hpp>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "CfgMgr32.lib")

using boost::locale::conv::utf_to_utf;
using namespace std;

std::wstring to_wstring(const std::string& str)
{
  return utf_to_utf<wchar_t>(str.c_str(), str.c_str() + str.size());
}

std::string to_string(const std::wstring& str)
{
  return utf_to_utf<char>(str.c_str(), str.c_str() + str.size());
}

wstring OptValToWString(PWNBD_OPTION_VALUE Value)
{
    wostringstream stream;
    switch(Value->Type) {
    case WnbdOptBool:
        stream << (Value->Data.AsBool ? L"true" : L"false");
        break;
    case WnbdOptInt64:
        stream << Value->Data.AsInt64;
        break;
    case WnbdOptWstr:
        stream << wstring(Value->Data.AsWstr);
        break;
    default:
        stream << L"UNKNOWN TYPE (" << Value->Type << ")";
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

    cerr << "Error code: " << Error
         << ". Error message: " << LpMsgBuf << endl,

    LocalFree(LpMsgBuf);
}

void PrintLastError()
{
    PrintFormattedError(GetLastError());
}

DWORD CmdMap(
    string InstanceName,
    string HostName,
    DWORD PortNumber,
    string ExportName,
    UINT64 DiskSize,
    UINT32 BlockSize,
    BOOLEAN SkipNegotiation,
    BOOLEAN ReadOnly)
{
    if (!PortNumber) {
        cerr << "Missing NBD server port number." << endl;
        return ERROR_INVALID_PARAMETER;
    }
    if (PortNumber > 65353) {
        cerr << "Invalid NBD server port number: " << PortNumber << endl;
        return ERROR_INVALID_PARAMETER;
    }
    if (SkipNegotiation && !(BlockSize && DiskSize)) {
        cerr << "The disk size and block size must be provided when "
                "skipping NBD negotiation" << endl;
        return ERROR_INVALID_PARAMETER;
    }
    if (ExportName.empty()) {
        ExportName = InstanceName;
    }
    if (InstanceName.empty()) {
        cerr << "Missing instace name." << endl;
        return ERROR_INVALID_PARAMETER;
    }
    if (HostName.empty()) {
        cerr << "Missing NBD hostname." << endl;
        return ERROR_INVALID_PARAMETER;
    }

    WNBD_PROPERTIES Props = { 0 };
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&WnbdDriverHandle);
    if (Status) {
        cerr << "Could not open WNBD device. Make sure that the driver "
                "is installed." << endl;
        return Status;
    }

    InstanceName.copy((char*)&Props.InstanceName, WNBD_MAX_NAME_LENGTH);
    InstanceName.copy((char*)&Props.SerialNumber, WNBD_MAX_NAME_LENGTH);
    string(WNBD_CLI_OWNER_NAME).copy((char*)&Props.Owner, WNBD_MAX_OWNER_LENGTH);

    HostName.copy((char*)&Props.NbdProperties.Hostname, WNBD_MAX_NAME_LENGTH);
    ExportName.copy((char*)&Props.NbdProperties.ExportName, WNBD_MAX_NAME_LENGTH);

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

DWORD CmdUnmap(
    string InstanceName,
    BOOLEAN HardRemove,
    BOOLEAN NoHardDisonnectFallback,
    DWORD SoftDisconnectTimeout,
    DWORD SoftDisconnectRetryInterval)
{
    WNBD_REMOVE_OPTIONS RemoveOptions = {0};
    RemoveOptions.Flags.HardRemove = HardRemove;

    RemoveOptions.Flags.HardRemoveFallback = !NoHardDisonnectFallback;
    RemoveOptions.SoftRemoveTimeoutMs = SoftDisconnectTimeout * 1000;
    RemoveOptions.SoftRemoveRetryIntervalMs = SoftDisconnectRetryInterval * 1000;

    DWORD Status = WnbdRemoveEx(InstanceName.c_str(), &RemoveOptions);
    return Status;
}

DWORD CmdStats(string InstanceName)
{
    WNBD_DRV_STATS Stats = {0};
    DWORD Status = WnbdGetDriverStats(InstanceName.c_str(), &Stats);
    if (Status) {
        return Status;
    }

    cout << "Disk stats" << endl << left
         << setw(30) << "TotalReceivedIORequests" << " : " <<  Stats.TotalReceivedIORequests << endl
         << setw(30) << "TotalSubmittedIORequests" << " : " <<  Stats.TotalSubmittedIORequests << endl
         << setw(30) << "TotalReceivedIOReplies" << " : " <<  Stats.TotalReceivedIOReplies << endl
         << setw(30) << "UnsubmittedIORequests" << " : " <<  Stats.UnsubmittedIORequests << endl
         << setw(30) << "PendingSubmittedIORequests" << " : " <<  Stats.PendingSubmittedIORequests << endl
         << setw(30) << "AbortedSubmittedIORequests" << " : " <<  Stats.AbortedSubmittedIORequests << endl
         << setw(30) << "AbortedUnsubmittedIORequests" << " : " <<  Stats.AbortedUnsubmittedIORequests << endl
         << setw(30) << "CompletedAbortedIORequests" << " : " <<  Stats.CompletedAbortedIORequests << endl
         << setw(30) << "OutstandingIOCount" << " : " <<  Stats.OutstandingIOCount << endl
         << endl;
    return Status;
}

DWORD CmdShow(string InstanceName)
{
    WNBD_CONNECTION_INFO ConnInfo = {0};
    DWORD Status = WnbdShow(InstanceName.c_str(), &ConnInfo);
    if (Status) {
        return Status;
    }

    cout << "Connection info" << endl << left
         << setw(25) << "InstanceName" << " : " <<  ConnInfo.Properties.InstanceName << endl
         << setw(25) << "SerialNumber" << " : " <<  ConnInfo.Properties.SerialNumber << endl
         << setw(25) << "Owner" << " : " <<  ConnInfo.Properties.Owner << endl
         << setw(25) << "ReadOnly" << " : " <<  ConnInfo.Properties.Flags.ReadOnly << endl
         << setw(25) << "FlushSupported" << " : " <<  ConnInfo.Properties.Flags.FlushSupported << endl
         << setw(25) << "FUASupported" << " : " <<  ConnInfo.Properties.Flags.FUASupported << endl
         << setw(25) << "UnmapSupported" << " : " <<  ConnInfo.Properties.Flags.UnmapSupported << endl
         << setw(25) << "UnmapAnchorSupported " << " : "
                     << ConnInfo.Properties.Flags.UnmapAnchorSupported << endl
         << setw(25) << "UseNbd" << " : " <<  ConnInfo.Properties.Flags.UseNbd << endl
         << setw(25) << "BlockCount" << " : " <<  ConnInfo.Properties.BlockCount << endl
         << setw(25) << "BlockSize" << " : " <<  ConnInfo.Properties.BlockSize << endl
         << setw(25) << "MaxUnmapDescCount" << " : " <<  ConnInfo.Properties.MaxUnmapDescCount << endl
         << setw(25) << "Pid" << " : " <<  ConnInfo.Properties.Pid << endl
         << setw(25) << "DiskNumber" << " : " <<  ConnInfo.DiskNumber << endl
         << setw(25) << "PNPDeviceID" << " : " <<  to_string(wstring(ConnInfo.PNPDeviceID)) << endl
         << endl;

    if (ConnInfo.Properties.Flags.UseNbd) {
        cout << "Nbd properties" << endl << left
             << setw(25) << "Hostname" << " : " << ConnInfo.Properties.NbdProperties.Hostname << endl
             << setw(25) << "PortNumber" << " : " << ConnInfo.Properties.NbdProperties.PortNumber << endl
             << setw(25) << "ExportName" << " : " << ConnInfo.Properties.NbdProperties.ExportName << endl
             << setw(25) << "SkipNegotiation" << " : "
                         << ConnInfo.Properties.NbdProperties.Flags.SkipNegotiation << endl
             << endl;
    }

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
                cerr << "Could not allocate " << BufferSize << " bytes." << endl;
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

DWORD CmdList()
{
    PWNBD_CONNECTION_LIST ConnList = NULL;
    DWORD err = GetList(&ConnList);
    if (err) {
        return err;
    }

    DWORD Status = 0;
    cout << left
         << setw(10) << "Pid" << "  "
         << setw(10) << "DiskNumber" << "  "
         << setw(5) << "Nbd" << "  "
         << setw(15) << "Owner" << "  "
         << "InstanceName" << endl;

    for (ULONG index = 0; index < ConnList->Count; index++) {
        cout << left
             << setw(10) << ConnList->Connections[index].Properties.Pid << "  "
             << setw(10) << ConnList->Connections[index].DiskNumber << "  "
             << setw(5) << (ConnList->Connections[index].Properties.Flags.UseNbd
                            ? "true" : "false") << "  "
             << setw(15) << ConnList->Connections[index].Properties.Owner << "  "
             << ConnList->Connections[index].Properties.InstanceName << endl;
    }
    free(ConnList);
    return Status;
}

DWORD CmdVersion()
{
    cout << "wnbd-client.exe: " << WNBD_VERSION_STR << endl;

    WNBD_VERSION Version = { 0 };
    WnbdGetLibVersion(&Version);
    cout << "libwnbd.dll: " << Version.Description << endl;

    Version = { 0 };
    DWORD Status = WnbdGetDriverVersion(&Version);
    if (!Status) {
        cout << "wnbd.sys: " << Version.Description << endl;
    }

    return Status;
}

DWORD
CmdGetOpt(string Name, BOOLEAN Persistent)
{
    WNBD_OPTION_VALUE Value;
    DWORD Status = WnbdGetDrvOpt(Name.c_str(), &Value, Persistent);
    if (Status) {
        return Status;
    }

    wcout << L"Name: " << to_wstring(Name) << endl
          << L"Value: " << OptValToWString(&Value).c_str() << endl;
    return Status;
}

DWORD
CmdSetOpt(string Name, string Value, BOOLEAN Persistent)
{
    WNBD_OPTION_VALUE OptValue = { WnbdOptWstr };
    to_wstring(Value).copy((PWCHAR)&OptValue.Data.AsWstr, WNBD_MAX_NAME_LENGTH);
    return WnbdSetDrvOpt(Name.c_str(), &OptValue, Persistent);
}

DWORD
CmdResetOpt(string Name, BOOLEAN Persistent)
{
    return WnbdResetDrvOpt(Name.c_str(), Persistent);
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
                cerr << "Could not allocate " << BufferSize << " bytes." << endl;
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

    size_t Width = 0;
    for (ULONG index = 0; index < OptList->Count; index++) {
        PWNBD_OPTION Option = &OptList->Options[index];
        Width = max(Width, wcslen(Option->Name));
    }

    for (ULONG index = 0; index < OptList->Count; index++) {
        PWNBD_OPTION Option = &OptList->Options[index];
        wcout << left << setw(Width) << Option->Name
              << L" : "
              << OptValToWString(&Option->Value)
              << L" (Default: "
              << OptValToWString(&Option->Default)
              << L")" << endl;
    }

    free(OptList);
    return 0;
}

DWORD
CmdUninstall()
{
    BOOL RebootRequired = FALSE;
    DWORD Status = WnbdUninstallDriver(&RebootRequired);
    if (RebootRequired) {
        cerr << "Reboot required to complete the cleanup process"
             << endl;
    }

    return Status;
}

DWORD
CmdInstall(std::string FileName)
{
    BOOL RebootRequired = FALSE;
    DWORD Status = WnbdInstallDriver(FileName.c_str(), &RebootRequired);
    if (RebootRequired) {
        cerr << "Reboot required to complete the installation"
             << endl;
    }

    return Status;
}
