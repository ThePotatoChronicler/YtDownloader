#include <string>
#include <regex>
#include "download.hpp"
#include "utils.hpp"
#include <nlohmann/json.hpp>
#include <winhttp.h>
#include <fstream>
#include "global.hpp"

using json = nlohmann::json;

void downloadVideoFromUrl(DownloadTask *task) {
    std::string videoformat;
    if (task->audio_only) {
        videoformat = "bestaudio";
    } else {
        videoformat = "best";
    }
    std::string args = "--no-playlist --format " + videoformat + " --no-warnings -J \"";
    args += task->url;
    args += "\"";

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(STARTUPINFOW));
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    SECURITY_ATTRIBUTES sattr;
    sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sattr.bInheritHandle = TRUE;
    sattr.lpSecurityDescriptor = NULL;

    HANDLE readPipe;
    HANDLE writePipe;

    BOOL created_pipe = ::CreatePipe(
            &readPipe,
            &writePipe,
            &sattr,
            std::powl(2, 16)
            );

    if (!created_pipe) {
        task->failure_what = "Nepodařilo se spustit ytdlp.exe (tvorba výstupní trubky): " + GetLastErrorAsString();
        task->state = DownloadState::InicializationFailure;
        return;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    si.cb = sizeof(STARTUPINFO);
    si.hStdOutput = writePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    BOOL created_process = ::CreateProcessW(
            ytdlp_path.c_str(),
            convert_utf8_to_utf16(args).data(),
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            execdir_path.c_str(),
            &si,
            &pi
            );

    if (!created_process) {
        task->failure_what = "Nepodařilo se spustit ytdlp.exe: " + GetLastErrorAsString();
        task->state = DownloadState::InicializationFailure;
        return;
    }

    CloseHandle(writePipe);

    DWORD bytes_read = 0;
    std::string stdout_text;
    std::vector<char> pipebuffer(1024);

    BOOL read_success;

    for (;;) {
        read_success = ReadFile(readPipe, pipebuffer.data(), pipebuffer.size(), &bytes_read, NULL);
        if (!read_success) {
            DWORD error = ::GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_INVALID_HANDLE) {
                SetLastError(0);
                break;
            } else {
                CloseHandle(readPipe);
                CloseHandle( pi.hProcess );
                CloseHandle( pi.hThread );
                task->failure_what = "Nepodařilo se komunikovat s ytdlp.exe (výstup): " + GetLastErrorAsString();
                task->state = DownloadState::InicializationFailure;
                return;
            }
        }
        if (bytes_read == 0) break;

        stdout_text += std::string_view(pipebuffer.data(), bytes_read);
    }

    // Close pipes
    CloseHandle(readPipe);

    // Wait until child process exits.
    WaitForSingleObject( pi.hProcess, INFINITE );

    DWORD ytdlp_exit_code;

    if (!GetExitCodeProcess(pi.hProcess, &ytdlp_exit_code)) {
        task->failure_what = "Nepodařilo se komunikovat s ytdlp.exe (exit kód): " + GetLastErrorAsString();
        task->state = DownloadState::InicializationFailure;
        return;
    };

    // Close process and thread handles. 
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );

    if (ytdlp_exit_code != 0) {
        task->state = DownloadState::InicializationInvalid;
        return;
    }

    json ytdlp_data;

    try {
        ytdlp_data = json::parse(stdout_text);
    } catch (json::parse_error error) {
        task->failure_what = "Nepodařilo se přečíst data o videu: " + std::string(error.what());
        task->state = DownloadState::InicializationFailure;
        return;
    }

    task->title = ytdlp_data["title"];
    task->state = DownloadState::InicializationSuccess;

    if (ytdlp_data["requested_downloads"].size() == 0) {
        task->failure_what = "Není dostupné žádné vhodné stažení";
        task->state = DownloadState::NoDownloadFound;
        return;
    };

    auto download = ytdlp_data["requested_downloads"][0];
    std::string download_url = download["url"];
    if (download.contains("filesize")) {
        task->filesize = download["filesize"];
    } else if (download.contains("filesize_approx")) {
        task->filesize = download["filesize_approx"];
    }

    task->connection_desc = "Připojování k serveru";
    task->state = DownloadState::Connecting;

    std::smatch matches;
    std::regex_match(download_url, matches, uri_regex);

    std::wstring host = convert_utf8_to_utf16(matches[4]);

    HINTERNET connection = WinHttpConnect(internet, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!connection) {
        task->failure_what = "Chyba při připojování: " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = "Otevírání požadavku";

    std::string file = matches[5];
    std::string queryargs = matches[6];
    std::wstring querystring = convert_utf8_to_utf16(file + queryargs);

    HINTERNET request = WinHttpOpenRequest(
            connection,
            L"GET",
            querystring.c_str(),
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
            );

    if (!request) {
        WinHttpCloseHandle(connection);
        task->failure_what = "Chyba při připojování: " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = "Přidávání hlaviček";

    for (const auto &[hk, hv] : ytdlp_data["http_headers"].items()) {
        std::string hval = hv; 
        std::wstring header = convert_utf8_to_utf16(hk + ": " + hval);
        BOOL success = WinHttpAddRequestHeaders(request, header.c_str(), header.length(), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (!success) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            task->failure_what = "Chyba při připojování (přidávání hlaviček): " + GetLastErrorAsString();
            task->state = DownloadState::ConnectionFailure;
            return;
        }
    }

    task->connection_desc = "Posílání požadavku";

    BOOL sent_request_success = WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0L,
            WINHTTP_NO_REQUEST_DATA,
            0L,
            0L,
            NULL);

    if (!sent_request_success) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        task->failure_what = "Chyba při připojování (posílání požadavku): " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = "Přijímání odpovědi";

    BOOL received_success = WinHttpReceiveResponse(request, NULL);

    if (!received_success) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        task->failure_what = "Chyba při připojování (přijímání odpovědi): " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = "Zjišťování status kódu";

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    BOOL got_status_code = WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_code_size,
            WINHTTP_NO_HEADER_INDEX
            );

    if (!got_status_code) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        task->failure_what = "Chyba při připojování (zjišťování status kódu): " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = "Kontrolování status kódu";

    if (status_code != 200) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        if (status_code == 429 || status_code == 402) {
            task->failure_what = "Youtube dočasně blokuje stahování, zkuste to později";
        } else {
            task->failure_what = "Chyba při připojování (špatný status kód " + std::to_string(status_code) + "): " + GetLastErrorAsString();
        }
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = "Začínání přijímání videa";

    task->state = DownloadState::Downloading;

    DWORD data_available = 0;
    DWORD data_downloaded = 0;
    std::vector<char> videodata;
    std::vector<char> downloadbuffer;
    downloadbuffer.reserve(4096);

    do {
        if (task->suggested_stop) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            return;
        }
        data_available = 0;
        data_downloaded = 0;

        BOOL query_successful = WinHttpQueryDataAvailable(request, &data_available);
        if (!query_successful)
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            task->failure_what = "Chyba při stahování (čtení velikosti bloku): " + GetLastErrorAsString();
            task->state = DownloadState::DownloadFailure;
            return;
        }

        downloadbuffer.resize(data_available);

        BOOL read_result = WinHttpReadData(request, downloadbuffer.data(), data_available, &data_downloaded);
        if (!read_result)
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            task->failure_what = "Chyba při stahování (čtení bloku): " + GetLastErrorAsString();
            task->state = DownloadState::DownloadFailure;
            return;
        }

        downloadbuffer.resize(data_downloaded);

        videodata.insert(std::end(videodata), std::begin(downloadbuffer), std::end(downloadbuffer));
        task->bytesdownloaded = videodata.size();
    } while (data_available > 0);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);

    task->state = DownloadState::Saving;

    std::string savepath = config["uloziste"];
    std::string videofile_name = download["_filename"];
    std::string videofile_path = savepath + "\\" + videofile_name;
    std::ofstream videofile(convert_utf8_to_utf16(videofile_path), std::ios::trunc | std::ios::binary);

    if (!videofile) {
        task->failure_what = "Chyba při ukládání: nelze otevřít výstupní soubor";
        task->state = DownloadState::SaveFailure;
        return;
    }

    videofile.write(videodata.data(), videodata.size());
    videofile.flush();
    videofile.close();

    task->state = DownloadState::Completed;
}
