/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "pch.h"

#include "utils.h"

std::string GetNewInstanceName()
{
    auto TestInstance = ::testing::UnitTest::GetInstance();
    auto TestName = std::string(TestInstance->current_test_info()->name());
    return TestName + "-" + std::to_string(rand());
}

std::string WinStrError(DWORD Err)
{
    LPSTR Msg = NULL;
    DWORD MsgLen = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        Err,
        0,
        (LPSTR)&Msg,
        0,
        NULL);

    std::ostringstream MsgStream;
    MsgStream << "(" << Err << ") ";
    if (!MsgLen) {
        MsgStream << "Unknown error";
    }
    else {
        MsgStream << Msg;
        ::LocalFree(Msg);
    }
    return MsgStream.str();
}

// Retrieves the disk path and waits for it to become available.
std::string GetDiskPath(std::string InstanceName)
{
    DWORD TimeoutMs = 10 * 1000;
    DWORD RetryInterval = 500;
    LARGE_INTEGER StartTime, CurrTime, ElapsedMs, CounterFreq;
    QueryPerformanceFrequency(&CounterFreq);
    QueryPerformanceCounter(&StartTime);
    ElapsedMs = { 0 };

    do {
        WNBD_CONNECTION_INFO ConnectionInfo = {0};
        NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
        if (Status) {
            std::string Msg = "couln't retrieve WNBD disk info, error: " +
                std::to_string(Status);
            throw std::runtime_error(Msg);
        }

        QueryPerformanceCounter(&CurrTime);
        ElapsedMs.QuadPart = CurrTime.QuadPart - StartTime.QuadPart;
        ElapsedMs.QuadPart *= 1000;
        ElapsedMs.QuadPart /= CounterFreq.QuadPart;

        if (ConnectionInfo.DiskNumber != -1) {
            std::string DiskPath = "\\\\.\\PhysicalDrive" + std::to_string(
                ConnectionInfo.DiskNumber);

            HANDLE DiskHandle = CreateFileA(
                DiskPath.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                NULL,
                NULL);
            if (INVALID_HANDLE_VALUE != DiskHandle) {
                // Disk available
                CloseHandle(DiskHandle);
                return DiskPath;
            }

            DWORD Status = GetLastError();
            if (Status != ERROR_NO_SUCH_DEVICE &&
                    Status != ERROR_FILE_NOT_FOUND) {
                std::string Msg = "unable to open WNBD disk, error: " +
                    std::to_string(Status);
                throw std::runtime_error(Msg);
            }
        }

        // Disk not available yet
        if (TimeoutMs > ElapsedMs.QuadPart) {
            Sleep(RetryInterval);
        }
    } while (TimeoutMs > ElapsedMs.QuadPart);

    std::string Msg = "couln't retrieve WNBD disk info, timed out";
    throw std::runtime_error(Msg);
}

void SetDiskWritable(std::string InstanceName)
{
    std::string DiskPath = GetDiskPath(InstanceName);

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        NULL,
        NULL);
    if (DiskHandle == INVALID_HANDLE_VALUE) {
        std::string Msg = "couldn't open wnbd disk: " + InstanceName +
            ", error: " + WinStrError(GetLastError());
        throw std::runtime_error(Msg);
    }
    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    SetDiskWritable(DiskHandle);
}

void SetDiskWritable(HANDLE DiskHandle)
{
    DWORD BytesReturned = 0;

    SET_DISK_ATTRIBUTES attributes = {0};
    attributes.Version = sizeof(attributes);
    attributes.Attributes = 0; // clear read-only flag
    attributes.AttributesMask = DISK_ATTRIBUTE_READ_ONLY;

    BOOL Succeeded = DeviceIoControl(
        DiskHandle,
        IOCTL_DISK_SET_DISK_ATTRIBUTES,
        (LPVOID) &attributes,
        (DWORD) sizeof(attributes),
        NULL,
        0,
        &BytesReturned,
        NULL);
    if (!Succeeded) {
        std::string Msg = "couln't set wnbd disk as writable, error: " +
            WinStrError(GetLastError());
        throw std::runtime_error(Msg);
    }
}

std::string GetEnv(std::string Name) {
    char* ValBuff;
    size_t ReqSize;

    // VS throws warnings when using getenv, which it considers unsafe.
    errno_t Err = getenv_s(&ReqSize, NULL, 0, Name.c_str());
    if (Err && Err != ERANGE) {
        std::string Msg = (
            "couldn't retrieve env var: " + Name +
            ". Error: " + std::to_string(Err));
    }
    if (!ReqSize) {
        return "";
    }

    ValBuff = (char*) malloc(ReqSize);
    if (!ValBuff) {
        std::string Msg = "couldn't allocate: " + std::to_string(ReqSize);
        throw std::runtime_error(Msg);
    }

    Err = getenv_s(&ReqSize, ValBuff, ReqSize, Name.c_str());
    if (Err) {
        free(ValBuff);

        std::string Msg = (
            "couldn't retrieve env var: " + Name +
            ". Error: " + std::to_string(Err));
        throw std::runtime_error(Msg);
    }

    std::string ValStr = std::string(ValBuff, ReqSize);
    free(ValBuff);
    return ValStr;
}

std::string ByteArrayToHex(BYTE* arr, int length) {
    std::stringstream ss;

    for (int i = 0; i < length; i++)
        ss << std::hex << (int) arr[i] << " ";

    return ss.str();
}
