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
#include "scsi_trace.h"
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

    while ((Request = ExInterlockedRemoveHeadList(&ScsiInfo->ListHead, &ScsiInfo->ListLock)) != NULL) {
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

    if(ScsiInfo->UserEntry) {
        ExFreePool(ScsiInfo->UserEntry);
        ScsiInfo->UserEntry = NULL;
    }

    if (-1 != ScsiInfo->Socket) {
        WNBD_LOG_INFO("Closing socket FD: %d", ScsiInfo->Socket);
        Close(ScsiInfo->Socket);
        ScsiInfo->Socket = -1;
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
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Ext->DeviceResourceLock, TRUE);

    LIST_FORALL_SAFE(&Ext->DeviceList, Link, Next) {
        Device = (PWNBD_SCSI_DEVICE)CONTAINING_RECORD(Link, WNBD_SCSI_DEVICE, ListEntry);
        if (Device->ReportedMissing || All) {
            WNBD_LOG_INFO("Deleting device %p with %d:%d:%d",
                Device, Device->PathId, Device->TargetId, Device->Lun);
            PSCSI_DEVICE_INFORMATION Info = (PSCSI_DEVICE_INFORMATION)Device->ScsiDeviceExtension;
            WnbdDeleteConnection((PGLOBAL_INFORMATION)Ext->GlobalInformation,
                                 &Info->UserEntry->UserInformation);
            RemoveEntryList(&Device->ListEntry);
            WnbdDeleteScsiInformation(Device->ScsiDeviceExtension);
            ExFreePool(Device);
            Device = NULL;
            if (FALSE == All) {
                break;
            }
        }
    }

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
               _In_ PSCSI_REQUEST_BLOCK Srb)
{
    WNBD_LOG_LOUD(": Entered");
    ASSERT(LuExtension);
    ASSERT(DeviceExtension);
    ASSERT(Srb);

    PWNBD_SCSI_DEVICE Device = NULL;

    for (PLIST_ENTRY Entry = DeviceExtension->DeviceList.Flink;
        Entry != &DeviceExtension->DeviceList; Entry = Entry->Flink) {

        Device = (PWNBD_SCSI_DEVICE) CONTAINING_RECORD(Entry, WNBD_SCSI_DEVICE, ListEntry);

        if (Device->PathId == Srb->PathId
            && Device->TargetId == Srb->TargetId
            && Device->Lun == Srb->Lun) {
            WnbdReportMissingDevice(DeviceExtension, Device, LuExtension);
            break;
        }
        Device = NULL;
    }

    WNBD_LOG_LOUD(": Exit");

    return Device;
}

NTSTATUS
WnbdProcessDeviceThreadRequestsReads(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation,
                                     _In_ PSRB_QUEUE_ELEMENT Element)
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
        NbdReadStat(DeviceInformation->Socket,
                    Element->StartingLbn,
                    Element->ReadLength,
                    &Status,
                    Buffer);
    }
    Element->Srb->DataTransferLength = Element->ReadLength;

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

NTSTATUS
WnbdProcessDeviceThreadRequestsWrites(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation,
                                      _In_ PSRB_QUEUE_ELEMENT Element)
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
                     Buffer);
    }
    Element->Srb->DataTransferLength = Element->ReadLength;

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

VOID
WnbdProcessDeviceThreadRequests(_In_ PSCSI_DEVICE_INFORMATION DeviceInformation)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(DeviceInformation);

    PLIST_ENTRY Request;
    PSRB_QUEUE_ELEMENT Element;
    NTSTATUS Status = STATUS_SUCCESS;

    while ((Request = ExInterlockedRemoveHeadList(&DeviceInformation->ListHead, &DeviceInformation->ListLock)) != NULL) {
        Element = CONTAINING_RECORD(Request, SRB_QUEUE_ELEMENT, Link);
        Element->Srb->DataTransferLength = 0;
        PCDB Cdb = (PCDB)&Element->Srb->Cdb;
        PWNBD_SCSI_DEVICE Device = (PWNBD_SCSI_DEVICE)DeviceInformation->Device;
        switch (Cdb->AsByte[0]) {
        case SCSIOP_READ6:
        case SCSIOP_READ:
        case SCSIOP_READ12:
        case SCSIOP_READ16:
            Status = WnbdProcessDeviceThreadRequestsReads(DeviceInformation, Element);
            break;

        case SCSIOP_WRITE6:
        case SCSIOP_WRITE:
        case SCSIOP_WRITE12:
        case SCSIOP_WRITE16:
            Status = WnbdProcessDeviceThreadRequestsWrites(DeviceInformation, Element);
            break;

        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16:
            /*
             * We just want to mark synchronize as been successful
             */
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_DRIVER_INTERNAL_ERROR;
            break;
        }

        if (STATUS_SUCCESS == Status) {
            Element->Srb->SrbStatus = SRB_STATUS_SUCCESS;
        } else {
            Element->Srb->DataTransferLength = 0;
            Element->Srb->SrbStatus = SRB_STATUS_TIMEOUT;
            if (STATUS_INVALID_SESSION == Status) {
                Element->Srb->SrbStatus = SRB_STATUS_ERROR;
                KeEnterCriticalRegion();
                ExAcquireResourceExclusiveLite(&DeviceInformation->GlobalInformation->ConnectionMutex, TRUE);
                if (-1 != DeviceInformation->Socket) {
                    WNBD_LOG_INFO("Closing socket FD: %d", DeviceInformation->Socket);
                    Close(DeviceInformation->Socket);
                    DeviceInformation->Socket = -1;
                    Device->Missing = TRUE;
                }
                ExReleaseResourceLite(&DeviceInformation->GlobalInformation->ConnectionMutex);
                KeLeaveCriticalRegion();
            }
        }

        InterlockedDecrement(&Device->OutstandingIoCount);
        WNBD_LOG_INFO("Notifying StorPort of completion of %p status: 0x%x(%s)",
            Element->Srb, Element->Srb->SrbStatus, WnbdToStringSrbStatus(Element->Srb->SrbStatus));
        StorPortNotification(RequestComplete, Element->DeviceExtension, Element->Srb);
        ExFreePool(Element);
    }

    WNBD_LOG_LOUD(": Exit");
}

VOID
WnbdDeviceThread(_In_ PVOID Context)
{
    WNBD_LOG_LOUD(": Enter");

    ASSERT(Context);
    
    PSCSI_DEVICE_INFORMATION DeviceInformation;
    PAGED_CODE();

    DeviceInformation = (PSCSI_DEVICE_INFORMATION) Context;

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    while (TRUE) {
        KeWaitForSingleObject(&DeviceInformation->DeviceEvent, Executive, KernelMode, FALSE, NULL);

        if (DeviceInformation->HardTerminateDevice) {
            WNBD_LOG_INFO("Hard terminate thread: %p", DeviceInformation);
            PsTerminateSystemThread(STATUS_SUCCESS);
        }

        WnbdProcessDeviceThreadRequests(DeviceInformation);

        if (DeviceInformation->SoftTerminateDevice) {
            WNBD_LOG_INFO("Soft terminate thread: %p", DeviceInformation);
            PsTerminateSystemThread(STATUS_SUCCESS);
        }
    }
}
