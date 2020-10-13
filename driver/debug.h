/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#ifndef DEBUG_H
#define DEBUG_H 1

#include <ntdef.h>
#include <wdm.h>

#define WNBD_LVL_ERROR    DPFLTR_ERROR_LEVEL
#define WNBD_LVL_WARN     DPFLTR_WARNING_LEVEL
#define WNBD_LVL_TRACE    DPFLTR_TRACE_LEVEL
#define WNBD_LVL_INFO     DPFLTR_INFO_LEVEL
#define WNBD_LVL_LOUD     (DPFLTR_INFO_LEVEL + 1)

VOID
WnbdLog(_In_ UINT32 Level,
        _In_ PCHAR FuncName,
        _In_ UINT32 Line,
        _In_ PCHAR Format, ...);

#define WNBD_LOG_LOUD(_format, ...) \
   WnbdLog(WNBD_LVL_LOUD, __FUNCTION__, __LINE__, _format,  __VA_ARGS__)

#define WNBD_LOG_INFO(_format, ...) \
   WnbdLog(WNBD_LVL_INFO, __FUNCTION__, __LINE__, _format, __VA_ARGS__)

#define WNBD_LOG_TRACE(_format, ...) \
   WnbdLog(WNBD_LVL_TRACE, __FUNCTION__, __LINE__, _format, __VA_ARGS__)

#define WNBD_LOG_ERROR(_format, ...) \
   WnbdLog(WNBD_LVL_ERROR, __FUNCTION__, __LINE__, _format, __VA_ARGS__)

#define WNBD_LOG_WARN(_format, ...) \
   WnbdLog(WNBD_LVL_WARN, __FUNCTION__, __LINE__, _format, __VA_ARGS__)

#endif
