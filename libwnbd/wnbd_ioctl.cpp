/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <windows.h>
#include <windef.h>
#include <winioctl.h>
#include <newdev.h>
#include <ntddscsi.h>
#include "Shlwapi.h"
#include <setupapi.h>
#include <string.h>
#include <infstr.h>
#include <devguid.h>

#include <set>

#include "wnbd.h"
#include "wnbd_log.h"
#include "utils.h"

#include <boost/filesystem.hpp>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Newdev.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace fs = boost::filesystem;
using namespace std;

#define STRING_OVERFLOWS(Str, MaxLen) (strlen(Str + 1) > MaxLen)

DWORD GetDeviceHardwareIds(
    HDEVINFO DeviceInfoList,
    PSP_DEVINFO_DATA DevInfoData,
    set<wstring>& HardwareIds)
{
    DWORD Status = 0;
    PBYTE Buff = NULL;
    PWSTR HardwareId = NULL;
    ULONG BuffSz = 0;

    HardwareIds.clear();

    if (!SetupDiGetDeviceRegistryPropertyW(
            DeviceInfoList, DevInfoData, SPDRP_HARDWAREID, 0,
            NULL, BuffSz, &BuffSz)) {
        Status = GetLastError();
        if (Status == ERROR_INVALID_DATA) {
            // No hardware id available.
            return 0;
        }
        if (Status != ERROR_INSUFFICIENT_BUFFER) {
            LogError("Couldn't retrieve device hardware id. "
                     "Error: %d. Error message: %s",
                     Status, win32_strerror(Status).c_str());
            return Status;
        }
        Status = 0;
    }

    Buff = (PBYTE) calloc(BuffSz, sizeof(WCHAR));
    if (!Buff) {
        LogError("Failed to allocate %d bytes.", BuffSz * sizeof(WCHAR));
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    if (!SetupDiGetDeviceRegistryPropertyW(
            DeviceInfoList, DevInfoData, SPDRP_HARDWAREID, 0,
            Buff, BuffSz, 0)) {
        Status = GetLastError();
        LogError("Couldn't retrieve device hardware id. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
        goto Exit;
    }

    HardwareId = (PWSTR) Buff;
    while (HardwareId[0]) {
        HardwareIds.insert(HardwareId);
        HardwareId += wcslen(HardwareId) + 1;
    }

Exit:
    if (Buff)
        free(Buff);

    return Status;
}

DWORD InitializeWnbdAdapterList(
    HDEVINFO* DeviceInfoList,
    const GUID *ClassGuid = &WNBD_GUID,
    DWORD Flags = DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
{
    DWORD Status = 0;
    *DeviceInfoList = SetupDiGetClassDevs(
        ClassGuid, NULL, NULL, Flags);
    if (&DeviceInfoList == INVALID_HANDLE_VALUE) {
        Status = GetLastError();
        LogError(
            "Could not enumerate WNBD adapter devices. "
            "Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
    }
    return Status;
}

// Open the WNBD SCSI adapter device.
DWORD WnbdOpenAdapterEx(
    PHANDLE Handle,
    HDEVINFO DeviceInfoList,
    PSP_DEVINFO_DATA DeviceInfoData,
    DWORD DevIndex,
    BOOLEAN ExpectExisting)
{
    SP_DEVICE_INTERFACE_DATA DevInterfaceData = { 0 };
    DevInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    PSP_DEVICE_INTERFACE_DETAIL_DATA DevInterfaceDetailData = NULL;
    ULONG RequiredSize = 0;
    ULONG Status = 0;
    HANDLE AdapterHandle = INVALID_HANDLE_VALUE;

    if (!SetupDiEnumDeviceInterfaces(DeviceInfoList, NULL, &WNBD_GUID,
                                     DevIndex, &DevInterfaceData)) {
        Status = GetLastError();
        if (Status != ERROR_NO_MORE_ITEMS) {
            LogError("Enumerating adapter devices failed.");
        }
        goto Exit;
    }

    if (!SetupDiGetDeviceInterfaceDetail(DeviceInfoList, &DevInterfaceData, NULL,
                                         0, &RequiredSize, NULL)) {
        Status = GetLastError();
        if (Status && ERROR_INSUFFICIENT_BUFFER != Status) {
            LogError("Could not get adapter details.");
            goto Exit;
        }
        else {
            Status = 0;
        }
    }

    DevInterfaceDetailData =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA) malloc(RequiredSize);
    if (!DevInterfaceDetailData) {
        LogError("Could not allocate %d bytes.", RequiredSize);
        Status = ERROR_NOT_ENOUGH_MEMORY;
        goto Exit;
    }
    DevInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    if (!SetupDiGetDeviceInterfaceDetail(
            DeviceInfoList, &DevInterfaceData, DevInterfaceDetailData,
            RequiredSize, &RequiredSize, DeviceInfoData))
    {
        LogError("Could not get adapter details.");
        Status = GetLastError();
        goto Exit;
    }

    // Double check the GUID. Opening the wrong device can have catastrophic
    // consequences, especially when removing devices.
    if (DevInterfaceData.InterfaceClassGuid != WNBD_GUID) {
        set<wstring> HardwareIds;
        GetDeviceHardwareIds(DeviceInfoList, DeviceInfoData, HardwareIds);
        wstring HardwareId = HardwareIds.empty() ? L"" : *HardwareIds.begin();
        LogError("The adapter GUID %s does not match the WNBD GUID %s. "
                 "Device hardware id: %ls. "
                 "This indicates a critical libwnbd bug.",
                 guid_to_string(
                     DevInterfaceData.InterfaceClassGuid).c_str(),
                 guid_to_string(WNBD_GUID).c_str(),
                 HardwareId.c_str());
        Status = ERROR_INVALID_HANDLE;
        goto Exit;
    }

    LogDebug("Opening WNBD adapter device: %ls",
             DevInterfaceDetailData->DevicePath);
    AdapterHandle = CreateFile(
        DevInterfaceDetailData->DevicePath,
        GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, 0);
    if (INVALID_HANDLE_VALUE == AdapterHandle) {
        LogWarning("Failed to open WNBD adapter device: %ls.",
                   DevInterfaceDetailData->DevicePath);
        Status = GetLastError();
        goto Exit;
    }

Exit:
    if (DevInterfaceDetailData) {
        free(DevInterfaceDetailData);
    }

    if (!Status) {
        *Handle = AdapterHandle;
    }
    else {
        if (Status == ERROR_ACCESS_DENIED) {
            LogError(
                "Could not open WNBD adapter device. Access denied, try "
                "using an elevated command prompt.");
        }
        else if (Status == ERROR_FILE_NOT_FOUND) {
            if (ExpectExisting)
                LogError(
                    "The WNBD adapter is currently unavailable. The driver may "
                    "need to be reinstalled. Failed device uninstalls (e.g. "
                    "due to open handles) can leak invalid device nodes, even "
                    "after rebooting the host.");
        } else if (Status == ERROR_NO_MORE_ITEMS) {
            if (ExpectExisting)
                LogError(
                    "No WNBD adapter found. Please make sure that the driver "
                    "is installed.");
        } else {
            LogError(
                "Could not open WNBD adapter device. Please make sure that "
                "the driver is installed. Error: %d. Error message: %s",
                Status, win32_strerror(Status).c_str());
        }
    }
    return Status;
}

// Open the WNBD SCSI adapter device.
DWORD WnbdOpenAdapter(PHANDLE Handle)
{
    DWORD Status = 0;
    SP_DEVINFO_DATA DevInfoData = { 0 };
    DevInfoData.cbSize = sizeof(DevInfoData);

    HDEVINFO DeviceInfoList = INVALID_HANDLE_VALUE;
    Status = InitializeWnbdAdapterList(&DeviceInfoList);
    if (Status) {
        return Status;
    }

    Status = WnbdOpenAdapterEx(
        Handle, DeviceInfoList, &DevInfoData, 0, TRUE);

    SetupDiDestroyDeviceInfoList(DeviceInfoList);
    return Status;
}

DWORD WnbdAdapterExists(PBOOL Exists) {
    DWORD Status = 0;
    SP_DEVINFO_DATA DevInfoData = { 0 };
    DevInfoData.cbSize = sizeof(DevInfoData);

    HDEVINFO DeviceInfoList = INVALID_HANDLE_VALUE;
    Status = InitializeWnbdAdapterList(&DeviceInfoList);
    if (Status) {
        return Status;
    }

    HANDLE Adapter = INVALID_HANDLE_VALUE;
    Status = WnbdOpenAdapterEx(
        &Adapter, DeviceInfoList, &DevInfoData, 0, FALSE);
    if (!Status || Status == ERROR_FILE_NOT_FOUND) {
        // If ERROR_FILE_NOT_FOUND is returned instead of ERROR_FILE_NOT_FOUND,
        // the device exists but cannot be opened in current state.
        *Exists = TRUE;
    }
    else if (Status == ERROR_NO_MORE_ITEMS) {
        Status = 0;
    }

    if (Adapter != INVALID_HANDLE_VALUE)
        CloseHandle(INVALID_HANDLE_VALUE);
    if (DeviceInfoList != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(DeviceInfoList);
    return Status;
}

DWORD CheckInfFile(LPCTSTR InfPath, PBOOL IsWnbdDriver)
{
    DWORD Status = 0;
    UINT ErrorLine;
    *IsWnbdDriver = FALSE;

    HINF HandleInf = SetupOpenInfFile(InfPath, NULL, INF_STYLE_WIN4, &ErrorLine);
    if (HandleInf == INVALID_HANDLE_VALUE) {
        Status = GetLastError();
        LogError("Could not open driver INF file %ls. "
                 "Error: %d, at line %d. Error message: %s",
                 InfPath, Status, ErrorLine, win32_strerror(Status).c_str());
        goto Exit;
    }

    INFCONTEXT Context;
    if (!SetupFindFirstLine(HandleInf, INFSTR_SECT_VERSION,
                            INFSTR_KEY_CATALOGFILE, &Context)) {
        // We'll treat it as a non-fatal error and keep looking for WNBD drivers.
        // Some drivers might have one catalog per architecture.
        Status = GetLastError();
        if (Status == ERROR_LINE_NOT_FOUND) {
            LogDebug("INF catalog section missing.");
            Status = 0;
        }
        else {
            LogError("Could not retrieve INF catalog section. "
                     "File: %ls. Error: %d. Error message: %s",
                     InfPath, Status, win32_strerror(Status).c_str());
        }
        goto Exit;
    }

    TCHAR InfData[MAX_INF_STRING_LENGTH];
    if (!SetupGetStringField(&Context, 1, InfData, ARRAYSIZE(InfData), NULL)) {
        Status = GetLastError();
        LogError("Could not retrieve driver catalog name. "
                 "File: %ls. Error: %d. Error message: %s",
                 InfPath, Status, win32_strerror(Status).c_str());
        goto Exit;
    }

    /* Match the OEM inf file based on the catalog string wnbd.cat */
    if (!wcscmp(InfData, L"wnbd.cat")) {
        *IsWnbdDriver = TRUE;
    }

Exit:
    if (HandleInf && HandleInf != INVALID_HANDLE_VALUE) {
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
        LogError("Couldn't retrieve Windows directory. "
                 "Error: %d. Error message: %s",
                 Status, win32_strerror(Status).c_str());
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
        BOOL IsWnbdDriver = FALSE;
        DWORD TempStatus = CheckInfFile(OemFindData.cFileName, &IsWnbdDriver);
        if (TempStatus) {
            Status = TempStatus;
            continue;
        }
        if (!IsWnbdDriver) {
            continue;
        }

        LogInfo("Removing WNBD driver: %ls", OemFindData.cFileName);
        wstring InfName = fs::path(OemFindData.cFileName).filename().wstring();
        if (!SetupUninstallOEMInf(InfName.c_str(), SUOI_FORCEDELETE, 0)) {
            Status = GetLastError();
            LogError("Could not uninstall driver: %ls. "
                     "Error: %d. Error message: %s",
                     InfName.c_str(), Status, win32_strerror(Status).c_str());
        }

    } while (FindNextFile(DirHandle, &OemFindData));

    FindClose(DirHandle);
    return Status;
}

DWORD RemoveWnbdAdapterDevice(
    HDEVINFO DeviceInfoList,
    PSP_DEVINFO_DATA DevInfoData,
    PBOOL RebootRequired)
{
    if (!DeviceInfoList || !DevInfoData) {
        LogError("Missing adapter device list.");
        return ERROR_INVALID_HANDLE;
    }

    set<wstring> HardwareIds;
    DWORD Status = GetDeviceHardwareIds(DeviceInfoList, DevInfoData, HardwareIds);
    if (Status) {
        return Status;
    }
    wstring HardwareId = HardwareIds.empty() ? L"" : *HardwareIds.begin();
    LogInfo("Removing WNBD adapter device. "
            "Hardware id: %ls. Class GUID: %s",
            HardwareId.c_str(), guid_to_string(DevInfoData->ClassGuid).c_str());

    SP_DRVINFO_DETAIL_DATA_A DrvDetailData = { 0 };
    DrvDetailData.cbSize = sizeof DrvDetailData;
    // Queue the device for removal before trying to remove the
    // OEM information file
    BOOL DeviceRemovalRequiresReboot = FALSE;
    #pragma warning(push)
    #pragma warning(disable:6387)
    // The first "DiUninstallDevice" parameter is missing the
    // "_In_opt_" annotation, although it's optional and can be NULL.
    if (!DiUninstallDevice(NULL, DeviceInfoList, DevInfoData, 0,
                           &DeviceRemovalRequiresReboot)) {
        Status = GetLastError();
        LogError(
            "Could not remove WNBD adapter. "
            "Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
    }
    #pragma warning(pop)
    // Avoid changing the input "RebootRequired" value if this specific
    // device removal didn't require a reboot.
    if (DeviceRemovalRequiresReboot) {
        *RebootRequired = TRUE;
        LogInfo("WNBD adapter removal requires a reboot. "
                "Hardware id: %s.", HardwareId);
    }

    return Status;
}

// Failed installs can leak WNBD adapter devices that don't have an associated
// class or driver.
DWORD RemoveStaleWnbdAdapters(
    PBOOL RebootRequired,
    PBOOL Found)
{
    HDEVINFO DeviceInfoList = INVALID_HANDLE_VALUE;
    DWORD Status = InitializeWnbdAdapterList(
        &DeviceInfoList, NULL, DIGCF_ALLCLASSES);
    if (Status) {
        return Status;
    }

    DWORD DevIndex = 0;
    while (!Status) {
        SP_DEVINFO_DATA DevInfoData = { 0 };
        DevInfoData.cbSize = sizeof DevInfoData;

        if (!SetupDiEnumDeviceInfo(DeviceInfoList, DevIndex, &DevInfoData)) {
            Status = GetLastError();
            if (Status == ERROR_NO_MORE_ITEMS) {
                Status = 0;
            } else {
                LogError("Enumerating adapter devices failed.");
            }
            goto Exit;
        }

        *Found = TRUE;

        // It's crucial to ensure that we're removing the right device.
        set<wstring> HardwareIds;
        Status = GetDeviceHardwareIds(DeviceInfoList, &DevInfoData, HardwareIds);
        if (Status) {
            goto Exit;
        }
        if (HardwareIds.count(to_wstring(WNBD_HARDWAREID)) &&
                (DevInfoData.ClassGuid == GUID_NULL ||
                 DevInfoData.ClassGuid == GUID_DEVCLASS_SCSIADAPTER)) {
            Status = RemoveWnbdAdapterDevice(
                DeviceInfoList, &DevInfoData, RebootRequired);
            if (Status) {
                goto Exit;
            }
        }

        DevIndex++;
    }

Exit:
    SetupDiDestroyDeviceInfoList(DeviceInfoList);

    return Status;
}

DWORD RemoveAllWnbdDisks(HANDLE AdapterHandle)
{
    DWORD BufferSize = 0;
    DWORD Status = 0;
    PWNBD_CONNECTION_LIST ConnList = NULL;

    WNBD_OPTION_VALUE OptValue = { WnbdOptBool };
    /* Disallow new mappings so we can remove all current mappings */
    OptValue.Data.AsBool = FALSE;
    Status = WnbdIoctlSetDrvOpt(
        AdapterHandle, "NewMappingsAllowed", &OptValue, FALSE, NULL);
    if (Status) {
        goto exit;
    }

    Status = WnbdIoctlList(AdapterHandle, ConnList, &BufferSize, NULL);
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
    Status = WnbdIoctlList(AdapterHandle, ConnList, &BufferSize, NULL);
    if (Status) {
        goto exit;
    }

    if (!ConnList->Count) {
        LogInfo("The specified WNBD adapter has no disks.");
    }
    for (ULONG index = 0; index < ConnList->Count; index++) {
        char* InstanceName = ConnList->Connections[index].Properties.InstanceName;
        LogInfo("Hard removing WNBD disk: %s", InstanceName);
        /* TODO add parallel and soft disconnect removal */
        Status = WnbdIoctlRemove(AdapterHandle, InstanceName, NULL, NULL);
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

// Remove all WNBD adapters and disks.
DWORD RemoveAllWnbdDevices(PBOOL RebootRequired) {
    DWORD Status = 0, TempStatus = 0;
    DWORD DevIndex = 0;

    HDEVINFO DeviceInfoList = INVALID_HANDLE_VALUE;
    Status = InitializeWnbdAdapterList(&DeviceInfoList);
    if (Status) {
        return Status;
    }

    // Iterate over all WNBD adapters and remove any associated disk.
    // In case of failure, we proceed to the next adapter and
    // return the most recent error at the end.
    BOOL Found = FALSE;
    while (TRUE) {
        HANDLE AdapterHandle = INVALID_HANDLE_VALUE;
        SP_DEVINFO_DATA DevInfoData = { 0 };
        DevInfoData.cbSize = sizeof DevInfoData;

        DWORD TempStatus = WnbdOpenAdapterEx(
            &AdapterHandle, DeviceInfoList, &DevInfoData, DevIndex++, FALSE);
        BOOL DeviceAvailable = FALSE;
        if (TempStatus) {
            if (TempStatus == ERROR_NO_MORE_ITEMS) {
                // We've handled all the adapters.
                break;
            }
            // ERROR_FILE_NOT_FOUND means that the device is currently
            // unavailable, probably it wasn't fully initialized yet.
            // We'll still have to remove it though.
            if (TempStatus == ERROR_FILE_NOT_FOUND) {
                TempStatus = 0;
            }
            else {
                // We got an unexpected error while trying to open an adapter,
                // we'll have to exit.
                Status = TempStatus;
                break;
            }
        } else {
            DeviceAvailable = TRUE;
        }

        Found = TRUE;
        if (DeviceAvailable) {
            TempStatus = RemoveAllWnbdDisks(AdapterHandle);
            Status = TempStatus ? TempStatus : Status;
        }
        else {
            LogWarning("Found existing but unavailable adapter device. "
                       "Skipping disk device removal.");
        }
        CloseHandle(AdapterHandle);

        TempStatus = RemoveWnbdAdapterDevice(
            DeviceInfoList, &DevInfoData,
            RebootRequired);
        Status = TempStatus ? TempStatus : Status;
    }

    // Remove WNBD adapters that don't have a class guid, usually
    // resulting from failed installs.
    LogDebug("Removing stale wnbd adapters.");
    TempStatus = RemoveStaleWnbdAdapters(RebootRequired, &Found);
    if (TempStatus) {
        LogError("Couldn't remove stale adapters. Error: %d.", TempStatus);
    }
    Status = TempStatus ? TempStatus : Status;

    if (!Found && !Status) {
        LogInfo("No WNBD adapters found.");
    }

    SetupDiDestroyDeviceInfoList(DeviceInfoList);
    return Status;
}

DWORD WnbdUninstallDriver(PBOOL RebootRequired)
{
    DWORD Status = RemoveAllWnbdDevices(RebootRequired);
    DWORD TempStatus = CleanDrivers();
    return TempStatus ? TempStatus : Status;
}

DWORD CreateWnbdAdapter(
    CHAR* ClassName,
    SP_DEVINFO_DATA* DevInfoData,
    HDEVINFO* DeviceInfoList)
{
    LogInfo("Creating WNBD adapter. Class: %s. Hardware id: %s.",
            ClassName, WNBD_HARDWAREID);
    DWORD Status = 0;

    *DeviceInfoList = SetupDiCreateDeviceInfoList(&WNBD_GUID, 0);
    if (INVALID_HANDLE_VALUE == *DeviceInfoList) {
        Status = GetLastError();
        LogError(
            "Could not create device info list. "
            "Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    if (!SetupDiCreateDeviceInfoA(*DeviceInfoList, ClassName,
                                  &WNBD_GUID, 0, 0,
                                  DICD_GENERATE_ID, DevInfoData)) {
        Status = GetLastError();
        LogError(
            "Could not initialize device info. "
            "Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    if (!SetupDiSetDeviceRegistryPropertyA(
            *DeviceInfoList, DevInfoData,
            SPDRP_HARDWAREID, (PBYTE)WNBD_HARDWAREID, WNBD_HARDWAREID_LEN))
    {
        Status = GetLastError();
        LogError(
            "Could not set hardware id. "
            "Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
    }

    return Status;
}

DWORD InstallDriver(
    CHAR* FileName,
    HDEVINFO* DeviceInfoList,
    PBOOL RebootRequired)
{
    GUID ClassGuid = { 0 };
    CHAR ClassName[MAX_CLASS_NAME_LEN];
    DWORD Status = 0;

    LogInfo("Installing WNBD driver: %s", FileName);
    if (!SetupDiGetINFClassA(FileName, &ClassGuid, ClassName,
                             MAX_CLASS_NAME_LEN, 0)) {
        Status = GetLastError();
        LogError(
            "Could not process driver INF file %s. "
            "Error: %d. Error message: %s",
            FileName, Status, win32_strerror(Status).c_str());
        return Status;
    }

    SP_DEVINFO_DATA DevInfoData;
    DevInfoData.cbSize = sizeof(DevInfoData);
    if (CreateWnbdAdapter(ClassName, &DevInfoData, DeviceInfoList)) {
        return Status;
    }

    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, *DeviceInfoList,
                                   &DevInfoData)) {
        Status = GetLastError();
        LogError(
            "Device class installer failed. "
            "Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        return Status;
    }

    SP_DEVINSTALL_PARAMS_A InstallParams;
    InstallParams.cbSize = sizeof InstallParams;
    if (!SetupDiGetDeviceInstallParamsA(*DeviceInfoList, &DevInfoData,
                                        &InstallParams)) {
        Status = GetLastError();
        LogError(
            "Couldn't retrieve device installation parameters. "
            "Error: %d. Error message: %s",
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
    DWORD Status = 0, TempStatus = 0;
    BOOL InstallAttempted = FALSE;

    if (0 == GetFullPathNameA(FileName, MAX_PATH, FullFileName, 0)) {
        Status = GetLastError();
        LogError(
            "Invalid file path: %s. Error: %d. Error message: %s",
            FileName, Status, win32_strerror(Status).c_str());
        return Status;
    }
    if (FALSE == PathFileExistsA(FullFileName)) {
        LogError("Could not find file: %s.", FullFileName);
        Status = ERROR_FILE_NOT_FOUND;
        return Status;
    }

    BOOL AdapterExists = FALSE;
    Status = WnbdAdapterExists(&AdapterExists);
    // Just because we failed to open it doesn't mean that it does not exist.
    if (Status) {
        return Status;
    }

    if (AdapterExists) {
        LogError("An active WNBD adapter already exists. "
                 "Please uninstall it before updating the driver.");
        Status = ERROR_DUPLICATE_FOUND;
        return Status;
    }

    // In certain situations, such as failed installs, we can end up with
    // stale WNBD adapters that don't have an associated driver or class.
    // Instead of erroring out, we're going to do a cleanup.
    BOOL StaleAdaptersFound = FALSE;
    Status = RemoveStaleWnbdAdapters(RebootRequired, &StaleAdaptersFound);
    if (Status) {
        return Status;
    }
    if (StaleAdaptersFound) {
        LogInfo("Cleaned up stale WNBD adapters.");
    }

    InstallAttempted = TRUE;
    HDEVINFO DeviceInfoList = INVALID_HANDLE_VALUE;
    Status = InstallDriver(FullFileName, &DeviceInfoList, RebootRequired);
    if (ERROR_SUCCESS != Status) {
        LogError(
            "Failed to install driver. Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        goto Exit;
    }

    if (!UpdateDriverForPlugAndPlayDevicesA(
            0, WNBD_HARDWAREID, FullFileName, 0, RebootRequired)) {
        Status = GetLastError();
        LogError(
            "Updating driver failed. Error: %d. Error message: %s",
            Status, win32_strerror(Status).c_str());
        goto Exit;
    }

Exit:
    if (Status && InstallAttempted) {
        LogInfo("Rolling back failed driver installation.");
        TempStatus = WnbdUninstallDriver(RebootRequired);
        if (TempStatus) {
            Status = TempStatus;
            LogError("Driver rollback failed. Error: %d.", Status);
        }
    }

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
        LogDebug(
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
