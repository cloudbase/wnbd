#include <ntifs.h>

#include "wnbd_dispatch.h"
#include "util.h"
#include "srb_helper.h"
#include "debug.h"
#include "scsi_function.h"
#include "scsi_trace.h"

inline int
ScsiOpToWnbdReqType(int ScsiOp)
{
    switch (ScsiOp) {
    case SCSIOP_READ6:
    case SCSIOP_READ:
    case SCSIOP_READ12:
    case SCSIOP_READ16:
        return WnbdReqTypeRead;
    case SCSIOP_WRITE6:
    case SCSIOP_WRITE:
    case SCSIOP_WRITE12:
    case SCSIOP_WRITE16:
        return WnbdReqTypeWrite;
    case SCSIOP_UNMAP:
        return WnbdReqTypeUnmap;
    case SCSIOP_SYNCHRONIZE_CACHE:
    case SCSIOP_SYNCHRONIZE_CACHE16:
        return WnbdReqTypeFlush;
    default:
        return WnbdReqTypeUnknown;
    }
}

NTSTATUS LockUsermodeBuffer(
    PVOID Buffer, UINT32 BufferSize, BOOLEAN Writeable,
    PVOID* OutBuffer, PMDL* OutMdl, BOOLEAN* Locked)
{
    NTSTATUS Status = 0;
    __try
    {
        if (Writeable)
            ProbeForWrite(Buffer, BufferSize, 1);
        else
            ProbeForRead(Buffer, BufferSize, 1);

        PMDL Mdl = IoAllocateMdl(
            Buffer,
            BufferSize,
            FALSE,
            FALSE,
            NULL);
        if (!Mdl)
        {
            WNBD_LOG_ERROR("Could not allocate MDL. Buffer: %p, size: %d.",
                           Buffer, BufferSize);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            __leave;
        }

        *OutMdl = Mdl;
        MmProbeAndLockPages(Mdl, UserMode, Writeable ? IoWriteAccess : IoReadAccess);
        *Locked = TRUE;

        *OutBuffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority | MdlMappingNoExecute);
        if (!*OutBuffer)
        {
            WNBD_LOG_ERROR("Could not get system MDL address. Buffer: %p, size: %d.",
                           Buffer, BufferSize);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            __leave;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Status = GetExceptionCode();
        WNBD_LOG_ERROR("Encountered exception while retrieving user buffer. "
                       "Exception: %d, buffer: %p, size: %d.",
                       Status, Buffer, BufferSize);
    }

    return Status;
}

NTSTATUS WnbdDispatchRequest(
    PIRP Irp,
    PWNBD_SCSI_DEVICE Device,
    PWNBD_IOCTL_FETCH_REQ_COMMAND Command)
{
    PVOID Buffer;
    NTSTATUS Status = 0;
    PWNBD_IO_REQUEST Request = &Command->Request;
    PMDL Mdl = NULL;
    BOOLEAN BufferLocked = FALSE;

    if ((ULONG)Device->Properties.Pid != IoGetRequestorProcessId(Irp)) {
        WNBD_LOG_LOUD("Invalid pid: %d != %u.",
            Device->Properties.Pid, IoGetRequestorProcessId(Irp));
        return STATUS_ACCESS_DENIED;
    }
    if ((ULONG)Device->Properties.Flags.UseNbd) {
        WNBD_LOG_LOUD("Direct IO is not allowed using NBD devices.");
        return STATUS_ACCESS_DENIED;
    }

    Status = LockUsermodeBuffer(
        Command->DataBuffer, Command->DataBufferSize, TRUE,
        &Buffer, &Mdl, &BufferLocked);
    if (Status)
        goto Exit;

    static UINT64 RequestHandle = 0;

    // We're looping through the requests until we manage to dispatch one.
    // Unsupported requests as well as most errors will be hidden from the caller.
    while (!Device->HardTerminateDevice) {
        PVOID WaitObjects[2];
        WaitObjects[0] = &Device->DeviceEvent;
        WaitObjects[1] = &Device->TerminateEvent;
        NTSTATUS WaitResult = KeWaitForMultipleObjects(
            2, WaitObjects, WaitAny, Executive, KernelMode,
            TRUE, NULL, NULL);
        if (STATUS_WAIT_1  == WaitResult)
            break;

        if (STATUS_ALERTED == WaitResult) {
            // This happens when the calling thread is terminating.
            // TODO: ensure that we haven't been alerted for some other reason.
            WNBD_LOG_INFO("Wait alterted, terminating.");
            KeSetEvent(&Device->TerminateEvent, IO_NO_INCREMENT, FALSE);
            break;
        }

        PLIST_ENTRY RequestEntry = ExInterlockedRemoveHeadList(
            &Device->RequestListHead,
            &Device->RequestListLock);

        if (Device->HardTerminateDevice) {
            break;
        }
        if (!RequestEntry) {
            continue;
        }

        // TODO: consider moving this part to a helper function.
        PSRB_QUEUE_ELEMENT Element = CONTAINING_RECORD(RequestEntry, SRB_QUEUE_ELEMENT, Link);
        Element->Tag = InterlockedIncrement64(&(LONG64)RequestHandle);
        Element->Srb->DataTransferLength = 0;
        PCDB Cdb = (PCDB)&Element->Srb->Cdb;

        RtlZeroMemory(Request, sizeof(WNBD_IO_REQUEST));
        WnbdRequestType RequestType = ScsiOpToWnbdReqType(Cdb->AsByte[0]);
        WNBD_LOG_LOUD("Processing request. Address: %p Tag: 0x%llx Type: %d",
                      Element->Srb, Element->Tag, RequestType);
        // TODO: check if the device supports the requested operation
        switch(RequestType) {
        case WnbdReqTypeRead:
        case WnbdReqTypeWrite:
            Request->RequestType = RequestType;
            Request->RequestHandle = Element->Tag;
            break;
        // TODO: flush/unmap
        default:
            Element->Srb->DataTransferLength = 0;
            Element->Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            CompleteRequest(Device, Element, TRUE);
            InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
            continue;
        }

        PWNBD_PROPERTIES DevProps = &Device->Properties;

        switch(RequestType) {
        case WnbdReqTypeRead:
            Request->Cmd.Read.BlockAddress =
                Element->StartingLbn / DevProps->BlockSize;;
            Request->Cmd.Read.BlockCount =
                Element->ReadLength / DevProps->BlockSize;
            break;
        case WnbdReqTypeWrite:
            Request->Cmd.Write.BlockAddress =
                Element->StartingLbn / DevProps->BlockSize;
            Request->Cmd.Write.BlockCount =
                Element->ReadLength / DevProps->BlockSize;
            if (Element->ReadLength > Command->DataBufferSize) {
                // The user buffer must be at least as large as
                // the specified maximum transfer length.
                Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
                CompleteRequest(Device, Element, TRUE);
                InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
                Status = STATUS_BUFFER_TOO_SMALL;
                goto Exit;
            }

            PVOID SrbBuffer;
            if (StorPortGetSystemAddress(Element->DeviceExtension,
                                         Element->Srb, &SrbBuffer)) {
                Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
                CompleteRequest(Device, Element, TRUE);
                InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
                continue;
            }

            RtlCopyMemory(Buffer, SrbBuffer, Element->ReadLength);
            break;
        }

        ExInterlockedInsertTailList(
            &Device->ReplyListHead,
            &Element->Link, &Device->ReplyListLock);
        InterlockedIncrement64(&Device->Stats.PendingSubmittedIORequests);
        InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
        // We managed to find a supported request, we can now exit the loop
        // and pass it forward.
        break;
    }

Exit:
    if (Mdl) {
        if (BufferLocked) {
            MmUnlockPages(Mdl);
        }
        IoFreeMdl(Mdl);
    }

    if (Device->HardTerminateDevice) {
        Request->RequestType = WnbdReqTypeDisconnect;
        Status = 0;
    }

    return Status;
}

NTSTATUS WnbdHandleResponse(
    PIRP Irp,
    PWNBD_SCSI_DEVICE Device,
    PWNBD_IOCTL_SEND_RSP_COMMAND Command)
{
    PSRB_QUEUE_ELEMENT Element = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    PVOID SrbBuff = NULL, LockedUserBuff = NULL;
    PMDL Mdl = NULL;
    BOOLEAN BufferLocked = FALSE;
    PWNBD_IO_RESPONSE Response = &Command->Response;

    if ((ULONG)Device->Properties.Pid != IoGetRequestorProcessId(Irp)) {
        WNBD_LOG_LOUD("Invalid pid: %d != %u.",
            Device->Properties.Pid, IoGetRequestorProcessId(Irp));
        return STATUS_ACCESS_DENIED;
    }
    if ((ULONG)Device->Properties.Flags.UseNbd) {
        WNBD_LOG_LOUD("Direct IO is not allowed using NBD devices.");
        return STATUS_ACCESS_DENIED;
    }

    PLIST_ENTRY ItemLink, ItemNext;
    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&Device->ReplyListLock, &Irql);
    LIST_FORALL_SAFE(&Device->ReplyListHead, ItemLink, ItemNext) {
        Element = CONTAINING_RECORD(ItemLink, SRB_QUEUE_ELEMENT, Link);
        if (Element->Tag == Response->RequestHandle) {
            RemoveEntryList(&Element->Link);
            break;
        }
        Element = NULL;
    }
    KeReleaseSpinLock(&Device->ReplyListLock, Irql);
    if (!Element) {
        WNBD_LOG_ERROR("Received reply with no matching request tag: 0x%llx",
            Response->RequestHandle);
        return STATUS_NOT_FOUND;
    }

    ULONG StorResult;
    if (!Element->Aborted) {
        // We need to avoid accessing aborted or already completed SRBs.
        PCDB Cdb = (PCDB)&Element->Srb->Cdb;
        WnbdRequestType RequestType = ScsiOpToWnbdReqType(Cdb->AsByte[0]);
        WNBD_LOG_LOUD("Received reply header for %d %p 0x%llx.",
                      RequestType, Element->Srb, Element->Tag);

        if (IsReadSrb(Element->Srb)) {
            StorResult = StorPortGetSystemAddress(Element->DeviceExtension, Element->Srb, &SrbBuff);
            if (STOR_STATUS_SUCCESS != StorResult) {
                WNBD_LOG_ERROR("Could not get SRB %p 0x%llx data buffer. Error: %d.",
                               Element->Srb, Element->Tag, StorResult);
                Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
                Status = STATUS_INTERNAL_ERROR;
                goto Exit;
            }
        }
    } else {
        WNBD_LOG_WARN("Received reply header for aborted request: %p 0x%llx.",
                      Element->Srb, Element->Tag);
    }

    if (!Response->Status.ScsiStatus && IsReadSrb(Element->Srb)) {
        Status = LockUsermodeBuffer(
            Command->DataBuffer, Command->DataBufferSize, FALSE,
            &LockedUserBuff, &Mdl, &BufferLocked);
        if (Status) {
            Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
            goto Exit;
        }

        // TODO: compare data buffer size with the read length
        if (!Element->Aborted) {
            // SrbBuff can't be NULL
#pragma warning(push)
#pragma warning(disable:6387)
            RtlCopyMemory(SrbBuff, LockedUserBuff, Element->ReadLength);
#pragma warning(pop)
        }
    }
    if (Response->Status.ScsiStatus) {
        Element->Srb->DataTransferLength = 0;
        Element->Srb->SrbStatus = SetSrbStatus(Element->Srb, &Response->Status);
    }
    else {
        // TODO: rename ReadLength to DataLength
        Element->Srb->DataTransferLength = Element->ReadLength;
        Element->Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

Exit:
    InterlockedIncrement64(&Device->Stats.TotalReceivedIOReplies);
    InterlockedDecrement64(&Device->Stats.PendingSubmittedIORequests);

    if (Element->Aborted) {
        InterlockedIncrement64(&Device->Stats.CompletedAbortedIORequests);
    }

    if (Mdl) {
        if (BufferLocked) {
            MmUnlockPages(Mdl);
        }
        IoFreeMdl(Mdl);
    }

    if (!Element->Aborted) {
        CompleteRequest(Device, Element, FALSE);
    }
    ExFreePool(Element);

    return Status;
}
