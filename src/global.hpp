#pragma once

#include <regex>
#include <nlohmann/json.hpp>
#include "download.hpp"
#include "utils.hpp"
#include <list>
#include <winhttp.h>
#include <future>
#include <imgui.h>
#include <GLFW/glfw3.h>

extern GLFWwindow* appwindow;
extern const ImVec4 clear_color;

extern const std::regex uri_regex;
extern const std::wstring config_filename;
extern std::wstring exec_path;
extern std::wstring execdir_path;
extern std::wstring desktop_path;
extern std::wstring config_path;
extern std::wstring font_path;
extern std::wstring ytdlp_path;
extern std::wstring imgui_ini_path;
extern char* font_data;
extern nlohmann::json config;
extern std::list<DownloadTask*> downloads;
extern std::string text_input_youtube_url;
extern UniqueWinHTTPINTERNET internet;

extern bool waiting_for_savepath;
extern std::future<std::optional<std::wstring>> filebrowser_for_savepath;
