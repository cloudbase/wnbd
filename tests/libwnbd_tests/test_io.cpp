#include "pch.h"
#include "mock_wnbd_daemon.h"
#include "utils.h"

void TestWrite(
    uint64_t BlockCount = DefaultBlockCount,
    uint32_t BlockSize = DefaultBlockSize,
    bool CacheEnabled = true,
    bool UseFUA = false)
{
    auto InstanceName = GetNewInstanceName();

    MockWnbdDaemon WnbdDaemon(
        InstanceName,
        BlockCount,
        BlockSize,
        false, // writable
        CacheEnabled
    );
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    std::string DiskPath = GetDiskPath(InstanceName);
    DWORD OpenFlags = FILE_ATTRIBUTE_NORMAL;
    if (UseFUA) {
        OpenFlags |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
    }
    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        OpenFlags,
        NULL);
    ASSERT_NE(INVALID_HANDLE_VALUE, DiskHandle)
        << "couldn't open disk: " << DiskPath
        << ", error: " << WinStrError(GetLastError());
    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    int WriteBufferSize = 4 << 20;
    std::unique_ptr<void, decltype(&free)> WriteBuffer(
        malloc(WriteBufferSize), free);
    ASSERT_TRUE(WriteBuffer.get()) << "couldn't allocate: " << WriteBufferSize;
    memset(WriteBuffer.get(), WRITE_BYTE_CONTENT, WriteBufferSize);

    WNBD_IO_REQUEST ExpWnbdRequest = { 0 };
    ExpWnbdRequest.RequestType = WnbdReqTypeWrite;
    ExpWnbdRequest.Cmd.Write.BlockAddress = 0;
    ExpWnbdRequest.Cmd.Write.BlockCount = 1;
    ExpWnbdRequest.Cmd.Write.ForceUnitAccess = UseFUA && CacheEnabled;
    // We expect to be the first ones writing to this disk.
    ASSERT_FALSE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));

    DWORD BytesWritten = 0;
    // 1 block
    ASSERT_TRUE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        BlockSize, &BytesWritten, NULL));
    ASSERT_EQ(BlockSize, BytesWritten);
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest, WriteBuffer.get(),
        BlockSize));

    // twice the maximum transfer size
    ASSERT_TRUE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        2 * WNBD_DEFAULT_MAX_TRANSFER_LENGTH,
        &BytesWritten, NULL));
    ASSERT_EQ(2 * WNBD_DEFAULT_MAX_TRANSFER_LENGTH, BytesWritten);

    // We expect writes larger than the max transfer length to be fragmented.
    // Here's the first fragment.
    ExpWnbdRequest.Cmd.Write.BlockAddress = 1;
    ExpWnbdRequest.Cmd.Write.BlockCount =
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH / BlockSize;
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(
        ExpWnbdRequest, WriteBuffer.get(),
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH));

    // Second fragment
    ExpWnbdRequest.Cmd.Write.BlockAddress +=
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH / BlockSize;
    ExpWnbdRequest.Cmd.Write.BlockCount =
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH / BlockSize;
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(
        ExpWnbdRequest, WriteBuffer.get(),
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH));

    // Write one block at the end of the disk.
    LARGE_INTEGER Offset;
    Offset.QuadPart = (BlockCount - 1) * BlockSize;
    ASSERT_TRUE(SetFilePointerEx(
        DiskHandle,
        Offset,
        NULL, FILE_BEGIN));
    ASSERT_TRUE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        BlockSize, &BytesWritten, NULL));
    ASSERT_EQ(BlockSize, BytesWritten);

    ExpWnbdRequest.Cmd.Write.BlockAddress = BlockCount - 1;
    ExpWnbdRequest.Cmd.Write.BlockCount = 1;
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest, WriteBuffer.get(),
        BlockSize));

    // Write passed the end of the disk, expecting a failure.
    ASSERT_FALSE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        BlockSize, &BytesWritten, NULL));
    ASSERT_EQ(0, BytesWritten);

    ASSERT_TRUE(FlushFileBuffers(DiskHandle));

    ExpWnbdRequest = { 0 };
    ExpWnbdRequest.RequestType = WnbdReqTypeFlush;
    ASSERT_EQ(CacheEnabled, WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));
}

TEST(TestWrite, CacheEnabled) {
    TestWrite();
}

TEST(TestWrite, CacheDisabled) {
    TestWrite(
        DefaultBlockCount, DefaultBlockSize,
        false);
}

// TODO: allow 4k sector sizes once we sort out the crashes
//
// TEST(TestWrite, Write4kSector) {
//     TestWrite(
//         DefaultBlockCount, 4096);
// }

TEST(TestWrite, Write2PBDisk) {
    TestWrite(
        (2LL << 50) / DefaultBlockSize,
        DefaultBlockSize);
}

TEST(TestWrite, FUACacheDisabled) {
    TestWrite(
        DefaultBlockCount, DefaultBlockSize,
        false, true);
}

TEST(TestWrite, FUACacheEnabled) {
    TestWrite(
        DefaultBlockCount, DefaultBlockSize,
        true, true);
}

TEST(TestWrite, WriteReadOnly) {
    auto InstanceName = GetNewInstanceName();

    MockWnbdDaemon WnbdDaemon(
        InstanceName,
        DefaultBlockCount,
        DefaultBlockSize,
        true, // non-writable
        true
    );
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    std::string DiskPath = GetDiskPath(InstanceName);

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    ASSERT_NE(INVALID_HANDLE_VALUE, DiskHandle)
        << "couldn't open disk: " << DiskPath
        << ", error: " << WinStrError(GetLastError());
    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    int WriteBufferSize = 4 << 20;
    std::unique_ptr<void, decltype(&free)> WriteBuffer(
        malloc(WriteBufferSize), free);
    ASSERT_TRUE(WriteBuffer.get()) << "couldn't allocate: " << WriteBufferSize;
    memset(WriteBuffer.get(), WRITE_BYTE_CONTENT, WriteBufferSize);

    WNBD_IO_REQUEST ExpWnbdRequest = { 0 };
    ExpWnbdRequest.RequestType = WnbdReqTypeWrite;
    ExpWnbdRequest.Cmd.Write.BlockAddress = 0;
    ExpWnbdRequest.Cmd.Write.BlockCount = 1;
    // We expect to be the first ones writing to this disk.
    ASSERT_FALSE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));

    DWORD BytesWritten = 0;

    // Expect failure since disk is read-only
    ASSERT_FALSE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        DefaultBlockSize, &BytesWritten, NULL));
    ASSERT_EQ(0, BytesWritten);
}

void TestRead(
    uint64_t BlockCount = DefaultBlockCount,
    uint32_t BlockSize = DefaultBlockSize,
    bool CacheEnabled = true)
{
    auto InstanceName = GetNewInstanceName();

    MockWnbdDaemon WnbdDaemon(
        InstanceName,
        BlockCount,
        BlockSize,
        false, // writable
        CacheEnabled
    );
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    std::string DiskPath = GetDiskPath(InstanceName);
    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        NULL,
        NULL);
    ASSERT_NE(INVALID_HANDLE_VALUE, DiskHandle)
        << "couldn't open disk: " << DiskPath
        << ", error: " << WinStrError(GetLastError());
    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    int ReadBufferSize = 4 << 20;
    std::unique_ptr<void, decltype(&free)> ReadBuffer(
        malloc(ReadBufferSize), free);
    ASSERT_TRUE(ReadBuffer.get()) << "couldn't allocate: " << ReadBufferSize;

    int ExpReadBufferSize = 4 << 20;
    std::unique_ptr<void, decltype(&free)> ExpReadBuffer(
        malloc(ExpReadBufferSize), free);
    ASSERT_TRUE(ExpReadBuffer.get()) << "couldn't allocate: " << ExpReadBufferSize;
    memset(ExpReadBuffer.get(), READ_BYTE_CONTENT, ExpReadBufferSize);

    DWORD BytesRead = 0;
    // Clear read buffer
    memset(ReadBuffer.get(), 0, ReadBufferSize);
    // 1 block
    ASSERT_TRUE(ReadFile(
        DiskHandle, ReadBuffer.get(),
        BlockSize, &BytesRead, NULL));
    ASSERT_EQ(BlockSize, BytesRead);

    WNBD_IO_REQUEST ExpWnbdRequest = { 0 };
    ExpWnbdRequest.RequestType = WnbdReqTypeRead;
    ExpWnbdRequest.Cmd.Read.BlockAddress = 0;
    ExpWnbdRequest.Cmd.Read.BlockCount = 1;
    // TODO: Windows doesn't set the FUA read flag, even when using
    // FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH.
    // We should consider using a passthrough scsi command to test read FUA.

    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));
    ASSERT_FALSE(
        memcmp(ReadBuffer.get(), ExpReadBuffer.get(), BlockSize));

    // twice the maximum transfer size
    memset(ReadBuffer.get(), 0, ReadBufferSize);
    ASSERT_TRUE(ReadFile(
        DiskHandle, ReadBuffer.get(),
        2 * WNBD_DEFAULT_MAX_TRANSFER_LENGTH,
        &BytesRead, NULL));
    ASSERT_EQ(2 * WNBD_DEFAULT_MAX_TRANSFER_LENGTH, BytesRead);

    // We expect reads larger than the max transfer length to be fragmented.
    // Here's the first fragment.
    ExpWnbdRequest.Cmd.Read.BlockAddress = 1;
    ExpWnbdRequest.Cmd.Read.BlockCount =
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH / BlockSize;
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(
        ExpWnbdRequest, ReadBuffer.get(),
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH));
    ASSERT_FALSE(
        memcmp(ReadBuffer.get(), ExpReadBuffer.get(),
            WNBD_DEFAULT_MAX_TRANSFER_LENGTH));

    // Second fragment
    ExpWnbdRequest.Cmd.Read.BlockAddress +=
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH / BlockSize;
    ExpWnbdRequest.Cmd.Read.BlockCount =
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH / BlockSize;
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(
        ExpWnbdRequest, ReadBuffer.get(),
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH));
    ASSERT_FALSE(
        memcmp(ReadBuffer.get(), ExpReadBuffer.get(),
            WNBD_DEFAULT_MAX_TRANSFER_LENGTH));

    // Read one block at the end of the disk.
    memset(ReadBuffer.get(), 0, ReadBufferSize);
    LARGE_INTEGER Offset;
    Offset.QuadPart = (BlockCount - 1) * BlockSize;
    ASSERT_TRUE(SetFilePointerEx(
        DiskHandle,
        Offset,
        NULL, FILE_BEGIN));
    ASSERT_TRUE(ReadFile(
        DiskHandle, ReadBuffer.get(),
        BlockSize, &BytesRead, NULL));
    ASSERT_EQ(BlockSize, BytesRead);

    ExpWnbdRequest.Cmd.Read.BlockAddress = BlockCount - 1;
    ExpWnbdRequest.Cmd.Read.BlockCount = 1;
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest, ReadBuffer.get(),
        BlockSize));
    ASSERT_FALSE(
        memcmp(ReadBuffer.get(), ExpReadBuffer.get(), BlockSize));

    // Read passed the end of the disk, expecting a failure.
    ASSERT_FALSE(ReadFile(
        DiskHandle, ReadBuffer.get(),
        BlockSize, &BytesRead, NULL));
    ASSERT_EQ(0, BytesRead);
}

TEST(TestRead, CacheEnabled) {
    TestRead();
}

TEST(TestRead, CacheDisabled) {
    TestRead(
        DefaultBlockCount, DefaultBlockSize,
        false);
}

// TODO: allow 4k sector sizes once we sort out the crashes
//
// TEST(TestRead, Read4kSector) {
//     TestRead(
//         DefaultBlockCount, 4096);
// }

TEST(TestRead, Read2PBDisk) {
    TestRead(
        (2LL << 50) / DefaultBlockSize,
        DefaultBlockSize);
}

TEST(TestIoStats, TestIoStats) {
    auto InstanceName = GetNewInstanceName();

    MockWnbdDaemon WnbdDaemon(
        InstanceName,
        DefaultBlockCount,
        DefaultBlockSize,
        false, // writable
        true
    );
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    std::string DiskPath = GetDiskPath(InstanceName);

    WNBD_USR_STATS UserspaceStats = { 0 };
    WNBD_DRV_STATS DriverStats = { 0 };

    WnbdGetUserspaceStats(WnbdDisk, &UserspaceStats);
    WnbdGetDriverStats(InstanceName.c_str(), &DriverStats);

    // No requests have been sent, write counters should be 0
    ASSERT_EQ(UserspaceStats.TotalWrittenBlocks, 0);
    ASSERT_EQ(UserspaceStats.WriteErrors, 0);

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    ASSERT_NE(INVALID_HANDLE_VALUE, DiskHandle)
        << "couldn't open disk: " << DiskPath
        << ", error: " << WinStrError(GetLastError());
    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    int WriteBufferSize = 4 << 20;
    std::unique_ptr<void, decltype(&free)> WriteBuffer(
        malloc(WriteBufferSize), free);
    ASSERT_TRUE(WriteBuffer.get()) << "couldn't allocate: " << WriteBufferSize;
    memset(WriteBuffer.get(), WRITE_BYTE_CONTENT, WriteBufferSize);

    int ReadBufferSize = 4 << 20;
    std::unique_ptr<void, decltype(&free)> ReadBuffer(
        malloc(ReadBufferSize), free);
    ASSERT_TRUE(ReadBuffer.get()) << "couldn't allocate: " << ReadBufferSize;

    DWORD BytesRead = 0;

    WNBD_IO_REQUEST ExpWnbdRequest = { 0 };
    ExpWnbdRequest.RequestType = WnbdReqTypeWrite;
    ExpWnbdRequest.Cmd.Write.BlockAddress = 0;
    ExpWnbdRequest.Cmd.Write.BlockCount = 1;
    // We expect to be the first ones writing to this disk.
    ASSERT_FALSE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));

    DWORD BytesWritten = 0;
    ASSERT_TRUE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        DefaultBlockSize, &BytesWritten, NULL));
    ASSERT_EQ(DefaultBlockSize, BytesWritten);
    ASSERT_TRUE(FlushFileBuffers(DiskHandle));

    WnbdGetUserspaceStats(WnbdDisk, &UserspaceStats);
    WnbdGetDriverStats(InstanceName.c_str(), &DriverStats);
    EVENTUALLY(UserspaceStats.TotalWrittenBlocks >= 1, 20, 100);
    EVENTUALLY(UserspaceStats.WriteErrors >= 0, 20, 100);
    EVENTUALLY(DriverStats.TotalReceivedIORequests >= 1, 20, 100);
    EVENTUALLY(DriverStats.TotalReceivedIOReplies >= 1, 20, 100);

    // Clear read buffer
    memset(ReadBuffer.get(), 0, ReadBufferSize);
    // 1 block
    ASSERT_TRUE(ReadFile(
        DiskHandle, ReadBuffer.get(),
        DefaultBlockSize, &BytesRead, NULL)) << "Read failed: "
        << GetLastError() << std::endl;
    ASSERT_EQ(DefaultBlockSize, BytesRead);

    EVENTUALLY(UserspaceStats.TotalReadBlocks >= 1, 20, 100);
    EVENTUALLY(UserspaceStats.TotalRWRequests >= 2, 20, 100);
    EVENTUALLY(DriverStats.TotalReceivedIORequests >= 2, 20, 100);
    EVENTUALLY(DriverStats.TotalReceivedIOReplies >= 2, 20, 100);

    // Write one block past the end of the disk.
    LARGE_INTEGER Offset;
    Offset.QuadPart = (DefaultBlockCount)*DefaultBlockSize;
    ASSERT_TRUE(SetFilePointerEx(
        DiskHandle,
        Offset,
        NULL, FILE_BEGIN));
    ASSERT_FALSE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        DefaultBlockSize, &BytesWritten, NULL));
    ASSERT_EQ(0, BytesWritten);

    BytesWritten = 0;
    // Write passed the end of the disk, expecting a failure.
    ASSERT_FALSE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        DefaultBlockSize, &BytesWritten, NULL));
    ASSERT_EQ(0, BytesWritten);

    WnbdGetUserspaceStats(WnbdDisk, &UserspaceStats);
    WnbdGetDriverStats(InstanceName.c_str(), &DriverStats);
    EVENTUALLY(UserspaceStats.WriteErrors >= 1, 20, 100);
    EVENTUALLY(UserspaceStats.TotalRWRequests >= 3, 20, 100);
    EVENTUALLY(DriverStats.TotalReceivedIORequests >= 3, 20, 100);
    EVENTUALLY(DriverStats.TotalReceivedIOReplies >= 3, 20, 100);

    ASSERT_EQ(UserspaceStats.InvalidRequests, 0);

    // TODO: flaky test, temporarily disabled.
    //
    // Move file pointer back to the beggining of the disk
    // Offset.QuadPart = 0;
    // ASSERT_TRUE(SetFilePointerEx(
    //     DiskHandle,
    //     Offset,
    //     NULL, FILE_BEGIN));
    // ASSERT_TRUE(WriteFile(
    //     DiskHandle, WriteBuffer.get(),
    //     DefaultBlockSize, &BytesWritten, NULL));
    // ASSERT_EQ(DefaultBlockSize, BytesWritten);

    // WnbdDisk->Properties.Flags.FlushSupported = 0;
    // ASSERT_FALSE(FlushFileBuffers(DiskHandle));
    // WnbdGetUserspaceStats(WnbdDisk, &UserspaceStats);

    // EVENTUALLY(UserspaceStats.InvalidRequests >= 1, 150, 100);
}
