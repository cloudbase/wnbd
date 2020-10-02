/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <codecvt>
#include <locale>
#include <string>
#include <sstream>

#include <windows.h>

std::wstring to_wstring(const char* str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> strconverter;
    return strconverter.from_bytes(str);
}

std::string to_string(std::wstring wstr)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> strconverter;
    return strconverter.to_bytes(wstr);
}

std::string win32_strerror(int err)
{
    LPSTR msg = NULL;
    DWORD msg_len = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        0,
        (LPSTR) &msg,
        0,
        NULL);
    if (!msg_len) {
        std::ostringstream msg_stream;
        msg_stream << "Unknown error (" << err << ").";
        return msg_stream.str();
    }
    std::string msg_s(msg);
    ::LocalFree(msg);
    return msg_s;
}
