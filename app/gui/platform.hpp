#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace szgui {

bool dlg_open(std::string& out, bool satz);
bool dlg_save(std::string& out, const std::string& suggest, int def_format);
bool dlg_folder(std::string& out);
void open_url(const char* url);
void open_in_explorer(const std::filesystem::path& p);
void open_file(const std::filesystem::path& p);
bool drag_out(const std::vector<std::filesystem::path>& paths, const std::function<void()>& on_drop);
void set_window_icon(GLFWwindow* win);
void install_crash_log();
std::string file_type_name(const std::string& name, bool is_dir);
std::string format_local_time(int64_t unix_ns);

}
