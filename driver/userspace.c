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
#include "userspace.h"
#include "util.h"

#define CHECK_I_LOCATION(Io, Size) (Io->Parameters.DeviceIoControl.InputBufferLength < sizeof(Size))
#define CHECK_O_LOCATION(Io, Size) (Io->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Size))
#define Malloc(S) ExAllocatePoolWithTag(NonPagedPoolNx, (S), 'DBNu')

extern RTL_BITMAP ScsiBitMapHeader = { 0 };
ULONG AssignedScsiIds[((SCSI_MAXIMUM_TARGETS_PER_BUS / 8) / sizeof(ULONG)) * SCSI_MAXIMUM_BUSES];
static ULONG LunId = 0;
VOID WnbdInitScsiIds()
{
    RtlZeroMemory(AssignedScsiIds, sizeof(AssignedScsiIds));
    RtlInitializeBitMap(&ScsiBitMapHeader, AssignedScsiIds, SCSI_MAXIMUM_TARGETS_PER_BUS * SCSI_MAXIMUM_BUSES);
}

_Use_decl_annotations_
BOOLEAN
WnbdFindConnection(PGLOBAL_INFORMATION GInfo,
                   PUSER_IN Info,
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

NTSTATUS
WnbdInitializeScsiInfo(_In_ PSCSI_DEVICE_INFORMATION ScsiInfo)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(ScsiInfo);
    HANDLE thread_handle;
    NTSTATUS Status = STATUS_SUCCESS;

    InitializeListHead(&ScsiInfo->ListHead);
    KeInitializeSpinLock(&ScsiInfo->ListLock);
    KeInitializeEvent(&ScsiInfo->DeviceEvent, SynchronizationEvent, FALSE);

    ScsiInfo->HardTerminateDevice = FALSE;
    ScsiInfo->SoftTerminateDevice = FALSE;

    Status = PsCreateSystemThread(&thread_handle, (ACCESS_MASK)0L, NULL, NULL, NULL, WnbdDeviceThread, ScsiInfo);

    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = ObReferenceObjectByHandle(thread_handle, THREAD_ALL_ACCESS, NULL, KernelMode,
        &ScsiInfo->DeviceThread, NULL);

    if (!NT_SUCCESS(Status)) {
        ZwClose(thread_handle);
        ScsiInfo->SoftTerminateDevice = TRUE;
        KeSetEvent(&ScsiInfo->DeviceEvent, (KPRIORITY)0, FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

Exit:
    WNBD_LOG_LOUD(": Exit");
    return Status;
}

VOID
WnbdSetNullUserInput(PUSER_IN Info)
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
                     PUSER_IN Info)
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
    RtlCopyMemory(&NewEntry->UserInformation, Info, sizeof(USER_IN));
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
    ULONG BusId = bitNumber / SCSI_MAXIMUM_BUSES;

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

NTSTATUS
WnbdDeleteConnection(_In_ PGLOBAL_INFORMATION GInfo,
                     _In_ PUSER_IN Info)
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
        KeSetEvent(&ScsiInfo->DeviceEvent, (KPRIORITY)0, FALSE);
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = (-1 * 1000 * 10000);
        KeWaitForSingleObject(ScsiInfo->DeviceThread, Executive, KernelMode, FALSE, &Timeout);
        if (-1 != ScsiInfo->Socket) {
            WNBD_LOG_INFO("Closing socket FD: %d", ScsiInfo->Socket);
            Close(ScsiInfo->Socket);
            ScsiInfo->Socket = -1;
        }
        ObDereferenceObject(ScsiInfo->DeviceThread);

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
    PGET_LIST_OUT OutList = (PGET_LIST_OUT) Irp->AssociatedIrp.SystemBuffer;
    PLIST_ENTRY_OUT OutEntry = &OutList->ActiveEntry[0];
    ULONG Remaining;
    NTSTATUS status = STATUS_BUFFER_OVERFLOW;

    OutList->ActiveListCount = 0;

    Remaining = (IoLocation->Parameters.DeviceIoControl.OutputBufferLength -
        sizeof(ULONG))/sizeof(LIST_ENTRY_OUT);

    SearchEntry = (PUSER_ENTRY)GInfo->ConnectionList.Flink;

    while((SearchEntry != (PUSER_ENTRY) &GInfo->ConnectionList.Flink) && Remaining) {
        OutEntry = &OutList->ActiveEntry[OutList->ActiveListCount];
        RtlZeroMemory(OutEntry, sizeof(LIST_ENTRY_OUT));
        RtlCopyMemory(OutEntry, &SearchEntry->UserInformation, sizeof(USER_IN));

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

    Irp->IoStatus.Information = (OutList->ActiveListCount * sizeof(LIST_ENTRY_OUT)) + sizeof(ULONG);
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
        PUSER_COMMAND Code = (PUSER_COMMAND)Irp->AssociatedIrp.SystemBuffer;
        if (NULL == Code || CHECK_I_LOCATION(IoLocation, USER_COMMAND)) {
            WNBD_LOG_ERROR(": IOCTL = 0x%x. Bad input buffer",
                IoLocation->Parameters.DeviceIoControl.IoControlCode);
            return STATUS_INVALID_PARAMETER;
        }

        switch(Code->IoCode) {

        case IOCTL_WNBDVM_PORT:
            {
            WNBD_LOG_LOUD("IOCTL_WNBDVMPORT_SCSIPORT");
            Status = STATUS_SUCCESS;
            }
            break;

        case IOCTL_WNBDVM_MAP:
            {
            WNBD_LOG_LOUD("IOCTL_WNBDVM_MAP");
            PUSER_IN Info = (PUSER_IN) Irp->AssociatedIrp.SystemBuffer;
            if(NULL == Info || CHECK_I_LOCATION(IoLocation, USER_IN)) {
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

        case IOCTL_WNBDVM_UNMAP:
            {
            WNBD_LOG_LOUD("IOCTL_WNBDVM_UNMAP");
            PUSER_IN Info = (PUSER_IN) Irp->AssociatedIrp.SystemBuffer;

            if(!Info || CHECK_I_LOCATION(IoLocation, USER_IN)) {
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

        case IOCTL_WNBDVM_LIST:
            {
            WNBD_LOG_LOUD("IOCTL_WNBDVM_LIST");
            if(!Irp->AssociatedIrp.SystemBuffer || CHECK_O_LOCATION(IoLocation, GET_LIST_OUT)) {
                WNBD_LOG_ERROR(": IOCTL = 0x%x. Bad output buffer",
                    IoLocation->Parameters.DeviceIoControl.IoControlCode);
                break;
            }
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&GInfo->ConnectionMutex, TRUE);
            Status = WnbdEnumerateActiveConnections(GInfo, Irp);
            ExReleaseResourceLite(&GInfo->ConnectionMutex);
            KeLeaveCriticalRegion();
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
