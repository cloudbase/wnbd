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

VOID DrainDeviceQueue(_In_ PWNBD_DISK_DEVICE Device,
                      _In_ BOOLEAN SubmittedRequests)
{
    WNBD_LOG_LOUD(": Enter");

    PLIST_ENTRY Request;
    PSRB_QUEUE_ELEMENT Element;
    PLIST_ENTRY ListHead;
    PKSPIN_LOCK ListLock;

    if (SubmittedRequests) {
        ListHead = &Device->PendingReqListHead;
        ListLock = &Device->PendingReqListLock;
    }
    else {
        ListHead = &Device->SubmittedReqListHead;
        ListLock = &Device->SubmittedReqListLock;
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

VOID AbortSubmittedRequests(_In_ PWNBD_DISK_DEVICE Device)
{
    // We're marking submitted requests as aborted and notifying Storport. We only cleaning
    // them up when eventually receiving a reply from the storage backend (needed by NBD,
    // in which case the IO payload is otherwise unknown).
    // TODO: consider cleaning up aborted requests when not using NBD. For NBD, we might
    // have a limit of aborted requests that we keep around.
    WNBD_LOG_LOUD(": Enter");

    PLIST_ENTRY ListHead = &Device->SubmittedReqListHead;
    PKSPIN_LOCK ListLock = &Device->SubmittedReqListLock;

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
WnbdAcquireDevice(_In_ PWNBD_DISK_DEVICE Device)
{
    BOOLEAN Acquired = FALSE;
    // TODO: limit the scope of critical regions.
    if (!Device)
        return Acquired;

    KIRQL Irql = KeGetCurrentIrql();
    if (Irql <= APC_LEVEL) {
        KeEnterCriticalRegion();
    }

    Acquired = ExAcquireRundownProtection(&Device->RundownProtection);

    if (Irql <= APC_LEVEL) {
        KeLeaveCriticalRegion();
    }

    return Acquired;
}

VOID
WnbdReleaseDevice(_In_ PWNBD_DISK_DEVICE Device)
{
    if (!Device)
        return;

    KIRQL Irql = KeGetCurrentIrql();
    if (Irql <= APC_LEVEL) {
        KeEnterCriticalRegion();
    }

    ExReleaseRundownProtection(&Device->RundownProtection);

    if (Irql <= APC_LEVEL) {
        KeLeaveCriticalRegion();
    }
}

// The returned device must be subsequently relased using WnbdReleaseDevice,
// if "Acquire" is set. Unacquired device pointers must not be dereferenced.
PWNBD_DISK_DEVICE
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
    PWNBD_DISK_DEVICE Device = NULL;
    for (PLIST_ENTRY Entry = DeviceExtension->DeviceList.Flink;
         Entry != &DeviceExtension->DeviceList; Entry = Entry->Flink)
    {
        Device = (PWNBD_DISK_DEVICE) CONTAINING_RECORD(Entry, WNBD_DISK_DEVICE, ListEntry);
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
PWNBD_DISK_DEVICE
WnbdFindDeviceByConnId(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ UINT64 ConnectionId,
    _In_ BOOLEAN Acquire)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);

    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&DeviceExtension->DeviceListLock, &Irql);
    PWNBD_DISK_DEVICE Device = NULL;
    for (PLIST_ENTRY Entry = DeviceExtension->DeviceList.Flink;
         Entry != &DeviceExtension->DeviceList; Entry = Entry->Flink)
    {
        Device = (PWNBD_DISK_DEVICE) CONTAINING_RECORD(Entry, WNBD_DISK_DEVICE, ListEntry);
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
PWNBD_DISK_DEVICE
WnbdFindDeviceByInstanceName(
    _In_ PWNBD_EXTENSION DeviceExtension,
    _In_ PCHAR InstanceName,
    _In_ BOOLEAN Acquire)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);

    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&DeviceExtension->DeviceListLock, &Irql);
    PWNBD_DISK_DEVICE Device = NULL;
    for (PLIST_ENTRY Entry = DeviceExtension->DeviceList.Flink;
         Entry != &DeviceExtension->DeviceList; Entry = Entry->Flink)
    {
        Device = (PWNBD_DISK_DEVICE) CONTAINING_RECORD(Entry, WNBD_DISK_DEVICE, ListEntry);
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

VOID CloseSocket(_In_ PWNBD_DISK_DEVICE Device) {
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

VOID DisconnectSocket(_In_ PWNBD_DISK_DEVICE Device) {
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
WnbdDisconnectAsync(PWNBD_DISK_DEVICE Device)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Device);

    Device->HardRemoveDevice = TRUE;
    KeSetEvent(&Device->DeviceRemovalEvent, IO_NO_INCREMENT, FALSE);

    WNBD_LOG_LOUD(": Exit");
}

// The specified device must be acquired. It will be released by
// WnbdDisconnectSync.
VOID
WnbdDisconnectSync(_In_ PWNBD_DISK_DEVICE Device)
{
    WNBD_LOG_LOUD(": Enter");
    // We're holding a device reference, preventing it from being
    // cleaned up while we're accessing it.
    PVOID DeviceMonitorThread = Device->DeviceMonitorThread;
    // Make sure that the thread handle stays valid.
    ObReferenceObject(DeviceMonitorThread);
    KeSetEvent(&Device->DeviceRemovalEvent, IO_NO_INCREMENT, FALSE);
    // It's very important to release our device reference, allowing it to be removed.
    // Do not access the device after releasing it.
    WnbdReleaseDevice(Device);

    KeWaitForSingleObject(DeviceMonitorThread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(DeviceMonitorThread);
    WNBD_LOG_LOUD(": Exit");
}

BOOLEAN
IsReadSrb(_In_ PSCSI_REQUEST_BLOCK Srb)
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

BOOLEAN
ValidateScsiRequest(
    _In_ PWNBD_DISK_DEVICE Device,
    _In_ PSRB_QUEUE_ELEMENT Element)
{
    PCDB Cdb = (PCDB)&Element->Srb->Cdb;
    int ScsiOp = Cdb->AsByte[0];
    int NbdReqType = ScsiOpToWnbdReqType(ScsiOp);
    PWNBD_PROPERTIES DevProps = &Device->Properties;

    switch (NbdReqType) {
    case WnbdReqTypeUnmap:
    case WnbdReqTypeWrite:
    case WnbdReqTypeFlush:
        if (DevProps->Flags.ReadOnly) {
            WNBD_LOG_LOUD(
                "Write, flush or trim requested on a read-only disk.");
            return FALSE;
        }
    case WnbdReqTypeRead:
        break;
    default:
        WNBD_LOG_LOUD("Unsupported SCSI operation: %d.", ScsiOp);
        return FALSE;
    }

    if (NbdReqType == WnbdReqTypeUnmap && !DevProps->Flags.UnmapSupported) {
        WNBD_LOG_LOUD("The NBD server doesn't accept TRIM/UNMAP.");
        return FALSE;
    }
    if (NbdReqType == WnbdReqTypeFlush && !DevProps->Flags.FlushSupported) {
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
    _In_ PWNBD_DISK_DEVICE Device,
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

VOID WnbdSendIoctl(
    ULONG ControlCode,
    PDEVICE_OBJECT DeviceObject,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength,
    PIO_STATUS_BLOCK IoStatus)
{
    ASSERT(!KeAreAllApcsDisabled());

    KEVENT Event;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    PIRP Irp = IoBuildDeviceIoControlRequest(
        ControlCode,
        DeviceObject,
        InputBuffer,
        InputBufferLength,
        OutputBuffer,
        OutputBufferLength,
        FALSE,
        &Event,
        IoStatus);
    if (!Irp)
    {
        IoStatus->Information = 0;
        IoStatus->Status = STATUS_INSUFFICIENT_RESOURCES;
        return;
    }

    NTSTATUS Result = IoCallDriver(DeviceObject, Irp);
    if (NT_ERROR(Result))
    {
        IoStatus->Status = Result;
        IoStatus->Information = 0;
    }
    if (STATUS_PENDING == Result) {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, 0);
    }
}

NTSTATUS WnbdGetScsiAddress(
    PDEVICE_OBJECT DeviceObject,
    PSCSI_ADDRESS ScsiAddress)
{
    IO_STATUS_BLOCK IoStatus = { 0 };

    RtlZeroMemory(ScsiAddress, sizeof(SCSI_ADDRESS));
    WnbdSendIoctl(
        IOCTL_SCSI_GET_ADDRESS,
        DeviceObject,
        0, 0,
        ScsiAddress, sizeof(SCSI_ADDRESS),
        &IoStatus);
    if (!NT_SUCCESS(IoStatus.Status))
        return IoStatus.Status;

    if (IoStatus.Information < sizeof(ScsiAddress))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    return STATUS_SUCCESS;
}
