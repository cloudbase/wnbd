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
#include "options.h"

#define CHECK_I_LOCATION(Io, Type) (Io->Parameters.DeviceIoControl.InputBufferLength < sizeof(Type))
#define CHECK_O_LOCATION(Io, Type) (Io->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Type))
#define CHECK_O_LOCATION_SZ(Io, Size) (Io->Parameters.DeviceIoControl.OutputBufferLength < Size)
#define Malloc(S) ExAllocatePoolWithTag(NonPagedPoolNx, (S), 'DBNu')

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
    ASSERT(InquiryData);

    RtlZeroMemory(InquiryData, sizeof(INQUIRYDATA));
    char ProductRevision[4] = {0};
    RtlStringCbPrintfA(ProductRevision, sizeof(ProductRevision), "%d.%d",
        WNBD_VERSION_MAJOR, WNBD_VERSION_MINOR);

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
        min(sizeof(InquiryData->VendorId), strlen(WNBD_INQUIRY_VENDOR_ID)));
    RtlCopyMemory((PUCHAR)&InquiryData->ProductId[0], WNBD_INQUIRY_PRODUCT_ID,
        min(sizeof(InquiryData->ProductId), strlen(WNBD_INQUIRY_PRODUCT_ID)));
    RtlCopyMemory((PUCHAR)&InquiryData->ProductRevisionLevel[0],
        ProductRevision, sizeof(ProductRevision));
}

NTSTATUS
WnbdInitializeNbdClient(_In_ PWNBD_DISK_DEVICE Device)
{
    ASSERT(Device);
    HANDLE request_thread_handle = NULL, reply_thread_handle = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    Device->ReadPreallocatedBuffer = NbdMalloc((UINT)WNBD_PREALLOC_BUFF_SZ);
    if (!Device->ReadPreallocatedBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    Device->ReadPreallocatedBufferLength = WNBD_PREALLOC_BUFF_SZ;
    Device->WritePreallocatedBuffer = NbdMalloc((UINT)WNBD_PREALLOC_BUFF_SZ);
    if (!Device->WritePreallocatedBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    Device->WritePreallocatedBufferLength = WNBD_PREALLOC_BUFF_SZ;

    Status = PsCreateSystemThread(&request_thread_handle, (ACCESS_MASK)0L, NULL,
                                  NULL, NULL, NbdDeviceRequestThread, Device);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = ObReferenceObjectByHandle(request_thread_handle, THREAD_ALL_ACCESS, NULL, KernelMode,
        &Device->DeviceRequestThread, NULL);

    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = PsCreateSystemThread(&reply_thread_handle, (ACCESS_MASK)0L, NULL,
                                  NULL, NULL, NbdDeviceReplyThread, Device);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = ObReferenceObjectByHandle(reply_thread_handle, THREAD_ALL_ACCESS, NULL, KernelMode,
        &Device->DeviceReplyThread, NULL);

    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(&Device->Stats, sizeof(WNBD_DRV_STATS));

    return Status;

Exit:
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
    Device->HardRemoveDevice = TRUE;
    KeSetEvent(&Device->DeviceRemovalEvent, IO_NO_INCREMENT, FALSE);

    return Status;
}

VOID
WnbdDeviceMonitorThread(_In_ PVOID Context)
{
    PWNBD_DISK_DEVICE Device = (PWNBD_DISK_DEVICE) Context;
    ASSERT(Device);
    ASSERT(Device->DeviceExtension);

    PWNBD_EXTENSION DeviceExtension = Device->DeviceExtension;
    PVOID WaitObjects[2];
    WaitObjects[0] = &DeviceExtension->GlobalDeviceRemovalEvent;
    WaitObjects[1] = &Device->DeviceRemovalEvent;
    KeWaitForMultipleObjects(
        2, WaitObjects, WaitAny, Executive, KernelMode,
        FALSE, NULL, NULL);

    WNBD_LOG_INFO("Cleaning up device connection: %s.",
                  Device->Properties.InstanceName);

    // Soft termination is currently handled by the userspace,
    // which notifies the PnP stack.
    Device->HardRemoveDevice = TRUE;
    KeSetEvent(&Device->DeviceRemovalEvent, IO_NO_INCREMENT, FALSE);
    LARGE_INTEGER Timeout;
    // TODO: consider making this configurable, currently 120s.
    // TODO: move this timeout to ksocket.
    Timeout.QuadPart = (-120 * 1000 * 10000);
    DisconnectSocket(Device);

    // Ensure that the device isn't currently being accessed.
    WNBD_LOG_INFO("Waiting for pending device requests: %s.",
                  Device->Properties.InstanceName);
    ExWaitForRundownProtectionRelease(&Device->RundownProtection);
    WNBD_LOG_INFO("Finished waiting for pending device requests: %s.",
                  Device->Properties.InstanceName);

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
    KeReleaseSpinLock(&DeviceExtension->DeviceListLock, Irql);

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

    // Release the extension device reference count, allowing it to be
    // unloaded.
    ExReleaseRundownProtection(&DeviceExtension->RundownProtection);
    KeLeaveCriticalRegion();
}

NTSTATUS
WnbdInitializeDevice(_In_ PWNBD_DISK_DEVICE Device, BOOLEAN UseNbd)
{
    // Internal resource initialization.
    ASSERT(Device);
    NTSTATUS Status = STATUS_SUCCESS;

    InitializeListHead(&Device->PendingReqListHead);
    KeInitializeSpinLock(&Device->PendingReqListLock);
    InitializeListHead(&Device->SubmittedReqListHead);
    KeInitializeSpinLock(&Device->SubmittedReqListLock);
    ExInitializeRundownProtection(&Device->RundownProtection);
    KeInitializeSemaphore(&Device->DeviceEvent, 0, 1 << 30);
    KeInitializeEvent(&Device->DeviceRemovalEvent, NotificationEvent, FALSE);
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
    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdCreateConnection(PWNBD_EXTENSION DeviceExtension,
                     PWNBD_PROPERTIES Properties,
                     PWNBD_CONNECTION_INFO ConnectionInfo)
{
    ASSERT(DeviceExtension);
    ASSERT(Properties);

    NTSTATUS Status = STATUS_SUCCESS;
    INT Sock = -1;
    PINQUIRYDATA InquiryData = NULL;

    KeEnterCriticalRegion();
    BOOLEAN RPAcquired = ExAcquireRundownProtection(&DeviceExtension->RundownProtection);
    KeLeaveCriticalRegion();

    BOOLEAN NewMappingsAllowed = WnbdDriverOptions[OptNewMappingsAllowed].Value.Data.AsBool;
    if (!NewMappingsAllowed) {
        WNBD_LOG_WARN("New mappings are not currently allowed. Please check the "
                      "'NewMappingsAllowed' option.");
        return STATUS_SHUTDOWN_IN_PROGRESS;
    }

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

    PWNBD_DISK_DEVICE Device = (PWNBD_DISK_DEVICE) Malloc(sizeof(WNBD_DISK_DEVICE));
    if (!Device) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    RtlZeroMemory(Device, sizeof(WNBD_DISK_DEVICE));
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

    static UINT64 ConnectionId = 1;

    Device->Bus = (USHORT)(bitNumber / MAX_NUMBER_OF_SCSI_TARGETS);
    Device->Target = bitNumber % SCSI_MAXIMUM_TARGETS_PER_BUS;
    Device->Lun = 0;
    Device->DiskNumber = -1;
    Device->ConnectionId = (UINT64)InterlockedIncrement64(&(LONG64)ConnectionId);
    WNBD_LOG_INFO("New device address: bus: %d, target: %d, lun: %d, "
                  "connection id: %llu, instance name: %s.",
                  Device->Bus, Device->Target, Device->Lun,
                  ConnectionInfo->ConnectionId,
                  Device->Properties.InstanceName);

    // TODO: consider moving NBD initialization to a separate function.
    UINT16 NbdFlags = 0;
    Device->SocketToClose = -1;
    Device->NbdSocket = -1;
    if (Properties->Flags.UseNbd) {
        WNBD_LOG_INFO("Connecting to NBD server: %s:%p. "
                      "Export name: %s.",
                      Properties->NbdProperties.Hostname,
                      Properties->NbdProperties.PortNumber,
                      Properties->NbdProperties.ExportName);
        Sock = NbdOpenAndConnect(
            Properties->NbdProperties.Hostname,
            Properties->NbdProperties.PortNumber);
        if (-1 == Sock) {
            Status = STATUS_CONNECTION_REFUSED;
            goto Exit;
        }
        Device->NbdSocket = Sock;

        if (!Properties->NbdProperties.Flags.SkipNegotiation) {
            WNBD_LOG_INFO("Performing NBD handshake.");
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
        WNBD_LOG_INFO("Closing socket FD: %d", Sock);
        Close(Sock);
        Sock = -1;
    }

    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdDeleteConnection(PWNBD_EXTENSION DeviceExtension,
                     PCHAR InstanceName)
{
    ASSERT(DeviceExtension);
    ASSERT(InstanceName);

    PWNBD_DISK_DEVICE Device = WnbdFindDeviceByInstanceName(
        DeviceExtension, InstanceName, TRUE);
    if (!Device) {
        WNBD_LOG_INFO("Could not find connection to delete");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    WnbdDisconnectSync(Device);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
WnbdSetDiskSize(PWNBD_EXTENSION DeviceExtension, WNBD_CONNECTION_ID ConnectionId, UINT64 BlockCount) {
    ASSERT(DeviceExtension);
    ASSERT(ConnectionId);

    PWNBD_DISK_DEVICE Device = WnbdFindDeviceByConnId(DeviceExtension, ConnectionId, TRUE);

    if (!Device) {
        WNBD_LOG_ERROR("Could not find the device to resize.");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    WNBD_LOG_INFO("Resized disk: %s. "
        "Old block count %lld, new block count: %lld. "
        "Block size: %d.",
        Device->Properties.InstanceName,
        Device->Properties.BlockCount,
        BlockCount,
        Device->Properties.BlockSize);

    Device->Properties.BlockCount = BlockCount;

    WnbdReleaseDevice(Device);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
WnbdEnumerateActiveConnections(PWNBD_EXTENSION DeviceExtension, PIRP Irp)
{
    ASSERT(DeviceExtension);
    ASSERT(Irp);

    PWNBD_DISK_DEVICE CurrentEntry = (PWNBD_DISK_DEVICE)DeviceExtension->DeviceList.Flink;
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
    while ((CurrentEntry != (PWNBD_DISK_DEVICE) &DeviceExtension->DeviceList.Flink) && Remaining) {
        OutEntry = &OutList->Connections[OutList->Count];
        RtlZeroMemory(OutEntry, sizeof(WNBD_CONNECTION_INFO));
        RtlCopyMemory(OutEntry, &CurrentEntry->Properties, sizeof(WNBD_PROPERTIES));

        OutEntry->BusNumber = (USHORT)CurrentEntry->Bus;
        OutEntry->TargetId = (USHORT)CurrentEntry->Target;
        OutEntry->Lun = (USHORT)CurrentEntry->Lun;
        OutEntry->DiskNumber = CurrentEntry->DiskNumber;
        RtlCopyMemory(&OutEntry->PNPDeviceID,
                      &CurrentEntry->PNPDeviceID,
                      sizeof(CurrentEntry->PNPDeviceID));

        OutList->Count++;
        Remaining--;

        CurrentEntry = (PWNBD_DISK_DEVICE)CurrentEntry->ListEntry.Flink;
    }
    KeReleaseSpinLock(&DeviceExtension->DeviceListLock, Irql);

    if (CurrentEntry == (PWNBD_DISK_DEVICE) &DeviceExtension->DeviceList.Flink) {
        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Information = (OutList->Count * sizeof(WNBD_CONNECTION_INFO)) +
        RTL_SIZEOF_THROUGH_FIELD(WNBD_CONNECTION_LIST, Count);
    WNBD_LOG_DEBUG("Exit: %d. Element count: %d, element size: %d. Total size: %d.",
                   Status, OutList->Count, OutList->ElementSize,
                   Irp->IoStatus.Information);

    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdParseUserIOCTL(PWNBD_EXTENSION DeviceExtension,
                   PIRP Irp)
{
    ASSERT(Irp);
    ASSERT(DeviceExtension);
    PIO_STACK_LOCATION IoLocation = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    PWNBD_DISK_DEVICE Device = NULL;
    DWORD Ioctl = IoLocation->Parameters.DeviceIoControl.IoControlCode;
    DWORD OutBuffLength = IoLocation->Parameters.DeviceIoControl.OutputBufferLength;
    WNBD_LOG_DEBUG("DeviceIoControl = 0x%x.", Ioctl);

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
        WNBD_LOG_DEBUG("IOCTL_WNBD_PING");
        Status = STATUS_SUCCESS;
        break;

    case IOCTL_WNBD_CREATE:
        WNBD_LOG_DEBUG("IOCTL_WNBD_CREATE");
        PWNBD_IOCTL_CREATE_COMMAND Command = (
            PWNBD_IOCTL_CREATE_COMMAND) Irp->AssociatedIrp.SystemBuffer;
        if (!Command ||
            CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_CREATE_COMMAND) ||
            CHECK_O_LOCATION(IoLocation, WNBD_CONNECTION_INFO))
        {
            WNBD_LOG_WARN("IOCTL_WNBD_CREATE: Bad input or output buffer");
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
            WNBD_LOG_WARN("IOCTL_WNBD_CREATE: Invalid instance name.");
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
                WNBD_LOG_WARN(
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
        if (!Status) {
            PWNBD_CONNECTION_INFO OutInfo =
                (PWNBD_CONNECTION_INFO) Irp->AssociatedIrp.SystemBuffer;
            RtlCopyMemory(OutInfo, &ConnectionInfo, sizeof(WNBD_CONNECTION_INFO));
            Irp->IoStatus.Information = sizeof(WNBD_CONNECTION_INFO);

            WNBD_LOG_INFO("Mapped disk. Name: %s, connection id: %llu",
                          Props.InstanceName, ConnectionInfo.ConnectionId);
        }

        KeEnterCriticalRegion();
        ExReleaseResourceLite(&DeviceExtension->DeviceCreationLock);
        KeLeaveCriticalRegion();
        break;

    case IOCTL_WNBD_REMOVE:
        WNBD_LOG_DEBUG("IOCTL_WNBD_REMOVE");
        PWNBD_IOCTL_REMOVE_COMMAND RmCmd = (
            PWNBD_IOCTL_REMOVE_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!RmCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_REMOVE_COMMAND)) {
            WNBD_LOG_WARN("IOCTL_WNBD_REMOVE: Bad input buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        RmCmd->InstanceName[WNBD_MAX_NAME_LENGTH - 1] = '\0';
        if (!strlen((PCHAR)RmCmd->InstanceName)) {
            WNBD_LOG_WARN("IOCTL_WNBD_REMOVE: Invalid instance name");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        WNBD_LOG_INFO("Disconnecting disk: %s.", RmCmd->InstanceName);
        Status = WnbdDeleteConnection(DeviceExtension, RmCmd->InstanceName);
        break;
    case IOCTL_WNBD_SET_DISK_SIZE:
        WNBD_LOG_DEBUG("IOCTL_WNBD_SET_DISK_SIZE");
        PWNBD_IOCTL_SET_SIZE_COMMAND SetSizeCmd = (
            PWNBD_IOCTL_SET_SIZE_COMMAND)Irp->AssociatedIrp.SystemBuffer;
        Status = WnbdSetDiskSize(
            DeviceExtension,
            SetSizeCmd->ConnectionId,
            SetSizeCmd->BlockCount);
        break;

     case IOCTL_WNBD_LIST:
        WNBD_LOG_DEBUG("IOCTL_WNBD_LIST");
        DWORD RequiredBuffSize = (
            DeviceExtension->DeviceCount * sizeof(WNBD_CONNECTION_INFO))
            + sizeof(WNBD_CONNECTION_LIST);

        if (!Irp->AssociatedIrp.SystemBuffer ||
            CHECK_O_LOCATION_SZ(IoLocation, RequiredBuffSize))
        {
            WNBD_LOG_DEBUG("IOCTL_WNBD_LIST: Bad output buffer");
            Irp->IoStatus.Information = RequiredBuffSize;
            break;
        }
        Status = WnbdEnumerateActiveConnections(DeviceExtension, Irp);
        break;

    case IOCTL_WNBD_SHOW:
        WNBD_LOG_DEBUG("WNBD_SHOW");
        PWNBD_IOCTL_SHOW_COMMAND ShowCmd =
            (PWNBD_IOCTL_SHOW_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!ShowCmd || CHECK_I_LOCATION(IoLocation, PWNBD_IOCTL_SHOW_COMMAND)) {
            WNBD_LOG_WARN("WNBD_SHOW: Bad input buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        ShowCmd->InstanceName[WNBD_MAX_NAME_LENGTH - 1] = '\0';
        if (!strlen((PSTR) &ShowCmd->InstanceName)) {
            WNBD_LOG_WARN("WNBD_SHOW: Invalid instance name");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!Irp->AssociatedIrp.SystemBuffer ||
                CHECK_O_LOCATION(IoLocation, WNBD_CONNECTION_INFO)) {
            WNBD_LOG_ERROR("WNBD_SHOW: Bad output buffer");
            Status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        Device = WnbdFindDeviceByInstanceName(
            DeviceExtension, ShowCmd->InstanceName, TRUE);
        if (!Device) {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            WNBD_LOG_DEBUG("WNBD_SHOW: Connection does not exist");
            break;
        }

        PWNBD_CONNECTION_INFO OutConnInfo = (
            PWNBD_CONNECTION_INFO) Irp->AssociatedIrp.SystemBuffer;
        RtlZeroMemory(OutConnInfo, sizeof(WNBD_CONNECTION_INFO));
        RtlCopyMemory(OutConnInfo, &Device->Properties, sizeof(WNBD_PROPERTIES));

        OutConnInfo->BusNumber = (USHORT)Device->Bus;
        OutConnInfo->TargetId = (USHORT)Device->Target;
        OutConnInfo->Lun = (USHORT)Device->Lun;
        OutConnInfo->DiskNumber = Device->DiskNumber;
        RtlCopyMemory(&OutConnInfo->PNPDeviceID,
                      &Device->PNPDeviceID,
                      sizeof(Device->PNPDeviceID));

        WnbdReleaseDevice(Device);

        Irp->IoStatus.Information = sizeof(WNBD_CONNECTION_INFO);
        Status = STATUS_SUCCESS;
        break;

    case IOCTL_WNBD_RELOAD_CONFIG:
        WNBD_LOG_DEBUG("IOCTL_WNBD_RELOAD_CONFIG");
        WnbdReloadPersistentOptions();
        break;

    case IOCTL_WNBD_STATS:
        WNBD_LOG_DEBUG("WNBD_STATS");
        PWNBD_IOCTL_STATS_COMMAND StatsCmd =
            (PWNBD_IOCTL_STATS_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!StatsCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_STATS_COMMAND)) {
            WNBD_LOG_WARN("WNBD_STATS: Bad input buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        StatsCmd->InstanceName[WNBD_MAX_NAME_LENGTH - 1] = '\0';
        if (!strlen((PSTR) &StatsCmd->InstanceName)) {
            WNBD_LOG_WARN("WNBD_STATS: Invalid instance name");
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
            WNBD_LOG_DEBUG("WNBD_STATS: Connection does not exist");
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
        WNBD_LOG_DEBUG("IOCTL_WNBD_FETCH_REQ");
        PWNBD_IOCTL_FETCH_REQ_COMMAND ReqCmd =
            (PWNBD_IOCTL_FETCH_REQ_COMMAND) Irp->AssociatedIrp.SystemBuffer;
        if (!ReqCmd ||
            CHECK_I_LOCATION(IoLocation, PWNBD_IOCTL_FETCH_REQ_COMMAND) ||
            CHECK_O_LOCATION(IoLocation, PWNBD_IOCTL_FETCH_REQ_COMMAND))
        {
            WNBD_LOG_WARN("IOCTL_WNBD_FETCH_REQ: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Device = WnbdFindDeviceByConnId(
            DeviceExtension, ReqCmd->ConnectionId, TRUE);
        if (!Device) {
            Status = STATUS_INVALID_HANDLE;
            WNBD_LOG_DEBUG(
                "IOCTL_WNBD_FETCH_REQ: Could not fetch request, invalid connection id: %d.",
                ReqCmd->ConnectionId);
            break;
        }

        Status = WnbdDispatchRequest(Irp, Device, ReqCmd);
        Irp->IoStatus.Information = sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND);
        WNBD_LOG_DEBUG("Request dispatch status: %d. Request type: %d Request handle: %llx",
                       Status, ReqCmd->Request.RequestType, ReqCmd->Request.RequestHandle);

        WnbdReleaseDevice(Device);
        break;

    case IOCTL_WNBD_SEND_RSP:
        WNBD_LOG_DEBUG("IOCTL_WNBD_SEND_RSP");
        PWNBD_IOCTL_SEND_RSP_COMMAND RspCmd =
            (PWNBD_IOCTL_SEND_RSP_COMMAND) Irp->AssociatedIrp.SystemBuffer;
        if (!RspCmd || CHECK_I_LOCATION(IoLocation, PWNBD_IOCTL_FETCH_REQ_COMMAND)) {
            WNBD_LOG_WARN("IOCTL_WNBD_SEND_RSP: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Device = WnbdFindDeviceByConnId(DeviceExtension, RspCmd->ConnectionId, TRUE);
        if (!Device) {
            Status = STATUS_INVALID_HANDLE;
            WNBD_LOG_DEBUG(
                "IOCTL_WNBD_SEND_RSP: Could not fetch request, invalid connection id: %d.",
                RspCmd->ConnectionId);
            break;
        }

        Status = WnbdHandleResponse(Irp, Device, RspCmd);
        WNBD_LOG_DEBUG("Reply handling status: 0x%x.", Status);

        WnbdReleaseDevice(Device);
        break;

    case IOCTL_WNBD_VERSION:
        WNBD_LOG_DEBUG("IOCTL_WNBD_VERSION");
        PWNBD_IOCTL_VERSION_COMMAND VersionCmd =
            (PWNBD_IOCTL_VERSION_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!VersionCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_VERSION_COMMAND)) {
            WNBD_LOG_WARN("IOCTL_WNBD_VERSION: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (!Irp->AssociatedIrp.SystemBuffer ||
                CHECK_O_LOCATION(IoLocation, WNBD_VERSION)) {
            WNBD_LOG_WARN("IOCTL_WNBD_VERSION: Bad output buffer");
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

    case IOCTL_WNBD_GET_DRV_OPT:
        WNBD_LOG_DEBUG("IOCTL_WNBD_GET_DRV_OPT");
        PWNBD_IOCTL_GET_DRV_OPT_COMMAND GetOptCmd =
            (PWNBD_IOCTL_GET_DRV_OPT_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!GetOptCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_GET_DRV_OPT_COMMAND)) {
            WNBD_LOG_WARN("IOCTL_WNBD_GET_DRV_OPT: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (!Irp->AssociatedIrp.SystemBuffer ||
                CHECK_O_LOCATION(IoLocation, WNBD_OPTION_VALUE)) {
            WNBD_LOG_ERROR("IOCTL_WNBD_GET_DRV_OPT: Bad output buffer");
            Status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        GetOptCmd->Name[WNBD_MAX_OPT_NAME_LENGTH - 1] = L'\0';
        PWNBD_OPTION_VALUE Value = (PWNBD_OPTION_VALUE) Irp->AssociatedIrp.SystemBuffer;
        Status = WnbdGetDrvOpt(GetOptCmd->Name, Value, GetOptCmd->Persistent);

        Irp->IoStatus.Information = sizeof(WNBD_OPTION_VALUE);
        break;

    case IOCTL_WNBD_SET_DRV_OPT:
        WNBD_LOG_DEBUG("IOCTL_WNBD_SET_DRV_OPT");
        PWNBD_IOCTL_SET_DRV_OPT_COMMAND SetOptCmd =
            (PWNBD_IOCTL_SET_DRV_OPT_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!SetOptCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_SET_DRV_OPT_COMMAND)) {
            WNBD_LOG_WARN("IOCTL_WNBD_SET_DRV_OPT: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        SetOptCmd->Name[WNBD_MAX_OPT_NAME_LENGTH - 1] = L'\0';
        Status = WnbdSetDrvOpt(SetOptCmd->Name, &SetOptCmd->Value, SetOptCmd->Persistent);
        if (!Status && !_wcsicmp(L"NewMappingsAllowed", SetOptCmd->Name)
                    && !SetOptCmd->Value.Data.AsBool) {
            WNBD_LOG_INFO("No new mappings allowed. Waiting for pending mappings.");
            KeEnterCriticalRegion();
            ExAcquireResourceSharedLite(&DeviceExtension->DeviceCreationLock, TRUE);
            ExReleaseResourceLite(&DeviceExtension->DeviceCreationLock);
            KeLeaveCriticalRegion();
            WNBD_LOG_INFO("Finished waiting for pending mappings. "
                          "No new mappings allowed.");
        }
        break;

    case IOCTL_WNBD_RESET_DRV_OPT:
        WNBD_LOG_DEBUG("IOCTL_WNBD_RESET_DRV_OPT");
        PWNBD_IOCTL_RESET_DRV_OPT_COMMAND ResetOptCmd =
            (PWNBD_IOCTL_RESET_DRV_OPT_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!ResetOptCmd || CHECK_I_LOCATION(IoLocation, WNBD_IOCTL_RESET_DRV_OPT_COMMAND)) {
            WNBD_LOG_WARN("IOCTL_WNBD_RESET_DRV_OPT: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        ResetOptCmd->Name[WNBD_MAX_OPT_NAME_LENGTH - 1] = L'\0';
        Status = WnbdResetDrvOpt(ResetOptCmd->Name, ResetOptCmd->Persistent);
        break;

    case IOCTL_WNBD_LIST_DRV_OPT:
        WNBD_LOG_DEBUG("IOCTL_WNBD_LIST_DRV_OPT");
        PWNBD_IOCTL_LIST_DRV_OPT_COMMAND ListOptCmd =
            (PWNBD_IOCTL_LIST_DRV_OPT_COMMAND) Irp->AssociatedIrp.SystemBuffer;

        if (!ListOptCmd || CHECK_I_LOCATION(IoLocation, IOCTL_WNBD_LIST_DRV_OPT)) {
            WNBD_LOG_DEBUG("IOCTL_WNBD_RESET_DRV_OPT: Bad input or output buffer");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        PWNBD_OPTION_LIST OptionList = (PWNBD_OPTION_LIST) Irp->AssociatedIrp.SystemBuffer;
        Status = WnbdListDrvOpt(OptionList, &OutBuffLength, ListOptCmd->Persistent);
        if (!Status || Status == STATUS_BUFFER_TOO_SMALL) {
            // We're masking STATUS_BUFFER_TOO_SMALL so that the required
            // buffer size gets gets passed to the userspace.
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = OutBuffLength;
        }
        break;

    default:
        WNBD_LOG_ERROR("Unsupported IOCTL command: %d", Cmd->IoControlCode);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WNBD_LOG_DEBUG("Exit: 0x%x", Status);
    return Status;
}
