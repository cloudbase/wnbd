/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <mutex>
#include <string.h>

#include <wnbd.h>

#include "request_log.h"

#define IO_REQ_WORKERS 2
#define WNBD_OWNER_NAME "WnbdTests"

// The following byte will be used to fill read buffers,
// allowing us to make assertions.
#define READ_BYTE_CONTENT 0x0f
#define WRITE_BYTE_CONTENT 0x0a

class MockWnbdDaemon
{
private:
    std::string InstanceName;
    uint64_t BlockCount;
    uint32_t BlockSize;
    bool ReadOnly;
    bool CacheEnabled;
    bool UseCustomNaaIdentifier;
    bool UseCustomDeviceSerial;
    bool EnablePersistentReservations;

public:
    MockWnbdDaemon(
            std::string _InstanceName,
            uint64_t _BlockCount, uint32_t _BlockSize,
            bool _ReadOnly, bool _CacheEnabled,
            bool _UseCustomNaaIdentifier = false,
            bool _UseCustomDeviceSerial = false,
            bool _EnablePersistentReservations = false)
        : InstanceName(_InstanceName)
        , BlockCount(_BlockCount)
        , BlockSize(_BlockSize)
        , ReadOnly(_ReadOnly)
        , CacheEnabled(_CacheEnabled)
        , UseCustomNaaIdentifier(_UseCustomNaaIdentifier)
        , UseCustomDeviceSerial(_UseCustomDeviceSerial)
        , EnablePersistentReservations(_EnablePersistentReservations)
    {
    };
    ~MockWnbdDaemon();

    void Start();
    // Wait for the daemon to stop, which normally happens when the driver
    // passes the "Disconnect" request.
    void Wait();
    void Shutdown();

    void SetStatus(WNBD_STATUS& Status) {
        MockStatus = Status;
    }

    RequestLog ReqLog;

private:
    bool Started = false;
    bool Terminated = false;
    bool TerminateInProgress = false;
    PWNBD_DISK WnbdDisk = nullptr;

    WNBD_STATUS MockStatus = { 0 };

    std::mutex ShutdownLock;

    // WNBD IO entry points
    static void Read(
        PWNBD_DISK Disk,
        UINT64 RequestHandle,
        PVOID Buffer,
        UINT64 BlockAddress,
        UINT32 BlockCount,
        BOOLEAN ForceUnitAccess);
    static void Write(
        PWNBD_DISK Disk,
        UINT64 RequestHandle,
        PVOID Buffer,
        UINT64 BlockAddress,
        UINT32 BlockCount,
        BOOLEAN ForceUnitAccess);
    static void Flush(
        PWNBD_DISK Disk,
        UINT64 RequestHandle,
        UINT64 BlockAddress,
        UINT32 BlockCount);
    static void Unmap(
        PWNBD_DISK Disk,
        UINT64 RequestHandle,
        PWNBD_UNMAP_DESCRIPTOR Descriptors,
        UINT32 Count);
    static void PersistentReserveIn(
        PWNBD_DISK Disk,
        UINT64 RequestHandle,
        UINT8 ServiceAction);
    static void PersistentReserveOut(
        PWNBD_DISK Disk,
        UINT64 RequestHandle,
        UINT8 ServiceAction,
        UINT8 Scope,
        UINT8 Type,
        PVOID Buffer,
        UINT32 ParameterListLength);

    static constexpr WNBD_INTERFACE MockWnbdInterface =
    {
        Read,
        Write,
        Flush,
        Unmap,
        PersistentReserveIn,
        PersistentReserveOut
    };

    void SendIoResponse(
        UINT64 RequestHandle,
        WnbdRequestType RequestType,
        WNBD_STATUS& Status,
        PVOID DataBuffer,
        UINT32 DataBufferSize
    );
public:
    PWNBD_DISK GetDisk();

    void TerminatingInProgress();
};
