/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "wnbd_wmi.h"

#pragma comment(lib, "comsuppd.lib")
#pragma comment(lib, "comsuppwd.lib")
#pragma comment(lib, "wbemuuid.lib")

HRESULT CoInitializeBasic()
{
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres))
        return hres;

    hres = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE,
        NULL);
    if (FAILED(hres)) {
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
    if (FAILED(hres))
        goto Exit;

    hres = Connection->WbemLoc->ConnectServer(
        _bstr_t(Namespace).GetBSTR(), NULL, NULL, NULL,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT, NULL, NULL,
        &Connection->WbemSvc);
    if (FAILED(hres))
        goto Exit;

    hres = CoSetProxyBlanket(
        Connection->WbemSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

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

    return hres;
}

HRESULT GetDiskDrives(
    PWMI_CONNECTION Connection,
    std::wstring Query,
    std::vector<DiskInfo>& Disks)
{
    HRESULT hres = 0;
    IEnumWbemClassObject* Enumerator = NULL;
    IWbemClassObject* ClsObj = NULL;
    ULONG ReturnedCount = 0;

    if (!Connection->WbemSvc) {
        hres = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32,
                            ERROR_INVALID_HANDLE);
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

    if (FAILED(hres))
        goto Exit;

    while (Enumerator) {
        Enumerator->Next(WBEM_INFINITE, 1, &ClsObj, &ReturnedCount);
        if (!ReturnedCount)
            break;

        DiskInfo d;

        hres = GetPropertyStr(ClsObj, L"DeviceID", d.deviceId);
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

HRESULT GetDiskNumberBySerialNumber(
    LPCWSTR SerialNumber,
    PDWORD DiskNumber)
{
    std::wstring Query = L"SELECT * FROM Win32_DiskDrive WHERE SerialNumber = '";
    Query.append(SerialNumber);
    Query.append(L"'");

    std::vector<DiskInfo> Disks;

    WMI_CONNECTION Connection = { 0 };
    HRESULT hres = CreateWmiConnection(L"root\\cimv2", &Connection);
    if (FAILED(hres))
        return hres;

    hres = GetDiskDrives(&Connection, Query, Disks);
    CloseWmiConnecton(&Connection);

    if (FAILED(hres))
        return hres;

    if (Disks.size() > 1) {
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_DUP_NAME);
    }
    if (Disks.size() < 1) {
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_FILE_NOT_FOUND);
    }

    *DiskNumber = Disks[0].Index;
    return hres;
}
