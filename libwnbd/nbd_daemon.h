/*
 * Copyright (C) 2023 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>

#include "nbd_protocol.h"
#include "wnbd_log.h"

// Minimal information to identify pending requests.
struct PendingRequestInfo
{
    UINT64 RequestHandle;
    WnbdRequestType RequestType;
    UINT32 Length;
};

class NbdDaemon
{
private:
    WNBD_PROPERTIES WnbdProps = {0};

    SOCKET Socket = INVALID_SOCKET;

    std::mutex ShutdownLock;
    bool Terminated = false;
    bool TerminateInProgress = false;
    PWNBD_DISK WnbdDisk = nullptr;

    void* PreallocatedWBuff = nullptr;
    ULONG PreallocatedWBuffSz = 0;
    void* PreallocatedRBuff = nullptr;
    ULONG PreallocatedRBuffSz = 0;

    // NBD replies provide limited information. We need to track
    // the request size on our own in order to know how large is
    // the data buffer that follows the reply header.
    std::unordered_map<UINT64, PendingRequestInfo> PendingRequests;

    std::thread ReplyDispatcher;

public:
    NbdDaemon(PWNBD_PROPERTIES Properties)
    {
        WnbdProps = *Properties;
    }

    ~NbdDaemon()
    {
        Shutdown();
        Wait();

        if (ReplyDispatcher.joinable()) {
            LogInfo("Waiting for NBD reply dispatcher thread.");
            ReplyDispatcher.join();
            LogInfo("NBD reply dispatcher stopped.");
        }

        if (PreallocatedRBuff) {
            free(PreallocatedRBuff);
        }
        if (PreallocatedWBuff) {
            free(PreallocatedWBuff);
        }

        if (WnbdDisk) {
            WnbdClose(WnbdDisk);
        }
    }

    DWORD Start();
    DWORD Wait();
    DWORD Shutdown(bool HardRemove=false);

private:
    DWORD TryStart();
    DWORD ConnectNbdServer(
        std::string HostName,
        uint32_t PortNumber);
    DWORD DisconnectNbd();

    void NbdReplyWorker();
    DWORD ProcessNbdReply(LPOVERLAPPED Overlapped);

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

    static constexpr WNBD_INTERFACE WnbdInterface =
    {
        Read,
        Write,
        Flush,
        Unmap,
    };
};