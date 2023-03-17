/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

// This module handles IO request dispatching and reply handling over
// the IOCTL interface.

#include <ntifs.h>

#include "wnbd_dispatch.h"
#include "util.h"
#include "srb_helper.h"
#include "debug.h"
#include "scsi_function.h"
#include "scsi_trace.h"

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
                       "Exception: 0x%x, buffer: %p, size: %d.",
                       Status, Buffer, BufferSize);
    }

    return Status;
}

NTSTATUS WnbdDispatchRequest(
    PIRP Irp,
    PWNBD_DISK_DEVICE Device,
    PWNBD_IOCTL_FETCH_REQ_COMMAND Command)
{
    PVOID Buffer;
    NTSTATUS Status = 0;
    PWNBD_IO_REQUEST Request = &Command->Request;
    PMDL Mdl = NULL;
    BOOLEAN BufferLocked = FALSE;

    if ((ULONG)Device->Properties.Pid != IoGetRequestorProcessId(Irp)) {
        WNBD_LOG_DEBUG("Invalid pid: %d != %u.",
            Device->Properties.Pid, IoGetRequestorProcessId(Irp));
        return STATUS_ACCESS_DENIED;
    }
    if ((ULONG)Device->Properties.Flags.UseNbd) {
        WNBD_LOG_DEBUG("Direct IO is not allowed using NBD devices.");
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
    while (!Device->HardRemoveDevice) {
        PVOID WaitObjects[2];
        WaitObjects[0] = &Device->DeviceEvent;
        WaitObjects[1] = &Device->DeviceRemovalEvent;
        NTSTATUS WaitResult = KeWaitForMultipleObjects(
            2, WaitObjects, WaitAny, Executive, KernelMode,
            TRUE, NULL, NULL);
        if (STATUS_WAIT_1  == WaitResult)
            break;

        if (STATUS_ALERTED == WaitResult) {
            // This happens when the calling thread is terminating.
            // TODO: ensure that we haven't been alerted for some other reason.
            WNBD_LOG_INFO("Wait alterted, terminating.");
            KeSetEvent(&Device->DeviceRemovalEvent, IO_NO_INCREMENT, FALSE);
            break;
        }

        PLIST_ENTRY RequestEntry = ExInterlockedRemoveHeadList(
            &Device->PendingReqListHead,
            &Device->PendingReqListLock);

        if (Device->HardRemoveDevice) {
            break;
        }
        if (!RequestEntry) {
            continue;
        }

        PSRB_QUEUE_ELEMENT Element = CONTAINING_RECORD(RequestEntry, SRB_QUEUE_ELEMENT, Link);
        Element->Tag = InterlockedIncrement64(&(LONG64)RequestHandle);
        SrbSetDataTransferLength(Element->Srb, 0);
        PCDB Cdb = SrbGetCdb(Element->Srb);
        RtlZeroMemory(Request, sizeof(WNBD_IO_REQUEST));
        WnbdRequestType RequestType = ScsiOpToWnbdReqType(Cdb->AsByte[0]);
        WNBD_LOG_DEBUG("Processing request. Address: %p Tag: 0x%llx Type: %d",
                       Element->Srb, Element->Tag, RequestType);

        // TODO: consider moving this to WnbdPendOperation so that we don't
        // queue unsupported requests.
        if (!ValidateScsiRequest(Device, Element)) {
            SrbSetDataTransferLength(Element->Srb, 0);
            SrbSetSrbStatus(Element->Srb, SRB_STATUS_INVALID_REQUEST);
            CompleteRequest(Device, Element, TRUE);
            InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
            continue;
        }

        Request->RequestType = RequestType;
        Request->RequestHandle = Element->Tag;

        PWNBD_PROPERTIES DevProps = &Device->Properties;
        switch(RequestType) {
        case WnbdReqTypeRead:
            Request->Cmd.Read.BlockAddress =
                Element->StartingLbn / DevProps->BlockSize;
            Request->Cmd.Read.BlockCount =
                Element->DataLength / DevProps->BlockSize;
            Request->Cmd.Read.ForceUnitAccess =
                Element->FUA && DevProps->Flags.FUASupported;
            break;
        case WnbdReqTypeWrite:
            Request->Cmd.Write.BlockAddress =
                Element->StartingLbn / DevProps->BlockSize;
            Request->Cmd.Write.BlockCount =
                Element->DataLength / DevProps->BlockSize;
            Request->Cmd.Write.ForceUnitAccess =
                Element->FUA && DevProps->Flags.FUASupported;
            break;
        case WnbdReqTypeFlush:
            Request->Cmd.Flush.BlockAddress =
                Element->StartingLbn / DevProps->BlockSize;
            Request->Cmd.Flush.BlockCount =
                Element->DataLength / DevProps->BlockSize;
            break;
        case WnbdReqTypeUnmap:
            // At the moment, we only support sending one unmap
            // descriptor at a time.
            Request->Cmd.Unmap.Count = 1;

            if (sizeof(WNBD_UNMAP_DESCRIPTOR) > Command->DataBufferSize) {
                // The user buffer must be at least as large as
                // the specified maximum transfer length, which in
                // turn must fit a WNBD_UNMAP_DESCRIPTOR.
                SrbSetSrbStatus(Element->Srb, SRB_STATUS_INTERNAL_ERROR);
                CompleteRequest(Device, Element, TRUE);
                InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
                Status = STATUS_BUFFER_TOO_SMALL;
                goto Exit;
            }

            PWNBD_UNMAP_DESCRIPTOR UnmapDescriptor = (
                PWNBD_UNMAP_DESCRIPTOR) Buffer;
            RtlZeroMemory(UnmapDescriptor, sizeof(WNBD_UNMAP_DESCRIPTOR));
            UnmapDescriptor->BlockAddress =
                Element->StartingLbn / DevProps->BlockSize;
            UnmapDescriptor->BlockCount =
                Element->DataLength / DevProps->BlockSize;
            break;
        case WnbdReqTypePersistResIn:
            Request->Cmd.PersistResIn.ServiceAction =
                Cdb->PERSISTENT_RESERVE_IN.ServiceAction;
            REVERSE_BYTES_2(
                &Request->Cmd.PersistResIn.AllocationLength,
                &Cdb->PERSISTENT_RESERVE_IN.AllocationLength);
            break;
        case WnbdReqTypePersistResOut:
            Request->Cmd.PersistResOut.ServiceAction =
                Cdb->PERSISTENT_RESERVE_OUT.ServiceAction;
            Request->Cmd.PersistResOut.Scope =
                Cdb->PERSISTENT_RESERVE_OUT.Scope;
            Request->Cmd.PersistResOut.Type =
                Cdb->PERSISTENT_RESERVE_OUT.Type;
            REVERSE_BYTES_2(
                &Request->Cmd.PersistResOut.ParameterListLength,
                &Cdb->PERSISTENT_RESERVE_OUT.ParameterListLength);
            break;
        }

        // Copy SRB buffer if needed
        switch(RequestType) {
        case WnbdReqTypeWrite:
        case WnbdReqTypePersistResOut:
            if (Element->DataLength > Command->DataBufferSize) {
                SrbSetSrbStatus(Element->Srb, SRB_STATUS_INTERNAL_ERROR);
                CompleteRequest(Device, Element, TRUE);
                InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
                Status = STATUS_BUFFER_TOO_SMALL;
                goto Exit;
            }

            PVOID SrbBuffer;
            ULONG StorResult = StorPortGetSystemAddress(
                Element->DeviceExtension, Element->Srb, &SrbBuffer);
            if (StorResult) {
                SrbSetSrbStatus(Element->Srb, SRB_STATUS_INTERNAL_ERROR);
                CompleteRequest(Device, Element, TRUE);
                InterlockedDecrement64(&Device->Stats.UnsubmittedIORequests);
                WNBD_LOG_WARN("Could not get SRB %p 0x%llx data buffer. Error: %lu.",
                              Element->Srb, Element->Tag, StorResult);
                continue;
            }

            RtlCopyMemory(Buffer, SrbBuffer, Element->DataLength);
            break;
        }

        ExInterlockedInsertTailList(
            &Device->SubmittedReqListHead,
            &Element->Link, &Device->SubmittedReqListLock);
        InterlockedIncrement64(&Device->Stats.TotalSubmittedIORequests);
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

    if (Device->HardRemoveDevice) {
        Request->RequestType = WnbdReqTypeDisconnect;
        Status = 0;
    }

    return Status;
}

NTSTATUS WnbdHandleResponse(
    PIRP Irp,
    PWNBD_DISK_DEVICE Device,
    PWNBD_IOCTL_SEND_RSP_COMMAND Command)
{
    PSRB_QUEUE_ELEMENT Element = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    PVOID SrbBuff = NULL, LockedUserBuff = NULL;
    PMDL Mdl = NULL;
    BOOLEAN BufferLocked = FALSE;
    PWNBD_IO_RESPONSE Response = &Command->Response;

    if ((ULONG)Device->Properties.Pid != IoGetRequestorProcessId(Irp)) {
        WNBD_LOG_DEBUG("Invalid pid: %d != %u.",
            Device->Properties.Pid, IoGetRequestorProcessId(Irp));
        return STATUS_ACCESS_DENIED;
    }
    if ((ULONG)Device->Properties.Flags.UseNbd) {
        WNBD_LOG_DEBUG("Direct IO is not allowed using NBD devices.");
        return STATUS_ACCESS_DENIED;
    }

    PLIST_ENTRY ItemLink, ItemNext;
    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&Device->SubmittedReqListLock, &Irql);
    LIST_FORALL_SAFE(&Device->SubmittedReqListHead, ItemLink, ItemNext) {
        Element = CONTAINING_RECORD(ItemLink, SRB_QUEUE_ELEMENT, Link);
        if (Element->Tag == Response->RequestHandle) {
            RemoveEntryList(&Element->Link);
            break;
        }
        Element = NULL;
    }
    KeReleaseSpinLock(&Device->SubmittedReqListLock, Irql);
    if (!Element) {
        WNBD_LOG_DEBUG("Received reply with no matching request tag: 0x%llx",
            Response->RequestHandle);
        return STATUS_NOT_FOUND;
    }

    ULONG StorResult;
    if (!Element->Aborted) {
        // We need to avoid accessing aborted or already completed SRBs.
        PCDB Cdb = SrbGetCdb(Element->Srb);
        WnbdRequestType RequestType = ScsiOpToWnbdReqType(Cdb->AsByte[0]);
        WNBD_LOG_DEBUG("Received reply header for %d %p 0x%llx.",
                       RequestType, Element->Srb, Element->Tag);

        if (IsReadSrb(Element->Srb) || IsPerResInSrb(Element->Srb)) {
            StorResult = StorPortGetSystemAddress(Element->DeviceExtension, Element->Srb, &SrbBuff);
            if (STOR_STATUS_SUCCESS != StorResult) {
                WNBD_LOG_WARN("Could not get SRB %p 0x%llx data buffer. Error: %lu.",
                              Element->Srb, Element->Tag, StorResult);
                SrbSetSrbStatus(Element->Srb, SRB_STATUS_INTERNAL_ERROR);
                Status = STATUS_INTERNAL_ERROR;
                goto Exit;
            }
        }
    } else {
        WNBD_LOG_DEBUG("Received reply header for aborted request: %p 0x%llx.",
                       Element->Srb, Element->Tag);
    }

    if (!Response->Status.ScsiStatus &&
            (IsReadSrb(Element->Srb) || IsPerResInSrb(Element->Srb))) {
        if (!Command->DataBuffer) {
            if (Command->DataBufferSize > 0) {
                WNBD_LOG_DEBUG("Invalid reply: %p 0x%llx. "
                               "NULL buffer with non-zero buffer size: %d.",
                               Element->Srb, Element->Tag, Command->DataBufferSize);
                Status = STATUS_INVALID_PARAMETER;
                SrbSetSrbStatus(Element->Srb, SRB_STATUS_INTERNAL_ERROR);
                goto Exit;
            }
        } else {
            Status = LockUsermodeBuffer(
                Command->DataBuffer, Command->DataBufferSize, FALSE,
                &LockedUserBuff, &Mdl, &BufferLocked);
            if (Status) {
                SrbSetSrbStatus(Element->Srb, SRB_STATUS_INTERNAL_ERROR);
                goto Exit;
            }
        }
        if (!Element->Aborted && SrbBuff) {
            if (Command->DataBuffer && Command->DataBufferSize > 0) {
                RtlCopyMemory(
                    SrbBuff, LockedUserBuff,
                    min(Element->DataLength, Command->DataBufferSize));
            }
            if (Command->DataBufferSize < Element->DataLength) {
                RtlZeroMemory(
                    (char*)SrbBuff + Command->DataBufferSize,
                    Element->DataLength - Command->DataBufferSize);
            }
        }
    }
    if (Response->Status.ScsiStatus) {
        WNBD_LOG_DEBUG(
            "Received reply with non-zero scsi status: 0x%llx # 0x%llx",
            Response->Status.ScsiStatus,
            Response->RequestHandle);
        SrbSetDataTransferLength(Element->Srb, 0);
        SetSrbStatus(Element->Srb, &Response->Status);
    }
    else {
        SrbSetDataTransferLength(Element->Srb, Element->DataLength);
        SrbSetSrbStatus(Element->Srb, SRB_STATUS_SUCCESS);
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
