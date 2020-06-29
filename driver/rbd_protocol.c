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

const UINT64 CLIENT_MAGIC = 0x00420281861253LL;
const UINT64 OPTION_MAGIC = 0x49484156454F5054LL;
const UINT64 REPLY_MAGIC  = 0x3e889045565a9LL;

INT
RbdReadExact(_In_ INT Fd,
             _Inout_ PVOID Data,
             _In_ size_t Length,
             _Inout_ PNTSTATUS error)
{
    WNBD_LOG_LOUD(": Enter");
    if (-1 == Fd) {
        *error = STATUS_CONNECTION_DISCONNECTED;
        return -1;
    }
    INT Result = 0;
    PUCHAR Temp = Data;
    while (0 < Length) {
        WNBD_LOG_INFO("Size to read = %lu", Length);
        Result = Recv(Fd, Temp, Length, WSK_FLAG_WAITALL, error);
        if (Result > 0) {
            Length -= Result;
            Temp = Temp + Result;
        } else {
            WNBD_LOG_ERROR("Failed with : %d", Result);
            *error = STATUS_CONNECTION_DISCONNECTED;
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
              _In_ size_t Length,
              _Inout_ PNTSTATUS error)
{
    WNBD_LOG_LOUD(": Enter");
    if (-1 == Fd) {
        *error = STATUS_CONNECTION_DISCONNECTED;
        return -1;
    }
    INT Result = 0;
    PUCHAR Temp = Data;
    while (Length > 0) {
        WNBD_LOG_INFO("Size to send = %lu", Length);
        Result = Send(Fd, Temp, Length, 0, error);
        if (Result <= 0) {
            WNBD_LOG_ERROR("Failed with : %d", Result);
            *error = STATUS_CONNECTION_DISCONNECTED;
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
    NTSTATUS error;
    Request.Magic = RtlUlonglongByteSwap(OPTION_MAGIC);
    Request.Option = RtlUlongByteSwap(Option);
    Request.Datasize = RtlUlongByteSwap((ULONG)Datasize);

    RbdWriteExact(Fd, &Request, sizeof(Request), &error);
    if (NULL != Data) {
        RbdWriteExact(Fd, Data, Datasize, &error);
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
    NTSTATUS error;
    size_t size = sizeof(UINT32) + strlen(Name) + sizeof(UINT16) + NumberOfRequests * sizeof(UINT16);

    RbdSendRequest(Fd, Options, size, NULL);
    RbdWriteExact(Fd, &nlen, sizeof(nlen), &error);
    RbdWriteExact(Fd, Name, strlen(Name), &error);
    RbdWriteExact(Fd, &rlen, sizeof(rlen), &error);
    if (NumberOfRequests > 0) {
        RbdWriteExact(Fd, Request, NumberOfRequests * sizeof(UINT16), &error);
    }
    WNBD_LOG_LOUD(": Exit");
}

PREPLY_HEADER
RbdReadReply(_In_ INT Fd)
{
    WNBD_LOG_LOUD(": Enter");
    PREPLY_HEADER Retval = NbdMalloc(sizeof(REPLY_HEADER));

    if (!Retval) {
        WNBD_LOG_ERROR("Insufficient resources to allocate memory");
        return NULL;
    }

    NTSTATUS error;
    RtlZeroMemory(Retval, sizeof(REPLY_HEADER));
    RbdReadExact(Fd, Retval, sizeof(*Retval), &error);

    Retval->Magic = RtlUlonglongByteSwap(Retval->Magic);
    Retval->Option = RtlUlongByteSwap(Retval->Option);
    Retval->ReplyType = RtlUlongByteSwap(Retval->ReplyType);
    Retval->Datasize = RtlUlongByteSwap(Retval->Datasize);

    if (REPLY_MAGIC != Retval->Magic) {
        WNBD_LOG_ERROR("Received invalid negotiation magic %llu (expected %llu)", Retval->Magic, REPLY_MAGIC);
        NbdFree(Retval);
        return NULL;
    }
    if (Retval->Datasize > 0) {
        INT NewSize = sizeof(REPLY_HEADER) + Retval->Datasize;
        PREPLY_HEADER RetvalTemp = NbdMalloc(NewSize);
        if (!RetvalTemp) {
            WNBD_LOG_ERROR("Insufficient resources to allocate memory");
            return NULL;
        }
        RtlCopyMemory(RetvalTemp, Retval, sizeof(REPLY_HEADER));
        if (Retval) {
            NbdFree(Retval);
        }
        Retval = NULL;
        RbdReadExact(Fd, &(RetvalTemp->Data), RetvalTemp->Datasize, &error);
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

    NTSTATUS error;

    RbdSendRequest(Fd, NBD_OPT_EXPORT_NAME, strlen(Name), Name);
    CHAR Buf[sizeof(*Flags) + sizeof(*Size)];
    if (RbdReadExact(Fd, Buf, sizeof(Buf), &error) < 0 && Go) {
        WNBD_LOG_ERROR("Server does not support NBD_OPT_GO and"
            "dropped connection after sending NBD_OPT_EXPORT_NAME.");
        return;
    }
    RbdParseSizes(Buf, Size, Flags);
    if (!(GFlags & NBD_FLAG_NO_ZEROES)) {
        CHAR Temp[125];
        RbdReadExact(Fd, Temp, 124, &error);
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
    NTSTATUS status = 0;

    RtlZeroMemory(Buf, 8);
    RbdReadExact(Fd, Buf, 8, &status);
    if (strcmp(Buf, INIT_PASSWD)) {
        WNBD_LOG_ERROR("INIT_PASSWD");
    }

    RbdReadExact(Fd, &Magic, sizeof(Magic), &status);
    Magic = RtlUlonglongByteSwap(Magic);
    if (OPTION_MAGIC != Magic
        && CLIENT_MAGIC == Magic) {
        WNBD_LOG_ERROR("Old-style server.");
    }

    RbdReadExact(Fd, &Temp, sizeof(UINT16), &status);
    GFlags = RtlUshortByteSwap(Temp);

    if (GFlags & NBD_FLAG_NO_ZEROES) {
        ClientFlags |= NBD_FLAG_NO_ZEROES;
    }

    ClientFlags = RtlUlongByteSwap(ClientFlags);
    if (Send(Fd, (PCHAR)&ClientFlags, sizeof(ClientFlags), 0, &status) < 0) {
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
            NbdFree(Reply);
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
                NbdFree(Reply);
                return STATUS_SUCCESS;

            case NBD_REP_ERR_POLICY:
                if (Reply->Datasize > 0) {
                    // Ugly hack to make the log message null terminated
                    Reply->Data[Reply->Datasize -1] = '\0';
                    WNBD_LOG_ERROR("Connection not allowed by server policy. Server said: %s", Reply->Data);
                } else {
                    WNBD_LOG_ERROR("Connection not allowed by server policy.");
                }
                NbdFree(Reply);
                return STATUS_FAIL_CHECK;

            default:
                if (Reply->Datasize > 0) {
                    // Ugly hack to make the log message null terminated
                    Reply->Data[Reply->Datasize - 1] = '\0';
                    WNBD_LOG_INFO("Unknown error returned by server. Server said: %s", Reply->Data);
                }
                else {
                    WNBD_LOG_ERROR("Unknown error returned by server.");
                }
                NbdFree(Reply);
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
        NbdFree(Reply);
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
NbdRequest(
    INT Fd,
    UINT64 Offset,
    ULONG Length,
    PNTSTATUS IoStatus,
    UINT64 Handle,
    NbdRequestType RequestType)
{
    WNBD_LOG_LOUD(": Enter");
    NTSTATUS Status = STATUS_SUCCESS;

    if (-1 == Fd) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    PAGED_CODE();

    NBD_REQUEST Request;
    NTSTATUS error;

    Request.Magic = RtlUlongByteSwap(NBD_REQUEST_MAGIC);
    Request.Type = RtlUlongByteSwap(RequestType);
    Request.Length = RtlUlongByteSwap(Length);
    Request.From = RtlUlonglongByteSwap(Offset);
    Request.Handle = Handle;

    if (-1 == RbdWriteExact(Fd, &Request, sizeof(NBD_REQUEST), &error)) {
        WNBD_LOG_INFO("Could not send request for %s.",
                      NbdRequestTypeStr(RequestType));
        Status = error;
        goto Exit;
    }
Exit:
    *IoStatus = Status;
    WNBD_LOG_LOUD(": Exit");
}

_Use_decl_annotations_
VOID
NbdWriteStat(INT Fd,
             UINT64 Offset,
             ULONG Length,
             PNTSTATUS IoStatus,
             PVOID SystemBuffer,
             PVOID *PreallocatedBuffer,
             PULONG PreallocatedLength,
             UINT64 Handle,
             UINT32 NbdTransmissionFlags)
{
    WNBD_LOG_LOUD(": Enter");

    NTSTATUS Status = STATUS_SUCCESS;
    if (SystemBuffer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    PAGED_CODE();

    NBD_REQUEST Request;
    NTSTATUS error;

    Request.Magic = RtlUlongByteSwap(NBD_REQUEST_MAGIC);
    Request.Type = RtlUlongByteSwap(NBD_CMD_WRITE | NbdTransmissionFlags);
    Request.Length = RtlUlongByteSwap(Length);
    Request.From = RtlUlonglongByteSwap(Offset);
    Request.Handle = Handle;

    UINT Needed = Length + sizeof(NBD_REQUEST);
    if (*PreallocatedLength < Needed) {
        PCHAR Buf = NULL;
        Buf = NbdMalloc(Needed);
        if (NULL == Buf) {
            WNBD_LOG_ERROR("Insufficient resources");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }
        ExFreePool(*PreallocatedBuffer);
        *PreallocatedLength = Needed;
        *PreallocatedBuffer = Buf;
    }

    if (-1 == Fd) {
        WNBD_LOG_ERROR("Invalid socket");
        Status = STATUS_INVALID_SESSION;
        goto Exit;
    }
#pragma warning(disable:6386)
    RtlCopyMemory(*PreallocatedBuffer, &Request, sizeof(NBD_REQUEST));
#pragma warning(default:6386)
    RtlCopyMemory(((PCHAR)*PreallocatedBuffer + sizeof(NBD_REQUEST)), SystemBuffer, Length);

    if (-1 == RbdWriteExact(Fd, *PreallocatedBuffer, sizeof(NBD_REQUEST) + Length, &error)) {
        WNBD_LOG_ERROR("Could not send request for NBD_CMD_WRITE");
        Status = error;
        goto Exit;
    }

Exit:
    *IoStatus = Status;
    WNBD_LOG_LOUD(": Exit");
}

_Use_decl_annotations_
NTSTATUS
NbdReadReply(INT Fd,
             PNBD_REPLY Reply) {
    WNBD_LOG_LOUD(": Enter");
    PAGED_CODE();

    NTSTATUS error;
    if (-1 == RbdReadExact(Fd, Reply, sizeof(NBD_REPLY), &error)) {
        WNBD_LOG_INFO("Could not read command reply.");
        return error;
    }

    if (NBD_REPLY_MAGIC != RtlUlongByteSwap(Reply->Magic)) {
        WNBD_LOG_INFO("Invalid NBD_REPLY_MAGIC.");
        return STATUS_ABANDONED;
    }

    WNBD_LOG_LOUD(": Exit");
    return STATUS_SUCCESS;
}

char* NbdRequestTypeStr(NbdRequestType RequestType) {
    switch(RequestType) {
    case NBD_CMD_READ:
        return "NBD_CMD_READ";
    case NBD_CMD_WRITE:
        return "NBD_CMD_WRITE";
    case NBD_CMD_DISC:
        return "NBD_CMD_DISC";
    case NBD_CMD_FLUSH:
        return "NBD_CMD_FLUSH";
    case NBD_CMD_TRIM:
        return "NBD_CMD_TRIM";
    default:
        return "UNKNOWN";
    }
}
