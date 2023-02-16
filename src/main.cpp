#include <cstddef>
#include <errhandlingapi.h>
#include <fstream>
#include <ios>
#include <iterator>
#include <locale.h>
#include <locale>
#include <minwinbase.h>
#include <optional>
#include <processthreadsapi.h>
#include <sstream>
#include <stdio.h>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <string.h>
#include <string_view>
#include <windows.h>
#include <shlobj.h>
#include <stdlib.h>
#include <knownfolders.h>
#include <pathcch.h>
#include <nlohmann/json.hpp>
#include <string>
#include <codecvt>
#include <iostream>
#include <winerror.h>
#include <winnt.h>
#include <future>
#include <list>
#include <winhttp.h>
#include <regex>
#include <shlwapi.h>

using json = nlohmann::json;

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

    DownloadTask(const std::string &url, bool audio_only) : audio_only(audio_only), url(url) {

    }
};

// Forward declarations
void main_loop_wrapper();
void main_loop();
std::wstring get_exec_path();
std::wstring get_user_desktop_path();
std::string convert_utf16_to_utf8(const std::wstring &);
std::optional<std::wstring> get_folder_browser_path();
void load_font();
void load_imgui_ini();
void save_imgui_ini();
void downloadVideoFromUrl(DownloadTask*);
std::string GetLastErrorAsString(DWORD* = NULL);
std::string format_filesize(DWORD filesize);

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Data
GLFWwindow* appwindow = nullptr;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.0f);

const std::regex uri_regex("^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?", std::regex::ECMAScript);
const std::wstring config_filename = L"nastaveni.json";
std::wstring exec_path;
std::wstring execdir_path;
std::wstring desktop_path;
std::wstring config_path;
std::wstring font_path;
std::wstring ytdlp_path;
std::wstring imgui_ini_path;
char* font_data;
json config;
std::list<DownloadTask*> downloads;
std::string text_input_youtube_url;
HINTERNET internet;

bool waiting_for_savepath = false;
std::future<std::optional<std::wstring>> filebrowser_for_savepath;


INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    internet = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (internet == NULL) {
        MessageBox(NULL, "Nelze načíst internetového klienta", NULL, MB_ICONERROR);
        return 1;
    }

    exec_path = get_exec_path();
    desktop_path = get_user_desktop_path();

    execdir_path = exec_path;
    PathCchRemoveFileSpec(execdir_path.data(), execdir_path.length());
    execdir_path.resize(wcslen(execdir_path.c_str()));

    config_path = execdir_path;
    config_path.resize(MAX_PATH);
    PathCchAppend(config_path.data(), MAX_PATH, config_filename.c_str());
    config_path.resize(wcslen(config_path.c_str()));

    font_path = execdir_path;
    font_path.resize(MAX_PATH);
    PathCchAppend(font_path.data(), MAX_PATH, L"font.ttf");
    font_path.resize(wcslen(font_path.c_str()));

    ytdlp_path = execdir_path;
    ytdlp_path.resize(MAX_PATH);
    PathCchAppend(ytdlp_path.data(), MAX_PATH, L"yt-dlp.exe");
    ytdlp_path.resize(wcslen(ytdlp_path.c_str()));

    imgui_ini_path = execdir_path;
    imgui_ini_path.resize(MAX_PATH);
    PathCchAppend(imgui_ini_path.data(), MAX_PATH, L"imgui.ini");
    imgui_ini_path.resize(wcslen(imgui_ini_path.c_str()));

    {
        std::ifstream configfile(config_path);
        if (configfile) {
            config = json::parse(configfile);
        } else {
            config = json::object({
                    {"uloziste", convert_utf16_to_utf8(desktop_path)},
                    {"velikostfontu", 26},
                    });
        }
        configfile.close();
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return 1;
    }

    appwindow = glfwCreateWindow(1280, 720, "YtDownloader", NULL, NULL);
    if (appwindow == NULL) {
        return 1;
    }
    glfwMakeContextCurrent(appwindow);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;

    load_imgui_ini();
    load_font();

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(appwindow, true);
    ImGui_ImplOpenGL3_Init();

    while (!glfwWindowShouldClose(appwindow)) {
        main_loop_wrapper();
    }

    save_imgui_ini();

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(appwindow);
    glfwTerminate();

    {
        std::ofstream configfile(config_path);
        configfile << config;
        configfile.close();
    }

    for (const auto& download : downloads) {
        download->suggested_stop = true;
    }

    for (const auto& download : downloads) {
        download->self_thread.join();
    }

    WinHttpCloseHandle(internet);
    CoUninitialize();

    return 0;
}

void main_loop_wrapper() {
    glfwPollEvents();

    // Create new frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    // Save imgui.ini
    if (io.WantSaveIniSettings) {
        save_imgui_ini();
        io.WantSaveIniSettings = false;
    }

    main_loop();

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(appwindow, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(appwindow);
}

void main_loop() {
    if (waiting_for_savepath && filebrowser_for_savepath.valid()) {
        std::optional<std::wstring> new_savepath = filebrowser_for_savepath.get();
        if (new_savepath.has_value()) {
            config["uloziste"] = convert_utf16_to_utf8(*new_savepath);
        }

        waiting_for_savepath = false;
    }

    // ImGui::Begin("Debug");
    // ImGui::Text("EXE Location: %s", convert_utf16_to_utf8(exec_path).c_str());
    // ImGui::Text("EXE Dir: %s", convert_utf16_to_utf8(execdir_path).c_str());
    // ImGui::Text("Font path: %s", convert_utf16_to_utf8(font_path).c_str());
    // ImGui::Text("User Desktop: %ls", desktop_path.c_str());
    // ImGui::Text("Config location: %s", convert_utf16_to_utf8(config_path).c_str());
    // ImGui::Text("yt-dlp location: %s", convert_utf16_to_utf8(ytdlp_path).c_str());
    // ImGui::End();

    ImGui::Begin("Nastavení");

    std::string savedir = config["uloziste"];
    ImGui::Text("Složka na písničky: %s", savedir.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Změnit") and !waiting_for_savepath) {
        filebrowser_for_savepath = std::async(std::launch::async, get_folder_browser_path);
        waiting_for_savepath = true;
    };

    ImGui::Spacing();

    int fontsize = config["velikostfontu"];
    ImGui::InputInt("Velikost textu", &fontsize, 1, 5);
    ImGui::Text("(Velikost textu se projeví až po restartování programu)");
    if (fontsize < 8) fontsize = 8;
    if (fontsize != config["velikostfontu"]) {
        config["velikostfontu"] = fontsize;
    }
    ImGui::End();

    ImGui::Begin("Stahování");

    ImGui::InputText("YouTube odkaz", &text_input_youtube_url);

    bool download_button_pressed = ImGui::Button("Stáhnout");
    ImGui::SameLine();
    bool download_audio_only_button_pressed = ImGui::Button("Stáhnout pouze zvuk");

    if (download_button_pressed || download_audio_only_button_pressed) {
        DownloadTask *new_task = new DownloadTask(text_input_youtube_url, download_audio_only_button_pressed);
        auto &task = downloads.emplace_back(new_task);
        task->self_thread = std::thread(downloadVideoFromUrl, task);
        text_input_youtube_url.clear();
    };

    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
    ImGui::Text("Probíhající stahování");
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));

    auto downloads_iter = downloads.begin();
    while (downloads_iter != std::end(downloads)) {
        bool should_erase = false;
        DownloadTask &download = **downloads_iter;
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        const char *labeltext = "!! Někde se stala chyba !!";
        switch (download.state) {
            case DownloadState::Inicialization:
                labeltext = download.url.c_str();
                break;
            case DownloadState::InicializationSuccess:
            case DownloadState::NoDownloadFound:
            case DownloadState::Connecting:
            case DownloadState::ConnectionFailure:
            case DownloadState::Downloading:
            case DownloadState::Saving:
            case DownloadState::Completed:
                labeltext = download.title.c_str();
                break;
            default:;
        }

        if (ImGui::TreeNode((const void*)&download, "%s", labeltext)) {
            ImGui::Text("Odkaz: %s", download.url.c_str());
            if (download.audio_only) {
                ImGui::Text("Pouze zvuk");
            }
            switch (download.state) {
                case DownloadState::DownloadFailure:
                case DownloadState::InicializationSuccess:
                case DownloadState::NoDownloadFound:
                case DownloadState::Connecting:
                case DownloadState::ConnectionFailure:
                case DownloadState::Downloading:
                case DownloadState::Saving:
                case DownloadState::SaveFailure:
                case DownloadState::Completed:
                    ImGui::Text("Titul: %s", download.title.c_str());
                default:;
            }
            switch (download.state) {
                case DownloadState::Connecting:
                case DownloadState::Downloading:
                case DownloadState::DownloadFailure:
                case DownloadState::Saving:
                case DownloadState::SaveFailure:
                case DownloadState::Completed:
                    ImGui::Text("Velikost videa: %s", format_filesize(download.filesize).c_str());
                default:;
            }
            if (download.state == DownloadState::Connecting) {
                ImGui::Text("Status připojení: %s", download.connection_desc.c_str());
            }
            if (download.state == DownloadState::Downloading) {
                ImGui::Text("Stahování...");
                ImGui::ProgressBar(float(download.bytesdownloaded) / float(download.filesize));
            }
            if (download.state == DownloadState::Saving || download.state == DownloadState::SaveFailure) {
                ImGui::Text("Staženo");
                ImGui::ProgressBar(1);
            }
            switch (download.state) {
                case DownloadState::InicializationFailure:
                case DownloadState::NoDownloadFound:
                case DownloadState::ConnectionFailure:
                case DownloadState::DownloadFailure:
                case DownloadState::SaveFailure:
                    ImGui::Text("Chyba: %s", download.failure_what.c_str());
                default:;
            }
            if (download.state == DownloadState::InicializationInvalid) {
                ImGui::Text("Chyba v získávání informací, nejspíše neplatný odkaz");
            }
            if (download.state == DownloadState::Completed) {
                ImGui::Text("Staženo a uloženo");
            }

            bool close_button = false;
            std::string close_button_text;
            switch(download.state) {
                case DownloadState::InicializationFailure:
                case DownloadState::NoDownloadFound:
                case DownloadState::ConnectionFailure:
                case DownloadState::DownloadFailure:
                case DownloadState::SaveFailure:
                case DownloadState::Completed:
                case DownloadState::InicializationInvalid:
                    close_button_text = "Zavřít";
                    close_button = true;
                    break;
                case DownloadState::Downloading:
                    close_button_text = "Zrušit";
                    close_button = true;
                    break;
                default:;
            }
            if (close_button) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 { 0.6f, 0.0f, 0.0f, 1.0f });
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 { 0.8f, 0.0f, 0.0f, 1.0f });
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4 { 0.8f, 0.2f, 0.2f, 1.0f });
                if (ImGui::Button(close_button_text.c_str())) {
                    download.suggested_stop = true;
                    should_erase = true;
                }
                ImGui::PopStyleColor(3);
            }
            ImGui::TreePop();
        }
        if (should_erase) {
            downloads_iter = downloads.erase(downloads_iter);
        } else {
            downloads_iter++;
        }
    }
    ImGui::End();
}

std::wstring get_user_desktop_path() {
    PWSTR ret;
    HRESULT result = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, NULL, &ret);
    std::wstring buf(ret);
    CoTaskMemFree(ret);
    return buf;
}

std::wstring get_exec_path() {
    std::wstring res;
    res.resize(MAX_PATH);
    HMODULE mod = GetModuleHandleW(NULL);
    DWORD newlen = GetModuleFileNameW(mod, res.data(), MAX_PATH);
    res.resize(newlen);
    return res;
}

std::string convert_utf16_to_utf8(const std::wstring &wstr) {
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}.to_bytes(wstr);
}

std::wstring convert_utf8_to_utf16(const std::string &str) {
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}.from_bytes(str);
}

// Shamelessly stolen
std::optional<std::wstring> get_folder_browser_path() {
 // Create an instance of the File Open Dialog object
    IFileOpenDialog *pFileOpen;
    HRESULT hr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr))
    {
        // Set the options on the dialog object
        DWORD dwOptions;
        hr = pFileOpen->GetOptions(&dwOptions);
        if (SUCCEEDED(hr))
        {
            hr = pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        }

        // Show the dialog
        if (SUCCEEDED(hr))
        {
            hr = pFileOpen->Show(nullptr);
        }

        // Get the result of the user's interaction with the dialog
        if (SUCCEEDED(hr))
        {
            // Get the selected folder
            IShellItem *pItem;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr))
            {
                // Get the folder's path
                PWSTR pszFolderPath;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath);
                if (SUCCEEDED(hr))
                {
                    // Use the folder path here
                    std::wstring buf(pszFolderPath);
                    CoTaskMemFree(pszFolderPath);
                    return buf;
                }

                pItem->Release();
            }
        }

        pFileOpen->Release();
    }
    return std::optional<std::wstring>();
}

void load_imgui_ini() {
    std::ifstream inifile(imgui_ini_path);

    if (!inifile) {
        return;
    }

    inifile.seekg(0, inifile.end);
    std::streamsize inifilesize = inifile.tellg();
    inifile.seekg(0, inifile.beg);

    char *ini_data = new char[inifilesize];
    inifile.read(ini_data, inifilesize);

    ImGui::LoadIniSettingsFromMemory(ini_data, inifilesize);
}

void save_imgui_ini() {
    std::ofstream inifile(imgui_ini_path, std::ios::trunc | std::ios::binary);

    size_t inimemorysize = 0;
    const char *inimemory = ImGui::SaveIniSettingsToMemory(&inimemorysize);

    inifile.write(inimemory, inimemorysize);
}

void load_font() {
    ImGuiIO& io = ImGui::GetIO();

    std::ifstream fontfile(font_path, std::ios::in | std::ios::binary);
    if (fontfile) {
        fontfile.seekg(0, fontfile.end);
        std::streamsize fontfilesize = fontfile.tellg();
        fontfile.seekg(0, fontfile.beg);

        font_data = new char[fontfilesize];
        fontfile.read(font_data, fontfilesize);

        ImVector<ImWchar> ranges;
        ImFontGlyphRangesBuilder builder;
        // No break space
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        builder.AddRanges(io.Fonts->GetGlyphRangesGreek());
        builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
        builder.AddRanges(io.Fonts->GetGlyphRangesThai());
        builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
        for (ImWchar val = u'\u0100'; val <= u'\u017f'; val++) {
            builder.AddChar(val);
        }
        for (ImWchar val = u'\u0080'; val <= u'\u00FF'; val++) {
            builder.AddChar(val);
        }
        for (ImWchar val = u'\u2000'; val <= u'\u206F'; val++) {
            builder.AddChar(val);
        }
        for (ImWchar val = u'\u2400'; val <= u'\u243F'; val++) {
            builder.AddChar(val);
        }
        for (ImWchar val = u'\u3000'; val <= u'\u303F'; val++) {
            builder.AddChar(val);
        }
        for (ImWchar val = u'\u3000'; val <= u'\u303F'; val++) {
            builder.AddChar(val);
        }
        for (ImWchar val = u'\uFE70'; val <= u'\uFEFF'; val++) {
            builder.AddChar(val);
        }
        for (ImWchar val = u'\uFF00'; val <= u'\uFFEF'; val++) {
            builder.AddChar(val);
        }
        builder.BuildRanges(&ranges);

        ImFont* font = io.Fonts->AddFontFromMemoryTTF(font_data, fontfilesize, config["velikostfontu"], NULL, ranges.Data);
        io.Fonts->Build();
        IM_ASSERT(font != NULL);
    } else {
        io.Fonts->AddFontDefault();
    }
}

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
