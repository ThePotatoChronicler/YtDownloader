#pragma once

#include <string>
#include <thread>
#include <windows.h>

enum class DownloadState {
    Inicialization,
    InicializationFailure,
    InicializationInvalid,
    InicializationSuccess,
    NoDownloadFound,
    Connecting,
    ConnectionFailure,
    Downloading,
    DownloadFailure,
    Saving,
    SaveFailure,
    Completed,
};

struct DownloadTask {
    std::thread self_thread;
    DownloadState state = DownloadState::Inicialization;

    bool audio_only;

    // Only valid when Failure occurs
    std::string failure_what;

    // Only valid with InicializationSuccess and onward
    std::string title;

    // Only valid during Connecting
    std::string connection_desc;

    // Only valid during and after connection
    DWORD filesize = 0;

    // Only valid while and after downloading
    DWORD bytesdownloaded = 0;

    // Populated by reader (the GUI)
    bool suggested_stop = false;

    std::string url;

    DownloadTask(const std::string url, bool audio_only) : audio_only(audio_only), url(url) {

    }
};

void downloadVideoFromUrl(DownloadTask*);
