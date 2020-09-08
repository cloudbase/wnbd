#include <windows.h>
#include <windef.h>
#include <winioctl.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <setupapi.h>
#include <string.h>

#include "wnbd.h"

#pragma comment(lib, "Setupapi.lib")

#define STRING_OVERFLOWS(Str, MaxLen) (strlen(Str + 1) > MaxLen)


DWORD WnbdOpenDevice(PHANDLE Handle)
{
    HDEVINFO DevInfo = { 0 };
    SP_DEVICE_INTERFACE_DATA DevInterfaceData = { 0 };
    PSP_DEVICE_INTERFACE_DETAIL_DATA DevInterfaceDetailData = NULL;
    ULONG DevIndex = 0;
    ULONG RequiredSize = 0;
    ULONG ErrorCode = 0;
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;

    DevInfo = SetupDiGetClassDevs(&WNBD_GUID, NULL, NULL,
                                  DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE) {
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
            goto Exit;
        }
        DevInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(
              DevInfo, &DevInterfaceData, DevInterfaceDetailData,
              RequiredSize, &RequiredSize, NULL)) {
            goto Exit;
        }

        WnbdDriverHandle = CreateFile(
            DevInterfaceDetailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, 0);

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

    if (!ErrorCode)
        *Handle = WnbdDriverHandle;

    return ErrorCode;
}

DWORD WnbdIoctlCreate(HANDLE Device, PWNBD_PROPERTIES Properties,
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
        return ERROR_BUFFER_OVERFLOW;
    }

    if (!Properties->InstanceName)
        return ERROR_INVALID_PARAMETER;

    DWORD BytesReturned = 0;
    WNBD_IOCTL_CREATE_COMMAND Command = { 0 };

    Command.IoControlCode = IOCTL_WNBD_CREATE;
    memcpy(&Command.Properties, Properties, sizeof(WNBD_PROPERTIES));

    BOOL DevStatus = DeviceIoControl(
        Device, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), ConnectionInfo, sizeof(WNBD_CONNECTION_INFO),
        &BytesReturned, NULL);
    if (!DevStatus) {
        ErrorCode = GetLastError();
    }

    return ErrorCode;
}


DWORD WnbdIoctlRemove(
    HANDLE Device, const char* InstanceName, BOOLEAN HardRemove)
{
    DWORD Status = ERROR_SUCCESS;

    if (STRING_OVERFLOWS(InstanceName, WNBD_MAX_NAME_LENGTH)) {
        return ERROR_BUFFER_OVERFLOW;
    }

    if (!InstanceName)
        return ERROR_INVALID_PARAMETER;

    DWORD BytesReturned = 0;
    WNBD_IOCTL_REMOVE_COMMAND Command = { 0 };

    Command.IoControlCode = IOCTL_WNBD_REMOVE;
    Command.Flags.HardRemove = !!HardRemove;
    memcpy(Command.InstanceName, InstanceName, strlen(InstanceName));

    BOOL DevStatus = DeviceIoControl(
        Device, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, NULL);
    if (!DevStatus) {
        Status = GetLastError();
    }

    return Status;
}

DWORD WnbdIoctlFetchRequest(
    HANDLE Device,
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
        Device, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND),
        &Command, sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND),
        &BytesReturned, NULL);
    if (!DevStatus) {
        Status = GetLastError();
    }
    else {
        memcpy(Request, &Command.Request, sizeof(WNBD_IO_REQUEST));
    }

    return Status;
}

DWORD WnbdIoctlSendResponse(
    HANDLE Device,
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
        Device, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(WNBD_IOCTL_SEND_RSP_COMMAND),
        NULL, 0, &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
    }

    return Status;
}

DWORD WnbdIoctlList(
    HANDLE Device,
    PWNBD_CONNECTION_LIST ConnectionList,
    PDWORD BufferSize)
{
    DWORD Status = ERROR_SUCCESS;
    WNBD_IOCTL_LIST_COMMAND Command = { IOCTL_WNBD_LIST };

    BOOL DevStatus = DeviceIoControl(
        Device, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), ConnectionList,
        *BufferSize, BufferSize, NULL);

    if (!DevStatus) {
        Status = GetLastError();
    }

    return Status;
}

DWORD WnbdIoctlStats(HANDLE Device, const char* InstanceName,
                     PWNBD_DRV_STATS Stats)
{
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;

    WNBD_IOCTL_STATS_COMMAND Command = { 0 };
    Command.IoControlCode = IOCTL_WNBD_STATS;
    memcpy(Command.InstanceName, InstanceName, strlen(InstanceName));

    BOOL DevStatus = DeviceIoControl(
        Device, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command),
        Stats, sizeof(WNBD_DRV_STATS), &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
    }

    return Status;
}

DWORD WnbdIoctlReloadConfig(HANDLE Device)
{
    DWORD BytesReturned = 0;
    WNBD_IOCTL_RELOAD_CONFIG_COMMAND Command = { IOCTL_WNBD_RELOAD_CONFIG };

    BOOL DevStatus = DeviceIoControl(
        Device, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, NULL);
    if (!DevStatus) {
        return GetLastError();
    }

    return ERROR_SUCCESS;
}

DWORD WnbdIoctlPing(HANDLE Device)
{
    DWORD BytesReturned = 0;
    WNBD_IOCTL_PING_COMMAND Command = { IOCTL_WNBD_PING };

    BOOL DevStatus = DeviceIoControl(
        Device, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, NULL);
    if (!DevStatus) {
        return GetLastError();
    }

    return ERROR_SUCCESS;
}

