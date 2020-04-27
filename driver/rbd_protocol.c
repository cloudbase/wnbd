/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <berkeley.h>
#include <ws2def.h>
#include "common.h"
#include "debug.h"
#include "rbd_protocol.h"


#define RBD_PROTOCOL_TAG      'pDBR'
#define BUF_SIZE              1024
#define INIT_PASSWD           "NBDMAGIC"
#define Malloc(S) ExAllocatePoolWithTag(NonPagedPoolNx, S, RBD_PROTOCOL_TAG)
#define Free(S) ExFreePool(S)

const UINT64 CLIENT_MAGIC = 0x00420281861253LL;
const UINT64 OPTION_MAGIC = 0x49484156454F5054LL;
const UINT64 REPLY_MAGIC  = 0x3e889045565a9LL;

INT
RbdReadExact(_In_ INT Fd,
             _Inout_ PVOID Data,
             _In_ size_t Length)
{
    WNBD_LOG_LOUD(": Enter");
    INT Result = 0;
    PUCHAR Temp = Data;
    while (0 < Length) {
        WNBD_LOG_INFO("Size to read = %lu", Length);
        Result = Recv(Fd, Temp, Length, 0);
        if (Result > 0) {
            Length -= Result;
            Temp = Temp + Result;
        } else {
            WNBD_LOG_ERROR("Failed with : %d", Result);
            return -1;
        }
    }
    ASSERT(Length == 0);
    WNBD_LOG_LOUD(": Exit");
    return 0;
}

INT
RbdWriteExact(_In_ INT Fd,
              _In_ PVOID Data,
              _In_ size_t Length)
{
    WNBD_LOG_LOUD(": Enter");
    INT Result = 0;
    PUCHAR Temp = Data;
    while (Length > 0) {
        WNBD_LOG_INFO("Size to send = %lu", Length);
        Result = Send(Fd, Temp, Length, 0);
        if (Result <= 0) {
            WNBD_LOG_ERROR("Failed with : %d", Result);
            return -1;
        }
        Length -= Result;
        Temp += Result;
    }
    ASSERT(Length == 0);
    WNBD_LOG_LOUD(": Exit");
    return 0;
}

VOID
RbdSendRequest(_In_ INT Fd,
               _In_ UINT32 Option,
               _In_ size_t Datasize,
               _Maybenull_ PVOID Data)
{
    WNBD_LOG_LOUD(": Enter");
    REQUEST_HEADER Request;
    Request.Magic = RtlUlonglongByteSwap(OPTION_MAGIC);
    Request.Option = RtlUlongByteSwap(Option);
    Request.Datasize = RtlUlongByteSwap((ULONG)Datasize);

    RbdWriteExact(Fd, &Request, sizeof(Request));
    if (NULL != Data) {
        RbdWriteExact(Fd, Data, Datasize);
    }
    WNBD_LOG_LOUD(": Exit");
}

VOID
RbdSendInfoRequest(_In_ INT Fd,
                   _In_ UINT32 Options,
                   _In_ INT NumberOfRequests,
                   _Maybenull_ PUINT16 Request,
                   _In_ PCHAR Name)
{
    WNBD_LOG_LOUD(": Enter");
    UINT16 rlen = RtlUshortByteSwap((USHORT)NumberOfRequests);
    UINT32 nlen = RtlUlongByteSwap((ULONG)strlen(Name));
    size_t size = sizeof(UINT32) + strlen(Name) + sizeof(UINT16) + NumberOfRequests * sizeof(UINT16);

    RbdSendRequest(Fd, Options, size, NULL);
    RbdWriteExact(Fd, &nlen, sizeof(nlen));
    RbdWriteExact(Fd, Name, strlen(Name));
    RbdWriteExact(Fd, &rlen, sizeof(rlen));
    if (NumberOfRequests > 0) {
        RbdWriteExact(Fd, Request, NumberOfRequests * sizeof(UINT16));
    }
    WNBD_LOG_LOUD(": Exit");
}

PREPLY_HEADER
RbdReadReply(_In_ INT Fd)
{
    WNBD_LOG_LOUD(": Enter");
    PREPLY_HEADER Retval = Malloc(sizeof(REPLY_HEADER));

    if (!Retval) {
        WNBD_LOG_ERROR("Insufficient resources to allocate memory");
        return NULL;
    }

    RtlZeroMemory(Retval, sizeof(REPLY_HEADER));
    RbdReadExact(Fd, Retval, sizeof(*Retval));

    Retval->Magic = RtlUlonglongByteSwap(Retval->Magic);
    Retval->Option = RtlUlongByteSwap(Retval->Option);
    Retval->ReplyType = RtlUlongByteSwap(Retval->ReplyType);
    Retval->Datasize = RtlUlongByteSwap(Retval->Datasize);

    if (REPLY_MAGIC != Retval->Magic) {
        WNBD_LOG_ERROR("Received invalid negotiation magic %llu (expected %llu)", Retval->Magic, REPLY_MAGIC);
        Free(Retval);
        return NULL;
    }
    if (Retval->Datasize > 0) {
        INT NewSize = sizeof(REPLY_HEADER) + Retval->Datasize;
        PREPLY_HEADER RetvalTemp = Malloc(NewSize);
        if (!RetvalTemp) {
            WNBD_LOG_ERROR("Insufficient resources to allocate memory");
            return NULL;
        }
        RtlCopyMemory(RetvalTemp, Retval, sizeof(REPLY_HEADER));
        if (Retval) {
            Free(Retval);
        }
        Retval = NULL;
        RbdReadExact(Fd, &(RetvalTemp->Data), RetvalTemp->Datasize);
        Retval = RetvalTemp;
    }
    WNBD_LOG_LOUD(": Exit");
    return Retval;
}

VOID
RbdParseSizes(_In_ PCHAR Data,
              _Inout_ PUINT64 Size,
              _Inout_ PUINT16 Flags)
{
    WNBD_LOG_LOUD(": Enter");

    RtlCopyMemory(Size, Data, sizeof(*Size));
    *Size = RtlUlonglongByteSwap(*Size);
    Data += sizeof(*Size);
    RtlCopyMemory(Flags, Data, sizeof(*Flags));
    *Flags = RtlUshortByteSwap(*Flags);

    WNBD_LOG_LOUD(": Exit");
}

VOID
RbdSendOptExportName(_In_ INT Fd,
                     _In_ PUINT64 Size,
                     _In_ PUINT16 Flags,
                     _In_ BOOLEAN Go,
                     _In_ PCHAR Name,
                     _In_ UINT16 GFlags)
{
    WNBD_LOG_LOUD(": Enter");

    RbdSendRequest(Fd, NBD_OPT_EXPORT_NAME, strlen(Name), Name);
    CHAR Buf[sizeof(*Flags) + sizeof(*Size)];
    if (RbdReadExact(Fd, Buf, sizeof(Buf)) < 0 && Go) {
        WNBD_LOG_ERROR("Server does not support NBD_OPT_GO and"
            "dropped connection after sending NBD_OPT_EXPORT_NAME.");
        return;
    }
    RbdParseSizes(Buf, Size, Flags);
    if (!(GFlags & NBD_FLAG_NO_ZEROES)) {
        CHAR Temp[125];
        RbdReadExact(Fd, Temp, 124);
    }

    WNBD_LOG_LOUD(": Exit");
}

NTSTATUS
RbdNegotiate(_In_ INT* Pfd,
             _In_ PUINT64 Size,
             _In_ PUINT16 Flags,
             _In_ PCHAR Name,
             _In_ UINT32 ClientFlags,
             _In_ BOOLEAN Go)
{
    WNBD_LOG_LOUD(": Enter");
    UINT64 Magic = 0;
    UINT16 Temp = 0;
    UINT16 GFlags;
    CHAR Buf[256];
    INT Fd = *Pfd;

    RtlZeroMemory(Buf, 8);
    RbdReadExact(Fd, Buf, 8);
    if (strcmp(Buf, INIT_PASSWD)) {
        WNBD_LOG_ERROR("INIT_PASSWD");
    }

    RbdReadExact(Fd, &Magic, sizeof(Magic));
    Magic = RtlUlonglongByteSwap(Magic);
    if (OPTION_MAGIC != Magic
        && CLIENT_MAGIC == Magic) {
        WNBD_LOG_ERROR("Old-style server.");
    }

    RbdReadExact(Fd, &Temp, sizeof(UINT16));
    GFlags = RtlUshortByteSwap(Temp);

    if (GFlags & NBD_FLAG_NO_ZEROES) {
        ClientFlags |= NBD_FLAG_NO_ZEROES;
    }

    ClientFlags = RtlUlongByteSwap(ClientFlags);
    if (Send(Fd, (PCHAR)&ClientFlags, sizeof(ClientFlags), 0) < 0) {
        WNBD_LOG_ERROR("Failed while sending data on socket");
        return STATUS_FAIL_CHECK;
    }

    PREPLY_HEADER Reply = NULL;
    if (!Go) {
        RbdSendOptExportName(Fd, Size, Flags, Go, Name, GFlags);
        return STATUS_SUCCESS;
    }
    RbdSendInfoRequest(Fd, NBD_OPT_GO, 0, NULL, Name);

    do {
        if (NULL != Reply) {
            Free(Reply);
        }
        Reply = RbdReadReply(Fd);
        if (!Reply) {
            return STATUS_ABANDONED;
        }
        if (Reply && (Reply->ReplyType & NBD_REP_FLAG_ERROR)) {
            switch (Reply->ReplyType) {
            case NBD_REP_ERR_UNSUP:
                WNBD_LOG_ERROR("NBD_REP_ERR_UNSUP");
                RbdSendOptExportName(Fd, Size, Flags, Go, Name, GFlags);
                Free(Reply);
                return STATUS_SUCCESS;

            case NBD_REP_ERR_POLICY:
                if (Reply->Datasize > 0) {
                    WNBD_LOG_ERROR("Connection not allowed by server policy. Server said: %s", Reply->Data);
                } else {
                    WNBD_LOG_ERROR("Connection not allowed by server policy.");
                }
                Free(Reply);
                return STATUS_FAIL_CHECK;

            default:
                if (Reply->Datasize > 0) {
                    WNBD_LOG_INFO("Unknown error returned by server. Server said: %s", Reply->Data);
                }
                else {
                    WNBD_LOG_ERROR("Unknown error returned by server.");
                }
                Free(Reply);
                return STATUS_FAIL_CHECK;
            }
        }

        UINT16 Type;
        switch (Reply->ReplyType) {
        case NBD_REP_INFO:
            memcpy(&Type, Reply->Data, 2);
            Type = RtlUshortByteSwap(Type);
            switch (Type) {
            case NBD_INFO_EXPORT:
                RbdParseSizes(Reply->Data + 2, Size, Flags);
                break;
            default:
                WNBD_LOG_INFO("Ignoring other reply information");
                break;
            }
            break;
        case NBD_REP_ACK:
            WNBD_LOG_INFO("NBD_REP_ACK NOOP");
            break;
        default:
            WNBD_LOG_INFO("Unknown reply to NBD_OPT_GO received, ignoring");
        }
    } while (Reply->ReplyType != NBD_REP_ACK);

    if (NULL != Reply) {
        Free(Reply);
    }

    WNBD_LOG_LOUD(": Exit");
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
INT
NbdOpenAndConnect(PCHAR HostName,
                  PCHAR PortName)
{
    WNBD_LOG_LOUD(": Enter");
    INT Fd = -1;
    struct addrinfo Hints;
    struct addrinfo* Ai = NULL;
    struct addrinfo* Rp = NULL;
    INT Error;
    RtlZeroMemory(&Hints, sizeof(Hints));
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
    Hints.ai_protocol = IPPROTO_TCP;

    Error = GetAddrInfo(HostName, PortName, &Hints, &Ai);

    if (0 != Error) {
        if (Ai) {
            FreeAddrInfo(Ai);
        }
        WNBD_LOG_ERROR("GetAddrInfo failed with error: %d", Error);
        return -1;
    }

    for (Rp = Ai; Rp != NULL; Rp = Rp->ai_next) {
        Fd = Socket(Rp->ai_family, Rp->ai_socktype, Rp->ai_protocol);

        if (-1 == Fd) {
            continue;	/* error */
        }

        if (Connect(Fd, Rp->ai_addr, (int)Rp->ai_addrlen) != -1) {
            break;		/* success */
        }

        WNBD_LOG_INFO("Closing socket FD: %d", Fd);
        Close(Fd);
        Fd = -1;
    }

    if (NULL == Rp) {
        WNBD_LOG_ERROR("Socket failed");
        Fd = -1;
        goto err;
    }

    WNBD_LOG_INFO("Opened socket FD: %d", Fd);
err:
    if (Ai) {
        FreeAddrInfo(Ai);
    }
    WNBD_LOG_LOUD(": Exit");
    return Fd;
}

_Use_decl_annotations_
VOID
NbdReadStat(INT Fd,
            UINT64 Offset,
            ULONG Length,
            PNTSTATUS IoStatus,
            PVOID SystemBuffer)
{
    WNBD_LOG_LOUD(": Enter");
    NTSTATUS Status = STATUS_SUCCESS;
    PCHAR Buf = NULL;
    if (NULL == SystemBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    ASSERT(NULL != SystemBuffer);

    PAGED_CODE();

    UINT64 i = 0;
    Buf = Malloc(Length);
    if (NULL == Buf || -1 == Fd) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    NBD_REQUEST Request;
    NBD_REPLY Reply = { 0 };

    Request.Magic = RtlUlongByteSwap(NBD_REQUEST_MAGIC);
    Request.Type = RtlUlongByteSwap(NBD_CMD_READ);
    Request.Length = RtlUlongByteSwap(Length);
    RtlCopyMemory(&(Request.Handle), &i, sizeof(i));
    Request.From = RtlUlonglongByteSwap(Offset);

    if (-1 == RbdWriteExact(Fd, &Request, sizeof(NBD_REQUEST))) {
        WNBD_LOG_INFO("Could not send request for NBD_CMD_READ");
        Status = STATUS_INVALID_SESSION;
        goto Exit;
    }
    if (-1 == RbdReadExact(Fd, &Reply, sizeof(NBD_REPLY))) {
        WNBD_LOG_INFO("Could not read request for NBD_CMD_READ");
        Status = STATUS_INVALID_SESSION;
        goto Exit;
    }
    if(NBD_REPLY_MAGIC != RtlUlongByteSwap(Reply.Magic)) {
        WNBD_LOG_INFO("Invalid NBD_REPLY_MAGIC for NBD_CMD_READ");
        Status = STATUS_ABANDONED;
        goto Exit;
    }
    if (0 != Reply.Error) {
        WNBD_LOG_INFO("Received reply error from NBD_CMD_READ: %llu", Reply.Error);
        Status = STATUS_ABANDONED;
        goto Exit;
    }
    if (-1 == RbdReadExact(Fd, Buf, Length)) {
        WNBD_LOG_INFO("Could not read request for NBD_CMD_READ");
        Status = STATUS_INVALID_SESSION;
        goto Exit;
    }
    RtlCopyMemory(SystemBuffer, Buf, Length);

Exit:
    if (NULL != Buf) {
        Free(Buf);
    }
    *IoStatus = Status;
    WNBD_LOG_LOUD(": Exit");
}

_Use_decl_annotations_
VOID
NbdWriteStat(INT Fd,
             UINT64 Offset,
             ULONG Length,
             PNTSTATUS IoStatus,
             PVOID SystemBuffer)
{
    WNBD_LOG_LOUD(": Enter");

    NTSTATUS Status = STATUS_SUCCESS;
    PCHAR Buf = NULL;
    if (SystemBuffer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    PAGED_CODE();
    
    UINT64 i = 0;
    NBD_REQUEST Request;
    NBD_REPLY Reply;

    Request.Magic = RtlUlongByteSwap(NBD_REQUEST_MAGIC);
    Request.Type = RtlUlongByteSwap(NBD_CMD_WRITE);
    Request.Length = RtlUlongByteSwap(Length);
    RtlCopyMemory(&(Request.Handle), &i, sizeof(i));
    Request.From = RtlUlonglongByteSwap(Offset);
    Buf = Malloc(Length + sizeof(NBD_REQUEST));

    if (NULL == Buf) {
        WNBD_LOG_ERROR("Insufficient resources");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    if (-1 == Fd) {
        WNBD_LOG_ERROR("Invalid socket");
        Status = STATUS_INVALID_SESSION;
        goto Exit;
    }
#pragma warning(disable:6386)
    RtlCopyMemory(Buf, &Request, sizeof(NBD_REQUEST));
#pragma warning(default:6386)
    RtlCopyMemory((Buf + sizeof(NBD_REQUEST)), SystemBuffer, Length);

    if (-1 == RbdWriteExact(Fd, Buf, sizeof(NBD_REQUEST) + Length)) {
        WNBD_LOG_ERROR("Could not send request for NBD_CMD_WRITE");
        Status = STATUS_INVALID_SESSION;
        goto Exit;
    }

    if (-1 == RbdReadExact(Fd, &Reply, sizeof(NBD_REPLY))) {
        WNBD_LOG_ERROR("Could not read request for NBD_CMD_WRITE");
        Status = STATUS_INVALID_SESSION;
        goto Exit;
    }
    if (NBD_REPLY_MAGIC != RtlUlongByteSwap(Reply.Magic)) {
        WNBD_LOG_ERROR("Invalid NBD_REPLY_MAGIC for NBD_CMD_WRITE");
        Status = STATUS_ABANDONED;
        goto Exit;
    }
    if (0 != Reply.Error) {
        WNBD_LOG_ERROR("Received reply error from NBD_CMD_WRITE: %llu", RtlUlongByteSwap(Reply.Error));
        Status = STATUS_ABANDONED;
        goto Exit;
    }

Exit:
    if (NULL != Buf) {
        Free(Buf);
    }
    *IoStatus = Status;
    WNBD_LOG_LOUD(": Exit");
}
