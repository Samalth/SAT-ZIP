#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif
#ifdef _WIN32
  #include <commdlg.h>
  #include <shlobj.h>
  #include <shellapi.h>
#endif

#include "platform.hpp"
#include "satzip_settings.hpp"
#include "satzip_version.h"

#include <GLFW/glfw3.h>
#ifdef _WIN32
  #define GLFW_EXPOSE_NATIVE_WIN32
  #include <GLFW/glfw3native.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace fs = std::filesystem;

namespace szgui {

#ifdef _WIN32
bool dlg_open(std::string& out, bool satz) {
    char buf[2048] = {0};
    OPENFILENAMEA o; ZeroMemory(&o, sizeof(o));
    o.lStructSize = sizeof(o); o.lpstrFile = buf; o.nMaxFile = sizeof(buf);
    o.lpstrFilter = satz ? "Archives (*.satz;*.zip;*.7z;*.tar;*.xz)\0*.satz;*.zip;*.7z;*.tar;*.xz;*.tar.xz\0All Files\0*.*\0" : "All Files\0*.*\0";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&o)) return false;
    out = buf; return true;
}
bool dlg_save(std::string& out, const std::string& suggest, int def_format) {
    char buf[2048] = "archive";
    if (!suggest.empty()) { strncpy(buf, suggest.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }
    OPENFILENAMEA o; ZeroMemory(&o, sizeof(o));
    o.lStructSize = sizeof(o); o.lpstrFile = buf; o.nMaxFile = sizeof(buf);
    o.lpstrFilter = "SAT-ZIP archive (*.satz)\0*.satz\0Zip archive (*.zip)\0*.zip\0"
                    "Tar archive (*.tar)\0*.tar\0" "7-Zip archive (*.7z)\0*.7z\0"
                    "Tar+XZ archive (*.tar.xz)\0*.tar.xz;*.xz\0";
    o.nFilterIndex = (def_format >= 0 && def_format <= 4) ? (DWORD)def_format + 1 : 1;
    o.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&o)) return false;
    out = buf;
    {
        const char* exts[] = { ".satz", ".zip", ".tar", ".7z", ".tar.xz" };
        const char* want = exts[(o.nFilterIndex >= 1 && o.nFilterIndex <= 5) ? o.nFilterIndex - 1 : 0];
        std::string lo = out;
        for (auto& c : lo) c = (char)tolower((unsigned char)c);
        size_t wl = strlen(want);
        bool has = lo.size() >= wl && lo.compare(lo.size() - wl, wl, want) == 0;
        if (!has && o.nFilterIndex == 5 && lo.size() >= 3 && lo.compare(lo.size() - 3, 3, ".xz") == 0) has = true;
        if (!has) out += want;
    }
    return true;
}
bool dlg_folder(std::string& out) {
    BROWSEINFOA bi; ZeroMemory(&bi, sizeof(bi));
    bi.lpszTitle = "Select folder"; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return false;
    char path[MAX_PATH]; bool ok = SHGetPathFromIDListA(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) out = path;
    return ok;
}
#else
static bool dlg_popen(std::string& out, const char* cmd) {
    FILE* p = popen(cmd, "r"); if (!p) return false;
    char buf[4096]; bool got = fgets(buf, sizeof(buf), p) != nullptr; pclose(p);
    if (!got) return false;
    std::string s(buf); if (!s.empty() && s.back() == '\n') s.pop_back();
    if (s.empty()) return false; out = s; return true;
}
bool dlg_open(std::string& out, bool) { return dlg_popen(out, "zenity --file-selection 2>/dev/null"); }
bool dlg_save(std::string& out, const std::string&, int) { return dlg_popen(out, "zenity --file-selection --save --confirm-overwrite 2>/dev/null"); }
bool dlg_folder(std::string& out) { return dlg_popen(out, "zenity --file-selection --directory 2>/dev/null"); }
#endif

void open_url(const char* url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string c = std::string("open \"") + url + "\""; (void)system(c.c_str());
#else
    std::string c = std::string("xdg-open \"") + url + "\" >/dev/null 2>&1 &"; (void)system(c.c_str());
#endif
}

void open_in_explorer(const fs::path& p) {
#ifdef _WIN32
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        std::wstring args = L"/select,\"" + p.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
#elif defined(__APPLE__)
    std::string c = std::string("open -R \"") + p.string() + "\""; (void)system(c.c_str());
#else
    std::error_code ec;
    fs::path dir = fs::is_directory(p, ec) ? p : p.parent_path();
    std::string c = std::string("xdg-open \"") + dir.string() + "\" >/dev/null 2>&1 &"; (void)system(c.c_str());
#endif
}

#ifdef _WIN32
static void set_hwnd_icon(HWND hwnd) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    HICON hBig = nullptr, hSmall = nullptr;
    if (ExtractIconExW(exe, 0, &hBig, &hSmall, 1)) {
        if (hBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hBig);
        if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);
    }
}
#endif

void set_window_icon(GLFWwindow* win) {
#ifdef _WIN32
    if (HWND hwnd = glfwGetWin32Window(win)) set_hwnd_icon(hwnd);
#else
    (void)win;
#endif
}

#ifdef _WIN32
static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep) {
    std::error_code ec;
    fs::create_directories(sz::settings_dir(), ec);
    FILE* f = nullptr;
    fopen_s(&f, (sz::settings_dir() / "crash.log").string().c_str(), "a");
    if (f) {
        fprintf(f, "SAT-ZIP %s crash: code=0x%08lX addr=%p\n", SATZIP_VERSION,
                ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0,
                ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void install_crash_log() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_filter);
#endif
    std::set_terminate([] {
        std::error_code ec;
        fs::create_directories(sz::settings_dir(), ec);
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, (sz::settings_dir() / "crash.log").string().c_str(), "a");
#else
        f = fopen((sz::settings_dir() / "crash.log").string().c_str(), "a");
#endif
        if (f) {
            const char* what = "terminate (unhandled C++ exception, possibly out of memory)";
            fprintf(f, "SAT-ZIP %s %s\n", SATZIP_VERSION, what);
            fclose(f);
        }
        std::abort();
    });
}

}
