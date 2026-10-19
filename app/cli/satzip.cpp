#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  #include <shlobj.h>
#endif

#include "satzip_ops.hpp"
#include "satzip_zip.hpp"
#include "satzip_7z.hpp"
#include "satzip_7z_write.hpp"
#include "satzip_tar.hpp"
#include "satzip_xz.hpp"
#include "satzip_version.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

using namespace sz;

static int cmd_compress(const char* in, const char* out) {
    std::error_code ec;
    auto n = fs::file_size(in, ec);
    if (ec) { std::fprintf(stderr, "satzip: cannot open %s\n", in); return 1; }
    auto data = read_file(in, n, SZ_MARGIN);
    std::vector<uint8_t> obuf(sz_stream_bound(n, 16));
    sz_state_t st{}; sz_state_reset(&st);
    size_t c = sz_stream_compress(&st, data.data(), n, obuf.data(), obuf.size(), 16, SZ_ACCEL_MAX, SZ_MODE_R);
    if (!c || !write_file_atomic(out, obuf.data(), c)) { std::fprintf(stderr, "satzip: compress failed\n"); return 1; }
    std::printf("compressed %ju -> %zu bytes (x%.2f)\n", (uintmax_t)n, c, n ? (double)n / c : 0);
    return 0;
}
static int cmd_decompress(const char* in, const char* out) {
    std::error_code ec;
    auto n = fs::file_size(in, ec);
    if (ec) { std::fprintf(stderr, "satzip: cannot open %s\n", in); return 1; }
    auto data = read_file(in, n, 0);
    uint64_t total; int blog;
    if (sz_stream_header(data.data(), n, &total, &blog) != 0) {
        std::fprintf(stderr, "satzip: %s is not a SAT-ZIP stream\n", in); return 1;
    }
    std::vector<uint8_t> obuf((size_t)total + SZ_MARGIN);
    size_t d = sz_stream_decompress(data.data(), n, obuf.data(), (size_t)total);
    if (d == SZ_ERR || !write_file(out, obuf.data(), d)) {
        std::fprintf(stderr, "satzip: corrupt or truncated stream\n"); return 1;
    }
    std::printf("decompressed %ju -> %zu bytes\n", (uintmax_t)n, d);
    return 0;
}

static int cmd_add(const char* archive, int npaths, char** paths) {
    std::vector<Item> items;
    for (int i = 0; i < npaths; ++i) gather(paths[i], items);
    std::vector<uint8_t> out;
    std::string ext = fs::path(archive).extension().string();
    size_t total;
#if defined(SATZIP_HAVE_ZIP)
    if (ext == ".zip") total = build_zip(items, out, 9, false, 0);
    else
#endif
#if defined(SATZIP_HAVE_7Z)
    if (ext == ".7z") total = sevenz_write(items, out, 9);
    else if (ext == ".xz") total = build_tar_xz(items, out, 9);
    else
#endif
    if (ext == ".tar") total = build_tar(items, out);
    else total = build_archive(items, out, SZ_ACCEL_MAX, 0, false, SZ_MODE_R);
    if (!total || !write_file_atomic(archive, out.data(), total)) {
        std::fprintf(stderr, "satzip: archive failed\n"); return 1;
    }
    size_t files = 0; uintmax_t raw = 0;
    for (auto& it : items) if (!it.is_dir) { ++files; raw += it.size; }
    std::printf("archived %zu file(s), %ju -> %zu bytes (x%.2f)\n",
                files, (uintmax_t)raw, total, raw ? (double)raw / total : 0);
    return 0;
}
static int cmd_extract(const char* archive, const char* destdir) {
#if defined(SATZIP_HAVE_ZIP)
    if (is_zip_file(archive)) {
        fs::path root = destdir ? destdir : ".";
        int bad = 0;
        int files = zip_extract(archive, root, nullptr, 0, {}, &bad);
        std::printf("extracted %d file(s) from zip to %s%s\n", files, root.string().c_str(),
                    bad ? " (some failed)" : "");
        return bad ? 1 : 0;
    }
#endif
#if defined(SATZIP_HAVE_7Z)
    if (is_7z_file(archive)) {
        fs::path root = destdir ? destdir : ".";
        int bad = 0;
        int files = sevenz_extract(archive, root, nullptr, {}, &bad);
        std::printf("extracted %d file(s) from 7z to %s%s\n", files, root.string().c_str(),
                    bad ? " (some failed)" : "");
        return bad ? 1 : 0;
    }
#endif
    {
        bool xzf = false;
#if defined(SATZIP_HAVE_7Z)
        xzf = is_xz_file(archive);
#endif
        if (xzf || fs::path(archive).extension() == ".tar") {
            std::error_code ec;
            auto fsz = fs::file_size(archive, ec);
            if (ec) { std::fprintf(stderr, "satzip: cannot open %s\n", archive); return 1; }
            auto raw = read_file(archive, (size_t)fsz, 0);
            std::vector<uint8_t> tb;
#if defined(SATZIP_HAVE_7Z)
            if (is_xz_data(raw.data(), raw.size())) {
                if (!xz_decompress(raw.data(), raw.size(), tb)) { std::fprintf(stderr, "satzip: corrupt xz\n"); return 1; }
            } else
#endif
            tb = std::move(raw);
            std::vector<TarEntry> tes;
            if (!tar_list(tb.data(), tb.size(), tes)) { std::fprintf(stderr, "satzip: not a tar\n"); return 1; }
            fs::path root = destdir ? destdir : ".";
            int bad = 0;
            int files = tar_extract(tb.data(), tb.size(), tes, root, nullptr, {}, &bad);
            std::printf("extracted %d file(s) from tar to %s%s\n", files, root.string().c_str(),
                        bad ? " (some failed)" : "");
            return bad ? 1 : 0;
        }
    }
    Archive a;
    if (!open_archive(archive, a)) { std::fprintf(stderr, "satzip: %s is not a SAT-ZIP archive\n", archive); return 1; }
    fs::path root = destdir ? destdir : ".";
    int bad = 0;
    int files = extract(a, root, nullptr, 0, {}, &bad);
    std::printf("extracted %d file(s) to %s%s\n", files, root.string().c_str(), bad ? " (some failed)" : "");
    return bad ? 1 : 0;
}
static int cmd_list(const char* archive) {
    {
        bool xzf = false;
#if defined(SATZIP_HAVE_7Z)
        xzf = is_xz_file(archive);
#endif
        if (xzf || fs::path(archive).extension() == ".tar") {
            std::error_code ec;
            auto fsz = fs::file_size(archive, ec);
            if (ec) { std::fprintf(stderr, "satzip: cannot open %s\n", archive); return 1; }
            auto raw = read_file(archive, (size_t)fsz, 0);
            std::vector<uint8_t> tb;
#if defined(SATZIP_HAVE_7Z)
            if (is_xz_data(raw.data(), raw.size())) {
                if (!xz_decompress(raw.data(), raw.size(), tb)) { std::fprintf(stderr, "satzip: corrupt xz\n"); return 1; }
            } else
#endif
            tb = std::move(raw);
            std::vector<TarEntry> tes;
            if (!tar_list(tb.data(), tb.size(), tes)) { std::fprintf(stderr, "satzip: not a tar\n"); return 1; }
            std::printf("%12s  %s\n", "orig", "name");
            uint64_t to = 0;
            for (auto& e : tes) {
                if (e.is_dir) { std::printf("%12s  %s/\n", "-", e.name.c_str()); continue; }
                std::printf("%12ju  %s\n", (uintmax_t)e.size, e.name.c_str());
                to += e.size;
            }
            std::printf("%12ju  total\n", (uintmax_t)to);
            return 0;
        }
    }
#if defined(SATZIP_HAVE_7Z)
    if (is_7z_file(archive)) {
        std::vector<SevenZEntry> ses;
        if (!sevenz_list(archive, ses)) { std::fprintf(stderr, "satzip: cannot read 7z %s\n", archive); return 1; }
        std::printf("%12s  %s\n", "orig", "name");
        uint64_t to = 0;
        for (auto& s : ses) {
            if (s.is_dir) { std::printf("%12s  %s/\n", "-", s.name.c_str()); continue; }
            std::printf("%12ju  %s\n", (uintmax_t)s.orig, s.name.c_str());
            to += s.orig;
        }
        std::printf("%12ju  total\n", (uintmax_t)to);
        return 0;
    }
#endif
#if defined(SATZIP_HAVE_ZIP)
    if (is_zip_file(archive)) {
        std::vector<ZipEntry> zes;
        if (!zip_list(archive, zes)) { std::fprintf(stderr, "satzip: cannot read zip %s\n", archive); return 1; }
        std::printf("%12s %12s  %s\n", "orig", "stored", "name");
        uint64_t to = 0, ts = 0;
        for (auto& z : zes) {
            if (z.is_dir) { std::printf("%12s %12s  %s/\n", "-", "-", z.name.c_str()); continue; }
            std::printf("%12ju %12ju  %s\n", (uintmax_t)z.orig, (uintmax_t)z.stored, z.name.c_str());
            to += z.orig; ts += z.stored;
        }
        std::printf("%12ju %12ju  total (x%.2f)\n", (uintmax_t)to, (uintmax_t)ts, ts ? (double)to / ts : 0);
        return 0;
    }
#endif
    Archive a;
    if (!open_archive(archive, a)) { std::fprintf(stderr, "satzip: %s is not a SAT-ZIP archive\n", archive); return 1; }
    std::printf("%12s %12s  %-6s  %s\n", "orig", "stored", "method", "name");
    uint64_t to = 0, ts = 0;
    for (uint32_t i = 0; i < a.count; ++i) {
        const sz_ar_entry& e = a.entries[i];
        if (e.is_dir) { std::printf("%12s %12s  %-6s  %s/\n", "-", "-", "dir", e.name); continue; }
        std::printf("%12ju %12ju  %-6s  %s\n", (uintmax_t)e.orig_size, (uintmax_t)e.stored_size,
                    e.method ? "satzip" : "stored", e.name);
        to += e.orig_size; ts += e.stored_size;
    }
    std::printf("%12ju %12ju  total (x%.2f)\n", (uintmax_t)to, (uintmax_t)ts, ts ? (double)to / ts : 0);
    return 0;
}

static int cmd_add_auto(const char* path) {
    std::string arc = std::string(path) + ".satz";
    char* p[1] = { const_cast<char*>(path) };
    return cmd_add(arc.c_str(), 1, p);
}
static int cmd_extract_to(const char* archive, bool subfolder) {
    fs::path a(archive);
    fs::path dest = a.parent_path();
    if (subfolder) dest /= a.stem();
    std::string ds = dest.empty() ? std::string(".") : dest.string();
    return cmd_extract(archive, ds.c_str());
}

#ifdef _WIN32
static const wchar_t* PROGID = L"SATZIP.Archive";
static bool reg_put(HKEY root, const std::wstring& sub, const wchar_t* name, const std::wstring& val) {
    HKEY k;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()),
                            (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
static int win_register(bool install) {
    wchar_t buf[MAX_PATH]; DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring exe(buf, n);
    std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/") + 1);
    std::wstring gui = L"\"" + dir + L"satzip_gui.exe\"";
    const std::wstring C = L"Software\\Classes\\";

    if (install) {
        reg_put(HKEY_CURRENT_USER, C + L".satz", nullptr, PROGID);
        reg_put(HKEY_CURRENT_USER, C + PROGID, nullptr, L"SAT-ZIP Archive");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\DefaultIcon", nullptr, exe + L",0");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\open\\command", nullptr, gui + L" --xto \"%1\"");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\satzip_here", nullptr, L"Extract Here");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\satzip_here", L"Icon", exe + L",0");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\satzip_here\\command", nullptr, gui + L" --xhere \"%1\"");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\satzip_to", nullptr, L"Extract to folder");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\satzip_to\\command", nullptr, gui + L" --xto \"%1\"");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\satzip_view", nullptr, L"Open in SAT-ZIP");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\satzip_view", L"Icon", exe + L",0");
        reg_put(HKEY_CURRENT_USER, C + PROGID + L"\\shell\\satzip_view\\command", nullptr, gui + L" \"%1\"");
        const wchar_t* bases[] = { L"*", L"Directory" };
        for (auto base : bases) {
            RegDeleteTreeW(HKEY_CURRENT_USER, (C + base + L"\\shell\\SATZIP.Add").c_str());
            RegDeleteTreeW(HKEY_CURRENT_USER, (C + base + L"\\shell\\SATZIP.Zip").c_str());
            std::wstring kb = C + base + L"\\shell\\SATZIP";
            reg_put(HKEY_CURRENT_USER, kb, L"MUIVerb", L"SAT-ZIP");
            reg_put(HKEY_CURRENT_USER, kb, L"Icon", exe + L",0");
            reg_put(HKEY_CURRENT_USER, kb, L"SubCommands", L"");
            reg_put(HKEY_CURRENT_USER, kb + L"\\shell\\010add", L"MUIVerb", L"Add to archive");
            reg_put(HKEY_CURRENT_USER, kb + L"\\shell\\010add\\command", nullptr, gui + L" \"%1\"");
            reg_put(HKEY_CURRENT_USER, kb + L"\\shell\\020zip", L"MUIVerb", L"Zip now");
            reg_put(HKEY_CURRENT_USER, kb + L"\\shell\\020zip\\command", nullptr, gui + L" --zip \"%1\"");
            reg_put(HKEY_CURRENT_USER, kb + L"\\shell\\030zipmail", L"MUIVerb", L"Zip now && email");
            reg_put(HKEY_CURRENT_USER, kb + L"\\shell\\030zipmail\\command", nullptr, gui + L" --zipmail \"%1\"");
        }
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"SystemFileAssociations\\.zip\\shell\\SATZIP.xhere").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"SystemFileAssociations\\.zip\\shell\\SATZIP.xto").c_str());
        const wchar_t* exts[] = { L".zip", L".7z", L".tar", L".xz" };
        for (auto ext : exts) {
            std::wstring zk = C + L"SystemFileAssociations\\" + ext + L"\\shell\\SATZIP";
            reg_put(HKEY_CURRENT_USER, zk, L"MUIVerb", L"SAT-ZIP");
            reg_put(HKEY_CURRENT_USER, zk, L"Icon", exe + L",0");
            reg_put(HKEY_CURRENT_USER, zk, L"SubCommands", L"");
            reg_put(HKEY_CURRENT_USER, zk + L"\\shell\\005open", L"MUIVerb", L"Open in SAT-ZIP");
            reg_put(HKEY_CURRENT_USER, zk + L"\\shell\\005open\\command", nullptr, gui + L" \"%1\"");
            reg_put(HKEY_CURRENT_USER, zk + L"\\shell\\010xhere", L"MUIVerb", L"Extract Here");
            reg_put(HKEY_CURRENT_USER, zk + L"\\shell\\010xhere\\command", nullptr, gui + L" --xhere \"%1\"");
            reg_put(HKEY_CURRENT_USER, zk + L"\\shell\\020xto", L"MUIVerb", L"Extract to folder");
            reg_put(HKEY_CURRENT_USER, zk + L"\\shell\\020xto\\command", nullptr, gui + L" --xto \"%1\"");
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        std::printf("SAT-ZIP shell integration registered (per-user).\n");
    } else {
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L".satz").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + PROGID).c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"*\\shell\\SATZIP.Add").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"Directory\\shell\\SATZIP.Add").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"*\\shell\\SATZIP.Zip").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"Directory\\shell\\SATZIP.Zip").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"*\\shell\\SATZIP").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"Directory\\shell\\SATZIP").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"SystemFileAssociations\\.zip\\shell\\SATZIP.xhere").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"SystemFileAssociations\\.zip\\shell\\SATZIP.xto").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"SystemFileAssociations\\.zip\\shell\\SATZIP").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"SystemFileAssociations\\.7z\\shell\\SATZIP").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"SystemFileAssociations\\.tar\\shell\\SATZIP").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (C + L"SystemFileAssociations\\.xz\\shell\\SATZIP").c_str());
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        std::printf("SAT-ZIP shell integration removed.\n");
    }
    return 0;
}
#else
static int win_register(bool) {
    std::fprintf(stderr, "satzip: shell integration is Windows-only\n");
    return 2;
}
#endif

static int usage() {
    std::fprintf(stderr,
        "SAT-ZIP %s (codec: %s)\n"
        "  satzip a <archive> <paths...>  create an archive from files/folders\n"
        "  satzip x <archive> [destdir]   extract an archive\n"
        "  satzip l <archive>             list archive contents\n"
        "  satzip c <in> <out>            compress a single file (.satz stream)\n"
        "  satzip d <in> <out>            decompress a single .satz stream\n"
        "  satzip --register              install Windows Explorer integration (per-user)\n"
        "  satzip --unregister            remove it\n", SATZIP_VERSION, sz_cpu_path());
    return 2;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    if (cmd == "a")            return argc >= 4 ? cmd_add(argv[2], argc - 3, argv + 3) : usage();
    if (cmd == "x")            return argc >= 3 ? cmd_extract(argv[2], argc >= 4 ? argv[3] : nullptr) : usage();
    if (cmd == "l")            return argc >= 3 ? cmd_list(argv[2]) : usage();
    if (cmd == "c")            return argc == 4 ? cmd_compress(argv[2], argv[3]) : usage();
    if (cmd == "d")            return argc == 4 ? cmd_decompress(argv[2], argv[3]) : usage();
    if (cmd == "aauto")        return argc >= 3 ? cmd_add_auto(argv[2]) : usage();
    if (cmd == "xhere")        return argc >= 3 ? cmd_extract_to(argv[2], false) : usage();
    if (cmd == "xto")          return argc >= 3 ? cmd_extract_to(argv[2], true) : usage();
    if (cmd == "--register")   return win_register(true);
    if (cmd == "--unregister") return win_register(false);
    return usage();
}
