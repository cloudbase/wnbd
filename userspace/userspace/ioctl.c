/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "ioctl.h"

#pragma comment(lib, "Setupapi.lib")

int Syntax(void)
{
    printf("Syntax:\n");
    printf("wnbd-client map  <InstanceName> <HostName> <PortName> <ExportName> <DoNotNegotiate>\n");
    printf("wnbd-client unmap <InstanceName>\n");
    printf("wnbd-client list \n");
    printf("wnbd-client set-debug <int>\n");

    return -1;
}

void GLAToString()
{
    LPVOID LpMsgBuf;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        0,
        (LPTSTR)&LpMsgBuf,
        0,
        NULL
    );

    printf("%s", (LPTSTR)LpMsgBuf);

    LocalFree(LpMsgBuf);
}

HANDLE
GetWnbdDriverHandle(VOID)
{
    HDEVINFO DevInfo = { 0 };
    SP_DEVICE_INTERFACE_DATA DevInterfaceData = { 0 };
    PSP_DEVICE_INTERFACE_DETAIL_DATA DevInterfaceDetailData = NULL;
    ULONG DevIndex = { 0 };
    ULONG RequiredSize = { 0 };
    ULONG GLA = { 0 };
    HANDLE WnbdDriverHandle = { 0 };
    DWORD BytesReturned = { 0 };
    WNBD_COMMAND Command = { 0 };
    BOOL DevStatus = { 0 };

    DevInfo = SetupDiGetClassDevs(&WNBD_GUID, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevs failed with error 0x%x\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    DevInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    DevIndex = 0;

    while (SetupDiEnumDeviceInterfaces(DevInfo, NULL, &WNBD_GUID, DevIndex++, &DevInterfaceData)) {
        if (DevInterfaceDetailData != NULL) {
            free(DevInterfaceDetailData);
            DevInterfaceDetailData = NULL;
        }

        if (!SetupDiGetDeviceInterfaceDetail(DevInfo, &DevInterfaceData, NULL, 0, &RequiredSize, NULL)) {
            GLA = GetLastError();

            if (GLA != ERROR_INSUFFICIENT_BUFFER) {
                printf("SetupDiGetDeviceInterfaceDetail failed with error 0x%x\n", GLA);
                SetupDiDestroyDeviceInfoList(DevInfo);
                return INVALID_HANDLE_VALUE;
            }
        }

        DevInterfaceDetailData =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(RequiredSize);

        if (!DevInterfaceDetailData) {
            printf("Unable to allocate resources\n");
            SetupDiDestroyDeviceInfoList(DevInfo);
            return INVALID_HANDLE_VALUE;
        }

        DevInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);


        if (!SetupDiGetDeviceInterfaceDetail(DevInfo, &DevInterfaceData, DevInterfaceDetailData,
            RequiredSize, &RequiredSize, NULL)) {
            printf("SetupDiGetDeviceInterfaceDetail failed with error 0x%x\n", GetLastError());
            SetupDiDestroyDeviceInfoList(DevInfo);
            free(DevInterfaceDetailData);
            return INVALID_HANDLE_VALUE;
        }

        WnbdDriverHandle = CreateFile(DevInterfaceDetailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, 0);

        Command.IoCode = IOCTL_WNBD_PORT;

        DevStatus = DeviceIoControl(WnbdDriverHandle, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
            &Command, sizeof(Command), &Command, sizeof(Command), &BytesReturned, NULL);

        if (!DevStatus) {
            DWORD error = GetLastError();
            printf("Failed sending NOOP command IOCTL_MINIPORT_PROCESS_SERVICE_IRP\n");
            printf("\\\\.\\SCSI%s: error:%d.\n", DevInterfaceDetailData->DevicePath, error);
            CloseHandle(WnbdDriverHandle);
            WnbdDriverHandle = INVALID_HANDLE_VALUE;
            GLAToString();
            continue;
        } else {
            SetupDiDestroyDeviceInfoList(DevInfo);
            free(DevInterfaceDetailData);
            return WnbdDriverHandle;
        }
    }

    GLA = GetLastError();

    if (GLA != ERROR_NO_MORE_ITEMS) {
        printf("SetupDiGetDeviceInterfaceDetail failed with error 0x%x\n", GLA);
        SetupDiDestroyDeviceInfoList(DevInfo);
        free(DevInterfaceDetailData);
        return INVALID_HANDLE_VALUE;
    }

    SetupDiDestroyDeviceInfoList(DevInfo);

    if (DevInterfaceDetailData == NULL) {
        printf("Unable to find any Nothing devices!\n");
        return INVALID_HANDLE_VALUE;
    }
    return INVALID_HANDLE_VALUE;

}

DWORD
WnbdMap(PCHAR InstanceName,
        PCHAR HostName,
        PCHAR PortName,
        PCHAR ExportName,
        UINT64 DiskSize,
        BOOLEAN MustNegotiate)
{
    CONNECTION_INFO ConnectIn = { 0 };
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;
    BOOL DevStatus = 0;
    INT Pid = _getpid();

    WnbdDriverHandle = GetWnbdDriverHandle();
    if (WnbdDriverHandle == INVALID_HANDLE_VALUE) {
        Status = GetLastError();
        printf("Could not get WNBD driver handle. Can not send requests.\n");
        printf("The driver maybe is not installed\n");
        GLAToString();
        goto Exit;
    }

    memcpy(&ConnectIn.InstanceName, InstanceName, min(strlen(InstanceName)+1, MAX_NAME_LENGTH));
    memcpy(&ConnectIn.Hostname, HostName, min(strlen(HostName)+1, MAX_NAME_LENGTH));
    memcpy(&ConnectIn.PortName, PortName, min(strlen(PortName)+1, MAX_NAME_LENGTH));
    memcpy(&ConnectIn.ExportName, ExportName, min(strlen(ExportName)+1, MAX_NAME_LENGTH));
    memcpy(&ConnectIn.SerialNumber, InstanceName, min(strlen(InstanceName)+1, MAX_NAME_LENGTH));
    ConnectIn.DiskSize = DiskSize;
    ConnectIn.IoControlCode = IOCTL_WNBD_MAP;
    ConnectIn.Pid = Pid;
    ConnectIn.MustNegotiate = MustNegotiate;
    ConnectIn.BlockSize = 0;

    DevStatus = DeviceIoControl(WnbdDriverHandle, IOCTL_MINIPORT_PROCESS_SERVICE_IRP, &ConnectIn, sizeof(CONNECTION_INFO),
        NULL, 0, &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
        printf("IOCTL_MINIPORT_PROCESS_SERVICE_IRP failed\n");
        GLAToString();
    }

    CloseHandle(WnbdDriverHandle);
Exit:
    return Status;
}


DWORD
WnbdUnmap(PCHAR InstanceName)
{
    CONNECTION_INFO DisconnectIn = { 0 };
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;
    BOOL DevStatus = FALSE;

    WnbdDriverHandle = GetWnbdDriverHandle();
    if (WnbdDriverHandle == INVALID_HANDLE_VALUE) {
        Status = GetLastError();
        printf("Could not get WNBD driver handle. Can not send requests.\n");
        printf("The driver maybe is not installed\n");
        GLAToString();
        goto Exit;
    }

    memcpy(&DisconnectIn.InstanceName[0], InstanceName, min(strlen(InstanceName), MAX_NAME_LENGTH));
    DisconnectIn.IoControlCode = IOCTL_WNBD_UNMAP;

    DevStatus = DeviceIoControl(WnbdDriverHandle, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &DisconnectIn, sizeof(CONNECTION_INFO), NULL, 0, &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
        printf("IOCTL_MINIPORT_PROCESS_SERVICE_IRP failed\n");
        GLAToString();
    }

    CloseHandle(WnbdDriverHandle);
Exit:
    return Status;
}


DWORD
WnbdList(PDISK_INFO_LIST* Output)
{
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;
    DWORD Status = ERROR_SUCCESS;
    DWORD Length = 0, BytesReturned = 0;
    PUCHAR Buffer = NULL;
    WNBD_COMMAND Command = { 0 };
    BOOL DevStatus = FALSE;

    WnbdDriverHandle = GetWnbdDriverHandle();
    if (WnbdDriverHandle == INVALID_HANDLE_VALUE) {
        Status = GetLastError();
        printf("Could not get WNBD driver handle. Can not send requests.\n");
        printf("The driver maybe is not installed\n");
        GLAToString();
        goto Exit;
    }

    Command.IoCode = IOCTL_WNBD_LIST;
    Length = 0;
    DevStatus = DeviceIoControl(WnbdDriverHandle, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &Length, NULL);

    Buffer = malloc(Length);
    if (!Buffer) {
        CloseHandle(WnbdDriverHandle);
        Status = ERROR_NOT_ENOUGH_MEMORY;
    }
    memset(Buffer, 0, Length);

    DevStatus = DeviceIoControl(WnbdDriverHandle, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), Buffer, Length, &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
        printf("IOCTL_MINIPORT_PROCESS_SERVICE_IRP failed\n");
        GLAToString();
    }

    PDISK_INFO_LIST ActiveConnectList = (PDISK_INFO_LIST)Buffer;

    if (Buffer && BytesReturned && ActiveConnectList->ActiveListCount) {
        Status = ERROR_SUCCESS;
    }
    if (ERROR_SUCCESS != Status) {
        free(Buffer);
        Buffer = NULL;
    }
    *Output = (PDISK_INFO_LIST)Buffer;
Exit:
    return Status;
}

HKEY OpenKey(HKEY RootKey, LPCTSTR StringKey, BOOLEAN Create)
{
    HKEY Key = NULL;
    DWORD Status = RegOpenKeyExA(RootKey, StringKey, 0, KEY_ALL_ACCESS, &Key);

    if (Status == ERROR_FILE_NOT_FOUND && Create)
    {
        printf("Creating registry key: %s\n", StringKey);
        Status = RegCreateKeyExA(RootKey, StringKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &Key, NULL);
    }

    if (ERROR_SUCCESS != Status) {
        printf("Could not open registry key: %s\n", StringKey);
        GLAToString();
    }

    return Key;
}

DWORD SetVal(HKEY RootKey, LPCTSTR StringKey, DWORD* Value)
{
    DWORD status = RegSetValueExA(RootKey, StringKey, 0, REG_DWORD, (LPBYTE)Value, sizeof(DWORD));
    if (ERROR_SUCCESS != status) {
        printf("Could not set registry value: %s\n", StringKey);
        GLAToString();
        return status;
    }

    return ERROR_SUCCESS;
}

DWORD
WnbdSetDebug(UINT32 LogLevel)
{
    HANDLE WnbdDriverHandle = INVALID_HANDLE_VALUE;
    DWORD Status = ERROR_SUCCESS;
    DWORD BytesReturned = 0;
    PUCHAR Buffer = NULL;
    WNBD_COMMAND Command = { 0 };
    BOOL DevStatus = FALSE;
    PCHAR Service = "SYSTEM\\CurrentControlSet\\Services\\wnbd\\";

    HKEY hKey = OpenKey(HKEY_LOCAL_MACHINE, Service, TRUE);
    if (!hKey) {
        printf("Could not open or create the registry path: %s\n", Service);
        return ERROR_PATH_NOT_FOUND;
    }

    SetVal(hKey, "DebugLogLevel", &LogLevel);

    WnbdDriverHandle = GetWnbdDriverHandle();
    if (WnbdDriverHandle == INVALID_HANDLE_VALUE) {
        Status = GetLastError();
        printf("Could not get WNBD driver handle. Can not send requests.\n");
        printf("The driver maybe is not installed\n");
        GLAToString();
        goto Exit;
    }

    Command.IoCode = IOCTL_WNBD_DEBUG;

    DevStatus = DeviceIoControl(WnbdDriverHandle, IOCTL_MINIPORT_PROCESS_SERVICE_IRP,
        &Command, sizeof(Command), NULL, 0, &BytesReturned, NULL);

    if (!DevStatus) {
        Status = GetLastError();
        printf("IOCTL_MINIPORT_PROCESS_SERVICE_IRP failed\n");
        GLAToString();
    }

Exit:
    return Status;
}
