#include <windows.h>

#include <stdio.h>

#define _NTSCSI_USER_MODE_
#include <scsi.h>

#include "wnbd.h"
#include "wnbd_wmi.h"
#include "version.h"

#define STRING_OVERFLOWS(Str, MaxLen) (strlen(Str + 1) > MaxLen)


VOID LogMessage(PWNBD_DEVICE Device, WnbdLogLevel LogLevel,
                const char* FileName, UINT32 Line, const char* FunctionName,
                const char* Format, ...) {
    if (!Device || !Device->Interface->LogMessage ||
            Device->LogLevel < LogLevel)
        return;

    va_list Args;
    va_start(Args, Format);

    size_t BufferLength = (size_t)_vscprintf(Format, Args) + 1;

    // TODO: consider enforcing WNBD_LOG_MESSAGE_MAX_SIZE and using a fixed
    // size buffer for performance reasons.
    char* Buff = (char*) malloc(BufferLength);
    if (!Buff)
        return;

    vsnprintf_s(Buff, BufferLength, BufferLength - 1, Format, Args);
    va_end(Args);

    Device->Interface->LogMessage(
        Device, LogLevel, Buff,
        FileName, Line, FunctionName);

    free(Buff);
}

#define LogCritical(Device, Format, ...) \
    LogMessage(Device, WnbdLogLevelCritical, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogError(Device, Format, ...) \
    LogMessage(Device, WnbdLogLevelError, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogWarning(Device, Format, ...) \
    LogMessage(Device, WnbdLogLevelWarning, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogInfo(Device, Format, ...) \
    LogMessage(Device, WnbdLogLevelInfo, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogDebug(Device, Format, ...) \
    LogMessage(Device, WnbdLogLevelDebug, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)
#define LogTrace(Device, Format, ...) \
    LogMessage(Device, WnbdLogLevelTrace, \
               __FILE__, __LINE__, __FUNCTION__, Format, __VA_ARGS__)

DWORD WnbdCreate(
    const PWNBD_PROPERTIES Properties,
    const PWNBD_INTERFACE Interface,
    PVOID Context,
    WnbdLogLevel LogLevel,
    PWNBD_DEVICE* PDevice)
{
    DWORD ErrorCode = ERROR_SUCCESS;
    PWNBD_DEVICE Device = NULL;

    Device = (PWNBD_DEVICE) calloc(1, sizeof(WNBD_DEVICE));
    if (!Device) {
        return ERROR_OUTOFMEMORY;
    }

    Device->Context = Context;
    Device->Interface = Interface;
    Device->Properties = *Properties;
    Device->LogLevel = LogLevel;
    ErrorCode = WnbdOpenDevice(&Device->Handle);

    LogDebug(Device,
             "Mapping device. Name=%s, Serial=%s, Owner=%s, "
             "BC=%llu, BS=%lu, RO=%u, Flush=%u, "
             "Unmap=%u, UnmapAnchor=%u, MaxUnmapDescCount=%u, "
             "Nbd=%u.",
             Properties->InstanceName,
             Properties->SerialNumber,
             Properties->Owner,
             Properties->BlockCount,
             Properties->BlockSize,
             Properties->Flags.ReadOnly,
             Properties->Flags.FlushSupported,
             Properties->Flags.UnmapSupported,
             Properties->Flags.UnmapAnchorSupported,
             Properties->MaxUnmapDescCount,
             Properties->Flags.UseNbd);
    if (Properties->Flags.UseNbd) {
        LogDebug(Device,
                 "Nbd properties: Hostname=%s, Port=%u, ExportName=%s, "
                 "SkipNegotiation=%u.",
                 Properties->NbdProperties.Hostname,
                 Properties->NbdProperties.PortNumber,
                 Properties->NbdProperties.ExportName,
                 Properties->NbdProperties.Flags.SkipNegotiation);
    }

    if (ErrorCode) {
        LogError(Device,
                 "Could not oped WNBD device. Please make sure "
                 "that the driver is installed. Error: %d.", ErrorCode);
        goto Exit;
    }

    ErrorCode = WnbdIoctlCreate(
        Device->Handle, &Device->Properties, &Device->ConnectionInfo);
    if (ErrorCode) {
        LogError(Device, "Could not map WNBD virtual disk. Error: %d.",
                 ErrorCode);
        goto Exit;
    }

    LogDebug(Device, "Mapped device. Connection id: %llu.",
             Device->ConnectionInfo.ConnectionId);

    *PDevice = Device;

Exit:
    if (ErrorCode) {
        WnbdClose(Device);
    }

    return ErrorCode;
}

DWORD WnbdRemove(PWNBD_DEVICE Device, BOOLEAN HardRemove)
{
    // TODO: check for null pointers.
    DWORD ErrorCode = ERROR_SUCCESS;

    LogDebug(Device, "Unmapping device %s.",
             Device->Properties.InstanceName);
    if (Device->Handle == INVALID_HANDLE_VALUE) {
        LogDebug(Device, "WNBD device already removed.");
        return 0;
    }

    ErrorCode = WnbdIoctlRemove(
        Device->Handle, Device->Properties.InstanceName, HardRemove);
    if (ErrorCode && ErrorCode != ERROR_FILE_NOT_FOUND) {
        LogError(Device, "Could not remove WNBD virtual disk. Error: %d.",
                 ErrorCode);
    }

    return ErrorCode;
}

DWORD WnbdRemoveEx(const char* InstanceName, BOOLEAN HardRemove)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenDevice(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlRemove(Handle, InstanceName, HardRemove);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdList(
    PWNBD_CONNECTION_LIST ConnectionList,
    PDWORD BufferSize)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenDevice(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlList(Handle, ConnectionList, BufferSize);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdGetUserspaceStats(
    PWNBD_DEVICE Device,
    PWNBD_USR_STATS Stats)
{
    if (!Device)
        return ERROR_INVALID_PARAMETER;

    memcpy(Stats, &Device->Stats, sizeof(WNBD_USR_STATS));
    return ERROR_SUCCESS;
}


DWORD WnbdGetDriverStats(
    const char* InstanceName,
    PWNBD_DRV_STATS Stats)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Status = WnbdOpenDevice(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlStats(Handle, InstanceName, Stats);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdGetLibVersion(PWNBD_VERSION Version)
{
    if (!Version) {
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
    DWORD Status = WnbdOpenDevice(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    Status = WnbdIoctlVersion(Handle, Version);

    CloseHandle(Handle);
    return Status;
}

DWORD WnbdGetConnectionInfo(
    PWNBD_DEVICE Device,
    PWNBD_CONNECTION_INFO ConnectionInfo)
{
    if (!Device)
        return ERROR_INVALID_PARAMETER;

    memcpy(ConnectionInfo, &Device->ConnectionInfo, sizeof(WNBD_CONNECTION_INFO));
    return ERROR_SUCCESS;
}

DWORD OpenRegistryKey(HKEY RootKey, LPCSTR KeyName, BOOLEAN Create, HKEY* OutKey)
{
    HKEY Key = NULL;
    DWORD Status = RegOpenKeyExA(RootKey, KeyName, 0, KEY_ALL_ACCESS, &Key);

    if (Status == ERROR_FILE_NOT_FOUND && Create)
    {
        Status = RegCreateKeyExA(RootKey, KeyName, 0, NULL, REG_OPTION_NON_VOLATILE,
                                 KEY_ALL_ACCESS, NULL, &Key, NULL);
    }

    if (!Status) {
        *OutKey = Key;
    }

    return Status;
}

DWORD WnbdRaiseLogLevel(USHORT LogLevel)
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD dwLogLevel = (DWORD)LogLevel;
    DWORD Status = WnbdOpenDevice(&Handle);
    if (Status) {
        return ERROR_OPEN_FAILED;
    }

    HKEY hKey = NULL;
    Status = OpenRegistryKey(HKEY_LOCAL_MACHINE, WNBD_REGISTRY_KEY,
                             TRUE, &hKey);
    if (Status)
        goto Exit;

    Status = RegSetValueExA(hKey, "DebugLogLevel", 0, REG_DWORD,
                            (LPBYTE)&dwLogLevel, sizeof(DWORD));
    if (Status)
        goto Exit;

    Status = WnbdIoctlReloadConfig(Handle);

Exit:
    CloseHandle(Handle);
    return Status;
}

void WnbdClose(PWNBD_DEVICE Device)
{
    if (!Device)
        return;

    LogDebug(Device, "Closing device");
    if (Device->Handle)
        CloseHandle(Device->Handle);

    if (Device->DispatcherThreads)
        free(Device->DispatcherThreads);

    free(Device);
}

VOID WnbdSignalStopped(PWNBD_DEVICE Device)
{
    LogDebug(Device, "Marking device as stopped.");
    if (Device)
        Device->Stopped = TRUE;
}

BOOLEAN WnbdIsStopped(PWNBD_DEVICE Device)
{
    return Device->Stopped;
}

BOOLEAN WnbdIsRunning(PWNBD_DEVICE Device)
{
    return Device->Started && !WnbdIsStopped(Device);
}

DWORD WnbdStopDispatcher(PWNBD_DEVICE Device, BOOLEAN HardRemove)
{
    // By not setting the "Stopped" event, we allow the driver to finish
    // pending IO requests, which we'll continue processing until getting
    // the "Disconnect" request from the driver.
    DWORD Ret = 0;

    LogDebug(Device, "Stopping dispatcher.");
    if (!InterlockedExchange8((CHAR*)&Device->Stopping, 1)) {
        Ret = WnbdRemove(Device, HardRemove);
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

DWORD WnbdSendResponse(
    PWNBD_DEVICE Device,
    PWNBD_IO_RESPONSE Response,
    PVOID DataBuffer,
    UINT32 DataBufferSize)
{
    LogDebug(
        Device,
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

    InterlockedIncrement64((PLONG64)&Device->Stats.TotalReceivedReplies);
    InterlockedDecrement64((PLONG64)&Device->Stats.PendingSubmittedRequests);

    if (Response->Status.ScsiStatus) {
        switch(Response->RequestType) {
            case WnbdReqTypeRead:
                InterlockedIncrement64((PLONG64)&Device->Stats.ReadErrors);
            case WnbdReqTypeWrite:
                InterlockedIncrement64((PLONG64)&Device->Stats.WriteErrors);
            case WnbdReqTypeFlush:
                InterlockedIncrement64((PLONG64)&Device->Stats.FlushErrors);
            case WnbdReqTypeUnmap:
                InterlockedIncrement64((PLONG64)&Device->Stats.UnmapErrors);
        }
    }

    if (!WnbdIsRunning(Device)) {
        LogDebug(Device, "Device disconnected, cannot send response.");
        return ERROR_PIPE_NOT_CONNECTED;
    }

    InterlockedIncrement64((PLONG64)&Device->Stats.PendingReplies);
    DWORD Status = WnbdIoctlSendResponse(
        Device->Handle,
        Device->ConnectionInfo.ConnectionId,
        Response,
        DataBuffer,
        DataBufferSize);
    InterlockedDecrement64((PLONG64)&Device->Stats.PendingReplies);

    return Status;
}

VOID WnbdHandleRequest(PWNBD_DEVICE Device, PWNBD_IO_REQUEST Request,
                       PVOID Buffer)
{
    UINT8 AdditionalSenseCode = 0;
    BOOLEAN IsValid = TRUE;

    InterlockedIncrement64((PLONG64)&Device->Stats.TotalReceivedRequests);
    InterlockedIncrement64((PLONG64)&Device->Stats.UnsubmittedRequests);

    switch (Request->RequestType) {
        case WnbdReqTypeDisconnect:
            LogInfo(Device, "Received disconnect request.");
            WnbdSignalStopped(Device);
            break;
        case WnbdReqTypeRead:
            if (!Device->Interface->Read)
                goto Unsupported;
            LogDebug(Device, "Dispatching READ @ 0x%llx~0x%x # %llx.",
                     Request->Cmd.Read.BlockAddress,
                     Request->Cmd.Read.BlockCount,
                     Request->RequestHandle);
            Device->Interface->Read(
                Device,
                Request->RequestHandle,
                Buffer,
                Request->Cmd.Read.BlockAddress,
                Request->Cmd.Read.BlockCount,
                Request->Cmd.Read.ForceUnitAccess);

            InterlockedIncrement64((PLONG64)&Device->Stats.TotalRWRequests);
            InterlockedAdd64((PLONG64)&Device->Stats.TotalReadBlocks,
                             Request->Cmd.Read.BlockCount);
            break;
        case WnbdReqTypeWrite:
            if (!Device->Interface->Write)
                goto Unsupported;
            LogDebug(Device, "Dispatching WRITE @ 0x%llx~0x%x # %llx." ,
                     Request->Cmd.Write.BlockAddress,
                     Request->Cmd.Write.BlockCount,
                     Request->RequestHandle);
            Device->Interface->Write(
                Device,
                Request->RequestHandle,
                Buffer,
                Request->Cmd.Write.BlockAddress,
                Request->Cmd.Write.BlockCount,
                Request->Cmd.Write.ForceUnitAccess);

            InterlockedIncrement64((PLONG64)&Device->Stats.TotalRWRequests);
            InterlockedAdd64((PLONG64)&Device->Stats.TotalWrittenBlocks,
                             Request->Cmd.Write.BlockCount);
            break;
        case WnbdReqTypeFlush:
            // TODO: should it be a no-op when unsupported?
            if (!Device->Interface->Flush || !Device->Properties.Flags.FlushSupported)
                goto Unsupported;
            LogDebug(Device, "Dispatching FLUSH @ 0x%llx~0x%x # %llx." ,
                     Request->Cmd.Flush.BlockAddress,
                     Request->Cmd.Flush.BlockCount,
                     Request->RequestHandle);
            Device->Interface->Flush(
                Device,
                Request->RequestHandle,
                Request->Cmd.Flush.BlockAddress,
                Request->Cmd.Flush.BlockCount);
        case WnbdReqTypeUnmap:
            if (!Device->Interface->Unmap || !Device->Properties.Flags.UnmapSupported)
                goto Unsupported;
            if (!Device->Properties.Flags.UnmapAnchorSupported &&
                Request->Cmd.Unmap.Anchor)
            {
                AdditionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
                goto Unsupported;
            }

            if (Device->Properties.MaxUnmapDescCount <=
                Request->Cmd.Unmap.Count)
            {
                AdditionalSenseCode = SCSI_ADSENSE_INVALID_FIELD_PARAMETER_LIST;
                goto Unsupported;
            }

            LogDebug(Device, "Dispatching UNMAP # %llx.",
                     Request->RequestHandle);
            Device->Interface->Unmap(
                Device,
                Request->RequestHandle,
                (PWNBD_UNMAP_DESCRIPTOR)Buffer,
                Request->Cmd.Unmap.Count);
        default:
        Unsupported:
            LogDebug(Device, "Received unsupported command. "
                     "Request type: %d, request handle: %llu.",
                     Request->RequestType,
                     Request->RequestHandle);
            IsValid = FALSE;
            if (!AdditionalSenseCode)
                AdditionalSenseCode = SCSI_ADSENSE_ILLEGAL_COMMAND;

            WNBD_IO_RESPONSE Response = { 0 };
            Response.RequestHandle = Request->RequestHandle;
            WnbdSetSense(&Response.Status,
                         SCSI_SENSE_ILLEGAL_REQUEST,
                         AdditionalSenseCode);
            WnbdSendResponse(Device, &Response, NULL, 0);
            break;
    }

    InterlockedDecrement64((PLONG64)&Device->Stats.UnsubmittedRequests);
    if (IsValid) {
        InterlockedIncrement64((PLONG64)&Device->Stats.TotalSubmittedRequests);
        InterlockedIncrement64((PLONG64)&Device->Stats.PendingSubmittedRequests);
    } else {
        InterlockedIncrement64((PLONG64)&Device->Stats.InvalidRequests);
    }
}

DWORD WnbdDispatcherLoop(PWNBD_DEVICE Device)
{
    DWORD ErrorCode = 0;
    WNBD_IO_REQUEST Request;
    DWORD BufferSize = WNBD_DEFAULT_MAX_TRANSFER_LENGTH;
    PVOID Buffer = malloc(BufferSize);

    while (WnbdIsRunning(Device)) {
        ErrorCode = WnbdIoctlFetchRequest(
            Device->Handle,
            Device->ConnectionInfo.ConnectionId,
            &Request,
            Buffer,
            BufferSize);
        if (ErrorCode) {
            LogWarning(Device,
                       "Could not fetch request. Error: %d. "
                       "Buffer: %p, buffer size: %d, connection id: %llu.",
                       ErrorCode, Buffer, BufferSize,
                       Device->ConnectionInfo.ConnectionId);
            break;
        }
        WnbdHandleRequest(Device, &Request, Buffer);
    }

    WnbdStopDispatcher(Device, TRUE);
    free(Buffer);

    return ErrorCode;
}

DWORD WnbdStartDispatcher(PWNBD_DEVICE Device, DWORD ThreadCount)
{
    DWORD ErrorCode = ERROR_SUCCESS;
    if (ThreadCount < WNBD_MIN_DISPATCHER_THREAD_COUNT ||
       ThreadCount > WNBD_MAX_DISPATCHER_THREAD_COUNT) {
        LogError(Device, "Invalid number of dispatcher threads: %u",
                 ThreadCount);
        return ERROR_INVALID_PARAMETER;
    }

    LogDebug(Device, "Starting dispatcher. Threads: %u", ThreadCount);
    Device->DispatcherThreads = (HANDLE*)malloc(sizeof(HANDLE) * ThreadCount);
    if (!Device->DispatcherThreads) {
        LogError(Device, "Could not allocate memory.");
        return ERROR_OUTOFMEMORY;
    }

    Device->Started = TRUE;
    Device->DispatcherThreadsCount = 0;

    for (DWORD i = 0; i < ThreadCount; i++)
    {
        HANDLE Thread = CreateThread(
            0, 0, (LPTHREAD_START_ROUTINE )WnbdDispatcherLoop, Device, 0, 0);
        if (!Thread)
        {
            LogError(Device, "Could not start dispatcher thread.");
            ErrorCode = GetLastError();
            WnbdStopDispatcher(Device, TRUE);
            WnbdWaitDispatcher(Device);
            break;
        }
        Device->DispatcherThreads[Device->DispatcherThreadsCount] = Thread;
        Device->DispatcherThreadsCount++;
    }

    return ErrorCode;
}

DWORD WnbdWaitDispatcher(PWNBD_DEVICE Device)
{
    LogDebug(Device, "Waiting for the dispatcher to stop.");
    if (!Device->Started) {
        LogError(Device, "The dispatcher hasn't been started.");
        return ERROR_PIPE_NOT_CONNECTED;
    }

    if (!Device->DispatcherThreads || !Device->DispatcherThreadsCount ||
            !WnbdIsRunning(Device)) {
        LogInfo(Device, "The dispatcher isn't running.");
        return 0;
    }

    DWORD Ret = WaitForMultipleObjects(
        Device->DispatcherThreadsCount,
        Device->DispatcherThreads,
        TRUE, // WaitAll
        INFINITE);

    if (Ret == WAIT_FAILED) {
        DWORD Err = GetLastError();
        LogError(Device, "Failed waiting for the dispatcher. Error: %d.", Err);
        return Err;
    }

    LogDebug(Device, "The dispatcher stopped.");
    return 0;
}

HRESULT WnbdGetDiskNumberBySerialNumber(
    LPCWSTR SerialNumber,
    PDWORD DiskNumber)
{
    return GetDiskNumberBySerialNumber(SerialNumber, DiskNumber);
}

HRESULT WnbdCoInitializeBasic() {
    return CoInitializeBasic();
}
