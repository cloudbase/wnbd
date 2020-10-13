/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "cmd.h"
#include <string>

bool arg_to_bool(char* arg) {
    return !_stricmp(arg, "1") ||
           !_stricmp(arg, "t") ||
           !_stricmp(arg, "true") ||
           !_stricmp(arg, "yes") ||
           !_stricmp(arg, "y");
}

int main(int argc, PCHAR argv[])
{
    PCHAR Command;
    PCHAR InstanceName = NULL;
    DWORD PortNumber = NULL;
    PCHAR ExportName = NULL;
    PCHAR HostName = NULL;

    BOOLEAN PersistentOpt = FALSE;
    PCHAR OptName = NULL;
    PCHAR OptValue = NULL;

    Command = argv[1];

    // This must be called only once. We're going to do it as early as
    // possible to avoid having issues because of uninitialized COM.
    HRESULT hres = WnbdCoInitializeBasic();
    if (FAILED(hres)) {
        return HRESULT_CODE(hres);
    }

    if ((argc >= 6) && !strcmp(Command, "map")) {
        InstanceName = argv[2];
        HostName = argv[3];
        PortNumber = atoi(argv[4]);
        ExportName = argv[5];
        BOOLEAN SkipNegotiation = FALSE;
        BOOLEAN ReadOnly = FALSE;
        UINT32 DiskSize = 0;
        UINT32 BlockSize = 512;

        // TODO: use named arguments.
        if (argc > 6) {
            SkipNegotiation = arg_to_bool(argv[6]);
        }
        if (argc > 7) {
            ReadOnly = arg_to_bool(argv[7]);
        }
        if (argc > 8) {
            DiskSize = atoi(argv[8]);
        }
        if (argc > 9) {
            BlockSize = atoi(argv[9]);
        }

        CmdMap(InstanceName, HostName, PortNumber, ExportName, DiskSize,
               BlockSize, SkipNegotiation, ReadOnly);
    } else if (argc >= 3 && !strcmp(Command, "unmap")) {
        InstanceName = argv[2];
        BOOLEAN HardRemove = FALSE;
        if (argc > 3) {
            HardRemove = arg_to_bool(argv[3]);
        }
        CmdUnmap(InstanceName, HardRemove);
    } else if (argc == 3 && !strcmp(Command, "stats")) {
        InstanceName = argv[2];
        CmdStats(InstanceName);
    } else if (argc == 2 && !strcmp(Command, "list")) {
        return CmdList();
    } else if (argc == 2 && (
            !strcmp(Command, "version") || !strcmp(Command, "-v"))) {
        return CmdVersion();
    } else if (argc >= 2 && !strcmp(Command, "list-opt")) {
        if (argc > 2) {
            PersistentOpt = arg_to_bool(argv[2]);
        }
        CmdListOpt(PersistentOpt);
    } else if (argc >= 3 && !strcmp(Command, "get-opt")) {
        OptName = argv[2];
        if (argc > 3) {
            PersistentOpt = arg_to_bool(argv[3]);
        }
        CmdGetOpt(OptName, PersistentOpt);
    } else if (argc >= 3 && !strcmp(Command, "reset-opt")) {
        OptName = argv[2];
        if (argc > 3) {
            PersistentOpt = arg_to_bool(argv[3]);
        }
        CmdResetOpt(OptName, PersistentOpt);
    } else if (argc >= 4 && !strcmp(Command, "set-opt")) {
        OptName = argv[2];
        OptValue = argv[3];
        if (argc > 4) {
            PersistentOpt = arg_to_bool(argv[4]);
        }
        CmdSetOpt(OptName, OptValue, PersistentOpt);
    } else {
        PrintSyntax();
        return -1;
    }
    
    return 0;
}