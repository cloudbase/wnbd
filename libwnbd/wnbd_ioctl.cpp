#include <windows.h>
#include <windef.h>
#include <winioctl.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <setupapi.h>
#include <string.h>

#include "wnbd.h"
#include "wnbd_log.h"
#include "utils.h"

#pragma comment(lib, "Setupapi.lib")

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

    while (SetupDiEnumDeviceInterfaces(DevInfo, NULL, &WNBD_GUID,
                                       DevIndex++, &DevInterfaceData)) {
        if (!SetupDiGetDeviceInterfaceDetail(DevInfo, &DevInterfaceData, NULL,
                                             0, &RequiredSize, NULL)) {
            ErrorCode = GetLastError();
            if (ERROR_INSUFFICIENT_BUFFER != ErrorCode) {
                goto Exit;
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
              RequiredSize, &RequiredSize, &DevInfoData)) {
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

        ErrorCode = WnbdIoctlPing(WnbdDriverHandle);
        if (ErrorCode) {
            CloseHandle(WnbdDriverHandle);
            WnbdDriverHandle = INVALID_HANDLE_VALUE;
            continue;
        } else {
            goto Exit;
        }
    }

    ErrorCode = GetLastError();
    if (ErrorCode != ERROR_NO_MORE_ITEMS) {
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

// Open the WNBD SCSI adapter device.
DWORD WnbdOpenAdapterCMDeviceInstance(PDEVINST DeviceInstance)
{
    HANDLE Handle;
    DWORD Status = WnbdOpenAdapterEx(&Handle, DeviceInstance);

    if (!Status) {
        CloseHandle(&Handle);
    }
    return Status;
}

DWORD WnbdIoctlCreate(HANDLE Adapter, PWNBD_PROPERTIES Properties,
                      PWNBD_CONNECTION_INFO ConnectionInfo)
{
    DWORD ErrorCode = ERROR_SUCCESS;

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

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), ConnectionInfo, sizeof(WNBD_CONNECTION_INFO),
        &BytesReturned, NULL);
    if (!DevStatus) {
        ErrorCode = GetLastError();
        LogError("Could not create WNBD disk. Error: %d. Error message: %s",
                 ErrorCode, win32_strerror(ErrorCode).c_str());
    }

    return ErrorCode;
}

DWORD WnbdIoctlRemove(
    HANDLE Adapter, const char* InstanceName,
    PWNBD_REMOVE_COMMAND_OPTIONS RemoveOptions)
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

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, NULL);
    if (!DevStatus) {
        Status = GetLastError();
    }

    if (Status == ERROR_FILE_NOT_FOUND) {
        LogDebug("Could not find the disk to be removed.");
    }
    else {
        LogError("Could not remove WNBD disk. Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }
    return Status;
}

DWORD WnbdIoctlFetchRequest(
    HANDLE Adapter,
    WNBD_CONNECTION_ID ConnectionId,
    PWNBD_IO_REQUEST Request,
    PVOID DataBuffer,
    UINT32 DataBufferSize)
{
    DWORD Status = ERROR_SUCCESS;

    DWORD BytesReturned = 0;
    WNBD_IOCTL_FETCH_REQ_COMMAND Command = { 0 };

    Command.IoControlCode = IOCTL_WNBD_FETCH_REQ;
    Command.ConnectionId = ConnectionId;
    Command.DataBuffer = DataBuffer;
    Command.DataBufferSize = DataBufferSize;

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND),
        &Command, sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND),
        &BytesReturned, NULL);
    if (!DevStatus) {
        Status = GetLastError();
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
    UINT32 DataBufferSize)
{
    DWORD Status = ERROR_SUCCESS;

    DWORD BytesReturned = 0;
    WNBD_IOCTL_SEND_RSP_COMMAND Command = { 0 };

    Command.IoControlCode = IOCTL_WNBD_SEND_RSP;
    Command.ConnectionId = ConnectionId;
    Command.DataBuffer = DataBuffer;
    Command.DataBufferSize = DataBufferSize;
    memcpy(&Command.Response, Response, sizeof(WNBD_IO_RESPONSE));

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(WNBD_IOCTL_SEND_RSP_COMMAND),
        NULL, 0, &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
        LogWarning(
            "Could not send response. Error: %d. "
            "Connection id: %llu. Error message: %s",
            Status, ConnectionId, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlList(
    HANDLE Adapter,
    PWNBD_CONNECTION_LIST ConnectionList,
    PDWORD BufferSize)
{
    DWORD Status = ERROR_SUCCESS;
    WNBD_IOCTL_LIST_COMMAND Command = { IOCTL_WNBD_LIST };

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), ConnectionList,
        *BufferSize, BufferSize, NULL);

    if (!DevStatus) {
        Status = GetLastError();
        LogError("Could not get disk list. Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlStats(HANDLE Adapter, const char* InstanceName,
                     PWNBD_DRV_STATS Stats)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_STATS_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_STATS;
    memcpy(Command.InstanceName, InstanceName, strlen(InstanceName));

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        Stats, sizeof(WNBD_DRV_STATS), &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
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

DWORD WnbdIoctlShow(HANDLE Adapter, const char* InstanceName,
                    PWNBD_CONNECTION_INFO ConnectionInfo)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_SHOW_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_SHOW;
    memcpy(Command.InstanceName, InstanceName, strlen(InstanceName));

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        ConnectionInfo, sizeof(WNBD_CONNECTION_INFO), &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
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

DWORD WnbdIoctlReloadConfig(HANDLE Adapter)
{
    DWORD BytesReturned = 0;
    DWORD Status = ERROR_SUCCESS;
    WNBD_IOCTL_RELOAD_CONFIG_COMMAND Command = { IOCTL_WNBD_RELOAD_CONFIG };

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, NULL);
    if (!DevStatus) {
        Status = GetLastError();
        LogError("Could not get reload driver config. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlPing(HANDLE Adapter)
{
    DWORD BytesReturned = 0;
    DWORD Status = ERROR_SUCCESS;
    WNBD_IOCTL_PING_COMMAND Command = { IOCTL_WNBD_PING };

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, NULL);
    if (!DevStatus) {
        Status = GetLastError();
        LogError("Failed while pinging driver. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD WnbdIoctlVersion(HANDLE Adapter, PWNBD_VERSION Version)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_VERSION_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_VERSION;

    BOOL DevStatus = DeviceIoControl(
        Adapter, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        Version, sizeof(WNBD_VERSION), &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
        LogError("Could not get driver version. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
    }

    return Status;
}
