#pragma once

#include <string>
#include <filesystem>

namespace sz {
namespace fs = std::filesystem;

#ifdef _WIN32
inline std::wstring reg_str(HKEY root, const wchar_t* sub, const wchar_t* val) {
    HKEY k;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
    wchar_t buf[1024]; DWORD sz = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(k, val, nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ) return L"";
    return std::wstring(buf, sz / sizeof(wchar_t)).c_str();
}
#endif

inline fs::path find_obelisk() {
#ifdef _WIN32
    const wchar_t* base = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
    const wchar_t* ids[] = {
        L"{8F3C2A14-7B1D-4A2E-9C77-A1B2C3D4E5F6}}_is1",
        L"{8F3C2A14-7B1D-4A2E-9C77-A1B2C3D4E5F6}_is1"
    };
    std::error_code ec;
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        for (const wchar_t* id : ids) {
            std::wstring loc = reg_str(root, (std::wstring(base) + id).c_str(), L"InstallLocation");
            if (!loc.empty()) {
                fs::path p = fs::path(loc) / L"Obelisk.exe";
                if (fs::exists(p, ec)) return p;
            }
        }
    }
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::vector<fs::path> cands = { fs::path(exe).parent_path() / L"Obelisk.exe" };
    if (const char* lad = std::getenv("LOCALAPPDATA"))
        cands.push_back(fs::path(lad) / "Programs" / "SAT-Obelisk" / "Obelisk.exe");
    cands.push_back(L"C:/Program Files/SAT-Obelisk/Obelisk.exe");
    cands.push_back(L"C:/Program Files (x86)/SAT-Obelisk/Obelisk.exe");
    for (const auto& c : cands)
        if (fs::exists(c, ec)) return c;
#else
    std::error_code ec;
    for (const char* c : { "/usr/local/bin/obelisk", "/usr/bin/obelisk" })
        if (fs::exists(c, ec)) return c;
#endif
    return {};
}

inline bool launch_obelisk(const fs::path& obelisk, const fs::path& target) {
#ifdef _WIN32
    std::wstring args = L"\"" + target.wstring() + L"\"";
    HINSTANCE r = ShellExecuteW(nullptr, L"open", obelisk.wstring().c_str(),
                                args.c_str(), nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
#else
    std::string c = "\"" + obelisk.string() + "\" \"" + target.string() + "\" &";
    return system(c.c_str()) == 0;
#endif
}

}
