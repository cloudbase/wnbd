/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <berkeley.h>
#include "common.h"
#include "debug.h"
#include "rbd_protocol.h"
#include "scsi_driver_extensions.h"
#include "scsi_function.h"
#include "scsi_trace.h"
#include "srb_helper.h"
#include "userspace.h"
#include "util.h"

VOID
WnbdDeleteScsiInformation(_In_ PVOID ScsiInformation)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(ScsiInformation);
    PSCSI_DEVICE_INFORMATION ScsiInfo = (PSCSI_DEVICE_INFORMATION)ScsiInformation;
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&ScsiInfo->GlobalInformation->ConnectionMutex, TRUE);

    PLIST_ENTRY Request;
    PSRB_QUEUE_ELEMENT Element;

    while ((Request = ExInterlockedRemoveHeadList(&ScsiInfo->RequestListHead, &ScsiInfo->RequestListLock)) != NULL) {
        Element = CONTAINING_RECORD(Request, SRB_QUEUE_ELEMENT, Link);
        Element->Srb->DataTransferLength = 0;
        Element->Srb->SrbStatus = SRB_STATUS_ABORTED;
        PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE)ScsiInfo->Device;
        InterlockedDecrement(&Device->OutstandingIoCount);
        ExFreePool(Element);
    }

    while ((Request = ExInterlockedRemoveHeadList(&ScsiInfo->ReplyListHead, &ScsiInfo->ReplyListLock)) != NULL) {
        Element = CONTAINING_RECORD(Request, SRB_QUEUE_ELEMENT, Link);
        Element->Srb->DataTransferLength = 0;
        Element->Srb->SrbStatus = SRB_STATUS_ABORTED;
        PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE)ScsiInfo->Device;
        InterlockedDecrement(&Device->OutstandingIoCount);
        WNBD_LOG_INFO("Notifying StorPort of completion of %p status: 0x%x(%s)",
            Element->Srb, Element->Srb->SrbStatus, WnbdToStringSrbStatus(Element->Srb->SrbStatus));
        StorPortNotification(RequestComplete, Element->DeviceExtension, Element->Srb);
        ExFreePool(Element);
    }

    if(ScsiInfo->InquiryData) {
        ExFreePool(ScsiInfo->InquiryData);
        ScsiInfo->InquiryData = NULL;
    }

    DisconnectConnection(ScsiInfo);

    ExDeleteResourceLite(&ScsiInfo->SocketLock);

    if(ScsiInfo->UserEntry) {
        ExFreePool(ScsiInfo->UserEntry);
        ScsiInfo->UserEntry = NULL;
    }

    if (ScsiInfo->ReadPreallocatedBuffer) {
        ExFreePool(ScsiInfo->ReadPreallocatedBuffer);
        ScsiInfo->ReadPreallocatedBuffer = NULL;
    }

    if (ScsiInfo->WritePreallocatedBuffer) {
        ExFreePool(ScsiInfo->WritePreallocatedBuffer);
        ScsiInfo->WritePreallocatedBuffer = NULL;
    }

    ExReleaseResourceLite(&ScsiInfo->GlobalInformation->ConnectionMutex);
    KeLeaveCriticalRegion();

    ExFreePool(ScsiInfo);
    ScsiInfo = NULL;

    WNBD_LOG_LOUD(": Exit");
}

VOID
WnbdDeleteDevices(_In_ PWNBD_EXTENSION Ext,
                  _In_ BOOLEAN All)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Ext);
    PWNBD_SCSI_DEVICE Device = NULL;
    PLIST_ENTRY Link, Next;
    if (NULL == Ext->GlobalInformation) {
        return;
    }
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Ext->DeviceResourceLock, TRUE);
    ExAcquireResourceExclusiveLite(&((PGLOBAL_INFORMATION)Ext->GlobalInformation)->ConnectionMutex, TRUE);
    LIST_FORALL_SAFE(&Ext->DeviceList, Link, Next) {
        Device = (PWNBD_SCSI_DEVICE)CONTAINING_RECORD(Link, WNBD_SCSI_DEVICE, ListEntry);
        if (Device->ReportedMissing || All) {
            WNBD_LOG_INFO("Deleting device %p with %d:%d:%d",
                Device, Device->PathId, Device->TargetId, Device->Lun);
            PSCSI_DEVICE_INFORMATION Info = (PSCSI_DEVICE_INFORMATION)Device->ScsiDeviceExtension;
            WnbdDeleteConnection((PGLOBAL_INFORMATION)Ext->GlobalInformation,
                                 Info->UserEntry->Properties.InstanceName);
            RemoveEntryList(&Device->ListEntry);
            WnbdDeleteScsiInformation(Device->ScsiDeviceExtension);
            ExFreePool(Device);
            Device = NULL;
            if (FALSE == All) {
                break;
            }
        }
    }
    ExReleaseResourceLite(&((PGLOBAL_INFORMATION)Ext->GlobalInformation)->ConnectionMutex);
    ExReleaseResourceLite(&Ext->DeviceResourceLock);
    KeLeaveCriticalRegion();

    WNBD_LOG_INFO("Request to exit DeleteDevicesThreadStart");

    WNBD_LOG_LOUD(": Exit");
}

VOID
WnbdDeviceCleanerThread(_In_ PVOID Context)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Context);
    PWNBD_EXTENSION Ext = (PWNBD_EXTENSION)Context;

    while (TRUE) {
        KeWaitForSingleObject(&Ext->DeviceCleanerEvent, Executive, KernelMode, FALSE, NULL);

        if (Ext->StopDeviceCleaner) {
            WNBD_LOG_INFO("Terminating Device Cleaner");
            WnbdDeleteDevices(Ext, TRUE);
            break;
        }

        WnbdDeleteDevices(Ext, FALSE);
    }

    WNBD_LOG_LOUD(": Exit");

    (void)PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID
WnbdReportMissingDevice(_In_ PWNBD_EXTENSION DeviceExtension,
                        _In_ PWNBD_SCSI_DEVICE Device,
                        _In_ PWNBD_LU_EXTENSION LuExtension)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Device);
    ASSERT(DeviceExtension);
    ASSERT(LuExtension);

    if (!Device->Missing) {
        LuExtension->WnbdScsiDevice = Device;
    } else {
        if (!Device->ReportedMissing) {
            WNBD_LOG_INFO(": Scheduling %p to be deleted and waking DeviceCleaner",
                          Device);
            Device->ReportedMissing = TRUE;
            KeSetEvent(&DeviceExtension->DeviceCleanerEvent, IO_DISK_INCREMENT, FALSE);
        }
        Device = NULL;
    }

    WNBD_LOG_LOUD(": Exit");
}

PWNBD_SCSI_DEVICE
WnbdFindDevice(_In_ PWNBD_LU_EXTENSION LuExtension,
               _In_ PWNBD_EXTENSION DeviceExtension,
               _In_ UCHAR PathId,
               _In_ UCHAR TargetId,
               _In_ UCHAR Lun)
{
    WNBD_LOG_LOUD(": Entered");
    ASSERT(LuExtension);
    ASSERT(DeviceExtension);

    PWNBD_SCSI_DEVICE Device = NULL;

    for (PLIST_ENTRY Entry = DeviceExtension->DeviceList.Flink;
        Entry != &DeviceExtension->DeviceList; Entry = Entry->Flink) {

        Device = (PWNBD_SCSI_DEVICE) CONTAINING_RECORD(Entry, WNBD_SCSI_DEVICE, ListEntry);

        if (Device->PathId == PathId
            && Device->TargetId == TargetId
            && Device->Lun == Lun) {
            WnbdReportMissingDevice(DeviceExtension, Device, LuExtension);
            break;
        }
        Device = NULL;
    }

    WNBD_LOG_LOUD(": Exit");

    return Device;
}

PWNBD_SCSI_DEVICE
WnbdFindDeviceEx(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);

    PWNBD_SCSI_DEVICE Device = NULL;
    PWNBD_LU_EXTENSION LuExtension;
    KIRQL Irql;
    KSPIN_LOCK DevLock = DeviceExtension->DeviceListLock;
    KeAcquireSpinLock(&DevLock, &Irql);

    LuExtension = (PWNBD_LU_EXTENSION)
        StorPortGetLogicalUnit(DeviceExtension, PathId, TargetId, Lun);

    if (!LuExtension) {
        WNBD_LOG_ERROR(": Unable to get LUN extension for device PathId: %d TargetId: %d LUN: %d",
            PathId, TargetId, Lun);
        goto Exit;
    }

    Device = WnbdFindDevice(LuExtension, DeviceExtension,
                            PathId, TargetId, Lun);
    if (!Device) {
        WNBD_LOG_INFO("Could not find device PathId: %d TargetId: %d LUN: %d",
            PathId, TargetId, Lun);
        goto Exit;
    }

    if (!Device->ScsiDeviceExtension) {
        WNBD_LOG_ERROR("%p has no ScsiDeviceExtension. PathId = %d. TargetId = %d. LUN = %d",
            Device, PathId, TargetId, Lun);
        goto Exit;
    }

Exit:
    KeReleaseSpinLock(&DevLock, Irql);
    return Device;
}

NTSTATUS
WnbdRequestWrite(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation,
                 _In_ PSRB_QUEUE_ELEMENT Element,
                 _In_ DWORD NbdTransmissionFlags)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceInformation);
    ASSERT(Element);
    ULONG StorResult;
    PVOID Buffer;
    NTSTATUS Status = STATUS_SUCCESS;

    StorResult = StorPortGetSystemAddress(Element->DeviceExtension, Element->Srb, &Buffer);
    if (STOR_STATUS_SUCCESS != StorResult) {
        Status = SRB_STATUS_INTERNAL_ERROR;
    } else {
        NbdWriteStat(DeviceInformation->Socket,
                     Element->StartingLbn,
                     Element->ReadLength,
                     &Status,
                     Buffer,
                     &DeviceInformation->WritePreallocatedBuffer,
                     &DeviceInformation->WritePreallocatedBufferLength,
                     Element->Tag,
                     NbdTransmissionFlags);
    }

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

VOID DisconnectConnection(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation) {
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &DeviceInformation->SocketLock, TRUE);
    if (-1 != DeviceInformation->SocketToClose) {
        WNBD_LOG_INFO("Closing socket FD: %d", DeviceInformation->Socket);
        if (-1 != DeviceInformation->Socket) {
            Close(DeviceInformation->Socket);
        } else {
            Close(DeviceInformation->SocketToClose);
        }
        DeviceInformation->Socket = -1;
        DeviceInformation->SocketToClose = -1;
        DeviceInformation->Device->Missing = TRUE;
    }
    ExReleaseResourceLite(&DeviceInformation->SocketLock);
    KeLeaveCriticalRegion();
}

VOID CloseConnection(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation) {
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &DeviceInformation->SocketLock, TRUE);
    // TODO: is SocketToClose actually necessary? We're closing both
    // SocketToClose and Socket. This logic seems very convoluted.
    // Also, "Close" is calling the socket "Disconnect" function and
    // Disconnect is actually calling "Close" ?! 
    DeviceInformation->SocketToClose = -1;
    if (-1 != DeviceInformation->Socket) {
        WNBD_LOG_INFO("Closing socket FD: %d", DeviceInformation->Socket);
        DeviceInformation->SocketToClose = DeviceInformation->Socket;
        Disconnect(DeviceInformation->Socket);
        DeviceInformation->Socket = -1;
        if (DeviceInformation->Device) {
            DeviceInformation->Device->Missing = TRUE;
        }
    }
    ExReleaseResourceLite(&DeviceInformation->SocketLock);
    KeLeaveCriticalRegion();
}

VOID
WnbdProcessDeviceThreadRequests(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceInformation);

    PLIST_ENTRY Request;
    PSRB_QUEUE_ELEMENT Element;
    NTSTATUS Status = STATUS_SUCCESS;
    static UINT64 RequestTag = 0;

    while ((Request = ExInterlockedRemoveHeadList(
            &DeviceInformation->RequestListHead,
            &DeviceInformation->RequestListLock)) != NULL) {
        RequestTag += 1;
        Element = CONTAINING_RECORD(Request, SRB_QUEUE_ELEMENT, Link);
        Element->Tag = RequestTag;
        Element->Srb->DataTransferLength = 0;
        PCDB Cdb = (PCDB)&Element->Srb->Cdb;
        WNBD_LOG_INFO("Processing request. Address: %p Tag: 0x%llx",
                      Status, Element->Srb, Element->Tag);
        int NbdReqType = ScsiOpToNbdReqType(Cdb->AsByte[0]);

        if(!ValidateScsiRequest(DeviceInformation, Element)) {
            Element->Srb->DataTransferLength = 0;
            Element->Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            StorPortNotification(RequestComplete,
                                 Element->DeviceExtension,
                                 Element->Srb);
            ExFreePool(Element);
            InterlockedDecrement64(&DeviceInformation->Stats.UnsubmittedIORequests);
            continue;
        }

        DWORD NbdTransmissionFlags = 0;
        PWNBD_PROPERTIES DevProps = &DeviceInformation->UserEntry->Properties;
        switch (NbdReqType) {
        case NBD_CMD_WRITE:
        case NBD_CMD_TRIM:
            if (Element->FUA && DevProps->Flags.UnmapSupported) {
                NbdTransmissionFlags |= NBD_CMD_FLAG_FUA;
            }
        case NBD_CMD_READ:
        case NBD_CMD_FLUSH:
            if(DeviceInformation->SoftTerminateDevice ||
                    DeviceInformation->HardTerminateDevice) {
                return;
            }
            ExInterlockedInsertTailList(
                &DeviceInformation->ReplyListHead,
                &Element->Link, &DeviceInformation->ReplyListLock);
            WNBD_LOG_LOUD("Sending %s request. Address: %p Tag: 0x%llx. FUA: %d",
                          NbdRequestTypeStr(NbdReqType), Element->Srb, Element->Tag,
                          Element->FUA);

            if(NbdReqType == NBD_CMD_WRITE){
                Status = WnbdRequestWrite(DeviceInformation, Element,
                                          NbdTransmissionFlags);
            } else {
                NbdRequest(
                    DeviceInformation->Socket,
                    Element->StartingLbn,
                    Element->ReadLength,
                    &Status,
                    Element->Tag,
                    NbdReqType | NbdTransmissionFlags);
            }

            InterlockedDecrement64(&DeviceInformation->Stats.UnsubmittedIORequests);
            InterlockedIncrement64(&DeviceInformation->Stats.TotalSubmittedIORequests);
            InterlockedIncrement64(&DeviceInformation->Stats.PendingSubmittedIORequests);
            break;
        }

        if (Status) {
            WNBD_LOG_INFO("FD failed with: %x. Address: %p Tag: 0x%llx",
                          Status, Element->Srb, Element->Tag);
            if (STATUS_CONNECTION_RESET == Status ||
                STATUS_CONNECTION_DISCONNECTED == Status ||
                STATUS_CONNECTION_ABORTED == Status) {
                CloseConnection(DeviceInformation);
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
    
    PSCSI_DEVICE_INFORMATION DeviceInformation;
    PAGED_CODE();

    DeviceInformation = (PSCSI_DEVICE_INFORMATION) Context;

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    while (TRUE) {
        // TODO: should this be moved in the WnbdProcessDeviceThreadRequests loop?
        KeWaitForSingleObject(&DeviceInformation->DeviceEvent, Executive, KernelMode, FALSE, NULL);

        if (DeviceInformation->HardTerminateDevice) {
            WNBD_LOG_INFO("Hard terminate thread: %p", DeviceInformation);
            PsTerminateSystemThread(STATUS_SUCCESS);
        }

        WnbdProcessDeviceThreadRequests(DeviceInformation);

        // TODO: should we continue processing requests on soft termination until
        // we drain our queues?
        if (DeviceInformation->SoftTerminateDevice) {
            WNBD_LOG_INFO("Soft terminate thread: %p", DeviceInformation);
            PsTerminateSystemThread(STATUS_SUCCESS);
        }
    }
}

VOID
WnbdDeviceReplyThread(_In_ PVOID Context)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Context);

    PSCSI_DEVICE_INFORMATION DeviceInformation;
    PAGED_CODE();

    DeviceInformation = (PSCSI_DEVICE_INFORMATION) Context;

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    while (TRUE) {

        if (DeviceInformation->SoftTerminateDevice) {
            WNBD_LOG_INFO("Soft terminate thread: %p", DeviceInformation);
            PsTerminateSystemThread(STATUS_SUCCESS);
        }

        WnbdProcessDeviceThreadReplies(DeviceInformation);
    }
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
WnbdProcessDeviceThreadReplies(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceInformation);

    PSRB_QUEUE_ELEMENT Element = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    NBD_REPLY Reply = { 0 };
    PVOID SrbBuff = NULL, TempBuff = NULL;
    NTSTATUS error = STATUS_SUCCESS;

    Status = NbdReadReply(DeviceInformation->Socket, &Reply);
    if (Status) {
        CloseConnection(DeviceInformation);
        // Sleep for a bit to avoid a lazy poll here since the connection
        // could already be closed by the time the device is actually removed.
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = (-1 * 100 * 10000);
        KeDelayExecutionThread(KernelMode, FALSE, &Timeout);
        return;
    }
    PLIST_ENTRY ItemLink, ItemNext;
    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&DeviceInformation->ReplyListLock, &Irql);
    LIST_FORALL_SAFE(&DeviceInformation->ReplyListHead, ItemLink, ItemNext) {
        Element = CONTAINING_RECORD(ItemLink, SRB_QUEUE_ELEMENT, Link);
        if (Element->Tag == Reply.Handle) {
            /* Remove the element from the list once found*/
            RemoveEntryList(&Element->Link);
            break;
        }
        Element = NULL;
    }
    KeReleaseSpinLock(&DeviceInformation->ReplyListLock, Irql);
    if(!Element) {
        WNBD_LOG_ERROR("Received reply with no matching request tag: 0x%llx",
            Reply.Handle);
        CloseConnection(DeviceInformation);
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
                CloseConnection(DeviceInformation);
                goto Exit;
            }
        }
    } else {
        WNBD_LOG_WARN("Received reply header for aborted request: %p 0x%llx.",
                      Element->Srb, Element->Tag);
    }

    if(!Reply.Error && IsReadSrb(Element->Srb)) {
        if (Element->ReadLength > DeviceInformation->ReadPreallocatedBufferLength) {
            TempBuff = NbdMalloc(Element->ReadLength);
            if (!TempBuff) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                CloseConnection(DeviceInformation);
                goto Exit;
            }
            DeviceInformation->ReadPreallocatedBufferLength = Element->ReadLength;
            ExFreePool(DeviceInformation->ReadPreallocatedBuffer);
            DeviceInformation->ReadPreallocatedBuffer = TempBuff;
        } else {
            TempBuff = DeviceInformation->ReadPreallocatedBuffer;
        }

        if (-1 == RbdReadExact(DeviceInformation->Socket, TempBuff, Element->ReadLength, &error)) {
            WNBD_LOG_ERROR("Failed receiving reply %p 0x%llx. Error: %d",
                           Element->Srb, Element->Tag, error);
            Element->Srb->DataTransferLength = 0;
            Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
            CloseConnection(DeviceInformation);
            goto Exit;
        } else {
            if(!Element->Aborted) {
                RtlCopyMemory(SrbBuff, TempBuff, Element->ReadLength);
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

    InterlockedIncrement64(&DeviceInformation->Stats.TotalReceivedIOReplies);
    InterlockedDecrement64(&DeviceInformation->Stats.PendingSubmittedIORequests);

    if(Element->Aborted) {
        InterlockedIncrement64(&DeviceInformation->Stats.CompletedAbortedIORequests);
    }
    else {
        WNBD_LOG_LOUD("Successfully completed request %p 0x%llx.",
                      Element->Srb, Element->Tag);
    }

Exit:
    InterlockedDecrement(&DeviceInformation->Device->OutstandingIoCount);
    if (Element) {
        if(!Element->Aborted) {
            WNBD_LOG_INFO("Notifying StorPort of completion of %p status: 0x%x(%s)",
                Element->Srb, Element->Srb->SrbStatus,
                WnbdToStringSrbStatus(Element->Srb->SrbStatus));
            StorPortNotification(RequestComplete, Element->DeviceExtension,
                                 Element->Srb);
        }
        ExFreePool(Element);
    }
}

BOOLEAN
ValidateScsiRequest(
    _In_ PSCSI_DEVICE_INFORMATION DeviceInformation,
    _In_ PSRB_QUEUE_ELEMENT Element)
{
    PCDB Cdb = (PCDB)&Element->Srb->Cdb;
    int ScsiOp = Cdb->AsByte[0];
    int NbdReqType = ScsiOpToNbdReqType(ScsiOp);
    PWNBD_PROPERTIES DevProps = &DeviceInformation->UserEntry->Properties;

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
