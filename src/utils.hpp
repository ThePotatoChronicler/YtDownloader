#pragma once


#include <windows.h>
#include <string>

std::string GetLastErrorAsString(DWORD* = NULL);
std::string format_filesize(DWORD filesize);
std::string convert_utf16_to_utf8(const std::wstring &wstr);
std::wstring convert_utf8_to_utf16(const std::string &str);