/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <windows.h>
#include <windef.h>
#include <winioctl.h>
#include <winioctl.h>
#include <newdev.h>
#include <ntddscsi.h>
#include "Shlwapi.h"
#include <setupapi.h>
#include <string.h>
#include <infstr.h>

#include "wnbd.h"
#include "wnbd_log.h"
#include "utils.h"

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Newdev.lib")
#pragma comment(lib, "Shlwapi.lib")

#define STRING_OVERFLOWS(Str, MaxLen) (strlen(Str + 1) > MaxLen)

// Open the WNBD SCSI adapter device.
DWORD WnbdOpenAdapterEx(PHANDLE Handle, PDEVINST CMDeviceInstance)
{
    HDEVINFO DevInfo = { 0 };
    SP_DEVINFO_DATA DevInfoData = { 0 };
    DevInfoData.cbSize = sizeof(DevInfoData);
    SP_DEVICE_INTERFACE_DATA DevInterfaceData = { 0 };
    PSP_DEVICE_INTERFACE_DETAIL_DATA DevInterfaceDetailData = NULL;
    ULONG DevIndex = 0;
    ULONG RequiredSize = 0;
    ULONG ErrorCode = 0;
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;

    DevInfo = SetupDiGetClassDevs(&WNBD_GUID, NULL, NULL,
                                  DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE) {
        ErrorCode = ERROR_OPEN_FAILED;
        goto Exit;
    }

    DevInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    DevIndex = 0;

    if (!SetupDiEnumDeviceInterfaces(DevInfo, NULL, &WNBD_GUID,
                                     DevIndex++, &DevInterfaceData)) {
        ErrorCode = GetLastError();
        goto Exit;
    }

    if (!SetupDiGetDeviceInterfaceDetail(DevInfo, &DevInterfaceData, NULL,
                                         0, &RequiredSize, NULL)) {
        ErrorCode = GetLastError();
        if (ErrorCode && ERROR_INSUFFICIENT_BUFFER != ErrorCode) {
            goto Exit;
        }
        else {
            ErrorCode = 0;
        }
    }

    DevInterfaceDetailData =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(RequiredSize);
    if (!DevInterfaceDetailData) {
        ErrorCode = ERROR_BUFFER_OVERFLOW;
        goto Exit;
    }
    DevInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    if (!SetupDiGetDeviceInterfaceDetail(
            DevInfo, &DevInterfaceData, DevInterfaceDetailData,
            RequiredSize, &RequiredSize, &DevInfoData))
    {
        ErrorCode = GetLastError();
        goto Exit;
    }

    WnbdDriverHandle = CreateFile(
        DevInterfaceDetailData->DevicePath,
        GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, 0);
    if (INVALID_HANDLE_VALUE == WnbdDriverHandle) {
        ErrorCode = GetLastError();
        goto Exit;
    }

Exit:
    if (DevInterfaceDetailData) {
        free(DevInterfaceDetailData);
    }
    if (DevInfo) {
        SetupDiDestroyDeviceInfoList(DevInfo);
    }

    if (!ErrorCode) {
        *Handle = WnbdDriverHandle;
        *CMDeviceInstance = DevInfoData.DevInst;
    }
    else {
        if (ErrorCode == ERROR_ACCESS_DENIED) {
            LogError(
                "Could not open WNBD adapter device. Access denied, try "
                "using an elevated command prompt.");
        }
        else {
            LogError(
                "Could not open WNBD adapter device. Please make sure that "
                "the driver is installed. Error: %d. Error message: %s",
                ErrorCode, win32_strerror(ErrorCode).c_str());
        }
    }
    return ErrorCode;
}

// Open the WNBD SCSI adapter device.
DWORD WnbdOpenAdapter(PHANDLE Handle)
{
    DEVINST DevInst = { 0 };
    return WnbdOpenAdapterEx(Handle, &DevInst);
}

DWORD RemoveWnbdInf(_In_ LPCTSTR InfName)
{
    DWORD Status = 0;
    UINT ErrorLine;

    HINF HandleInf = SetupOpenInfFile(InfName, NULL, INF_STYLE_WIN4, &ErrorLine);
    if (HandleInf == INVALID_HANDLE_VALUE) {
        Status = GetLastError();
        LogError("SetupOpenInfFile failed with "
            "error: %d, at line. Error message: %s",
            ErrorLine, Status, win32_strerror(Status).c_str());
        goto failed;
    }

    INFCONTEXT Context;
    if (!SetupFindFirstLine(HandleInf, INFSTR_SECT_VERSION, INFSTR_KEY_CATALOGFILE, &Context)) {
        Status = GetLastError();
        LogError("SetupFindFirstLine failed with "
            "error: %d. Error message: %s", Status, win32_strerror(Status).c_str());
        goto failed;
    }

    TCHAR InfData[MAX_INF_STRING_LENGTH];
    if (!SetupGetStringField(&Context, 1, InfData, ARRAYSIZE(InfData), NULL)) {
        Status = GetLastError();
        LogError("SetupGetStringField failed with "
            "error: %d. Error message: %s", Status, win32_strerror(Status).c_str());
        goto failed;
    }

    /* Match the OEM inf file based on the catalog string wnbd.cat */
    if (!wcscmp(InfData, L"wnbd.cat")) {
        std::wstring SearchString(InfName);
        if (!SetupUninstallOEMInf(
            SearchString.substr(SearchString.find_last_of(L"\\") + 1).c_str(), SUOI_FORCEDELETE, 0)) {
            Status = GetLastError();
            LogError("SetupUninstallOEMInfA failed with "
                "error: %d. Error message: %s", Status, win32_strerror(Status).c_str());
        }
    }

failed:
    if (HandleInf != INVALID_HANDLE_VALUE) {
        SetupCloseInfFile(HandleInf);
    }

    return Status;
}

/* Cycle through all OEM inf files and try a best effort mode to remove all
 * files which include the string wnbd.cat */
DWORD CleanDrivers()
{
    DWORD Status = 0;
    TCHAR OemName[MAX_PATH];
    HANDLE DirHandle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATA OemFindData;

    if (!GetWindowsDirectory(OemName, ARRAYSIZE(OemName))) {
        Status = GetLastError();
        LogError("GetWindowsDirectory failed with "
            "error: %d. Error message: %s", Status, win32_strerror(Status).c_str());
        return Status;
    }
    if (wcscat_s(OemName, ARRAYSIZE(OemName), L"\\INF\\OEM*.INF")) {
        LogError("Couldn't process path: %ls.", OemName);
        return ERROR_BAD_PATHNAME;
    }
    DirHandle = FindFirstFile(OemName, &OemFindData);
    if (DirHandle == INVALID_HANDLE_VALUE) {
        /* No OEM files */
        return Status;
    }

    do {
        Status = RemoveWnbdInf(OemFindData.cFileName);
        if (Status) {
            LogError("Failed while trying to remove OEM file: %ls",
                OemFindData.cFileName);
            break;
        }
    } while (FindNextFile(DirHandle, &OemFindData));

    FindClose(DirHandle);
    return Status;
}

DWORD RemoveWnbdDevice(HDEVINFO AdapterDevHandle, PSP_DEVINFO_DATA DevInfoData, PBOOL RebootRequired)
{
    SP_DRVINFO_DETAIL_DATA_A DrvDetailData = { 0 };
    DrvDetailData.cbSize = sizeof DrvDetailData;
    DWORD Status = ERROR_SUCCESS;

    /* Queue the device for removal before trying to remove the OEM information file */
    if (!DiUninstallDevice(0, AdapterDevHandle, DevInfoData, 0, RebootRequired)) {
        Status = GetLastError();
        LogError(
            "Could not remove driver. "
            "Error: %d. Error message: %s", Status, win32_strerror(Status).c_str());
    }

    return Status;
}

static DWORD FindWnbdAdapterDevice(HDEVINFO* AdapterDevHandle, SP_DEVINFO_DATA* DevInfoData)
{
    CHAR TempBuf[2048];
    memset(TempBuf, 0, sizeof(TempBuf));
    BOOL Found = FALSE;
    DWORD Status = ERROR_FILE_NOT_FOUND;

    *AdapterDevHandle = SetupDiGetClassDevsA(&WNBD_GUID, 0, 0, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (INVALID_HANDLE_VALUE == *AdapterDevHandle) {
        Status = GetLastError();
        LogError(
            "SetupDiGetClassDevs failed. "
            "Error: %d. Error message: %s", Status, win32_strerror(Status).c_str());
        return Status;
    }

    for (DWORD I = 0; !Found && SetupDiEnumDeviceInfo(*AdapterDevHandle, I, DevInfoData); I++)
    {
        if (!SetupDiGetDeviceRegistryPropertyA(*AdapterDevHandle, DevInfoData, SPDRP_HARDWAREID, 0,
            (PBYTE)TempBuf, sizeof(TempBuf) - 1, 0)) {
            continue;
        }

        if (strstr(TempBuf, WNBD_HARDWAREID) != NULL) {
            Found = TRUE;
            break;
        }
    }

    if (!Found) {
        Status = GetLastError();
        if (ERROR_NO_MORE_ITEMS != Status) {
            LogError(
                "Failed to locate device with hardware id: %s. Error: %d. Error message: %s",
                WNBD_HARDWAREID, Status, win32_strerror(Status).c_str());
        }
        return Status;
    }

    return ERROR_SUCCESS;
}

DWORD RemoveAllDevices()
{
    DWORD BufferSize = 0;
    DWORD Status = 0;
    PWNBD_CONNECTION_LIST ConnList = NULL;

    WNBD_OPTION_VALUE OptValue = { WnbdOptBool };
    /* Disallow new mappings so we can remove all current mappings */
    OptValue.Data.AsBool = FALSE;
    Status = WnbdSetDrvOpt("NewMappingsAllowed", &OptValue, FALSE);
    if (Status) {
        goto exit;
    }

    Status = WnbdList(ConnList, &BufferSize);
    if (!BufferSize) {
        goto exit;
    }
    ConnList = (PWNBD_CONNECTION_LIST)calloc(1, BufferSize);
    if (NULL == ConnList) {
        Status = ERROR_NOT_ENOUGH_MEMORY;
        LogError(
            "Failed to allocate %d bytes.", BufferSize);
        goto exit;
    }
    Status = WnbdList(ConnList, &BufferSize);
    if (Status) {
        goto exit;
    }
    CHAR* InstanceName;
    for (ULONG index = 0; index < ConnList->Count; index++) {
        InstanceName = ConnList->Connections[index].Properties.InstanceName;
        WNBD_REMOVE_OPTIONS RemoveOptions = { 0 };
        /* TODO add parallel and soft disconnect removal */
        RemoveOptions.Flags.HardRemove = TRUE;
        Status = WnbdRemoveEx(InstanceName, &RemoveOptions);
        if (Status) {
            goto exit;
        }
    }

exit:
    if (ConnList) {
        free(ConnList);
    }
    return Status;
}

DWORD WnbdUninstallDriver(PBOOL RebootRequired)
{
    DWORD Status = ERROR_SUCCESS;
    HDEVINFO AdapterDevHandle = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA DevInfoData;
    DevInfoData.cbSize = sizeof DevInfoData;

    PWNBD_CONNECTION_LIST ConnList = NULL;
    Status = RemoveAllDevices();
    if (Status) {
        goto exit;
    }

    while (Status == ERROR_SUCCESS) {
        Status = FindWnbdAdapterDevice(&AdapterDevHandle, &DevInfoData);
        if (ERROR_SUCCESS != Status) {
            goto exit;
        }

        Status = RemoveWnbdDevice(AdapterDevHandle, &DevInfoData, RebootRequired);

        if (!SetupDiDestroyDeviceInfoList(AdapterDevHandle)) {
            Status = GetLastError();
            LogError("SetupDiDestroyDeviceInfoList failed with "
                "error: %d. Error message: %s", Status, win32_strerror(Status).c_str());
        }
    }

exit:
    Status = CleanDrivers();
    return Status;
}

DWORD CreateWnbdAdapter(CHAR* ClassName, SP_DEVINFO_DATA* DevInfoData, HDEVINFO* AdapterDevHandle)
{
    DWORD Status = 0;

    if (INVALID_HANDLE_VALUE == (*AdapterDevHandle = SetupDiCreateDeviceInfoList(&WNBD_GUID, 0))) {
        Status = GetLastError();
        LogError(
            "SetupDiCreateDeviceInfoList failed with error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    if (!SetupDiCreateDeviceInfoA(*AdapterDevHandle, ClassName, &WNBD_GUID, 0, 0, DICD_GENERATE_ID, DevInfoData)) {
        Status = GetLastError();
        LogError(
            "SetupDiCreateDeviceInfoA failed with error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    if (!SetupDiSetDeviceRegistryPropertyA(*AdapterDevHandle, DevInfoData,
        SPDRP_HARDWAREID, (PBYTE)WNBD_HARDWAREID, WNBD_HARDWAREID_LEN)) {
        Status = GetLastError();
        LogError(
            "SetupDiSetDeviceRegistryPropertyA failed with error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    return Status;
}

DWORD InstallDriver(CHAR* FileNameBuf, HDEVINFO* AdapterDevHandle, PBOOL RebootRequired)
{
    GUID ClassGuid = { 0 };
    CHAR ClassName[MAX_CLASS_NAME_LEN];
    SP_DEVINFO_DATA DevInfoData;
    DevInfoData.cbSize = sizeof(DevInfoData);
    SP_DEVINSTALL_PARAMS_A InstallParams;
    DWORD Status = 0;

    if (!SetupDiGetINFClassA(FileNameBuf, &ClassGuid, ClassName, MAX_CLASS_NAME_LEN, 0)) {
        Status = GetLastError();
        LogError(
            "SetupDiGetINFClassA failed with error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    if (CreateWnbdAdapter(ClassName, &DevInfoData, AdapterDevHandle)) {
        return Status;
    }

    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, *AdapterDevHandle, &DevInfoData)) {
        Status = GetLastError();
        LogError(
            "SetupDiCallClassInstaller failed with error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    InstallParams.cbSize = sizeof InstallParams;
    if (!SetupDiGetDeviceInstallParamsA(*AdapterDevHandle, &DevInfoData, &InstallParams)) {
        Status = GetLastError();
        LogError(
            "SetupDiGetDeviceInstallParamsA failed with error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    if (0 != (InstallParams.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART))) {
        *RebootRequired = TRUE;
    }

    return Status;
}

DWORD WnbdInstallDriver(CONST CHAR* FileName, PBOOL RebootRequired)
{
    CHAR FullFileName[MAX_PATH];
    DWORD Status = ERROR_SUCCESS;
    HDEVINFO AdapterDevHandle = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA DevInfoData;
    DevInfoData.cbSize = sizeof DevInfoData;

    if (0 == GetFullPathNameA(FileName, MAX_PATH, FullFileName, 0)) {
        Status = GetLastError();
        LogError(
            "Invalid file path: %s. Error: %d. Error message: %s",
            FileName, Status, win32_strerror(Status).c_str());
        goto exit;
    }
    if (FALSE == PathFileExistsA(FullFileName)) {
        LogError("Could not find file: %s.", FullFileName);
        Status = ERROR_FILE_NOT_FOUND;
        goto exit;
    }

    // We assume that an installed driver has an WNBD device
    if (ERROR_SUCCESS == FindWnbdAdapterDevice(&AdapterDevHandle, &DevInfoData)) {
        LogError("Driver already installed");
        Status = ERROR_DUPLICATE_FOUND;
        goto exit;
    }

    Status = InstallDriver(FullFileName, &AdapterDevHandle, RebootRequired);
    if (ERROR_SUCCESS != Status) {
        LogError(
            "Failed to install driver. Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        goto exit;
    }

    if (!UpdateDriverForPlugAndPlayDevicesA(0, WNBD_HARDWAREID, FullFileName, 0, RebootRequired)) {
        Status = GetLastError();
        LogError(
            "UpdateDriverForPlugAndPlayDevicesA failed with error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        goto exit;
    }

exit:
    return Status;
}

DWORD WnbdDeviceIoControl(
    HANDLE hDevice,
    DWORD dwIoControlCode,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned,
    LPOVERLAPPED lpOverlapped)
{
    OVERLAPPED InternalOverlapped = { 0 };
    DWORD Status = ERROR_SUCCESS;
    HANDLE TempEvent = NULL;

    // DeviceIoControl can hang when FILE_FLAG_OVERLAPPED is used
    // without a valid overlapped structure. We're providing one and also
    // do the wait on behalf of the caller when lpOverlapped is NULL,
    // mimicking the Windows API.
    if (!lpOverlapped) {
        TempEvent = CreateEventA(0, FALSE, FALSE, NULL);
        if (!TempEvent) {
            Status = GetLastError();
            LogError("Could not create event. Error: %d. Error message: %s",
                     Status, win32_strerror(Status).c_str());
            return Status;
        }
        InternalOverlapped.hEvent = TempEvent;
        lpOverlapped = &InternalOverlapped;
    }

    BOOL DevStatus = DeviceIoControl(
        hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer,
        nOutBufferSize, lpBytesReturned, lpOverlapped);
    if (!DevStatus) {
        Status = GetLastError();
        if (Status == ERROR_IO_PENDING && TempEvent) {
            // We might consider an alertable wait using GetOverlappedResultEx.
            if (!GetOverlappedResult(hDevice, lpOverlapped,
                                     lpBytesReturned, TRUE)) {
                Status = GetLastError();
            }
            else {
                // The asynchronous operation completed successfully.
                Status = 0;
            }
        }
    }

    if (TempEvent) {
        CloseHandle(TempEvent);
    }

    return Status;
}

DWORD WnbdIoctlCreate(
    HANDLE Adapter,
    PWNBD_PROPERTIES Properties,
    PWNBD_CONNECTION_INFO ConnectionInfo,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;

    // Perform some minimal input validation before passing the request.
    if (STRING_OVERFLOWS(Properties->InstanceName, WNBD_MAX_NAME_LENGTH) ||
        STRING_OVERFLOWS(Properties->SerialNumber, WNBD_MAX_NAME_LENGTH) ||
        STRING_OVERFLOWS(Properties->Owner, WNBD_MAX_OWNER_LENGTH) ||
        STRING_OVERFLOWS(Properties->NbdProperties.Hostname, WNBD_MAX_NAME_LENGTH) ||
        STRING_OVERFLOWS(Properties->NbdProperties.ExportName, WNBD_MAX_NAME_LENGTH))
    {
        LogError("Invalid WNBD properties. Buffer overflow.");
        return ERROR_BUFFER_OVERFLOW;
    }

    if (!Properties->InstanceName) {
        LogError("Missing instance name.");
        return ERROR_INVALID_PARAMETER;
    }

    DWORD BytesReturned = 0;
    WNBD_IOCTL_CREATE_COMMAND Command = { 0 };

    Command.IoControlCode = IOCTL_WNBD_CREATE;
    memcpy(&Command.Properties, Properties, sizeof(WNBD_PROPERTIES));

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), ConnectionInfo, sizeof(WNBD_CONNECTION_INFO),
        &BytesReturned, Overlapped);
    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        LogError("Could not create WNBD disk. Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlRemove(
    HANDLE Adapter, const char* InstanceName,
    PWNBD_REMOVE_COMMAND_OPTIONS RemoveOptions,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;

    if (STRING_OVERFLOWS(InstanceName, WNBD_MAX_NAME_LENGTH)) {
        LogError("Invalid instance name. Buffer overflow.");
        return ERROR_BUFFER_OVERFLOW;
    }

    if (!InstanceName) {
        LogError("Missing instance name.");
        return ERROR_INVALID_PARAMETER;
    }

    DWORD BytesReturned = 0;
    WNBD_IOCTL_REMOVE_COMMAND Command = { 0 };

    Command.IoControlCode = IOCTL_WNBD_REMOVE;
    if (RemoveOptions) {
        Command.Options = *RemoveOptions;
    }
    memcpy(Command.InstanceName, InstanceName, strlen(InstanceName));

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        if (Status == ERROR_FILE_NOT_FOUND) {
            LogDebug("Could not find the disk to be removed.");
        }
        else {
            LogError("Could not remove WNBD disk. Error: %d. Error message: %s",
                     Status, win32_strerror(Status).c_str());
        }
    }
    return Status;
}

DWORD WnbdIoctlFetchRequest(
    HANDLE Adapter,
    WNBD_CONNECTION_ID ConnectionId,
    PWNBD_IO_REQUEST Request,
    PVOID DataBuffer,
    UINT32 DataBufferSize,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;

    DWORD BytesReturned = 0;
    WNBD_IOCTL_FETCH_REQ_COMMAND Command = { 0 };

    Command.IoControlCode = IOCTL_WNBD_FETCH_REQ;
    Command.ConnectionId = ConnectionId;
    Command.DataBuffer = DataBuffer;
    Command.DataBufferSize = DataBufferSize;

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND),
        &Command, sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND),
        &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        LogWarning(
            "Could not fetch request. Error: %d. "
            "Buffer: %p, buffer size: %d, connection id: %llu. "
            "Error message: %s",
            Status, DataBuffer, DataBufferSize, ConnectionId,
            win32_strerror(Status).c_str());
    }
    else {
        memcpy(Request, &Command.Request, sizeof(WNBD_IO_REQUEST));
    }

    return Status;
}

DWORD WnbdIoctlSendResponse(
    HANDLE Adapter,
    WNBD_CONNECTION_ID ConnectionId,
    PWNBD_IO_RESPONSE Response,
    PVOID DataBuffer,
    UINT32 DataBufferSize,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;

    DWORD BytesReturned = 0;
    WNBD_IOCTL_SEND_RSP_COMMAND Command = { 0 };

    Command.IoControlCode = IOCTL_WNBD_SEND_RSP;
    Command.ConnectionId = ConnectionId;
    Command.DataBuffer = DataBuffer;
    Command.DataBufferSize = DataBufferSize;
    memcpy(&Command.Response, Response, sizeof(WNBD_IO_RESPONSE));

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(WNBD_IOCTL_SEND_RSP_COMMAND),
        NULL, 0, &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        LogWarning(
            "Could not send response. "
            "Connection id: %llu. Request id: %llu. "
            "Error: %d. Error message: %s",
            ConnectionId, Response->RequestHandle,
            Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlList(
    HANDLE Adapter,
    PWNBD_CONNECTION_LIST ConnectionList,
    PDWORD BufferSize,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;
    WNBD_IOCTL_LIST_COMMAND Command = { IOCTL_WNBD_LIST };

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), ConnectionList,
        *BufferSize, BufferSize, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        LogError("Could not get disk list. Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlStats(
    HANDLE Adapter,
    const char* InstanceName,
    PWNBD_DRV_STATS Stats,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_STATS_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_STATS;
    memcpy(Command.InstanceName, InstanceName, strlen(InstanceName));

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        Stats, sizeof(WNBD_DRV_STATS), &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        if (Status == ERROR_FILE_NOT_FOUND) {
            LogInfo("Could not find the specified disk.");
        }
        else {
            LogError("Could not get disk stats. Error: %d. Error message: %s",
                     Status, win32_strerror(Status).c_str());
        }
    }

    return Status;
}

DWORD WnbdIoctlShow(
    HANDLE Adapter,
    const char* InstanceName,
    PWNBD_CONNECTION_INFO ConnectionInfo,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_SHOW_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_SHOW;
    memcpy(Command.InstanceName, InstanceName, strlen(InstanceName));

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        ConnectionInfo, sizeof(WNBD_CONNECTION_INFO),
        &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        if (Status == ERROR_FILE_NOT_FOUND) {
            LogInfo("Could not find the specified disk.");
        }
        else {
            LogError("Could not get disk details. Error: %d. Error message: %s",
                     Status, win32_strerror(Status).c_str());
        }
    }

    return Status;
}

DWORD WnbdIoctlReloadConfig(
    HANDLE Adapter,
    LPOVERLAPPED Overlapped)
{
    DWORD BytesReturned = 0;
    DWORD Status = ERROR_SUCCESS;
    WNBD_IOCTL_RELOAD_CONFIG_COMMAND Command = { IOCTL_WNBD_RELOAD_CONFIG };

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        LogError("Could not get reload driver config. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlPing(
    HANDLE Adapter,
    LPOVERLAPPED Overlapped)
{
    DWORD BytesReturned = 0;
    DWORD Status = ERROR_SUCCESS;
    WNBD_IOCTL_PING_COMMAND Command = { IOCTL_WNBD_PING };

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        LogError("Failed while pinging driver. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlGetDrvOpt(
    HANDLE Adapter,
    const char* Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_GET_DRV_OPT_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_GET_DRV_OPT;
    Command.Persistent = Persistent;
    to_wstring(Name).copy(Command.Name, WNBD_MAX_OPT_NAME_LENGTH);

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        Value, sizeof(WNBD_OPTION_VALUE),
        &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        if (Status == ERROR_FILE_NOT_FOUND) {
            LogError("Could not find driver option: %s.", Name);
        }
        else {
            LogError("Could not get adapter option. "
                     "Error: %d. Error message: %s",
                     Status, win32_strerror(Status).c_str());
        }
    }

    return Status;
}

DWORD WnbdIoctlSetDrvOpt(
    HANDLE Adapter,
    const char* Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_SET_DRV_OPT_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_SET_DRV_OPT;
    Command.Persistent = Persistent;
    Command.Value = *Value;
    to_wstring(Name).copy(Command.Name, WNBD_MAX_OPT_NAME_LENGTH);

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        NULL, 0,
        &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        if (Status == ERROR_FILE_NOT_FOUND) {
            LogError("Could not find driver option: %s.", Name);
        }
        else {
            LogError("Could not set adapter option. "
                     "Error: %d. Error message: %s",
                     Status, win32_strerror(Status).c_str());
        }
    }

    return Status;
}

DWORD WnbdIoctlResetDrvOpt(
    HANDLE Adapter,
    const char* Name,
    BOOLEAN Persistent,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_RESET_DRV_OPT_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_RESET_DRV_OPT;
    Command.Persistent = Persistent;
    to_wstring(Name).copy(Command.Name, WNBD_MAX_OPT_NAME_LENGTH);

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        NULL, 0,
        &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        if (Status == ERROR_FILE_NOT_FOUND) {
            LogError("Could not find driver option: %s.", Name);
        }
        else {
            LogError("Could not reset adapter option. "
                     "Error: %d. Error message: %s",
                     Status, win32_strerror(Status).c_str());
        }
    }

    return Status;
}

DWORD WnbdIoctlListDrvOpt(
    HANDLE Adapter,
    PWNBD_OPTION_LIST OptionList,
    PDWORD BufferSize,
    BOOLEAN Persistent,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;
    WNBD_IOCTL_LIST_DRV_OPT_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_LIST_DRV_OPT;
    Command.Persistent = Persistent;

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), OptionList,
        *BufferSize, BufferSize, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        LogError("Could not get option list. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlVersion(
    HANDLE Adapter,
    PWNBD_VERSION Version,
    LPOVERLAPPED Overlapped)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_VERSION_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_VERSION;

    Status = WnbdDeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        Version, sizeof(WNBD_VERSION),
        &BytesReturned, Overlapped);

    if (Status && !(Status == ERROR_IO_PENDING && Overlapped)) {
        LogError("Could not get driver version. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}
