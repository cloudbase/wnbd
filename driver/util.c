/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <berkeley.h>
#include <ksocket.h>
#include "common.h"
#include "debug.h"
#include "nbd_protocol.h"
#include "scsi_driver_extensions.h"
#include "scsi_function.h"
#include "scsi_trace.h"
#include "srb_helper.h"
#include "userspace.h"
#include "util.h"

VOID DrainDeviceQueue(_In_ PWNBD_SCSI_DEVICE Device,
                      _In_ BOOLEAN SubmittedRequests)
{
    WNBD_LOG_LOUD(": Enter");

    PLIST_ENTRY Request;
    PSRB_QUEUE_ELEMENT Element;
    PLIST_ENTRY ListHead;
    PKSPIN_LOCK ListLock;

    if (SubmittedRequests) {
        ListHead = &Device->RequestListHead;
        ListLock = &Device->RequestListLock;
    }
    else {
        ListHead = &Device->ReplyListHead;
        ListLock = &Device->ReplyListLock;
    }

    while ((Request = ExInterlockedRemoveHeadList(ListHead, ListLock)) != NULL) {
        Element = CONTAINING_RECORD(Request, SRB_QUEUE_ELEMENT, Link);
        Element->Srb->DataTransferLength = 0;
        Element->Srb->SrbStatus = SRB_STATUS_ABORTED;
        if (!Element->Aborted) {
            Element->Aborted = 1;
            if (SubmittedRequests)
                InterlockedIncrement64(&Device->Stats.AbortedSubmittedIORequests);
            else
                InterlockedIncrement64(&Device->Stats.AbortedUnsubmittedIORequests);
        }
        CompleteRequest(Device, Element, TRUE);
    }
}

VOID AbortSubmittedRequests(_In_ PWNBD_SCSI_DEVICE Device)
{
    // We're marking submitted requests as aborted and notifying Storport. We only cleaning
    // them up when eventually receiving a reply from the storage backend (needed by NBD,
    // in which case the IO payload is otherwise unknown).
    // TODO: consider cleaning up aborted requests when not using NBD. For NBD, we might
    // have a limit of aborted requests that we keep around.
    WNBD_LOG_LOUD(": Enter");

    PLIST_ENTRY ListHead = &Device->ReplyListHead;
    PKSPIN_LOCK ListLock = &Device->ReplyListLock;

    PSRB_QUEUE_ELEMENT Element;
    PLIST_ENTRY ItemLink, ItemNext;
    KIRQL Irql = { 0 };

    KeAcquireSpinLock(ListLock, &Irql);
    LIST_FORALL_SAFE(ListHead, ItemLink, ItemNext) {
        Element = CONTAINING_RECORD(ItemLink, SRB_QUEUE_ELEMENT, Link);
        Element->Srb->DataTransferLength = 0;
        Element->Srb->SrbStatus = SRB_STATUS_ABORTED;
        if (!Element->Aborted) {
            Element->Aborted = 1;
            InterlockedIncrement64(&Device->Stats.AbortedUnsubmittedIORequests);
        }
        CompleteRequest(Device, Element, FALSE);
    }
    KeReleaseSpinLock(ListLock, Irql);
}

VOID
WnbdCleanupAllDevices(_In_ PWNBD_EXTENSION DeviceExtension)
{
    WNBD_LOG_LOUD(": Enter");
    KeSetEvent(&DeviceExtension->GlobalDeviceRemovalEvent, IO_NO_INCREMENT, FALSE);

    // The rundown protection is a device reference count. We're going to wait
    // for them to be removed after signaling the global device removal event.
    ExWaitForRundownProtectionRelease(&DeviceExtension->RundownProtection);

    KsInitialize();
    KsDestroy();

    WNBD_LOG_LOUD(": Exit");
}

BOOLEAN
WnbdAcquireDevice(_In_ PWNBD_SCSI_DEVICE Device)
{
    // TODO: limit the scopes of critical regions.
    BOOLEAN Acquired = FALSE;

    KeEnterCriticalRegion();
    if (Device)
        Acquired = ExAcquireRundownProtection(&Device->RundownProtection);
    KeLeaveCriticalRegion();
    return Acquired;
}

VOID
WnbdReleaseDevice(_In_ PWNBD_SCSI_DEVICE Device)
{
    KeEnterCriticalRegion();
    if (Device)
        ExReleaseRundownProtection(&Device->RundownProtection);
    KeLeaveCriticalRegion();
}

// The returned device must be subsequently relased using WnbdReleaseDevice,
// if "Acquire" is set. Unacquired device pointers must not be dereferenced.
PWNBD_SCSI_DEVICE
WnbdFindDeviceByAddr(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ BOOLEAN Acquire)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);

    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&DeviceExtension->DeviceListLock, &Irql);
    PWNBD_SCSI_DEVICE Device = NULL;
    for (PLIST_ENTRY Entry = DeviceExtension->DeviceList.Flink;
         Entry != &DeviceExtension->DeviceList; Entry = Entry->Flink)
    {
        Device = (PWNBD_SCSI_DEVICE) CONTAINING_RECORD(Entry, WNBD_SCSI_DEVICE, ListEntry);
        if (Device->Bus == PathId
            && Device->Target == TargetId
            && Device->Lun == Lun)
        {
            if (Acquire && !WnbdAcquireDevice(Device))
                Device = NULL;
            break;
        }
        Device = NULL;
    }
    KeReleaseSpinLock(&DeviceExtension->DeviceListLock, Irql);

    WNBD_LOG_LOUD(": Exit");
    return Device;
}

// The returned device must be subsequently relased using WnbdReleaseDevice,
// if "Acquire" is set. Unacquired device pointers must not be dereferenced.
PWNBD_SCSI_DEVICE
WnbdFindDeviceByConnId(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ UINT64 ConnectionId,
    _In_ BOOLEAN Acquire)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);

    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&DeviceExtension->DeviceListLock, &Irql);
    PWNBD_SCSI_DEVICE Device = NULL;
    for (PLIST_ENTRY Entry = DeviceExtension->DeviceList.Flink;
         Entry != &DeviceExtension->DeviceList; Entry = Entry->Flink)
    {
        Device = (PWNBD_SCSI_DEVICE) CONTAINING_RECORD(Entry, WNBD_SCSI_DEVICE, ListEntry);
        if (Device->ConnectionId == ConnectionId) {
            if (Acquire && !WnbdAcquireDevice(Device))
                Device = NULL;
            break;
        }
        Device = NULL;
    }
    KeReleaseSpinLock(&DeviceExtension->DeviceListLock, Irql);

    WNBD_LOG_LOUD(": Exit");
    return Device;
}

// The returned device must be subsequently relased using WnbdReleaseDevice,
// if "Acquire" is set. Unacquired device pointers must not be dereferenced.
PWNBD_SCSI_DEVICE
WnbdFindDeviceByInstanceName(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ PCHAR InstanceName,
    _In_ BOOLEAN Acquire)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);

    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&DeviceExtension->DeviceListLock, &Irql);
    PWNBD_SCSI_DEVICE Device = NULL;
    for (PLIST_ENTRY Entry = DeviceExtension->DeviceList.Flink;
         Entry != &DeviceExtension->DeviceList; Entry = Entry->Flink)
    {
        Device = (PWNBD_SCSI_DEVICE) CONTAINING_RECORD(Entry, WNBD_SCSI_DEVICE, ListEntry);
        if (!strcmp((CONST CHAR*)&Device->Properties.InstanceName, InstanceName)) {
            if (Acquire && !WnbdAcquireDevice(Device))
                Device = NULL;
            break;
        }
        Device = NULL;
    }
    KeReleaseSpinLock(&DeviceExtension->DeviceListLock, Irql);

    WNBD_LOG_LOUD(": Exit");
    return Device;
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

VOID CloseSocket(_In_ PWNBD_SCSI_DEVICE Device) {
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Device->SocketLock, TRUE);
    if (-1 != Device->SocketToClose) {
        WNBD_LOG_INFO("Closing socket FD: %d", Device->SocketToClose);
        Close(Device->SocketToClose);
    }
    if (-1 != Device->NbdSocket) {
        WNBD_LOG_INFO("Closing socket FD: %d", Device->NbdSocket);
        Close(Device->NbdSocket);
    }

    Device->NbdSocket = -1;
    Device->SocketToClose = -1;
    ExReleaseResourceLite(&Device->SocketLock);
    KeLeaveCriticalRegion();
}

VOID DisconnectSocket(_In_ PWNBD_SCSI_DEVICE Device) {
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Device->SocketLock, TRUE);
    if (-1 != Device->NbdSocket) {
        WNBD_LOG_INFO("Closing socket FD: %d", Device->NbdSocket);
        Device->SocketToClose = Device->NbdSocket;
        // We're setting this to -1 to avoid sending further requests. We're
        // using SocketToClose so that CloseSocket can actually close it.
        // TODO: consider merging those two functions.
        Device->NbdSocket = -1;
        Disconnect(Device->SocketToClose);
    }
    ExReleaseResourceLite(&Device->SocketLock);
    KeLeaveCriticalRegion();
}

VOID
WnbdDisconnectAsync(PWNBD_SCSI_DEVICE Device, BOOLEAN Hard)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Device);

    Device->SoftTerminateDevice = TRUE;
    if (Hard)
        Device->HardTerminateDevice = TRUE;
    KeSetEvent(&Device->TerminateEvent, IO_NO_INCREMENT, FALSE);

    WNBD_LOG_LOUD(": Exit");
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
WnbdDeviceRequestThread(_In_ PVOID Context)
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
WnbdDeviceReplyThread(_In_ PVOID Context)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Context);
    PAGED_CODE();

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE) Context;
    while (!Device->HardTerminateDevice) {
        WnbdProcessDeviceThreadReplies(Device);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

_Use_decl_annotations_
inline BOOLEAN
IsReadSrb(PSCSI_REQUEST_BLOCK Srb)
{
    PCDB Cdb = SrbGetCdb(Srb);
    if(!Cdb) {
        return FALSE;
    }

    switch (Cdb->AsByte[0]) {
    case SCSIOP_READ6:
    case SCSIOP_READ:
    case SCSIOP_READ12:
    case SCSIOP_READ16:
        return TRUE;
    default:
        return FALSE;
    }
}

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

VOID
WnbdProcessDeviceThreadReplies(_In_ PWNBD_SCSI_DEVICE Device)
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

BOOLEAN
ValidateScsiRequest(
    _In_ PWNBD_SCSI_DEVICE Device,
    _In_ PSRB_QUEUE_ELEMENT Element)
{
    PCDB Cdb = (PCDB)&Element->Srb->Cdb;
    int ScsiOp = Cdb->AsByte[0];
    int NbdReqType = ScsiOpToNbdReqType(ScsiOp);
    PWNBD_PROPERTIES DevProps = &Device->Properties;

    switch (NbdReqType) {
    case NBD_CMD_TRIM:
    case NBD_CMD_WRITE:
    case NBD_CMD_FLUSH:
        if (DevProps->Flags.ReadOnly) {
            WNBD_LOG_LOUD(
                "Write, flush or trim requested on a read-only disk.");
            return FALSE;
        }
    case NBD_CMD_READ:
        break;
    default:
        WNBD_LOG_LOUD("Unsupported SCSI operation: %d.", ScsiOp);
        return FALSE;
    }

    if (NbdReqType == NBD_CMD_TRIM && !DevProps->Flags.UnmapSupported) {
        WNBD_LOG_LOUD("The NBD server doesn't accept TRIM/UNMAP.");
        return FALSE;
    }
    if (NbdReqType == NBD_CMD_FLUSH && !DevProps->Flags.FlushSupported) {
        WNBD_LOG_LOUD("The NBD server doesn't accept flush requests");
        return FALSE;
    }

    return TRUE;
}

UCHAR SetSrbStatus(PVOID Srb, PWNBD_STATUS Status)
{
    UCHAR SrbStatus = SRB_STATUS_ERROR;
    PSENSE_DATA SenseInfoBuffer = SrbGetSenseInfoBuffer(Srb);
    UCHAR SenseInfoBufferLength = SrbGetSenseInfoBufferLength(Srb);

    if (SenseInfoBuffer && sizeof(SENSE_DATA) <= SenseInfoBufferLength &&
        !(SrbGetSrbFlags(Srb) & SRB_FLAGS_DISABLE_AUTOSENSE))
    {
        RtlZeroMemory(SenseInfoBuffer, SenseInfoBufferLength);
        SenseInfoBuffer->ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
        SenseInfoBuffer->SenseKey = Status->SenseKey;
        SenseInfoBuffer->AdditionalSenseCode = Status->ASC;
        SenseInfoBuffer->AdditionalSenseCodeQualifier = Status->ASCQ;
        SenseInfoBuffer->AdditionalSenseLength = sizeof(SENSE_DATA) -
            RTL_SIZEOF_THROUGH_FIELD(SENSE_DATA, AdditionalSenseLength);
        if (Status->InformationValid)
        {
            // TODO: should we use REVERSE_BYTES_8? What's the expected endianness?
            (*(PUINT64)SenseInfoBuffer->Information) = Status->Information;
            SenseInfoBuffer->Valid = 1;
        }

        SrbSetScsiStatus(Srb, SCSISTAT_CHECK_CONDITION);
        SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
    }

    return SrbStatus;
}

VOID CompleteRequest(
    _In_ PWNBD_SCSI_DEVICE Device,
    _In_ PSRB_QUEUE_ELEMENT Element,
    _In_ BOOLEAN FreeElement)
{
    // We must be very careful not to complete the same SRB twice in order
    // to avoid crashes.
    if (!InterlockedExchange8((CHAR*)&Element->Completed, TRUE)) {
        WNBD_LOG_LOUD(
            "Notifying StorPort of completion of %p 0x%llx status: 0x%x(%s)",
            Element->Srb, Element->Tag, Element->Srb->SrbStatus,
            WnbdToStringSrbStatus(Element->Srb->SrbStatus));
        StorPortNotification(RequestComplete, Element->DeviceExtension, Element->Srb);
        InterlockedDecrement64(&Device->Stats.OutstandingIOCount);
    }

    if (FreeElement) {
        ExFreePool(Element);
    }
}
