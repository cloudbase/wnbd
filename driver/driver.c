/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "common.h"
#include "debug.h"
#include "driver.h"
#include "driver_extension.h"
#include "scsi_driver_extensions.h"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD WnbdDriverUnload;
DRIVER_DISPATCH WnbdDispatchPnp;
PDRIVER_UNLOAD StorPortDriverUnload;
PDRIVER_DISPATCH StorPortDispatchPnp;

extern PGLOBAL_INFORMATION GlobalInformation;

_Use_decl_annotations_
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath)
{
    WNBD_LOG_LOUD(": Enter");

    /*
     * Register Virtual Storport Miniport data
     */
    NTSTATUS Status;
    VIRTUAL_HW_INITIALIZATION_DATA WnbdInitData = { 0 };
    WnbdInitData.HwInitializationDataSize = sizeof(VIRTUAL_HW_INITIALIZATION_DATA);

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
    WnbdInitData.SpecificLuExtensionSize  = sizeof(WNBD_LU_EXTENSION);
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
    ASSERT(IoLocation);

    switch (IoLocation->MinorFunction) {
    case IRP_MN_QUERY_CAPABILITIES:
        /*
         * Set our device capability
         */
        WNBD_LOG_INFO("IRP_MN_QUERY_CAPABILITIES");
        IoLocation->Parameters.DeviceCapabilities.Capabilities->Removable = 1;
        IoLocation->Parameters.DeviceCapabilities.Capabilities->SilentInstall = 1;
        IoLocation->Parameters.DeviceCapabilities.Capabilities->SurpriseRemovalOK = 1;
        break;

    case IRP_MN_START_DEVICE:
        WNBD_LOG_INFO("IRP_MN_START_DEVICE");
        break;

    case IRP_MN_REMOVE_DEVICE:
        WNBD_LOG_INFO("IRP_MN_REMOVE_DEVICE");
        break;
    }

    Status = StorPortDispatchPnp(DeviceObject, Irp);

    WNBD_LOG_LOUD(": Exit");
    return Status;
}

_Use_decl_annotations_
VOID
WnbdDriverUnload(PDRIVER_OBJECT DriverObject)
{
    WNBD_LOG_LOUD(": Enter");

    WnbdDeleteGlobalInformation(GlobalInformation);
    if (0 != StorPortDriverUnload) {
        StorPortDriverUnload(DriverObject);
    }

    WNBD_LOG_LOUD(": Exit");
}
