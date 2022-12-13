/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <mutex>
#include <list>
#include <string.h>

#include <wnbd.h>

struct RequestLogEntry
{
    WNBD_IO_REQUEST WnbdRequest;
    std::shared_ptr<void> DataBuffer;
    size_t DataBufferSize;

    RequestLogEntry(
            WNBD_IO_REQUEST _WnbdRequest,
            void* _DataBuffer,
            size_t _DataBufferSize)
        : WnbdRequest(_WnbdRequest)
        , DataBuffer(std::shared_ptr<void>(_DataBuffer, free))
        , DataBufferSize(_DataBufferSize)
    {}

    RequestLogEntry(WNBD_IO_REQUEST _WnbdRequest)
        : WnbdRequest(_WnbdRequest)
        , DataBuffer(nullptr)
        , DataBufferSize(0)
    {}
};

struct RequestLog
{
    std::mutex ListLock;
    std::list<RequestLogEntry> Entries;

    // We'll ensure that there are no duplicate request handles
    std::set<UINT64> RequestHandles;

    void PushBack(const RequestLogEntry& Entry);

    // Adds the specified Entry to the request log. If a buffer is specified,
    // we'll make a copy.
    void AddEntry(
        WNBD_IO_REQUEST& WnbdRequest,
        void* DataBuffer,
        size_t DataBufferSize);

    void AddEntry(WNBD_IO_REQUEST& WnbdRequest)
    {
        RequestLogEntry Entry(WnbdRequest, nullptr, 0);
        PushBack(Entry);
    }

    bool HasEntry(
        WNBD_IO_REQUEST& ExpWnbdRequest,
        void* DataBuffer,
        size_t DataBufferSize,
        bool CheckDataBuffer);

    // Searches for the specified request, ignoring the request handle.
    bool HasEntry(
        WNBD_IO_REQUEST& ExpWnbdRequest,
        void* DataBuffer,
        size_t DataBufferSize)
    {
        return HasEntry(ExpWnbdRequest, DataBuffer, DataBufferSize, true);
    }

    // Searches for the specified request, ignoring the request handle
    // and data buffers.
    bool HasEntry(
        WNBD_IO_REQUEST& ExpWnbdRequest)
    {
        return HasEntry(ExpWnbdRequest, nullptr, 0, false);
    }
};
