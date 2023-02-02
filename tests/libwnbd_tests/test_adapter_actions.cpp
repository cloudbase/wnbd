/*
 * Copyright (C) 2023 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "pch.h"
#include "mock_wnbd_daemon.h"
#include "utils.h"

// The following test covers get/set/reset operations for both
// ephemeral and persistent settings. Normally I'd avoid such a
// long test, however it allows us to transition through various
// scenarios.
TEST(TestDrvOptions, TestGetSetDrvOptInt64) {
    WNBD_OPTION_VALUE OptVal = { WnbdOptUnknown, 0 };

    // 1. Reset the option
    // -------------------
    DWORD Status = WnbdResetDrvOpt("LogLevel", TRUE);
    ASSERT_TRUE(!Status || Status == ERROR_FILE_NOT_FOUND)
        << "couldn't reset opt";

    // retrieve persistent setting
    Status = WnbdGetDrvOpt("LogLevel", &OptVal, TRUE);
    // the persistent setting was cleared out, so we expect it
    // to be unset.
    ASSERT_EQ(ERROR_FILE_NOT_FOUND, Status)
        << "couldn't retrieve persistent opt";

    // retrieve current non-persistent setting
    Status = WnbdGetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";

    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    // the default value
    ASSERT_EQ(OptVal.Data.AsInt64, 1);

    // 2. Set a non-persistent value
    // -----------------------------
    OptVal.Data.AsInt64 = 2;
    Status = WnbdSetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't set opt";

    Status = WnbdGetDrvOpt("LogLevel", &OptVal, TRUE);
    // no persistent setting yet
    ASSERT_EQ(ERROR_FILE_NOT_FOUND, Status);

    Status = WnbdGetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";
    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    ASSERT_EQ(OptVal.Data.AsInt64, 2);

    // 3. Set a persistent value
    // -------------------------
    OptVal.Data.AsInt64 = 3;
    Status = WnbdSetDrvOpt("LogLevel", &OptVal, TRUE);
    ASSERT_FALSE(Status) << "couldn't set opt";

    Status = WnbdGetDrvOpt("LogLevel", &OptVal, TRUE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";
    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    ASSERT_EQ(OptVal.Data.AsInt64, 3);

    Status = WnbdGetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";
    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    ASSERT_EQ(OptVal.Data.AsInt64, 3);

    // 4. Set an ephemeral value, masking the persistent one
    // -----------------------------------------------------
    OptVal.Data.AsInt64 = 4;
    Status = WnbdSetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't set opt";

    Status = WnbdGetDrvOpt("LogLevel", &OptVal, TRUE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";
    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    // We've retrieved the original persistent value
    ASSERT_EQ(OptVal.Data.AsInt64, 3);

    Status = WnbdGetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";
    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    // We're now checking the current ephemeral value
    ASSERT_EQ(OptVal.Data.AsInt64, 4);

    // 5. Reset the ephemeral value
    // ----------------------------
    Status = WnbdResetDrvOpt("LogLevel", FALSE);
    ASSERT_FALSE(Status) << "couldn't reset opt";

    Status = WnbdGetDrvOpt("LogLevel", &OptVal, TRUE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";
    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    // We've retrieved the original persistent value
    ASSERT_EQ(OptVal.Data.AsInt64, 3);

    Status = WnbdGetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";
    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    // We're now checking the current ephemeral value
    ASSERT_EQ(OptVal.Data.AsInt64, 1);

    // 5. Reset the persistent value, back to square one
    // -------------------------------------------------
    Status = WnbdResetDrvOpt("LogLevel", TRUE);
    ASSERT_FALSE(Status) << "couldn't reset opt";

    // retrieve persistent setting
    Status = WnbdGetDrvOpt("LogLevel", &OptVal, TRUE);
    // the persistent setting was cleared out, so we expect it
    // to be unset.
    ASSERT_EQ(ERROR_FILE_NOT_FOUND, Status)
        << "couldn't retrieve persistent opt";

    // retrieve current non-persistent setting
    Status = WnbdGetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't retrieve opt";

    ASSERT_EQ(OptVal.Type, WnbdOptInt64);
    // the default value
    ASSERT_EQ(OptVal.Data.AsInt64, 1);
}

TEST(TestDrvOptions, TestListOptions) {
    // 1. Reset the option
    // -------------------
    DWORD Status = WnbdResetDrvOpt("LogLevel", TRUE);
    ASSERT_TRUE(!Status || Status == ERROR_FILE_NOT_FOUND)
        << "couldn't reset opt";

    Status = WnbdResetDrvOpt("DbgPrintEnabled", TRUE);
    ASSERT_TRUE(!Status || Status == ERROR_FILE_NOT_FOUND)
        << "couldn't reset opt";

    Status = WnbdResetDrvOpt("EtwLoggingEnabled", TRUE);
    ASSERT_TRUE(!Status || Status == ERROR_FILE_NOT_FOUND)
        << "couldn't reset opt";

    // 2. Modify some of the options
    // -----------------------------
    WNBD_OPTION_VALUE OptVal = { WnbdOptUnknown, 0 };

    OptVal.Type = WnbdOptInt64;
    OptVal.Data.AsInt64 = 3;
    Status = WnbdSetDrvOpt("LogLevel", &OptVal, TRUE);
    ASSERT_FALSE(Status) << "couldn't set opt";

    OptVal.Type = WnbdOptInt64;
    OptVal.Data.AsInt64 = 2;
    Status = WnbdSetDrvOpt("LogLevel", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't set opt";

    OptVal.Type = WnbdOptBool;
    OptVal.Data.AsBool = FALSE;
    Status = WnbdSetDrvOpt("DbgPrintEnabled", &OptVal, FALSE);
    ASSERT_FALSE(Status) << "couldn't set opt";

    OptVal.Type = WnbdOptBool;
    OptVal.Data.AsBool = TRUE;
    Status = WnbdSetDrvOpt("EtwLoggingEnabled", &OptVal, TRUE);
    ASSERT_FALSE(Status) << "couldn't set opt";

    // 3. Check current options
    // ------------------------
    WnbdOptionList OptList = WnbdOptionList();
    Status = OptList.Retrieve(FALSE);
    ASSERT_FALSE(Status) << "couldn't list opt";

    PWNBD_OPTION Option = OptList.GetOpt(L"LogLevel");
    ASSERT_TRUE(Option) << "couldn't retrieve opt";
    ASSERT_EQ(Option->Type, WnbdOptInt64);
    ASSERT_EQ(Option->Default.Type, WnbdOptInt64);
    ASSERT_EQ(Option->Default.Data.AsInt64, 1);
    ASSERT_EQ(Option->Value.Type, WnbdOptInt64);
    ASSERT_EQ(Option->Value.Data.AsInt64, 2);

    Option = OptList.GetOpt(L"DbgPrintEnabled");
    ASSERT_TRUE(Option) << "couldn't retrieve opt";
    ASSERT_EQ(Option->Type, WnbdOptBool);
    ASSERT_EQ(Option->Default.Type, WnbdOptBool);
    ASSERT_TRUE(Option->Default.Data.AsBool);
    ASSERT_EQ(Option->Value.Type, WnbdOptBool);
    ASSERT_FALSE(Option->Value.Data.AsBool);

    Option = OptList.GetOpt(L"EtwLoggingEnabled");
    ASSERT_TRUE(Option) << "couldn't retrieve opt";
    ASSERT_EQ(Option->Type, WnbdOptBool);
    ASSERT_EQ(Option->Default.Type, WnbdOptBool);
    ASSERT_TRUE(Option->Default.Data.AsBool);
    ASSERT_EQ(Option->Value.Type, WnbdOptBool);
    ASSERT_TRUE(Option->Value.Data.AsBool);

    // 4. Check persistent options
    // ---------------------------
    Status = OptList.Retrieve(TRUE);
    ASSERT_FALSE(Status) << "couldn't list opt";

    Option = OptList.GetOpt(L"LogLevel");
    ASSERT_TRUE(Option) << "couldn't retrieve opt";
    ASSERT_EQ(Option->Type, WnbdOptInt64);
    ASSERT_EQ(Option->Default.Type, WnbdOptInt64);
    ASSERT_EQ(Option->Default.Data.AsInt64, 1);
    ASSERT_EQ(Option->Value.Type, WnbdOptInt64);
    ASSERT_EQ(Option->Value.Data.AsInt64, 3);

    Option = OptList.GetOpt(L"EtwLoggingEnabled");
    ASSERT_TRUE(Option) << "couldn't retrieve opt";
    ASSERT_EQ(Option->Type, WnbdOptBool);
    ASSERT_EQ(Option->Default.Type, WnbdOptBool);
    ASSERT_TRUE(Option->Default.Data.AsBool);
    ASSERT_EQ(Option->Value.Type, WnbdOptBool);
    ASSERT_TRUE(Option->Value.Data.AsBool);

    Option = OptList.GetOpt(L"DbgPrintEnabled");
    ASSERT_FALSE(Option)
        << "the persistent list contains an option that isn't persistent";

    // 5. Reset modified options
    // -------------------------
    Status = WnbdResetDrvOpt("LogLevel", TRUE);
    ASSERT_TRUE(!Status || Status == ERROR_FILE_NOT_FOUND)
        << "couldn't reset opt";

    Status = WnbdResetDrvOpt("DbgPrintEnabled", TRUE);
    ASSERT_TRUE(!Status || Status == ERROR_FILE_NOT_FOUND)
        << "couldn't reset opt";

    Status = WnbdResetDrvOpt("EtwLoggingEnabled", TRUE);
    ASSERT_TRUE(!Status || Status == ERROR_FILE_NOT_FOUND)
        << "couldn't reset opt";
}
