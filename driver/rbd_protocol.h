/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef RBD_PROTOCOL_H
#define RBD_PROTOCOL_H 1

#define NBD_REQUEST_MAGIC 0x25609513
#define NBD_REPLY_MAGIC   0x67446698

#define NBD_REQUEST_MAGIC 0x25609513
#define NBD_REPLY_MAGIC   0x67446698

/* values for flags field, these are server interaction specific. */
#define NBD_FLAG_HAS_FLAGS  (1 << 0) /* nbd-server supports flags */
#define NBD_FLAG_READ_ONLY  (1 << 1) /* device is read-only */
#define NBD_FLAG_SEND_FLUSH (1 << 2) /* can flush writeback cache */
#define NBD_FLAG_SEND_FUA   (1 << 3) /* send FUA (forced unit access) */
/* there is a gap here to match userspace */
#define NBD_FLAG_SEND_TRIM  (1 << 5) /* send trim/discard */
#define NBD_FLAG_CAN_MULTI_CONN (1 << 8) /* Server supports multiple connections per export. */

/* values for cmd flags in the upper 16 bits of request type */
#define NBD_CMD_FLAG_FUA    (1 << 16) /* FUA (forced unit access) op */

#define CHECK_NBD_FLAG(nbd_flags, flag) \
    !!(nbd_flags & NBD_FLAG_HAS_FLAGS && nbd_flags & flag)
#define CHECK_NBD_READONLY(nbd_flags) \
    CHECK_NBD_FLAG(nbd_flags, NBD_FLAG_READ_ONLY)
#define CHECK_NBD_SEND_FUA(nbd_flags) \
    CHECK_NBD_FLAG(nbd_flags, NBD_FLAG_SEND_FUA)
#define CHECK_NBD_SEND_TRIM(nbd_flags) \
    CHECK_NBD_FLAG(nbd_flags, NBD_FLAG_SEND_TRIM)
#define CHECK_NBD_SEND_FLUSH(nbd_flags) \
    CHECK_NBD_FLAG(nbd_flags, NBD_FLAG_SEND_FLUSH)


typedef enum {
    NBD_CMD_READ = 0,
    NBD_CMD_WRITE = 1,
    NBD_CMD_DISC = 2, //DISCONNECT
    NBD_CMD_FLUSH = 3,
    NBD_CMD_TRIM = 4
} NbdRequestType;

__pragma(pack(push, 1))
typedef struct _NBD_REQUEST {
    UINT32 Magic;
    UINT32 Type;
    UINT64 Handle;
    UINT64 From;
    UINT32 Length;
} NBD_REQUEST, *PNBD_REQUEST;
__pragma(pack(pop))

__pragma(pack(push, 1))
typedef struct _NBD_REPLY {
    UINT32 Magic;
    UINT32 Error;
    UINT64 Handle;
} NBD_REPLY, *PNBD_REPLY;
__pragma(pack(pop))

__pragma(pack(push, 1))
typedef struct _REQUEST_HEADER {
    UINT64 Magic;
    UINT32 Option;
    UINT32 Datasize;
} REQUEST_HEADER, *PREQUEST_HEADER;
__pragma(pack(pop))

#pragma warning(disable:4200)
__pragma(pack(push, 1))
typedef struct _REPLY_HEADER {
    UINT64 Magic;
    UINT32 Option;
    UINT32 ReplyType;
    UINT32 Datasize;
    CHAR   Data[];
} REPLY_HEADER, *PREPLY_HEADER;
__pragma(pack(pop))
#pragma warning(default:4200)

#define NBD_OPT_EXPORT_NAME	 1
#define NBD_OPT_GO		     7

#define NBD_REP_ACK		     1
#define NBD_REP_INFO		 3
#define NBD_REP_FLAG_ERROR	 1 << 31
#define NBD_REP_ERR_UNSUP	 1 | NBD_REP_FLAG_ERROR
#define NBD_REP_ERR_POLICY	 2 | NBD_REP_FLAG_ERROR

#define NBD_FLAG_NO_ZEROES	 1 << 1

#define NBD_INFO_EXPORT		 0

#define NBDC_DO_LIST 1

#define RBD_PROTOCOL_TAG      'pDBR'
#define BUF_SIZE              1024
#define INIT_PASSWD           "NBDMAGIC"
#define NbdMalloc(S) ExAllocatePoolWithTag(NonPagedPoolNx, S, RBD_PROTOCOL_TAG)
#define NbdFree(S) ExFreePool(S)

#ifdef __cplusplus
extern "C" {
#endif

VOID
NbdRequest(_In_ INT Fd,
            _In_ UINT64 Offset,
            _In_ ULONG Length,
            _Out_ PNTSTATUS IoStatus,
            _In_ UINT64 Handle,
            _In_ NbdRequestType RequestType);
#pragma alloc_text (PAGE, NbdRequest)

VOID
NbdWriteStat(_In_ INT Fd,
             _In_ UINT64 Offset,
             _In_ ULONG Length,
             _Out_ PNTSTATUS IoStatus,
             _In_ PVOID SystemBuffer,
             _In_ PVOID *PreallocatedBuffer,
             _In_ PULONG PreallocatedLength,
             _In_ UINT64 Handle,
             _In_ UINT32 NbdTransmissionFlags);
#pragma alloc_text (PAGE, NbdWriteStat)

INT
NbdOpenAndConnect(_In_ PCHAR HostName,
                  _In_ DWORD PortNumber);

NTSTATUS
RbdNegotiate(_In_ INT* Pfd,
             _In_ PUINT64 Size,
             _In_ PUINT16 Flags,
             _In_ PCHAR Name,
             _In_ UINT32 ClientFlags,
             _In_ BOOLEAN Go);

NTSTATUS
NbdReadReply(_In_ INT Fd,
             _Out_ PNBD_REPLY Reply);
#pragma alloc_text (PAGE, NbdReadReply)

INT
RbdReadExact(_In_ INT Fd,
             _Inout_ PVOID Data,
             _In_ size_t Length,
             _Inout_ PNTSTATUS error);

char* NbdRequestTypeStr(NbdRequestType RequestType);

#ifdef __cplusplus
}
#endif

#endif
