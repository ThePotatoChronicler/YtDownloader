#pragma once

#include <windows.h>
#include <string>
#include <winhttp.h>

std::string GetLastErrorAsString(DWORD* = NULL);
std::string format_filesize(DWORD filesize);
std::string convert_utf16_to_utf8(const std::wstring &wstr);
std::wstring convert_utf8_to_utf16(const std::string &str);

struct UniqueWinHTTPINTERNETDeleter {
    void operator()(HINTERNET handle);
};

using UniqueWinHTTPINTERNET = std::unique_ptr<void, UniqueWinHTTPINTERNETDeleter>;

struct UniqueWinHandleDeleter {
    void operator()(HANDLE handle);
};

using UniqueWinHandle = std::unique_ptr<void, UniqueWinHandleDeleter>;
