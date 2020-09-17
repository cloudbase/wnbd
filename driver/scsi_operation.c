/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "common.h"
#include "debug.h"
#include "scsi_operation.h"
#include "scsi_function.h"
#include "scsi_trace.h"
#include "srb_helper.h"
#include "userspace.h"
#include "util.h"

UCHAR
WnbdReadCapacity(_In_ PWNBD_SCSI_DEVICE Device,
                 _In_ PSCSI_REQUEST_BLOCK Srb,
                 _In_ PCDB Cdb,
                 _In_ UINT BlockSize,
                 _In_ UINT64 BlockCount)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Srb);
    ASSERT(Cdb);
    WNBD_LOG_INFO("Using BlockSize: %u, BlockCount: %llu", BlockSize, BlockCount);

    PVOID DataBuffer = SrbGetDataBuffer(Srb);
    ULONG DataTransferLength = SrbGetDataTransferLength(Srb);
    UCHAR SrbStatus = SRB_STATUS_INVALID_REQUEST;

    if (0 == DataBuffer || 0 == BlockCount) {
        SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        goto Exit;
    }
    UINT64 maxLba = BlockCount - 1;

    RtlZeroMemory(DataBuffer, DataTransferLength);

    switch (Cdb->CDB10.OperationCode)
    {
    case SCSIOP_READ_CAPACITY:
        {
        if (sizeof(READ_CAPACITY_DATA) > DataTransferLength) {
            goto Exit;
        }

        PREAD_CAPACITY_DATA ReadCapacityData = DataBuffer;

        if (maxLba >= MAXULONG) {
            ReadCapacityData->LogicalBlockAddress = MAXULONG;
        }
        else {
            REVERSE_BYTES_4(&ReadCapacityData->LogicalBlockAddress, &maxLba);
        }

        REVERSE_BYTES_4(&ReadCapacityData->BytesPerBlock, &BlockSize);
        SrbSetDataTransferLength(Srb, sizeof(READ_CAPACITY_DATA));
        SrbStatus = SRB_STATUS_SUCCESS;
        }
        break;
    case SCSIOP_READ_CAPACITY16:
        {
        if (Cdb->READ_CAPACITY16.ServiceAction != SERVICE_ACTION_READ_CAPACITY16) {
            goto Exit;
        }

        if (sizeof(READ_CAPACITY_DATA_EX) > DataTransferLength) {
            goto Exit;
        }

        ULONG ReturnDataLength = 0;
        PREAD_CAPACITY16_DATA ReadCapacityData16 = DataBuffer;
        REVERSE_BYTES_8(&ReadCapacityData16->LogicalBlockAddress, &maxLba);
        REVERSE_BYTES_4(&ReadCapacityData16->BytesPerBlock, &BlockSize);

        if (DataTransferLength >= (ULONG)FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3)) {
            if (Device->Properties.Flags.UnmapSupported) {
                ReadCapacityData16->LBPME = 1;
            }

            if (DataTransferLength >= sizeof(READ_CAPACITY16_DATA)) {
                ReturnDataLength = sizeof(READ_CAPACITY16_DATA);
            }
            else {
                ReturnDataLength = FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3);
            }
        }
        else {
            ReturnDataLength = sizeof(READ_CAPACITY_DATA_EX);
        }

        SrbSetDataTransferLength(Srb, ReturnDataLength);
        SrbStatus = SRB_STATUS_SUCCESS;
        }
        break;
    default:
        WNBD_LOG_ERROR("Unknown read capacity operation: %u", Cdb->CDB10.OperationCode);
        break;
    }

Exit:
    WNBD_LOG_LOUD(": Exit");

    return SrbStatus;
}

UCHAR
WnbdSetVpdSupportedPages(_In_ PVOID Data,
                         _In_ UCHAR NumberOfSupportedPages,
                         _In_ PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Data);
    ASSERT(Srb);

    PVPD_SUPPORTED_PAGES_PAGE VpdPages;

    VpdPages = Data;
    VpdPages->DeviceType = DIRECT_ACCESS_DEVICE;
    VpdPages->DeviceTypeQualifier = DEVICE_QUALIFIER_ACTIVE;
    VpdPages->PageCode = VPD_SUPPORTED_PAGES;
    VpdPages->PageLength = NumberOfSupportedPages;
    VpdPages->SupportedPageList[0] = VPD_SUPPORTED_PAGES;
    VpdPages->SupportedPageList[1] = VPD_SERIAL_NUMBER;
    VpdPages->SupportedPageList[2] = VPD_BLOCK_LIMITS;
    VpdPages->SupportedPageList[3] = VPD_BLOCK_DEVICE_CHARACTERISTICS;
    VpdPages->SupportedPageList[4] = VPD_LOGICAL_BLOCK_PROVISIONING;

    SrbSetDataTransferLength(Srb, sizeof(VPD_SUPPORTED_PAGES_PAGE) + NumberOfSupportedPages);

    WNBD_LOG_LOUD(": Exit");
    return SRB_STATUS_SUCCESS;
}

UCHAR
WnbdSetVpdSerialNumber(_In_ PVOID Data,
                       _In_ PCHAR DeviceSerial,
                       _In_ PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Data);
    ASSERT(Srb);

    PVPD_SERIAL_NUMBER_PAGE VpdSerial;
    UCHAR Size = (UCHAR)strlen(DeviceSerial) + 1;
    WNBD_LOG_INFO("DeviceSerial: %s, size: %d", DeviceSerial, Size);

    VpdSerial = Data;
    VpdSerial->DeviceType = DIRECT_ACCESS_DEVICE;
    VpdSerial->DeviceTypeQualifier = DEVICE_QUALIFIER_ACTIVE;
    VpdSerial->PageCode = VPD_SERIAL_NUMBER;
    VpdSerial->PageLength = Size;
    RtlCopyMemory(VpdSerial->SerialNumber, DeviceSerial, Size);

    SrbSetDataTransferLength(Srb, sizeof(VPD_SERIAL_NUMBER_PAGE) + Size);

    WNBD_LOG_LOUD(": Exit");
    return SRB_STATUS_SUCCESS;
}

VOID
WnbdSetVpdBlockLimits(_In_ PVOID Data,
                      _In_ PWNBD_SCSI_DEVICE Device,
                      _In_ PSCSI_REQUEST_BLOCK Srb,
                      _In_ UINT32 MaximumTransferLength)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Data);
    ASSERT(Srb);

    PVPD_BLOCK_LIMITS_PAGE BlockLimits = Data;

    BlockLimits->DeviceType = DIRECT_ACCESS_DEVICE;
    BlockLimits->DeviceTypeQualifier = DEVICE_QUALIFIER_ACTIVE;
    BlockLimits->PageCode = VPD_BLOCK_LIMITS;
    BlockLimits->PageLength[1] = sizeof(VPD_BLOCK_LIMITS_PAGE) -
        RTL_SIZEOF_THROUGH_FIELD(VPD_BLOCK_LIMITS_PAGE, PageLength);

    UINT32 MaximumTransferBlocks = MaximumTransferLength / Device->Properties.BlockSize;
    REVERSE_BYTES_4(&BlockLimits->MaximumTransferLength,
                    &MaximumTransferBlocks);
    if (Device->Properties.Flags.UnmapSupported)
    {
        // To keep it simple, we'll have one UNMAP descriptor per SRB.
        // TODO: allow passing multiple unmap descriptors.
        UINT32 MaximumUnmapBlockDescCount = 1;
        UINT32 MaximumUnmapLBACount = 0xffffffff;
        REVERSE_BYTES_4(&BlockLimits->MaximumUnmapLBACount, &MaximumUnmapLBACount);
        REVERSE_BYTES_4(&BlockLimits->MaximumUnmapBlockDescriptorCount,
                        &MaximumUnmapBlockDescCount);
    }

    SrbSetDataTransferLength(Srb, sizeof(VPD_BLOCK_LIMITS_PAGE));

    WNBD_LOG_LOUD(": Exit");
}

VOID
WnbdSetVpdLogicalBlockProvisioning(
    _In_ PVOID Data,
    _In_ PWNBD_SCSI_DEVICE Device,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");

    PVPD_LOGICAL_BLOCK_PROVISIONING_PAGE LogicalBlockProvisioning = Data;
    LogicalBlockProvisioning->DeviceType = DIRECT_ACCESS_DEVICE;
    LogicalBlockProvisioning->DeviceTypeQualifier = DEVICE_QUALIFIER_ACTIVE;
    LogicalBlockProvisioning->PageCode = VPD_LOGICAL_BLOCK_PROVISIONING;
    LogicalBlockProvisioning->PageLength[1] = sizeof(VPD_LOGICAL_BLOCK_PROVISIONING_PAGE) -
        RTL_SIZEOF_THROUGH_FIELD(VPD_LOGICAL_BLOCK_PROVISIONING_PAGE, PageLength);
    if (Device->Properties.Flags.UnmapSupported)
    {
        LogicalBlockProvisioning->LBPU = 1;
        // TODO: trim support doesn't imply thin provisioning, but there
        // might be some assumptions. Check if we actually have to set this.
        LogicalBlockProvisioning->ProvisioningType = PROVISIONING_TYPE_THIN;
    }

    SrbSetDataTransferLength(Srb, sizeof(VPD_LOGICAL_BLOCK_PROVISIONING_PAGE));

    WNBD_LOG_LOUD(": Exit");
}

VOID
WnbdSetVpdBlockDeviceCharacteristics(
    _In_ PVOID Data,
    _In_ PWNBD_SCSI_DEVICE Device,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");

    PVPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE CharacteristicsPage = Data;

    CharacteristicsPage->DeviceType = DIRECT_ACCESS_DEVICE;
    CharacteristicsPage->DeviceTypeQualifier = DEVICE_CONNECTED;
    CharacteristicsPage->PageCode = VPD_BLOCK_DEVICE_CHARACTERISTICS;
    CharacteristicsPage->PageLength = 0x3C;
    CharacteristicsPage->MediumRotationRateMsb = 0;
    CharacteristicsPage->MediumRotationRateLsb = 0;
    CharacteristicsPage->NominalFormFactor = 0;
    CharacteristicsPage->FUAB = !!Device->Properties.Flags.FUASupported;

    SrbSetDataTransferLength(Srb, sizeof(VPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE));

    WNBD_LOG_LOUD(": Exit");
}

UCHAR
WnbdProcessExtendedInquiry(_In_ PVOID Data,
                           _In_ ULONG Length,
                           _In_ PSCSI_REQUEST_BLOCK Srb,
                           _In_ PCDB Cdb,
                           _In_ PWNBD_SCSI_DEVICE Device)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Data);
    ASSERT(Srb);
    ASSERT(Cdb);

    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    UCHAR NumberOfSupportedPages = 5;
    UCHAR MaxSerialNumberSize = sizeof(VPD_SERIAL_NUMBER_PAGE) + sizeof(UCHAR[36]);
    UCHAR MaxVpdSuportedPage = sizeof(VPD_SUPPORTED_PAGES_PAGE) + NumberOfSupportedPages;

    switch (Cdb->CDB6INQUIRY3.PageCode) {
    case VPD_SUPPORTED_PAGES:
        if (MaxVpdSuportedPage > Length) {
            SrbStatus = SRB_STATUS_DATA_OVERRUN;
            goto Exit;
        }
        SrbStatus = WnbdSetVpdSupportedPages(Data, NumberOfSupportedPages, Srb);
        break;

    case VPD_SERIAL_NUMBER:
        {
        if (MaxSerialNumberSize > Length) {
            SrbStatus = SRB_STATUS_DATA_OVERRUN;
            goto Exit;
        }
        SrbStatus = WnbdSetVpdSerialNumber(Data,
                                           Device->Properties.SerialNumber,
                                           Srb);
        }
        break;
    case VPD_BLOCK_LIMITS:
        if (sizeof(VPD_BLOCK_LIMITS_PAGE) > Length)
            return SRB_STATUS_DATA_OVERRUN;

        WnbdSetVpdBlockLimits(Data, Device, Srb, WNBD_DEFAULT_MAX_TRANSFER_LENGTH);
        break;
    case VPD_LOGICAL_BLOCK_PROVISIONING:
        if (sizeof(VPD_LOGICAL_BLOCK_PROVISIONING_PAGE) > Length)
            return SRB_STATUS_DATA_OVERRUN;

        WnbdSetVpdLogicalBlockProvisioning(Data, Device, Srb);
        break;
    case VPD_BLOCK_DEVICE_CHARACTERISTICS:
        if (sizeof(VPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE) > Length)
            return SRB_STATUS_DATA_OVERRUN;

        WnbdSetVpdBlockDeviceCharacteristics(Data, Device, Srb);
        break;
    default:
        WNBD_LOG_ERROR("Unknown VPD Page: %u", Cdb->CDB6INQUIRY3.PageCode);
        break;
    }

Exit:
    WNBD_LOG_LOUD(": Exit");
    return SrbStatus;
}

UCHAR
WnbdInquiry(_In_ PWNBD_SCSI_DEVICE Device,
            _In_ PSCSI_REQUEST_BLOCK Srb,
            _In_ PCDB Cdb)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Srb);
    ASSERT(Cdb);

    PVOID DataBuffer = SrbGetDataBuffer(Srb);
    ULONG DataTransferLength = SrbGetDataTransferLength(Srb);
    UCHAR SrbStatus = SRB_STATUS_INTERNAL_ERROR;

    if (NULL == DataBuffer) {
        SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        goto Exit;
    }

    RtlZeroMemory(DataBuffer, DataTransferLength);

    switch (Cdb->CDB6INQUIRY3.EnableVitalProductData) {
    case 0:
        WNBD_LOG_INFO("Normal Inquiry");
        if (0 != Cdb->CDB6INQUIRY3.PageCode) {
            SrbStatus = SRB_STATUS_INTERNAL_ERROR;
            goto Exit;
        }

        RtlCopyMemory(DataBuffer, Device->InquiryData, INQUIRYDATABUFFERSIZE);
        SrbSetDataTransferLength(Srb, INQUIRYDATABUFFERSIZE);
        SrbStatus = SRB_STATUS_SUCCESS;
        break;
    default:
        WNBD_LOG_INFO("Extended Inquiry");
        SrbStatus = WnbdProcessExtendedInquiry(DataBuffer, DataTransferLength, Srb, Cdb, Device);
        break;
    }

Exit:
    WNBD_LOG_LOUD(": Exit");
    return SrbStatus;
}

UCHAR
WnbdSetModeSense(_In_ PVOID Data,
                 _In_ PCDB Cdb,
                 _In_ ULONG MaxLength,
                 _In_ BOOLEAN ReadOnly,
                 _In_ BOOLEAN AcceptFUA,
                 _Out_ PVOID* Page,
                 _Out_ PULONG Length)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Cdb);
    ASSERT(Data);

    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    *Length = 0;
    *Page = NULL;

    switch (Cdb->AsByte[0]) {
    case SCSIOP_MODE_SENSE:
        {
        *Length = sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_CACHING_PAGE);

        if (CHECK_MODE_SENSE(Cdb, MODE_PAGE_CACHING)) {
            SrbStatus = SRB_STATUS_INVALID_REQUEST;
            goto Exit;
        }
        if(*Length > MaxLength) {
            WNBD_LOG_ERROR("MODE_SENSE overrun: %d > %d", *Length, MaxLength);
            SrbStatus = SRB_STATUS_DATA_OVERRUN;
            goto Exit;
        }

        PMODE_PARAMETER_HEADER ModeParameterHeader = Data;
        *Page = (PVOID)(ModeParameterHeader + 1);

        ModeParameterHeader->ModeDataLength = (UCHAR)(*Length -
            RTL_SIZEOF_THROUGH_FIELD(MODE_PARAMETER_HEADER, ModeDataLength));
        ModeParameterHeader->MediumType = 0;
        ModeParameterHeader->DeviceSpecificParameter =
            (ReadOnly ? MODE_DSP_WRITE_PROTECT : 0) |
            (AcceptFUA ? MODE_DSP_FUA_SUPPORTED : 0);
        ModeParameterHeader->BlockDescriptorLength = 0;
        }
        break;
    case SCSIOP_MODE_SENSE10:
        {
        *Length = sizeof(MODE_PARAMETER_HEADER10) + sizeof(MODE_CACHING_PAGE);

        if (CHECK_MODE_SENSE10(Cdb, MODE_PAGE_CACHING)) {
            SrbStatus = SRB_STATUS_INVALID_REQUEST;
            goto Exit;
        }
        if(*Length > MaxLength) {
            WNBD_LOG_ERROR("MODE_SENSE overrun: %d > %d", *Length, MaxLength);
            SrbStatus = SRB_STATUS_DATA_OVERRUN;
            goto Exit;
        }

        PMODE_PARAMETER_HEADER10 ModeParameterHeader = Data;
        *Page = (PVOID)(ModeParameterHeader + 1);

        ModeParameterHeader->ModeDataLength[0] = 0;
        ModeParameterHeader->ModeDataLength[1] = (UCHAR)(*Length -
            RTL_SIZEOF_THROUGH_FIELD(MODE_PARAMETER_HEADER10, ModeDataLength));
        ModeParameterHeader->MediumType = 0;
        ModeParameterHeader->DeviceSpecificParameter =
            (ReadOnly ? MODE_DSP_WRITE_PROTECT : 0) |
            (AcceptFUA ? MODE_DSP_FUA_SUPPORTED : 0);
        ModeParameterHeader->BlockDescriptorLength[0] = 0;
        ModeParameterHeader->BlockDescriptorLength[1] = 0;
        }
        break;
    default:
        WNBD_LOG_ERROR("Unknown MODE_SENSE byte: %u", Cdb->AsByte[0]);
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;
    }

Exit:
    WNBD_LOG_LOUD(": Exit: %#02x", SrbStatus);
    return SrbStatus;
}

UCHAR
WnbdModeSense(_In_ PWNBD_SCSI_DEVICE Device,
              _In_ PSCSI_REQUEST_BLOCK Srb,
              _In_ PCDB Cdb)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Srb);
    ASSERT(Cdb);

    PVOID DataBuffer = SrbGetDataBuffer(Srb);
    ULONG DataTransferLength = SrbGetDataTransferLength(Srb);
    UCHAR SrbStatus = SRB_STATUS_INTERNAL_ERROR;
    PMODE_CACHING_PAGE Page = NULL;
    ULONG Length = 0;

    if (NULL == DataBuffer) {
        SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        goto Exit;
    }

    SrbStatus = WnbdSetModeSense(
        DataBuffer, Cdb, DataTransferLength,
        !!Device->Properties.Flags.ReadOnly,
        !!Device->Properties.Flags.FUASupported,
        &Page, &Length);
    if (SRB_STATUS_SUCCESS != SrbStatus || NULL == Page) {
        WNBD_LOG_ERROR("Could not set mode sense.");
        goto Exit;
    }

    Page->PageCode = MODE_PAGE_CACHING;
    Page->PageSavable = 0;
    Page->PageLength = sizeof(MODE_CACHING_PAGE) -
        RTL_SIZEOF_THROUGH_FIELD(MODE_CACHING_PAGE, PageLength);
    Page->ReadDisableCache = !Device->Properties.Flags.FlushSupported;
    Page->WriteCacheEnable = !!Device->Properties.Flags.FlushSupported;

    SrbSetDataTransferLength(Srb, Length);

Exit:
    WNBD_LOG_LOUD(": Exit");
    return SrbStatus;
}

NTSTATUS
WnbdPendElement(_In_ PWNBD_EXTENSION DeviceExtension,
                _In_ PWNBD_SCSI_DEVICE Device,
                _In_ PSCSI_REQUEST_BLOCK Srb,
                _In_ UINT64 StartingLbn,
                _In_ UINT64 DataLength,
                _In_ BOOLEAN FUA)
{
    WNBD_LOG_LOUD(": Enter");
    NTSTATUS Status = STATUS_SUCCESS;

    InterlockedIncrement64(&Device->Stats.TotalReceivedIORequests);
    InterlockedIncrement64(&Device->Stats.UnsubmittedIORequests);

    PSRB_QUEUE_ELEMENT Element = (PSRB_QUEUE_ELEMENT)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(SRB_QUEUE_ELEMENT), 'DBNs');
    if (NULL == Element) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        Srb->SrbStatus = SRB_STATUS_ABORTED;
        goto Exit;
    }
    WNBD_LOG_INFO("Queuing Element, SRB= %p", Srb);

    Element->DeviceExtension = DeviceExtension;
    Element->Srb = Srb;
    Element->StartingLbn = StartingLbn;
    Element->ReadLength = (ULONG)DataLength;
    Element->Aborted = 0;
    Element->Completed = 0;
    Element->FUA = FUA;
    ExInterlockedInsertTailList(&Device->RequestListHead, &Element->Link, &Device->RequestListLock);
    KeReleaseSemaphore(&Device->DeviceEvent, 0, 1, FALSE);
    Status = STATUS_PENDING;

Exit:
    WNBD_LOG_LOUD(": Exit");
    return Status;
}

NTSTATUS
WnbdPendOperation(_In_ PWNBD_EXTENSION DeviceExtension,
                  _In_ PWNBD_SCSI_DEVICE Device,
                  _In_ PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    ASSERT(Device);
    ASSERT(Srb);

    NTSTATUS Status = STATUS_SUCCESS;
    PCDB Cdb = (PCDB) &Srb->Cdb;

    switch(Cdb->AsByte[0]) {
    case SCSIOP_READ6:
    case SCSIOP_READ:
    case SCSIOP_READ12:
    case SCSIOP_READ16:
    case SCSIOP_WRITE6:
    case SCSIOP_WRITE:
    case SCSIOP_WRITE12:
    case SCSIOP_WRITE16:
    case SCSIOP_SYNCHRONIZE_CACHE:
    case SCSIOP_SYNCHRONIZE_CACHE16:
        {
        UINT64 BlockAddress = 0;
        UINT32 BlockCount = 0;
        ULONG DataLength = 0;
        UINT32 FUA = 0;
        SrbCdbGetRange(Cdb, &BlockAddress, &BlockCount, &FUA);
        DataLength = BlockCount * Device->Properties.BlockSize;
        if (DataLength < Srb->DataTransferLength &&
            (Cdb->AsByte[0] != SCSIOP_SYNCHRONIZE_CACHE || Cdb->AsByte[0] != SCSIOP_SYNCHRONIZE_CACHE16)) {
            WNBD_LOG_ERROR("STATUS_BUFFER_TOO_SMALL");
            Srb->SrbStatus = SRB_STATUS_ABORTED;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        Status = WnbdPendElement(DeviceExtension, Device, Srb,
            BlockAddress * Device->Properties.BlockSize, DataLength, (BOOLEAN)FUA);
        }
        break;
    case SCSIOP_UNMAP:
    {
        PUNMAP_LIST_HEADER DataBuffer = SrbGetDataBuffer(Srb);
        ULONG DataTransferLength = SrbGetDataTransferLength(Srb);
        UINT16 BlockDescLength;

        if (!DataBuffer ||
            DataTransferLength < sizeof(UNMAP_LIST_HEADER) ||
            DataTransferLength < sizeof(UNMAP_LIST_HEADER) + (
                BlockDescLength = ((ULONG)DataBuffer->BlockDescrDataLength[0] << 8) |
                (ULONG)DataBuffer->BlockDescrDataLength[1]) ||
            BlockDescLength > WNBD_DEFAULT_MAX_TRANSFER_LENGTH)
        {
            Srb->SrbStatus = SRB_STATUS_ABORTED;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        // We don't support the anchored state at the moment.
        if (Cdb->UNMAP.Anchor)
        {
            WNBD_LOG_ERROR("Unmap 'anchor' state not supported.");
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        // Empty request, let's mark it as completed right away.
        if (0 == BlockDescLength) {
            Srb->DataTransferLength = 0;
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            Status = STATUS_SUCCESS;
            break;
        }

        UINT32 DescriptorCount = BlockDescLength / sizeof(UNMAP_BLOCK_DESCRIPTOR);
        if (DescriptorCount != 1) {
            // Storport should honor the VPD limits.
            WNBD_LOG_ERROR("Cannot send multiple UNMAP descriptors at a time.");
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        PUNMAP_BLOCK_DESCRIPTOR Src = &DataBuffer->Descriptors[0];
        UINT64 BlockAddress;
        UINT32 BlockCount;
        REVERSE_BYTES_8(&BlockAddress, &Src->StartingLba);
        REVERSE_BYTES_4(&BlockCount, &Src->LbaCount);
        UINT64 LastBlockAddress = BlockAddress + BlockCount;

        if (LastBlockAddress < BlockAddress ||
            LastBlockAddress >= Device->Properties.BlockCount)
        {
            WNBD_LOG_ERROR("Unmap overflow.");
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        // TODO: can FUA be requested for UNMAP requests? NBD specs suggest
        // that it can, storport.h and SCSI specs suggest otherwise.
        Status = WnbdPendElement(DeviceExtension, Device, Srb,
            BlockAddress * Device->Properties.BlockSize,
            (UINT64)BlockCount * Device->Properties.BlockSize,
            FALSE);
        }
        break;
    default:
        WNBD_LOG_ERROR("Unknown Pending SCSI Operation received");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        Srb->SrbStatus = SRB_STATUS_ABORTED;
        break;
    }

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdHandleSrbOperation(PWNBD_EXTENSION DeviceExtension,
                       PWNBD_SCSI_DEVICE Device,
                       PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    ASSERT(Device);
    ASSERT(Srb);
    PCDB Cdb = (PCDB) &Srb->Cdb;
    NTSTATUS status = STATUS_SUCCESS;
    if (!Device || Device->SoftTerminateDevice || Device->HardTerminateDevice) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return status;
    }
    UINT32 BlockSize = Device->Properties.BlockSize;
    UINT64 BlockCount = Device->Properties.BlockCount;


    WNBD_LOG_LOUD("Processing %#02x command", Cdb->AsByte[0]);

    switch (Cdb->AsByte[0]) {
    case SCSIOP_READ6:
    case SCSIOP_READ:
    case SCSIOP_READ12:
    case SCSIOP_READ16:
    case SCSIOP_WRITE6:
    case SCSIOP_WRITE:
    case SCSIOP_WRITE12:
    case SCSIOP_WRITE16:
    case SCSIOP_SYNCHRONIZE_CACHE:
    case SCSIOP_SYNCHRONIZE_CACHE16:
    case SCSIOP_UNMAP:
        Srb->SrbStatus = SRB_STATUS_ABORTED;
        status = WnbdPendOperation(DeviceExtension, Device, Srb);
        break;
    case SCSIOP_INQUIRY:
        Srb->SrbStatus = WnbdInquiry(Device, Srb, Cdb);
        break;

    case SCSIOP_MODE_SENSE:
    case SCSIOP_MODE_SENSE10:
        Srb->SrbStatus = WnbdModeSense(Device, Srb, Cdb);
        break;

    case SCSIOP_READ_CAPACITY:
        Srb->SrbStatus = WnbdReadCapacity(Device, Srb, Cdb, BlockSize, BlockCount);
        break;
    case SCSIOP_READ_CAPACITY16:
        if (Cdb->READ_CAPACITY16.ServiceAction == SERVICE_ACTION_READ_CAPACITY16) {
            Srb->SrbStatus = WnbdReadCapacity(Device, Srb, Cdb, BlockSize, BlockCount);
        }
        else {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        }
        break;

    case SCSIOP_VERIFY:
    case SCSIOP_TEST_UNIT_READY:
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;

    default:
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;
    };

    WNBD_LOG_LOUD(": Exit");
    return status;
}
