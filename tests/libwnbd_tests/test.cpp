/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "pch.h"

#include "mock_wnbd_daemon.h"
#include "utils.h"

#include <stdexcept>

static const uint64_t DefaultBlockCount = 1 << 20;
static const uint64_t DefaultBlockSize = 512;

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    std::string WnbdLogLevelStr = GetEnv("WNBD_LOG_LEVEL");
    try {
        if (!WnbdLogLevelStr.empty()) {
            int LogLevel = std::stoi(WnbdLogLevelStr);
            WnbdSetLogLevel((WnbdLogLevel) LogLevel);
        }
    } catch (...) {
        std::cerr << "invalid wnbd log level: " << WnbdLogLevelStr << std::endl;
        exit(1);
    }

    return RUN_ALL_TESTS();
}

std::string GetNewInstanceName()
{
    auto TestInstance = ::testing::UnitTest::GetInstance();
    auto TestName = std::string(TestInstance->current_test_info()->name());
    return TestName + "-" + std::to_string(rand());
}

void TestMap(
    uint64_t BlockCount = DefaultBlockCount,
    uint32_t BlockSize = DefaultBlockSize,
    bool ReadOnly = false,
    bool CacheEnabled = true)
{
    auto InstanceName = GetNewInstanceName();

    MockWnbdDaemon WnbdDaemon(
        InstanceName,
        BlockCount,
        BlockSize,
        ReadOnly,
        CacheEnabled
    );
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    EXPECT_EQ(
        InstanceName,
        std::string(ConnectionInfo.Properties.InstanceName));
    EXPECT_EQ(
        InstanceName,
        std::string(ConnectionInfo.Properties.SerialNumber));
    EXPECT_EQ(
        std::string(WNBD_OWNER_NAME),
        std::string(ConnectionInfo.Properties.Owner));
    EXPECT_EQ(
        BlockCount,
        ConnectionInfo.Properties.BlockCount);
    EXPECT_EQ(
        BlockSize,
        ConnectionInfo.Properties.BlockSize);
    EXPECT_EQ(
        _getpid(),
        ConnectionInfo.Properties.Pid);

    EXPECT_EQ(ReadOnly, ConnectionInfo.Properties.Flags.ReadOnly);
    EXPECT_EQ(CacheEnabled, ConnectionInfo.Properties.Flags.FlushSupported);
    EXPECT_EQ(CacheEnabled, ConnectionInfo.Properties.Flags.FUASupported);
    EXPECT_TRUE(ConnectionInfo.Properties.Flags.UnmapSupported);
    EXPECT_FALSE(ConnectionInfo.Properties.Flags.UseNbd);

    // Should be called anyway by the destructor
    WnbdDaemon.Shutdown();
}

TEST(TestMap, CacheEnabled) {
    TestMap();
}

TEST(TestMap, CacheDisabled) {
    TestMap(
        DefaultBlockCount, DefaultBlockSize,
        false, false);
}

TEST(TestMap, ReadOnly) {
    TestMap(
        DefaultBlockCount, DefaultBlockSize,
        true);
}

// TODO: we're now enforcing the sector size to be 512 due to some crashes
// that occur when accessing the read SRB data buffers while handling IO
// replies.
//
// TEST(TestMap, Map4kSector) {
//     TestMap(
//         DefaultBlockCount, 4096);
// }

TEST(TestMap, Map2PBDisk) {
    TestMap(
        (2LL << 50) / DefaultBlockSize,
        DefaultBlockSize);
}

void TestMapUnsupported(
    uint64_t BlockCount = DefaultBlockCount,
    uint32_t BlockSize = DefaultBlockSize)
{
    auto InstanceName = GetNewInstanceName();

    HANDLE AdapterHandle = INVALID_HANDLE_VALUE;
    DWORD ErrorCode = WnbdOpenAdapter(&AdapterHandle);
    ASSERT_FALSE(ErrorCode) << "unable to open WNBD adapter";
    std::unique_ptr<void, decltype(&CloseHandle)> HandleCloser(
        AdapterHandle, &CloseHandle);

    WNBD_PROPERTIES WnbdProps = { 0 };
    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };

    InstanceName.copy(WnbdProps.InstanceName, sizeof(WnbdProps.InstanceName));

    WnbdProps.BlockCount = BlockCount;
    WnbdProps.BlockSize = BlockSize;
    WnbdProps.MaxUnmapDescCount = 1;

    DWORD err = WnbdIoctlCreate(
        AdapterHandle,
        &WnbdProps,
        &ConnectionInfo,
        nullptr);
    ASSERT_TRUE(err)
        << "WnbdCreate succeeded although it was expected to fail";
}

TEST(TestMapUnsupported, UnsupportedBlockCount) {
    TestMapUnsupported(0);
}

TEST(TestMapUnsupported, UnsupportedBlockSize) {
    TestMapUnsupported(DefaultBlockCount, 0);
    TestMapUnsupported(DefaultBlockCount, 256);
    // TODO: allow 4k sector sizes once we sort out the crashes
    TestMapUnsupported(DefaultBlockCount, 4096);
    TestMapUnsupported(DefaultBlockCount, 64 * 1024);
}

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

void TestWriteReadOnly() {
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
    TestWriteReadOnly();
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

void TestLiveResize(
    uint64_t BlockCount = DefaultBlockCount,
    uint64_t NewBlockCount = 2 * DefaultBlockCount)
{
    auto InstanceName = GetNewInstanceName();

    MockWnbdDaemon WnbdDaemon(
        InstanceName,
        BlockCount,
        DefaultBlockSize,
        false,
        false
    );
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    ASSERT_EQ(BlockCount, WnbdDisk->Properties.BlockCount);
    ASSERT_EQ(WnbdSetDiskSize(WnbdDisk, NewBlockCount), ERROR_SUCCESS);
    ASSERT_EQ(NewBlockCount, WnbdDisk->Properties.BlockCount);

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
    DWORD BytesWritten = 0;

    // Set file pointer past the old disk size.
    LARGE_INTEGER Offset;

    if (NewBlockCount < BlockCount) {
        Offset.QuadPart = (BlockCount - 1) * DefaultBlockSize;
        ASSERT_TRUE(SetFilePointerEx(
            DiskHandle,
            Offset,
            NULL, FILE_BEGIN));
        // Write passed the end of the disk after shrinking, expecting a failure.
        ASSERT_FALSE(WriteFile(
            DiskHandle, WriteBuffer.get(),
            DefaultBlockSize, &BytesWritten, NULL));
        ASSERT_EQ(0, BytesWritten);
    }

    // We should be able to write before the new limit
    Offset.QuadPart = (NewBlockCount - 1) * DefaultBlockSize;
    ASSERT_TRUE(SetFilePointerEx(
        DiskHandle,
        Offset,
        NULL, FILE_BEGIN));
    ASSERT_TRUE(WriteFile(
        DiskHandle, WriteBuffer.get(),
        DefaultBlockSize, &BytesWritten, NULL));
    ASSERT_EQ(DefaultBlockSize, BytesWritten);
}

TEST(TestLiveResize, DoubleDiskSize) {
    TestLiveResize();
}

TEST(TestLiveResize, HalfDiskSize) {
    TestLiveResize(DefaultBlockCount, DefaultBlockCount/2);
}

void TestNaaIdentifier() {
    auto InstanceName = GetNewInstanceName();

    MockWnbdDaemon WnbdDaemon(
        InstanceName,
        DefaultBlockCount,
        DefaultBlockSize,
        false,
        false,
        true
    );
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    ASSERT_EQ(WnbdDisk->Properties.Flags.NaaIdSpecified, 1);

    std::string DiskPath = GetDiskPath(InstanceName);

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        NULL,
        NULL);

    ASSERT_NE(DiskHandle, INVALID_HANDLE_VALUE)
        << "couldn't open wnbd disk: " << InstanceName
        << ", error: " << WinStrError(GetLastError());
        

    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    const int DeviceIdDescriptorSize = sizeof(STORAGE_DEVICE_ID_DESCRIPTOR)
                                       + sizeof(STORAGE_IDENTIFIER) + 16;
    std::unique_ptr<STORAGE_DEVICE_ID_DESCRIPTOR> DeviceIdDescriptor(
        (PSTORAGE_DEVICE_ID_DESCRIPTOR) new BYTE[DeviceIdDescriptorSize]);

    STORAGE_PROPERTY_QUERY property_query = { 0 };
    property_query.PropertyId = StorageDeviceIdProperty;
    property_query.QueryType = PropertyStandardQuery;

    DWORD BytesReturned = 0;

    BOOL Succeeded = DeviceIoControl(
        DiskHandle,
        IOCTL_STORAGE_QUERY_PROPERTY,
        (LPVOID)&property_query,
        sizeof(STORAGE_PROPERTY_QUERY),
        (LPVOID)DeviceIdDescriptor.get(),
        DeviceIdDescriptorSize,
        &BytesReturned,
        NULL
    );

    ASSERT_NE(Succeeded, 0) << "DeviceIoControlFailed: "
                            << GetLastError() << std::endl;
    ASSERT_NE(BytesReturned, 0);

    PSTORAGE_IDENTIFIER StorageIdentifier =
        (PSTORAGE_IDENTIFIER)&DeviceIdDescriptor->Identifiers[0];

    ASSERT_EQ(StorageIdentifier->CodeSet, StorageIdCodeSetBinary);
    ASSERT_EQ(StorageIdentifier->Type, StorageIdTypeFCPHName);
    ASSERT_EQ(StorageIdentifier->Association, StorageIdAssocDevice);

    ASSERT_EQ(
        sizeof(WnbdDisk->Properties.NaaIdentifier.data),
        (unsigned int)StorageIdentifier->IdentifierSize
    );
    ASSERT_EQ(memcmp(WnbdDisk->Properties.NaaIdentifier.data,
                     StorageIdentifier->Identifier,
                     sizeof(WnbdDisk->Properties.NaaIdentifier.data)), 0)
        << "VPD 83h identifiers don't match: [ 0x"
        << ByteArrayToHex(WnbdDisk->Properties.NaaIdentifier.data, 16)
        << " ] vs [ 0x" 
        << ByteArrayToHex(StorageIdentifier->Identifier,
            (int)StorageIdentifier->IdentifierSize)
        << " ]" << std::endl;
}

TEST(TestNaaIdentifier, TestSetNaaId) {
    TestNaaIdentifier();
}

void TestDeviceSerial() {
    auto InstanceName = GetNewInstanceName();

    MockWnbdDaemon WnbdDaemon(
        InstanceName,
        DefaultBlockCount,
        DefaultBlockSize,
        false,
        false,
        false,
        true
    );
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(InstanceName.c_str(), &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    std::string DiskPath = GetDiskPath(InstanceName);

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        NULL,
        NULL);

    ASSERT_NE(DiskHandle, INVALID_HANDLE_VALUE)
        << "couldn't open wnbd disk: " << InstanceName
        << ", error: " << WinStrError(GetLastError());

    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    STORAGE_PROPERTY_QUERY storagePropertyQuery;
    ZeroMemory(&storagePropertyQuery, sizeof(STORAGE_PROPERTY_QUERY));
    storagePropertyQuery.PropertyId = StorageDeviceProperty;
    storagePropertyQuery.QueryType = PropertyStandardQuery;

    STORAGE_DESCRIPTOR_HEADER storageDescriptorHeader = { 0 };
    DWORD dwBytesReturned = 0;
    ASSERT_NE(DeviceIoControl(
        DiskHandle,
        IOCTL_STORAGE_QUERY_PROPERTY,
        &storagePropertyQuery,
        sizeof(STORAGE_PROPERTY_QUERY),
        &storageDescriptorHeader,
        sizeof(STORAGE_DESCRIPTOR_HEADER),
        &dwBytesReturned,
        NULL), 0);

    const DWORD dwOutBufferSize = storageDescriptorHeader.Size;
    std::unique_ptr<BYTE> pOutBuffer(new BYTE[dwOutBufferSize]);
    ZeroMemory(pOutBuffer.get(), dwOutBufferSize);

    ASSERT_NE(DeviceIoControl(
        DiskHandle,
        IOCTL_STORAGE_QUERY_PROPERTY,
        &storagePropertyQuery,
        sizeof(STORAGE_PROPERTY_QUERY),
        pOutBuffer.get(),
        dwOutBufferSize,
        &dwBytesReturned,
        NULL), 0);

    STORAGE_DEVICE_DESCRIPTOR* pDeviceDescriptor =
        (STORAGE_DEVICE_DESCRIPTOR*)pOutBuffer.get();
    const DWORD dwSerialNumberOffset = pDeviceDescriptor->SerialNumberOffset;
    char* strSerialNumber;

    if (dwSerialNumberOffset != 0) {
        strSerialNumber = (char*)(pOutBuffer.get() + dwSerialNumberOffset);
    }

    ASSERT_STREQ(strSerialNumber, WnbdDisk->Properties.SerialNumber);
}

TEST(TestDeviceSerial, TestSetDeviceSerial) {
    TestDeviceSerial();
}
