/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "common.h"
#include "debug.h"
#include "driver.h"
#include "scsi_driver_extensions.h"
#include "scsi_trace.h"
#include "userspace.h"
#include "util.h"
#include "options.h"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD WnbdDriverUnload;
DRIVER_DISPATCH WnbdDispatchPnp;
PDRIVER_UNLOAD StorPortDriverUnload;
PDRIVER_DISPATCH StorPortDispatchPnp;

WCHAR  GlobalRegistryPathBuffer[256];
PWNBD_EXTENSION GlobalExt = NULL;
extern UNICODE_STRING GlobalRegistryPath = { 0, 0, GlobalRegistryPathBuffer};
extern UINT32 GlobalLogLevel = 0;

_Use_decl_annotations_
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath)
{
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    WNBD_LOG_LOUD(": Enter");

    /*
     * Register Virtual Storport Miniport data
     */
    NTSTATUS Status;
    VIRTUAL_HW_INITIALIZATION_DATA WnbdInitData = { 0 };
    WnbdInitData.HwInitializationDataSize = sizeof(VIRTUAL_HW_INITIALIZATION_DATA);
    GlobalLogLevel = 0;

    /*
     * Set our SCSI Driver Extensions
     */
    WnbdInitData.HwAdapterControl        = WnbdHwAdapterControl;
    WnbdInitData.HwCompleteServiceIrp    = 0;
    WnbdInitData.HwFindAdapter           = WnbdHwFindAdapter;
    WnbdInitData.HwFreeAdapterResources  = WnbdHwFreeAdapterResources;
    WnbdInitData.HwInitialize            = WnbdHwInitialize;
    WnbdInitData.HwProcessServiceRequest = WnbdHwProcessServiceRequest;
    WnbdInitData.HwResetBus              = WnbdHwResetBus;
    WnbdInitData.HwStartIo               = WnbdHwStartIo;

    WnbdInitData.AdapterInterfaceType    = Internal;
    WnbdInitData.MultipleRequestPerLu	 = TRUE;
    WnbdInitData.PortVersionFlags		 = 0;

    WnbdInitData.DeviceExtensionSize      = sizeof(WNBD_EXTENSION);
    WnbdInitData.SpecificLuExtensionSize  = 0;
    WnbdInitData.SrbExtensionSize         = 0;

    /*
     * NOOP for virtual devices
     */
    WnbdInitData.HwInterrupt    = 0;
    WnbdInitData.HwDmaStarted   = 0;
    WnbdInitData.HwAdapterState = 0;

    WnbdInitData.MapBuffers           = STOR_MAP_NON_READ_WRITE_BUFFERS;
    WnbdInitData.TaggedQueuing        = TRUE;
    WnbdInitData.AutoRequestSense     = TRUE;
    WnbdInitData.MultipleRequestPerLu = TRUE;

    if (RegistryPath->MaximumLength > 0) {
        if (RegistryPath->MaximumLength > sizeof(GlobalRegistryPathBuffer)) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        GlobalRegistryPath.MaximumLength = RegistryPath->MaximumLength;
        RtlUnicodeStringCopy(&GlobalRegistryPath, RegistryPath);

        WnbdReloadPersistentOptions();
    }

    /*
     * Register our driver
     */
    Status =  StorPortInitialize(DriverObject,
                                 RegistryPath,
                                 (PHW_INITIALIZATION_DATA)&WnbdInitData,
                                 NULL);
    if (!NT_SUCCESS(Status)) {
        WNBD_LOG_ERROR("DriverEntry failure in call to StorPortInitialize. Status: 0x%x", Status);
        ASSERT(FALSE);
        return Status;
    }

    /*
     * Set up PNP and Unload routines
     */
    StorPortDriverUnload = DriverObject->DriverUnload;
    DriverObject->DriverUnload = WnbdDriverUnload;
    StorPortDispatchPnp = DriverObject->MajorFunction[IRP_MJ_PNP];
    DriverObject->MajorFunction[IRP_MJ_PNP] = 0 != StorPortDispatchPnp ? WnbdDispatchPnp : 0;
    GlobalExt = NULL;

    WNBD_LOG_LOUD(": Exit");

    /*
     * Report status in upper layers
     */
    return Status;
}

_Use_decl_annotations_
NTSTATUS
WnbdDispatchPnp(PDEVICE_OBJECT DeviceObject,
                PIRP Irp)
{
    WNBD_LOG_LOUD(": Enter");
    ASSERT(Irp);
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    PIO_STACK_LOCATION IoLocation = IoGetCurrentIrpStackLocation(Irp);
    SCSI_ADDRESS ScsiAddress = { 0 };
    ASSERT(IoLocation);
    UCHAR MinorFunction = IoLocation->MinorFunction;

    WNBD_LOG_LOUD("Received PnP request: %s (%d).",
                  WnbdToStringPnpMinorFunction(MinorFunction), MinorFunction);
    switch (MinorFunction) {
    case IRP_MN_QUERY_CAPABILITIES:
        IoLocation->Parameters.DeviceCapabilities.Capabilities->SilentInstall = 1;
        // We're disabling SurpriseRemovalOK in order to
        // receive device removal PnP events.
        IoLocation->Parameters.DeviceCapabilities.Capabilities->SurpriseRemovalOK = 0;
        IoLocation->Parameters.DeviceCapabilities.Capabilities->Removable = 1;
        IoLocation->Parameters.DeviceCapabilities.Capabilities->EjectSupported = 1;
        break;
    // We won't remove the device upon receiving IRP_MN_QUERY_REMOVE_DEVICE.
    // The device removal might be vetoed by other parts of the storage stack,
    // so we'd affect soft removals. The only downside is that if the remove
    // gets vetoed, uninstalling the driver will require a reboot.
    case IRP_MN_REMOVE_DEVICE:
        {
            if (NULL == GlobalExt || !GlobalExt->DeviceCount) {
                break;
            }
            Status = WnbdGetScsiAddress(DeviceObject, &ScsiAddress);
            if (Status) {
                WNBD_LOG_ERROR("Could not query SCSI address. Error: %d.", Status);
                break;
            }

            WNBD_LOG_INFO("Removing device.");
            PWNBD_DISK_DEVICE Device = WnbdFindDeviceByAddr(
                GlobalExt, ScsiAddress.PathId,
                ScsiAddress.TargetId, ScsiAddress.Lun, TRUE);
            if (!Device) {
                WNBD_LOG_INFO("Device already removed.");
                break;
            }
            WnbdDisconnectSync(Device);
            WNBD_LOG_INFO("Successfully removed device.");
        }
        break;
    }

    Status = StorPortDispatchPnp(DeviceObject, Irp);

    WNBD_LOG_LOUD(": Exit: %d", Status);
    return Status;
}

_Use_decl_annotations_
VOID
WnbdDriverUnload(PDRIVER_OBJECT DriverObject)
{
    WNBD_LOG_LOUD(": Enter");

    if (0 != StorPortDriverUnload) {
        StorPortDriverUnload(DriverObject);
    }

    WNBD_LOG_LOUD(": Exit");
    WPP_CLEANUP(DriverObject);
}
