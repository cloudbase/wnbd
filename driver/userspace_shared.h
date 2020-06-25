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

#define IOCTL_WNBD_PORT   CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START, METHOD_BUFFERED, FILE_ALL_ACCESS)
#define IOCTL_WNBD_MAP    CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START+1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_WNBD_UNMAP  CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START+2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_WNBD_LIST   CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START+3, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_WNBD_DEBUG  CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START+4, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_WNBD_STATS CTL_CODE(FILE_DEVICE_WNBD, USER_WNBD_IOCTL_START+5, METHOD_BUFFERED, FILE_WRITE_ACCESS)

static const GUID WNBD_GUID = {
      0x949dd17c,
      0xb06c,
      0x4c14,
      {0x8e, 0x13, 0xf1, 0xa3, 0xa4, 0xa6, 0xdb, 0xcb}
};

typedef struct _CONNECTION_INFO {
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
    UINT16          NbdFlags;
} CONNECTION_INFO, * PCONNECTION_INFO;

typedef struct _WNBD_COMMAND {
    ULONG		IoCode;
} WNBD_COMMAND, *PWNBD_COMMAND;

typedef struct _DISK_INFO {
    CONNECTION_INFO         ConnectionInformation;
    USHORT          Connected;
    USHORT          BusNumber;
    USHORT          TargetId;
    USHORT          Lun;
    ULONGLONG       DiskSize;
} DISK_INFO, *PDISK_INFO;

typedef struct _DISK_INFO_LIST {
    ULONG                   ActiveListCount;
    DISK_INFO          ActiveEntry[1];
} DISK_INFO_LIST, *PDISK_INFO_LIST;

typedef struct _WNBD_STATS {
    INT64 TotalReceivedIORequests;
    INT64 TotalSubmittedIORequests;
    INT64 TotalReceivedIOReplies;
    INT64 UnsubmittedIORequests;
    INT64 PendingSubmittedIORequests;
    INT64 AbortedSubmittedIORequests;
    INT64 AbortedUnsubmittedIORequests;
    INT64 CompletedAbortedIORequests;
} WNBD_STATS, *PWNBD_STATS;

#ifdef __cplusplus
}
#endif

#endif
