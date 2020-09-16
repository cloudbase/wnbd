#ifndef WNBD_IOCTL_H
#define WNBD_IOCTL_H

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

static const GUID WNBD_GUID = {
    0x949dd17c,
    0xb06c,
    0x4c14,
    {0x8e, 0x13, 0xf1, 0xa3, 0xa4, 0xa6, 0xdb, 0xcb}
};

#define WNBD_MAX_NAME_LENGTH 256
#define WNBD_MAX_OWNER_LENGTH 16
#define WNBD_MAX_VERSION_STR_LENGTH 128
// For transfers larger than 16MB, Storport sends 0 sized buffers.
#define WNBD_DEFAULT_MAX_TRANSFER_LENGTH 2 * 1024 * 1024

// Only used for NBD connections, in which case the block size is optional.
#define WNBD_DEFAULT_BLOCK_SIZE 512

typedef enum
{
    WnbdReqTypeUnknown = 0,
    WnbdReqTypeRead = 1,
    WnbdReqTypeWrite = 2,
    WnbdReqTypeFlush = 3,
    WnbdReqTypeUnmap = 4,
    WnbdReqTypeDisconnect = 5
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

typedef struct {
    // Skip NBD negotiation and jump directly to the transmission phase.
    // When skipping negotiation, properties such as the block size,
    // block count or NBD server capabilities will have to be provided
    // through WNBD_PROPERTIES.
    UINT32 SkipNegotiation:1;
    UINT32 Reserved:31;
} NBD_CONNECTION_FLAGS, *PNBD_CONNECTION_FLAGS;

typedef struct
{
    CHAR Hostname[WNBD_MAX_NAME_LENGTH];
    UINT32 PortNumber;
    CHAR ExportName[WNBD_MAX_NAME_LENGTH];
    NBD_CONNECTION_FLAGS Flags;
    UINT64 Reserved[4];
} NBD_CONNECTION_PROPERTIES, *PNBD_CONNECTION_PROPERTIES;

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
    UINT32 Reserved: 26;
} WNBD_FLAGS, *PWNBD_FLAGS;

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
    UINT64 Reserved[32];
} WNBD_PROPERTIES, *PWNBD_PROPERTIES;

typedef struct
{
    UINT32 Disconnecting:1;
    UINT32 Reserved:31;
} WNBD_CONNECTION_INFO_FLAGS, PWNBD_CONNECTION_INFO_FLAGS;

typedef struct
{
    WNBD_PROPERTIES Properties;
    PWNBD_CONNECTION_INFO_FLAGS ConnectionFlags;
    USHORT BusNumber;
    USHORT TargetId;
    USHORT Lun;
    WNBD_CONNECTION_ID ConnectionId;
    UINT64 Reserved[16];
} WNBD_CONNECTION_INFO, *PWNBD_CONNECTION_INFO;

typedef struct
{
    UINT32 ElementSize;
    UINT32 Count;
    WNBD_CONNECTION_INFO Connections[1];
} WNBD_CONNECTION_LIST, *PWNBD_CONNECTION_LIST;

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
    INT64 Reserved[16];
} WNBD_DRV_STATS, *PWNBD_DRV_STATS;

typedef struct
{
    UINT64 BlockAddress;
    UINT32 BlockCount;
    UINT32 Reserved;
} WNBD_UNMAP_DESCRIPTOR, *PWNBD_UNMAP_DESCRIPTOR;

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
    } Cmd;
    UINT64 Reserved[4];
} WNBD_IO_REQUEST, *PWNBD_IO_REQUEST;

typedef struct
{
    UINT64 RequestHandle;
    WnbdRequestType RequestType;
    WNBD_STATUS Status;
    UINT64 Reserved[4];
} WNBD_IO_RESPONSE, *PWNBD_IO_RESPONSE;

typedef struct
{
    ULONG IoControlCode;
} WNBD_IOCTL_BASE_COMMAND, *PWNBD_IOCTL_BASE_COMMAND;

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
    UINT64 Reserved[4];
} WNBD_IOCTL_CREATE_COMMAND, *PWNBD_IOCTL_CREATE_COMMAND;

typedef struct
{
    UINT32 HardRemove:1;
    UINT32 Reserved:31;
} WNBD_REMOVE_COMMAND_FLAGS, *PWNBD_REMOVE_COMMAND_FLAGS;

typedef struct
{
    ULONG IoControlCode;
    CHAR InstanceName[WNBD_MAX_NAME_LENGTH];
    WNBD_REMOVE_COMMAND_FLAGS Flags;
    // In the future, we'll add a "hard" remove flag.
    UINT64 Reserved[4];
} WNBD_IOCTL_REMOVE_COMMAND, *PWNBD_IOCTL_REMOVE_COMMAND;

typedef struct
{
    ULONG IoControlCode;
    CHAR InstanceName[WNBD_MAX_NAME_LENGTH];
    UINT64 Reserved[4];
} WNBD_IOCTL_STATS_COMMAND, *PWNBD_IOCTL_STATS_COMMAND;

typedef struct
{
    ULONG IoControlCode;
    WNBD_IO_REQUEST Request;
    WNBD_CONNECTION_ID ConnectionId;
    PVOID DataBuffer;
    UINT32 DataBufferSize;
    UINT64 Reserved[4];
} WNBD_IOCTL_FETCH_REQ_COMMAND, *PWNBD_IOCTL_FETCH_REQ_COMMAND;

typedef struct
{
    ULONG IoControlCode;
    WNBD_IO_RESPONSE Response;
    WNBD_CONNECTION_ID ConnectionId;
    PVOID DataBuffer;
    UINT32 DataBufferSize;
    UINT64 Reserved[4];
} WNBD_IOCTL_SEND_RSP_COMMAND, *PWNBD_IOCTL_SEND_RSP_COMMAND;

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

#endif // WNBD_IOCTL_H
