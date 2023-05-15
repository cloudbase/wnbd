/*
 * Copyright (C) 2023 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "nbd_daemon.h"
#include "nbd_protocol.h"
#include "utils.h"

#define _NTSCSI_USER_MODE_
#include <scsi.h>

DWORD SetTcpFlags(SOCKET Fd)
{
    LogDebug("Setting TCP_NODELAY.");

    int Flag = 1;
    int Ret = setsockopt(Fd, IPPROTO_TCP, TCP_NODELAY,
                         (char*) &Flag, sizeof(Flag));
    if (Ret) {
        auto Err = WSAGetLastError();
        LogError("Couldn't set TCP_NODELAY. "
                 "Error: %d. Error message: %s",
                 Err, win32_strerror(Err).c_str());
    }
    return Ret;
}

DWORD NbdDaemon::ConnectNbdServer(
    std::string HostName,
    uint32_t PortNumber)
{
    LogInfo("Initializing NBD connection.");
    struct addrinfo Hints = { 0 };
    struct addrinfo* Ai = nullptr;
    struct addrinfo* Rp = nullptr;

    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_protocol = IPPROTO_TCP;

    int Ret = getaddrinfo(HostName.c_str(), std::to_string(PortNumber).c_str(),
                          &Hints, &Ai);
    if (Ret) {
        auto Err = WSAGetLastError();
        LogWarning("Couldn't resolve address: %s. "
                   "Error: %d. Error message: %s",
                   HostName.c_str(),
                   Err, win32_strerror(Err).c_str());
        goto exit;
    }

    for (Rp = Ai; Rp != NULL; Rp = Rp->ai_next) {
        Socket = socket(Rp->ai_family, Rp->ai_socktype, Rp->ai_protocol);

        if (Socket == INVALID_SOCKET) {
            auto Err = WSAGetLastError();
            LogWarning("Initializing socket failed. "
                       "Error: %d. Error message: %s",
                       Err, win32_strerror(Err).c_str());
            continue;
        }

        if (connect(Socket, Rp->ai_addr, (int) Rp->ai_addrlen) != SOCKET_ERROR) {
            break;      /* success */
        }

        auto Err = WSAGetLastError();
        LogWarning("Connect failed. "
                   "Error: %d. Error message: %s",
                   Err, win32_strerror(Err).c_str());
        closesocket(Socket);
        Socket = INVALID_SOCKET;
    }

    if (!Rp) {
        Ret = ERROR_NOT_CONNECTED;
    }

exit:
    if (Ai) {
        freeaddrinfo(Ai);
    }

    if (Ret) {
        LogError("Could not connect to %s.", HostName.c_str());
    } else {
        LogInfo("Successfully connected to NBD server: %s.", HostName.c_str());
    }
    return Ret;
}

DWORD NbdDaemon::DisconnectNbd()
{
    DWORD Retval = 0;
    if (Socket != INVALID_SOCKET) {
        LogInfo("Removing NBD connection.");
        if (shutdown(Socket, SD_BOTH)) {
            Retval = SOCKET_ERROR;
            auto Err = WSAGetLastError();
            LogWarning("NBD socket shutdown failed. "
                       "Error: %d. Error message: %s",
                       Err, win32_strerror(Err).c_str());
        }
        if (closesocket(Socket)) {
            Retval = SOCKET_ERROR;
            auto Err = WSAGetLastError();
            LogWarning("NBD socket close failed. "
                       "Error: %d. Error message: %s",
                       Err, win32_strerror(Err).c_str());
        }
        Socket = INVALID_SOCKET;
        LogInfo("NBD connection closed.");
    } else {
        LogDebug("Socket already closed.");
    }
    return Retval;
}

DWORD NbdDaemon::TryStart()
{
    WnbdProps.MaxUnmapDescCount = 1;
    WnbdProps.Flags.PersistResSupported = 0;

    if (!WnbdProps.BlockSize) {
        WnbdProps.BlockSize = WNBD_DEFAULT_BLOCK_SIZE;
    }
    if (WnbdProps.BlockSize != 512) {
        // TODO: fix the 4k sector size issue, potentially allowing
        // even larger sector sizes.
        LogError("Invalid block size: %d. "
                 "Only 512 is allowed for the time being.", WnbdProps.BlockSize);
        return ERROR_INVALID_PARAMETER;
    }

    // We're preallocating buffers for NBD write requests, enough to fit the
    // maxium transfer length plus the NBD request header.
    PreallocatedWBuffSz = WNBD_DEFAULT_MAX_TRANSFER_LENGTH +
                          sizeof(NBD_REQUEST);
    PreallocatedWBuff = (PVOID) calloc(1, PreallocatedWBuffSz);
    if (!PreallocatedWBuff) {
        LogError("Could not allocate %d bytes.", PreallocatedWBuffSz);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    PreallocatedRBuffSz = WNBD_DEFAULT_MAX_TRANSFER_LENGTH;
    PreallocatedRBuff = (PVOID) calloc(1, PreallocatedRBuffSz);
    if (!PreallocatedRBuff) {
        LogError("Could not allocate %d bytes.", PreallocatedRBuffSz);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    DWORD Err = ConnectNbdServer(
        WnbdProps.NbdProperties.Hostname,
        WnbdProps.NbdProperties.PortNumber);
    if (Err) {
        return Err;
    }
    Err = SetTcpFlags(Socket);
    if (Err) {
        return Err;
    }

    UINT16 NbdFlags = 0;
    if (!WnbdProps.NbdProperties.Flags.SkipNegotiation) {
        uint64_t DiskSize = 0;

        Err = NbdNegotiate(Socket, &DiskSize, &NbdFlags,
                           WnbdProps.NbdProperties.ExportName,
                           NBD_FLAG_FIXED_NEWSTYLE);
        if (Err) {
            LogError("NBD negotiation failed.");
            return Err;
        }

        WnbdProps.BlockCount = DiskSize / WnbdProps.BlockSize;
    }

    WnbdProps.Flags.ReadOnly |= CHECK_NBD_READONLY(NbdFlags);
    WnbdProps.Flags.UnmapSupported |= CHECK_NBD_SEND_TRIM(NbdFlags);
    WnbdProps.Flags.FlushSupported |= CHECK_NBD_SEND_FLUSH(NbdFlags);
    WnbdProps.Flags.FUASupported |= CHECK_NBD_SEND_FUA(NbdFlags);

    if (!WnbdProps.BlockCount ||
            WnbdProps.BlockCount > ULLONG_MAX / WnbdProps.BlockSize) {
        LogError("Invalid block size or block count. "
                 "Block size: %d. Block count: %lld.",
                 WnbdProps.BlockSize, WnbdProps.BlockCount);
        return ERROR_INVALID_PARAMETER;
    }

    LogInfo("Retrieved NBD flags: %d. Read-only: %d, TRIM enabled: %d, "
            "FLUSH enabled: %d, FUA enabled: %d.",
            NbdFlags,
            WnbdProps.Flags.ReadOnly,
            WnbdProps.Flags.UnmapSupported,
            WnbdProps.Flags.FlushSupported,
            WnbdProps.Flags.FUASupported);

    ReplyDispatcher = std::thread(&NbdDaemon::NbdReplyWorker, this);

    Err = WnbdCreate(
        &WnbdProps, (const PWNBD_INTERFACE) &WnbdInterface,
        this, &WnbdDisk);
    if (Err) {
        return Err;
    }

    // We currently use a single NBD connection, which is why
    // we'll stick with a single WNBD worker thread.
    Err = WnbdStartDispatcher(WnbdDisk, 1);
    if (Err) {
        return Err;
    }

    LogInfo("NBD mapping initialized successfully.");
    return 0;
}

DWORD NbdDaemon::Start()
{
    DWORD Retval = TryStart();
    if (Retval) {
        LogError("NBD daemon failed to initialize, attempting cleanup.");
        Shutdown(true);
        Wait();
    }
    return Retval;
}

DWORD NbdDaemon::Shutdown(bool HardRemove)
{
    std::unique_lock<std::mutex> Lock(ShutdownLock);

    if (!Terminated) {
        TerminateInProgress = true;

        if (WnbdDisk) {
            WNBD_REMOVE_OPTIONS RmOpt = { 0 };
            RmOpt.Flags.HardRemove = HardRemove;
            RmOpt.Flags.HardRemoveFallback = TRUE;
            RmOpt.SoftRemoveTimeoutMs = WNBD_DEFAULT_RM_TIMEOUT_MS;
            RmOpt.SoftRemoveRetryIntervalMs =  WNBD_DEFAULT_RM_RETRY_INTERVAL_MS;
            // We're requesting the disk to be removed but continue serving IO
            // requests until the driver sends us the "Disconnect" event, unless
            // a hard remove was requested.
            DWORD Err = WnbdRemove(WnbdDisk, &RmOpt);
            if (Err) {
                if (Err == ERROR_FILE_NOT_FOUND) {
                    LogDebug("WNBD mapping already removed.");
                } else {
                    LogError("Couldn't remove WNBD mapping. "
                             "Error: %d. Error message: %s",
                             Err, win32_strerror(Err).c_str());
                    return Err;
                }
            }
        }

        // We're setting this here in order to stop the NBD dispatchers.
        Terminated = true;
        DWORD Err = DisconnectNbd();
        if (Err) {
            LogWarning("Couldn't remove NBD connection cleanly.");
        }
    }

    return 0;
}

DWORD NbdDaemon::Wait()
{
    DWORD Err = 0;
    if (WnbdDisk) {
        LogInfo("Waiting for the WNBD dispatchers.");

        Err = WnbdWaitDispatcher(WnbdDisk);
        if (Err) {
            LogError("Failed waiting for WNBD dispatchers to stop. "
                     "Error: %d. Error message: %s",
                     Err, win32_strerror(Err).c_str());
        } else {
            LogInfo("WNBD dispatchers stopped.");
        }
    }

    return Err;
}

void NbdDaemon::Read(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    PVOID Buffer,
    UINT64 BlockAddress,
    UINT32 BlockCount,
    BOOLEAN ForceUnitAccess)
{
    NbdDaemon* Handler = nullptr;
    WnbdGetUserContext(Disk, (PVOID*)&Handler);
    assert(Handler);

    {
        std::unique_lock Lock{Handler->PendingRequestsLock};
        Handler->PendingRequests.emplace(std::make_pair(
            RequestHandle,
            PendingRequestInfo {
                .RequestHandle = RequestHandle,
                .RequestType = WnbdReqTypeRead,
                .Length = BlockCount * Handler->WnbdProps.BlockSize,
            }
        ));
    }

    // NBD doesn't currently support read FUA.
    DWORD Err = NbdRequest(
        Handler->Socket,
        BlockAddress * Handler->WnbdProps.BlockSize,
        BlockCount * Handler->WnbdProps.BlockSize,
        RequestHandle,
        NBD_CMD_READ);
    if (Err) {
        // TODO: try resetting the connection instead.
        LogError("Couldn't submit read request. Closing connection.");
        Handler->Shutdown(true);
    }
}

void NbdDaemon::Write(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    PVOID Buffer,
    UINT64 BlockAddress,
    UINT32 BlockCount,
    BOOLEAN ForceUnitAccess)
{
    NbdDaemon* Handler = nullptr;
    WnbdGetUserContext(Disk, (PVOID*)&Handler);
    assert(Handler);

    DWORD NbdTransmissionFlags = 0;
    if (ForceUnitAccess) {
        NbdTransmissionFlags |= NBD_CMD_FLAG_FUA;
    }

    {
        std::unique_lock Lock{Handler->PendingRequestsLock};
        Handler->PendingRequests.emplace(std::make_pair(
            RequestHandle,
            PendingRequestInfo {
                .RequestHandle = RequestHandle,
                .RequestType = WnbdReqTypeWrite,
                .Length = BlockCount * Handler->WnbdProps.BlockSize,
            }
        ));
    }

    DWORD Err = NbdSendWrite(
        Handler->Socket,
        BlockAddress * Handler->WnbdProps.BlockSize,
        BlockCount * Handler->WnbdProps.BlockSize,
        Buffer,
        &Handler->PreallocatedWBuff,
        &Handler->PreallocatedWBuffSz,
        RequestHandle,
        NbdTransmissionFlags);
    if (Err) {
        // TODO: try resetting the connection instead.
        LogError("Couldn't submit write request. Closing connection.");
        Handler->Shutdown(true);
    }
}

void NbdDaemon::Flush(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    UINT64 BlockAddress,
    UINT32 BlockCount)
{
    NbdDaemon* Handler = nullptr;
    WnbdGetUserContext(Disk, (PVOID*)&Handler);
    assert(Handler);

    {
        std::unique_lock Lock{Handler->PendingRequestsLock};
        Handler->PendingRequests.emplace(std::make_pair(
            RequestHandle,
            PendingRequestInfo {
                .RequestHandle = RequestHandle,
                .RequestType = WnbdReqTypeFlush,
                .Length = BlockCount * Handler->WnbdProps.BlockSize,
            }
        ));
    }

    DWORD Err = NbdRequest(
        Handler->Socket,
        BlockAddress * Handler->WnbdProps.BlockSize,
        BlockCount * Handler->WnbdProps.BlockSize,
        RequestHandle,
        NBD_CMD_FLUSH);
    if (Err) {
        // TODO: try resetting the connection instead.
        LogError("Couldn't submit flush request. Closing connection.");
        Handler->Shutdown(true);
    }
}

void NbdDaemon::Unmap(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    PWNBD_UNMAP_DESCRIPTOR Descriptors,
    UINT32 Count)
{
    NbdDaemon* Handler = nullptr;
    WnbdGetUserContext(Disk, (PVOID*)&Handler);
    assert(Handler);
    assert(1 == Count);

    {
        std::unique_lock Lock{Handler->PendingRequestsLock};
        Handler->PendingRequests.emplace(std::make_pair(
            RequestHandle,
            PendingRequestInfo {
                .RequestHandle = RequestHandle,
                .RequestType = WnbdReqTypeUnmap,
                .Length = Descriptors[0].BlockCount * Handler->WnbdProps.BlockSize,
            }
        ));
    }

    DWORD Err = NbdRequest(
        Handler->Socket,
        Descriptors[0].BlockAddress * Handler->WnbdProps.BlockSize,
        Descriptors[0].BlockCount * Handler->WnbdProps.BlockSize,
        RequestHandle,
        NBD_CMD_TRIM);
    if (Err) {
        // TODO: try resetting the connection instead.
        LogError("Couldn't submit unmap request. Closing connection.");
        Handler->Shutdown(true);
    }
}

void NbdDaemon::NbdReplyWorker()
{
    // For performance reasons, we're reusing the overlapped structure
    // when submitting IO replies to WNBD.
    OVERLAPPED Overlapped = { 0 };
    HANDLE OverlappedEvent = CreateEventA(0, TRUE, TRUE, NULL);
    DWORD Err = 0;
    if (!OverlappedEvent) {
        Err = GetLastError();
        LogError("Could not create event. Error: %d. Error message: %s",
                 Err, win32_strerror(Err).c_str());
        Shutdown(true);
        return;
    }
    Overlapped.hEvent = OverlappedEvent;
    std::unique_ptr<void, decltype(&CloseHandle)> HandleCloser(
        OverlappedEvent, &CloseHandle);

    while (!Terminated) {
        if (!ResetEvent(OverlappedEvent)) {
            Err = GetLastError();
            LogError("Could not reset event. Error: %d. Error message: %s",
                     Err, win32_strerror(Err).c_str());
            Shutdown(true);
            return;
        }

        Err = ProcessNbdReply(&Overlapped);
        if (Err) {
            if (Err == ERROR_CANCELLED || Err == ERROR_GRACEFUL_DISCONNECT) {
                LogInfo("Connection closed.");
            } else {
                // TODO: try resetting the connection instead.
                LogError("Couldn't process NBD reply. Closing connection.");
            }
            Shutdown(true);
            return;
        }
    }
}

DWORD NbdDaemon::ProcessNbdReply(LPOVERLAPPED Overlapped)
{
    NBD_REPLY Reply = { 0 };

    DWORD Err = NbdReadReply(Socket, &Reply);
    if (Err) {
        return Err;
    }

    PendingRequestInfo Request = { 0 };

    {
        std::unique_lock Lock{PendingRequestsLock};
        auto RequestIt = PendingRequests.find(Reply.Handle);
        if (RequestIt == PendingRequests.end()) {
            LogError("Received unexpected NBD reply hanldle: %lld.",
                     Reply.Handle);
            return ERROR_INVALID_PARAMETER;
        }

        Request = RequestIt->second;
        PendingRequests.erase(RequestIt);
    }

    PVOID DataBuffer = nullptr;
    UINT32 DataBufferSize = 0;    

    if (!Reply.Error && Request.RequestType == WnbdReqTypeRead) {
        // We shouldn't get requests larger than the maximum transfer
        // length.
        if (Request.Length > PreallocatedRBuffSz) {
            LogError("Invalid read request length: %ld. Maximum length: %ld.",
                     PreallocatedRBuffSz, PreallocatedRBuffSz);
            return ERROR_FILE_TOO_LARGE;
        }

        Err = RecvExact(Socket, PreallocatedRBuff, Request.Length);
        if (Err) {
            LogError("Couldn't retrieve NBD read payload.");
            return Err;
        }

        DataBuffer = PreallocatedRBuff;
        DataBufferSize = PreallocatedRBuffSz;
    }

    WNBD_IO_RESPONSE Resp = { 0 };
    Resp.RequestHandle = Reply.Handle;
    Resp.RequestType = Request.RequestType;
    if (Reply.Error) {
        // TODO: parse the actual error
        WnbdSetSense(
            &Resp.Status,
            SCSI_SENSE_MEDIUM_ERROR,
            SCSI_ADSENSE_UNRECOVERED_ERROR);
    }

    Err = WnbdSendResponseEx(
        WnbdDisk,
        &Resp,
        DataBuffer,
        DataBufferSize,
        Overlapped);
    if (Err && TerminateInProgress) {
        // Suppress errors that might occur because of pending disk removals.
        LogDebug("Daemon terminating, ignoring the error received while "
                 "attempting to submit NBD reply.");
        Err = 0;
    }
    if (Err == ERROR_IO_PENDING) {
        DWORD BytesReturned = 0;
        if (!GetOverlappedResult(WnbdDisk->Handle,
                                 Overlapped,
                                 &BytesReturned, TRUE)) {
            Err = GetLastError();
        } else {
            Err = 0;
        }
    }
    if (Err) {
        LogError("Couldn't send IO response. "
                 "Request id: %lld. "
                 "Error: %d. Error message: %s",
                 Reply.Handle, Err, win32_strerror(Err).c_str());
    }

    return Err;
}
