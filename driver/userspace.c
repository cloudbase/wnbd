/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <ntifs.h>
#include <limits.h>

#include <berkeley.h>
#include <ksocket.h>
#include "common.h"
#include "debug.h"
#include "nbd_protocol.h"
#include "scsi_function.h"
#include "userspace.h"
#include "nbd_dispatch.h"
#include "wnbd_dispatch.h"
#include "wnbd_ioctl.h"
#include "util.h"
#include "version.h"

#define CHECK_I_LOCATION(Io, Type) (Io->Parameters.DeviceIoControl.InputBufferLength < sizeof(Type))
#define CHECK_O_LOCATION(Io, Type) (Io->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Type))
#define CHECK_O_LOCATION_SZ(Io, Size) (Io->Parameters.DeviceIoControl.OutputBufferLength < Size)
#define Malloc(S) ExAllocatePoolWithTag(NonPagedPoolNx, (S), 'DBNu')

extern UNICODE_STRING GlobalRegistryPath;

extern RTL_BITMAP ScsiBitMapHeader = { 0 };
ULONG AssignedScsiIds[((SCSI_MAXIMUM_TARGETS_PER_BUS / 8) / sizeof(ULONG)) * MAX_NUMBER_OF_SCSI_TARGETS];
VOID WnbdInitScsiIds()
{
    RtlZeroMemory(AssignedScsiIds, sizeof(AssignedScsiIds));
    RtlInitializeBitMap(&ScsiBitMapHeader, AssignedScsiIds, SCSI_MAXIMUM_TARGETS_PER_BUS * MAX_NUMBER_OF_SCSI_TARGETS);
}

VOID
WnbdSetInquiryData(_Inout_ PINQUIRYDATA InquiryData)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(InquiryData);

    RtlZeroMemory(InquiryData, sizeof(INQUIRYDATA));
    InquiryData->DeviceType = DIRECT_ACCESS_DEVICE;
    InquiryData->DeviceTypeQualifier = DEVICE_QUALIFIER_ACTIVE;
    InquiryData->DeviceTypeModifier = 0;
    InquiryData->RemovableMedia = 0;
    // TODO: consider bumping to SPC-4 or SPC-5.
    InquiryData->Versions = 5;
    InquiryData->ResponseDataFormat = 2;
    InquiryData->Wide32Bit = TRUE;
    InquiryData->Synchronous = FALSE;
    InquiryData->CommandQueue = 1;
    InquiryData->AdditionalLength = INQUIRYDATABUFFERSIZE -
        RTL_SIZEOF_THROUGH_FIELD(INQUIRYDATA, AdditionalLength);
    InquiryData->LinkedCommands = FALSE;
    RtlCopyMemory((PUCHAR)&InquiryData->VendorId[0], WNBD_INQUIRY_VENDOR_ID,
        strlen(WNBD_INQUIRY_VENDOR_ID));
    RtlCopyMemory((PUCHAR)&InquiryData->ProductId[0], WNBD_INQUIRY_PRODUCT_ID,
        strlen(WNBD_INQUIRY_PRODUCT_ID));
    RtlCopyMemory((PUCHAR)&InquiryData->ProductRevisionLevel[0], WNBD_INQUIRY_PRODUCT_REVISION,
        strlen(WNBD_INQUIRY_PRODUCT_REVISION));
    RtlCopyMemory((PUCHAR)&InquiryData->VendorSpecific[0], WNBD_INQUIRY_VENDOR_SPECIFIC,
        strlen(WNBD_INQUIRY_VENDOR_SPECIFIC));

    WNBD_LOG_LOUD(": Exit");
}

NTSTATUS
WnbdInitializeNbdClient(_In_ PWNBD_SCSI_DEVICE Device)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Device);
    HANDLE request_thread_handle = NULL, reply_thread_handle = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    Device->ReadPreallocatedBuffer = NbdMalloc((UINT)WNBD_PREALLOC_BUFF_SZ);
    if (!Device->ReadPreallocatedBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }
    Device->ReadPreallocatedBufferLength = WNBD_PREALLOC_BUFF_SZ;
    Device->WritePreallocatedBuffer = NbdMalloc((UINT)WNBD_PREALLOC_BUFF_SZ);
    if (!Device->WritePreallocatedBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }
    Device->WritePreallocatedBufferLength = WNBD_PREALLOC_BUFF_SZ;

    Status = PsCreateSystemThread(&request_thread_handle, (ACCESS_MASK)0L, NULL,
                                  NULL, NULL, NbdDeviceRequestThread, Device);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

    Status = ObReferenceObjectByHandle(request_thread_handle, THREAD_ALL_ACCESS, NULL, KernelMode,
        &Device->DeviceRequestThread, NULL);

    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

    Status = PsCreateSystemThread(&reply_thread_handle, (ACCESS_MASK)0L, NULL,
                                  NULL, NULL, NbdDeviceReplyThread, Device);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

    Status = ObReferenceObjectByHandle(reply_thread_handle, THREAD_ALL_ACCESS, NULL, KernelMode,
        &Device->DeviceReplyThread, NULL);

    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

    RtlZeroMemory(&Device->Stats, sizeof(WNBD_DRV_STATS));

    return Status;

SoftTerminate:
    ExDeleteResourceLite(&Device->SocketLock);
    if (Device->ReadPreallocatedBuffer) {
        ExFreePool(Device->ReadPreallocatedBuffer);
    }
    if (Device->WritePreallocatedBuffer) {
        ExFreePool(Device->WritePreallocatedBuffer);
    }
    if (request_thread_handle)
        ZwClose(request_thread_handle);
    if (reply_thread_handle)
        ZwClose(reply_thread_handle);
    Device->SoftTerminateDevice = TRUE;
    KeReleaseSemaphore(&Device->DeviceEvent, 0, 1, FALSE);

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

VOID
WnbdDeviceMonitorThread(_In_ PVOID Context)
{
    PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE) Context;
    ASSERT(Device);
    ASSERT(Device->DeviceExtension);

    PWNBD_EXTENSION DeviceExtension = Device->DeviceExtension;
    PVOID WaitObjects[2];
    WaitObjects[0] = &DeviceExtension->GlobalDeviceRemovalEvent;
    WaitObjects[1] = &Device->TerminateEvent;
    KeWaitForMultipleObjects(
        2, WaitObjects, WaitAny, Executive, KernelMode,
        FALSE, NULL, NULL);

    Device->SoftTerminateDevice = TRUE;
    // TODO: implement proper soft termination.
    Device->HardTerminateDevice = TRUE;
    KeSetEvent(&Device->TerminateEvent, IO_NO_INCREMENT, FALSE);
    LARGE_INTEGER Timeout;
    // TODO: consider making this configurable, currently 120s.
    // TODO: move this timeout to ksocket.
    Timeout.QuadPart = (-120 * 1000 * 10000);
    DisconnectSocket(Device);

    // Ensure that the device isn't currently being accessed.
    ExWaitForRundownProtectionRelease(&Device->RundownProtection);

    if (Device->Properties.Flags.UseNbd) {
        KeWaitForSingleObject(Device->DeviceRequestThread, Executive, KernelMode, FALSE, NULL);
        KeWaitForSingleObject(Device->DeviceReplyThread, Executive, KernelMode, FALSE, &Timeout);
        ObDereferenceObject(Device->DeviceRequestThread);
        ObDereferenceObject(Device->DeviceReplyThread);
    }

    DrainDeviceQueue(Device, FALSE);
    DrainDeviceQueue(Device, TRUE);

    CloseSocket(Device);

    // After acquiring the device spinlock, we should return as quickly as possible.
    KIRQL Irql = { 0 };
    KeEnterCriticalRegion();
    KeAcquireSpinLock(&DeviceExtension->DeviceListLock, &Irql);
    RemoveEntryList(&Device->ListEntry);
    StorPortNotification(BusChangeDetected, DeviceExtension, 0);

    RtlClearBits(&ScsiBitMapHeader,
                 Device->Target + (Device->Bus * SCSI_MAXIMUM_TARGETS_PER_BUS), 1);
    InterlockedDecrement(&DeviceExtension->DeviceCount);

    if (Device->InquiryData) {
        ExFreePool(Device->InquiryData);
        Device->InquiryData = NULL;
    }

    ExDeleteResourceLite(&Device->SocketLock);

    if (Device->ReadPreallocatedBuffer) {
        ExFreePool(Device->ReadPreallocatedBuffer);
        Device->ReadPreallocatedBuffer = NULL;
    }
    if (Device->WritePreallocatedBuffer) {
        ExFreePool(Device->WritePreallocatedBuffer);
        Device->WritePreallocatedBuffer = NULL;
    }

    ObDereferenceObject(Device->DeviceMonitorThread);
    ExFreePool(Device);
    KeReleaseSpinLock(&DeviceExtension->DeviceListLock, Irql);

    // Release the extension device reference count, allowing it to be
    // unloaded.
    ExReleaseRundownProtection(&DeviceExtension->RundownProtection);
    KeLeaveCriticalRegion();

    WNBD_LOG_LOUD(": Exit");    
}

NTSTATUS
WnbdInitializeDevice(_In_ PWNBD_SCSI_DEVICE Device, BOOLEAN UseNbd)
{
    // Internal resource initialization.
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Device);
    NTSTATUS Status = STATUS_SUCCESS;

    InitializeListHead(&Device->PendingReqListHead);
    KeInitializeSpinLock(&Device->PendingReqListLock);
    InitializeListHead(&Device->SubmittedReqListHead);
    KeInitializeSpinLock(&Device->SubmittedReqListLock);
    ExInitializeRundownProtection(&Device->RundownProtection);
    KeInitializeSemaphore(&Device->DeviceEvent, 0, 1 << 30);
    KeInitializeEvent(&Device->TerminateEvent, NotificationEvent, FALSE);
    // TODO: check if this is still needed.
    Status = ExInitializeResourceLite(&Device->SocketLock);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    HANDLE monitor_thread_handle;
    Status = PsCreateSystemThread(&monitor_thread_handle, (ACCESS_MASK)0L, NULL,
                                  NULL, NULL, WnbdDeviceMonitorThread, Device);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    Status = ObReferenceObjectByHandle(
        monitor_thread_handle, THREAD_ALL_ACCESS, NULL, KernelMode,
        &Device->DeviceMonitorThread, NULL);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    if (UseNbd) {
        Status = WnbdInitializeNbdClient(Device);
    }

Exit:
    WNBD_LOG_LOUD(": Exit");
    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdCreateConnection(PWNBD_EXTENSION DeviceExtension,
                     PWNBD_PROPERTIES Properties,
                     PWNBD_CONNECTION_INFO ConnectionInfo)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    ASSERT(Properties);

    NTSTATUS Status = STATUS_SUCCESS;
    INT Sock = -1;
    PINQUIRYDATA InquiryData = NULL;

    KeEnterCriticalRegion();
    BOOLEAN RPAcquired = ExAcquireRundownProtection(&DeviceExtension->RundownProtection);
    KeLeaveCriticalRegion();

    if (!RPAcquired) {
        // This shouldn't really happen while having a pending "HwProcessServiceRequest".
        WNBD_LOG_WARN("The device extension is being removed.");
        return STATUS_SHUTDOWN_IN_PROGRESS;
    }

    if (WnbdFindDeviceByInstanceName(
        DeviceExtension, Properties->InstanceName, FALSE)) {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto Exit;
    }

    PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE) Malloc(sizeof(WNBD_SCSI_DEVICE));
    if (!Device) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    RtlZeroMemory(Device, sizeof(WNBD_SCSI_DEVICE));
    RtlCopyMemory(&Device->Properties, Properties, sizeof(WNBD_PROPERTIES));

    Device->DeviceExtension = DeviceExtension;

    InquiryData = (PINQUIRYDATA) Malloc(sizeof(INQUIRYDATA));
    if (NULL == InquiryData) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    WnbdSetInquiryData(InquiryData);
    Device->InquiryData = InquiryData;

    ULONG bitNumber = RtlFindClearBitsAndSet(&ScsiBitMapHeader, 1, 0);
    if (0xFFFFFFFF == bitNumber) {
        Status = STATUS_INVALID_FIELD_IN_PARAMETER_LIST;
        goto Exit;
    }

    Device->Bus = (USHORT)(bitNumber / MAX_NUMBER_OF_SCSI_TARGETS);
    Device->Target = bitNumber % SCSI_MAXIMUM_TARGETS_PER_BUS;
    Device->Lun = 0;
    Device->ConnectionId =  WNBD_CONNECTION_ID_FROM_ADDR(
        Device->Bus, Device->Target, Device->Lun);
    WNBD_LOG_INFO("Bus: %d, target: %d, lun: %d, connection id: %llu.",
                  Device->Bus, Device->Target, Device->Lun, ConnectionInfo->ConnectionId);

    // TODO: consider moving NBD initialization to a separate function.
    UINT16 NbdFlags = 0;
    Device->SocketToClose = -1;
    Device->NbdSocket = -1;
    if (Properties->Flags.UseNbd) {
        Sock = NbdOpenAndConnect(
            Properties->NbdProperties.Hostname,
            Properties->NbdProperties.PortNumber);
        if (-1 == Sock) {
            Status = STATUS_CONNECTION_REFUSED;
            goto Exit;
        }
        Device->NbdSocket = Sock;

        if (!Properties->NbdProperties.Flags.SkipNegotiation) {
            WNBD_LOG_INFO("Trying to negotiate handshake with NBD Server");
            UINT64 DiskSize = 0;
            Status = NbdNegotiate(&Sock, &DiskSize, &NbdFlags,
                                  Properties->NbdProperties.ExportName, 1, 1);
            if (!NT_SUCCESS(Status)) {
                goto Exit;
            }
            WNBD_LOG_INFO("Negotiated disk size: %llu", DiskSize);
            // TODO: negotiate block size.
            Device->Properties.BlockSize = WNBD_DEFAULT_BLOCK_SIZE;
            Device->Properties.BlockCount = DiskSize / Device->Properties.BlockSize;
        }

        Device->Properties.Flags.ReadOnly |= CHECK_NBD_READONLY(NbdFlags);
        Device->Properties.Flags.UnmapSupported |= CHECK_NBD_SEND_TRIM(NbdFlags);
        Device->Properties.Flags.FlushSupported |= CHECK_NBD_SEND_FLUSH(NbdFlags);
        Device->Properties.Flags.FUASupported |= CHECK_NBD_SEND_FUA(NbdFlags);
    }

    if (!Device->Properties.BlockSize || !Device->Properties.BlockCount ||
        Device->Properties.BlockCount > ULLONG_MAX / Device->Properties.BlockSize)
    {
        WNBD_LOG_ERROR("Invalid block size or block count. "
                       "Block size: %d. Block count: %lld.",
                       Device->Properties.BlockSize,
                       Device->Properties.BlockCount);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    WNBD_LOG_INFO("Retrieved NBD flags: %d. Read-only: %d, TRIM enabled: %d, "
                  "FLUSH enabled: %d, FUA enabled: %d.",
                   NbdFlags,
                   Device->Properties.Flags.ReadOnly,
                   Device->Properties.Flags.UnmapSupported,
                   Device->Properties.Flags.FlushSupported,
                   Device->Properties.Flags.FUASupported);

    Status = WnbdInitializeDevice(Device, !!Properties->Flags.UseNbd);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    // The connection properties might be slightly different than the ones set
    // by the client (e.g. after NBD negotiation or setting default values).
    RtlCopyMemory(&ConnectionInfo->Properties, &Device->Properties, sizeof(WNBD_PROPERTIES));
    ConnectionInfo->BusNumber = Device->Bus;
    ConnectionInfo->TargetId = Device->Target;
    ConnectionInfo->Lun = Device->Lun;
    ConnectionInfo->ConnectionId = Device->ConnectionId;

    ExInterlockedInsertTailList(
        &DeviceExtension->DeviceList, &Device->ListEntry,
        &DeviceExtension->DeviceListLock);

    InterlockedIncrement(&DeviceExtension->DeviceCount);
    StorPortNotification(BusChangeDetected, DeviceExtension, 0);

    Device->Connected = TRUE;
    Status = STATUS_SUCCESS;

    WNBD_LOG_LOUD(": Exit");

    return Status;

Exit:
    if (Status && RPAcquired) {
        KeEnterCriticalRegion();
        ExReleaseRundownProtection(&DeviceExtension->RundownProtection);
        KeLeaveCriticalRegion();
    }
    if (InquiryData) {
        ExFreePool(InquiryData);
    }
    if (-1 != Sock) {
        WNBD_LOG_ERROR("Closing socket FD: %d", Sock);
        Close(Sock);
        Sock = -1;
    }

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdDeleteConnection(PWNBD_EXTENSION DeviceExtension,
                     PCHAR InstanceName)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    ASSERT(InstanceName);

    PWNBD_SCSI_DEVICE Device = WnbdFindDeviceByInstanceName(
        DeviceExtension, InstanceName, TRUE);
    if (!Device) {
        WNBD_LOG_ERROR("Could not find connection to delete");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    // We're holding a device reference, preventing it from being
    // cleaned up while we're accessing it.
    PVOID DeviceMonitorThread = Device->DeviceMonitorThread;
    // Make sure that the thread handle stays valid.
    ObReferenceObject(DeviceMonitorThread);
    KeSetEvent(&Device->TerminateEvent, IO_NO_INCREMENT, FALSE);
    // It's very important to release our device reference, allowing it to be removed.
    // Do not access the device after releasing it.
    WnbdReleaseDevice(Device);

    KeWaitForSingleObject(DeviceMonitorThread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(DeviceMonitorThread);
    
    WNBD_LOG_LOUD(": Exit");  
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
WnbdEnumerateActiveConnections(PWNBD_EXTENSION DeviceExtension, PIRP Irp)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    ASSERT(Irp);

    PWNBD_SCSI_DEVICE CurrentEntry = (PWNBD_SCSI_DEVICE)DeviceExtension->DeviceList.Flink;
    PIO_STACK_LOCATION IoLocation = IoGetCurrentIrpStackLocation(Irp);
    PWNBD_CONNECTION_LIST OutList = (
        PWNBD_CONNECTION_LIST) Irp->AssociatedIrp.SystemBuffer;
    PWNBD_CONNECTION_INFO OutEntry = &OutList->Connections[0];

    // If we propagate STATUS_BUFFER_OVERFLOW to the userspace, it won't
    // receive the actual required buffer size.
    NTSTATUS Status = STATUS_BUFFER_OVERFLOW;
    ULONG Remaining = (
        IoLocation->Parameters.DeviceIoControl.OutputBufferLength -
            RTL_SIZEOF_THROUGH_FIELD(WNBD_CONNECTION_LIST, Count)
        ) / sizeof(WNBD_CONNECTION_INFO);
    OutList->Count = 0;
    OutList->ElementSize = sizeof(WNBD_CONNECTION_INFO);

    KIRQL Irql = { 0 };
    KeAcquireSpinLock(&DeviceExtension->DeviceListLock, &Irql);
    while ((CurrentEntry != (PWNBD_SCSI_DEVICE) &DeviceExtension->DeviceList.Flink) && Remaining) {
        OutEntry = &OutList->Connections[OutList->Count];
        RtlZeroMemory(OutEntry, sizeof(WNBD_CONNECTION_INFO));
        RtlCopyMemory(OutEntry, &CurrentEntry->Properties, sizeof(WNBD_PROPERTIES));

        OutEntry->BusNumber = (USHORT)CurrentEntry->Bus;
        OutEntry->TargetId = (USHORT)CurrentEntry->Target;
        OutEntry->Lun = (USHORT)CurrentEntry->Lun;

        OutList->Count++;
        Remaining--;

        CurrentEntry = (PWNBD_SCSI_DEVICE)CurrentEntry->ListEntry.Flink;
    }
    KeReleaseSpinLock(&DeviceExtension->DeviceListLock, Irql);

    if (CurrentEntry == (PWNBD_SCSI_DEVICE) &DeviceExtension->DeviceList.Flink) {
        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Information = (OutList->Count * sizeof(WNBD_CONNECTION_INFO)) +
        RTL_SIZEOF_THROUGH_FIELD(WNBD_CONNECTION_LIST, Count);
    WNBD_LOG_LOUD(": Exit: %d. Element count: %d, element size: %d. Total size: %d.",
                  Status, OutList->Count, OutList->ElementSize,
                  Irp->IoStatus.Information);

    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdParseUserIOCTL(PWNBD_EXTENSION DeviceExtension,
                   PIRP Irp)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Irp);
    ASSERT(DeviceExtension);
    PIO_STACK_LOCATION IoLocation = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    PWNBD_SCSI_DEVICE Device = NULL;
    DWORD Ioctl = IoLocation->Parameters.DeviceIoControl.IoControlCode;
    WNBD_LOG_LOUD("DeviceIoControl = 0x%x.", Ioctl);

    if (IOCTL_MINIPORT_PROCESS_SERVICE_IRP != Ioctl) {
        WNBD_LOG_ERROR("Unsupported IOCTL (%x)", Ioctl);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    PWNBD_IOCTL_BASE_COMMAND Cmd = (
        PWNBD_IOCTL_BASE_COMMAND) Irp->AssociatedIrp.SystemBuffer;
    if (NULL == Cmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_BASE_COMMAND)) {
        WNBD_LOG_ERROR("Missing IOCTL command.");
        return STATUS_INVALID_PARAMETER;
    }

    switch (Cmd->IoControlCode) {
    case IOCTL_WNBD_PING:
        WNBD_LOG_LOUD("IOCTL_WNBD_PING");
        Status = STATUS_SUCCESS;
        break;

    case IOCTL_WNBD_CREATE:
        WNBD_LOG_LOUD("IOCTL_WNBD_CREATE");
        PWNBD_IOCTL_CREATE_COMMAND Command = (
            PWNBD_IOCTL_CREATE_COMMAND) Irp->AssociatedIrp.SystemBuffer;
        if (!Command ||
            CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_CREATE_COMMAND) ||
            CHECK_O_LOCATION(IoLocation, WNBD_CONNECTION_INFO))
        {
            WNBD_LOG_ERROR("IOCTL_WNBD_CREATE: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        WNBD_PROPERTIES Props = Command->Properties;
        Props.InstanceName[WNBD_MAX_NAME_LENGTH - 1] = '\0';
        Props.SerialNumber[WNBD_MAX_NAME_LENGTH - 1] = '\0';
        Props.Owner[WNBD_MAX_OWNER_LENGTH - 1] = '\0';
        Props.NbdProperties.Hostname[WNBD_MAX_NAME_LENGTH - 1] = '\0';
        Props.NbdProperties.ExportName[WNBD_MAX_NAME_LENGTH - 1] = '\0';

        if (!strlen((char*)&Props.InstanceName)) {
            WNBD_LOG_ERROR("IOCTL_WNBD_CREATE: Invalid instance name.");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (!strlen((char*)&Props.SerialNumber)) {
            RtlCopyMemory((char*) &Props.SerialNumber, &Props.InstanceName,
                          strlen(Props.InstanceName));
        }
        if (!Props.Pid) {
            Props.Pid = IoGetRequestorProcessId(Irp);
        }

        // Those might be retrieved later through NBD negotiation.
        BOOLEAN UseNbdNegotiation =
            Props.Flags.UseNbd && !Props.NbdProperties.Flags.SkipNegotiation;
        if (!UseNbdNegotiation) {
            if (!Props.BlockCount || !Props.BlockCount ||
                Props.BlockCount > ULLONG_MAX / Props.BlockSize)
            {
                WNBD_LOG_ERROR(
                    "IOCTL_WNBD_CREATE: Invalid block size or block count. "
                    "Block size: %d. Block count: %lld.",
                    Props.BlockSize, Props.BlockCount);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        KeEnterCriticalRegion();
        ExAcquireResourceSharedLite(&DeviceExtension->DeviceCreationLock, TRUE);
        KeLeaveCriticalRegion();

        WNBD_LOG_INFO("Mapping disk. Name: %s, Serial=%s, BC=%llu, BS=%lu, Pid=%d",
                      Props.InstanceName, Props.SerialNumber,
                      Props.BlockCount, Props.BlockSize, Props.Pid);

        WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
        Status = WnbdCreateConnection(DeviceExtension, &Props, &ConnectionInfo);

        PWNBD_CONNECTION_INFO OutHandle = (PWNBD_CONNECTION_INFO) Irp->AssociatedIrp.SystemBuffer;
        RtlCopyMemory(OutHandle, &ConnectionInfo, sizeof(WNBD_CONNECTION_INFO));
        Irp->IoStatus.Information = sizeof(WNBD_CONNECTION_INFO);

        WNBD_LOG_LOUD("Mapped disk. Name: %s, connection id: %llu",
                      Props.InstanceName, ConnectionInfo.ConnectionId);

        KeEnterCriticalRegion();
        ExReleaseResourceLite(&DeviceExtension->DeviceCreationLock);
        KeLeaveCriticalRegion();
        break;

    case IOCTL_WNBD_REMOVE:
        WNBD_LOG_LOUD("IOCTL_WNBD_REMOVE");
        PWNBD_IOCTL_REMOVE_COMMAND RmCmd = (
            PWNBD_IOCTL_REMOVE_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!RmCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_REMOVE_COMMAND)) {
            WNBD_LOG_ERROR("IOCTL_WNBD_REMOVE: Bad input buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        RmCmd->InstanceName[WNBD_MAX_NAME_LENGTH - 1] = '\0';
        if (!strlen((PCHAR)RmCmd->InstanceName)) {
            WNBD_LOG_ERROR("IOCTL_WNBD_REMOVE: Invalid instance name");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = WnbdDeleteConnection(DeviceExtension, RmCmd->InstanceName);
        break;

     case IOCTL_WNBD_LIST:
        WNBD_LOG_LOUD("IOCTL_WNBD_LIST");
        DWORD RequiredBuffSize = (
            DeviceExtension->DeviceCount * sizeof(WNBD_CONNECTION_INFO))
            + sizeof(WNBD_CONNECTION_LIST);

        if (!Irp->AssociatedIrp.SystemBuffer ||
            CHECK_O_LOCATION_SZ(IoLocation, RequiredBuffSize))
        {
            WNBD_LOG_ERROR("IOCTL_WNBD_LIST: Bad output buffer");
            Irp->IoStatus.Information = RequiredBuffSize;
            break;
        }
        Status = WnbdEnumerateActiveConnections(DeviceExtension, Irp);
        break;

    case IOCTL_WNBD_RELOAD_CONFIG:
        WNBD_LOG_LOUD("IOCTL_WNBD_RELOAD_CONFIG");
        WCHAR* KeyName = L"DebugLogLevel";
        UINT32 U32Val = 0;
        if (WNBDReadRegistryValue(
                &GlobalRegistryPath, KeyName,
                (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT), &U32Val))
        {
            WnbdSetLogLevel(U32Val);
        }
        break;

    case IOCTL_WNBD_STATS:
        WNBD_LOG_LOUD("WNBD_STATS");
        PWNBD_IOCTL_STATS_COMMAND StatsCmd =
            (PWNBD_IOCTL_STATS_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!StatsCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_STATS_COMMAND)) {
            WNBD_LOG_ERROR("WNBD_STATS: Bad input buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        StatsCmd->InstanceName[WNBD_MAX_NAME_LENGTH - 1] = '\0';
        if (!strlen((PSTR) &StatsCmd->InstanceName)) {
            WNBD_LOG_ERROR("WNBD_STATS: Invalid instance name");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!Irp->AssociatedIrp.SystemBuffer ||
                CHECK_O_LOCATION(IoLocation, WNBD_DRV_STATS)) {
            WNBD_LOG_ERROR("WNBD_STATS: Bad output buffer");
            Status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        Device = WnbdFindDeviceByInstanceName(
            DeviceExtension, StatsCmd->InstanceName, TRUE);
        if (!Device) {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            WNBD_LOG_ERROR("WNBD_STATS: Connection does not exist");
            break;
        }

        PWNBD_DRV_STATS OutStatus = (
            PWNBD_DRV_STATS) Irp->AssociatedIrp.SystemBuffer;
        RtlCopyMemory(OutStatus, &Device->Stats, sizeof(WNBD_DRV_STATS));
        WnbdReleaseDevice(Device);

        Irp->IoStatus.Information = sizeof(WNBD_DRV_STATS);
        Status = STATUS_SUCCESS;
        break;

    case IOCTL_WNBD_FETCH_REQ:
        // TODO: consider moving out individual command handling.
        WNBD_LOG_LOUD("IOCTL_WNBD_FETCH_REQ");
        PWNBD_IOCTL_FETCH_REQ_COMMAND ReqCmd =
            (PWNBD_IOCTL_FETCH_REQ_COMMAND) Irp->AssociatedIrp.SystemBuffer;
        if (!ReqCmd ||
            CHECK_I_LOCATION(IoLocation, PWNBD_IOCTL_FETCH_REQ_COMMAND) ||
            CHECK_O_LOCATION(IoLocation, PWNBD_IOCTL_FETCH_REQ_COMMAND))
        {
            WNBD_LOG_ERROR("IOCTL_WNBD_FETCH_REQ: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Device = WnbdFindDeviceByConnId(
            DeviceExtension, ReqCmd->ConnectionId, TRUE);
        if (!Device) {
            Status = STATUS_INVALID_HANDLE;
            WNBD_LOG_ERROR(
                "IOCTL_WNBD_FETCH_REQ: Could not fetch request, invalid connection id: %d.",
                ReqCmd->ConnectionId);
            break;
        }

        Status = WnbdDispatchRequest(Irp, Device, ReqCmd);
        Irp->IoStatus.Information = sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND);
        WNBD_LOG_LOUD("Request dispatch status: %d. Request type: %d Request handle: %llx",
                      Status, ReqCmd->Request.RequestType, ReqCmd->Request.RequestHandle);

        WnbdReleaseDevice(Device);
        break;

    case IOCTL_WNBD_SEND_RSP:
        WNBD_LOG_LOUD("IOCTL_WNBD_SEND_RSP");
        PWNBD_IOCTL_SEND_RSP_COMMAND RspCmd =
            (PWNBD_IOCTL_SEND_RSP_COMMAND) Irp->AssociatedIrp.SystemBuffer;
        if (!RspCmd || CHECK_I_LOCATION(IoLocation, PWNBD_IOCTL_FETCH_REQ_COMMAND)) {
            WNBD_LOG_ERROR("IOCTL_WNBD_SEND_RSP: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Device = WnbdFindDeviceByConnId(DeviceExtension, RspCmd->ConnectionId, TRUE);
        if (!Device) {
            Status = STATUS_INVALID_HANDLE;
            WNBD_LOG_ERROR(
                "IOCTL_WNBD_SEND_RSP: Could not fetch request, invalid connection id: %d.",
                RspCmd->ConnectionId);
            break;
        }

        Status = WnbdHandleResponse(Irp, Device, RspCmd);
        WNBD_LOG_LOUD("Reply handling status: %d.", Status);

        WnbdReleaseDevice(Device);
        break;

    case IOCTL_WNBD_VERSION:
        WNBD_LOG_LOUD("IOCTL_WNBD_VERSION");
        PWNBD_IOCTL_VERSION_COMMAND VersionCmd =
            (PWNBD_IOCTL_VERSION_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!VersionCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_VERSION_COMMAND)) {
            WNBD_LOG_ERROR("IOCTL_WNBD_VERSION: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (!Irp->AssociatedIrp.SystemBuffer ||
                CHECK_O_LOCATION(IoLocation, WNBD_VERSION)) {
            WNBD_LOG_ERROR("IOCTL_WNBD_VERSION: Bad output buffer");
            Status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        PWNBD_VERSION Version = (PWNBD_VERSION) Irp->AssociatedIrp.SystemBuffer;
        RtlZeroMemory(Version, sizeof(WNBD_VERSION));
        Version->Major = WNBD_VERSION_MAJOR;
        Version->Minor = WNBD_VERSION_MINOR;
        Version->Patch = WNBD_VERSION_PATCH;
        RtlCopyMemory(&Version->Description, WNBD_VERSION_STR,
                      min(strlen(WNBD_VERSION_STR), WNBD_MAX_VERSION_STR_LENGTH - 1));

        Irp->IoStatus.Information = sizeof(WNBD_VERSION);
        Status = STATUS_SUCCESS;
        break;

    default:
        WNBD_LOG_ERROR("Unsupported IOCTL command: %x");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WNBD_LOG_LOUD("Exit: %d", Status);
    return Status;
}
