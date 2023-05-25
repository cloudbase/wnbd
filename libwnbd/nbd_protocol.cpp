/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "nbd_protocol.h"
#include "wnbd_log.h"
#include "utils.h"

#include <boost/endian/conversion.hpp>

using boost::endian::native_to_big;
using boost::endian::native_to_big_inplace;
using boost::endian::big_to_native;
using boost::endian::big_to_native_inplace;

_Use_decl_annotations_
DWORD RecvExact(
    SOCKET Fd,
    PVOID Data,
    size_t Length)
{
    if (Fd == INVALID_SOCKET) {
        return ERROR_INVALID_HANDLE;
    }
    INT Result = 0;
    PCHAR CurrDataPtr = (PCHAR) Data;
    while (0 < Length) {
        Result = ::recv(Fd, CurrDataPtr, (int) Length, 0);
        if (Result > 0) {
            Length -= Result;
            CurrDataPtr = CurrDataPtr + Result;
        } else {
            if (Result) {
                auto Err = WSAGetLastError();
                switch(Err) {
                case WSAEINTR:
                    LogInfo("Request canceled.");
                    // Not a typo.
                    Result = ERROR_CANCELLED;
                    break;
                case WSAESHUTDOWN:
                case WSAECONNRESET:
                case WSAEDISCON:
                    LogInfo("Connection closed. "
                            "Status: %d. Message: %s",
                            Err, win32_strerror(Err).c_str());
                    Result = ERROR_GRACEFUL_DISCONNECT;
                    break;
                default:
                    LogError("Read failed. "
                             "Error: %d. Error message: %s",
                             Err, win32_strerror(Err).c_str());
                }
            } else {
                LogInfo("Connection closed.");
                Result = ERROR_GRACEFUL_DISCONNECT;
            }
            return Result;
        }
    }
    if (Length) {
        // Shouldn't get here.
        LogError("Couldn't receive all data.");
        return ERROR_GEN_FAILURE;
    }
    return 0;
}

DWORD SendExact(
    _In_ SOCKET Fd,
    _In_ PVOID Data,
    _In_ size_t Length)
{
    if (Fd == INVALID_SOCKET) {
        return ERROR_INVALID_HANDLE;
    }
    INT Result = 0;
    PCHAR CurrDataPtr = (PCHAR) Data;
    while (Length > 0) {
        Result = ::send(Fd, CurrDataPtr, (int) Length, 0);
        if (Result <= 0) {
            auto Err = WSAGetLastError();
            switch(Err) {
            case WSAEINTR:
                LogInfo("Request canceled.");
                Result = ERROR_CANCELLED;
                break;
            case WSAESHUTDOWN:
            case WSAECONNRESET:
            case WSAEDISCON:
                LogInfo("Connection closed. "
                        "Status: %d. Message: %s.",
                        Err, win32_strerror(Err).c_str());
                Result = ERROR_GRACEFUL_DISCONNECT;
                break;
            default:
                LogError("Send failed. "
                         "Error: %d. Error message: %s",
                         Err, win32_strerror(Err).c_str());
            }
            return Result;
        }
        Length -= Result;
        CurrDataPtr += Result;
    }
    if (Length) {
        // Shouldn't get here.
        LogError("Couldn't send all data.");
        return ERROR_GEN_FAILURE;
    }
    return 0;
}

DWORD NbdSendHandshakeRequest(
    _In_ SOCKET Fd,
    _In_ UINT32 Option,
    _In_ size_t Datasize,
    _Maybenull_ PVOID Data)
{
    NBD_HANDSHAKE_REQ Request;
    Request.Magic = native_to_big(OPTION_MAGIC);
    Request.Option = native_to_big(Option);
    Request.Datasize = native_to_big((ULONG)Datasize);

    DWORD Retval = SendExact(Fd, &Request, sizeof(Request));
    if (!Retval && Data) {
        Retval = SendExact(Fd, Data, Datasize);
    }
    return Retval;
}

DWORD NbdSendInfoRequest(
    _In_ SOCKET Fd,
    _In_ UINT32 Options,
    _In_ USHORT NumberOfRequests,
    _Maybenull_ PUINT16 Requests,
    _In_ std::string ExportName)
{
    UINT16 NumberOfRequestsBE = native_to_big(NumberOfRequests);
    UINT32 NameLenBE = native_to_big((UINT32) ExportName.length());
    size_t TotalLen = sizeof(UINT32) +
                    ExportName.length() +
                    sizeof(UINT16) +
                    NumberOfRequests * sizeof(UINT16);

    DWORD Retval = 0;
    if (Retval = NbdSendHandshakeRequest(Fd, Options, TotalLen, NULL); Retval)
        return Retval;
    if (Retval = SendExact(Fd, &NameLenBE, sizeof(NameLenBE)); Retval)
        return Retval;
    if (Retval = SendExact(Fd, (PVOID) ExportName.c_str(),
                           ExportName.length()); Retval)
        return Retval;
    if (Retval = SendExact(Fd, &NumberOfRequestsBE,
                           sizeof(NumberOfRequestsBE)); Retval)
        return Retval;

    if (NumberOfRequests > 0 && Requests) {
        Retval = SendExact(
            Fd, Requests,
            NumberOfRequests * sizeof(UINT16));
    }

    return Retval;
}

PNBD_HANDSHAKE_RPL NbdReadHandshakeReply(_In_ SOCKET Fd)
{
    PNBD_HANDSHAKE_RPL Reply = (PNBD_HANDSHAKE_RPL) calloc(
        1, sizeof(NBD_HANDSHAKE_RPL));
    if (!Reply) {
        LogError("Unable to allocate %d bytes.", sizeof(NBD_HANDSHAKE_RPL));
        return nullptr;
    }

    DWORD Retval = RecvExact(Fd, Reply, sizeof(*Reply));
    if (Retval) {
        free(Reply);
        return nullptr;
    }

    big_to_native_inplace(Reply->Magic);
    big_to_native_inplace(Reply->Option);
    big_to_native_inplace(Reply->ReplyType);
    big_to_native_inplace(Reply->Datasize);

    if (REPLY_MAGIC != Reply->Magic) {
        LogError("Received invalid negotiation magic %llu (expected %llu)",
                 Reply->Magic, REPLY_MAGIC);
        free(Reply);
        return nullptr;
    }

    if (Reply->Datasize > 0) {
        size_t NewSize = sizeof(NBD_HANDSHAKE_RPL) + Reply->Datasize;
        PVOID ReplyTemp = realloc(Reply, NewSize);
        if (!ReplyTemp) {
            LogError("Unable to allocate %d bytes.", NewSize);
            free(Reply);
            return nullptr;
        }
        Reply = (PNBD_HANDSHAKE_RPL) ReplyTemp;

        Retval = RecvExact(Fd, &(Reply->Data), Reply->Datasize);
        if (Retval) {
            free(Reply);
        }
    }
    return Reply;
}

void NbdParseSizes(
    _In_ PCHAR Data,
    _Inout_ PUINT64 Size,
    _Inout_ PUINT16 Flags)
{
    CopyMemory(Size, Data, sizeof(*Size));
    big_to_native_inplace(*Size);
    Data += sizeof(*Size);
    CopyMemory(Flags, Data, sizeof(*Flags));
    big_to_native_inplace(*Flags);
}

DWORD NbdSendOptExportName(
    _In_ SOCKET Fd,
    _In_ PUINT64 Size,
    _In_ PUINT16 Flags,
    _In_ std::string ExportName,
    _In_ UINT16 GFlags)
{
    DWORD Retval = NbdSendHandshakeRequest(
        Fd, NBD_OPT_EXPORT_NAME,
        ExportName.length(), (PVOID) ExportName.c_str());
    if (Retval) {
        return Retval;
    }

    CHAR Buf[sizeof(*Flags) + sizeof(*Size)];
    ZeroMemory(Buf, sizeof(*Flags) + sizeof(*Size));
    if (Retval = RecvExact(Fd, Buf, sizeof(Buf)); Retval) {
        return Retval;
    }
    
    NbdParseSizes(Buf, Size, Flags);
    if (!(GFlags & NBD_FLAG_NO_ZEROES)) {
        CHAR Temp[125] = { 0 };
        // read the reserved bytes.
        Retval = RecvExact(Fd, Temp, 124);
    }

    return Retval;
}

DWORD NbdNegotiate(
    _In_ SOCKET Fd,
    _In_ PUINT64 Size,
    _In_ PUINT16 Flags,
    _In_ std::string ExportName,
    _In_ UINT32 ClientFlags)
{
    UINT64 Magic = 0;
    UINT16 GFlags = 0;
    CHAR Buf[256] = { 0 };

    DWORD Retval = RecvExact(Fd, Buf, 8);
    if (Retval) {
        return Retval;
    }
    if (!strcmp(Buf, INIT_PASSWD)) {
        // TODO: should we error out otherwise?
        LogDebug("Received NBD INIT_PASSWD");
    }

    if (Retval = RecvExact(Fd, &Magic, sizeof(Magic)); Retval) {
        return Retval;
    }
    big_to_native_inplace(Magic);
    if (OPTION_MAGIC != Magic && CLIENT_MAGIC == Magic) {
        LogInfo("Old-style NBD server.");
    }

    if (Retval = RecvExact(Fd, &GFlags, sizeof(UINT16)); Retval) {
        return Retval;
    }
    big_to_native_inplace(GFlags);

    if (GFlags & NBD_FLAG_NO_ZEROES) {
        ClientFlags |= NBD_FLAG_NO_ZEROES;
    }

    big_to_native_inplace(ClientFlags);
    Retval = SendExact(Fd, (PCHAR) &ClientFlags, sizeof(ClientFlags));
    if (Retval) {
        LogError("Could not send NBD handshake request.");
        return Retval;
    }

    PNBD_HANDSHAKE_RPL Reply = NULL;
    Retval = NbdSendInfoRequest(Fd, NBD_OPT_GO, 0, NULL, ExportName);
    if (Retval) {
        LogError("Could not send NBD handshake request.");
        return Retval;
    }

    do {
        if (Reply) {
            free(Reply);
        }
        Reply = NbdReadHandshakeReply(Fd);
        if (!Reply) {
            LogError("Couldn't retrieve handshake reply.");
            return ERROR_GEN_FAILURE;
        }
        if (Reply->ReplyType & NBD_REP_FLAG_ERROR) {
            switch (Reply->ReplyType) {
            case NBD_REP_ERR_UNSUP:
                LogWarning("Received NBD_REP_ERR_UNSUP reply. "
                           "Trying NBD_OPT_EXPORT_NAME as fallback.");
                free(Reply);
                Retval = NbdSendOptExportName(Fd, Size, Flags,
                                              ExportName, GFlags);
                if (Retval) {
                    LogError("NBD_OPT_EXPORT_NAME failed.");
                } else {
                    LogInfo("NBD_OPT_EXPORT_NAME fallback succeeded.");
                }
                return Retval;
            case NBD_REP_ERR_POLICY:
                if (Reply->Datasize > 0) {
                    // ensure that the log message is null terminated.
                    Reply->Data[Reply->Datasize - 1] = '\0';
                    LogError("Connection not allowed by server policy. "
                             "Server said: %s", Reply->Data);
                } else {
                    LogError("Connection not allowed by server policy.");
                }
                free(Reply);
                return ERROR_ACCESS_DENIED;
            default:
                if (Reply->Datasize > 0) {
                     // ensure that the log message is null terminated.
                    Reply->Data[Reply->Datasize - 1] = '\0';
                    LogError("Unknown error returned by server. "
                             "Server said: %s", Reply->Data);
                } else {
                    LogWarning("Unknown error returned by server.");
                }
                free(Reply);
                return ERROR_ACCESS_DENIED;
            }
        }

        UINT16 Type;
        switch (Reply->ReplyType) {
        case NBD_REP_INFO:
            memcpy(&Type, Reply->Data, 2);
            big_to_native_inplace(Type);
            switch (Type) {
            case NBD_INFO_EXPORT:
                NbdParseSizes(Reply->Data + 2, Size, Flags);
                break;
            default:
                LogWarning("Ignoring unsupported NBD reply info type: %u",
                           (unsigned int) Type);
                break;
            }
            break;
        case NBD_REP_ACK:
            LogDebug("Received NBD_REP_ACK.");
            break;
        default:
            LogWarning("Ignoring unknown reply to NBD_OPT_GO: %u.",
                       (unsigned int) Reply->ReplyType);
        }
    } while (Reply->ReplyType != NBD_REP_ACK);

    if (Reply) {
        free(Reply);
    }

    LogInfo("NBD negotiation successful.");
    return 0;
}

_Use_decl_annotations_
DWORD NbdRequest(
    SOCKET Fd,
    UINT64 Offset,
    ULONG Length,
    UINT64 Handle,
    NbdRequestType RequestType)
{
    if (INVALID_SOCKET == Fd) {
        return ERROR_INVALID_HANDLE;
    }

    NBD_REQUEST Request;
    Request.Magic = native_to_big((ULONG) NBD_REQUEST_MAGIC);
    Request.Type = native_to_big((ULONG) RequestType);
    Request.Length = native_to_big((ULONG) Length);
    Request.From = native_to_big((UINT64) Offset);
    Request.Handle = Handle;

    DWORD Retval = SendExact(Fd, &Request, sizeof(NBD_REQUEST));
    if (Retval) {
        LogError("Could not send request: %s.",
                 NbdRequestTypeStr(RequestType));
    }
    return Retval;
}

_Use_decl_annotations_
DWORD NbdSendWrite(
    SOCKET Fd,
    UINT64 Offset,
    ULONG Length,
    PVOID Data,
    PVOID *PreallocatedBuffer,
    PULONG PreallocatedLength,
    UINT64 Handle,
    UINT32 NbdTransmissionFlags)
{

    if (!Data) {
        LogError("No input buffer.");
        return ERROR_INVALID_PARAMETER;
    }
    if (INVALID_SOCKET == Fd) {
        LogError("Invalid socket.");
        return ERROR_INVALID_HANDLE;
    }

    NBD_REQUEST Request;
    Request.Magic = native_to_big((ULONG) NBD_REQUEST_MAGIC);
    Request.Type = native_to_big(
        (ULONG) (NBD_CMD_WRITE | NbdTransmissionFlags));
    Request.Length = native_to_big((ULONG) Length);
    Request.From = native_to_big((UINT64) Offset);
    Request.Handle = Handle;

    UINT Needed = Length + sizeof(NBD_REQUEST);
    if (*PreallocatedLength < Needed) {
        PCHAR Buf = (PCHAR) calloc(1, Needed);
        if (!Buf) {
            LogError("Insufficient resources. "
                     "Failed to allocate: %ud bytes", Needed);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        free(*PreallocatedBuffer);
        *PreallocatedLength = Needed;
        *PreallocatedBuffer = Buf;
    }

#pragma warning(disable:6386)
    CopyMemory(*PreallocatedBuffer, &Request,
               sizeof(NBD_REQUEST));
#pragma warning(default:6386)
    CopyMemory(((PCHAR)*PreallocatedBuffer + sizeof(NBD_REQUEST)),
               Data, Length);

    DWORD Retval = SendExact(
        Fd, *PreallocatedBuffer,
        sizeof(NBD_REQUEST) + Length);
    if (Retval) {
        LogError("Couldn't submit NBD_CMD_WRITE.");
    }
    return Retval;
}

_Use_decl_annotations_
DWORD NbdReadReply(SOCKET Fd, PNBD_REPLY Reply)
{
    DWORD Retval = RecvExact(Fd, Reply, sizeof(NBD_REPLY));
    if (Retval) {
        if (Retval != ERROR_GRACEFUL_DISCONNECT &&
                Retval != ERROR_CANCELLED) {
            LogError("Couldn't read NBD reply.");
        }
        return Retval;
    }

    if (NBD_REPLY_MAGIC != big_to_native(Reply->Magic)) {
        LogError("Invalid NBD_REPLY_MAGIC: %#x",
                 big_to_native(Reply->Magic));
        return ERROR_BAD_FORMAT;
    }

    return 0;
}

const char* NbdRequestTypeStr(NbdRequestType RequestType) {
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
