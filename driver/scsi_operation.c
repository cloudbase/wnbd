/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "common.h"
#include "debug.h"
#include "scsi_operation.h"
#include "scsi_trace.h"
#include "srb_helper.h"
#include "userspace.h"
#include "util.h"

UINT16
WnbdGetShiftBlockSize(_In_ UINT16 BlockSize)
{
    ASSERT(BlockSize && !(BlockSize & (BlockSize - 1)));
    return (UINT16) MultiplyDeBruijnBitPosition2[(UINT32)(BlockSize * 0x077CB531U) >> 27];
}

UINT64
WnbdGetBlockCount(_In_ UINT64 DiskSize, UINT16 BlockSize)
{
    return (DiskSize >> WnbdGetShiftBlockSize(BlockSize));
}

UCHAR
WnbdReadCapacity(_In_ PSCSI_REQUEST_BLOCK Srb,
                 _In_ PCDB Cdb,
                 _In_ UINT64 BlockSize,
                 _In_ UINT64 BlockCount)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Srb);
    ASSERT(Cdb);
    WNBD_LOG_INFO("Using BlockCount: %llu", BlockCount);

    PVOID DataBuffer = SrbGetDataBuffer(Srb);
    ULONG DataTransferLength = SrbGetDataTransferLength(Srb);
    UCHAR SrbStatus = SRB_STATUS_DATA_OVERRUN;

    if (0 == DataBuffer) {
        goto Exit;
    }

    RtlZeroMemory(DataBuffer, DataTransferLength);

    switch (Cdb->AsByte[0])
    {
    case SCSIOP_READ_CAPACITY:
        {
        if (sizeof(READ_CAPACITY_DATA) > DataTransferLength) {
            goto Exit;
        }
        PREAD_CAPACITY_DATA ReadCapacityData = DataBuffer;
        REVERSE_BYTES_4(&ReadCapacityData->LogicalBlockAddress, &BlockCount);
        REVERSE_BYTES_4(&ReadCapacityData->BytesPerBlock, &BlockSize);
        SrbSetDataTransferLength(Srb, sizeof(READ_CAPACITY_DATA));
        SrbStatus = SRB_STATUS_SUCCESS;
        }
        break;
    case SCSIOP_READ_CAPACITY16:
        {
        if (sizeof(READ_CAPACITY16_DATA) > DataTransferLength) {
            goto Exit;
        }
        PREAD_CAPACITY16_DATA ReadCapacityData16 = DataBuffer;
        REVERSE_BYTES_8(&ReadCapacityData16->LogicalBlockAddress, &BlockCount);
        REVERSE_BYTES_8(&ReadCapacityData16->BytesPerBlock, &BlockSize);
        SrbSetDataTransferLength(Srb, sizeof(READ_CAPACITY16_DATA));
        SrbStatus = SRB_STATUS_SUCCESS;
        }
        break;
    default:
        WNBD_LOG_ERROR("Unknown read capacity operation: %u", Cdb->AsByte[0]);
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

UCHAR
WnbdProcessExtendedInquiry(_In_ PVOID Data,
                           _In_ ULONG Length,
                           _In_ PSCSI_REQUEST_BLOCK Srb,
                           _In_ PCDB Cdb,
                           _In_ PSCSI_DEVICE_INFORMATION Info)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Data);
    ASSERT(Srb);
    ASSERT(Cdb);

    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    UCHAR NumberOfSupportedPages = 2;
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
                                           Info->UserEntry->UserInformation.SerialNumber,
                                           Srb);
        }
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
WnbdInquiry(_In_ PSCSI_DEVICE_INFORMATION Info,
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

        RtlCopyMemory(DataBuffer, Info->InquiryData, INQUIRYDATABUFFERSIZE);
        SrbSetDataTransferLength(Srb, INQUIRYDATABUFFERSIZE);
        SrbStatus = SRB_STATUS_SUCCESS;
        break;
    default:
        WNBD_LOG_INFO("Extended Inquiry");
        SrbStatus = WnbdProcessExtendedInquiry(DataBuffer, DataTransferLength, Srb, Cdb, Info);
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
                 _Out_ PVOID Page,
                 _Out_ PULONG Length)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Cdb);
    ASSERT(Data);

    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    *Length = 0;
    Page = NULL;

    switch (Cdb->AsByte[0]) {
    case SCSIOP_MODE_SENSE:
        {
        *Length = sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_CACHING_PAGE);

        if (CHECK_MODE_SENSE(Cdb) || (*Length > MaxLength)) {
            SrbStatus = SRB_STATUS_DATA_OVERRUN;
            goto Exit;
        }
        PMODE_PARAMETER_HEADER ModeParameterHeader = Data;
        Page = (PVOID)(ModeParameterHeader + 1);

        ModeParameterHeader->ModeDataLength = (UCHAR)(*Length -
            RTL_SIZEOF_THROUGH_FIELD(MODE_PARAMETER_HEADER, ModeDataLength));
        ModeParameterHeader->MediumType = 0;
        ModeParameterHeader->DeviceSpecificParameter = (0) | (0);
        ModeParameterHeader->BlockDescriptorLength = 0;
        }
        break;
    case SCSIOP_MODE_SENSE10:
        {
        *Length = sizeof(MODE_PARAMETER_HEADER10) + sizeof(MODE_CACHING_PAGE);

        if (CHECK_MODE_SENSE10(Cdb) || (*Length > MaxLength)) {
            SrbStatus = SRB_STATUS_DATA_OVERRUN;
            goto Exit;
        }

        PMODE_PARAMETER_HEADER10 ModeParameterHeader = Data;
        Page = (PVOID)(ModeParameterHeader + 1);

        ModeParameterHeader->ModeDataLength[0] = 0;
        ModeParameterHeader->ModeDataLength[1] = (UCHAR)(*Length -
            RTL_SIZEOF_THROUGH_FIELD(MODE_PARAMETER_HEADER10, ModeDataLength));
        ModeParameterHeader->MediumType = 0;
        ModeParameterHeader->DeviceSpecificParameter = (0) | (0);
        ModeParameterHeader->BlockDescriptorLength[0] = 0;
        ModeParameterHeader->BlockDescriptorLength[1] = 0;
        }
        break;
    default:
        WNBD_LOG_ERROR("Unknown MODE_SENSE byte: %u", Cdb->AsByte[0]);
        SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        break;
    }

Exit:
    WNBD_LOG_LOUD(": Exit");
    return SrbStatus;
}

UCHAR
WnbdModeSense(_In_ PSCSI_REQUEST_BLOCK Srb,
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

    SrbStatus = WnbdSetModeSense(DataBuffer, Cdb, DataTransferLength, Page, &Length);
    if (SRB_STATUS_SUCCESS != SrbStatus || NULL == Page) {
        goto Exit;
    }

    Page->PageCode = MODE_PAGE_CACHING;
    Page->PageSavable = 0;
    Page->PageLength = sizeof(MODE_CACHING_PAGE) -
        RTL_SIZEOF_THROUGH_FIELD(MODE_CACHING_PAGE, PageLength);
    Page->ReadDisableCache = 1;
    Page->WriteCacheEnable = 0;

    SrbSetDataTransferLength(Srb, Length);

Exit:
    WNBD_LOG_LOUD(": Exit");
    return SrbStatus;
}

NTSTATUS
WnbdPendElement(_In_ PVOID DeviceExtension,
                _In_ PVOID ScsiDeviceExtension,
                _In_ PSCSI_REQUEST_BLOCK Srb,
                _In_ UINT64 StartingLbn,
                _In_ UINT64 DataLength)
{
    WNBD_LOG_LOUD(": Enter");
    NTSTATUS Status = STATUS_SUCCESS;
    PSCSI_DEVICE_INFORMATION ScsiInfo = (PSCSI_DEVICE_INFORMATION)ScsiDeviceExtension;

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
    ExInterlockedInsertTailList(&ScsiInfo->RequestListHead, &Element->Link, &ScsiInfo->RequestListLock);
    KeReleaseSemaphore(&ScsiInfo->DeviceEvent, 0, 1, FALSE);
    Status = STATUS_PENDING;

Exit:
    WNBD_LOG_LOUD(": Exit");
    return Status;
}

NTSTATUS
WnbdPendOperation(_In_ PVOID DeviceExtension,
                  _In_ PVOID ScsiDeviceExtension,
                  _In_ PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    ASSERT(ScsiDeviceExtension);
    ASSERT(Srb);

    NTSTATUS Status = STATUS_SUCCESS;
    PCDB Cdb = (PCDB) &Srb->Cdb;
    PSCSI_DEVICE_INFORMATION ScsiInfo = (PSCSI_DEVICE_INFORMATION)ScsiDeviceExtension;

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
        UINT32 Access = 0;
        SrbCdbGetRange(Cdb, &BlockAddress, &BlockCount, &Access);
        DataLength = BlockCount * ScsiInfo->UserEntry->BlockSize;
        if (DataLength < Srb->DataTransferLength &&
            (Cdb->AsByte[0] != SCSIOP_SYNCHRONIZE_CACHE || Cdb->AsByte[0] != SCSIOP_SYNCHRONIZE_CACHE16)) {
            WNBD_LOG_ERROR("STATUS_BUFFER_TOO_SMALL");
            Srb->SrbStatus = SRB_STATUS_ABORTED;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        Status = WnbdPendElement(DeviceExtension, ScsiDeviceExtension, Srb,
            BlockAddress * ScsiInfo->UserEntry->BlockSize, DataLength);
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
WnbdHandleSrbOperation(PVOID DeviceExtension,
                       PVOID ScsiDeviceExtension,
                       PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceExtension);
    ASSERT(ScsiDeviceExtension);
    ASSERT(Srb);
    PCDB Cdb = (PCDB) &Srb->Cdb;
    NTSTATUS status = STATUS_SUCCESS;
    PSCSI_DEVICE_INFORMATION Info = (PSCSI_DEVICE_INFORMATION)ScsiDeviceExtension;
    if (!Info || !Info->UserEntry || Info->SoftTerminateDevice || Info->HardTerminateDevice) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return status;
    }
    UINT16 BlockSize = Info->UserEntry->BlockSize;
    UINT64 BlockCount = WnbdGetBlockCount(Info->UserEntry->DiskSize, BlockSize);


    WNBD_LOG_LOUD("Processing %s command",
                  WnbdToStringSrbCdbOperation(Cdb->AsByte[0]));

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
        Srb->SrbStatus = SRB_STATUS_ABORTED;
        status = WnbdPendOperation(DeviceExtension, ScsiDeviceExtension, Srb);
        break;

    case SCSIOP_INQUIRY:
        Srb->SrbStatus = WnbdInquiry(Info, Srb, Cdb);
        break;

    case SCSIOP_MODE_SENSE:
    case SCSIOP_MODE_SENSE10:
        Srb->SrbStatus = WnbdModeSense(Srb, Cdb);
        break;

    case SCSIOP_READ_CAPACITY:
    case SCSIOP_READ_CAPACITY16:
        WNBD_LOG_INFO("Using BlockSize: %u", BlockSize);
        Srb->SrbStatus = WnbdReadCapacity(Srb, Cdb, BlockSize, BlockCount);
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
