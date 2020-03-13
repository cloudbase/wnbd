/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef USERSPACE_SHARED_H
#define USERSPACE_SHARED_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NAME_LENGTH    256

#define FILE_DEVICE_WNBD      39088
#define USER_WNBD_IOCTL_START   3908

#define IOCTL_WNBDVM_PORT  CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START, METHOD_BUFFERED, FILE_ALL_ACCESS)
#define IOCTL_WNBDVM_MAP   CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START+1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_WNBDVM_UNMAP CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START+2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_WNBDVM_LIST  CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START+3, METHOD_BUFFERED, FILE_WRITE_ACCESS)

static const GUID WNBD_GUID = {
      0x949dd17c,
      0xb06c,
      0x4c14,
      {0x8e, 0x13, 0xf1, 0xa3, 0xa4, 0xa6, 0xdb, 0xcb}
};

typedef struct _USER_IN {
    ULONG		    IoControlCode;
    CHAR            InstanceName[MAX_NAME_LENGTH];
    CHAR            Hostname[MAX_NAME_LENGTH];
    CHAR            PortName[MAX_NAME_LENGTH];
    CHAR            ExportName[MAX_NAME_LENGTH];
    CHAR            SerialNumber[MAX_NAME_LENGTH];
    BOOLEAN         MustNegotiate;
    INT             Pid;
    UINT64          DiskSize;
    UINT16          BlockSize;
} USER_IN, * PUSER_IN;

typedef struct _USER_COMMAND {
    ULONG		IoCode;
} USER_COMMAND, *PUSER_COMMAND;

typedef struct _LIST_ENTRY_OUT {
    USER_IN         ConnectionInformation;
    USHORT          Connected;
    USHORT          BusNumber;
    USHORT          TargetId;
    USHORT          Lun;
    ULONGLONG       DiskSize;
} LIST_ENTRY_OUT, *PLIST_ENTRY_OUT;

typedef struct _GET_LIST_OUT {
    ULONG                   ActiveListCount;
    LIST_ENTRY_OUT          ActiveEntry[1];
} GET_LIST_OUT, *PGET_LIST_OUT;

#ifdef __cplusplus
}
#endif

#endif
