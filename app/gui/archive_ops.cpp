#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "archive_ops.hpp"
#include "fonts.hpp"
#include "satzip_zip.hpp"
#include "satzip_7z.hpp"
#include "satzip_7z_write.hpp"
#include "satzip_tar.hpp"
#include "satzip_xz.hpp"
#include "satzip_any.hpp"
#include "satzip_mail.hpp"

#include <algorithm>
#include <cstring>

namespace szgui {

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
    if (sz::is_tar_or_xz(path)) {
        std::vector<uint8_t> tb;
        std::vector<sz::TarEntry> tes;
        if (sz::load_tar_bytes(path, tb) && !tb.empty() && sz::tar_list(tb.data(), tb.size(), tes)) {
            for (uint32_t i = 0; i < tes.size(); ++i)
                ve.push_back({ tes[i].name, tes[i].is_dir, tes[i].size, 0, i });
            std::lock_guard<std::mutex> lk(a->mtx);
            a->tarbuf = std::move(tb);
            a->tentries = std::move(tes);
            kind = AKind::Tar; ok = true;
        }
    } else
    if (sz::open_archive(path, tmp)) {
        for (uint32_t i = 0; i < tmp.count; ++i) {
            const sz_ar_entry& e = tmp.entries[i];
            ve.push_back({ e.name, e.is_dir != 0, e.orig_size, e.stored_size, i });
        }
        kind = AKind::Satz; ok = true;
    }

    for (const auto& v : ve) want_glyphs(v.name);
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

void stage_paths(App* a, const std::vector<std::string>& paths) {
    if (a->running) return;
    if (paths.size() == 1 && is_archive_path(fs::path(paths[0]))) {
        launch(a, std::thread(do_open, a, paths[0]));
        return;
    }
    std::lock_guard<std::mutex> lk(a->mtx);
    if (a->mode != Mode::Building) { a->staged.clear(); a->suggest_name.clear(); a->mode = Mode::Building; }
    for (const auto& s : paths) sz::gather(fs::path(s), a->staged);
    for (const auto& it : a->staged) want_glyphs(it.name);
    if (a->suggest_name.empty() && !paths.empty()) {
        fs::path first(paths[0]);
        a->suggest_name = first.stem().string();
        if (a->suggest_name.empty()) a->suggest_name = first.filename().string();
    }
    a->status = std::to_string(a->staged.size()) + " item(s) staged";
}

void do_create(App* a, std::string outpath) {
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
    if (ext == ".7z") n = sz::sevenz_write(items, out, level, prog, &a->cancel);
    else if (ext == ".xz") n = sz::build_tar_xz(items, out, level, prog, &a->cancel);
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
    bool ok = n && sz::write_file_atomic(outpath, out.data(), n);
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

void do_extract(App* a, std::string destdir, bool selected_only) {
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
            [a](size_t d, size_t t) { a->prog_done = d; a->prog_total = t; }, &bad, &a->cancel, &failed);
    } else
#endif
    if (kind == AKind::Tar) {
        files = sz::tar_extract(a->tarbuf.data(), a->tarbuf.size(), a->tentries,
            fs::path(destdir), selected_only ? &sel : nullptr,
            [a](size_t d, size_t t) { a->prog_done = d; a->prog_total = t; }, &bad, &a->cancel, &failed);
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

void do_open_entry(App* a, uint32_t vidx) {
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

void do_edit(App* a, std::vector<std::string> add_paths, bool remove_selected) {
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
            if (!removed[e]) bound += SZ_AR_DIR_BOUND + strlen(a->opened.entries[e].name) +
                                      (size_t)a->opened.entries[e].stored_size;
        for (auto& it : newitems) bound += SZ_AR_DIR_BOUND + it.name.size() + sz_stream_bound((size_t)it.size, 16);

        std::vector<uint8_t> out(bound);
        sz_archive_writer w;
        if (sz_aw_init(&w, out.data(), out.size()) == 0) {
            ok = true;
            size_t total_steps = a->opened.count + newitems.size(), step = 0;
            for (uint32_t e = 0; e < a->opened.count && ok; ++e) {
                const sz_ar_entry& en = a->opened.entries[e];
                if (!removed[e]) {
                    ok = sz_aw_add_blob(&w, en.name, en.is_dir, en.method, en.crc, en.orig_size,
                                        a->opened.buf.data() + en.offset, (size_t)en.stored_size) == 0;
                    sz_aw_set_mtime(&w, en.mtime);
                }
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
                sz_aw_set_mtime(&w, it.mtime);
                a->prog_done = ++step;
            }
            free(st);
            size_t n = sz_aw_finish(&w);
            ok = ok && n && sz::write_file_atomic(apath, out.data(), n);
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
                    MZ_TIME_T t = (MZ_TIME_T)(it.mtime / 1000000000);
                    MZ_TIME_T* tp = it.mtime != SZ_AR_NO_MTIME ? &t : nullptr;
                    std::string ex = sz::zip_time_extra(it.mtime);
                    mz_uint exn = (mz_uint)ex.size();
                    if (it.is_dir) {
                        std::string d = it.name + "/";
                        ok = mz_zip_writer_add_mem_ex_v2(&w2, d.c_str(), nullptr, 0, nullptr, 0, 0, 0, 0, tp,
                                                         ex.data(), exn, ex.data(), exn) != 0;
                    } else {
                        auto data = sz::read_file(it.abs, (size_t)it.size, 0);
                        int lv = (!call && sz::is_precompressed(it.name)) ? 0 : level;
                        ok = mz_zip_writer_add_mem_ex_v2(&w2, it.name.c_str(), data.data(), data.size(), nullptr, 0,
                                                         (mz_uint)lv, 0, 0, tp, ex.data(), exn, ex.data(), exn) != 0;
                    }
                }
                void* obuf = nullptr; size_t on = 0;
                if (ok) ok = mz_zip_writer_finalize_heap_archive(&w2, &obuf, &on) != 0;
                mz_zip_writer_end(&w2);
                mz_zip_reader_end(&r);
                if (ok) ok = sz::write_file_atomic(apath, (uint8_t*)obuf, on);
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

}
