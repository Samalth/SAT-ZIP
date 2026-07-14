#pragma once

#include <cstdlib>
#include <string>
#include <fstream>
#include <filesystem>
#include <system_error>

namespace sz {
namespace fs = std::filesystem;

struct Settings {
    int  level        = 9;
    int  threads      = 0;
    bool compress_all = false;
    int  mode         = 1;
    bool dark         = true;
    bool zipnow_folder = true;
    int  format       = 0;
    int  win_w        = 1100;
    int  win_h        = 760;
};

inline fs::path settings_dir() {
#ifdef _WIN32
    const char* a = std::getenv("APPDATA");
    return (a ? fs::path(a) : fs::temp_directory_path()) / "SAT-ZIP";
#elif defined(__APPLE__)
    const char* h = std::getenv("HOME");
    return fs::path(h ? h : ".") / "Library" / "Application Support" / "SAT-ZIP";
#else
    const char* x = std::getenv("XDG_CONFIG_HOME");
    if (x) return fs::path(x) / "sat-zip";
    const char* h = std::getenv("HOME");
    return fs::path(h ? h : ".") / ".config" / "sat-zip";
#endif
}
inline fs::path settings_path() { return settings_dir() / "settings.ini"; }

inline void load_settings(Settings& s) {
    std::ifstream f(settings_path());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if      (k == "level")        s.level = std::atoi(v.c_str());
        else if (k == "threads")      s.threads = std::atoi(v.c_str());
        else if (k == "compress_all") s.compress_all = std::atoi(v.c_str()) != 0;
        else if (k == "mode")         s.mode = std::atoi(v.c_str());
        else if (k == "dark")         s.dark = std::atoi(v.c_str()) != 0;
        else if (k == "zipnow_folder") s.zipnow_folder = std::atoi(v.c_str()) != 0;
        else if (k == "format")       s.format = std::atoi(v.c_str());
        else if (k == "win_w")        s.win_w = std::atoi(v.c_str());
        else if (k == "win_h")        s.win_h = std::atoi(v.c_str());
    }
    if (s.level < 1) s.level = 1;
    if (s.level > 9) s.level = 9;
    if (s.mode < 0 || s.mode > 2) s.mode = 1;
    if (s.format < 0 || s.format > 4) s.format = 0;
    if (s.threads < 0) s.threads = 0;
    if (s.win_w < 700) s.win_w = 700;
    if (s.win_h < 500) s.win_h = 500;
}

inline bool save_settings(const Settings& s) {
    std::error_code ec;
    fs::create_directories(settings_dir(), ec);
    std::ofstream f(settings_path(), std::ios::trunc);
    if (!f) return false;
    f << "level=" << s.level << "\n"
      << "threads=" << s.threads << "\n"
      << "compress_all=" << (s.compress_all ? 1 : 0) << "\n"
      << "mode=" << s.mode << "\n"
      << "dark=" << (s.dark ? 1 : 0) << "\n"
      << "zipnow_folder=" << (s.zipnow_folder ? 1 : 0) << "\n"
      << "format=" << s.format << "\n"
      << "win_w=" << s.win_w << "\n"
      << "win_h=" << s.win_h << "\n";
    return (bool)f;
}

} // namespace sz
