/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "pch.h"

#include "request_log.h"

void RequestLog::PushBack(const RequestLogEntry& Entry)
{
    std::unique_lock<std::mutex> Lock(ListLock);

    auto RequestHandle = Entry.WnbdRequest.RequestHandle;

    // Validate the request handle.
    auto Result = RequestHandles.insert(RequestHandle);
    ASSERT_TRUE(Result.second)
        << "duplicate request handle received : "
        << RequestHandle;

    Entries.push_back(Entry);
}

void RequestLog::AddEntry(
    WNBD_IO_REQUEST& WnbdRequest,
    void* DataBuffer,
    size_t DataBufferSize)
{
    void* BufferCopy = nullptr;
    size_t BufferCopySize = 0;

    if (DataBuffer && DataBufferSize > 0) {
        BufferCopySize = DataBufferSize;
        BufferCopy = malloc(BufferCopySize);
        ASSERT_TRUE(BufferCopy)
            << "couldn't allocate buffer, size: " << BufferCopySize;
        memcpy(BufferCopy, DataBuffer, BufferCopySize);
    }

    RequestLogEntry Entry(WnbdRequest, BufferCopy, BufferCopySize);
    PushBack(Entry);
}

bool RequestLog::HasEntry(
    WNBD_IO_REQUEST& ExpWnbdRequest,
    void* DataBuffer,
    size_t DataBufferSize,
    bool CheckDataBuffer)
{
    std::unique_lock<std::mutex> Lock(ListLock);

    for (auto Entry: Entries) {
        // Skip the request handle when comparing the requests
        if (!memcmp((char*) &ExpWnbdRequest + sizeof(ExpWnbdRequest.RequestHandle),
                   (char*) &Entry + sizeof(ExpWnbdRequest.RequestHandle),
                   sizeof(WNBD_IO_REQUEST) - sizeof(ExpWnbdRequest.RequestHandle))) {
            if (CheckDataBuffer) {
                if (!Entry.DataBuffer.get()) {
                    continue;
                }

                // Found a matching request, let's compare the buffers.
                if (Entry.DataBufferSize != DataBufferSize) {
                    continue;
                }

                if (memcmp(DataBuffer, Entry.DataBuffer.get(), DataBufferSize)) {
                    continue;
                }
            }

            // Found the expected request
            return true;
        }
    }
    return false;
}
