/*
 * Copyright (c) 2019 SUSE LLC
 * Copyright (c) 2023 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>

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

const UINT64 CLIENT_MAGIC = 0x00420281861253LL;
const UINT64 OPTION_MAGIC = 0x49484156454F5054LL;
const UINT64 REPLY_MAGIC  = 0x3e889045565a9LL;

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
typedef struct _NBD_HANDSHAKE_REQ {
    UINT64 Magic;
    UINT32 Option;
    UINT32 Datasize;
} NBD_HANDSHAKE_REQ, *PNBD_HANDSHAKE_REQ;
__pragma(pack(pop))

#pragma warning(disable:4200)
__pragma(pack(push, 1))
typedef struct _NBD_HANDSHAKE_RPL {
    UINT64 Magic;
    UINT32 Option;
    UINT32 ReplyType;
    UINT32 Datasize;
    CHAR   Data[];
} NBD_HANDSHAKE_RPL, *PNBD_HANDSHAKE_RPL;
__pragma(pack(pop))
#pragma warning(default:4200)

#define NBD_OPT_EXPORT_NAME  1
#define NBD_OPT_GO           7

#define NBD_REP_ACK          1
#define NBD_REP_INFO         3
#define NBD_REP_FLAG_ERROR   1 << 31
#define NBD_REP_ERR_UNSUP    1 | NBD_REP_FLAG_ERROR
#define NBD_REP_ERR_POLICY   2 | NBD_REP_FLAG_ERROR

#define NBD_FLAG_FIXED_NEWSTYLE 1
#define NBD_FLAG_NO_ZEROES      2

#define NBD_INFO_EXPORT      0

#define INIT_PASSWD           "NBDMAGIC"

#ifdef __cplusplus
extern "C" {
#endif

DWORD NbdRequest(
    _In_ SOCKET Fd,
    _In_ UINT64 Offset,
    _In_ ULONG Length,
    _In_ UINT64 Handle,
    _In_ NbdRequestType RequestType);

DWORD NbdSendWrite(
    _In_ SOCKET Fd,
    _In_ UINT64 Offset,
    _In_ ULONG Length,
    _In_ PVOID Data,
    _In_ PVOID *PreallocatedBuffer,
    _In_ PULONG PreallocatedLength,
    _In_ UINT64 Handle,
    _In_ UINT32 NbdTransmissionFlags);

DWORD NbdNegotiate(
    _In_ SOCKET Fd,
    _In_ PUINT64 Size,
    _In_ PUINT16 Flags,
    _In_ std::string ExportName,
    _In_ UINT32 ClientFlags);

DWORD NbdReadReply(
    _In_ SOCKET Fd,
    _Inout_ PNBD_REPLY Reply);

DWORD RecvExact(
    _In_ SOCKET Fd,
    _Inout_ PVOID Data,
    _In_ size_t Length);

const char* NbdRequestTypeStr(NbdRequestType RequestType);

#ifdef __cplusplus
}
#endif
