#pragma once

#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <shellapi.h>
  #include <mapi.h>
  #include <shlobj.h>
#endif

namespace sz {
namespace fs = std::filesystem;

enum class MailResult { Failed, Attached, ClipboardCompose };

#ifdef _WIN32
namespace mail_detail {

inline std::wstring reg_sz(HKEY root, const wchar_t* sub, const wchar_t* val) {
    HKEY k;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
    wchar_t buf[1024]; DWORD sz = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(k, val, nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return L"";
    std::wstring s(buf, sz / sizeof(wchar_t));
    while (!s.empty() && s.back() == L'\0') s.pop_back();
    if (type == REG_EXPAND_SZ) {
        wchar_t ex[1024];
        if (ExpandEnvironmentStringsW(s.c_str(), ex, 1024)) s = ex;
    }
    return s;
}

inline void mapi_dll_candidates(std::vector<std::wstring>& out) {
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        std::wstring def = reg_sz(root, L"SOFTWARE\\Clients\\Mail", nullptr);
        std::vector<std::wstring> clients;
        if (!def.empty()) clients.push_back(def);
        HKEY k;
        if (RegOpenKeyExW(root, L"SOFTWARE\\Clients\\Mail", 0, KEY_READ, &k) == ERROR_SUCCESS) {
            wchar_t name[256];
            for (DWORD i = 0;; ++i) {
                DWORD nl = 256;
                if (RegEnumKeyExW(k, i, name, &nl, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                clients.push_back(name);
            }
            RegCloseKey(k);
        }
        for (const auto& c : clients) {
            std::wstring sub = L"SOFTWARE\\Clients\\Mail\\" + c;
            for (const wchar_t* v : { L"DLLPathEx", L"DLLPath" }) {
                std::wstring d = reg_sz(root, sub.c_str(), v);
                if (!d.empty()) out.push_back(d);
            }
        }
    }
    out.push_back(L"MAPI32.DLL");
}

inline bool try_mapi(const fs::path& file) {
    std::string full = file.string();
    std::string name = file.filename().string();
    std::string subject = "Archive: " + name;

    MapiFileDesc att{};
    att.nPosition = (ULONG)-1;
    att.lpszPathName = (LPSTR)full.c_str();
    att.lpszFileName = (LPSTR)name.c_str();

    MapiMessage msg{};
    msg.lpszSubject = (LPSTR)subject.c_str();
    msg.lpszNoteText = (LPSTR)"";
    msg.nFileCount = 1;
    msg.lpFiles = &att;

    std::vector<std::wstring> dlls;
    mapi_dll_candidates(dlls);
    for (const auto& d : dlls) {
        HMODULE m = LoadLibraryW(d.c_str());
        if (!m) continue;
        auto send = (decltype(&MAPISendMail))GetProcAddress(m, "MAPISendMail");
        if (send) {
            ULONG r = send(0, 0, &msg, MAPI_DIALOG | MAPI_LOGON_UI, 0);
            if (r == SUCCESS_SUCCESS || r == MAPI_E_USER_ABORT) { FreeLibrary(m); return true; }
        }
        FreeLibrary(m);
    }
    return false;
}

inline bool file_to_clipboard(const fs::path& file) {
    std::wstring w = fs::absolute(file).wstring();
    size_t bytes = sizeof(DROPFILES) + (w.size() + 2) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) return false;
    auto* df = (DROPFILES*)GlobalLock(h);
    memset(df, 0, bytes);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    memcpy((uint8_t*)df + sizeof(DROPFILES), w.c_str(), w.size() * sizeof(wchar_t));
    GlobalUnlock(h);
    if (!OpenClipboard(nullptr)) { GlobalFree(h); return false; }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_HDROP, h) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(h);
    return ok;
}

}
#endif

#ifdef _WIN32
namespace mail_detail {
inline bool try_classic_outlook(const fs::path& file) {
    std::wstring exe = reg_sz(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OUTLOOK.EXE", nullptr);
    std::error_code ec;
    if (exe.empty() || !fs::exists(exe, ec)) return false;
    std::wstring args = L"/c ipm.note /a \"" + fs::absolute(file).wstring() + L"\"";
    HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}
}
#endif

inline bool mail_try_direct(const fs::path& file) {
#ifdef _WIN32
    if (mail_detail::try_mapi(file)) return true;
    if (mail_detail::try_classic_outlook(file)) return true;
    return false;
#else
    std::string c = "xdg-email --attach \"" + file.string() + "\" >/dev/null 2>&1 &";
    return system(c.c_str()) == 0;
#endif
}

inline MailResult mail_fallback_compose(const fs::path& file) {
#ifdef _WIN32
    bool clip = mail_detail::file_to_clipboard(file);
    std::wstring url = L"mailto:?subject=Archive:%20" + file.filename().wstring();
    HINSTANCE r = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r > 32) return clip ? MailResult::ClipboardCompose : MailResult::Attached;
    return MailResult::Failed;
#else
    (void)file;
    return MailResult::Failed;
#endif
}

inline MailResult mail_with_attachment(const fs::path& file) {
    if (mail_try_direct(file)) return MailResult::Attached;
    return mail_fallback_compose(file);
}

}
