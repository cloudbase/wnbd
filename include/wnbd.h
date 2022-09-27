/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef WNBD_SHARED_H
#define WNBD_SHARED_H

#ifdef _MSC_VER
#pragma warning(push)
// Disable "enum class" warnings, libwnbd must be C compatible.
#pragma warning(disable:26812)
#endif

#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>

#include "wnbd_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WNBD_MIN_DISPATCHER_THREAD_COUNT 1
#define WNBD_MAX_DISPATCHER_THREAD_COUNT 255
#define WNBD_LOG_MESSAGE_MAX_SIZE 4096
#define WNBD_DEFAULT_RM_TIMEOUT_MS 30 * 1000
#define WNBD_DEFAULT_RM_RETRY_INTERVAL_MS 2000

#define WNBD_HARDWAREID "root\\wnbd"
#define WNBD_HARDWAREID_LEN sizeof(WNBD_HARDWAREID)

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
    UINT64 PersistResInErrors;
    UINT64 PersistResOutErrors;
    BYTE Reserved[128];
} WNBD_USR_STATS, *PWNBD_USR_STATS;
WNBD_ASSERT_SZ_EQ(WNBD_USR_STATS, 256);

typedef struct
{
    UINT32 HardRemove:1;
    // Fallback to a hard remove if the soft remove fails.
    UINT32 HardRemoveFallback:1;
    UINT32 Reserved:30;
} WNBD_REMOVE_FLAGS, *PWNBD_REMOVE_FLAGS;
WNBD_ASSERT_SZ_EQ(WNBD_REMOVE_FLAGS, 4);

typedef struct
{
    WNBD_REMOVE_FLAGS Flags;
    DWORD SoftRemoveTimeoutMs;
    DWORD SoftRemoveRetryIntervalMs;
    BYTE Reserved[64];
} WNBD_REMOVE_OPTIONS, *PWNBD_REMOVE_OPTIONS;
WNBD_ASSERT_SZ_EQ(WNBD_REMOVE_OPTIONS, 76);

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
typedef VOID (*PersistResInFunc)(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    UINT8 ServiceAction);
typedef VOID (*PersistResOutFunc)(
    PWNBD_DISK Disk,
    UINT64 RequestHandle,
    UINT8 ServiceAction,
    UINT8 Scope,
    UINT8 Type,
    PVOID Buffer,
    UINT32 ParameterListLength);

// The following IO callbacks should be implemented by the consumer when
// not using NBD. As an alternative, the underlying *Ioctl* functions may
// be used directly in order to retrieve and process requests.
typedef struct _WNBD_INTERFACE
{
    ReadFunc Read;
    WriteFunc Write;
    FlushFunc Flush;
    UnmapFunc Unmap;
    PersistResInFunc PersistResIn;
    PersistResOutFunc PersistResOut;
    VOID* Reserved[13];
} WNBD_INTERFACE, *PWNBD_INTERFACE;
WNBD_ASSERT_SZ_EQ(WNBD_INTERFACE, 152);

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
DWORD WnbdGetUserContext(
    PWNBD_DISK Disk,
    PVOID* Context);
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
DWORD WnbdSendResponseEx(
    PWNBD_DISK Disk,
    PWNBD_IO_RESPONSE Response,
    PVOID DataBuffer,
    UINT32 DataBufferSize,
    LPOVERLAPPED Overlapped);

/**
* Retrieve a specific WNBD option.
*
* \param Name The option name
* \param Value A pointer to receive the value
* \param Persistent If set, the persistent value is retrieved instead of the
*                   current runtime value.
* \return a non-zero error code in case of failure. ERROR_FILE_NOT_FOUND
*         is returned if the option is not defined or if a persistent value
*         was requested but it hasn't been set.
*/
DWORD WnbdGetDrvOpt(
    const char* Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent);

/**
* Set a WNBD option.
*
* \param Name The option name
* \param Value The new option value
* \param Persistent If set, the option will survive reboots.
* \return a non-zero error code in case of failure.
*/
DWORD WnbdSetDrvOpt(
    const char* Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent);

/**
* Reset a WNBD option, applying the default value.
*
* \param Name The option name
* \param Persistent If set, the persistent value is removed as well.
* \return a non-zero error code in case of failure.
*/
DWORD WnbdResetDrvOpt(
    const char* Name,
    BOOLEAN Persistent);

/**
* List WNBD options.
*
* \param OptionList An option list buffer to receive the options.
* \param BufferSize A pointer to the input buffer size. If the buffer
                    size is too small, it will be updated with the
                    required buffer size and the return value will be 0.
                    It might seem a bit counterintuitive but we're
                    preserving the DeviceIoControl behavior.
* \param Persistent If set, the currently set persistent options are
                    retrieved.
* \return a non-zero error code in case of failure.
*/
DWORD WnbdListDrvOpt(
    PWNBD_OPTION_LIST OptionList,
    PDWORD BufferSize,
    BOOLEAN Persistent);

// Open the WNBD SCSI adapter device.
DWORD WnbdOpenAdapter(PHANDLE Handle);
DWORD WnbdIoctlPing(HANDLE Adapter, LPOVERLAPPED Overlapped);
DWORD WnbdUninstallDriver(PBOOL RebootRequired);
DWORD WnbdInstallDriver(CONST CHAR* FileName, PBOOL RebootRequired);

// The "Overlapped" parameter used by WnbdIoctl* functions allows
// asynchronous calls. If NULL, a valid overlapped structure is
// provided by libwnbd, also performing the wait on behalf of the
// caller.
//
// NOTE: Overlapped structures should be re-used as much as possible,
// avoiding the need of reinitializing the embedded event all the time,
// especially for functions that are used in the IO path.
DWORD WnbdIoctlCreate(
    HANDLE Adapter,
    PWNBD_PROPERTIES Properties,
    // The resulting connecting info.
    PWNBD_CONNECTION_INFO ConnectionInfo,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlRemove(
    HANDLE Adapter,
    const char* InstanceName,
    PWNBD_REMOVE_COMMAND_OPTIONS RemoveOptions,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlList(
    HANDLE Adapter,
    PWNBD_CONNECTION_LIST ConnectionList,
    // Connection list buffer size.
    PDWORD BufferSize,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlShow(
    HANDLE Adapter,
    const char* InstanceName,
    PWNBD_CONNECTION_INFO ConnectionInfo,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlStats(
    HANDLE Adapter,
    const char* InstanceName,
    PWNBD_DRV_STATS Stats,
    LPOVERLAPPED Overlapped);
// Reload the persistent settings provided through registry keys.
DWORD WnbdIoctlReloadConfig(
    HANDLE Adapter,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlVersion(
    HANDLE Adapter,
    PWNBD_VERSION Version,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlGetDrvOpt(
    HANDLE Adapter,
    const char* Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlSetDrvOpt(
    HANDLE Adapter,
    const char* Name,
    PWNBD_OPTION_VALUE Value,
    BOOLEAN Persistent,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlResetDrvOpt(
    HANDLE Adapter,
    const char* Name,
    BOOLEAN Persistent,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlListDrvOpt(
    HANDLE Adapter,
    PWNBD_OPTION_LIST OptionList,
    PDWORD BufferSize,
    BOOLEAN Persistent,
    LPOVERLAPPED Overlapped);

// The connection id should be handled carefully in order to avoid delayed replies
// from being submitted to other disks after being remapped.
DWORD WnbdIoctlFetchRequest(
    HANDLE Adapter,
    WNBD_CONNECTION_ID ConnectionId,
    PWNBD_IO_REQUEST Request,
    PVOID DataBuffer,
    UINT32 DataBufferSize,
    LPOVERLAPPED Overlapped);
DWORD WnbdIoctlSendResponse(
    HANDLE Adapter,
    WNBD_CONNECTION_ID ConnectionId,
    PWNBD_IO_RESPONSE Response,
    PVOID DataBuffer,
    UINT32 DataBufferSize,
    LPOVERLAPPED Overlapped);

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

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* WNBD_SHARED_H */
