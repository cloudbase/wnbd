/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <berkeley.h>
#include <ksocket.h>
#include <ws2def.h>
#include "common.h"
#include "debug.h"
#include "nbd_protocol.h"

const UINT64 CLIENT_MAGIC = 0x00420281861253LL;
const UINT64 OPTION_MAGIC = 0x49484156454F5054LL;
const UINT64 REPLY_MAGIC  = 0x3e889045565a9LL;

INT
NbdReadExact(_In_ INT Fd,
             _Inout_ PVOID Data,
             _In_ size_t Length,
             _Inout_ PNTSTATUS error)
{
    if (-1 == Fd) {
        *error = STATUS_CONNECTION_DISCONNECTED;
        return -1;
    }
    INT Result = 0;
    PUCHAR Temp = Data;
    while (0 < Length) {
        Result = Recv(Fd, Temp, Length, WSK_FLAG_WAITALL, error);
        if (Result > 0) {
            Length -= Result;
            Temp = Temp + Result;
        } else {
            WNBD_LOG_WARN("Failed with : %d", Result);
            *error = STATUS_CONNECTION_DISCONNECTED;
            return -1;
        }
    }
    ASSERT(Length == 0);
    return 0;
}

INT
NbdWriteExact(_In_ INT Fd,
              _In_ PVOID Data,
              _In_ size_t Length,
              _Inout_ PNTSTATUS error)
{
    if (-1 == Fd) {
        *error = STATUS_CONNECTION_DISCONNECTED;
        return -1;
    }
    INT Result = 0;
    PUCHAR Temp = Data;
    while (Length > 0) {
        Result = Send(Fd, Temp, Length, 0, error);
        if (Result <= 0) {
            WNBD_LOG_WARN("Failed with : %d", Result);
            *error = STATUS_CONNECTION_DISCONNECTED;
            return -1;
        }
        Length -= Result;
        Temp += Result;
    }
    ASSERT(Length == 0);
    return 0;
}

VOID
NbdSendRequest(_In_ INT Fd,
               _In_ UINT32 Option,
               _In_ size_t Datasize,
               _Maybenull_ PVOID Data)
{
    NBD_HANDSHAKE_REQ Request;
    NTSTATUS error = STATUS_SUCCESS;
    Request.Magic = RtlUlonglongByteSwap(OPTION_MAGIC);
    Request.Option = RtlUlongByteSwap(Option);
    Request.Datasize = RtlUlongByteSwap((ULONG)Datasize);

    NbdWriteExact(Fd, &Request, sizeof(Request), &error);
    if (NULL != Data) {
        NbdWriteExact(Fd, Data, Datasize, &error);
    }
}

VOID
NbdSendInfoRequest(_In_ INT Fd,
                   _In_ UINT32 Options,
                   _In_ INT NumberOfRequests,
                   _Maybenull_ PUINT16 Request,
                   _In_ PCHAR Name)
{
    UINT16 rlen = RtlUshortByteSwap((USHORT)NumberOfRequests);
    UINT32 nlen = RtlUlongByteSwap((ULONG)strlen(Name));
    NTSTATUS error = STATUS_SUCCESS;
    size_t size = sizeof(UINT32) + strlen(Name) + sizeof(UINT16) + NumberOfRequests * sizeof(UINT16);

    NbdSendRequest(Fd, Options, size, NULL);
    NbdWriteExact(Fd, &nlen, sizeof(nlen), &error);
    NbdWriteExact(Fd, Name, strlen(Name), &error);
    NbdWriteExact(Fd, &rlen, sizeof(rlen), &error);
    if (NumberOfRequests > 0 && NULL != Request) {
        NbdWriteExact(Fd, Request, NumberOfRequests * sizeof(UINT16), &error);
    }
}

PNBD_HANDSHAKE_RPL
NbdReadHandshakeReply(_In_ INT Fd)
{
    PNBD_HANDSHAKE_RPL Retval = NbdMalloc(
        sizeof(NBD_HANDSHAKE_RPL));

    if (!Retval) {
        WNBD_LOG_ERROR("Insufficient resources. Failed to allocate: %d bytes",
            sizeof(NBD_HANDSHAKE_RPL));
        return NULL;
    }

    NTSTATUS error = STATUS_SUCCESS;
    RtlZeroMemory(Retval, sizeof(NBD_HANDSHAKE_RPL));
    NbdReadExact(Fd, Retval, sizeof(*Retval), &error);

    Retval->Magic = RtlUlonglongByteSwap(Retval->Magic);
    Retval->Option = RtlUlongByteSwap(Retval->Option);
    Retval->ReplyType = RtlUlongByteSwap(Retval->ReplyType);
    Retval->Datasize = RtlUlongByteSwap(Retval->Datasize);

    if (REPLY_MAGIC != Retval->Magic) {
        WNBD_LOG_ERROR("Received invalid negotiation magic %llu (expected %llu)",
                       Retval->Magic, REPLY_MAGIC);
        NbdFree(Retval);
        return NULL;
    }
    if (Retval->Datasize > 0) {
        INT NewSize = sizeof(NBD_HANDSHAKE_RPL) + Retval->Datasize;
        PNBD_HANDSHAKE_RPL RetvalTemp = NbdMalloc(NewSize);
        if (!RetvalTemp) {
            WNBD_LOG_ERROR("Insufficient resources. Failed to allocate: %d bytes",
                NewSize);
            return NULL;
        }
        RtlCopyMemory(RetvalTemp, Retval, sizeof(NBD_HANDSHAKE_RPL));
        if (Retval) {
            NbdFree(Retval);
        }
        Retval = NULL;
        NbdReadExact(Fd, &(RetvalTemp->Data), RetvalTemp->Datasize, &error);
        Retval = RetvalTemp;
    }
    return Retval;
}

VOID
NbdParseSizes(_In_ PCHAR Data,
              _Inout_ PUINT64 Size,
              _Inout_ PUINT16 Flags)
{
    RtlCopyMemory(Size, Data, sizeof(*Size));
    *Size = RtlUlonglongByteSwap(*Size);
    Data += sizeof(*Size);
    RtlCopyMemory(Flags, Data, sizeof(*Flags));
    *Flags = RtlUshortByteSwap(*Flags);
}

VOID
NbdSendOptExportName(_In_ INT Fd,
                     _In_ PUINT64 Size,
                     _In_ PUINT16 Flags,
                     _In_ BOOLEAN Go,
                     _In_ PCHAR Name,
                     _In_ UINT16 GFlags)
{
    NTSTATUS error = STATUS_SUCCESS;

    NbdSendRequest(Fd, NBD_OPT_EXPORT_NAME, strlen(Name), Name);
    CHAR Buf[sizeof(*Flags) + sizeof(*Size)];
    RtlZeroMemory(Buf, sizeof(*Flags) + sizeof(*Size));
    if (NbdReadExact(Fd, Buf, sizeof(Buf), &error) < 0 && Go) {
        WNBD_LOG_WARN("Server does not support NBD_OPT_GO and"
                      "dropped connection after sending NBD_OPT_EXPORT_NAME.");
        return;
    }
    NbdParseSizes(Buf, Size, Flags);
    if (!(GFlags & NBD_FLAG_NO_ZEROES)) {
        CHAR Temp[125];
        RtlZeroMemory(Temp, 125);
        NbdReadExact(Fd, Temp, 124, &error);
    }
}

NTSTATUS
NbdNegotiate(_In_ INT* Pfd,
             _In_ PUINT64 Size,
             _In_ PUINT16 Flags,
             _In_ PCHAR Name,
             _In_ UINT32 ClientFlags,
             _In_ BOOLEAN Go)
{
    UINT64 Magic = 0;
    UINT16 Temp = 0;
    UINT16 GFlags;
    CHAR Buf[256];
    INT Fd = *Pfd;
    NTSTATUS status = 0;

    RtlZeroMemory(Buf, 8);
    NbdReadExact(Fd, Buf, 8, &status);
    if (strcmp(Buf, INIT_PASSWD)) {
        WNBD_LOG_INFO("Received NBD INIT_PASSWD");
    }

    NbdReadExact(Fd, &Magic, sizeof(Magic), &status);
    Magic = RtlUlonglongByteSwap(Magic);
    if (OPTION_MAGIC != Magic
        && CLIENT_MAGIC == Magic) {
        WNBD_LOG_INFO("Old-style NBD server.");
    }

    NbdReadExact(Fd, &Temp, sizeof(UINT16), &status);
    GFlags = RtlUshortByteSwap(Temp);

    if (GFlags & NBD_FLAG_NO_ZEROES) {
        ClientFlags |= NBD_FLAG_NO_ZEROES;
    }

    ClientFlags = RtlUlongByteSwap(ClientFlags);
    INT Result = Send(Fd, (PCHAR)&ClientFlags, sizeof(ClientFlags), 0, &status);
    if (Result < 0) {
        WNBD_LOG_ERROR("Could not send NBD handshake request. "
                       "Error: %d", Result);
        return STATUS_FAIL_CHECK;
    }

    PNBD_HANDSHAKE_RPL Reply = NULL;
    if (!Go) {
        NbdSendOptExportName(Fd, Size, Flags, Go, Name, GFlags);
        return STATUS_SUCCESS;
    }
    NbdSendInfoRequest(Fd, NBD_OPT_GO, 0, NULL, Name);

    do {
        if (NULL != Reply) {
            NbdFree(Reply);
        }
        Reply = NbdReadHandshakeReply(Fd);
        if (!Reply) {
            return STATUS_UNSUCCESSFUL;
        }
        if (Reply && (Reply->ReplyType & NBD_REP_FLAG_ERROR)) {
            switch (Reply->ReplyType) {
            case NBD_REP_ERR_UNSUP:
                WNBD_LOG_ERROR("Received NBD_REP_ERR_UNSUP reply.");
                NbdSendOptExportName(Fd, Size, Flags, Go, Name, GFlags);
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
                return STATUS_ACCESS_DENIED;

            default:
                if (Reply->Datasize > 0) {
                    // Ugly hack to make the log message null terminated
                    Reply->Data[Reply->Datasize - 1] = '\0';
                    WNBD_LOG_ERROR("Unknown error returned by server. Server said: %s", Reply->Data);
                }
                else {
                    WNBD_LOG_WARN("Unknown error returned by server.");
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
                NbdParseSizes(Reply->Data + 2, Size, Flags);
                break;
            default:
                WNBD_LOG_WARN("Ignoring unsupported NBD reply info type: %u",
                              (unsigned int) Type);
                break;
            }
            break;
        case NBD_REP_ACK:
            WNBD_LOG_DEBUG("Received NBD_REP_ACK.");
            break;
        default:
            WNBD_LOG_WARN("Ignoring unknown reply to NBD_OPT_GO: %u.",
                          (unsigned int) Reply->ReplyType);
        }
    } while (Reply->ReplyType != NBD_REP_ACK);

    if (NULL != Reply) {
        NbdFree(Reply);
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
INT
NbdOpenAndConnect(PCHAR HostName,
                  DWORD PortNumber)
{
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

    DWORD Status = KsInitialize();
    if (!NT_SUCCESS(Status)) {
        WNBD_LOG_ERROR("Could not initialize WSK framework. Status: 0x%x.", Status);
        return -1;
    }

    char* PortName[12] = { 0 };
    RtlStringCbPrintfA((char*)PortName, sizeof(PortName), "%d", PortNumber);

    Error = GetAddrInfo(HostName, (char*)PortName, &Hints, &Ai);

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
        WNBD_LOG_ERROR("Socket failure");
        Fd = -1;
        goto err;
    }

    WNBD_LOG_INFO("Opened socket FD: %d", Fd);
err:
    if (Ai) {
        FreeAddrInfo(Ai);
    }
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
    NTSTATUS Status = STATUS_SUCCESS;

    if (-1 == Fd) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    PAGED_CODE();

    NBD_REQUEST Request;
    NTSTATUS error = STATUS_SUCCESS;

    Request.Magic = RtlUlongByteSwap(NBD_REQUEST_MAGIC);
    Request.Type = RtlUlongByteSwap(RequestType);
    Request.Length = RtlUlongByteSwap(Length);
    Request.From = RtlUlonglongByteSwap(Offset);
    Request.Handle = Handle;

    if (-1 == NbdWriteExact(Fd, &Request, sizeof(NBD_REQUEST), &error)) {
        WNBD_LOG_ERROR("Could not send request for %s.",
                       NbdRequestTypeStr(RequestType));
        Status = error;
        goto Exit;
    }
Exit:
    *IoStatus = Status;
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
    NTSTATUS Status = STATUS_SUCCESS;
    if (SystemBuffer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    PAGED_CODE();

    NBD_REQUEST Request;
    NTSTATUS error = STATUS_SUCCESS;

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
            WNBD_LOG_ERROR("Insufficient resources. Failed to allocate: %ud bytes", Needed);
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

    if (-1 == NbdWriteExact(Fd, *PreallocatedBuffer, sizeof(NBD_REQUEST) + Length, &error)) {
        WNBD_LOG_ERROR("Could not send request for NBD_CMD_WRITE");
        Status = error;
        goto Exit;
    }

Exit:
    *IoStatus = Status;
}

_Use_decl_annotations_
NTSTATUS
NbdReadReply(INT Fd,
             PNBD_REPLY Reply)
{
    PAGED_CODE();

    NTSTATUS error = STATUS_SUCCESS;
    if (-1 == NbdReadExact(Fd, Reply, sizeof(NBD_REPLY), &error)) {
        if (error == STATUS_CONNECTION_DISCONNECTED) {
            WNBD_LOG_INFO("NBD connection closed.");
        } else {
            WNBD_LOG_ERROR("Could not read command reply.");
        }
        return error;
    }

    if (NBD_REPLY_MAGIC != RtlUlongByteSwap(Reply->Magic)) {
        WNBD_LOG_ERROR("Invalid NBD_REPLY_MAGIC: %#x",
                       RtlUlongByteSwap(Reply->Magic));
        return STATUS_UNSUCCESSFUL;
    }

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
