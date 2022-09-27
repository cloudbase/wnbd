/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef WNBD_IOCTL_H
#define WNBD_IOCTL_H

#ifdef _MSC_VER
#pragma warning(push)
// Disable "enum class" warnings, libwnbd must be C compatible.
#pragma warning(disable:26812)
#endif

#include <assert.h>

#define WNBD_REGISTRY_KEY "SYSTEM\\CurrentControlSet\\Services\\wnbd"

#define IOCTL_WNBD_PING 1
#define IOCTL_WNBD_CREATE 2
#define IOCTL_WNBD_REMOVE 3
#define IOCTL_WNBD_FETCH_REQ 4
#define IOCTL_WNBD_SEND_RSP 5
#define IOCTL_WNBD_LIST 6
#define IOCTL_WNBD_STATS 7
#define IOCTL_WNBD_RELOAD_CONFIG 8
#define IOCTL_WNBD_VERSION 9
#define IOCTL_WNBD_SHOW 10
#define IOCTL_WNBD_GET_DRV_OPT 11
#define IOCTL_WNBD_SET_DRV_OPT 12
#define IOCTL_WNBD_RESET_DRV_OPT 13
#define IOCTL_WNBD_LIST_DRV_OPT 14

static const GUID WNBD_GUID = {
    0x949dd17c,
    0xb06c,
    0x4c14,
    {0x8e, 0x13, 0xf1, 0xa3, 0xa4, 0xa6, 0xdb, 0xcb}
};

#define WNBD_MAX_NAME_LENGTH 256
#define WNBD_MAX_OWNER_LENGTH 16
#define WNBD_MAX_OPT_NAME_LENGTH 64
#define WNBD_MAX_VERSION_STR_LENGTH 128
// For transfers larger than 16MB, Storport sends 0 sized buffers.
#define WNBD_DEFAULT_MAX_TRANSFER_LENGTH 2 * 1024 * 1024

// Only used for NBD connections, in which case the block size is optional.
#define WNBD_DEFAULT_BLOCK_SIZE 512

#define WNBD_ASSERT_SZ_EQ(Structure, Size) \
    static_assert(sizeof(Structure) == Size, "Invalid structure size");

typedef enum
{
    WnbdReqTypeUnknown = 0,
    WnbdReqTypeRead = 1,
    WnbdReqTypeWrite = 2,
    WnbdReqTypeFlush = 3,
    WnbdReqTypeUnmap = 4,
    WnbdReqTypeDisconnect = 5,
    WnbdReqTypePersistResIn = 6,
    WnbdReqTypePersistResOut = 7,
} WnbdRequestType;

typedef UINT64 WNBD_CONNECTION_ID;
typedef WNBD_CONNECTION_ID *PWNBD_CONNECTION_ID;

typedef struct
{
    UINT8 ScsiStatus;
    UINT8 SenseKey;
    UINT8 ASC;
    UINT8 ASCQ;
    UINT64 Information;
    UINT64 ReservedCSI;
    UINT32 ReservedSKS;
    UINT32 ReservedFRU:8;
    UINT32 InformationValid:1;
} WNBD_STATUS, *PWNBD_STATUS;
WNBD_ASSERT_SZ_EQ(WNBD_STATUS, 32);

typedef struct {
    // Skip NBD negotiation and jump directly to the transmission phase.
    // When skipping negotiation, properties such as the block size,
    // block count or NBD server capabilities will have to be provided
    // through WNBD_PROPERTIES.
    UINT32 SkipNegotiation:1;
    UINT32 Reserved:31;
} NBD_CONNECTION_FLAGS, *PNBD_CONNECTION_FLAGS;
WNBD_ASSERT_SZ_EQ(NBD_CONNECTION_FLAGS, 4);

typedef struct
{
    CHAR Hostname[WNBD_MAX_NAME_LENGTH];
    UINT32 PortNumber;
    CHAR ExportName[WNBD_MAX_NAME_LENGTH];
    NBD_CONNECTION_FLAGS Flags;
    BYTE Reserved[32];
} NBD_CONNECTION_PROPERTIES, *PNBD_CONNECTION_PROPERTIES;
WNBD_ASSERT_SZ_EQ(NBD_CONNECTION_PROPERTIES, 552);

typedef struct
{
    UINT32 ReadOnly:1;
    UINT32 FlushSupported:1;
    // Force Unit Accesss
    UINT32 FUASupported:1;
    UINT32 UnmapSupported:1;
    UINT32 UnmapAnchorSupported:1;
    // Connect to an NBD server. If disabled, IO requests and replies will
    // be submitted through the IOCTL_WNBD_FETCH_REQ/IOCTL_WNBD_SEND_RSP
    // DeviceIoControl commands.
    UINT32 UseNbd:1;
    UINT32 PersistResSupported:1;
    UINT32 Reserved: 25;
} WNBD_FLAGS, *PWNBD_FLAGS;
WNBD_ASSERT_SZ_EQ(WNBD_FLAGS, 4);

typedef struct
{
    // Unique disk identifier
    CHAR InstanceName[WNBD_MAX_NAME_LENGTH];
    // If no serial number is provided, the instance name will be used.
    // This will be exposed through the VPD page.
    CHAR SerialNumber[WNBD_MAX_NAME_LENGTH];
    // Optional string used to identify the owner of this disk
    // (e.g. can be the project name).
    CHAR Owner[WNBD_MAX_OWNER_LENGTH];
    WNBD_FLAGS Flags;
    UINT64 BlockCount;
    UINT32 BlockSize;
    // Optional, defaults to 1.
    UINT32 MaxUnmapDescCount;
    // The userspace process associated with this device. If not
    // specified, the caller PID will be used.
    INT Pid;
    // NBD server details must be provided when the "UseNbd" flag
    // is set.
    NBD_CONNECTION_PROPERTIES NbdProperties;
    BYTE Reserved[256];
} WNBD_PROPERTIES, *PWNBD_PROPERTIES;
WNBD_ASSERT_SZ_EQ(WNBD_PROPERTIES, 1368);

typedef struct
{
    UINT32 Disconnecting:1;
    UINT32 Reserved:31;
} WNBD_CONNECTION_INFO_FLAGS, PWNBD_CONNECTION_INFO_FLAGS;
WNBD_ASSERT_SZ_EQ(WNBD_CONNECTION_INFO_FLAGS, 4);

typedef struct
{
    WNBD_PROPERTIES Properties;
    PWNBD_CONNECTION_INFO_FLAGS ConnectionFlags;
    USHORT BusNumber;
    USHORT TargetId;
    USHORT Lun;
    WNBD_CONNECTION_ID ConnectionId;
    INT DiskNumber;
    WCHAR PNPDeviceID[WNBD_MAX_NAME_LENGTH];
    BYTE Reserved[124];
} WNBD_CONNECTION_INFO, *PWNBD_CONNECTION_INFO;
WNBD_ASSERT_SZ_EQ(WNBD_CONNECTION_INFO, 2032);

typedef struct
{
    UINT32 ElementSize;
    UINT32 Count;
    WNBD_CONNECTION_INFO Connections[1];
} WNBD_CONNECTION_LIST, *PWNBD_CONNECTION_LIST;
WNBD_ASSERT_SZ_EQ(WNBD_CONNECTION_LIST, 2040);

typedef struct
{
    INT64 TotalReceivedIORequests;
    INT64 TotalSubmittedIORequests;
    INT64 TotalReceivedIOReplies;
    INT64 UnsubmittedIORequests;
    INT64 PendingSubmittedIORequests;
    INT64 AbortedSubmittedIORequests;
    INT64 AbortedUnsubmittedIORequests;
    INT64 CompletedAbortedIORequests;
    // Pending requests, without including aborted ones.
    // Soft device removals will wait for outstanding IO
    // requests.
    INT64 OutstandingIOCount;
    INT64 Reserved[15];
} WNBD_DRV_STATS, *PWNBD_DRV_STATS;
WNBD_ASSERT_SZ_EQ(WNBD_DRV_STATS, 192);

typedef struct
{
    UINT64 BlockAddress;
    UINT32 BlockCount;
    UINT32 Reserved;
} WNBD_UNMAP_DESCRIPTOR, *PWNBD_UNMAP_DESCRIPTOR;
WNBD_ASSERT_SZ_EQ(WNBD_UNMAP_DESCRIPTOR, 16);

typedef struct
{
    UINT64 RequestHandle;
    WnbdRequestType RequestType;
    union
    {
        struct
        {
            UINT64 BlockAddress;
            UINT32 BlockCount;
            UINT32 ForceUnitAccess:1;
            UINT32 Reserved:31;
        } Read;
        struct
        {
            UINT64 BlockAddress;
            UINT32 BlockCount;
            UINT32 ForceUnitAccess:1;
            UINT32 Reserved:31;
        } Write;
        struct
        {
            UINT64 BlockAddress;
            UINT32 BlockCount;
            UINT32 Reserved;
        } Flush;
        struct
        {
            UINT32 Count;
            UINT32 Anchor:1;
            UINT32 Reserved:31;
        } Unmap;
        struct
        {
            UINT8 ServiceAction;
            UINT16 AllocationLength;
        } PersistResIn;
        struct
        {
            UINT8 ServiceAction;
            UINT8 Scope:4;
            UINT8 Type:4;
            UINT16 ParameterListLength;
        } PersistResOut;
    } Cmd;
    BYTE Reserved[32];
} WNBD_IO_REQUEST, *PWNBD_IO_REQUEST;
WNBD_ASSERT_SZ_EQ(WNBD_IO_REQUEST, 64);

typedef struct
{
    UINT64 RequestHandle;
    WnbdRequestType RequestType;
    WNBD_STATUS Status;
    BYTE Reserved[32];
} WNBD_IO_RESPONSE, *PWNBD_IO_RESPONSE;
WNBD_ASSERT_SZ_EQ(WNBD_IO_RESPONSE, 80);

typedef enum
{
    WnbdOptUnknon = 0,
    WnbdOptBool = 1,
    WnbdOptInt64 = 2,
    WnbdOptWstr = 3,
} WnbdOptValType;

// Using fixed size strings makes it much easier to pass
// data between userspace and the driver.
typedef struct
{
    WnbdOptValType Type;
    union {
        BOOLEAN AsBool;
        INT64 AsInt64;
        WCHAR AsWstr[WNBD_MAX_NAME_LENGTH];
    } Data;
    BYTE Reserved[64];
} WNBD_OPTION_VALUE, *PWNBD_OPTION_VALUE;
WNBD_ASSERT_SZ_EQ(WNBD_OPTION_VALUE, 584);

typedef struct
{
    WCHAR Name[WNBD_MAX_OPT_NAME_LENGTH];
    WnbdOptValType Type;
    WNBD_OPTION_VALUE Default;
    WNBD_OPTION_VALUE Value;
    BYTE Reserved[64];
} WNBD_OPTION, *PWNBD_OPTION;
WNBD_ASSERT_SZ_EQ(WNBD_OPTION, 1368);

typedef struct
{
    UINT32 ElementSize;
    UINT32 Count;
    WNBD_OPTION Options[1];
} WNBD_OPTION_LIST, *PWNBD_OPTION_LIST;
WNBD_ASSERT_SZ_EQ(WNBD_OPTION_LIST, 1376);

typedef struct
{
    ULONG IoControlCode;
} WNBD_IOCTL_BASE_COMMAND, *PWNBD_IOCTL_BASE_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_BASE_COMMAND, 4);

typedef WNBD_IOCTL_BASE_COMMAND WNBD_IOCTL_PING_COMMAND;
typedef PWNBD_IOCTL_BASE_COMMAND PWNBD_IOCTL_PING_COMMAND;

typedef WNBD_IOCTL_BASE_COMMAND WNBD_IOCTL_LIST_COMMAND;
typedef PWNBD_IOCTL_BASE_COMMAND PWNBD_IOCTL_LIST_COMMAND;

typedef WNBD_IOCTL_BASE_COMMAND WNBD_IOCTL_RELOAD_CONFIG_COMMAND;
typedef PWNBD_IOCTL_BASE_COMMAND PWNBD_IOCTL_RELOAD_CONFIG_COMMAND;

typedef WNBD_IOCTL_BASE_COMMAND WNBD_IOCTL_VERSION_COMMAND;
typedef PWNBD_IOCTL_BASE_COMMAND PWNBD_IOCTL_VERSION_COMMAND;

typedef struct
{
    ULONG IoControlCode;
    WNBD_PROPERTIES Properties;
    BYTE Reserved[32];
} WNBD_IOCTL_CREATE_COMMAND, *PWNBD_IOCTL_CREATE_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_CREATE_COMMAND, 1408);

typedef struct
{
    UINT32 Reserved:32;
} WNBD_REMOVE_COMMAND_FLAGS, *PWNBD_REMOVE_COMMAND_FLAGS;
WNBD_ASSERT_SZ_EQ(WNBD_REMOVE_COMMAND_FLAGS, 4);

// Similar to the userspace WNBD_REMOVE_OPTIONS, currently unused.
// The "Remove" API kept changing, so having a placeholder here
// seems the right thing to do.
typedef struct
{
    WNBD_REMOVE_COMMAND_FLAGS Flags;
    BYTE Reserved[80];
} WNBD_REMOVE_COMMAND_OPTIONS, *PWNBD_REMOVE_COMMAND_OPTIONS;
WNBD_ASSERT_SZ_EQ(WNBD_REMOVE_COMMAND_OPTIONS, 84);

typedef struct
{
    ULONG IoControlCode;
    CHAR InstanceName[WNBD_MAX_NAME_LENGTH];
    WNBD_REMOVE_COMMAND_OPTIONS Options;
    BYTE Reserved[32];
} WNBD_IOCTL_REMOVE_COMMAND, *PWNBD_IOCTL_REMOVE_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_REMOVE_COMMAND, 376);

typedef struct
{
    ULONG IoControlCode;
    CHAR InstanceName[WNBD_MAX_NAME_LENGTH];
    BYTE Reserved[32];
} WNBD_IOCTL_STATS_COMMAND, *PWNBD_IOCTL_STATS_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_STATS_COMMAND, 292);

typedef struct
{
    ULONG IoControlCode;
    CHAR InstanceName[WNBD_MAX_NAME_LENGTH];
    BYTE Reserved[32];
} WNBD_IOCTL_SHOW_COMMAND, *PWNBD_IOCTL_SHOW_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_SHOW_COMMAND, 292);

typedef struct
{
    ULONG IoControlCode;
    WNBD_IO_REQUEST Request;
    WNBD_CONNECTION_ID ConnectionId;
    PVOID DataBuffer;
    UINT32 DataBufferSize;
    BYTE Reserved[32];
} WNBD_IOCTL_FETCH_REQ_COMMAND, *PWNBD_IOCTL_FETCH_REQ_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_FETCH_REQ_COMMAND, 128);

typedef struct
{
    ULONG IoControlCode;
    WNBD_IO_RESPONSE Response;
    WNBD_CONNECTION_ID ConnectionId;
    PVOID DataBuffer;
    UINT32 DataBufferSize;
    BYTE Reserved[32];
} WNBD_IOCTL_SEND_RSP_COMMAND, *PWNBD_IOCTL_SEND_RSP_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_SEND_RSP_COMMAND, 144);

typedef struct
{
    ULONG IoControlCode;
    WCHAR Name[WNBD_MAX_NAME_LENGTH];
    BOOLEAN Persistent;
    BYTE Reserved[32];
} WNBD_IOCTL_GET_DRV_OPT_COMMAND, *PWNBD_IOCTL_GET_DRV_OPT_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_GET_DRV_OPT_COMMAND, 552);

typedef struct
{
    ULONG IoControlCode;
    WCHAR Name[WNBD_MAX_NAME_LENGTH];
    WNBD_OPTION_VALUE Value;
    BOOLEAN Persistent;
    BYTE Reserved[32];
} WNBD_IOCTL_SET_DRV_OPT_COMMAND, *PWNBD_IOCTL_SET_DRV_OPT_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_SET_DRV_OPT_COMMAND, 1144);

typedef struct
{
    ULONG IoControlCode;
    WCHAR Name[WNBD_MAX_NAME_LENGTH];
    BOOLEAN Persistent;
    BYTE Reserved[32];
} WNBD_IOCTL_RESET_DRV_OPT_COMMAND, *PWNBD_IOCTL_RESET_DRV_OPT_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_RESET_DRV_OPT_COMMAND, 552);

typedef struct
{
    ULONG IoControlCode;
    BOOLEAN Persistent;
    BYTE Reserved[32];
} WNBD_IOCTL_LIST_DRV_OPT_COMMAND, *PWNBD_IOCTL_LIST_DRV_OPT_COMMAND;
WNBD_ASSERT_SZ_EQ(WNBD_IOCTL_LIST_DRV_OPT_COMMAND, 40);

static inline const CHAR* WnbdRequestTypeToStr(WnbdRequestType RequestType) {
    switch(RequestType)
    {
        case WnbdReqTypeRead:
            return "READ";
        case WnbdReqTypeWrite:
            return "WRITE";
        case WnbdReqTypeFlush:
            return "FLUSH";
        case WnbdReqTypeUnmap:
            return "UNMAP";
        case WnbdReqTypeDisconnect:
            return "DISCONNECT";
        case WnbdReqTypePersistResIn:
            return "PERSISTENT_RESERVE_IN";
        case WnbdReqTypePersistResOut:
            return "PERSISTENT_RESERVE_OUT";
        default:
            return "UNKNOWN";
    }
}

typedef struct {
    UINT32 Major;
    UINT32 Minor;
    UINT32 Patch;
    CHAR Description[WNBD_MAX_VERSION_STR_LENGTH];
    BYTE Reserved[256];
} WNBD_VERSION, *PWNBD_VERSION;
WNBD_ASSERT_SZ_EQ(WNBD_VERSION, 396);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // WNBD_IOCTL_H
