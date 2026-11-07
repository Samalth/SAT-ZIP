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
#include <ctime>
#include <exception>
#include <functional>
#include <thread>
#include <unordered_map>

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

void open_file(const fs::path& p) {
#ifdef _WIN32
    std::thread([w = p.wstring()] {
        HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        SHELLEXECUTEINFOW sei; ZeroMemory(&sei, sizeof(sei));
        sei.cbSize = sizeof(sei); sei.fMask = SEE_MASK_FLAG_NO_UI; sei.lpFile = w.c_str(); sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei) && GetLastError() == ERROR_NO_ASSOCIATION) {
            OPENASINFO oi = { w.c_str(), nullptr, OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_EXEC };
            SHOpenWithDialog(nullptr, &oi);
        }
        if (SUCCEEDED(co)) CoUninitialize();
    }).detach();
#elif defined(__APPLE__)
    std::string c = "open \"" + p.string() + "\""; (void)system(c.c_str());
#else
    std::string c = "xdg-open \"" + p.string() + "\" >/dev/null 2>&1 &"; (void)system(c.c_str());
#endif
}

#ifdef _WIN32
class DropSource : public IDropSource {
    LONG refs_ = 1;
    DWORD effect_ = DROPEFFECT_NONE;
    const std::function<void()>& on_drop_;
public:
    bool dropped = false;
    explicit DropSource(const std::function<void()>& on_drop) : on_drop_(on_drop) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { return (ULONG)InterlockedDecrement(&refs_); }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape || (keys & MK_RBUTTON)) return DRAGDROP_S_CANCEL;
        if (keys & MK_LBUTTON) return S_OK;
        POINT pt; GetCursorPos(&pt);
        DWORD pid = 0;
        GetWindowThreadProcessId(GetAncestor(WindowFromPoint(pt), GA_ROOT), &pid);
        if (effect_ == DROPEFFECT_NONE || pid == GetCurrentProcessId()) return DRAGDROP_S_CANCEL;
        on_drop_();
        dropped = true;
        return DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect) override { effect_ = effect; return DRAGDROP_S_USEDEFAULTCURSORS; }
};

bool drag_out(const std::vector<fs::path>& paths, const std::function<void()>& on_drop) {
    static const bool ole = SUCCEEDED(OleInitialize(nullptr));
    if (!ole || paths.empty()) return false;
    size_t chars = 1;
    for (const auto& p : paths) chars += p.wstring().size() + 1;
    HGLOBAL h = GlobalAlloc(GHND, sizeof(DROPFILES) + chars * sizeof(wchar_t));
    if (!h) return false;
    DROPFILES* df = (DROPFILES*)GlobalLock(h);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    wchar_t* w = (wchar_t*)(df + 1);
    for (const auto& p : paths) { std::wstring s = p.wstring(); memcpy(w, s.c_str(), s.size() * sizeof(wchar_t)); w += s.size() + 1; }
    GlobalUnlock(h);

    IDataObject* obj = nullptr;
    if (FAILED(SHCreateDataObject(nullptr, 0, nullptr, nullptr, IID_PPV_ARGS(&obj)))) { GlobalFree(h); return false; }
    FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM sm = {}; sm.tymed = TYMED_HGLOBAL; sm.hGlobal = h;
    if (FAILED(obj->SetData(&fe, &sm, TRUE))) { GlobalFree(h); obj->Release(); return false; }

    IDragSourceHelper* helper = nullptr;
    IShellItemImageFactory* images = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper))) &&
        SUCCEEDED(SHCreateItemFromParsingName(paths[0].wstring().c_str(), nullptr, IID_PPV_ARGS(&images)))) {
        const int px = 96;
        SHDRAGIMAGE di = {};
        if (SUCCEEDED(images->GetImage({ px, px }, SIIGBF_ICONONLY, &di.hbmpDragImage))) {
            di.sizeDragImage = { px, px };
            di.ptOffset = { px / 2, px - 12 };
            di.crColorKey = CLR_NONE;
            if (FAILED(helper->InitializeFromBitmap(&di, obj))) DeleteObject(di.hbmpDragImage);
        }
    }
    if (images) images->Release();
    if (helper) helper->Release();

    DropSource src(on_drop);
    DWORD effect = DROPEFFECT_NONE;
    DoDragDrop(obj, &src, DROPEFFECT_COPY, &effect);
    obj->Release();
    return src.dropped;
}
#else
bool drag_out(const std::vector<fs::path>&, const std::function<void()>&) { return false; }
#endif

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

std::string file_type_name(const std::string& name, bool is_dir) {
    size_t dot = name.rfind('.');
    std::string ext = (is_dir || dot == std::string::npos || dot == 0) ? std::string() : name.substr(dot + 1);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    static std::unordered_map<std::string, std::string> cache;
    std::string key = is_dir ? std::string("/") : ext;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::string type;
#ifdef _WIN32
    std::string probe = is_dir ? std::string("folder") : (ext.empty() ? std::string("file") : "file." + ext);
    int wn = MultiByteToWideChar(CP_UTF8, 0, probe.c_str(), -1, nullptr, 0);
    std::wstring wprobe((size_t)wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, probe.c_str(), -1, &wprobe[0], wn);
    SHFILEINFOW sfi;
    ZeroMemory(&sfi, sizeof(sfi));
    if (SHGetFileInfoW(wprobe.c_str(), is_dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) && sfi.szTypeName[0]) {
        int un = WideCharToMultiByte(CP_UTF8, 0, sfi.szTypeName, -1, nullptr, 0, nullptr, nullptr);
        type.assign((size_t)(un > 0 ? un - 1 : 0), '\0');
        if (un > 1) WideCharToMultiByte(CP_UTF8, 0, sfi.szTypeName, -1, &type[0], un, nullptr, nullptr);
    }
#endif
    if (type.empty()) {
        if (is_dir) type = "File folder";
        else if (ext.empty()) type = "File";
        else {
            type = ext;
            for (auto& c : type) c = (char)toupper((unsigned char)c);
            type += " File";
        }
    }
    cache.emplace(key, type);
    return type;
}

std::string format_local_time(int64_t unix_ns) {
    if (unix_ns == INT64_MIN) return std::string();
#ifdef _WIN32
    const int64_t ticks = unix_ns / 100 + 116444736000000000LL;
    if (ticks < 0) return std::string();
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(uint64_t)ticks;
    ft.dwHighDateTime = (DWORD)((uint64_t)ticks >> 32);
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return std::string();
    wchar_t d[64] = {0}, t[64] = {0};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, d, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, t, 64);
    std::wstring w = std::wstring(d) + L" " + t;
    int un = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)(un > 0 ? un - 1 : 0), '\0');
    if (un > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], un, nullptr, nullptr);
    return out;
#else
    time_t secs = (time_t)(unix_ns / 1000000000);
    struct tm lt;
    if (!localtime_r(&secs, &lt)) return std::string();
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &lt);
    return buf;
#endif
}

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
