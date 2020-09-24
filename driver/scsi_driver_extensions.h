/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef SCSI_DRIVER_EXTENSIONS_H
#define SCSI_DRIVER_EXTENSIONS_H 1

#include "common.h"
#include "wnbd_ioctl.h"

typedef struct _WNBD_EXTENSION {
    SCSI_ADAPTER_CONTROL_TYPE         ScsiAdapterControlState;

    UNICODE_STRING                    DeviceInterface;
    LIST_ENTRY                        DeviceList;
    KSPIN_LOCK                        DeviceListLock;
    LONG                              DeviceCount;
    // TODO: try to remove this lock, maybe by adding a "Pending" device state
    // That should avoid device duplicates, while allowing multiple devices
    // to be created simultaneously. In particular, this might be a concern
    // for NBD devices, in which case connecting to the NBD server might
    // take longer. It shouldn't be a concern when not using NBD.
    ERESOURCE                         DeviceCreationLock;

    EX_RUNDOWN_REF                    RundownProtection;
    KEVENT                            GlobalDeviceRemovalEvent;
} WNBD_EXTENSION, *PWNBD_EXTENSION;

typedef struct _WNBD_SCSI_DEVICE
{
    LIST_ENTRY                  ListEntry;
    PWNBD_EXTENSION             DeviceExtension;

    BOOLEAN                     Connected;
    WNBD_PROPERTIES             Properties;
    WNBD_CONNECTION_ID          ConnectionId;

    USHORT                      Bus;
    USHORT                      Target;
    USHORT                      Lun;

    PINQUIRYDATA                InquiryData;

    INT                         NbdSocket;
    INT                         SocketToClose;
    ERESOURCE                   SocketLock;

    LIST_ENTRY                  PendingReqListHead;
    KSPIN_LOCK                  PendingReqListLock;

    LIST_ENTRY                  SubmittedReqListHead;
    KSPIN_LOCK                  SubmittedReqListLock;

    KSEMAPHORE                  DeviceEvent;
    PVOID                       DeviceRequestThread;
    PVOID                       DeviceReplyThread;
    PVOID                       DeviceMonitorThread;
    BOOLEAN                     HardTerminateDevice;
    BOOLEAN                     SoftTerminateDevice;
    KEVENT                      TerminateEvent;
    // The rundown protection provides device reference counting, preventing
    // it from being deallocated while still being accessed. This is
    // especially important for IO dispatching.
    EX_RUNDOWN_REF              RundownProtection;

    WNBD_DRV_STATS              Stats;
    PVOID                       ReadPreallocatedBuffer;
    ULONG                       ReadPreallocatedBufferLength;
    PVOID                       WritePreallocatedBuffer;
    ULONG                       WritePreallocatedBufferLength;
} WNBD_SCSI_DEVICE, *PWNBD_SCSI_DEVICE;

typedef struct _SRB_QUEUE_ELEMENT {
    LIST_ENTRY Link;
    PSCSI_REQUEST_BLOCK Srb;
    UINT64 StartingLbn;
    ULONG DataLength;
    BOOLEAN FUA;
    PVOID DeviceExtension;
    UINT64 Tag;
    BOOLEAN Aborted;
    BOOLEAN Completed;
} SRB_QUEUE_ELEMENT, * PSRB_QUEUE_ELEMENT;

SCSI_ADAPTER_CONTROL_STATUS
WnbdHwAdapterControl(_In_ PVOID DeviceExtension,
                     _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
                     _In_ PVOID Parameters);

ULONG
WnbdHwFindAdapter(_In_ PVOID DeviceExtension,
                  _In_ PVOID HwContext,
                  _In_ PVOID BusInformation,
                  _In_ PVOID LowerDevice,
                  _In_ PCHAR ArgumentString,
                  _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                  _In_ PBOOLEAN Again);

VOID
WnbdHwFreeAdapterResources(_In_ PVOID DeviceExtension);


BOOLEAN
WnbdHwInitialize(_In_ PVOID DeviceExtension);

VOID
WnbdHwProcessServiceRequest(_In_ PVOID DeviceExtension,
                            _In_ PVOID Irp);

BOOLEAN
WnbdHwResetBus(_In_ PVOID DeviceExtension,
               _In_ ULONG PathId);

BOOLEAN
WnbdHwStartIo(_In_ PVOID PDevExt,
              _In_ PSCSI_REQUEST_BLOCK  PSrb);

#endif
