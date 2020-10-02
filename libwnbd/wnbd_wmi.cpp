/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "wnbd_wmi.h"
#include "wnbd_log.h"
#include "utils.h"

#pragma comment(lib, "comsuppd.lib")
#pragma comment(lib, "comsuppwd.lib")
#pragma comment(lib, "wbemuuid.lib")

HRESULT CoInitializeBasic()
{
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) {
        LogError("CoInitializeEx failed. HRESULT: %d", hres);
        return hres;
    }

    hres = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE,
        NULL);
    if (FAILED(hres)) {
        LogError("CoInitializeSecurity failed. HRESULT: %d", hres);
        CoUninitialize();
        return hres;
    }

    return 0;
}

VOID CloseWmiConnecton(PWMI_CONNECTION Connection)
{
    if (!Connection)
        return;

    if (Connection->WbemSvc != NULL) {
        Connection->WbemSvc->Release();
        Connection->WbemSvc = NULL;
    }
    if (Connection->WbemLoc != NULL) {
        Connection->WbemLoc->Release();
        Connection->WbemLoc = NULL;
    }
}

HRESULT CreateWmiConnection(
    LPCWSTR Namespace,
    PWMI_CONNECTION Connection)
{
    HRESULT hres = CoCreateInstance(
        CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&Connection->WbemLoc);
    if (FAILED(hres)) {
        LogError("CoCreateInstance failed. HRESULT: %d", hres);
        goto Exit;
    }

    hres = Connection->WbemLoc->ConnectServer(
        _bstr_t(Namespace).GetBSTR(), NULL, NULL, NULL,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT, NULL, NULL,
        &Connection->WbemSvc);
    if (FAILED(hres)) {
        LogError("Could not connect to WMI service. HRESULT: %d", hres);
        goto Exit;
    }

    hres = CoSetProxyBlanket(
        Connection->WbemSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(hres)) {
        LogError("CoSetProxyBlanket failed. HRESULT: %d", hres);
    }

Exit:
    if (FAILED(hres)) {
        CloseWmiConnecton(Connection);
    }
    return hres;
}

HRESULT GetPropertyStr(
    IWbemClassObject* ClsObj,
    const std::wstring property,
    std::wstring& value)
{
    VARIANT vtProp;
    VariantInit(&vtProp);
    HRESULT hres = ClsObj->Get(property.c_str(), 0, &vtProp, 0, 0);
    if (!FAILED(hres))
    {
        VARIANT vtBstrProp;
        VariantInit(&vtBstrProp);
        hres = VariantChangeType(&vtBstrProp, &vtProp, 0, VT_BSTR);
        if (!FAILED(hres))
        {
            value = vtBstrProp.bstrVal;
        }
        VariantClear(&vtBstrProp);
    }
    VariantClear(&vtProp);

    if (FAILED(hres)) {
        LogDebug("Could not get WMI property: %s. HRESULT: %d",
                 to_string(property), hres);
    }
    return hres;
}

HRESULT GetPropertyInt(
    IWbemClassObject* ClsObj,
    const std::wstring& property,
    UINT32& value)
{
    VARIANT vtProp;
    VariantInit(&vtProp);
    HRESULT hres = ClsObj->Get(property.c_str(), 0, &vtProp, 0, 0);
    if (!FAILED(hres))
    {
        VARIANT vtBstrProp;
        VariantInit(&vtBstrProp);
        hres = VariantChangeType(&vtBstrProp, &vtProp, 0, VT_UINT);
        if (!FAILED(hres))
        {
            value = vtBstrProp.intVal;
        }
        VariantClear(&vtBstrProp);
    }
    VariantClear(&vtProp);

    if (FAILED(hres)) {
        LogDebug("Could not get WMI property: %s. HRESULT: %d",
                 to_string(property), hres);
    }
    return hres;
}

HRESULT GetDiskDrives(
    PWMI_CONNECTION Connection,
    std::wstring Query,
    std::vector<DISK_INFO>& Disks)
{
    HRESULT hres = 0;
    IEnumWbemClassObject* Enumerator = NULL;
    IWbemClassObject* ClsObj = NULL;
    ULONG ReturnedCount = 0;

    if (!Connection->WbemSvc) {
        hres = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32,
                            ERROR_INVALID_HANDLE);
        LogError("Invalid WMI connection.");
        return hres;
    }

    BSTR BstrWql = SysAllocString(L"WQL");
    BSTR BstrQuery = SysAllocString(Query.c_str());
    hres = Connection->WbemSvc->ExecQuery(
        BstrWql, BstrQuery,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &Enumerator);
    SysFreeString(BstrWql);
    SysFreeString(BstrQuery);

    if (FAILED(hres)) {
        LogError("WMI query failed.");
        goto Exit;
    }

    while (Enumerator) {
        Enumerator->Next(WBEM_INFINITE, 1, &ClsObj, &ReturnedCount);
        if (!ReturnedCount)
            break;

        DISK_INFO d;

        hres = GetPropertyStr(ClsObj, L"DeviceID", d.deviceId);
        if (FAILED(hres))
            goto Exit;

        hres = GetPropertyStr(ClsObj, L"PNPDeviceID", d.PNPDeviceID);
        if (FAILED(hres))
            goto Exit;

        hres = GetPropertyInt(ClsObj, L"Index", d.Index);
        if (FAILED(hres))
            goto Exit;

        Disks.push_back(d);
    }

Exit:
    if (ClsObj != NULL)
        ClsObj->Release();
    if (Enumerator != NULL)
        Enumerator->Release();

    return hres;
}

HRESULT GetDiskInfoBySerialNumber(
    LPCWSTR SerialNumber,
    PDISK_INFO DiskInfo)
{
    std::wstring Query = L"SELECT * FROM Win32_DiskDrive WHERE SerialNumber = '";
    Query.append(SerialNumber);
    Query.append(L"'");

    std::vector<DISK_INFO> Disks;

    WMI_CONNECTION Connection = { 0 };
    HRESULT hres = CreateWmiConnection(L"root\\cimv2", &Connection);
    if (FAILED(hres)) {
        LogError("Could not create WMI connection");
        return hres;
    }

    hres = GetDiskDrives(&Connection, Query, Disks);
    CloseWmiConnecton(&Connection);

    if (FAILED(hres)) {
        LogError("Could not get WMI disk drives.");
        return hres;
    }

    if (Disks.size() > 1) {
        LogError(
            "Found %d disks having the same serial number: %s. "
            "This can lead to data loss.",
            Disks.size(), to_string(SerialNumber));
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_DUP_NAME);
    }
    if (Disks.size() < 1) {
        LogDebug("Could not find device having serial: %s.",
                 to_string(SerialNumber));
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_FILE_NOT_FOUND);
    }

    *DiskInfo = Disks[0];
    return hres;
}

HRESULT GetDiskNumberBySerialNumber(
    LPCWSTR SerialNumber,
    PDWORD DiskNumber)
{
    DISK_INFO Disk;
    HRESULT hres = GetDiskInfoBySerialNumber(SerialNumber, &Disk);
    if (!FAILED(hres)) {
        *DiskNumber = Disk.Index;
    }
    return hres;
}
