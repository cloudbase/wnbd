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
#include "driver_extension.h"
#include "rbd_protocol.h"
#include "scsi_function.h"
#include "userspace.h"
#include "wnbd_dispatch.h"
#include "wnbd_ioctl.h"
#include "util.h"

#define CHECK_I_LOCATION(Io, Type) (Io->Parameters.DeviceIoControl.InputBufferLength < sizeof(Type))
#define CHECK_O_LOCATION(Io, Type) (Io->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Type))
#define CHECK_O_LOCATION_SZ(Io, Size) (Io->Parameters.DeviceIoControl.OutputBufferLength < Size)
#define Malloc(S) ExAllocatePoolWithTag(NonPagedPoolNx, (S), 'DBNu')

extern UNICODE_STRING GlobalRegistryPath;

extern RTL_BITMAP ScsiBitMapHeader = { 0 };
ULONG AssignedScsiIds[((SCSI_MAXIMUM_TARGETS_PER_BUS / 8) / sizeof(ULONG)) * MAX_NUMBER_OF_SCSI_TARGETS];
static USHORT LunId = 0;
VOID WnbdInitScsiIds()
{
    RtlZeroMemory(AssignedScsiIds, sizeof(AssignedScsiIds));
    RtlInitializeBitMap(&ScsiBitMapHeader, AssignedScsiIds, SCSI_MAXIMUM_TARGETS_PER_BUS * MAX_NUMBER_OF_SCSI_TARGETS);
}

_Use_decl_annotations_
BOOLEAN
WnbdFindConnection(PGLOBAL_INFORMATION GInfo,
                   PCHAR InstanceName,
                   PUSER_ENTRY* Entry)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);
    ASSERT(InstanceName);

    // TODO: consider returning the "Entry" directly.
    BOOLEAN Found = FALSE;
    PUSER_ENTRY SearchEntry;

    SearchEntry = (PUSER_ENTRY)GInfo->ConnectionList.Flink;

    while (SearchEntry != (PUSER_ENTRY)&GInfo->ConnectionList.Flink) {
        if (!strcmp((CONST CHAR*)&SearchEntry->Properties.InstanceName, InstanceName)) {
            if (Entry) {
                *Entry = SearchEntry;

            }
            Found = TRUE;
            break;
        }
        SearchEntry = (PUSER_ENTRY)SearchEntry->ListEntry.Flink;
    }

    WNBD_LOG_LOUD(": Exit");
    return Found;
}

_Use_decl_annotations_
PUSER_ENTRY
WnbdFindConnectionEx(_In_ PGLOBAL_INFORMATION GInfo,
                     _In_ UINT64 ConnectionId)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);

    PUSER_ENTRY FoundEntry = NULL;
    PUSER_ENTRY SearchEntry = (PUSER_ENTRY)GInfo->ConnectionList.Flink;

    while (SearchEntry != (PUSER_ENTRY)&GInfo->ConnectionList.Flink) {
        if (SearchEntry->ConnectionId == ConnectionId) {
            FoundEntry = SearchEntry;
            break;
        }
        SearchEntry = (PUSER_ENTRY)SearchEntry->ListEntry.Flink;
    }

    WNBD_LOG_LOUD(": Exit");
    return FoundEntry;
}

PVOID WnbdCreateScsiDevice(_In_ PVOID Extension,
                           _In_ ULONG PathId,
                           _In_ ULONG TargetId,
                           _In_ ULONG Lun,
                           _In_ PVOID ScsiDeviceExtension,
                           _In_ PINQUIRYDATA InquiryData)
{
    PWNBD_EXTENSION	Ext = (PWNBD_EXTENSION)Extension;
    PWNBD_SCSI_DEVICE Dev = NULL;
    PWNBD_LU_EXTENSION LuExt;

    LuExt = (PWNBD_LU_EXTENSION) StorPortGetLogicalUnit(Ext,
                                   (UCHAR) PathId,
                                   (UCHAR) TargetId,
                                   (UCHAR) Lun);

    if (LuExt) {
        WNBD_LOG_ERROR(": LU extension %p already found for %d:%d:%d", LuExt, PathId, TargetId, Lun);
        return NULL;
    }

    Dev = (PWNBD_SCSI_DEVICE) ExAllocatePoolWithTag(NonPagedPoolNx,sizeof(WNBD_SCSI_DEVICE),'DBNs');

    if (!Dev) {
        WNBD_LOG_ERROR(": Allocation failure");
        return NULL;
    }

    WNBD_LOG_INFO(": Device %p with SCSI_INFO %p and LU extension has "
        "been created for %p at %d:%d:%d",
        Dev, ScsiDeviceExtension, LuExt, PathId, TargetId, Lun);

    RtlZeroMemory(Dev,sizeof(WNBD_SCSI_DEVICE));
    Dev->ScsiDeviceExtension = ScsiDeviceExtension;
    Dev->PathId = PathId;
    Dev->TargetId = TargetId;
    Dev->Lun = Lun;
    Dev->PInquiryData = InquiryData;
    Dev->Missing = FALSE;
    Dev->DriverExtension = (PVOID) Ext;

    WNBD_LOG_LOUD(": Exit");

    return Dev;
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

#define MallocT(S) ExAllocatePoolWithTag(NonPagedPoolNx, S, 'pDBR')

NTSTATUS
WnbdInitializeNbdClient(_In_ PSCSI_DEVICE_INFORMATION ScsiInfo)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(ScsiInfo);
    HANDLE request_thread_handle = NULL, reply_thread_handle = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    ScsiInfo->ReadPreallocatedBuffer = MallocT(((UINT)WNBD_PREALLOC_BUFF_SZ));
    if (!ScsiInfo->ReadPreallocatedBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }
    ScsiInfo->ReadPreallocatedBufferLength = WNBD_PREALLOC_BUFF_SZ;
    ScsiInfo->WritePreallocatedBuffer = MallocT(((UINT)WNBD_PREALLOC_BUFF_SZ));
    if (!ScsiInfo->WritePreallocatedBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }
    ScsiInfo->WritePreallocatedBufferLength = WNBD_PREALLOC_BUFF_SZ;

    Status = PsCreateSystemThread(&request_thread_handle, (ACCESS_MASK)0L, NULL,
                                  NULL, NULL, WnbdDeviceRequestThread, ScsiInfo);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

    Status = ObReferenceObjectByHandle(request_thread_handle, THREAD_ALL_ACCESS, NULL, KernelMode,
        &ScsiInfo->DeviceRequestThread, NULL);

    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

    Status = PsCreateSystemThread(&reply_thread_handle, (ACCESS_MASK)0L, NULL,
                                  NULL, NULL, WnbdDeviceReplyThread, ScsiInfo);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

    Status = ObReferenceObjectByHandle(reply_thread_handle, THREAD_ALL_ACCESS, NULL, KernelMode,
        &ScsiInfo->DeviceReplyThread, NULL);

    RtlZeroMemory(&ScsiInfo->Stats, sizeof(WNBD_DRV_STATS));

    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

    return Status;

SoftTerminate:
    ExDeleteResourceLite(&ScsiInfo->SocketLock);
    if (ScsiInfo->ReadPreallocatedBuffer) {
        ExFreePool(ScsiInfo->ReadPreallocatedBuffer);
    }
    if (ScsiInfo->WritePreallocatedBuffer) {
        ExFreePool(ScsiInfo->WritePreallocatedBuffer);
    }
    if (request_thread_handle)
        ZwClose(request_thread_handle);
    if (reply_thread_handle)
        ZwClose(reply_thread_handle);
    ScsiInfo->SoftTerminateDevice = TRUE;
    KeReleaseSemaphore(&ScsiInfo->DeviceEvent, 0, 1, FALSE);

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

NTSTATUS
WnbdInitializeScsiInfo(_In_ PSCSI_DEVICE_INFORMATION ScsiInfo, BOOLEAN UseNbd)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(ScsiInfo);
    NTSTATUS Status = STATUS_SUCCESS;

    InitializeListHead(&ScsiInfo->RequestListHead);
    KeInitializeSpinLock(&ScsiInfo->RequestListLock);
    InitializeListHead(&ScsiInfo->ReplyListHead);
    KeInitializeSpinLock(&ScsiInfo->ReplyListLock);
    ExInitializeRundownProtection(&ScsiInfo->RundownProtection);
    KeInitializeSemaphore(&ScsiInfo->DeviceEvent, 0, 1 << 30);
    KeInitializeEvent(&ScsiInfo->TerminateEvent, NotificationEvent, FALSE);

    // TODO: check if this is still needed.
    Status = ExInitializeResourceLite(&ScsiInfo->SocketLock);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    ScsiInfo->HardTerminateDevice = FALSE;
    ScsiInfo->SoftTerminateDevice = FALSE;

    RtlZeroMemory(&ScsiInfo->Stats, sizeof(WNBD_DRV_STATS));

    if (UseNbd) {
        Status = WnbdInitializeNbdClient(ScsiInfo);
    }

Exit:
    WNBD_LOG_LOUD(": Exit");
    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdCreateConnection(PGLOBAL_INFORMATION GInfo,
                     PWNBD_PROPERTIES Properties,
                     PWNBD_CONNECTION_INFO ConnectionInfo)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);
    ASSERT(Properties);

    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Added = FALSE;
    INT Sock = -1;

    PUSER_ENTRY NewEntry = (PUSER_ENTRY)
        ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(USER_ENTRY), 'DBNu');
    if (!NewEntry) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = KsInitialize();
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    if (WnbdFindConnection(GInfo, Properties->InstanceName, NULL)) {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto Exit;
    }

    RtlZeroMemory(NewEntry,sizeof(USER_ENTRY));
    RtlCopyMemory(&NewEntry->Properties, Properties, sizeof(WNBD_PROPERTIES));
    InsertTailList(&GInfo->ConnectionList, &NewEntry->ListEntry);
    Added = TRUE;

    PINQUIRYDATA InquiryData = (PINQUIRYDATA) Malloc(sizeof(INQUIRYDATA));
    if (NULL == InquiryData) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    WnbdSetInquiryData(InquiryData);

    if (Properties->Flags.UseNbd) {
        Sock = NbdOpenAndConnect(
            Properties->NbdProperties.Hostname,
            Properties->NbdProperties.PortNumber);
        if (-1 == Sock) {
            Status = STATUS_CONNECTION_REFUSED;
            goto ExitInquiryData;
        }
    }

    ULONG bitNumber = RtlFindClearBitsAndSet(&ScsiBitMapHeader, 1, 0);

    if (0xFFFFFFFF == bitNumber) {
        Status = STATUS_INVALID_FIELD_IN_PARAMETER_LIST;
        goto ExitInquiryData;
    }

    UINT16 NbdFlags = 0;
    if (Properties->Flags.UseNbd && !Properties->NbdProperties.Flags.SkipNegotiation) {
        WNBD_LOG_INFO("Trying to negotiate handshake with RBD Server");
        UINT64 DiskSize = 0;
        Status = RbdNegotiate(&Sock, &DiskSize, &NbdFlags,
                              Properties->NbdProperties.ExportName, 1, 1);
        if (!NT_SUCCESS(Status)) {
            goto ExitInquiryData;
        }
        WNBD_LOG_INFO("Negotiated disk size: %llu", DiskSize);
        // TODO: negotiate block size.
        NewEntry->Properties.BlockSize = WNBD_DEFAULT_BLOCK_SIZE;
        NewEntry->Properties.BlockCount = DiskSize / NewEntry->Properties.BlockSize;
    }

    if (!NewEntry->Properties.BlockSize || !NewEntry->Properties.BlockCount ||
        NewEntry->Properties.BlockCount > ULLONG_MAX / NewEntry->Properties.BlockSize)
    {
        WNBD_LOG_ERROR("Invalid block size or block count. "
                       "Block size: %d. Block count: %lld.",
                       NewEntry->Properties.BlockSize,
                       NewEntry->Properties.BlockCount);
        Status = STATUS_INVALID_PARAMETER;
        goto ExitInquiryData;
    }

    NewEntry->Properties.Flags.ReadOnly |= CHECK_NBD_READONLY(NbdFlags);
    NewEntry->Properties.Flags.UnmapSupported |= CHECK_NBD_SEND_TRIM(NbdFlags);
    NewEntry->Properties.Flags.FlushSupported |= CHECK_NBD_SEND_FLUSH(NbdFlags);
    NewEntry->Properties.Flags.FUASupported |= CHECK_NBD_SEND_FUA(NbdFlags);

    USHORT TargetId = bitNumber % SCSI_MAXIMUM_TARGETS_PER_BUS;
    USHORT BusId = (USHORT)(bitNumber / MAX_NUMBER_OF_SCSI_TARGETS);

    WNBD_LOG_INFO("Retrieved NBD flags: %d. Read-only: %d, TRIM enabled: %d, "
                  "FLUSH enabled: %d, FUA enabled: %d.",
                   NbdFlags,
                   NewEntry->Properties.Flags.ReadOnly,
                   NewEntry->Properties.Flags.UnmapSupported,
                   NewEntry->Properties.Flags.FlushSupported,
                   NewEntry->Properties.Flags.FUASupported);

    PSCSI_DEVICE_INFORMATION ScsiInfo = (PSCSI_DEVICE_INFORMATION) Malloc(sizeof(SCSI_DEVICE_INFORMATION));
    if (!ScsiInfo) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ExitInquiryData;
    }

    RtlZeroMemory(ScsiInfo, sizeof(SCSI_DEVICE_INFORMATION));
    ScsiInfo->Device = WnbdCreateScsiDevice(GInfo->Handle,
                                            BusId,
                                            TargetId,
                                            LunId,
                                            ScsiInfo,
                                            InquiryData);

    if (!ScsiInfo->Device) {
        Status = STATUS_DEVICE_ALREADY_ATTACHED;
        goto ExitScsiInfo;
    }

    ScsiInfo->GlobalInformation = GInfo;
    ScsiInfo->InquiryData = InquiryData;
    ScsiInfo->Socket = Sock;

    Status = WnbdInitializeScsiInfo(ScsiInfo, !!Properties->Flags.UseNbd);
    if (!NT_SUCCESS(Status)) {
        goto ExitScsiInfo;
    }

    ScsiInfo->UserEntry = NewEntry;

    // The connection properties might be slightly different than the ones set
    // by the client (e.g. after NBD negotiation or setting default values).
    RtlCopyMemory(&ConnectionInfo->Properties, &NewEntry->Properties, sizeof(WNBD_PROPERTIES));
    ConnectionInfo->BusNumber = BusId;
    ConnectionInfo->TargetId = TargetId;
    ConnectionInfo->Lun = LunId;
    ConnectionInfo->ConnectionId = WNBD_CONNECTION_ID_FROM_ADDR(BusId, TargetId, LunId);
    WNBD_LOG_INFO("Bus: %d, target: %d, lun: %d, connection id: %llu.",
                  BusId, TargetId, LunId, ConnectionInfo->ConnectionId);

    NewEntry->ScsiInformation = ScsiInfo;
    NewEntry->BusIndex = BusId;
    NewEntry->TargetIndex = TargetId;
    NewEntry->LunIndex = LunId;
    NewEntry->ConnectionId = ConnectionInfo->ConnectionId;

    PWNBD_EXTENSION	Ext = (PWNBD_EXTENSION)GInfo->Handle;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Ext->DeviceResourceLock, TRUE);

    InsertTailList(&Ext->DeviceList, &ScsiInfo->Device->ListEntry);

    ExReleaseResourceLite(&Ext->DeviceResourceLock);
    KeLeaveCriticalRegion();

    InterlockedIncrement(&GInfo->ConnectionCount);
    StorPortNotification(BusChangeDetected, GInfo->Handle, 0);

    NewEntry->Connected = TRUE;
    Status = STATUS_SUCCESS;

    WNBD_LOG_LOUD(": Exit");

    return Status;

ExitScsiInfo:
    if (ScsiInfo) {
        if (ScsiInfo->Device) {
            ExFreePool(ScsiInfo->Device);
        }
        ExFreePool(ScsiInfo);
    }
ExitInquiryData:
    if (InquiryData) {
        ExFreePool(InquiryData);
    }
Exit:
    if (-1 != Sock) {
        WNBD_LOG_ERROR("Closing socket FD: %d", Sock);
        Close(Sock);
        Sock = -1;
    }
    if (Added) {
        WnbdDeleteConnectionEntry(NewEntry);
    }
    if (NewEntry) {
        ExFreePool(NewEntry);
    }

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdDeleteConnectionEntry(PUSER_ENTRY Entry)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Entry);

    RemoveEntryList(&Entry->ListEntry);

    WNBD_LOG_LOUD(": Exit");
    return STATUS_SUCCESS;
}

BOOLEAN
WnbdSetDeviceMissing(_In_ PVOID Handle,
                     _In_ BOOLEAN Force)
{
    WNBD_LOG_LOUD(": Enter");
    PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE)Handle;
    
    if (Device == NULL) {
        return TRUE;
    }

    if (Device->OutstandingIoCount && !Force) {
        return FALSE;
    }
    
    ASSERT(!Device->OutstandingIoCount);
    WNBD_LOG_INFO("Disconnecting with, OutstandingIoCount: %d", Device->OutstandingIoCount);

    Device->Missing = TRUE;

    WNBD_LOG_LOUD(": Exit");

    return TRUE;
}


VOID
WnbdDrainQueueOnClose(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation)
{
    PLIST_ENTRY ItemLink, ItemNext;
    KIRQL Irql = { 0 };
    PSRB_QUEUE_ELEMENT Element = NULL;

    KeAcquireSpinLock(&DeviceInformation->RequestListLock, &Irql);
    if (IsListEmpty(&DeviceInformation->RequestListHead))
        goto Reply;

    LIST_FORALL_SAFE(&DeviceInformation->RequestListHead, ItemLink, ItemNext) {
        Element = CONTAINING_RECORD(ItemLink, SRB_QUEUE_ELEMENT, Link);
        if (Element) {
            RemoveEntryList(&Element->Link);
            Element->Srb->DataTransferLength = 0;
            Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
            StorPortNotification(RequestComplete, Element->DeviceExtension,
                Element->Srb);
            ExFreePool(Element);
        }
        Element = NULL;
    }
Reply:
    KeReleaseSpinLock(&DeviceInformation->RequestListLock, Irql);
    KeAcquireSpinLock(&DeviceInformation->ReplyListLock, &Irql);
    if (IsListEmpty(&DeviceInformation->ReplyListHead))
        goto Exit;

    LIST_FORALL_SAFE(&DeviceInformation->ReplyListHead, ItemLink, ItemNext) {
        Element = CONTAINING_RECORD(ItemLink, SRB_QUEUE_ELEMENT, Link);
        if (Element) {
            RemoveEntryList(&Element->Link);
            if (!Element->Aborted) {
                Element->Srb->DataTransferLength = 0;
                Element->Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
                StorPortNotification(RequestComplete, Element->DeviceExtension,
                    Element->Srb);
            }
            ExFreePool(Element);
            InterlockedDecrement64(&DeviceInformation->Stats.PendingSubmittedIORequests);
        }
        Element = NULL;
    }
Exit:
    KeReleaseSpinLock(&DeviceInformation->ReplyListLock, Irql);
}

_Use_decl_annotations_
NTSTATUS
WnbdDeleteConnection(PGLOBAL_INFORMATION GInfo,
                     PCHAR InstanceName)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);
    ASSERT(InstanceName);
    PUSER_ENTRY EntryMarked = NULL;
    ULONG TargetIndex = 0;
    ULONG BusIndex = 0;
    if (!WnbdFindConnection(GInfo, InstanceName, &EntryMarked)) {
        WNBD_LOG_ERROR("Could not find connection to delete");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (NULL == EntryMarked) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    PSCSI_DEVICE_INFORMATION ScsiInfo = EntryMarked->ScsiInformation;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    TargetIndex = EntryMarked->TargetIndex;
    BusIndex = EntryMarked->BusIndex;

    if (ScsiInfo) {
        ScsiInfo->SoftTerminateDevice = TRUE;
        // TODO: implement proper soft termination.
        ScsiInfo->HardTerminateDevice = TRUE;
        KeSetEvent(&ScsiInfo->TerminateEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSemaphore(&ScsiInfo->DeviceEvent, 0, 1, FALSE);
        LARGE_INTEGER Timeout;
        // TODO: consider making this configurable, currently 120s.
        Timeout.QuadPart = (-120 * 1000 * 10000);
        CloseConnection(ScsiInfo);

        // Ensure that the device isn't currently being accessed.
        ExWaitForRundownProtectionRelease(&ScsiInfo->RundownProtection);

        if (ScsiInfo->UserEntry->Properties.Flags.UseNbd) {
            KeWaitForSingleObject(ScsiInfo->DeviceRequestThread, Executive, KernelMode, FALSE, NULL);
            KeWaitForSingleObject(ScsiInfo->DeviceReplyThread, Executive, KernelMode, FALSE, &Timeout);
            ObDereferenceObject(ScsiInfo->DeviceRequestThread);
            ObDereferenceObject(ScsiInfo->DeviceReplyThread);
        }
        WnbdDrainQueueOnClose(ScsiInfo);
        DisconnectConnection(ScsiInfo);

        if (!WnbdSetDeviceMissing(ScsiInfo->Device,TRUE)) {
            WNBD_LOG_WARN("Could not delete media because it is still in use.");
            return STATUS_UNABLE_TO_UNLOAD_MEDIA;
        }
        StorPortNotification(BusChangeDetected, GInfo->Handle, 0);
    } else {
        WNBD_LOG_ERROR("Could not find device needed for deletion");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Status = WnbdDeleteConnectionEntry(EntryMarked);

    RtlClearBits(&ScsiBitMapHeader, TargetIndex + (BusIndex * SCSI_MAXIMUM_TARGETS_PER_BUS), 1);

    InterlockedDecrement(&GInfo->ConnectionCount);
    WNBD_LOG_LOUD(": Exit");

    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdEnumerateActiveConnections(PGLOBAL_INFORMATION GInfo, PIRP Irp)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);
    ASSERT(Irp);

    PUSER_ENTRY CurrentEntry = (PUSER_ENTRY)GInfo->ConnectionList.Flink;
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

    while ((CurrentEntry != (PUSER_ENTRY) &GInfo->ConnectionList.Flink) && Remaining) {
        OutEntry = &OutList->Connections[OutList->Count];
        RtlZeroMemory(OutEntry, sizeof(WNBD_CONNECTION_INFO));
        RtlCopyMemory(OutEntry, &CurrentEntry->Properties, sizeof(WNBD_PROPERTIES));

        OutEntry->BusNumber = (USHORT)CurrentEntry->BusIndex;
        OutEntry->TargetId = (USHORT)CurrentEntry->TargetIndex;
        OutEntry->Lun = (USHORT)CurrentEntry->LunIndex;

        OutList->Count++;
        Remaining--;

        CurrentEntry = (PUSER_ENTRY)CurrentEntry->ListEntry.Flink;
    }

    if (CurrentEntry == (PUSER_ENTRY) &GInfo->ConnectionList.Flink) {
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
WnbdParseUserIOCTL(PVOID GlobalHandle,
                   PIRP Irp)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Irp);
    ASSERT(GlobalHandle);
    PIO_STACK_LOCATION IoLocation = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;
    PGLOBAL_INFORMATION	GInfo = (PGLOBAL_INFORMATION) GlobalHandle;

    PUSER_ENTRY Device = NULL;
    BOOLEAN RPAcquired = FALSE;

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
        ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);

        WNBD_LOG_INFO("Mapping disk. Name: %s, Serial=%s, BC=%llu, BS=%lu, Pid=%d",
                      Props.InstanceName, Props.SerialNumber,
                      Props.BlockCount, Props.BlockSize, Props.Pid);

        if (WnbdFindConnection(GInfo, Props.InstanceName, NULL)) {
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
            Status = STATUS_FILES_OPEN;
            WNBD_LOG_ERROR("IOCTL_WNBD_CREATE: InstanceName already used.");
            break;
        }

        WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
        Status = WnbdCreateConnection(GInfo, &Props, &ConnectionInfo);

        PWNBD_CONNECTION_INFO OutHandle = (PWNBD_CONNECTION_INFO) Irp->AssociatedIrp.SystemBuffer;
        RtlCopyMemory(OutHandle, &ConnectionInfo, sizeof(WNBD_CONNECTION_INFO));
        Irp->IoStatus.Information = sizeof(WNBD_CONNECTION_INFO);

        WNBD_LOG_LOUD("Mapped disk. Name: %s, connection id: %llu",
                      Props.InstanceName, ConnectionInfo.ConnectionId);

        ExReleaseResourceLite(&GInfo->ConnectionMutex);
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

        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
        if (!WnbdFindConnection(GInfo, RmCmd->InstanceName, NULL)) {
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            WNBD_LOG_ERROR("IOCTL_WNBD_REMOVE: Connection does not exist");
            break;
        }
        WNBD_LOG_LOUD("IOCTL_WNBDVM_UNMAP DeleteConnection");
        Status = WnbdDeleteConnection(GInfo, RmCmd->InstanceName);
        ExReleaseResourceLite(&GInfo->ConnectionMutex);
        KeLeaveCriticalRegion();
        break;

     case IOCTL_WNBD_LIST:
        WNBD_LOG_LOUD("IOCTL_WNBD_LIST");
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
        DWORD RequiredBuffSize = (
            GInfo->ConnectionCount * sizeof(WNBD_CONNECTION_INFO))
            + sizeof(WNBD_CONNECTION_LIST);

        if (!Irp->AssociatedIrp.SystemBuffer ||
            CHECK_O_LOCATION_SZ(IoLocation, RequiredBuffSize))
        {
            WNBD_LOG_ERROR("IOCTL_WNBD_LIST: Bad output buffer");
            Irp->IoStatus.Information = RequiredBuffSize;
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
            break;
        }
        Status = WnbdEnumerateActiveConnections(GInfo, Irp);
        ExReleaseResourceLite(&GInfo->ConnectionMutex);
        KeLeaveCriticalRegion();
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

        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
        if (!Irp->AssociatedIrp.SystemBuffer ||
                CHECK_O_LOCATION(IoLocation, WNBD_DRV_STATS)) {
            WNBD_LOG_ERROR("WNBD_STATS: Bad output buffer");
            Irp->IoStatus.Information = sizeof(WNBD_DRV_STATS);
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
            break;
        }

        PUSER_ENTRY DiskEntry = NULL;
        if (!WnbdFindConnection(GInfo, StatsCmd->InstanceName, &DiskEntry)) {
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            WNBD_LOG_ERROR("WNBD_STATS: Connection does not exist");
            break;
        }

        PWNBD_DRV_STATS OutStatus = (
            PWNBD_DRV_STATS) Irp->AssociatedIrp.SystemBuffer;
        RtlCopyMemory(OutStatus, &DiskEntry->ScsiInformation->Stats,
                      sizeof(WNBD_DRV_STATS));

        Irp->IoStatus.Information = sizeof(WNBD_DRV_STATS);
        ExReleaseResourceLite(&GInfo->ConnectionMutex);
        KeLeaveCriticalRegion();

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

        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
        Device = WnbdFindConnectionEx(GInfo, ReqCmd->ConnectionId);
        // If we can't acquire the rundown protection, it means that the device
        // is being deallocated. Acquiring it guarantees that it won't be deallocated
        // while we're dispatching requests. This doesn't prevent other requests from
        // acquiring it at the same time, which will increase its counter.
        if (Device) {
            RPAcquired = ExAcquireRundownProtection(
                &Device->ScsiInformation->RundownProtection);
        }
        ExReleaseResourceLite(&GInfo->ConnectionMutex);
        KeLeaveCriticalRegion();

        if (!Device || !RPAcquired) {
            Status = STATUS_INVALID_HANDLE;
            WNBD_LOG_ERROR(
                "IOCTL_WNBD_FETCH_REQ: Could not fetch request, invalid connection id: %d.",
                ReqCmd->ConnectionId);
            break;
        }

        Status = WnbdDispatchRequest(Irp, Device->ScsiInformation, ReqCmd);
        Irp->IoStatus.Information = sizeof(WNBD_IOCTL_FETCH_REQ_COMMAND);
        WNBD_LOG_LOUD("Request dispatch status: %d. Request type: %d Request handle: %llx",
                      Status, ReqCmd->Request.RequestType, ReqCmd->Request.RequestHandle);

        KeEnterCriticalRegion();
        ExReleaseRundownProtection(&Device->ScsiInformation->RundownProtection);
        KeLeaveCriticalRegion();
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

        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
        Device = WnbdFindConnectionEx(GInfo, RspCmd->ConnectionId);
        // If we can't acquire the rundown protection, it means that the device
        // is being deallocated. Acquiring it guarantees that it won't be deallocated
        // while we're dispatching requests. This doesn't prevent other requests from
        // acquiring it at the same time, which will increase its counter.
        if (Device) {
            RPAcquired = ExAcquireRundownProtection(
                &Device->ScsiInformation->RundownProtection);
        }
        ExReleaseResourceLite(&GInfo->ConnectionMutex);
        KeLeaveCriticalRegion();

        if (!Device || !RPAcquired) {
            Status = STATUS_INVALID_HANDLE;
            WNBD_LOG_ERROR(
                "IOCTL_WNBD_SEND_RSP: Could not fetch request, invalid connection id: %d.",
                RspCmd->ConnectionId);
            break;
        }

        Status = WnbdHandleResponse(Irp, Device->ScsiInformation, RspCmd);
        WNBD_LOG_LOUD("Reply handling status: %d.", Status);

        KeEnterCriticalRegion();
        ExReleaseRundownProtection(&Device->ScsiInformation->RundownProtection);
        KeLeaveCriticalRegion();
        break;

    default:
        WNBD_LOG_ERROR("Unsupported IOCTL command: %x");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WNBD_LOG_LOUD("Exit: %d", Status);
    return Status;
}
