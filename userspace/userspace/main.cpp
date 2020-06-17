/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "wmi.h"
#include "ioctl.h"
#include <userspace_shared.h>
#include <string>
#include <codecvt>
#include <locale>

using convert_t = std::codecvt_utf8<wchar_t>;
std::wstring_convert<convert_t, wchar_t> strconverter;

std::string to_string(std::wstring wstr)
{
    return strconverter.to_bytes(wstr);
}

std::wstring to_wstring(std::string str)
{
    return strconverter.from_bytes(str);
}

int main(int argc, PCHAR argv[])
{
    PCHAR Command;
    PCHAR InstanceName = NULL;
    PCHAR PortName = NULL;
    PCHAR ExportName = NULL;
    PCHAR HostName = NULL;

    Command = argv[1];

    if ((argc >= 6) && !strcmp(Command, "map")) {
        InstanceName = argv[2];
        HostName = argv[3];
        PortName = argv[4];
        ExportName = argv[5];
        BOOLEAN MustNegotiate = TRUE;
        if (argc == 7) {
            MustNegotiate = FALSE;
        }
        WnbdMap(InstanceName, HostName, PortName, ExportName, 50000, MustNegotiate);
    } else if (argc == 3 && !strcmp(Command, "unmap")) {
        InstanceName = argv[2];
        WnbdUnmap(InstanceName);
    } else if (argc == 3 && !strcmp(Command, "stats")) {
        InstanceName = argv[2];
        WnbdStats(InstanceName);
    } else if (argc == 2 && !strcmp(Command, "list")) {
        PDISK_INFO_LIST Output = NULL;
        DWORD Status = WnbdList(&Output);
        if (!SUCCEEDED(Status)) {
            return Status;
        }
        if (NULL != Output && ERROR_SUCCESS == Status) {
            InitWMI();
            printf("InstanceName\t\tPid\t\tDiskNumber\t\t\n");
            for (ULONG index = 0; index < Output->ActiveListCount; index++) {
                std::wstring WideString = to_wstring(Output->ActiveEntry[index].ConnectionInformation.SerialNumber);
                std::wstring WQL = L"SELECT * FROM Win32_DiskDrive WHERE SerialNumber = '";
                WQL.append(WideString);
                WQL.append(L"'");
                std::vector<DiskInfo> d;
                bool ret = QueryWMI(bstr_t(WQL.c_str()), d);
                printf("%s\t\t", Output->ActiveEntry[index].ConnectionInformation.InstanceName);
                printf("%d\t\t", Output->ActiveEntry[index].ConnectionInformation.Pid);
                if (d.size() == 1) {
                    DiskInfo temp = d[0];
                    printf("%d\t\t\n", temp.Index);
                } else {
                    printf("\t\t\n");
                }
            }
            ReleaseWMI();
        }
    } else if (argc == 3 && !strcmp(Command, "set-debug")) {
        int Value = atoi(argv[2]);
        WnbdSetDebug(Value);
    } else {
        return Syntax();
    }
    
    return 0;
}