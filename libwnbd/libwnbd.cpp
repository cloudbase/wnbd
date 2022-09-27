/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <windows.h>

#include <stdio.h>

#define _NTSCSI_USER_MODE_
#include <scsi.h>

#include "wnbd.h"
#include "wnbd_log.h"
#include "utils.h"
#include "version.h"

DWORD WnbdCreate(
    const PWNBD_PROPERTIES Properties,
    const PWNBD_INTERFACE Interface,
    PVOID Context,
    PWNBD_DISK* PDisk)
{
    DWORD ErrorCode = ERROR_SUCCESS;
    PWNBD_DISK Disk = NULL;

    Disk = (PWNBD_DISK) calloc(1, sizeof(WNBD_DISK));
    if (!Disk) {
        LogError("Failed to allocate %d bytes.", sizeof(WNBD_DISK));
        return ERROR_OUTOFMEMORY;
    }

    Disk->Context = Context;
    Disk->Interface = Interface;
    Disk->Properties = *Properties;
    ErrorCode = WnbdOpenAdapter(&Disk->Handle);

    LogDebug("Mapping device. Name=%s, Serial=%s, Owner=%s, "
             "BC=%llu, BS=%lu, RO=%u, Flush=%u, PerRes=%u, "
             "Unmap=%u, UnmapAnchor=%u, MaxUnmapDescCount=%u, "
             "Nbd=%u.",
             Properties->InstanceName,
             Properties->SerialNumber,
             Properties->Owner,
             Properties->BlockCount,
             Properties->BlockSize,
             Properties->Flags.ReadOnly,
             Properties->Flags.FlushSupported,
             Properties->Flags.PersistResSupported,
             Properties->Flags.UnmapSupported,
             Properties->Flags.UnmapAnchorSupported,
             Properties->MaxUnmapDescCount,
             Properties->Flags.UseNbd);
    if (Properties->Flags.UseNbd) {
        LogDebug("Nbd properties: Hostname=%s, Port=%u, ExportName=%s, "
                 "SkipNegotiation=%u.",
                 Properties->NbdProperties.Hostname,
                 Properties->NbdProperties.PortNumber,
                 Properties->NbdProperties.ExportName,
                 Properties->NbdProperties.Flags.SkipNegotiation);
    }

    if (ErrorCode) {
        goto Exit;
    }

    ErrorCode = WnbdIoctlCreate(
        Disk->Handle, &Disk->Properties,
        &Disk->ConnectionInfo, NULL);
    if (ErrorCode) {
        goto Exit;
    }

    LogInfo("Mapped device. Connection id: %llu.",
            Disk->ConnectionInfo.ConnectionId);

    *PDisk = Disk;

Exit:
    if (ErrorCode) {
        WnbdClose(Disk);
    }

    return ErrorCode;
}

DWORD PnpRemoveDevice(
    DEVINST DiskDeviceInst,
    DWORD TimeoutMs,
    DWORD RetryIntervalMs)
{
    DWORD Status = 0;

    LARGE_INTEGER StartTime, CurrTime, ElapsedMs, CounterFreq;
    QueryPerformanceFrequency(&CounterFreq);
    QueryPerformanceCounter(&StartTime);

    BOOLEAN RemoveVetoed = FALSE;
    BOOLEAN TimeLeft = TRUE;
    WCHAR VetoName[MAX_PATH];
    do {
        PNP_VETO_TYPE VetoType = PNP_VetoTypeUnknown;
        memset(VetoName, 0, sizeof(VetoName));

        // We're supposed to use CM_Query_And_Remove_SubTreeW when
        // SurpriseRemovalOK is not set, otherwise we'd have to go
        // with CM_Request_Device_EjectW.
        DWORD CMStatus = CM_Query_And_Remove_SubTreeW(
            DiskDeviceInst, &VetoType, VetoName, MAX_PATH, CM_REMOVE_NO_RESTART);

        QueryPerformanceCounter(&CurrTime);
        ElapsedMs.QuadPart = CurrTime.QuadPart - StartTime.QuadPart;
        ElapsedMs.QuadPart *= 1000;
        ElapsedMs.QuadPart /= CounterFreq.QuadPart;

        RemoveVetoed = CMStatus == CR_REMOVE_VETOED;
        TimeLeft = !TimeoutMs || (TimeoutMs > ElapsedMs.QuadPart);
        if (CMStatus) {
            if (!RemoveVetoed){
                Status = ERROR_REMOVE_FAILED;
            }
            else if (!TimeLeft) {
                Status = ERROR_TIMEOUT;
            }

            LogDebug(
               "Could not remove device. CM status: %d, "
               "veto type %d, veto name: %ls. Time elapsed: %.2fs",
               CMStatus, VetoType, VetoName,
               ElapsedMs.QuadPart / 1000.0);
        }
        if (RemoveVetoed && TimeLeft) {
            Sleep(RetryIntervalMs);
        }
    } while (RemoveVetoed && TimeLeft);

    return Status;
}

DWORD WnbdPnpRemoveDevice(
    PWSTR PNPDeviceID,
    DWORD TimeoutMs,
    DWORD RetryIntervalMs)
{
    DEVINST DiskDeviceInst = 0;
    DWORD CMStatus = CM_Locate_DevNode_ExW(
        &DiskDeviceInst, PNPDeviceID,
        CM_LOCATE_DEVNODE_NORMAL,
        NULL);
    if (CMStatus) {
        LogInfo("Could not open device: %ls. CM status: %d.",
                PNPDeviceID, CMStatus);
        return ERROR_OPEN_FAILED;
    }

    return PnpRemoveDevice(DiskDeviceInst, TimeoutMs, RetryIntervalMs);
}

DWORD WnbdRemove(
    PWNBD_DISK Disk,
    PWNBD_REMOVE_OPTIONS RemoveOptions)
{
    // TODO: check for null pointers.
    DWORD ErrorCode = ERROR_SUCCESS;

    LogDebug("Unmapping device %s.",
             Disk->Properties.InstanceName);
    if (!Disk->Handle || Disk->Handle == INVALID_HANDLE_VALUE) {
        LogDebug("WNBD device already removed.");
        return 0;
    }

    return WnbdRemoveEx(Disk->Properties.InstanceName, RemoveOptions);
}

DWORD WnbdRemoveEx(
    const char* InstanceName,
    PWNBD_REMOVE_OPTIONS RemoveOptions)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    WNBD_REMOVE_OPTIONS DefaultRmOpt = { 0 };
    DefaultRmOpt.Flags.HardRemoveFallback = TRUE;
    DefaultRmOpt.SoftRemoveTimeoutMs = WNBD_DEFAULT_RM_TIMEOUT_MS;
    DefaultRmOpt.SoftRemoveRetryIntervalMs =  WNBD_DEFAULT_RM_RETRY_INTERVAL_MS;

    if (!RemoveOptions) {
        RemoveOptions = &DefaultRmOpt;
    }

    BOOLEAN DiskFound = FALSE;
    if (!RemoveOptions->Flags.HardRemove) {
        WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
        Status = WnbdShow(InstanceName, &ConnectionInfo);
        if (Status) {
            return Status;
        }
        DiskFound = TRUE;
        // PnP subscribers are notified that the device is about to be removed
        // and can block the remove until ready.
        Status = WnbdPnpRemoveDevice(
            ConnectionInfo.PNPDeviceID,
            RemoveOptions->SoftRemoveTimeoutMs,
            RemoveOptions->SoftRemoveRetryIntervalMs);
        if (Status && Status != ERROR_FILE_NOT_FOUND) {
            if (!RemoveOptions->Flags.HardRemoveFallback) {
                LogError("Soft device removal failed. "
                         "Hard removal fallback disabled, exiting.");
                return Status;
            }
            else {
                LogWarning("Soft device removal failed. "
                           "Falling back to hard removal.");
            }
        }
        else {
            LogDebug("PNP removal succeeded");
        }
    }
    Status = WnbdIoctlRemove(Handle, InstanceName, NULL, NULL);
    CloseHandle(Handle);

    // We'll mask ERROR_FILE_NOT_FOUND errors that occur after actually
    // managing to locate the device. It might've been removed by the
    // PnP request.
    if (DiskFound && Status == ERROR_FILE_NOT_FOUND) {
        Status = 0;
    }

    return Status;
}

DWORD WnbdList(
    PWNBD_CONNECTION_LIST ConnectionList,
    PDWORD BufferSize)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlList(Handle, ConnectionList, BufferSize, NULL);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdShow(
    const char* InstanceName,
    PWNBD_CONNECTION_INFO ConnectionInfo)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlShow(Handle, InstanceName, ConnectionInfo, NULL);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdGetUserspaceStats(
    PWNBD_DISK Disk,
    PWNBD_USR_STATS Stats)
{
    if (!Disk) {
        LogError("No device specified.");
        return ERROR_INVALID_PARAMETER;
    }

    memcpy(Stats, &Disk->Stats, sizeof(WNBD_USR_STATS));
    return ERROR_SUCCESS;
}

DWORD WnbdGetUserContext(
    PWNBD_DISK Disk,
    PVOID* Context)
{
    if (!Disk) {
        LogError("No device specified.");
        return ERROR_INVALID_PARAMETER;
    }

    *Context = Disk->Context;
    return ERROR_SUCCESS;
}

DWORD WnbdGetDriverStats(
    const char* InstanceName,
    PWNBD_DRV_STATS Stats)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlStats(Handle, InstanceName, Stats, NULL);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdGetLibVersion(PWNBD_VERSION Version)
{
    if (!Version) {
        LogError("No input parameter.");
        return ERROR_INVALID_PARAMETER;
    }

    Version->Major = WNBD_VERSION_MAJOR;
    Version->Minor = WNBD_VERSION_MINOR;
    Version->Patch = WNBD_VERSION_PATCH;
    strncpy_s((char*)&Version->Description, WNBD_MAX_VERSION_STR_LENGTH,
              WNBD_VERSION_STR,
              min(strlen(WNBD_VERSION_STR), WNBD_MAX_VERSION_STR_LENGTH - 1));
    return 0;
}

DWORD WnbdGetDriverVersion(PWNBD_VERSION Version)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlVersion(Handle, Version, NULL);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdGetDrvOpt(
    const char* Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlGetDrvOpt(Handle, Name, Value, Persistent, NULL);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdSetDrvOpt(
    const char* Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlSetDrvOpt(Handle, Name, Value, Persistent, NULL);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdResetDrvOpt(
    const char* Name,
    BOOLEAN Persistent)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlResetDrvOpt(Handle, Name, Persistent, NULL);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdListDrvOpt(
    PWNBD_OPTION_LIST OptionList,
    PDWORD BufferSize,
    BOOLEAN Persistent)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenAdapter(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlListDrvOpt(Handle, OptionList, BufferSize, Persistent, NULL);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdGetConnectionInfo(
    PWNBD_DISK Disk,
    PWNBD_CONNECTION_INFO ConnectionInfo)
{
    if (!Disk) {
        LogError("No device specified.");
        return ERROR_INVALID_PARAMETER;
    }

    memcpy(ConnectionInfo, &Disk->ConnectionInfo, sizeof(WNBD_CONNECTION_INFO));
    return ERROR_SUCCESS;
}

DWORD OpenRegistryKey(HKEY RootKey, LPCSTR KeyName, BOOLEAN Create, HKEY* OutKey)
{
    HKEY Key = NULL;
    DWORD Status = RegOpenKeyExA(RootKey, KeyName, 0, KEY_ALL_ACCESS, &Key);

    if (Status) {
        if (Status == ERROR_FILE_NOT_FOUND && Create) {
            Status = RegCreateKeyExA(
                RootKey, KeyName, 0, NULL, REG_OPTION_NON_VOLATILE,
                KEY_ALL_ACCESS, NULL, &Key, NULL);
            if (Status) {
                LogError("Could not create registry key: %s. "
                         "Error: %d. Error message: %s",
                         KeyName, Status, win32_strerror(Status).c_str());
            }
        }
        else {
            LogError("Could not open registry key: %s. "
                     "Error: %d. Error message: %s",
                     KeyName, Status, win32_strerror(Status).c_str());
        }
    }

    if (!Status) {
        *OutKey = Key;
    }

    return Status;
}

void WnbdClose(PWNBD_DISK Disk)
{
    if (!Disk)
        return;

    LogDebug("Closing device");
    if (Disk->Handle)
        CloseHandle(Disk->Handle);

    if (Disk->DispatcherThreads)
        free(Disk->DispatcherThreads);

    free(Disk);
}

VOID WnbdSignalStopped(PWNBD_DISK Disk)
{
    LogDebug("Marking device as stopped.");
    if (Disk)
        Disk->Stopped = TRUE;
}

BOOLEAN WnbdIsStopped(PWNBD_DISK Disk)
{
    return Disk->Stopped;
}

BOOLEAN WnbdIsRunning(PWNBD_DISK Disk)
{
    return Disk->Started && !WnbdIsStopped(Disk);
}

DWORD WnbdStopDispatcher(PWNBD_DISK Disk, PWNBD_REMOVE_OPTIONS RemoveOptions)
{
    // By not setting the "Stopped" event, we allow the driver to finish
    // pending IO requests, which we'll continue processing until getting
    // the "Disconnect" request from the driver.
    DWORD Ret = 0;

    LogDebug("Stopping dispatcher.");
    if (!InterlockedExchange8((CHAR*)&Disk->Stopping, 1)) {
        Ret = WnbdRemove(Disk, RemoveOptions);
    }

    return Ret;
}

void WnbdSetSenseEx(PWNBD_STATUS Status, UINT8 SenseKey, UINT8 Asc, UINT64 Info)
{
    Status->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    Status->SenseKey = SenseKey;
    Status->ASC = Asc;
    Status->Information = Info;
    Status->InformationValid = 1;
}

void WnbdSetSense(PWNBD_STATUS Status, UINT8 SenseKey, UINT8 Asc)
{
    Status->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    Status->SenseKey = SenseKey;
    Status->ASC = Asc;
    Status->Information = 0;
    Status->InformationValid = 0;
}

DWORD WnbdSendResponseEx(
    PWNBD_DISK Disk,
    PWNBD_IO_RESPONSE Response,
    PVOID DataBuffer,
    UINT32 DataBufferSize,
    LPOVERLAPPED Overlapped)
{
    LogDebug(
        "Sending response: [%s] : (SS:%u, SK:%u, ASC:%u, I:%llu) # %llx "
        "@ %p~0x%x.",
        WnbdRequestTypeToStr(Response->RequestType),
        Response->Status.ScsiStatus,
        Response->Status.SenseKey,
        Response->Status.ASC,
        Response->Status.InformationValid ?
            Response->Status.Information : 0,
        Response->RequestHandle,
        DataBuffer,
        DataBufferSize);

    InterlockedIncrement64((PLONG64)&Disk->Stats.TotalReceivedReplies);
    InterlockedDecrement64((PLONG64)&Disk->Stats.PendingSubmittedRequests);

    if (Response->Status.ScsiStatus) {
        switch(Response->RequestType) {
        case WnbdReqTypeRead:
            InterlockedIncrement64((PLONG64)&Disk->Stats.ReadErrors);
            break;
        case WnbdReqTypeWrite:
            InterlockedIncrement64((PLONG64)&Disk->Stats.WriteErrors);
            break;
        case WnbdReqTypeFlush:
            InterlockedIncrement64((PLONG64)&Disk->Stats.FlushErrors);
            break;
        case WnbdReqTypeUnmap:
            InterlockedIncrement64((PLONG64)&Disk->Stats.UnmapErrors);
            break;
        case WnbdReqTypePersistResIn:
            InterlockedIncrement64((PLONG64)&Disk->Stats.PersistResInErrors);
            break;
        case WnbdReqTypePersistResOut:
            InterlockedIncrement64((PLONG64)&Disk->Stats.PersistResOutErrors);
            break;
        }
    }

    if (!WnbdIsRunning(Disk)) {
        LogDebug("Disk disconnected, cannot send response.");
        return ERROR_PIPE_NOT_CONNECTED;
    }

    InterlockedIncrement64((PLONG64)&Disk->Stats.PendingReplies);
    DWORD Status = WnbdIoctlSendResponse(
        Disk->Handle,
        Disk->ConnectionInfo.ConnectionId,
        Response,
        DataBuffer,
        DataBufferSize,
        Overlapped);
    InterlockedDecrement64((PLONG64)&Disk->Stats.PendingReplies);

    return Status;
}

DWORD WnbdSendResponse(
    PWNBD_DISK Disk,
    PWNBD_IO_RESPONSE Response,
    PVOID DataBuffer,
    UINT32 DataBufferSize)
{
    return WnbdSendResponseEx(Disk, Response, DataBuffer, DataBufferSize, NULL);
}

VOID WnbdHandleRequest(PWNBD_DISK Disk, PWNBD_IO_REQUEST Request,
                       PVOID Buffer)
{
    UINT8 AdditionalSenseCode = 0;
    BOOLEAN IsValid = TRUE;

    InterlockedIncrement64((PLONG64)&Disk->Stats.TotalReceivedRequests);
    InterlockedIncrement64((PLONG64)&Disk->Stats.UnsubmittedRequests);

    switch (Request->RequestType) {
        case WnbdReqTypeDisconnect:
            LogInfo("Received disconnect request.");
            WnbdSignalStopped(Disk);
            break;
        case WnbdReqTypeRead:
            if (!Disk->Interface->Read)
                goto Unsupported;
            LogDebug("Dispatching READ @ 0x%llx~0x%x # %llx.",
                     Request->Cmd.Read.BlockAddress,
                     Request->Cmd.Read.BlockCount,
                     Request->RequestHandle);
            Disk->Interface->Read(
                Disk,
                Request->RequestHandle,
                Buffer,
                Request->Cmd.Read.BlockAddress,
                Request->Cmd.Read.BlockCount,
                Request->Cmd.Read.ForceUnitAccess);

            InterlockedIncrement64((PLONG64)&Disk->Stats.TotalRWRequests);
            InterlockedAdd64((PLONG64)&Disk->Stats.TotalReadBlocks,
                             Request->Cmd.Read.BlockCount);
            break;
        case WnbdReqTypeWrite:
            if (!Disk->Interface->Write)
                goto Unsupported;
            LogDebug("Dispatching WRITE @ 0x%llx~0x%x # %llx." ,
                     Request->Cmd.Write.BlockAddress,
                     Request->Cmd.Write.BlockCount,
                     Request->RequestHandle);
            Disk->Interface->Write(
                Disk,
                Request->RequestHandle,
                Buffer,
                Request->Cmd.Write.BlockAddress,
                Request->Cmd.Write.BlockCount,
                Request->Cmd.Write.ForceUnitAccess);

            InterlockedIncrement64((PLONG64)&Disk->Stats.TotalRWRequests);
            InterlockedAdd64((PLONG64)&Disk->Stats.TotalWrittenBlocks,
                             Request->Cmd.Write.BlockCount);
            break;
        case WnbdReqTypeFlush:
            if (!Disk->Interface->Flush || !Disk->Properties.Flags.FlushSupported)
                goto Unsupported;
            LogDebug("Dispatching FLUSH @ 0x%llx~0x%x # %llx." ,
                     Request->Cmd.Flush.BlockAddress,
                     Request->Cmd.Flush.BlockCount,
                     Request->RequestHandle);
            Disk->Interface->Flush(
                Disk,
                Request->RequestHandle,
                Request->Cmd.Flush.BlockAddress,
                Request->Cmd.Flush.BlockCount);
            break;
        case WnbdReqTypeUnmap:
            if (!Disk->Interface->Unmap || !Disk->Properties.Flags.UnmapSupported)
                goto Unsupported;
            if (!Disk->Properties.Flags.UnmapAnchorSupported &&
                Request->Cmd.Unmap.Anchor)
            {
                LogDebug("Unmap 'anchored' state not supported.");
                AdditionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
                goto Unsupported;
            }

            if (Disk->Properties.MaxUnmapDescCount < Request->Cmd.Unmap.Count)
            {
                LogDebug("Too many unmap descriptors: %d. Maximum supported: %d",
                         Request->Cmd.Unmap.Count, Disk->Properties.MaxUnmapDescCount);
                AdditionalSenseCode = SCSI_ADSENSE_INVALID_FIELD_PARAMETER_LIST;
                goto Unsupported;
            }

            LogDebug("Dispatching UNMAP # %llx.",
                     Request->RequestHandle);
            Disk->Interface->Unmap(
                Disk,
                Request->RequestHandle,
                (PWNBD_UNMAP_DESCRIPTOR)Buffer,
                Request->Cmd.Unmap.Count);
            break;
        case WnbdReqTypePersistResIn:
            if (!Disk->Interface->PersistResIn ||
                    !Disk->Properties.Flags.PersistResSupported)
                goto Unsupported;
            LogDebug("Dispatching PERSISTENT_RESERVE_IN(action=0x%x) "
                        "allocLen=0x%x # %llx.",
                     Request->Cmd.PersistResIn.ServiceAction,
                     Request->Cmd.PersistResIn.AllocationLength,
                     Request->RequestHandle);

            Disk->Interface->PersistResIn(
                Disk,
                Request->RequestHandle,
                Request->Cmd.PersistResIn.ServiceAction);
            break;
        case WnbdReqTypePersistResOut:
            if (!Disk->Interface->PersistResOut ||
                    !Disk->Properties.Flags.PersistResSupported)
                goto Unsupported;
            LogDebug("Dispatching PERSISTENT_RESERVE_OUT"
                        "(action=0x%x, scope=0x%x, type=0x%x) "
                        "paramListLen=0x%x # %llx.",
                     Request->Cmd.PersistResOut.ServiceAction,
                     Request->Cmd.PersistResOut.Scope,
                     Request->Cmd.PersistResOut.Type,
                     Request->Cmd.PersistResOut.ParameterListLength,
                     Request->RequestHandle);
            Disk->Interface->PersistResOut(
                Disk,
                Request->RequestHandle,
                Request->Cmd.PersistResOut.ServiceAction,
                Request->Cmd.PersistResOut.Scope,
                Request->Cmd.PersistResOut.Type,
                Buffer,
                Request->Cmd.PersistResOut.ParameterListLength);
            break;
        default:
        Unsupported:
            LogDebug("Received unsupported command. "
                     "Request type: %d, request handle: %llu.",
                     Request->RequestType,
                     Request->RequestHandle);
            IsValid = FALSE;
            if (!AdditionalSenseCode)
                AdditionalSenseCode = SCSI_ADSENSE_ILLEGAL_COMMAND;

            WNBD_IO_RESPONSE Response = { 0 };
            Response.RequestType = Request->RequestType;
            Response.RequestHandle = Request->RequestHandle;
            WnbdSetSense(&Response.Status,
                         SCSI_SENSE_ILLEGAL_REQUEST,
                         AdditionalSenseCode);
            // Avoid negative count
            InterlockedIncrement64((PLONG64)&Disk->Stats.PendingSubmittedRequests);
            DWORD Status = WnbdSendResponse(Disk, &Response, NULL, 0);
            
            if (Status == ERROR_NOT_FOUND) {
                LogDebug("Unable to send response, device not found. Performing cleanup.");
                WnbdSignalStopped(Disk);
            }

            break;
    }

    InterlockedDecrement64((PLONG64)&Disk->Stats.UnsubmittedRequests);
    if (IsValid) {
        InterlockedIncrement64((PLONG64)&Disk->Stats.TotalSubmittedRequests);
        InterlockedIncrement64((PLONG64)&Disk->Stats.PendingSubmittedRequests);
    } else {
        InterlockedIncrement64((PLONG64)&Disk->Stats.InvalidRequests);
    }
}

DWORD WnbdDispatcherLoop(PWNBD_DISK Disk)
{
    DWORD ErrorCode = 0;
    WNBD_IO_REQUEST Request;
    DWORD BufferSize = WNBD_DEFAULT_MAX_TRANSFER_LENGTH;
    PVOID Buffer = NULL;
    OVERLAPPED Overlapped = { 0 };

    HANDLE OverlappedEvent = CreateEventA(0, TRUE, TRUE, NULL);
    if (!OverlappedEvent) {
        ErrorCode = GetLastError();
        LogError("Could not create event. Error: %d. Error message: %s",
                 ErrorCode, win32_strerror(ErrorCode).c_str());
        goto Exit;
        
    }
    Overlapped.hEvent = OverlappedEvent;

    Buffer = malloc(BufferSize);
    if (!Buffer) {
        LogError("Could not allocate %d bytes.", BufferSize);
        ErrorCode = ERROR_OUTOFMEMORY;
        goto Exit;
    }

    while (WnbdIsRunning(Disk)) {
        if (!ResetEvent(OverlappedEvent)) {
            ErrorCode = GetLastError();
            LogError("Could not reset event. Error: %d. Error message: %s",
                     ErrorCode, win32_strerror(ErrorCode).c_str());
            break;
        }

        ErrorCode = WnbdIoctlFetchRequest(
            Disk->Handle,
            Disk->ConnectionInfo.ConnectionId,
            &Request,
            Buffer,
            BufferSize,
            &Overlapped);

        if (ErrorCode == ERROR_IO_PENDING) {
            DWORD BytesReturned = 0;
            if (!GetOverlappedResult(Disk->Handle, &Overlapped,
                                     &BytesReturned, TRUE)) {
                ErrorCode = GetLastError();
            }
            else {
                ErrorCode = 0;
            }
        }

        if (ErrorCode) {
            break;
        }

        WnbdHandleRequest(Disk, &Request, Buffer);
    }

Exit:
    WNBD_REMOVE_OPTIONS RemoveOptions = {0};
    RemoveOptions.Flags.HardRemove = TRUE;
    WnbdStopDispatcher(Disk, &RemoveOptions);

    if (OverlappedEvent)
        CloseHandle(OverlappedEvent);

    if (Buffer)
        free(Buffer);

    return ErrorCode;
}

DWORD WnbdStartDispatcher(PWNBD_DISK Disk, DWORD ThreadCount)
{
    DWORD ErrorCode = ERROR_SUCCESS;
    if (ThreadCount < WNBD_MIN_DISPATCHER_THREAD_COUNT ||
       ThreadCount > WNBD_MAX_DISPATCHER_THREAD_COUNT) {
        LogError("Invalid number of dispatcher threads: %u",
                 ThreadCount);
        return ERROR_INVALID_PARAMETER;
    }

    LogDebug("Starting dispatcher. Threads: %u", ThreadCount);
    Disk->DispatcherThreads = (HANDLE*)malloc(sizeof(HANDLE) * ThreadCount);
    if (!Disk->DispatcherThreads) {
        LogError("Could not allocate memory.");
        return ERROR_OUTOFMEMORY;
    }

    Disk->Started = TRUE;
    Disk->DispatcherThreadsCount = 0;

    for (DWORD i = 0; i < ThreadCount; i++)
    {
        HANDLE Thread = CreateThread(
            0, 0, (LPTHREAD_START_ROUTINE )WnbdDispatcherLoop, Disk, 0, 0);
        if (!Thread)
        {
            ErrorCode = GetLastError();
            LogError("Could not start dispatcher thread. "
                     "Error: %d. Error message: %s.",
                     ErrorCode, win32_strerror(ErrorCode).c_str());
            WNBD_REMOVE_OPTIONS RemoveOptions = {0};
            RemoveOptions.Flags.HardRemove = TRUE;
            WnbdStopDispatcher(Disk, &RemoveOptions);
            WnbdWaitDispatcher(Disk);
            break;
        }
        Disk->DispatcherThreads[Disk->DispatcherThreadsCount] = Thread;
        Disk->DispatcherThreadsCount++;
    }

    return ErrorCode;
}

DWORD WnbdWaitDispatcher(PWNBD_DISK Disk)
{
    LogDebug("Waiting for the dispatcher to stop.");
    if (!Disk->Started) {
        LogError("The dispatcher hasn't been started.");
        return ERROR_PIPE_NOT_CONNECTED;
    }

    if (!Disk->DispatcherThreads || !Disk->DispatcherThreadsCount ||
            !WnbdIsRunning(Disk)) {
        LogInfo("The dispatcher isn't running.");
        return 0;
    }

    DWORD Ret = WaitForMultipleObjects(
        Disk->DispatcherThreadsCount,
        Disk->DispatcherThreads,
        TRUE, // WaitAll
        INFINITE);

    if (Ret == WAIT_FAILED) {
        DWORD Err = GetLastError();
        LogError("Failed waiting for the dispatcher. Error: %d.", Err);
        return Err;
    }

    LogDebug("The dispatcher stopped.");
    return 0;
}
