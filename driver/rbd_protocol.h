/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef RBD_PROTOCOL_H
#define RBD_PROTOCOL_H 1

#include "common.h"

#define NBD_REQUEST_MAGIC 0x25609513
#define NBD_REPLY_MAGIC   0x67446698

#define NBD_REQUEST_MAGIC 0x25609513
#define NBD_REPLY_MAGIC   0x67446698

enum {
    NBD_CMD_READ = 0,
    NBD_CMD_WRITE = 1,
    NBD_CMD_DISC = 2, //DISCONNECT
    NBD_CMD_FLUSH = 3,
    NBD_CMD_TRIM = 4
};

__pragma(pack(push, 1))
typedef struct _NBD_REQUEST {
    UINT32 Magic;
    UINT32 Type;
    UINT8  Handle[8];
    UINT64 From;
    UINT32 Length;
} NBD_REQUEST, *PNBD_REQUEST;
__pragma(pack(pop))

__pragma(pack(push, 1))
typedef struct _NBD_REPLY {
    UINT32 Magic;
    UINT32 Error;
    UINT8  Handle[8];
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

VOID
NbdReadStat(_In_ INT Fd,
            _In_ UINT64 Offset,
            _In_ ULONG Length,
            _Out_ PNTSTATUS IoStatus,
            _Inout_ PVOID SystemBuffer);

VOID
NbdWriteStat(_In_ INT Fd,
             _In_ UINT64 Offset,
             _In_ ULONG Length,
             _Out_ PNTSTATUS IoStatus,
             _In_ PVOID SystemBuffer);

INT
NbdOpenAndConnect(_In_ PCHAR HostName,
                  _In_ PCHAR PortName);

NTSTATUS
RbdNegotiate(_In_ INT* Pfd,
             _In_ PUINT64 Size,
             _In_ PUINT16 Flags,
             _In_ PCHAR Name,
             _In_ UINT32 ClientFlags,
             _In_ BOOLEAN Go);

#endif
