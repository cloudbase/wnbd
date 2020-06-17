/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <berkeley.h>
#include <ksocket.h>
#include "common.h"
#include "debug.h"
#include "driver_extension.h"
#include "rbd_protocol.h"
#include "scsi_function.h"
#include "userspace.h"
#include "util.h"

#define CHECK_I_LOCATION(Io, Size) (Io->Parameters.DeviceIoControl.InputBufferLength < sizeof(Size))
#define CHECK_O_LOCATION(Io, Size) (Io->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Size))
#define Malloc(S) ExAllocatePoolWithTag(NonPagedPoolNx, (S), 'DBNu')

extern UNICODE_STRING GlobalRegistryPath;

extern RTL_BITMAP ScsiBitMapHeader = { 0 };
ULONG AssignedScsiIds[((SCSI_MAXIMUM_TARGETS_PER_BUS / 8) / sizeof(ULONG)) * MAX_NUMBER_OF_SCSI_TARGETS];
static ULONG LunId = 0;
VOID WnbdInitScsiIds()
{
    RtlZeroMemory(AssignedScsiIds, sizeof(AssignedScsiIds));
    RtlInitializeBitMap(&ScsiBitMapHeader, AssignedScsiIds, SCSI_MAXIMUM_TARGETS_PER_BUS * MAX_NUMBER_OF_SCSI_TARGETS);
}

_Use_decl_annotations_
BOOLEAN
WnbdFindConnection(PGLOBAL_INFORMATION GInfo,
                   PCONNECTION_INFO Info,
                   PUSER_ENTRY* Entry)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);
    ASSERT(Info);

    BOOLEAN Found = FALSE;
    PUSER_ENTRY SearchEntry;

    SearchEntry = (PUSER_ENTRY)GInfo->ConnectionList.Flink;

    while (SearchEntry != (PUSER_ENTRY)&GInfo->ConnectionList.Flink) {
        if (!strcmp((CONST CHAR*)&SearchEntry->UserInformation.InstanceName, (CONST CHAR*)&Info->InstanceName)) {
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

    if(LuExt) {
        WNBD_LOG_ERROR(": LU extension %p already found for %d:%d:%d", LuExt, PathId, TargetId, Lun);
        return NULL;
    }

    Dev = (PWNBD_SCSI_DEVICE) ExAllocatePoolWithTag(NonPagedPoolNx,sizeof(WNBD_SCSI_DEVICE),'DBNs');

    if(!Dev) {
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

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Ext->DeviceResourceLock, TRUE);

    InsertTailList(&Ext->DeviceList, &Dev->ListEntry);

    ExReleaseResourceLite(&Ext->DeviceResourceLock);
    KeLeaveCriticalRegion();

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
WnbdInitializeScsiInfo(_In_ PSCSI_DEVICE_INFORMATION ScsiInfo)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(ScsiInfo);
    HANDLE request_thread_handle = NULL, reply_thread_handle = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    InitializeListHead(&ScsiInfo->RequestListHead);
    KeInitializeSpinLock(&ScsiInfo->RequestListLock);
    KeInitializeSemaphore(&ScsiInfo->RequestSemaphore,
                          WNBD_MAX_IN_FLIGHT_REQUESTS,
                          WNBD_MAX_IN_FLIGHT_REQUESTS);
    InitializeListHead(&ScsiInfo->ReplyListHead);
    KeInitializeSpinLock(&ScsiInfo->ReplyListLock);
    KeInitializeSemaphore(&ScsiInfo->DeviceEvent, 0, 1 << 30);
    Status = ExInitializeResourceLite(&ScsiInfo->SocketLock);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    ScsiInfo->HardTerminateDevice = FALSE;
    ScsiInfo->SoftTerminateDevice = FALSE;
    ScsiInfo->ReadPreallocatedBuffer = MallocT(((UINT)WNBD_PREALLOC_BUFF_SZ));
    if (!ScsiInfo->ReadPreallocatedBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    ScsiInfo->ReadPreallocatedBufferLength = WNBD_PREALLOC_BUFF_SZ;
    ScsiInfo->WritePreallocatedBuffer = MallocT(((UINT)WNBD_PREALLOC_BUFF_SZ));
    if (!ScsiInfo->WritePreallocatedBuffer) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    ScsiInfo->WritePreallocatedBufferLength = WNBD_PREALLOC_BUFF_SZ;

    Status = PsCreateSystemThread(&request_thread_handle, (ACCESS_MASK)0L, NULL,
                                  NULL, NULL, WnbdDeviceRequestThread, ScsiInfo);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
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

    RtlZeroMemory(&ScsiInfo->Stats, sizeof(WNBD_STATS));

    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto SoftTerminate;
    }

Exit:
    WNBD_LOG_LOUD(": Exit");
    return Status;

SoftTerminate:
    if(request_thread_handle)
        ZwClose(request_thread_handle);
    if(reply_thread_handle)
        ZwClose(reply_thread_handle);
    ScsiInfo->SoftTerminateDevice = TRUE;
    WnbdReleaseSemaphore(&ScsiInfo->DeviceEvent, 0, 1, FALSE);
    Status = STATUS_INSUFFICIENT_RESOURCES;
    goto Exit;
}

VOID
WnbdSetNullUserInput(PCONNECTION_INFO Info)
{
    Info->InstanceName[MAX_NAME_LENGTH - 1] = '\0';
    Info->Hostname[MAX_NAME_LENGTH - 1] = '\0';
    Info->PortName[MAX_NAME_LENGTH - 1] = '\0';
    Info->ExportName[MAX_NAME_LENGTH - 1] = '\0';
    Info->SerialNumber[MAX_NAME_LENGTH - 1] = '\0';
}

_Use_decl_annotations_
NTSTATUS
WnbdCreateConnection(PGLOBAL_INFORMATION GInfo,
                     PCONNECTION_INFO Info)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);
    ASSERT(Info);

    NTSTATUS Status = STATUS_UNSUCCESSFUL;
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

    WnbdSetNullUserInput(Info);

    if(WnbdFindConnection(GInfo, Info, NULL)) {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto Exit;
    }

    RtlZeroMemory(NewEntry,sizeof(USER_ENTRY));
    RtlCopyMemory(&NewEntry->UserInformation, Info, sizeof(CONNECTION_INFO));
    InsertTailList(&GInfo->ConnectionList, &NewEntry->ListEntry);
    Added = TRUE;

    // TODO FOR SN MAYBE ?? status = ExUuidCreate(&tmpGuid);

    PINQUIRYDATA InquiryData = (PINQUIRYDATA) Malloc(sizeof(INQUIRYDATA));
    if (NULL == InquiryData) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    WnbdSetInquiryData(InquiryData);

    Status = STATUS_SUCCESS;
    WNBD_LOG_LOUD("Received disk size: %llu", Info->DiskSize);
    NewEntry->DiskSize = Info->DiskSize;
    NewEntry->BlockSize = LOGICAL_BLOCK_SIZE;
    if (Info->BlockSize) {
        WNBD_LOG_LOUD("Received block size: %u", Info->BlockSize);
        NewEntry->BlockSize = Info->BlockSize;
    }

    Sock = NbdOpenAndConnect(Info->Hostname, Info->PortName);
    if (-1 == Sock) {
        Status = STATUS_CONNECTION_REFUSED;
        goto ExitInquiryData;
    }

    ULONG bitNumber = RtlFindClearBitsAndSet(&ScsiBitMapHeader, 1, 0);

    if (0xFFFFFFFF == bitNumber) {
        Status = STATUS_INVALID_FIELD_IN_PARAMETER_LIST;
        goto ExitInquiryData;
    }

    if (Info->MustNegotiate) {
        WNBD_LOG_INFO("Trying to negotiate handshake with RBD Server");
        Info->DiskSize = 0;
        UINT16 RbdFlags = 0;
        Status = RbdNegotiate(&Sock, &Info->DiskSize, &RbdFlags, Info->ExportName, 1, 1);
        if (!NT_SUCCESS(Status)) {
            goto ExitInquiryData;
        }
        WNBD_LOG_INFO("Negotiated disk size: %llu", Info->DiskSize);
        NewEntry->DiskSize = Info->DiskSize;
    }
    ULONG TargetId = bitNumber % SCSI_MAXIMUM_TARGETS_PER_BUS;
    ULONG BusId = bitNumber / MAX_NUMBER_OF_SCSI_TARGETS;

    PSCSI_DEVICE_INFORMATION ScsiInfo = (PSCSI_DEVICE_INFORMATION) Malloc(sizeof(SCSI_DEVICE_INFORMATION));
    if(!ScsiInfo) {
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

    Status = WnbdInitializeScsiInfo(ScsiInfo);
    if (!NT_SUCCESS(Status)) {
        goto ExitScsiInfo;
    }

    ScsiInfo->UserEntry = NewEntry;
    ScsiInfo->TargetIndex = TargetId;
    ScsiInfo->BusIndex = BusId;
    ScsiInfo->LunIndex = LunId;

    NewEntry->ScsiInformation = ScsiInfo;
    NewEntry->BusIndex = BusId;
    NewEntry->TargetIndex = TargetId;
    NewEntry->LunIndex = LunId;

    InterlockedIncrement(&GInfo->ConnectionCount);
    StorPortNotification(BusChangeDetected, GInfo->Handle, 0);

    NewEntry->Connected = TRUE;
    Status = STATUS_SUCCESS;

    WNBD_LOG_LOUD(": Exit");

    return Status;

ExitScsiInfo:
    if (ScsiInfo) {
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

    if(Device->OutstandingIoCount && !Force) {
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
    if (IsListEmpty(&DeviceInformation->RequestListHead))
        goto Reply;
    PLIST_ENTRY ItemLink, ItemNext;
    KIRQL Irql = { 0 };
    PSRB_QUEUE_ELEMENT Element = NULL;
    KeAcquireSpinLock(&DeviceInformation->RequestListLock, &Irql);
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
    KeReleaseSpinLock(&DeviceInformation->RequestListLock, Irql);
Reply:
    if (IsListEmpty(&DeviceInformation->ReplyListHead))
        return;
    KeAcquireSpinLock(&DeviceInformation->ReplyListLock, &Irql);
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
    KeReleaseSpinLock(&DeviceInformation->ReplyListLock, Irql);
}

_Use_decl_annotations_
NTSTATUS
WnbdDeleteConnection(PGLOBAL_INFORMATION GInfo,
                     PCONNECTION_INFO Info)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);
    ASSERT(Info);
    PUSER_ENTRY EntryMarked = NULL;
    ULONG TargetIndex = 0;
    ULONG BusIndex = 0;
    if(!WnbdFindConnection(GInfo, Info, &EntryMarked)) {
        WNBD_LOG_ERROR("Could not find connection to delete");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (NULL == EntryMarked) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    PSCSI_DEVICE_INFORMATION ScsiInfo = EntryMarked->ScsiInformation;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if(ScsiInfo) {
        TargetIndex = ScsiInfo->TargetIndex;
        BusIndex = ScsiInfo->BusIndex;
        ScsiInfo->SoftTerminateDevice = TRUE;
        WnbdReleaseSemaphore(&ScsiInfo->DeviceEvent, 0, 1, FALSE);
        LARGE_INTEGER Timeout;
        // TODO: consider making this configurable, currently 120s.
        Timeout.QuadPart = (-120 * 1000 * 10000);
        CloseConnection(ScsiInfo);
        KeWaitForSingleObject(ScsiInfo->DeviceRequestThread, Executive, KernelMode, FALSE, NULL);
        KeWaitForSingleObject(ScsiInfo->DeviceReplyThread, Executive, KernelMode, FALSE, &Timeout);
        ObDereferenceObject(ScsiInfo->DeviceRequestThread);
        ObDereferenceObject(ScsiInfo->DeviceReplyThread);
        WnbdDrainQueueOnClose(ScsiInfo);
        DisconnectConnection(ScsiInfo);

        if(!WnbdSetDeviceMissing(ScsiInfo->Device,TRUE)) {
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
WnbdEnumerateActiveConnections(PGLOBAL_INFORMATION GInfo,
                               PIRP Irp)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(GInfo);
    ASSERT(Irp);

    PUSER_ENTRY SearchEntry;
    PIO_STACK_LOCATION IoLocation = IoGetCurrentIrpStackLocation(Irp);
    PDISK_INFO_LIST OutList = (PDISK_INFO_LIST) Irp->AssociatedIrp.SystemBuffer;
    PDISK_INFO OutEntry = &OutList->ActiveEntry[0];
    ULONG Remaining;
    NTSTATUS status = STATUS_BUFFER_OVERFLOW;

    OutList->ActiveListCount = 0;

    Remaining = (IoLocation->Parameters.DeviceIoControl.OutputBufferLength -
        sizeof(ULONG))/sizeof(DISK_INFO);

    SearchEntry = (PUSER_ENTRY)GInfo->ConnectionList.Flink;

    while((SearchEntry != (PUSER_ENTRY) &GInfo->ConnectionList.Flink) && Remaining) {
        OutEntry = &OutList->ActiveEntry[OutList->ActiveListCount];
        RtlZeroMemory(OutEntry, sizeof(DISK_INFO));
        RtlCopyMemory(OutEntry, &SearchEntry->UserInformation, sizeof(CONNECTION_INFO));

        OutEntry->BusNumber = (USHORT)SearchEntry->BusIndex;
        OutEntry->TargetId = (USHORT)SearchEntry->TargetIndex;
        OutEntry->Lun = (USHORT)SearchEntry->LunIndex;
        OutEntry->Connected = SearchEntry->Connected;
        OutEntry->DiskSize = SearchEntry->UserInformation.DiskSize;

        WNBD_LOG_INFO(": %d:%d:%d Connected: %d",
            SearchEntry->BusIndex, SearchEntry->TargetIndex, SearchEntry->LunIndex,
            OutEntry->Connected);

        OutList->ActiveListCount++;
        Remaining--;

        SearchEntry = (PUSER_ENTRY)SearchEntry->ListEntry.Flink;
    }

    if(SearchEntry == (PUSER_ENTRY) &GInfo->ConnectionList.Flink) {
        status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Information = (OutList->ActiveListCount * sizeof(DISK_INFO)) + sizeof(ULONG);
    WNBD_LOG_LOUD(": Exit");

    return status;
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

    WNBD_LOG_LOUD(": DeviceIoControl = 0x%x.",
                  IoLocation->Parameters.DeviceIoControl.IoControlCode);

    switch (IoLocation->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_MINIPORT_PROCESS_SERVICE_IRP:
        {
        PWNBD_COMMAND Code = (PWNBD_COMMAND)Irp->AssociatedIrp.SystemBuffer;
        if (NULL == Code || CHECK_I_LOCATION(IoLocation, WNBD_COMMAND)) {
            WNBD_LOG_ERROR(": IOCTL = 0x%x. Bad input buffer",
                IoLocation->Parameters.DeviceIoControl.IoControlCode);
            return STATUS_INVALID_PARAMETER;
        }

        switch(Code->IoCode) {

        case IOCTL_WNBD_PORT:
            {
            WNBD_LOG_LOUD("IOCTL_WNBDVMPORT_SCSIPORT");
            Status = STATUS_SUCCESS;
            }
            break;

        case IOCTL_WNBD_MAP:
            {
            WNBD_LOG_LOUD("IOCTL_WNBDVM_MAP");
            PCONNECTION_INFO Info = (PCONNECTION_INFO) Irp->AssociatedIrp.SystemBuffer;
            if(NULL == Info || CHECK_I_LOCATION(IoLocation, CONNECTION_INFO)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. Bad input buffer",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if(!wcslen((PWSTR) &Info->InstanceName)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. InstanceName Error",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);

            if(WnbdFindConnection(GInfo, Info, NULL)) {
                ExReleaseResourceLite(&GInfo->ConnectionMutex);
                KeLeaveCriticalRegion();
                Status = STATUS_FILES_OPEN;
                WNBD_LOG_ERROR(": IOCTL = 0x%x. EEXIST",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                break;
            }
            WNBD_LOG_LOUD("IOCTL_WNBDVM_MAP CreateConnection");
            Status = WnbdCreateConnection(GInfo,Info);
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
            }
            break;

        case IOCTL_WNBD_UNMAP:
            {
            WNBD_LOG_LOUD("IOCTL_WNBDVM_UNMAP");
            PCONNECTION_INFO Info = (PCONNECTION_INFO) Irp->AssociatedIrp.SystemBuffer;

            if(!Info || CHECK_I_LOCATION(IoLocation, CONNECTION_INFO)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. Bad input buffer",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if(!wcslen((PWSTR) &Info->InstanceName)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. InstanceName Error",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
            if(!WnbdFindConnection(GInfo, Info, NULL)) {
                ExReleaseResourceLite(&GInfo->ConnectionMutex);
                KeLeaveCriticalRegion();
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                WNBD_LOG_ERROR(": IOCTL = 0x%x. Connection does not exist",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                break;
            }
            WNBD_LOG_LOUD("IOCTL_WNBDVM_UNMAP DeleteConnection");
            Status = WnbdDeleteConnection(GInfo, Info);
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
            }
            break;

        case IOCTL_WNBD_LIST:
            {
            WNBD_LOG_LOUD("IOCTL_WNBDVM_LIST");
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
            if(!Irp->AssociatedIrp.SystemBuffer || CHECK_O_LOCATION(IoLocation, DISK_INFO_LIST)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. Bad output buffer",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);

                Irp->IoStatus.Information = (GInfo->ConnectionCount * sizeof(DISK_INFO) ) + sizeof(DISK_INFO_LIST);
                ExReleaseResourceLite(&GInfo->ConnectionMutex);
                KeLeaveCriticalRegion();
                break;
            }
            Status = WnbdEnumerateActiveConnections(GInfo, Irp);
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
            }
            break;

        case IOCTL_WNBD_DEBUG:
        {
            WNBD_LOG_LOUD("IOCTL_WNBDVM_DEBUG");
            WCHAR* DebugKey = L"DebugLogLevel";
            UINT32 temp = 0;

            if (WNBDReadRegistryValue(&GlobalRegistryPath, DebugKey,
                (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT), &temp)) {
                WnbdSetLogLevel(temp);
            }
        }
        break;

        case IOCTL_WNBD_STATS:
        {
            // Retrieve per mapping stats. TODO: consider providing global stats.
            WNBD_LOG_LOUD("IOCTL_WNBDVM_STATS");
            PCONNECTION_INFO Info = (PCONNECTION_INFO) Irp->AssociatedIrp.SystemBuffer;

            if(!Info || CHECK_I_LOCATION(IoLocation, CONNECTION_INFO)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. Bad input buffer",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if(!wcslen((PWSTR) &Info->InstanceName)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. InstanceName Error",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
            if(!Irp->AssociatedIrp.SystemBuffer || CHECK_O_LOCATION(IoLocation, WNBD_STATS)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. Bad output buffer",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);

                Irp->IoStatus.Information = sizeof(WNBD_STATS);
                ExReleaseResourceLite(&GInfo->ConnectionMutex);
                KeLeaveCriticalRegion();
                break;
            }

            PUSER_ENTRY DiskEntry = NULL;
            if(!WnbdFindConnection(GInfo, Info, &DiskEntry)) {
                ExReleaseResourceLite(&GInfo->ConnectionMutex);
                KeLeaveCriticalRegion();
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                WNBD_LOG_ERROR(": IOCTL = 0x%x. Connection does not exist",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                break;
            }

            PWNBD_STATS OutStatus = (PWNBD_STATS) Irp->AssociatedIrp.SystemBuffer;
            RtlCopyMemory(OutStatus, &DiskEntry->ScsiInformation->Stats, sizeof(WNBD_STATS));

            Irp->IoStatus.Information = sizeof(WNBD_STATS);
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();

            Status = STATUS_SUCCESS;
        }
        break;

        default:
            {
            WNBD_LOG_ERROR("ScsiPortDeviceControl: Unsupported IOCTL (%x)",
                IoLocation->Parameters.DeviceIoControl.IoControlCode);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            }
            break;
        }
        }
        break;

    default:
        WNBD_LOG_ERROR("Unsupported IOCTL (%x)",
                      IoLocation->Parameters.DeviceIoControl.IoControlCode);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WNBD_LOG_LOUD(": Exit");

    return Status;
}
