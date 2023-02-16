#include "pch.h"
#include "mock_wnbd_daemon.h"
#include "utils.h"

#include <ntddscsi.h>

void TestWrite(
    uint64_t BlockCount = DefaultBlockCount,
    uint32_t BlockSize = DefaultBlockSize,
    bool CacheEnabled = true,
    bool UseFUA = false)
{
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);

    WnbdProps.BlockCount = BlockCount;
    WnbdProps.BlockSize = BlockSize;

    if (CacheEnabled) {
        WnbdProps.Flags.FUASupported = 1;
        WnbdProps.Flags.FlushSupported = 1;
    }

    MockWnbdDaemon WnbdDaemon(&WnbdProps);

    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(WnbdProps.InstanceName, &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);
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
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);

    WnbdProps.Flags.ReadOnly = 1;

    MockWnbdDaemon WnbdDaemon(&WnbdProps);
    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(WnbdProps.InstanceName, &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

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
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);

    WnbdProps.BlockCount = BlockCount;
    WnbdProps.BlockSize = BlockSize;

    if (CacheEnabled) {
        WnbdProps.Flags.FUASupported = 1;
        WnbdProps.Flags.FlushSupported = 1;
    }

    MockWnbdDaemon WnbdDaemon(&WnbdProps);

    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(WnbdProps.InstanceName, &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

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
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));
    ASSERT_FALSE(
        memcmp(ReadBuffer.get(), ExpReadBuffer.get(),
            WNBD_DEFAULT_MAX_TRANSFER_LENGTH));

    // Second fragment
    ExpWnbdRequest.Cmd.Read.BlockAddress +=
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH / BlockSize;
    ExpWnbdRequest.Cmd.Read.BlockCount =
        WNBD_DEFAULT_MAX_TRANSFER_LENGTH / BlockSize;
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));
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
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));
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
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);

    MockWnbdDaemon WnbdDaemon(&WnbdProps);

    WnbdDaemon.Start();

    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    NTSTATUS Status = WnbdShow(WnbdProps.InstanceName, &ConnectionInfo);
    ASSERT_FALSE(Status) << "couldn't retrieve WNBD disk info";

    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);

    WNBD_USR_STATS UserspaceStats = { 0 };
    WNBD_DRV_STATS DriverStats = { 0 };

    WnbdGetUserspaceStats(WnbdDisk, &UserspaceStats);
    WnbdGetDriverStats(WnbdProps.InstanceName, &DriverStats);

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
    WnbdGetDriverStats(WnbdProps.InstanceName, &DriverStats);
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
    WnbdGetDriverStats(WnbdProps.InstanceName, &DriverStats);
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

TEST(TestPersistentReservations, TestPersistentReserveIn) {
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);
    WnbdProps.Flags.PersistResSupported = 1;

    MockWnbdDaemon WnbdDaemon(&WnbdProps);

    WnbdDaemon.Start();
    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    ASSERT_FALSE(WnbdShow(WnbdProps.InstanceName, &ConnectionInfo))
        << "couldn't retrieve WNBD disk info";

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);
    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    UCHAR Data[24];
    ZeroMemory(Data, sizeof(Data));

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    ASSERT_NE(DiskHandle, INVALID_HANDLE_VALUE)
        << "Error opening device: " << GetLastError();

    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    SCSI_PASS_THROUGH_DIRECT Sptd;
    ZeroMemory(&Sptd, sizeof(Sptd));
    Sptd.Length = sizeof(Sptd);
    Sptd.CdbLength = 6;
    Sptd.DataIn = SCSI_IOCTL_DATA_IN;
    Sptd.DataBuffer = Data;
    Sptd.DataTransferLength = sizeof(Data);
    Sptd.TimeOutValue = 2;
    Sptd.Cdb[0] = SCSIOP_PERSISTENT_RESERVE_IN;
    Sptd.Cdb[1] = RESERVATION_ACTION_READ_RESERVATIONS;

    DWORD BytesReturned = 0;
    BOOL Result = DeviceIoControl(
        DiskHandle,
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &Sptd,
        sizeof(Sptd),
        &Sptd,
        sizeof(Sptd),
        &BytesReturned,
        NULL);

    EXPECT_NE(Result, 0) << "Error sending command: " << GetLastError();
    ASSERT_EQ(Sptd.ScsiStatus, 0) << "Command returned with error status: "
                                  << std::hex << Sptd.ScsiStatus;
    EXPECT_EQ(*(PUINT32)&Data[0], MOCK_PR_GENERATION);
    EXPECT_EQ(BytesReturned, sizeof(SCSI_PASS_THROUGH_DIRECT));

    WNBD_IO_REQUEST ExpWnbdRequest = { 0 };
    ExpWnbdRequest.RequestType = WnbdReqTypePersistResIn;
    ExpWnbdRequest.Cmd.PersistResIn.ServiceAction = RESERVATION_ACTION_READ_RESERVATIONS;
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(ExpWnbdRequest));

    WNBD_USR_STATS UserspaceStats = { 0 };
    WnbdGetUserspaceStats(WnbdDisk, &UserspaceStats);
    ASSERT_EQ(UserspaceStats.PersistResInErrors, 0);
}

TEST(TestPersistentReservations, TestPersistentReserveOut) {
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);
    WnbdProps.Flags.PersistResSupported = 1;

    MockWnbdDaemon WnbdDaemon(&WnbdProps);
    WnbdDaemon.Start();
    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    ASSERT_FALSE(WnbdShow(WnbdProps.InstanceName, &ConnectionInfo))
        << "couldn't retrieve WNBD disk info";

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);
    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    PRO_PARAMETER_LIST OutParamList = { 0 };
    *(PUINT64)&OutParamList.ReservationKey = 0x1111beef;

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    ASSERT_NE(DiskHandle, INVALID_HANDLE_VALUE)
        << "Error opening device: " << GetLastError();

    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    SCSI_PASS_THROUGH_DIRECT Sptd;
    ZeroMemory(&Sptd, sizeof(Sptd));
    Sptd.Length = sizeof(Sptd);
    Sptd.CdbLength = 10;
    Sptd.DataIn = SCSI_IOCTL_DATA_OUT;
    Sptd.DataBuffer = &OutParamList;
    Sptd.DataTransferLength = sizeof(OutParamList);
    Sptd.TimeOutValue = 2;
    Sptd.Cdb[0] = SCSIOP_PERSISTENT_RESERVE_OUT;
    Sptd.Cdb[1] = RESERVATION_ACTION_CLEAR;
    Sptd.Cdb[2] = RESERVATION_TYPE_EXCLUSIVE;
    Sptd.Cdb[8] = sizeof(PRO_PARAMETER_LIST);

    DWORD BytesReturned = 0;
    BOOL Result = DeviceIoControl(
        DiskHandle,
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &Sptd,
        sizeof(Sptd),
        &Sptd,
        sizeof(Sptd),
        &BytesReturned,
        NULL);

    EXPECT_NE(Result, 0) << "Error sending command: " << GetLastError();
    ASSERT_EQ(Sptd.ScsiStatus, 0) << "Command returned with error status: "
                                  << std::hex << Sptd.ScsiStatus;

    WNBD_IO_REQUEST ExpWnbdRequest = { 0 };
    ExpWnbdRequest.RequestType = WnbdReqTypePersistResOut;
    ExpWnbdRequest.Cmd.PersistResOut.ServiceAction = RESERVATION_ACTION_CLEAR;
    ExpWnbdRequest.Cmd.PersistResOut.Type = RESERVATION_TYPE_EXCLUSIVE;
    ExpWnbdRequest.Cmd.PersistResOut.ParameterListLength = sizeof(OutParamList);
    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(
        ExpWnbdRequest, &OutParamList, sizeof(OutParamList)));

    *(PUINT64)&OutParamList.ReservationKey = 0x33331111;
    // Ensure that the buffer check works as expected by using a negative test.
    ASSERT_FALSE(WnbdDaemon.ReqLog.HasEntry(
        ExpWnbdRequest, &OutParamList, sizeof(OutParamList)));

    WNBD_USR_STATS UserspaceStats = { 0 };
    WnbdGetUserspaceStats(WnbdDisk, &UserspaceStats);
    ASSERT_EQ(UserspaceStats.PersistResOutErrors, 0);
}

TEST(TestScsiUnmap, TestUnmap) {
    WNBD_PROPERTIES WnbdProps = { 0 };
    GetNewWnbdProps(&WnbdProps);
    MockWnbdDaemon WnbdDaemon(&WnbdProps);
    WnbdDaemon.Start();
    WNBD_CONNECTION_INFO ConnectionInfo = { 0 };
    ASSERT_FALSE(WnbdShow(WnbdProps.InstanceName, &ConnectionInfo))
        << "couldn't retrieve WNBD disk info";

    std::string DiskPath = GetDiskPath(WnbdProps.InstanceName);
    PWNBD_DISK WnbdDisk = WnbdDaemon.GetDisk();

    HANDLE DiskHandle = CreateFileA(
        DiskPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    ASSERT_NE(DiskHandle, INVALID_HANDLE_VALUE)
        << "Error opening device: " << GetLastError();

    std::unique_ptr<void, decltype(&CloseHandle)> DiskHandleCloser(
        DiskHandle, &CloseHandle);

    const int AllocationLength = sizeof(UNMAP_LIST_HEADER) + sizeof(UNMAP_BLOCK_DESCRIPTOR);
    std::unique_ptr<UNMAP_LIST_HEADER> UnmapListHeader(
        (PUNMAP_LIST_HEADER) new BYTE[AllocationLength]);

    ZeroMemory(UnmapListHeader.get(), AllocationLength);

    CDB UnmapCdb = { 0 };
    UnmapCdb.UNMAP.OperationCode = SCSIOP_UNMAP;
    REVERSE_BYTES_2(&UnmapCdb.UNMAP.AllocationLength, &AllocationLength);
    int LbaCount = 1;
    REVERSE_BYTES_4(UnmapListHeader->Descriptors[0].LbaCount, &LbaCount);
    int64_t StartingLba = 2;
    REVERSE_BYTES_8(UnmapListHeader->Descriptors[0].StartingLba, &StartingLba);
    int BlockDescrDataLength = sizeof(UNMAP_BLOCK_DESCRIPTOR);
    REVERSE_BYTES_2(&UnmapListHeader->BlockDescrDataLength, &BlockDescrDataLength);

    SCSI_PASS_THROUGH_DIRECT Sptd;
    ZeroMemory(&Sptd, sizeof(Sptd));
    Sptd.Length = sizeof(Sptd);
    Sptd.CdbLength = 10;
    Sptd.DataIn = SCSI_IOCTL_DATA_OUT;
    Sptd.DataBuffer = UnmapListHeader.get();
    Sptd.DataTransferLength = sizeof(UNMAP_LIST_HEADER) + sizeof(UNMAP_BLOCK_DESCRIPTOR);
    Sptd.TimeOutValue = 2;
    RtlCopyMemory(&Sptd.Cdb, &UnmapCdb, sizeof(CDB));

    DWORD BytesReturned = 0;
    BOOL Result = DeviceIoControl(
        DiskHandle,
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &Sptd,
        sizeof(Sptd),
        &Sptd,
        sizeof(Sptd),
        &BytesReturned,
        NULL);

    EXPECT_NE(Result, 0) << "Error sending command: " << GetLastError();
    ASSERT_EQ(Sptd.ScsiStatus, 0) << "Command returned with error status: "
        << std::hex << Sptd.ScsiStatus;

    WNBD_IO_REQUEST ExpWnbdRequest = { 0 };
    ExpWnbdRequest.RequestType = WnbdReqTypeUnmap;
    ExpWnbdRequest.Cmd.Unmap.Count = 1;

    WNBD_UNMAP_DESCRIPTOR ExpectedUnmapDescriptor = { 0 };
    ZeroMemory(&ExpectedUnmapDescriptor, sizeof(WNBD_UNMAP_DESCRIPTOR));
    ExpectedUnmapDescriptor.BlockCount = 1;
    ExpectedUnmapDescriptor.BlockAddress = 2;

    ASSERT_TRUE(WnbdDaemon.ReqLog.HasEntry(
        ExpWnbdRequest,
        (void*) &ExpectedUnmapDescriptor,
        sizeof(WNBD_UNMAP_DESCRIPTOR)));
}
