#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  #include <commdlg.h>
  #include <shlobj.h>
  #include <shellapi.h>
#endif

#include "satzip_bench.h"
#include "satzip_tans.h"
#include "satzip_ops.hpp"
#include "satzip_zip.hpp"
#include "satzip_7z.hpp"
#include "satzip_7z_write.hpp"
#include "satzip_tar.hpp"
#include "satzip_xz.hpp"
#if defined(SATZIP_HAVE_7Z)
#include "LzmaEnc.h"
#include "LzmaDec.h"
#endif
#include "satzip_obelisk.hpp"
#include "satzip_mail.hpp"
#ifdef _WIN32
#include "satzip_share.hpp"
#endif
#include "satzip_settings.hpp"
#include "satzip_version.h"
#if defined(HAVE_LZ4)
  #include "lz4.h"
#endif

#include "imgui.h"
#include "implot.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
  #define GLFW_EXPOSE_NATIVE_WIN32
  #include <GLFW/glfw3native.h>
#endif

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fs = std::filesystem;

static ImFont* g_font_bold = nullptr;
static bool    g_icons = false;

#define SZ_ICON_MAIL   "\xEE\x9C\x95"
#define SZ_ICON_FILTER "\xEE\x9C\x9C"

#ifdef _WIN32
static bool dlg_open(std::string& out, bool satz) {
    char buf[2048] = {0};
    OPENFILENAMEA o; ZeroMemory(&o, sizeof(o));
    o.lStructSize = sizeof(o); o.lpstrFile = buf; o.nMaxFile = sizeof(buf);
    o.lpstrFilter = satz ? "Archives (*.satz;*.zip;*.7z;*.tar;*.xz)\0*.satz;*.zip;*.7z;*.tar;*.xz;*.tar.xz\0All Files\0*.*\0" : "All Files\0*.*\0";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&o)) return false;
    out = buf; return true;
}
static bool dlg_save(std::string& out, const std::string& suggest, int def_format) {
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
static bool dlg_folder(std::string& out) {
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
static bool dlg_open(std::string& out, bool) { return dlg_popen(out, "zenity --file-selection 2>/dev/null"); }
static bool dlg_save(std::string& out, const std::string&, int) { return dlg_popen(out, "zenity --file-selection --save --confirm-overwrite 2>/dev/null"); }
static bool dlg_folder(std::string& out) { return dlg_popen(out, "zenity --file-selection --directory 2>/dev/null"); }
#endif

static void open_url(const char* url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string c = std::string("open \"") + url + "\""; (void)system(c.c_str());
#else
    std::string c = std::string("xdg-open \"") + url + "\" >/dev/null 2>&1 &"; (void)system(c.c_str());
#endif
}

static void open_in_explorer(const fs::path& p) {
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

static void set_window_icon(GLFWwindow* win) {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(win);
    if (!hwnd) return;
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    HICON hBig = nullptr, hSmall = nullptr;
    if (ExtractIconExW(exe, 0, &hBig, &hSmall, 1)) {
        if (hBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hBig);
        if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);
    }
#else
    (void)win;
#endif
}

enum class Mode { Empty, Building, Viewing };

struct Series { std::vector<float> x, y; void clear() { x.clear(); y.clear(); } };
struct CodecView { const char* name = ""; Series comp, decomp; sz_bench_result result{}; bool done = false; };

enum class AKind { Satz, Zip, SevenZ, Tar };
struct ViewEntry { std::string name; bool is_dir; uint64_t orig, stored; uint32_t idx; };

struct App {
    GLFWwindow* win = nullptr;
    std::mutex  mtx;
    std::atomic<bool> running{false};
    std::atomic<size_t> prog_done{0}, prog_total{0};
    std::thread worker;
    std::string status = "Drop files or a .satz/.zip archive here, or use the buttons above.";

    Mode mode = Mode::Empty;
    std::vector<sz::Item> staged;
    std::string suggest_name;
    sz::Archive opened;
    AKind okind = AKind::Satz;
    std::string opened_path;
    std::vector<uint8_t> tarbuf;
    std::vector<sz::TarEntry> tentries;
    std::vector<ViewEntry> ventries;
    std::vector<char> selected;
    std::vector<uint32_t> view_order;
    std::atomic<bool> cancel{false};
    std::atomic<bool> share_pending{false};
    std::string share_path;
    char search[256] = {0};
    std::string filter_applied = "\x01";
    float table_h = 360.0f;
    fs::path obelisk = sz::find_obelisk();
    bool obelisk_after = false;
    bool mail_after = false;
    sz::Settings settings{};

    std::vector<uint8_t> bench_data;
    std::string bench_source = "(none)";
    CodecView sat_s, sat_r, sat_t, lz4, zipdef, lzma;
    bool show_s = true, show_r = true, show_t = true, show_lz4 = true, show_zip = true, show_lzma = true;
    bool skip_hidden = false;
    int block_kib = 64, runs = 25, bench_level = 9, bench_threads = 0;
    sz::Settings settings_saved{};

    ~App() { if (worker.joinable()) worker.join(); }
};

static void join_worker(App* a) { if (a->worker.joinable()) a->worker.join(); }
static void launch(App* a, std::thread&& t) { join_worker(a); a->cancel = false; a->worker = std::move(t); }

static bool load_archive_into(App* a, const std::string& path);

static std::string failed_summary(const std::vector<std::string>& names) {
    std::string s;
    size_t show = names.size() < 15 ? names.size() : 15;
    for (size_t i = 0; i < show; ++i) { s += "\n  "; s += names[i]; }
    if (names.size() > show) s += "\n  ...and " + std::to_string(names.size() - show) + " more";
    return s;
}

static void show_failed_dialog(const std::vector<std::string>& names) {
    if (names.empty()) return;
#ifdef _WIN32
    std::string m = std::to_string(names.size()) + " entr" + (names.size() == 1 ? "y" : "ies") +
                    " could not be extracted:" + failed_summary(names);
    MessageBoxA(nullptr, m.c_str(), "SAT-ZIP", MB_ICONWARNING);
#endif
}

static void do_open(App* a, std::string path) {
    a->running = true;
    { std::lock_guard<std::mutex> lk(a->mtx);
      a->status = "Opening " + fs::path(path).filename().string() + "..."; }
    load_archive_into(a, path);
    a->running = false;
}

static bool load_archive_into(App* a, const std::string& path) {
    bool ok = false;
    AKind kind = AKind::Satz;
    std::vector<ViewEntry> ve;
    sz::Archive tmp;
#if defined(SATZIP_HAVE_ZIP)
    if (sz::is_zip_file(path)) {
        std::vector<sz::ZipEntry> zes;
        if (sz::zip_list(path, zes)) {
            for (auto& z : zes) ve.push_back({ z.name, z.is_dir, z.orig, z.stored, z.idx });
            kind = AKind::Zip; ok = true;
        }
    } else
#endif
#if defined(SATZIP_HAVE_7Z)
    if (sz::is_7z_file(path)) {
        std::vector<sz::SevenZEntry> ses;
        if (sz::sevenz_list(path, ses)) {
            for (auto& s : ses) ve.push_back({ s.name, s.is_dir, s.orig, 0, s.idx });
            kind = AKind::SevenZ; ok = true;
        }
    } else
#endif
    if (
#if defined(SATZIP_HAVE_7Z)
        sz::is_xz_file(path) ||
#endif
        fs::path(path).extension() == ".tar") {
        std::error_code ec;
        auto fsz = fs::file_size(path, ec);
        if (!ec) {
            auto raw = sz::read_file(path, (size_t)fsz, 0);
            std::vector<uint8_t> tb;
#if defined(SATZIP_HAVE_7Z)
            if (sz::is_xz_data(raw.data(), raw.size())) {
                if (!sz::xz_decompress(raw.data(), raw.size(), tb)) tb.clear();
            } else
#endif
            tb = std::move(raw);
            std::vector<sz::TarEntry> tes;
            if (!tb.empty() && sz::tar_list(tb.data(), tb.size(), tes)) {
                for (uint32_t i = 0; i < tes.size(); ++i)
                    ve.push_back({ tes[i].name, tes[i].is_dir, tes[i].size, 0, i });
                std::lock_guard<std::mutex> lk(a->mtx);
                a->tarbuf = std::move(tb);
                a->tentries = std::move(tes);
                kind = AKind::Tar; ok = true;
            }
        }
    } else
    if (sz::open_archive(path, tmp)) {
        for (uint32_t i = 0; i < tmp.count; ++i) {
            const sz_ar_entry& e = tmp.entries[i];
            ve.push_back({ e.name, e.is_dir != 0, e.orig_size, e.stored_size, i });
        }
        kind = AKind::Satz; ok = true;
    }

    { std::lock_guard<std::mutex> lk(a->mtx);
      if (ok) {
          a->opened.close();
          if (kind == AKind::Satz) {
              a->opened.buf = std::move(tmp.buf);
              a->opened.entries = tmp.entries; a->opened.count = tmp.count;
              tmp.entries = nullptr; tmp.count = 0;
          }
          a->okind = kind;
          a->ventries = std::move(ve);
          a->selected.assign(a->ventries.size(), 0);
          a->view_order.clear();
          a->filter_applied = "\x01";
          a->opened_path = path;
          a->mode = Mode::Viewing;
          a->status = "Opened " + fs::path(path).filename().string() +
                      " (" + std::to_string(a->ventries.size()) + " entries)";
      } else {
          a->status = "Could not open " + fs::path(path).filename().string();
      } }
    return ok;
}

static bool is_archive_path(const fs::path& p) {
    if (!fs::is_regular_file(p)) return false;
    if (p.extension() == ".satz") return true;
    if (p.extension() == ".tar") return true;
#if defined(SATZIP_HAVE_ZIP)
    if (p.extension() == ".zip" && sz::is_zip_file(p)) return true;
#endif
#if defined(SATZIP_HAVE_7Z)
    if (p.extension() == ".7z" && sz::is_7z_file(p)) return true;
    if (p.extension() == ".xz" && sz::is_xz_file(p)) return true;
#endif
    return false;
}

static void stage_paths(App* a, const std::vector<std::string>& paths) {
    if (a->running) return;
    if (paths.size() == 1 && is_archive_path(fs::path(paths[0]))) {
        launch(a, std::thread(do_open, a, paths[0]));
        return;
    }
    std::lock_guard<std::mutex> lk(a->mtx);
    if (a->mode != Mode::Building) { a->staged.clear(); a->suggest_name.clear(); a->mode = Mode::Building; }
    for (const auto& s : paths) sz::gather(fs::path(s), a->staged);
    if (a->suggest_name.empty() && !paths.empty()) {
        fs::path first(paths[0]);
        a->suggest_name = first.stem().string();
        if (a->suggest_name.empty()) a->suggest_name = first.filename().string();
    }
    a->status = std::to_string(a->staged.size()) + " item(s) staged";
}

static void do_create(App* a, std::string outpath) {
    a->running = true;
    std::vector<sz::Item> items; int level, threads, mode; bool all;
    { std::lock_guard<std::mutex> lk(a->mtx);
      items = a->staged; level = a->settings.level; threads = a->settings.threads;
      all = a->settings.compress_all; mode = a->settings.mode; }
    int accel = 10 - level; if (accel < 1) accel = 1;
    a->prog_done = 0; a->prog_total = items.size() + 1;
    { std::lock_guard<std::mutex> lk(a->mtx);
      a->status = "Creating " + fs::path(outpath).filename().string() + "..."; }
    std::vector<uint8_t> out;
    size_t n = 0;
    std::string ext = fs::path(outpath).extension().string();
    auto prog = [a](size_t d, size_t t) { a->prog_done = d; a->prog_total = t + 1; };
#if defined(SATZIP_HAVE_ZIP)
    if (ext == ".zip") n = sz::build_zip(items, out, level, all, threads, prog, &a->cancel);
    else
#endif
#if defined(SATZIP_HAVE_7Z)
    if (ext == ".7z") n = sz::sevenz_write(items, out, level, prog);
    else if (ext == ".xz") n = sz::build_tar_xz(items, out, level, prog);
    else
#endif
    if (ext == ".tar") n = sz::build_tar(items, out, prog);
    else n = sz::build_archive(items, out, accel, threads, all, mode, prog, &a->cancel);
    if (a->cancel) {
        std::lock_guard<std::mutex> lk(a->mtx);
        a->status = "Cancelled.";
        a->running = false;
        return;
    }
    { std::lock_guard<std::mutex> lk(a->mtx);
      a->status = "Writing " + fs::path(outpath).filename().string() + "..."; }
    bool ok = n && sz::write_file(outpath, out.data(), n);
    a->prog_done = a->prog_total.load();
    uintmax_t raw = 0; for (auto& it : items) if (!it.is_dir) raw += it.size;
    bool enc = false, mail = false;
    { std::lock_guard<std::mutex> lk(a->mtx);
      enc  = ok && a->obelisk_after && !a->obelisk.empty();
      mail = ok && a->mail_after;
      a->status = ok ? ("Created " + fs::path(outpath).filename().string() + "  " +
                        std::to_string(raw) + " -> " + std::to_string(n) + " bytes" +
                        (enc ? "  (opening SAT-OBELISK to encrypt...)" : "") +
                        (mail ? "  (opening email...)" : ""))
                     : "Create failed"; }
    if (enc) sz::launch_obelisk(a->obelisk, fs::path(outpath));
    if (mail) {
        if (!sz::mail_try_direct(fs::path(outpath))) {
            std::lock_guard<std::mutex> lk(a->mtx);
            a->share_path = outpath;
            a->share_pending = true;
        }
    }
    a->running = false;
}

static void do_extract(App* a, std::string destdir, bool selected_only) {
    a->running = true;
    std::vector<int> sel; int threads; AKind kind; std::string apath;
    { std::lock_guard<std::mutex> lk(a->mtx);
      threads = a->settings.threads; kind = a->okind; apath = a->opened_path;
      if (selected_only)
          for (size_t i = 0; i < a->ventries.size(); ++i)
              if (a->selected[i]) sel.push_back((int)a->ventries[i].idx); }
    a->prog_done = 0; a->prog_total = selected_only ? sel.size() : a->ventries.size();
    int bad = 0, files = 0;
    std::vector<std::string> failed;
#if defined(SATZIP_HAVE_ZIP)
    if (kind == AKind::Zip) {
        files = sz::zip_extract(fs::path(apath), fs::path(destdir), selected_only ? &sel : nullptr, threads,
            [a](size_t d, size_t t) { a->prog_done = d; a->prog_total = t; }, &bad, &a->cancel, &failed);
    } else
#endif
#if defined(SATZIP_HAVE_7Z)
    if (kind == AKind::SevenZ) {
        files = sz::sevenz_extract(fs::path(apath), fs::path(destdir), selected_only ? &sel : nullptr,
            [a](size_t d, size_t t) { a->prog_done = d; a->prog_total = t; }, &bad, &failed);
    } else
#endif
    if (kind == AKind::Tar) {
        files = sz::tar_extract(a->tarbuf.data(), a->tarbuf.size(), a->tentries,
            fs::path(destdir), selected_only ? &sel : nullptr,
            [a](size_t d, size_t t) { a->prog_done = d; a->prog_total = t; }, &bad, &failed);
    } else
    {
        files = sz::extract(a->opened, fs::path(destdir), selected_only ? &sel : nullptr, threads,
            [a](size_t d, size_t t) { a->prog_done = d; a->prog_total = t; }, &bad, &a->cancel, &failed);
    }
    { std::lock_guard<std::mutex> lk(a->mtx);
      a->status = a->cancel ? "Cancelled." :
                  ("Extracted " + std::to_string(files) + " file(s) to " +
                   fs::path(destdir).filename().string() + (bad ? ("  (" + std::to_string(bad) + " failed)") : "")); }
    if (!a->cancel) show_failed_dialog(failed);
    a->running = false;
}

static void do_open_entry(App* a, uint32_t vidx) {
    a->running = true;
    AKind kind; std::string apath, name; std::vector<int> sel;
    { std::lock_guard<std::mutex> lk(a->mtx);
      if (vidx >= a->ventries.size()) { a->running = false; return; }
      kind = a->okind; apath = a->opened_path;
      name = a->ventries[vidx].name;
      sel.push_back((int)a->ventries[vidx].idx); }

    fs::path temp = fs::temp_directory_path() / "SAT-ZIP" / fs::path(apath).stem();
    std::error_code ec;
    fs::create_directories(temp, ec);
    int bad = 0;
#if defined(SATZIP_HAVE_ZIP)
    if (kind == AKind::Zip) sz::zip_extract(fs::path(apath), temp, &sel, 1, {}, &bad);
    else
#endif
#if defined(SATZIP_HAVE_7Z)
    if (kind == AKind::SevenZ) sz::sevenz_extract(fs::path(apath), temp, &sel, {}, &bad);
    else
#endif
    if (kind == AKind::Tar) sz::tar_extract(a->tarbuf.data(), a->tarbuf.size(), a->tentries, temp, &sel, {}, &bad);
    else sz::extract(a->opened, temp, &sel, 1, {}, &bad);

    fs::path file = temp / fs::path(name).lexically_normal();
    bool ok = !bad && fs::exists(file, ec);
    if (ok) {
#ifdef _WIN32
        ShellExecuteW(nullptr, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
        std::string c = "open \"" + file.string() + "\""; (void)system(c.c_str());
#else
        std::string c = "xdg-open \"" + file.string() + "\" >/dev/null 2>&1 &"; (void)system(c.c_str());
#endif
    }
    { std::lock_guard<std::mutex> lk(a->mtx);
      a->status = ok ? ("Opened " + name + " (temporary copy)") : ("Could not open " + name); }
    a->running = false;
}

static void do_edit(App* a, std::vector<std::string> add_paths, bool remove_selected) {
    a->running = true;
    AKind kind; std::string apath; int level, threads, cmode; bool call;
    std::vector<char> removed;
    { std::lock_guard<std::mutex> lk(a->mtx);
      kind = a->okind; apath = a->opened_path;
      level = a->settings.level; threads = a->settings.threads;
      call = a->settings.compress_all; cmode = a->settings.mode;
      removed.assign(a->opened.count ? a->opened.count : a->ventries.size(), 0);
      if (remove_selected)
          for (size_t i = 0; i < a->ventries.size(); ++i)
              if (a->selected[i]) removed[a->ventries[i].idx] = 1; }
    int accel = 10 - level; if (accel < 1) accel = 1;

    std::vector<sz::Item> newitems;
    for (const auto& p : add_paths) sz::gather(fs::path(p), newitems);

    bool ok = false;
    a->prog_done = 0; a->prog_total = 1;

    if (kind == AKind::Satz) {
        size_t bound = SZ_AR_HEADER + SZ_AR_END + SZ_MARGIN;
        for (uint32_t e = 0; e < a->opened.count; ++e)
            if (!removed[e]) bound += SZ_AR_DIR_FIXED + strlen(a->opened.entries[e].name) +
                                      (size_t)a->opened.entries[e].stored_size;
        for (auto& it : newitems) bound += SZ_AR_DIR_FIXED + it.name.size() + sz_stream_bound((size_t)it.size, 16);

        std::vector<uint8_t> out(bound);
        sz_archive_writer w;
        if (sz_aw_init(&w, out.data(), out.size()) == 0) {
            ok = true;
            size_t total_steps = a->opened.count + newitems.size(), step = 0;
            for (uint32_t e = 0; e < a->opened.count && ok; ++e) {
                const sz_ar_entry& en = a->opened.entries[e];
                if (!removed[e])
                    ok = sz_aw_add_blob(&w, en.name, en.is_dir, en.method, en.crc, en.orig_size,
                                        a->opened.buf.data() + en.offset, (size_t)en.stored_size) == 0;
                a->prog_done = ++step; a->prog_total = total_steps;
            }
            static thread_local std::vector<uint8_t> dummy;
            sz_state_t* st = (sz_state_t*)calloc(1, sizeof(sz_state_t));
            for (auto& it : newitems) {
                if (!ok || !st) { ok = false; break; }
                if (it.is_dir) ok = sz_aw_add_blob(&w, it.name.c_str(), 1, 0, 0, 0, nullptr, 0) == 0;
                else {
                    auto data = sz::read_file(it.abs, (size_t)it.size, SZ_MARGIN);
                    ok = sz_aw_add(&w, it.name.c_str(), 0, data.data(), (size_t)it.size, st, accel, cmode) == 0;
                }
                a->prog_done = ++step;
            }
            free(st);
            size_t n = sz_aw_finish(&w);
            ok = ok && n && sz::write_file(apath, out.data(), n);
        }
    }
#if defined(SATZIP_HAVE_ZIP)
    else if (kind == AKind::Zip) {
        std::error_code ec;
        auto fsz = fs::file_size(apath, ec);
        if (!ec) {
            auto buf = sz::read_file(apath, (size_t)fsz, 0);
            mz_zip_archive r, w2;
            memset(&r, 0, sizeof(r)); memset(&w2, 0, sizeof(w2));
            if (mz_zip_reader_init_mem(&r, buf.data(), buf.size(), 0) &&
                mz_zip_writer_init_heap(&w2, 0, 1 << 20)) {
                ok = true;
                mz_uint cnt = mz_zip_reader_get_num_files(&r);
                for (mz_uint e = 0; e < cnt && ok; ++e)
                    if (!removed[e]) ok = mz_zip_writer_add_from_zip_reader(&w2, &r, e) != 0;
                for (auto& it : newitems) {
                    if (!ok) break;
                    if (it.is_dir) { std::string d = it.name + "/"; ok = mz_zip_writer_add_mem(&w2, d.c_str(), nullptr, 0, 0) != 0; }
                    else {
                        auto data = sz::read_file(it.abs, (size_t)it.size, 0);
                        int lv = (!call && sz::is_precompressed(it.name)) ? 0 : level;
                        ok = mz_zip_writer_add_mem(&w2, it.name.c_str(), data.data(), data.size(), (mz_uint)lv) != 0;
                    }
                }
                void* obuf = nullptr; size_t on = 0;
                if (ok) ok = mz_zip_writer_finalize_heap_archive(&w2, &obuf, &on) != 0;
                mz_zip_writer_end(&w2);
                mz_zip_reader_end(&r);
                if (ok) ok = sz::write_file(apath, (uint8_t*)obuf, on);
                if (obuf) mz_free(obuf);
            }
        }
    }
#endif
    (void)threads;

    if (ok) {
        { std::lock_guard<std::mutex> lk(a->mtx); a->status = "Rebuilding view..."; }
        load_archive_into(a, apath);
        std::lock_guard<std::mutex> lk(a->mtx);
        a->status = "Archive updated: " + fs::path(apath).filename().string();
    } else {
        std::lock_guard<std::mutex> lk(a->mtx);
        a->status = "Could not update archive.";
    }
    a->running = false;
}

#if defined(HAVE_LZ4)
static size_t lz4_c(void*, const uint8_t* s, size_t n, uint8_t* d) {
    return (size_t)LZ4_compress_default((const char*)s, (char*)d, (int)n, LZ4_compressBound((int)n));
}
static size_t lz4_d(const uint8_t* s, size_t n, uint8_t* d, size_t cap) {
    int r = LZ4_decompress_safe((const char*)s, (char*)d, (int)n, (int)cap); return r < 0 ? SZ_ERR : (size_t)r;
}
static size_t lz4_bound(size_t n) { return (size_t)LZ4_compressBound((int)n); }
#endif
struct Codec3 {
    const char* name;
    std::function<size_t(const uint8_t*, size_t, uint8_t*)>        compress;
    std::function<size_t(const uint8_t*, size_t, uint8_t*, size_t)> decompress;
    std::function<size_t(size_t)>                                  bound;
};

static double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void bench_parallel(App* a, const Codec3& c, CodecView* view,
                           const uint8_t* d, size_t n, size_t blk, int runs, int threads) {
    size_t nblk = (n + blk - 1) / blk; if (!nblk) nblk = 1;
    size_t stride = c.bound(blk) + SZ_MARGIN;
    std::vector<uint8_t> arena(nblk * stride);
    std::vector<size_t>  csz(nblk, 0);
    std::vector<char>    raw(nblk, 0);
    auto blen = [&](size_t b) { size_t off = b * blk; return (off + blk <= n) ? blk : (n - off); };

    double best = 1e30;
    for (int r = 0; r < runs && !a->cancel; ++r) {
        double t0 = now_s();
        sz::parallel_for(nblk, threads, [&](size_t b) {
            size_t off = b * blk, bn = blen(b);
            size_t cc = c.compress(d + off, bn, arena.data() + b * stride);
            if (!cc || cc >= bn) { memcpy(arena.data() + b * stride, d + off, bn); cc = bn; raw[b] = 1; }
            else raw[b] = 0;
            csz[b] = cc;
        }, &a->cancel);
        double dt = now_s() - t0; if (dt < best) best = dt;
    }
    if (a->cancel) return;
    size_t total_c = 0; for (size_t b = 0; b < nblk; ++b) total_c += csz[b];

    double bestd = 1e30;
    for (int r = 0; r < runs && !a->cancel; ++r) {
        double t0 = now_s();
        sz::parallel_for(nblk, threads, [&](size_t b) {
            static thread_local std::vector<uint8_t> dec;
            size_t off = b * blk, bn = blen(b);
            if (dec.size() < bn + SZ_MARGIN) dec.resize(blk + SZ_MARGIN);
            if (raw[b]) memcpy(dec.data(), arena.data() + b * stride, bn);
            else        c.decompress(arena.data() + b * stride, csz[b], dec.data(), bn);
        }, &a->cancel);
        double dt = now_s() - t0; if (dt < bestd) bestd = dt;
    }
    if (a->cancel) return;

    { std::lock_guard<std::mutex> lk(a->mtx); view->comp.clear(); view->decomp.clear(); }
    if (threads <= 1) {
        std::vector<uint8_t> scratch(stride);
        std::vector<uint8_t> dec(blk + SZ_MARGIN);
        for (size_t b = 0; b < nblk; ++b) {
            size_t off = b * blk, bn = blen(b);
            double t0 = now_s(); c.compress(d + off, bn, scratch.data()); double dt = now_s() - t0;
            std::lock_guard<std::mutex> lk(a->mtx);
            view->comp.x.push_back((float)b); view->comp.y.push_back(dt > 0 ? (float)(bn / 1e6 / dt) : 0.0f);
        }
        for (size_t b = 0; b < nblk; ++b) {
            size_t off = b * blk, bn = blen(b);
            double t0 = now_s();
            if (raw[b]) memcpy(dec.data(), arena.data() + b * stride, bn);
            else        c.decompress(arena.data() + b * stride, csz[b], dec.data(), bn);
            double dt = now_s() - t0;
            std::lock_guard<std::mutex> lk(a->mtx);
            view->decomp.x.push_back((float)b); view->decomp.y.push_back(dt > 0 ? (float)(bn / 1e6 / dt) : 0.0f);
        }
    }

    bool ok = true;
    for (size_t b = 0; b < nblk && ok; ++b) {
        size_t off = b * blk, bn = blen(b);
        std::vector<uint8_t> dec(bn + SZ_MARGIN);
        if (raw[b]) { if (memcmp(arena.data() + b * stride, d + off, bn)) ok = false; }
        else { size_t dd = c.decompress(arena.data() + b * stride, csz[b], dec.data(), bn);
               if (dd != bn || memcmp(dec.data(), d + off, bn)) ok = false; }
    }

    sz_bench_result r{};
    r.comp_mbs = n / 1e6 / best; r.decomp_mbs = n / 1e6 / bestd;
    r.ratio = total_c ? (double)n / (double)total_c : 0.0;
    r.comp_bytes = total_c; r.in_bytes = n; r.blocks = nblk; r.ok = ok;
    std::lock_guard<std::mutex> lk(a->mtx);
    view->result = r; view->done = true; view->name = c.name;
}

static void run_benchmark(App* a) {
    a->running = true;
    int level, threads, runs, blk_kib;
    { std::lock_guard<std::mutex> lk(a->mtx);
      level = a->bench_level; threads = a->bench_threads; runs = a->runs; blk_kib = a->block_kib; }
    int accel = 10 - level; if (accel < 1) accel = 1;
    size_t blk = (size_t)blk_kib * 1024;
    const uint8_t* d = a->bench_data.data();
    size_t n = a->bench_data.size() - SZ_MARGIN;

    bool rs, rr, rt, rl, rz, rm, skip;
    { std::lock_guard<std::mutex> lk(a->mtx);
      skip = a->skip_hidden;
      rs = !skip || a->show_s; rr = !skip || a->show_r; rt = !skip || a->show_t;
      rl = !skip || a->show_lz4; rz = !skip || a->show_zip; rm = !skip || a->show_lzma; }

    Codec3 cS{ "SAT-ZIP S",
        [accel](const uint8_t* s, size_t m, uint8_t* o){ sz_state_t st; sz_state_reset(&st); return sz_compress_block(&st, s, m, o, accel); },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){ return sz_decompress_block(s, m, o, cap); },
        [](size_t m){ return sz_compress_bound(m); } };
    Codec3 cR{ "SAT-ZIP R",
        [accel](const uint8_t* s, size_t m, uint8_t* o){ sz_state_t st; sz_state_reset(&st); return sz_compress_block_r(&st, s, m, o, accel); },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){ return sz_decompress_block(s, m, o, cap); },
        [](size_t m){ return sz_compress_bound(m); } };
    Codec3 cT{ "SAT-ZIP T",
        [accel](const uint8_t* s, size_t m, uint8_t* o){
            std::vector<uint8_t> tok(sz_compress_bound(m) + SZ_MARGIN);
            sz_state_t st; sz_state_reset(&st);
            size_t t = sz_compress_block_r(&st, s, m, tok.data(), accel);
            if (!t) return (size_t)0;
            size_t a2 = sz_split_compress(tok.data(), t, o + 1, sz_compress_bound(m));
            if (a2 && a2 + 1 < t) { o[0] = 2; return a2 + 1; }
            o[0] = 1; memcpy(o + 1, tok.data(), t); return t + 1;
        },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){
            if (!m) return (size_t)SZ_ERR;
            if (s[0] == 1) return sz_decompress_block(s + 1, m - 1, o, cap);
            std::vector<uint8_t> tok(sz_compress_bound(cap) + SZ_MARGIN);
            size_t t = sz_split_decompress(s + 1, m - 1, tok.data(), tok.size());
            if (t == SZ_ERR) return (size_t)SZ_ERR;
            return sz_decompress_block(tok.data(), t, o, cap);
        },
        [](size_t m){ return sz_compress_bound(m) + 8; } };
    if (rs) bench_parallel(a, cS, &a->sat_s, d, n, blk, runs, threads);
    if (rr) bench_parallel(a, cR, &a->sat_r, d, n, blk, runs, threads);
    if (rt) bench_parallel(a, cT, &a->sat_t, d, n, blk, runs, threads);
#if defined(HAVE_LZ4)
    Codec3 cL{ "LZ4",
        [](const uint8_t* s, size_t m, uint8_t* o){ return lz4_c(nullptr, s, m, o); },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){ return lz4_d(s, m, o, cap); },
        [](size_t m){ return lz4_bound(m); } };
    if (rl) bench_parallel(a, cL, &a->lz4, d, n, blk, runs, threads);
#endif
#if defined(SATZIP_HAVE_ZIP)
    Codec3 cZ{ "zip (deflate)",
        [level](const uint8_t* s, size_t m, uint8_t* o){
            mz_ulong dl = mz_compressBound((mz_ulong)m);
            int lv = level; if (lv < 1) lv = 1; if (lv > 9) lv = 9;
            if (mz_compress2(o, &dl, s, (mz_ulong)m, lv) != MZ_OK) return (size_t)0;
            return (size_t)dl;
        },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){
            mz_ulong dl = (mz_ulong)cap;
            if (mz_uncompress(o, &dl, s, (mz_ulong)m) != MZ_OK) return (size_t)SZ_ERR;
            return (size_t)dl;
        },
        [](size_t m){ return (size_t)mz_compressBound((mz_ulong)m) + 64; } };
    if (rz) bench_parallel(a, cZ, &a->zipdef, d, n, blk, runs, threads);
#endif
#if defined(SATZIP_HAVE_7Z)
    Codec3 cM{ "LZMA",
        [level](const uint8_t* s, size_t m, uint8_t* o){
            CLzmaEncProps p; LzmaEncProps_Init(&p);
            p.level = level; p.reduceSize = m;
            Byte props[5]; SizeT ps = 5;
            SizeT dl = sz_compress_bound(m);
            SRes r = LzmaEncode(o + 5, &dl, s, m, &p, props, &ps, 0, NULL,
                                &sz::sevenz::alloc(), &sz::sevenz::alloc());
            if (r != SZ_OK) return (size_t)0;
            memcpy(o, props, 5);
            return (size_t)dl + 5;
        },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){
            if (m < 6) return (size_t)SZ_ERR;
            SizeT dl = cap, sl = m - 5;
            ELzmaStatus st;
            SRes r = LzmaDecode(o, &dl, s + 5, &sl, s, 5, LZMA_FINISH_ANY, &st,
                                &sz::sevenz::alloc());
            return (r == SZ_OK) ? (size_t)dl : (size_t)SZ_ERR;
        },
        [](size_t m){ return sz_compress_bound(m) + 64; } };
    if (rm) bench_parallel(a, cM, &a->lzma, d, n, blk, runs, threads);
#endif
    a->running = false;
}
static void load_bench_file(App* a, const std::string& path) {
    std::error_code ec; auto sz = fs::file_size(path, ec); if (ec) return;
    auto buf = sz::read_file(path, sz, SZ_MARGIN);
    std::lock_guard<std::mutex> lk(a->mtx);
    a->bench_data = std::move(buf); a->bench_source = path;
}

static void do_load_bench_folder(App* a, std::string dir) {
    a->running = true;
    { std::lock_guard<std::mutex> lk(a->mtx); a->status = "Loading folder for benchmark..."; }
    const size_t CAP = 512u * 1024 * 1024;
    std::vector<sz::Item> items;
    sz::gather(fs::path(dir), items);
    std::vector<uint8_t> buf;
    size_t files = 0;
    bool capped = false;
    for (auto& it : items) {
        if (a->cancel) break;
        if (it.is_dir || !it.size) continue;
        if (buf.size() + it.size > CAP) { capped = true; break; }
        auto data = sz::read_file(it.abs, (size_t)it.size, 0);
        buf.insert(buf.end(), data.begin(), data.end());
        ++files;
        a->prog_done = files; a->prog_total = items.size();
    }
    buf.resize(buf.size() + SZ_MARGIN, 0);
    { std::lock_guard<std::mutex> lk(a->mtx);
      a->bench_data = std::move(buf);
      char s[64]; snprintf(s, sizeof(s), " (%zu files, %.1f MB%s)", files,
                           (a->bench_data.size() - SZ_MARGIN) / 1e6, capped ? ", capped at 512MB" : "");
      a->bench_source = "folder: " + fs::path(dir).filename().string() + s; }
    a->running = false;
}

static uint64_t g_rng = 0x243F6A8885A308D3ULL;
static uint32_t xr() { uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; g_rng = x; return (uint32_t)(x >> 32); }
static void gen_corpus(App* a, const char* kind) {
    const size_t CAP = 16u * 1024 * 1024;
    std::vector<uint8_t> b(CAP + SZ_MARGIN, 0);
    if (!strcmp(kind, "random")) { for (size_t i = 0; i < CAP; ++i) b[i] = (uint8_t)xr(); }
    else if (!strcmp(kind, "text")) {
        static const char* w[] = {"the ","quick ","brown ","fox ","jumps ","over ","lazy ","dog ","while ","cat "};
        size_t i = 0; while (i < CAP - 16) { const char* s = w[xr() % 10]; size_t l = strlen(s); memcpy(&b[i], s, l); i += l; }
    } else if (!strcmp(kind, "mixed")) {
        size_t i = 0;
        while (i < CAP) {
            if (xr() & 1) { uint8_t v = (uint8_t)xr(); size_t r = 1 + xr() % 80; while (r-- && i < CAP) b[i++] = v; }
            else          { size_t r = 1 + xr() % 24; while (r-- && i < CAP) b[i++] = (uint8_t)xr(); }
        }
    }
    std::lock_guard<std::mutex> lk(a->mtx);
    a->bench_data = std::move(b);
    a->bench_source = std::string("synthetic: ") + kind + " (16 MB)";
}

static void draw_progress(App* a) {
    size_t d = a->prog_done, t = a->prog_total;
    float f = (a->running && t) ? (float)d / (float)t : (a->running ? 0.0f : 1.0f);
    if (a->running) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.62f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.13f, 0.13f, 1.0f));
        if (ImGui::Button("Cancel", ImVec2(90, 0))) a->cancel = true;
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip("Stop the current operation");
        ImGui::SameLine();
        ImGui::ProgressBar(f, ImVec2(-1, 0));
    } else {
        ImGui::ProgressBar(f, ImVec2(-1, 0));
    }
    { std::lock_guard<std::mutex> lk(a->mtx); ImGui::TextUnformatted(a->status.c_str()); }
}

static void wrap_same_line(float need) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < need) ImGui::NewLine();
}

static void draw_archive_tab(App* a) {
    bool busy = a->running;
    if (busy) ImGui::BeginDisabled();

    if (ImGui::Button("Open Archive...")) { std::string p; if (dlg_open(p, true)) stage_paths(a, {p}); }
    ImGui::SetItemTooltip("Open a .satz, .zip, .7z, .tar or .tar.xz archive");
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) ImGui::OpenPopup("browse");
    ImGui::SetItemTooltip("Add files or a folder to a new archive");
    if (ImGui::BeginPopup("browse")) {
        if (ImGui::MenuItem("Add files..."))  { std::string p; if (dlg_open(p, false)) stage_paths(a, {p}); }
        if (ImGui::MenuItem("Add folder...")) { std::string p; if (dlg_folder(p))      stage_paths(a, {p}); }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { std::lock_guard<std::mutex> lk(a->mtx);
        a->staged.clear(); a->suggest_name.clear(); a->opened.close(); a->selected.clear();
        a->ventries.clear(); a->view_order.clear(); a->mode = Mode::Empty;
        a->status = "Cleared."; }
    ImGui::SetItemTooltip("Clear the staged items / close the opened archive");

    if (a->mode == Mode::Building) {
        const float btnW = 170.0f;
        ImGui::SameLine();
        float target = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - btnW;
        if (target > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(target);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.16f, 0.55f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.66f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.13f, 0.45f, 0.16f, 1.0f));
        if (g_font_bold) ImGui::PushFont(g_font_bold);
        bool create = ImGui::Button("Create Archive", ImVec2(btnW, 0));
        if (g_font_bold) ImGui::PopFont();
        ImGui::PopStyleColor(3);
        if (create) {
            std::string suggest = "archive";
            int deffmt;
            { std::lock_guard<std::mutex> lk(a->mtx);
              if (!a->suggest_name.empty()) suggest = a->suggest_name;
              deffmt = a->settings.format; }
            std::string out;
            if (dlg_save(out, suggest, deffmt)) launch(a, std::thread(do_create, a, out));
        }
        ImGui::SetItemTooltip("Compress the staged items into one .satz archive");
    } else if (a->mode == Mode::Viewing) {
        ImGui::SameLine(0, 24);
        if (ImGui::Button("Extract All...")) {
            std::string dst; if (dlg_folder(dst)) launch(a, std::thread(do_extract, a, dst, false));
        }
        ImGui::SetItemTooltip("Extract every entry to a folder");
        ImGui::SameLine();
        if (ImGui::Button("Extract Selected...")) {
            std::string dst; if (dlg_folder(dst)) launch(a, std::thread(do_extract, a, dst, true));
        }
        ImGui::SetItemTooltip("Extract only the checked entries");
        if (a->okind != AKind::SevenZ && a->okind != AKind::Tar) {
            wrap_same_line(160);
            if (ImGui::Button("Add to archive...")) ImGui::OpenPopup("editadd");
            ImGui::SetItemTooltip("Add files or a folder to this archive");
            if (ImGui::BeginPopup("editadd")) {
                if (ImGui::MenuItem("Add files...")) { std::string p; if (dlg_open(p, false)) launch(a, std::thread(do_edit, a, std::vector<std::string>{p}, false)); }
                if (ImGui::MenuItem("Add folder...")) { std::string p; if (dlg_folder(p)) launch(a, std::thread(do_edit, a, std::vector<std::string>{p}, false)); }
                ImGui::EndPopup();
            }
            wrap_same_line(150);
            if (ImGui::Button("Remove selected")) launch(a, std::thread(do_edit, a, std::vector<std::string>{}, true));
            ImGui::SetItemTooltip("Rebuild the archive without the checked entries");
        }
    }
    if (busy) ImGui::EndDisabled();

    if (busy) ImGui::BeginDisabled();
    const char* levels[] = {"1 (fastest)","2","3","4","5","6","7","8","9 (smallest)"};
    int li = a->settings.level - 1; if (li < 0) li = 0; if (li > 8) li = 8;
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("compression", &li, levels, 9)) a->settings.level = li + 1;
    wrap_same_line(240); ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("threads", &a->settings.threads, 0, 32, a->settings.threads == 0 ? "auto" : "%d");
    wrap_same_line(300);
    ImGui::Checkbox("compress already-compressed files", &a->settings.compress_all);
    if (!a->obelisk.empty()) {
        wrap_same_line(240);
        ImGui::Checkbox("encrypt with SAT-OBELISK", &a->obelisk_after);
        ImGui::SetItemTooltip("After creating the archive, open it in SAT-OBELISK to encrypt it.\n"
                              "Your password stays in SAT-OBELISK; SAT-ZIP never sees it.");
    }
    wrap_same_line(60);
    ImGui::Checkbox(g_icons ? SZ_ICON_MAIL "##email" : "@##email", &a->mail_after);
    ImGui::SetItemTooltip("Email: after creating the archive, open a new email with it attached.");
    if (busy) ImGui::EndDisabled();

    ImGui::Separator();

    std::lock_guard<std::mutex> lk(a->mtx);
    if (a->mode == Mode::Building) {
        ImGui::Text("New archive - %zu item(s) staged", a->staged.size());
        if (ImGui::BeginTable("staged", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY, ImVec2(0, a->table_h))) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_DefaultSort);
            ImGui::TableSetupColumn("size (bytes)");
            ImGui::TableHeadersRow();
            if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
                if (ss->SpecsDirty && ss->SpecsCount > 0) {
                    const ImGuiTableColumnSortSpecs& sp = ss->Specs[0];
                    bool asc = sp.SortDirection != ImGuiSortDirection_Descending;
                    std::stable_sort(a->staged.begin(), a->staged.end(), [&](const sz::Item& x, const sz::Item& y) {
                        bool lt = (sp.ColumnIndex == 1) ? (x.size < y.size) : (x.name < y.name);
                        bool gt = (sp.ColumnIndex == 1) ? (x.size > y.size) : (x.name > y.name);
                        return asc ? lt : gt;
                    });
                    ss->SpecsDirty = false;
                }
            }
            ImGuiListClipper clip;
            clip.Begin((int)a->staged.size());
            while (clip.Step()) {
                for (int idx = clip.DisplayStart; idx < clip.DisplayEnd; ++idx) {
                    const sz::Item& it = a->staged[(size_t)idx];
                    ImGui::TableNextRow(); ImGui::TableNextColumn();
                    ImGui::PushID(idx);
                    std::string label = it.name + (it.is_dir ? "/" : "");
                    ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                    if (ImGui::BeginPopupContextItem("ctx")) {
                        if (ImGui::MenuItem("Open in Explorer")) open_in_explorer(it.abs);
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    if (it.is_dir) ImGui::TextDisabled("-"); else ImGui::Text("%ju", (uintmax_t)it.size);
                }
            }
            ImGui::EndTable();
        }
    } else if (a->mode == Mode::Viewing) {
        ImGui::Text("Viewing %s - %zu entries%s", fs::path(a->opened_path).filename().string().c_str(),
                    a->ventries.size(), a->okind == AKind::Zip ? "  [zip]" :
                                        a->okind == AKind::SevenZ ? "  [7z]" :
                                        a->okind == AKind::Tar ? "  [tar]" : "");
        ImGui::SameLine(0, 24);
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##search", "Search in archive...", a->search, sizeof(a->search));
        ImGui::SetItemTooltip("Filter entries by name (case-insensitive). Double-click an entry to open it.");
        if (ImGui::BeginTable("entries", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY, ImVec2(0, a->table_h))) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 24);
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_DefaultSort);
            ImGui::TableSetupColumn("size");
            ImGui::TableSetupColumn("stored");
            ImGui::TableHeadersRow();
            bool refilter = false;
            if (a->filter_applied != a->search) {
                std::string f = a->search;
                std::string flc = f;
                for (auto& c : flc) c = (char)tolower((unsigned char)c);
                a->view_order.clear();
                for (uint32_t i = 0; i < a->ventries.size(); ++i) {
                    if (flc.empty()) { a->view_order.push_back(i); continue; }
                    std::string nlc = a->ventries[i].name;
                    for (auto& c : nlc) c = (char)tolower((unsigned char)c);
                    if (nlc.find(flc) != std::string::npos) a->view_order.push_back(i);
                }
                a->filter_applied = f;
                refilter = true;
            }
            if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
                if ((ss->SpecsDirty || refilter) && ss->SpecsCount > 0) {
                    const ImGuiTableColumnSortSpecs& sp = ss->Specs[0];
                    bool asc = sp.SortDirection != ImGuiSortDirection_Descending;
                    const std::vector<ViewEntry>& es = a->ventries;
                    std::stable_sort(a->view_order.begin(), a->view_order.end(), [&](uint32_t x, uint32_t y) {
                        const ViewEntry& ex = es[x]; const ViewEntry& ey = es[y];
                        bool lt, gt;
                        if (sp.ColumnIndex == 2)      { lt = ex.orig   < ey.orig;   gt = ex.orig   > ey.orig; }
                        else if (sp.ColumnIndex == 3) { lt = ex.stored < ey.stored; gt = ex.stored > ey.stored; }
                        else { int c = ex.name.compare(ey.name); lt = c < 0; gt = c > 0; }
                        return asc ? lt : gt;
                    });
                    ss->SpecsDirty = false;
                }
            }
            ImGuiListClipper clip;
            clip.Begin((int)a->view_order.size());
            while (clip.Step()) {
                for (int k = clip.DisplayStart; k < clip.DisplayEnd; ++k) {
                    uint32_t i = a->view_order[(size_t)k];
                    const ViewEntry& e = a->ventries[i];
                    ImGui::TableNextRow(); ImGui::TableNextColumn();
                    if (!e.is_dir) { bool c = a->selected[i] != 0; ImGui::PushID((int)i);
                        if (ImGui::Checkbox("##s", &c)) a->selected[i] = c ? 1 : 0; ImGui::PopID(); }
                    ImGui::TableNextColumn();
                    ImGui::PushID((int)i + 100000);
                    std::string lbl = e.name + (e.is_dir ? "/" : "");
                    ImGui::Selectable(lbl.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
                    if (!e.is_dir && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !a->running)
                        launch(a, std::thread(do_open_entry, a, i));
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    if (e.is_dir) ImGui::TextDisabled("-"); else ImGui::Text("%ju", (uintmax_t)e.orig);
                    ImGui::TableNextColumn();
                    if (e.is_dir) ImGui::TextDisabled("-"); else ImGui::Text("%ju", (uintmax_t)e.stored);
                }
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("Drop files/folders to build an archive, or a .satz/.zip to open it.");
    }
    if (a->mode != Mode::Empty) {
        ImGui::InvisibleButton("vsplit", ImVec2(-1, 8.0f));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) {
            a->table_h += ImGui::GetIO().MouseDelta.y;
            if (a->table_h < 140.0f) a->table_h = 140.0f;
            if (a->table_h > 1200.0f) a->table_h = 1200.0f;
        }
    }
}

static void plot_phase(App* a, const char* title, int phase) {
    if (ImPlot::BeginPlot(title, ImVec2(-1, 220))) {
        ImPlot::SetupAxes("block", "MB/s", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        std::lock_guard<std::mutex> lk(a->mtx);
        auto line = [&](const char* nm, const Series& s) {
            if (!s.x.empty()) ImPlot::PlotLine(nm, s.x.data(), s.y.data(), (int)s.x.size());
        };
        if (a->show_s)    line("SAT-ZIP S", phase == 0 ? a->sat_s.comp : a->sat_s.decomp);
        if (a->show_r)    line("SAT-ZIP R", phase == 0 ? a->sat_r.comp : a->sat_r.decomp);
        if (a->show_t)    line("SAT-ZIP T", phase == 0 ? a->sat_t.comp : a->sat_t.decomp);
        if (a->show_lz4)  line("LZ4",       phase == 0 ? a->lz4.comp   : a->lz4.decomp);
        if (a->show_zip)  line("zip",       phase == 0 ? a->zipdef.comp : a->zipdef.decomp);
        if (a->show_lzma) line("LZMA",      phase == 0 ? a->lzma.comp  : a->lzma.decomp);
        ImPlot::EndPlot();
    }
}
static void bench_row(const CodecView& c) {
    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(c.name);
    if (!c.done) { for (int k = 0; k < 5; ++k) { ImGui::TableNextColumn(); ImGui::TextDisabled("--"); } return; }
    ImGui::TableNextColumn(); ImGui::Text("x%.2f", c.result.ratio);
    ImGui::TableNextColumn(); ImGui::Text("%.2f MB", (double)c.result.comp_bytes / 1e6);
    ImGui::TableNextColumn(); ImGui::Text("%.0f", c.result.comp_mbs);
    ImGui::TableNextColumn(); ImGui::Text("%.0f", c.result.decomp_mbs);
    ImGui::TableNextColumn();
    if (c.result.ok) ImGui::TextColored(ImVec4(0.3f,0.9f,0.3f,1), "OK");
    else             ImGui::TextColored(ImVec4(0.95f,0.3f,0.3f,1), "FAIL");
}
static void draw_benchmark_tab(App* a) {
    bool busy = a->running;
    ImGui::Text("Source: %s", a->bench_source.c_str());
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Open file...")) { std::string p; if (dlg_open(p, false)) load_bench_file(a, p); }
    ImGui::SetItemTooltip("Benchmark a single file");
    ImGui::SameLine();
    if (ImGui::Button("Open folder...")) { std::string p; if (dlg_folder(p)) launch(a, std::thread(do_load_bench_folder, a, p)); }
    ImGui::SetItemTooltip("Benchmark all files in a folder (concatenated, up to 512 MB)");
    ImGui::SameLine();
    if (ImGui::Button("Synthetic...")) ImGui::OpenPopup("synth");
    ImGui::SetItemTooltip("Generate a 16 MB test corpus");
    if (ImGui::BeginPopup("synth")) {
        if (ImGui::MenuItem("text"))   gen_corpus(a, "text");
        if (ImGui::MenuItem("mixed"))  gen_corpus(a, "mixed");
        if (ImGui::MenuItem("zeros"))  gen_corpus(a, "zeros");
        if (ImGui::MenuItem("random")) gen_corpus(a, "random");
        ImGui::EndPopup();
    }
    wrap_same_line(200); ImGui::SetNextItemWidth(110); ImGui::SliderInt("block KiB", &a->block_kib, 4, 64);
    ImGui::SetItemTooltip("Block size used per compression call");
    wrap_same_line(170); ImGui::SetNextItemWidth(110); ImGui::SliderInt("runs", &a->runs, 1, 50);
    ImGui::SetItemTooltip("Best-of-N timed passes");
    wrap_same_line(170); ImGui::SetNextItemWidth(110); ImGui::SliderInt("level", &a->bench_level, 1, 9);
    ImGui::SetItemTooltip("Compression level for SAT-ZIP / zip / LZMA");
    wrap_same_line(180); ImGui::SetNextItemWidth(110);
    ImGui::SliderInt("threads", &a->bench_threads, 0, 32, a->bench_threads == 0 ? "auto" : "%d");
    ImGui::SetItemTooltip("Worker threads for the aggregate numbers (0 = all cores)");

    {
        const float fw = g_icons ? 44.0f : 90.0f, bw = 150.0f;
        ImGui::SameLine();
        float target = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fw - bw - ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::GetContentRegionAvail().x < fw + bw + ImGui::GetStyle().ItemSpacing.x) ImGui::NewLine();
        else if (target > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(target);
        if (ImGui::Button(g_icons ? SZ_ICON_FILTER "##filter" : "Filter...", ImVec2(fw, 0))) ImGui::OpenPopup("benchfilter");
        ImGui::SetItemTooltip("Choose which codecs to show (and run)");
        if (ImGui::BeginPopup("benchfilter")) {
            ImGui::Checkbox("SAT-ZIP S", &a->show_s);
            ImGui::Checkbox("SAT-ZIP R", &a->show_r);
            ImGui::Checkbox("SAT-ZIP T", &a->show_t);
            ImGui::Checkbox("LZ4", &a->show_lz4);
            ImGui::Checkbox("zip (deflate)", &a->show_zip);
            ImGui::Checkbox("LZMA", &a->show_lzma);
            ImGui::Separator();
            ImGui::Checkbox("don't run hidden codecs", &a->skip_hidden);
            ImGui::SetItemTooltip("Applies to the next benchmark run; already-computed results stay visible when re-shown");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        bool can = a->bench_data.size() > SZ_MARGIN;
        if (!can) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.16f, 0.55f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.66f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.13f, 0.45f, 0.16f, 1.0f));
        if (g_font_bold) ImGui::PushFont(g_font_bold);
        bool go = ImGui::Button("Benchmark", ImVec2(bw, 0));
        if (g_font_bold) ImGui::PopFont();
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip("Run the benchmark on the loaded source");
        if (go) launch(a, std::thread(run_benchmark, a));
        if (!can) ImGui::EndDisabled();
    }
    if (busy) ImGui::EndDisabled();
    ImGui::TextDisabled("Table = aggregate at the chosen thread count; charts show per-block speed (single thread only).");
    size_t bn = a->bench_data.size() > SZ_MARGIN ? a->bench_data.size() - SZ_MARGIN : 0;
    if (bn && bn < 4u * 1024 * 1024)
        ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1),
            "Source is small (%.2f MB): timings are dominated by overhead, not codec speed. Use >= 4 MB.", bn / 1e6);
    if (a->sat_r.done && a->lz4.done && a->sat_r.result.ratio < 1.05 && a->lz4.result.ratio < 1.05)
        ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1),
            "Source is incompressible (already compressed/encrypted?): no codec can gain here. Try 'text' or a document/source file.");

    if (ImGui::BeginTable("bench", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("codec"); ImGui::TableSetupColumn("ratio"); ImGui::TableSetupColumn("result");
        ImGui::TableSetupColumn("comp MB/s"); ImGui::TableSetupColumn("decomp MB/s");
        ImGui::TableSetupColumn("status"); ImGui::TableHeadersRow();
        if (a->show_s)    bench_row(a->sat_s);
        if (a->show_r)    bench_row(a->sat_r);
        if (a->show_t)    bench_row(a->sat_t);
        if (a->show_lz4)  bench_row(a->lz4);
        if (a->show_zip)  bench_row(a->zipdef);
        if (a->show_lzma) bench_row(a->lzma);
        ImGui::EndTable();
    }
    plot_phase(a, "Compress throughput", 0);
    plot_phase(a, "Decompress throughput", 1);
}

static void apply_style(bool dark) {
    if (dark) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 6.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 6.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.ScrollbarSize     = 14.0f;
    s.FramePadding      = ImVec2(10, 8);
    s.ItemSpacing       = ImVec2(10, 8);
    s.ItemInnerSpacing  = ImVec2(2, 4);
    s.WindowPadding     = ImVec2(12, 4);
    s.WindowBorderSize  = 1.0f;
}

static bool settings_dirty(const App* a) {
    const sz::Settings& x = a->settings;
    const sz::Settings& y = a->settings_saved;
    return x.level != y.level || x.threads != y.threads || x.compress_all != y.compress_all ||
           x.mode != y.mode || x.dark != y.dark || x.zipnow_folder != y.zipnow_folder ||
           x.format != y.format || x.win_w != y.win_w || x.win_h != y.win_h;
}

static void save_settings_now(App* a) {
    bool ok = sz::save_settings(a->settings);
    a->settings_saved = a->settings;
    std::lock_guard<std::mutex> lk(a->mtx);
    a->status = ok ? ("Settings saved to " + sz::settings_path().string()) : "Could not save settings";
}

static void draw_settings_tab(App* a) {
    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::CollapsingHeader("Compression")) {
    const char* levels[] = {"1 (fastest)","2","3","4","5","6","7","8","9 (smallest)"};
    int li = a->settings.level - 1; if (li < 0) li = 0; if (li > 8) li = 8;
    ImGui::SetNextItemWidth(180);
    if (ImGui::Combo("default level", &li, levels, 9)) a->settings.level = li + 1;
    ImGui::SetNextItemWidth(180);
    ImGui::SliderInt("default threads", &a->settings.threads, 0, 32, a->settings.threads == 0 ? "auto" : "%d");
    ImGui::Checkbox("compress already-compressed files by default", &a->settings.compress_all);
    const char* cmodes[] = {"Fast (Mode S)", "Better ratio (Mode R)", "Max ratio (Mode T)"};
    int mi = a->settings.mode; if (mi < 0) mi = 0; if (mi > 2) mi = 2;
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("mode", &mi, cmodes, 3)) a->settings.mode = mi;
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Mode S  - fastest compression, good ratio.\n"
                          "Mode R  - denser search, better ratio, slower to compress\n"
                          "          (extraction stays just as fast).\n"
                          "Mode T  - Mode R + ANS entropy coding, best ratio,\n"
                          "          slowest to compress and slower to extract.");
    }

    if (ImGui::CollapsingHeader("Archive format & Zip now")) {
    const char* fmts[] = {"SAT-ZIP (.satz)", "Zip (.zip)", "Tar (.tar)", "7-Zip (.7z)", "Tar+XZ (.tar.xz)"};
    int fi = a->settings.format; if (fi < 0) fi = 0; if (fi > 4) fi = 4;
    ImGui::SetNextItemWidth(180);
    if (ImGui::Combo("default archive format", &fi, fmts, 5)) a->settings.format = fi;
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Used by 'Zip now' and preselected in the Create Archive dialog.\n"
                          ".satz is fastest (our codec); .zip opens everywhere and is built\n"
                          "multithreaded, so it is still faster than Windows' built-in zipping.");
    ImGui::Checkbox("Zip now: wrap single files in a folder (named without extension)", &a->settings.zipnow_folder);
    ImGui::SetItemTooltip("report.pdf becomes report.satz containing report/report.pdf");
    }

    if (ImGui::CollapsingHeader("Appearance")) {
    if (ImGui::Checkbox("Dark theme", &a->settings.dark)) apply_style(a->settings.dark);
    }

    if (ImGui::CollapsingHeader("Window")) {
    ImGui::SetNextItemWidth(180); ImGui::SliderInt("default width",  &a->settings.win_w, 700, 2560);
    ImGui::SetNextItemWidth(180); ImGui::SliderInt("default height", &a->settings.win_h, 500, 1440);
    ImGui::SameLine();
    if (ImGui::Button("Apply now", ImVec2(110, 0))) glfwSetWindowSize(a->win, a->settings.win_w, a->settings.win_h);
    ImGui::SetItemTooltip("Resize the window right away");
    ImGui::TextDisabled("The UI scales with the window height.");
    }

    ImGui::Dummy(ImVec2(0, 10));
    if (ImGui::Button("Save settings")) save_settings_now(a);
    ImGui::SetItemTooltip("Persist these settings for every next start");
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) a->settings = sz::Settings{};
    ImGui::SetItemTooltip("Restore defaults (not saved until you press Save)");
    if (settings_dirty(a)) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1), "unsaved changes"); }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("%s", sz::settings_path().string().c_str());
}

static void draw_info_tab(App* a) {
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Text("SAT-ZIP  v%s", SATZIP_VERSION);
    ImGui::TextDisabled("High-throughput file archiver");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::TextUnformatted("License: Apache-2.0");
    ImGui::TextDisabled("Copyright 2026 Sam Alex Thiers");
    if (ImGui::Button("View full license", ImVec2(170, 0))) open_url("https://www.apache.org/licenses/LICENSE-2.0");
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::TextUnformatted("Latest version & downloads:");
    if (ImGui::Button("Open " SATZIP_REPO_URL)) open_url(SATZIP_REPO_URL);
    ImGui::SameLine();
    if (ImGui::Button("Releases", ImVec2(110, 0))) open_url(SATZIP_REPO_URL "/releases");
    ImGui::Dummy(ImVec2(0, 10));

    if (ImGui::CollapsingHeader("FAQ")) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Why is an archive sometimes barely smaller, or even slightly larger?");
        ImGui::TextUnformatted("Already-compressed or encrypted files (.zip, .7z, .jpg, .mp4, .obl, ...) "
            "have almost no redundancy left, so no tool can shrink them - not SAT-ZIP, 7-Zip or Windows. "
            "SAT-ZIP stores them as-is and adds a little container bookkeeping, which can make the archive "
            "a touch larger than the original.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Does compressing an image (or any file) reduce its quality?");
        ImGui::TextUnformatted("No. Compression is lossless: the extracted file is byte-for-byte identical "
            "to the original. It only changes size, never content.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Why is the ratio lower than 7-Zip or Windows zip?");
        ImGui::TextUnformatted("The default modes optimize for speed, especially fast extraction. For the "
            "tightest packing, switch Settings > mode to 'Max ratio (Mode T)': it adds an ANS "
            "entropy-coding stage, trading compression/extraction speed for size.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::Separator();
    if (!a->obelisk.empty())
        ImGui::TextDisabled("SAT-OBELISK found: %s", a->obelisk.string().c_str());
    else
        ImGui::TextDisabled("SAT-OBELISK not found (install it to enable encrypt-after-create).");
    ImGui::TextDisabled("Built with Dear ImGui, ImPlot and GLFW; LZ4 used for comparison.");
}

static int run_headless_zip(const char* path, std::string* made = nullptr) {
    std::vector<sz::Item> items;
    fs::path src(path);
    sz::gather(src, items);
    sz::Settings cfg; sz::load_settings(cfg);
    int accel = 10 - cfg.level; if (accel < 1) accel = 1;

    const char* fmt_ext = ".satz";
#if defined(SATZIP_HAVE_ZIP)
    if (cfg.format == 1) fmt_ext = ".zip";
#endif
    if (cfg.format == 2) fmt_ext = ".tar";
#if defined(SATZIP_HAVE_7Z)
    if (cfg.format == 3) fmt_ext = ".7z";
    if (cfg.format == 4) fmt_ext = ".tar.xz";
#endif

    std::string arc = std::string(path) + fmt_ext;
    std::error_code ec;
    if (cfg.zipnow_folder && fs::is_regular_file(src, ec)) {
        std::string stem = src.stem().string();
        for (auto& it : items) it.name = stem + "/" + it.name;
        items.insert(items.begin(), { src.parent_path(), stem, true, 0 });
        arc = (src.parent_path() / (stem + fmt_ext)).string();
    }

    std::vector<uint8_t> out;
    size_t n = 0;
#if defined(SATZIP_HAVE_ZIP)
    if (cfg.format == 1) n = sz::build_zip(items, out, cfg.level, cfg.compress_all, cfg.threads);
    else
#endif
#if defined(SATZIP_HAVE_7Z)
    if (cfg.format == 3) n = sz::sevenz_write(items, out, cfg.level);
    else if (cfg.format == 4) n = sz::build_tar_xz(items, out, cfg.level);
    else
#endif
    if (cfg.format == 2) n = sz::build_tar(items, out);
    else n = sz::build_archive(items, out, accel, cfg.threads, cfg.compress_all, cfg.mode);
    if (made) *made = arc;
    if (!n || !sz::write_file(arc, out.data(), n)) {
#ifdef _WIN32
        MessageBoxA(nullptr, "Could not create archive.", "SAT-ZIP", MB_ICONERROR);
#endif
        return 1;
    }
    return 0;
}
static int run_headless_extract(const char* archive, bool subfolder) {
#if defined(SATZIP_HAVE_ZIP) || defined(SATZIP_HAVE_7Z)
    bool zip = false, svz = false;
#if defined(SATZIP_HAVE_ZIP)
    zip = sz::is_zip_file(archive);
#endif
#if defined(SATZIP_HAVE_7Z)
    svz = !zip && sz::is_7z_file(archive);
#endif
    if (zip || svz) {
        fs::path ap(archive);
        fs::path dest = ap.parent_path();
        if (subfolder) dest /= ap.stem();
        if (dest.empty()) dest = fs::path(".");
        int bad = 0;
        std::vector<std::string> failed;
#if defined(SATZIP_HAVE_ZIP)
        if (zip) sz::zip_extract(ap, dest, nullptr, 0, {}, &bad, nullptr, &failed);
#endif
#if defined(SATZIP_HAVE_7Z)
        if (svz) sz::sevenz_extract(ap, dest, nullptr, {}, &bad, &failed);
#endif
        show_failed_dialog(failed);
        return bad ? 1 : 0;
    }
    {
        bool xzf = false;
#if defined(SATZIP_HAVE_7Z)
        xzf = sz::is_xz_file(archive);
#endif
        if (xzf || fs::path(archive).extension() == ".tar") {
            fs::path ap(archive);
            fs::path dest = ap.parent_path();
            if (subfolder) { fs::path st = ap.stem(); if (st.extension() == ".tar") st = st.stem(); dest /= st; }
            if (dest.empty()) dest = fs::path(".");
            std::error_code ec;
            auto fsz = fs::file_size(ap, ec);
            if (ec) return 1;
            auto raw = sz::read_file(ap, (size_t)fsz, 0);
            std::vector<uint8_t> tb;
#if defined(SATZIP_HAVE_7Z)
            if (sz::is_xz_data(raw.data(), raw.size())) { if (!sz::xz_decompress(raw.data(), raw.size(), tb)) return 1; }
            else
#endif
            tb = std::move(raw);
            std::vector<sz::TarEntry> tes;
            if (!sz::tar_list(tb.data(), tb.size(), tes)) return 1;
            int bad = 0;
            std::vector<std::string> failed;
            sz::tar_extract(tb.data(), tb.size(), tes, dest, nullptr, {}, &bad, &failed);
            show_failed_dialog(failed);
            return bad ? 1 : 0;
        }
    }
#endif
    sz::Archive a;
    if (!sz::open_archive(fs::path(archive), a)) {
#ifdef _WIN32
        MessageBoxA(nullptr, "Not a SAT-ZIP archive.", "SAT-ZIP", MB_ICONERROR);
#endif
        return 1;
    }
    fs::path ap(archive);
    fs::path dest = ap.parent_path();
    if (subfolder) dest /= ap.stem();
    if (dest.empty()) dest = fs::path(".");
    int bad = 0;
    int files = sz::extract(a, dest, nullptr, 0, {}, &bad);
#ifdef _WIN32
    if (bad) {
        std::string m = "Extracted " + std::to_string(files) + " file(s) to:\n" + dest.string() +
                        "\n\n" + std::to_string(bad) + " item(s) could not be written "
                        "(folder access, a name collision, or path length?).";
        MessageBoxA(nullptr, m.c_str(), "SAT-ZIP", MB_ICONWARNING);
    }
#endif
    return bad ? 1 : 0;
}

#ifdef _WIN32
static WNDPROC g_prev_wndproc = nullptr;
static App*    g_app = nullptr;
static LRESULT CALLBACK sz_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COPYDATA && g_app) {
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lp;
        if (cds && cds->lpData && cds->cbData) {
            std::string path((const char*)cds->lpData, cds->cbData - 1);
            stage_paths(g_app, { path });
            SetForegroundWindow(h);
        }
        return TRUE;
    }
    return g_prev_wndproc ? CallWindowProcW(g_prev_wndproc, h, msg, wp, lp)
                          : DefWindowProcW(h, msg, wp, lp);
}
static bool forward_to_existing(const std::string& path) {
    HWND other = FindWindowW(nullptr, L"SAT-ZIP");
    if (!other) return false;
    COPYDATASTRUCT cds;
    cds.dwData = 1;
    cds.cbData = (DWORD)path.size() + 1;
    cds.lpData = (void*)path.c_str();
    SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
    return true;
}
#endif

static void drop_cb(GLFWwindow* w, int count, const char** paths) {
    App* a = (App*)glfwGetWindowUserPointer(w);
    if (a->running) return;
    std::vector<std::string> v; for (int i = 0; i < count; ++i) v.emplace_back(paths[i]);
    stage_paths(a, v);
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

static void install_crash_log() {
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

int main(int argc, char** argv) {
    install_crash_log();
    if (argc >= 3 && std::string(argv[1]) == "--zip")   return run_headless_zip(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--zipmail") {
        std::string made;
        int rc = run_headless_zip(argv[2], &made);
        if (rc == 0) sz::mail_with_attachment(fs::path(made));
        return rc;
    }
    if (argc >= 3 && std::string(argv[1]) == "--xhere") return run_headless_extract(argv[2], false);
    if (argc >= 3 && std::string(argv[1]) == "--xto")   return run_headless_extract(argv[2], true);
    std::string launch_path = (argc >= 2) ? std::string(argv[1]) : std::string();

#ifdef _WIN32
    HANDLE inst_mtx = CreateMutexW(nullptr, FALSE, L"Local\\SATZIP_GUI_Instance");
    if (inst_mtx && GetLastError() == ERROR_ALREADY_EXISTS && !launch_path.empty()) {
        if (forward_to_existing(launch_path)) return 0;
    }
#endif

    sz::Settings settings; sz::load_settings(settings);

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(settings.win_w, settings.win_h, "SAT-ZIP", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    set_window_icon(win);

    App app; app.win = win; app.settings = settings; app.settings_saved = settings;
    glfwSetWindowUserPointer(win, &app);
    glfwSetDropCallback(win, drop_cb);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
#ifdef _WIN32
    if (fs::exists("C:\\Windows\\Fonts\\segoeui.ttf"))
        ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    if (fs::exists("C:\\Windows\\Fonts\\segmdl2.ttf")) {
        static const ImWchar icon_range[] = { 0xE700, 0xE900, 0 };
        ImFontConfig mcfg;
        mcfg.MergeMode = true;
        mcfg.GlyphMinAdvanceX = 18.0f;
        mcfg.GlyphOffset = ImVec2(0, 2);
        g_icons = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segmdl2.ttf",
                                                           15.0f, &mcfg, icon_range) != nullptr;
    }
    if (fs::exists("C:\\Windows\\Fonts\\segoeuib.ttf"))
        g_font_bold = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 18.0f);
#endif
    apply_style(app.settings.dark);
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");

#ifdef _WIN32
    g_app = &app;
    {
        HWND hwnd = glfwGetWin32Window(win);
        if (hwnd) g_prev_wndproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)sz_wndproc);
    }
#endif
    if (!launch_path.empty()) stage_paths(&app, { launch_path });

    int prev_tab = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        {
            int wh = 0, ww = 0;
            glfwGetWindowSize(win, &ww, &wh);
            float scale = (float)wh / 760.0f;
            if (scale < 0.8f) scale = 0.8f;
            if (scale > 2.0f) scale = 2.0f;
            ImGui::GetIO().FontGlobalScale = scale;
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("SAT-ZIP", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        int cur_tab = prev_tab;
        if (ImGui::BeginTabBar("tabs")) {
            static const char* tab_names[4] = { "Archive", "Benchmark", "Settings", "Info" };
            float tabw = 0.0f;
            if (g_font_bold) ImGui::PushFont(g_font_bold);
            for (const char* n : tab_names) { float w = ImGui::CalcTextSize(n).x; if (w > tabw) tabw = w; }
            if (g_font_bold) ImGui::PopFont();
            tabw += ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
            auto tab_begin = [&](const char* name, int id) {
                bool bold = g_font_bold && prev_tab == id;
                ImGui::SetNextItemWidth(tabw);
                if (bold) ImGui::PushFont(g_font_bold);
                bool open = ImGui::BeginTabItem(name);
                if (bold) ImGui::PopFont();
                return open;
            };
            if (tab_begin("Archive", 0))   { cur_tab = 0; draw_archive_tab(&app);   ImGui::EndTabItem(); }
            if (tab_begin("Benchmark", 1)) { cur_tab = 1; draw_benchmark_tab(&app); ImGui::EndTabItem(); }
            if (tab_begin("Settings", 2))  { cur_tab = 2; draw_settings_tab(&app);  ImGui::EndTabItem(); }
            if (tab_begin("Info", 3))      { cur_tab = 3; draw_info_tab(&app);      ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        if (prev_tab == 2 && cur_tab != 2 && settings_dirty(&app))
            ImGui::OpenPopup("Unsaved settings");
        prev_tab = cur_tab;
        if (ImGui::BeginPopupModal("Unsaved settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("You changed settings without saving.");
            ImGui::Dummy(ImVec2(0, 6));
            if (ImGui::Button("Save", ImVec2(120, 0))) { save_settings_now(&app); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120, 0))) {
                app.settings = app.settings_saved;
                apply_style(app.settings.dark);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (app.share_pending.exchange(false)) {
            std::string sp;
            { std::lock_guard<std::mutex> lk(app.mtx); sp = app.share_path; }
#ifdef _WIN32
            HWND hw = glfwGetWin32Window(win);
            bool shared = hw && sz::share_file(hw, fs::path(sp).wstring());
#else
            bool shared = false;
#endif
            std::lock_guard<std::mutex> lk(app.mtx);
            if (shared) {
                app.status += "  Choose an app (e.g. Outlook) in the share panel to email the archive.";
            } else {
                sz::MailResult mr = sz::mail_fallback_compose(fs::path(sp));
                if (mr == sz::MailResult::ClipboardCompose)
                    app.status += "  Mail opened; the archive is on your clipboard - press Ctrl+V to attach it.";
                else if (mr == sz::MailResult::Failed)
                    app.status += "  (could not open a mail client)";
            }
        }
        ImGui::Separator();
        draw_progress(&app);
        ImGui::End();

        ImGui::Render();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        if (app.settings.dark) glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        else                   glClearColor(0.94f, 0.94f, 0.94f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    if (app.worker.joinable()) app.worker.join();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
