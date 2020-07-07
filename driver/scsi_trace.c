/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "common.h"
#include "scsi_trace.h"

PCHAR SRB_FUNCTION_STRINGS[] = {
    (PCHAR)"SRB_FUNCTION_EXECUTE_SCSI           0x00",
    (PCHAR)"SRB_FUNCTION_CLAIM_DEVICE           0x01",
    (PCHAR)"SRB_FUNCTION_IO_CONTROL             0x02",
    (PCHAR)"SRB_FUNCTION_RECEIVE_EVENT          0x03",
    (PCHAR)"SRB_FUNCTION_RELEASE_QUEUE          0x04",
    (PCHAR)"SRB_FUNCTION_ATTACH_DEVICE          0x05",
    (PCHAR)"SRB_FUNCTION_RELEASE_DEVICE         0x06",
    (PCHAR)"SRB_FUNCTION_SHUTDOWN               0x07",
    (PCHAR)"SRB_FUNCTION_FLUSH                  0x08",
    (PCHAR)"SRB_FUNCTION_PROTOCOL_COMMAND       0x09",
    (PCHAR)"SRB_FUNCTION_ABORT_COMMAND          0x10",
    (PCHAR)"SRB_FUNCTION_RELEASE_RECOVERY       0x11",
    (PCHAR)"SRB_FUNCTION_RESET_BUS              0x12",
    (PCHAR)"SRB_FUNCTION_RESET_DEVICE           0x13",
    (PCHAR)"SRB_FUNCTION_TERMINATE_IO           0x14",
    (PCHAR)"SRB_FUNCTION_FLUSH_QUEUE            0x15",
    (PCHAR)"SRB_FUNCTION_REMOVE_DEVICE          0x16",
    (PCHAR)"SRB_FUNCTION_WMI                    0x17",
    (PCHAR)"SRB_FUNCTION_LOCK_QUEUE             0x18",
    (PCHAR)"SRB_FUNCTION_UNLOCK_QUEUE           0x19",
    (PCHAR)"SRB_FUNCTION_QUIESCE_DEVICE         0x1a",
    (PCHAR)"SRB_FUNCTION_RESET_LOGICAL_UNIT     0x20",
    (PCHAR)"SRB_FUNCTION_SET_LINK_TIMEOUT       0x21",
    (PCHAR)"SRB_FUNCTION_LINK_TIMEOUT_OCCURRED  0x22",
    (PCHAR)"SRB_FUNCTION_LINK_TIMEOUT_COMPLETE  0x23",
    (PCHAR)"SRB_FUNCTION_POWER                  0x24",
    (PCHAR)"SRB_FUNCTION_PNP                    0x25",
    (PCHAR)"SRB_FUNCTION_DUMP_POINTERS          0x26",
    (PCHAR)"SRB_FUNCTION_FREE_DUMP_POINTERS     0x27",
    (PCHAR)"SRB_FUNCTION_STORAGE_REQUEST_BLOCK  0x28"
};

PCHAR SRB_STATUS_STRINGS[] = {
    (PCHAR)"SRB_STATUS_PENDING                  0x00",
    (PCHAR)"SRB_STATUS_SUCCESS                  0x01",
    (PCHAR)"SRB_STATUS_ABORTED                  0x02",
    (PCHAR)"SRB_STATUS_ABORT_FAILED             0x03",
    (PCHAR)"SRB_STATUS_ERROR                    0x04",
    (PCHAR)"SRB_STATUS_BUSY                     0x05",
    (PCHAR)"SRB_STATUS_INVALID_REQUEST          0x06",
    (PCHAR)"SRB_STATUS_INVALID_PATH_ID          0x07",
    (PCHAR)"SRB_STATUS_NO_DEVICE                0x08",
    (PCHAR)"SRB_STATUS_TIMEOUT                  0x09",
    (PCHAR)"SRB_STATUS_SELECTION_TIMEOUT        0x0A",
    (PCHAR)"SRB_STATUS_COMMAND_TIMEOUT          0x0B",
    (PCHAR)"SRB_STATUS_MESSAGE_REJECTED         0x0D",
    (PCHAR)"SRB_STATUS_BUS_RESET                0x0E",
    (PCHAR)"SRB_STATUS_PARITY_ERROR             0x0F",
    (PCHAR)"SRB_STATUS_REQUEST_SENSE_FAILED     0x10",
    (PCHAR)"SRB_STATUS_NO_HBA                   0x11",
    (PCHAR)"SRB_STATUS_DATA_OVERRUN             0x12",
    (PCHAR)"SRB_STATUS_UNEXPECTED_BUS_FREE      0x13",
    (PCHAR)"SRB_STATUS_PHASE_SEQUENCE_FAILURE   0x14",
    (PCHAR)"SRB_STATUS_BAD_SRB_BLOCK_LENGTH     0x15",
    (PCHAR)"SRB_STATUS_REQUEST_FLUSHED          0x16",
    (PCHAR)"SRB_STATUS_INVALID_LUN              0x20",
    (PCHAR)"SRB_STATUS_INVALID_TARGET_ID        0x21",
    (PCHAR)"SRB_STATUS_BAD_FUNCTION             0x22",
    (PCHAR)"SRB_STATUS_ERROR_RECOVERY           0x23",
    (PCHAR)"SRB_STATUS_NOT_POWERED              0x24",
    (PCHAR)"SRB_STATUS_LINK_DOWN                0x25"
};

_Use_decl_annotations_
PCHAR
WnbdToStringSrbFunction(UCHAR SrbFunction)
{
    return SRB_FUNCTION_STRINGS[SrbFunction];
}

_Use_decl_annotations_
PCHAR
WnbdToStringSrbStatus(UCHAR SrbStatus)
{
    // TODO: switch to a "switch", this might cause an overflow if new
    // fields are added.
    return SRB_STATUS_STRINGS[SrbStatus];
}
