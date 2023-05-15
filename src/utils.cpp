#include <string>
#include <windows.h>
#include <shlwapi.h>
#include "utils.hpp"
#include <locale>
#include <codecvt>

// https://stackoverflow.com/a/17387176
//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
std::string GetLastErrorAsString(DWORD *errorptr)
{
    //Get the error message ID, if any.
    DWORD errorMessageID;
    if (errorptr) {
        errorMessageID = *errorptr;
    } else {
        errorMessageID = ::GetLastError();
    }

    if(errorMessageID == 0) {
        return std::string(); //No error message has been recorded
    }

    LPSTR messageBuffer = nullptr;

    //Ask Win32 to give us the string version of that message ID.
    //The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, LANG_USER_DEFAULT, (LPSTR)&messageBuffer, 0, NULL);

    //Copy the error message into a std::string.
    std::string message(messageBuffer, size);

    //Free the Win32's string's buffer.
    LocalFree(messageBuffer);

    return message;
}

std::string format_filesize(DWORD filesize) {
    std::wstring buf;
    buf.resize(64);
    StrFormatByteSizeW(filesize, buf.data(), 64);
    buf.resize(wcslen(buf.c_str()));
    return convert_utf16_to_utf8(buf);
}

std::string convert_utf16_to_utf8(const std::wstring &wstr) {
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}.to_bytes(wstr);
}

std::wstring convert_utf8_to_utf16(const std::string &str) {
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}.from_bytes(str);
}