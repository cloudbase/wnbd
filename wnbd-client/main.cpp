/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "client.h"

#include <string>

#include <wnbd.h>

int main(int argc, const char** argv)
{
    // This must be called only once. We're going to do it as early as
    // possible to avoid having issues because of uninitialized COM.
    HRESULT hres = WnbdCoInitializeBasic();
    if (FAILED(hres)) {
        return HRESULT_CODE(hres);
    }

    return Client().execute(argc, argv);
}
