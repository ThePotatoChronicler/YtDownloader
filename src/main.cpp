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
#include "utils.hpp"
#include "download.hpp"
#include "global.hpp"

using json = nlohmann::json;

// Forward declarations
void main_loop_wrapper();
void main_loop();
std::wstring get_exec_path();
std::wstring get_user_desktop_path();
std::optional<std::wstring> get_folder_browser_path();
void load_font();
void load_imgui_ini();
void save_imgui_ini();

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Data
GLFWwindow* appwindow = nullptr;
const ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.0f);

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
