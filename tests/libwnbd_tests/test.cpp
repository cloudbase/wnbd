/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "pch.h"

#include "utils.h"
#include "options.h"

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--help")) {
        PrintHelp();
    }

    ::testing::InitGoogleTest(&argc, argv);

    ParseOptions(argc, argv);

    DWORD LogLevel = GetOpt<DWORD>("log-level");
    WnbdSetLogLevel((WnbdLogLevel) LogLevel);

    if (int Err = InitializeWinsock()) {
        return Err;
    }

    return RUN_ALL_TESTS();
}
