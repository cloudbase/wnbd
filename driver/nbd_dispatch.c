/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

// This module handles IO request dispatching and reply handling over NBD

#include "nbd_dispatch.h"
#include "util.h"
#include "debug.h"

_Use_decl_annotations_
inline int
ScsiOpToNbdReqType(int ScsiOp)
{
    switch (ScsiOp) {
    case SCSIOP_READ6:
    case SCSIOP_READ:
    case SCSIOP_READ12:
    case SCSIOP_READ16:
        return NBD_CMD_READ;
    case SCSIOP_WRITE6:
    case SCSIOP_WRITE:
    case SCSIOP_WRITE12:
    case SCSIOP_WRITE16:
        return NBD_CMD_WRITE;
    case SCSIOP_UNMAP:
        return NBD_CMD_TRIM;
    case SCSIOP_SYNCHRONIZE_CACHE:
    case SCSIOP_SYNCHRONIZE_CACHE16:
        return NBD_CMD_FLUSH;
    default:
        return -1;
    }
}

NTSTATUS
WnbdRequestWrite(_In_ PWNBD_SCSI_DEVICE Device,
                 _In_ PSRB_QUEUE_ELEMENT Element,
                 _In_ DWORD NbdTransmissionFlags)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Device);
    ASSERT(Element);
    ULONG StorResult;
    PVOID Buffer;
    NTSTATUS Status = STATUS_SUCCESS;

    StorResult = StorPortGetSystemAddress(Element->DeviceExtension, Element->Srb, &Buffer);
    if (STOR_STATUS_SUCCESS != StorResult) {
        Status = SRB_STATUS_INTERNAL_ERROR;
    } else {
        NbdWriteStat(Device->NbdSocket,
                     Element->StartingLbn,
                     Element->ReadLength,
                     &Status,
                     Buffer,
                     &Device->WritePreallocatedBuffer,
                     &Device->WritePreallocatedBufferLength,
                     Element->Tag,
                     NbdTransmissionFlags);
    }

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

VOID
WnbdProcessDeviceThreadRequests(_In_ PWNBD_SCSI_DEVICE Device)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Device);

    PLIST_ENTRY Request;
    PSRB_QUEUE_ELEMENT Element;
    NTSTATUS Status = STATUS_SUCCESS;
    static UINT64 RequestTag = 0;

    while ((Request = ExInterlockedRemoveHeadList(
            &Device->RequestListHead,
            &Device->RequestListLock)) != NULL) {
        RequestTag += 1;
        Element = CONTAINING_RECORD(Request, SRB_QUEUE_ELEMENT, Link);
        Element->Tag = RequestTag;
        Element->Srb->DataTransferLength = 0;
        PCDB Cdb = (PCDB)&Element->Srb->Cdb;
        WNBD_LOG_INFO("Processing request. Address: %p Tag: 0x%llx",
                      Status, Element->Srb, Element->Tag);
        int NbdReqType = ScsiOpToNbdReqType(Cdb->AsByte[0]);

        if(!ValidateScsiRequest(Device, Element)) {
            Element->Srb->DataTransferLength = 0;
            Element->Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            CompleteRequest(Device, Element, TRUE);
            InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
            continue;
        }

        DWORD NbdTransmissionFlags = 0;
        PWNBD_PROPERTIES DevProps = &Device->Properties;
        switch (NbdReqType) {
        case NBD_CMD_WRITE:
        case NBD_CMD_TRIM:
            if (Element->FUA && DevProps->Flags.UnmapSupported) {
                NbdTransmissionFlags |= NBD_CMD_FLAG_FUA;
            }
        case NBD_CMD_READ:
        case NBD_CMD_FLUSH:
            if (Device->HardTerminateDevice) {
                return;
            }
            ExInterlockedInsertTailList(
                &Device->ReplyListHead,
                &Element->Link, &Device->ReplyListLock);
            WNBD_LOG_LOUD("Sending %s request. Address: %p Tag: 0x%llx. FUA: %d",
                          NbdRequestTypeStr(NbdReqType), Element->Srb, Element->Tag,
                          Element->FUA);

            if(NbdReqType == NBD_CMD_WRITE){
                Status = WnbdRequestWrite(Device, Element,
                                          NbdTransmissionFlags);
            } else {
                NbdRequest(
                    Device->NbdSocket,
                    Element->StartingLbn,
                    Element->ReadLength,
                    &Status,
                    Element->Tag,
                    NbdReqType | NbdTransmissionFlags);
            }

            InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
            InterlockedIncrement64(&Device->Stats.TotalSubmittedIORequests);
            InterlockedIncrement64(&Device->Stats.PendingSubmittedIORequests);
            break;
        }

        if (Status) {
            WNBD_LOG_INFO("FD failed with: %x. Address: %p Tag: 0x%llx",
                          Status, Element->Srb, Element->Tag);
            if (STATUS_CONNECTION_RESET == Status ||
                STATUS_CONNECTION_DISCONNECTED == Status ||
                STATUS_CONNECTION_ABORTED == Status) {
                WnbdDisconnectAsync(Device, TRUE);
            }
        }
    }

    WNBD_LOG_LOUD(": Exit");
}

VOID
NbdDeviceRequestThread(_In_ PVOID Context)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Context);
    PAGED_CODE();

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE) Context;
    while (!Device->HardTerminateDevice) {
        PVOID WaitObjects[2];
        WaitObjects[0] = &Device->DeviceEvent;
        WaitObjects[1] = &Device->TerminateEvent;
        NTSTATUS WaitResult = KeWaitForMultipleObjects(
            2, WaitObjects, WaitAny, Executive, KernelMode,
            TRUE, NULL, NULL);
        if (STATUS_WAIT_1 == WaitResult || Device->HardTerminateDevice)
            break;

        if (STATUS_ALERTED == WaitResult) {
            // This happens when the calling thread is terminating.
            // TODO: ensure that we haven't been alerted for some other reason.
            WNBD_LOG_INFO("Wait alterted, terminating.");
            KeSetEvent(&Device->TerminateEvent, IO_NO_INCREMENT, FALSE);
            break;
        }

        WnbdProcessDeviceThreadRequests(Device);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID
NbdDeviceReplyThread(_In_ PVOID Context)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Context);
    PAGED_CODE();

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE) Context;
    while (!Device->HardTerminateDevice) {
        NbdProcessDeviceThreadReplies(Device);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID
NbdProcessDeviceThreadReplies(_In_ PWNBD_SCSI_DEVICE Device)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Device);

    PSRB_QUEUE_ELEMENT Element = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    NBD_REPLY Reply = { 0 };
    PVOID SrbBuff = NULL, TempBuff = NULL;
    NTSTATUS error = STATUS_SUCCESS;

    Status = NbdReadReply(Device->NbdSocket, &Reply);
    if (Status) {
        WnbdDisconnectAsync(Device, TRUE);
        return;
    }
    PLIST_ENTRY ItemLink, ItemNext;
    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&Device->ReplyListLock, &Irql);
    LIST_FORALL_SAFE(&Device->ReplyListHead, ItemLink, ItemNext) {
        Element = CONTAINING_RECORD(ItemLink, SRB_QUEUE_ELEMENT, Link);
        if (Element->Tag == Reply.Handle) {
            /* Remove the element from the list once found*/
            RemoveEntryList(&Element->Link);
            break;
        }
        Element = NULL;
    }
    KeReleaseSpinLock(&Device->ReplyListLock, Irql);
    if(!Element) {
        WNBD_LOG_ERROR("Received reply with no matching request tag: 0x%llx",
            Reply.Handle);
        WnbdDisconnectAsync(Device, TRUE);
        goto Exit;
    }

    ULONG StorResult;
    if(!Element->Aborted) {
        // We need to avoid accessing aborted or already completed SRBs.
        PCDB Cdb = (PCDB)&Element->Srb->Cdb;
        int NbdReqType = ScsiOpToNbdReqType(Cdb->AsByte[0]);
        WNBD_LOG_LOUD("Received reply header for %s %p 0x%llx.",
                      NbdRequestTypeStr(NbdReqType), Element->Srb, Element->Tag);

        if(IsReadSrb(Element->Srb)) {
            StorResult = StorPortGetSystemAddress(Element->DeviceExtension, Element->Srb, &SrbBuff);
            if (STOR_STATUS_SUCCESS != StorResult) {
                WNBD_LOG_ERROR("Could not get SRB %p 0x%llx data buffer. Error: %d.",
                               Element->Srb, Element->Tag, error);
                Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
                WnbdDisconnectAsync(Device, TRUE);
                goto Exit;
            }
        }
    } else {
        WNBD_LOG_WARN("Received reply header for aborted request: %p 0x%llx.",
                      Element->Srb, Element->Tag);
    }

    if(!Reply.Error && IsReadSrb(Element->Srb)) {
        if (Element->ReadLength > Device->ReadPreallocatedBufferLength) {
            TempBuff = NbdMalloc(Element->ReadLength);
            if (!TempBuff) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                WnbdDisconnectAsync(Device, TRUE);
                goto Exit;
            }
            Device->ReadPreallocatedBufferLength = Element->ReadLength;
            ExFreePool(Device->ReadPreallocatedBuffer);
            Device->ReadPreallocatedBuffer = TempBuff;
        } else {
            TempBuff = Device->ReadPreallocatedBuffer;
        }

        if (-1 == NbdReadExact(Device->NbdSocket, TempBuff, Element->ReadLength, &error)) {
            WNBD_LOG_ERROR("Failed receiving reply %p 0x%llx. Error: %d",
                           Element->Srb, Element->Tag, error);
            Element->Srb->DataTransferLength = 0;
            Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
            WnbdDisconnectAsync(Device, TRUE);
            goto Exit;
        } else {
            if(!Element->Aborted) {
                // SrbBuff can't be NULL
#pragma warning(push)
#pragma warning(disable:6387)
                RtlCopyMemory(SrbBuff, TempBuff, Element->ReadLength);
#pragma warning(pop)
            }
        }
    }
    if (Reply.Error) {
        // TODO: do we care about the actual error?
        WNBD_LOG_INFO("NBD reply contains error: %llu", Reply.Error);
        Element->Srb->DataTransferLength = 0;
        Element->Srb->SrbStatus = SRB_STATUS_ABORTED;
    }
    else {
        // TODO: rename ReadLength to DataLength
        Element->Srb->DataTransferLength = Element->ReadLength;
        Element->Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

    InterlockedIncrement64(&Device->Stats.TotalReceivedIOReplies);
    InterlockedDecrement64(&Device->Stats.PendingSubmittedIORequests);

    if(Element->Aborted) {
        InterlockedIncrement64(&Device->Stats.CompletedAbortedIORequests);
    }
    else {
        WNBD_LOG_LOUD("Successfully completed request %p 0x%llx.",
                      Element->Srb, Element->Tag);
    }

Exit:
    if (Element) {
        CompleteRequest(Device, Element, TRUE);
    }
}
