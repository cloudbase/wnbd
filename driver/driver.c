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
#include "events.h"

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
    /*
     * Register with ETW
     */
    EventRegisterWNBD();
    WPP_INIT_TRACING(DriverObject, RegistryPath);

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
    ASSERT(Irp);
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    PIO_STACK_LOCATION IoLocation = IoGetCurrentIrpStackLocation(Irp);
    SCSI_ADDRESS ScsiAddress = { 0 };
    ASSERT(IoLocation);
    UCHAR MinorFunction = IoLocation->MinorFunction;

    WNBD_LOG_DEBUG("Received PnP request: %s (%d).",
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
    case IRP_MN_START_DEVICE:
        {
            if (NULL == GlobalExt || !GlobalExt->DeviceCount) {
                break;
            }
            Status = WnbdGetScsiAddress(DeviceObject, &ScsiAddress);
            if (Status) {
                WNBD_LOG_ERROR("Could not query SCSI address. Error: %d.", Status);
                break;
            }

            WNBD_LOG_INFO("Starting device.");
            PWNBD_DISK_DEVICE Device = WnbdFindDeviceByAddr(
                GlobalExt, ScsiAddress.PathId,
                ScsiAddress.TargetId, ScsiAddress.Lun, TRUE);
            if (!Device) {
                break;
            }
            Device->PDO = DeviceObject;

            PDEVICE_OBJECT AttachedDisk = IoGetAttachedDeviceReference(DeviceObject);
            if (AttachedDisk != DeviceObject) {
                Status = WnbdGetDiskNumber(
                    AttachedDisk, (PULONG) &Device->DiskNumber);
                if (Status) {
                    WNBD_LOG_WARN("Could not get disk number. Error: %d.",
                                  Status);
                }
            }
            else {
                WNBD_LOG_WARN("Couldn't not get disk number. "
                              "Couldn't get attached PDO.");
            }
            ObDereferenceObject(AttachedDisk);

            DWORD RequiredSize = 0;
            Status = WnbdGetDiskInstancePath(
                DeviceObject, Device->PNPDeviceID,
                sizeof(Device->PNPDeviceID),
                &RequiredSize);
            if (Status) {
                WNBD_LOG_WARN("Couldn't get PNP device id. Error: %d", Status);
            }

            WnbdReleaseDevice(Device);
        }
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
                WNBD_LOG_WARN("Could not query SCSI address. Error: 0x%x.", Status);
                break;
            }

            WNBD_LOG_DEBUG("Removing disk device.");
            PWNBD_DISK_DEVICE Device = WnbdFindDeviceByAddr(
                GlobalExt, ScsiAddress.PathId,
                ScsiAddress.TargetId, ScsiAddress.Lun, TRUE);
            if (!Device) {
                WNBD_LOG_DEBUG("Device already removed.");
                break;
            }
            if (Device->PDO != DeviceObject) {
                WNBD_LOG_INFO(
                    "Different device found at the specified address. "
                    "The requested device might've been removed already.");
                WnbdReleaseDevice(Device);
                break;
            }
            WNBD_LOG_INFO("Disconnecting disk: %s.",
                          Device->Properties.InstanceName);
            WnbdDisconnectSync(Device);
            WNBD_LOG_INFO("Successfully disconnected disk: %s",
                          Device->Properties.InstanceName);
        }
        break;
    }

    Status = StorPortDispatchPnp(DeviceObject, Irp);

    WNBD_LOG_DEBUG("Exit: 0x%x", Status);
    return Status;
}

_Use_decl_annotations_
VOID
WnbdDriverUnload(PDRIVER_OBJECT DriverObject)
{

    if (0 != StorPortDriverUnload) {
        StorPortDriverUnload(DriverObject);
    }

    /*
     *  Unregister from ETW
     */
    EventUnregisterWNBD();
    WPP_CLEANUP(DriverObject);
}
