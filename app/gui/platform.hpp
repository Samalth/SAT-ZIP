#pragma once

#include <filesystem>
#include <string>

struct GLFWwindow;

namespace szgui {

bool dlg_open(std::string& out, bool satz);
bool dlg_save(std::string& out, const std::string& suggest, int def_format);
bool dlg_folder(std::string& out);
void open_url(const char* url);
void open_in_explorer(const std::filesystem::path& p);
void set_window_icon(GLFWwindow* win);
void install_crash_log();

}
