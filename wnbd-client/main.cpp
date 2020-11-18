/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "client.h"

int main(int argc, const char** argv)
{
    return Client().execute(argc, argv);
}
