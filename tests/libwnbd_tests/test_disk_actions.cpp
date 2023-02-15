#include "pch.h"
#include "mock_wnbd_daemon.h"
#include "utils.h"

void TestMap(
    uint64_t BlockCount = DefaultBlockCount,
    uint32_t BlockSize = DefaultBlockSize,
    bool ReadOnly = false,
    bool CacheEnabled = true)
{
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);

    WnbdProps.BlockCount = BlockCount;
    WnbdProps.BlockSize = BlockSize;

    WnbdProps.Flags.ReadOnly = ReadOnly;

    if (CacheEnabled) {
        WnbdProps.Flags.FUASupported = 1;
        WnbdProps.Flags.FlushSupported = 1;
    }

    MockWnbdDaemon WnbdDaemon(&WnbdProps);
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(WnbdProps.InstanceName, &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    EXPECT_EQ(
        WnbdProps.InstanceName,
        std::string(ConnectionInfo.Properties.InstanceName));
    EXPECT_EQ(
        WnbdProps.InstanceName,
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
    GetNewWnbdProps(&WnbdProps);
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

void TestLiveResize(
    uint64_t BlockCount = DefaultBlockCount,
    uint64_t NewBlockCount = DefaultBlockCount * 2)
{
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);
    WnbdProps.BlockCount = BlockCount;

    MockWnbdDaemon WnbdDaemon(&WnbdProps);
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(WnbdProps.InstanceName, &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    ASSERT_EQ(BlockCount, WnbdDisk->Properties.BlockCount);
    ASSERT_EQ(WnbdSetDiskSize(WnbdDisk, NewBlockCount), ERROR_SUCCESS);
    ASSERT_EQ(NewBlockCount, WnbdDisk->Properties.BlockCount);

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);
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
    TestLiveResize(DefaultBlockCount, DefaultBlockCount / 2);
}

TEST(TestNaaIdentifier, TestSetNaaId) {
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);
    WnbdProps.Flags.NaaIdSpecified = 1;
    WnbdProps.NaaIdentifier.data[0] = 0x60;
    for (int i = 1; i < sizeof(WnbdProps.NaaIdentifier.data); i++)
        WnbdProps.NaaIdentifier.data[i] = (BYTE)rand();

    MockWnbdDaemon WnbdDaemon(&WnbdProps);
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(WnbdProps.InstanceName, &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    ASSERT_EQ(WnbdDisk->Properties.Flags.NaaIdSpecified, 1);

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        NULL,
        NULL);

    ASSERT_NE(DiskHandle, INVALID_HANDLE_VALUE)
        << "couldn't open wnbd disk: " << WnbdProps.InstanceName
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

TEST(TestDeviceSerial, TestSetDeviceSerial) {
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);
    std::string Serial = std::to_string(rand()) + "-" +
        std::to_string(rand());
    Serial.copy(WnbdProps.SerialNumber, sizeof(WnbdProps.SerialNumber));

    MockWnbdDaemon WnbdDaemon(&WnbdProps);
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(WnbdProps.InstanceName, &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        NULL,
        NULL);

    ASSERT_NE(DiskHandle, INVALID_HANDLE_VALUE)
        << "couldn't open wnbd disk: " << WnbdProps.InstanceName
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

void TestUnmap(
    bool FailureExpected = false,
    int SoftRemoveTimeoutMs = WNBD_DEFAULT_RM_TIMEOUT_MS,
    int SoftRemoveRetryIntervalMs = WNBD_DEFAULT_RM_RETRY_INTERVAL_MS,
    bool HardRemove = false,
    bool HardRemoveFallback = true
)
{
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);
    MockWnbdDaemon WnbdDaemon(&WnbdProps);

    WnbdDaemon.Start();
    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    ASSERT_FALSE(WnbdShow(WnbdProps.InstanceName, &ConnectionInfo))
        << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    WNBD_REMOVE_OPTIONS RemoveOptions = { 0 };
    RemoveOptions.Flags.HardRemove = HardRemove;
    RemoveOptions.Flags.HardRemoveFallback = HardRemoveFallback;
    RemoveOptions.SoftRemoveTimeoutMs = SoftRemoveTimeoutMs;
    RemoveOptions.SoftRemoveRetryIntervalMs = SoftRemoveRetryIntervalMs;

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);

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

    WnbdDaemon.TerminatingInProgress();

    if (FailureExpected) {
        ASSERT_NE(WnbdRemove(WnbdDisk, &RemoveOptions), 0);
        CloseHandle(DiskHandle);
    } else {
        if (!HardRemove && !HardRemoveFallback) {
            CloseHandle(DiskHandle);
        }

        ASSERT_EQ(WnbdRemove(WnbdDisk, &RemoveOptions), 0);

        if (HardRemove || HardRemoveFallback) {
            CloseHandle(DiskHandle);
        }
    }
}

TEST(TestUnmap, TestSoftUnmap) {
    TestUnmap(false, 0, 0, false, false);
}

TEST(TestUnmap, TestSoftUnmapTimeout) {
    // Expect to fail due to time-out
    TestUnmap(true, 1, 1, false, false);
}

TEST(TestUnmap, TestHardUnmapFallback) {
    // Make soft unmap time-out and fallback to hard unmap
    TestUnmap(false, 1, 1);
}

TEST(TestUnmap, TestHardUnmap) {
    TestUnmap(false, 0, 0, true);
}
