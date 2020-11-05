/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include <locale>
#include <string>
#include <sstream>

#include <windows.h>

#include <boost/locale/encoding_utf.hpp>

using boost::locale::conv::utf_to_utf;


std::wstring to_wstring(const std::string& str)
{
  return utf_to_utf<wchar_t>(str.c_str(), str.c_str() + str.size());
}

std::string to_string(const std::wstring& str)
{
  return utf_to_utf<char>(str.c_str(), str.c_str() + str.size());
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
