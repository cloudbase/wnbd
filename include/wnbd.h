#ifndef WNBD_SHARED_H
#define WNBD_SHARED_H

#include <windows.h>
#include <cfgmgr32.h>

#include "wnbd_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WNBD_MIN_DISPATCHER_THREAD_COUNT 1
#define WNBD_MAX_DISPATCHER_THREAD_COUNT 255
#define WNBD_LOG_MESSAGE_MAX_SIZE 4096
#define WNBD_DEFAULT_RM_TIMEOUT_MS 30 * 1000
#define WNBD_DEFAULT_RM_RETRY_INTERVAL_MS 2000

typedef enum
{
    WnbdLogLevelCritical = 0,
    WnbdLogLevelError = 1,
    WnbdLogLevelWarning = 2,
    WnbdLogLevelInfo = 3,
    WnbdLogLevelDebug = 4,
    WnbdLogLevelTrace = 5
} WnbdLogLevel;

// Userspace stats.
typedef struct _WNBD_USR_STATS
{
    UINT64 TotalReceivedRequests;
    UINT64 TotalSubmittedRequests;
    UINT64 TotalReceivedReplies;
    UINT64 UnsubmittedRequests;
    UINT64 PendingSubmittedRequests;
    // IO replies that are currently being transmitted to WNBD.
    UINT64 PendingReplies;
    UINT64 ReadErrors;
    UINT64 WriteErrors;
    UINT64 FlushErrors;
    UINT64 UnmapErrors;
    UINT64 InvalidRequests;
    UINT64 TotalRWRequests;
    UINT64 TotalReadBlocks;
    UINT64 TotalWrittenBlocks;
} WNBD_USR_STATS, *PWNBD_USR_STATS;

typedef struct
{
    UINT32 HardRemove:1;
    // Fallback to a hard remove if the soft remove fails.
    UINT32 HardRemoveFallback:1;
    UINT32 Reserved:30;
} WNBD_REMOVE_FLAGS, *PWNBD_REMOVE_FLAGS;

typedef struct
{
    WNBD_REMOVE_FLAGS Flags;
    DWORD SoftRemoveTimeoutMs;
    DWORD SoftRemoveRetryIntervalMs;
    BYTE Reserved[64];
} WNBD_REMOVE_OPTIONS, *PWNBD_REMOVE_OPTIONS;

typedef struct _WNBD_INTERFACE WNBD_INTERFACE;
// This should be handled as an opaque structure by library consumers.
typedef struct _WNBD_DISK
{
    HANDLE Handle;
    PVOID Context;
    WNBD_PROPERTIES Properties;
    WNBD_CONNECTION_INFO ConnectionInfo;
    const WNBD_INTERFACE *Interface;
    BOOLEAN Stopping;
    BOOLEAN Stopped;
    BOOLEAN Started;
    HANDLE* DispatcherThreads;
    UINT32 DispatcherThreadsCount;
    WNBD_USR_STATS Stats;
} WNBD_DISK, *PWNBD_DISK;

typedef VOID (*ReadFunc)(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    PVOID Buffer,
    UINT64 BlockAddress,
    UINT32 BlockCount,
    BOOLEAN ForceUnitAccess);
typedef VOID (*WriteFunc)(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    PVOID Buffer,
    UINT64 BlockAddress,
    UINT32 BlockCount,
    BOOLEAN ForceUnitAccess);
typedef VOID (*FlushFunc)(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    UINT64 BlockAddress,
    UINT32 BlockCount);
typedef VOID (*UnmapFunc)(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    PWNBD_UNMAP_DESCRIPTOR Descriptors,
    UINT32 Count);
typedef VOID (*LogMessageFunc)(
    WnbdLogLevel LogLevel,
    const char* Message,
    const char* FileName,
    UINT32 Line,
    const char* FunctionName);

// The following IO callbacks should be implemented by the consumer when
// not using NBD. As an alternative, the underlying *Ioctl* functions may
// be used directly in order to retrieve and process requests.
typedef struct _WNBD_INTERFACE
{
    ReadFunc Read;
    WriteFunc Write;
    FlushFunc Flush;
    UnmapFunc Unmap;
    VOID* Reserved[15];
} WNBD_INTERFACE, *PWNBD_INTERFACE;

DWORD WnbdCreate(
    const PWNBD_PROPERTIES Properties,
    const PWNBD_INTERFACE Interface,
    PVOID Context,
    PWNBD_DISK* PDisk);
// Remove the disk. The existing dispatchers will continue running until all
// the driver IO requests are completed unless the "HardRemove" flag is set.
DWORD WnbdRemove(
    PWNBD_DISK Disk,
    PWNBD_REMOVE_OPTIONS RemoveOptions);
DWORD WnbdRemoveEx(
    const char* InstanceName,
    PWNBD_REMOVE_OPTIONS RemoveOptions);
// Cleanup the PWNBD_DISK structure. This should be called after stopping
// the IO dispatchers.
VOID WnbdClose(PWNBD_DISK Disk);

DWORD WnbdList(
    PWNBD_CONNECTION_LIST ConnectionList,
    // Connection list buffer size.
    PDWORD BufferSize);
DWORD WnbdShow(
    const char* InstanceName,
    PWNBD_CONNECTION_INFO ConnectionInfo);
// Userspace counters
DWORD WnbdGetUserspaceStats(
    PWNBD_DISK Disk,
    PWNBD_USR_STATS Stats);
// Driver counters
DWORD WnbdGetDriverStats(
    const char* InstanceName,
    PWNBD_DRV_STATS Stats);
DWORD WnbdGetConnectionInfo(
    PWNBD_DISK Disk,
    PWNBD_CONNECTION_INFO ConnectionInfo);

// libwnbd logger
VOID WnbdSetLogger(LogMessageFunc Logger);
VOID WnbdSetLogLevel(WnbdLogLevel LogLevel);

DWORD WnbdRaiseDrvLogLevel(USHORT LogLevel);

// Get libwnbd version.
DWORD WnbdGetLibVersion(PWNBD_VERSION Version);
DWORD WnbdGetDriverVersion(PWNBD_VERSION Version);

// Setting the SCSI SENSE data provides detailed information about
// the status of a request.
void WnbdSetSenseEx(
    PWNBD_STATUS Status,
    UINT8 SenseKey,
    UINT8 Asc,
    UINT64 Info);
void WnbdSetSense(PWNBD_STATUS Status, UINT8 SenseKey, UINT8 Asc);

DWORD WnbdStartDispatcher(PWNBD_DISK Disk, DWORD ThreadCount);
DWORD WnbdStopDispatcher(PWNBD_DISK Disk, PWNBD_REMOVE_OPTIONS RemoveOptions);
DWORD WnbdWaitDispatcher(PWNBD_DISK Disk);
// Must be called after an IO request completes, notifying the driver about
// the result. Storport will timeout requests that don't complete in a timely
// manner (usually after 30 seconds).
DWORD WnbdSendResponse(
    PWNBD_DISK Disk,
    PWNBD_IO_RESPONSE Response,
    PVOID DataBuffer,
    UINT32 DataBufferSize);

// Open the WNBD SCSI adapter device.
DWORD WnbdOpenAdapter(PHANDLE Handle);
DWORD WnbdOpenAdapterEx(PHANDLE Handle, PDEVINST CMDeviceInstance);
DWORD WnbdOpenAdapterCMDeviceInstance(PDEVINST DeviceInstance);
DWORD WnbdIoctlPing(HANDLE Adapter);

DWORD WnbdIoctlCreate(
    HANDLE Adapter,
    PWNBD_PROPERTIES Properties,
    // The resulting connecting info.
    PWNBD_CONNECTION_INFO ConnectionInfo);
DWORD WnbdIoctlRemove(
    HANDLE Adapter,
    const char* InstanceName,
    PWNBD_REMOVE_COMMAND_OPTIONS RemoveOptions);
DWORD WnbdIoctlList(
    HANDLE Adapter,
    PWNBD_CONNECTION_LIST ConnectionList,
    // Connection list buffer size.
    PDWORD BufferSize);
DWORD WnbdIoctlShow(
    HANDLE Adapter,
    const char* InstanceName,
    PWNBD_CONNECTION_INFO ConnectionInfo);
DWORD WnbdIoctlStats(
    HANDLE Adapter,
    const char* InstanceName,
    PWNBD_DRV_STATS Stats);
// Reload the persistent settings provided through registry keys.
DWORD WnbdIoctlReloadConfig(HANDLE Adapter);
DWORD WnbdIoctlVersion(HANDLE Adapter, PWNBD_VERSION Version);

// The connection id should be handled carefully in order to avoid delayed replies
// from being submitted to other disks after being remapped.
DWORD WnbdIoctlFetchRequest(
    HANDLE Adapter,
    WNBD_CONNECTION_ID ConnectionId,
    PWNBD_IO_REQUEST Request,
    PVOID DataBuffer,
    UINT32 DataBufferSize);
DWORD WnbdIoctlSendResponse(
    HANDLE Adapter,
    WNBD_CONNECTION_ID ConnectionId,
    PWNBD_IO_RESPONSE Response,
    PVOID DataBuffer,
    UINT32 DataBufferSize);

HRESULT WnbdCoInitializeBasic();
// Requires COM. For convenience, WnbdCoInitializeBasic may be used.
HRESULT WnbdGetDiskNumberBySerialNumber(
    LPCWSTR SerialNumber,
    PDWORD DiskNumber);

static inline const CHAR* WnbdLogLevelToStr(WnbdLogLevel LogLevel) {
    switch(LogLevel)
    {
        case WnbdLogLevelCritical:
            return "CRITICAL";
        case WnbdLogLevelError:
            return "ERROR";
        case WnbdLogLevelWarning:
            return "WARNING";
        case WnbdLogLevelInfo:
            return "INFO";
        case WnbdLogLevelDebug:
            return "DEBUG";
        case WnbdLogLevelTrace:
        default:
            return "TRACE";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* WNBD_SHARED_H */
