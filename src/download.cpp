#include <string>
#include <regex>
#include "download.hpp"
#include "utils.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <winhttp.h>
#include <fstream>
#include "global.hpp"
#include "i18n.h"

using json = nlohmann::json;

// FIXME: Error text language won't change when the language changes

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

    HANDLE raw_readPipe;
    HANDLE raw_writePipe;

    BOOL created_pipe = ::CreatePipe(
            &raw_readPipe,
            &raw_writePipe,
            &sattr,
            std::powl(2, 16)
            );

    if (!created_pipe) {
        task->failure_what = std::string(get_phrase("download_error_fail_pipe")) + ": " + GetLastErrorAsString();
        task->state = DownloadState::InicializationFailure;
        return;
    }

    UniqueWinHandle readPipe(raw_readPipe);
    UniqueWinHandle writePipe(raw_writePipe);

    SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0);

    si.cb = sizeof(STARTUPINFO);
    si.hStdOutput = writePipe.get();
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
        task->failure_what = std::string(get_phrase("download_error_launch_ytdlp")) + ": " + GetLastErrorAsString();
        task->state = DownloadState::InicializationFailure;
        return;
    }

    CloseHandle(writePipe.release());
    CloseHandle(pi.hThread);

    UniqueWinHandle ytdlp_process(pi.hProcess);

    DWORD bytes_read = 0;
    std::string stdout_text;
    std::vector<char> pipebuffer(1024);

    BOOL read_success;

    for (;;) {
        read_success = ReadFile(readPipe.get(), pipebuffer.data(), pipebuffer.size(), &bytes_read, NULL);
        if (!read_success) {
            DWORD error = ::GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_INVALID_HANDLE) {
                SetLastError(0);
                break;
            } else {
                task->failure_what = std::string(get_phrase("download_error_communicate_ytdlp_output")) + ": " + GetLastErrorAsString();
                task->state = DownloadState::InicializationFailure;
                return;
            }
        }
        if (bytes_read == 0) break;

        stdout_text += std::string_view(pipebuffer.data(), bytes_read);
    }

    // Wait until child process exits.
    WaitForSingleObject( ytdlp_process.get(), INFINITE );

    DWORD ytdlp_exit_code;

    if (!GetExitCodeProcess(ytdlp_process.get(), &ytdlp_exit_code)) {
        task->failure_what = std::string(get_phrase("download_error_communicate_ytdlp_exitcode")) + ": " + GetLastErrorAsString();
        task->state = DownloadState::InicializationFailure;
        return;
    };

    if (ytdlp_exit_code != 0) {
        task->state = DownloadState::InicializationInvalid;
        return;
    }

    CloseHandle(ytdlp_process.release());

    json ytdlp_data;

    try {
        ytdlp_data = json::parse(stdout_text);
    } catch (json::parse_error error) {
        task->failure_what = std::string(get_phrase("download_error_video_data_parse")) + ": " + std::string(error.what());
        task->state = DownloadState::InicializationFailure;
        return;
    }

    task->title = ytdlp_data["title"];
    task->state = DownloadState::InicializationSuccess;

    if (ytdlp_data["requested_downloads"].size() == 0) {
        task->failure_what = get_phrase("download_error_no_requested_download");
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

    task->connection_desc = get_phrase("connecting_to_server");
    task->state = DownloadState::Connecting;

    std::smatch matches;
    std::regex_match(download_url, matches, uri_regex);

    std::wstring host = convert_utf8_to_utf16(matches[4]);

    HINTERNET raw_connection = WinHttpConnect(internet.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!raw_connection) {
        task->failure_what = std::string(get_phrase("download_error_connection_failure_connection")) + ": " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    UniqueWinHTTPINTERNET connection(raw_connection);

    task->connection_desc = get_phrase("opening_request");

    std::string file = matches[5];
    std::string queryargs = matches[6];
    std::wstring querystring = convert_utf8_to_utf16(file + queryargs);

    HINTERNET raw_request = WinHttpOpenRequest(
            connection.get(),
            L"GET",
            querystring.c_str(),
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
            );

    if (!raw_request) {
        task->failure_what = std::string(get_phrase("download_error_connection_failure_request")) + ": " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    UniqueWinHTTPINTERNET request(raw_request);

    task->connection_desc = get_phrase("adding_headers");

    for (const auto &[hk, hv] : ytdlp_data["http_headers"].items()) {
        std::string hval = hv; 
        std::wstring header = convert_utf8_to_utf16(hk + ": " + hval);
        BOOL success = WinHttpAddRequestHeaders(request.get(), header.c_str(), header.length(), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (!success) {
            task->failure_what = std::string(get_phrase("download_error_connection_failure_headers")) + ": " + GetLastErrorAsString();
            task->state = DownloadState::ConnectionFailure;
            return;
        }
    }

    task->connection_desc = get_phrase("sending_request");

    BOOL sent_request_success = WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0L,
            WINHTTP_NO_REQUEST_DATA,
            0L,
            0L,
            NULL);

    if (!sent_request_success) {
        task->failure_what = std::string(get_phrase("download_error_connection_failure_sending")) + ": " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = get_phrase("accepting_response");

    BOOL received_success = WinHttpReceiveResponse(request.get(), NULL);

    if (!received_success) {
        task->failure_what = std::string(get_phrase("download_error_connection_failure_response")) + ": " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = get_phrase("getting_status_code");

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    BOOL got_status_code = WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_code_size,
            WINHTTP_NO_HEADER_INDEX
            );

    if (!got_status_code) {
        task->failure_what = std::string(get_phrase("download_error_connection_failure_status")) + ": " + GetLastErrorAsString();
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = get_phrase("checking_status_code");

    if (status_code != 200) {
        if (status_code == 429 || status_code == 402) {
            task->failure_what = get_phrase("error_youtube_temporary_block");
        } else {
            std::string what = std::string(get_phrase("download_error_connection_failure_bad_status_1"));
            what += " (";
            what += std::string(get_phrase("download_error_connection_failure_bad_status_2"));
            what += " ";
            what += std::to_string(status_code);
            what += "): ";
            what += GetLastErrorAsString();
            task->failure_what = what;
        }
        task->state = DownloadState::ConnectionFailure;
        return;
    }

    task->connection_desc = get_phrase("starting_download");

    task->state = DownloadState::Downloading;

    DWORD data_available = 0;
    DWORD data_downloaded = 0;
    std::vector<char> videodata;
    std::vector<char> downloadbuffer;
    downloadbuffer.reserve(4096);

    do {
        if (task->suggested_stop) {
            return;
        }
        data_available = 0;
        data_downloaded = 0;

        BOOL query_successful = WinHttpQueryDataAvailable(request.get(), &data_available);
        if (!query_successful)
        {
            task->failure_what = std::string(get_phrase("download_error_download_block_size")) + ": " + GetLastErrorAsString();
            task->state = DownloadState::DownloadFailure;
            return;
        }

        downloadbuffer.resize(data_available);

        BOOL read_result = WinHttpReadData(request.get(), downloadbuffer.data(), data_available, &data_downloaded);
        if (!read_result)
        {
            task->failure_what = std::string(get_phrase("download_error_download_read_block")) + ": " + GetLastErrorAsString();
            task->state = DownloadState::DownloadFailure;
            return;
        }

        downloadbuffer.resize(data_downloaded);

        videodata.insert(std::end(videodata), std::begin(downloadbuffer), std::end(downloadbuffer));
        task->bytesdownloaded = videodata.size();
    } while (data_available > 0);


    task->state = DownloadState::Saving;

    std::string savepath = config["downloads"];
    std::string videofile_name = download["_filename"];
    std::string videofile_path = savepath + "\\" + videofile_name;
    std::ofstream videofile(convert_utf8_to_utf16(videofile_path), std::ios::trunc | std::ios::binary);

    if (!videofile) {
        task->failure_what = get_phrase("download_error_save_open_outfile");
        task->state = DownloadState::SaveFailure;
        return;
    }

    videofile.write(videodata.data(), videodata.size());
    videofile.flush();
    videofile.close();

    task->state = DownloadState::Completed;
}
