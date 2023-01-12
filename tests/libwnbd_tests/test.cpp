/*
 * Copyright (C) 2022 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "pch.h"

#include "utils.h"

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
