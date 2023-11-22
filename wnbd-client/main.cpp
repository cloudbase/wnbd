/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "client.h"

int main(int argc, const char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    return Client().execute(argc, argv);
}
