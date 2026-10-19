#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "headless_jobs.hpp"
#include "satzip_ops.hpp"
#include "satzip_zip.hpp"
#include "satzip_7z.hpp"
#include "satzip_7z_write.hpp"
#include "satzip_tar.hpp"
#include "satzip_xz.hpp"
#include "satzip_any.hpp"
#include "satzip_settings.hpp"

namespace fs = std::filesystem;

namespace szgui {

void headless_zip_job(const char* path, HeadlessJob& j) {
    sz::Progress prog = [&j](size_t d, size_t t) { j.done = d; j.total = t; };
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

    j.dest = std::string(path) + fmt_ext;
    std::error_code ec;
    if (cfg.zipnow_folder && fs::is_regular_file(src, ec)) {
        std::string stem = src.stem().string();
        for (auto& it : items) it.name = stem + "/" + it.name;
        items.insert(items.begin(), { src.parent_path(), stem, true, 0 });
        j.dest = src.parent_path() / (stem + fmt_ext);
    }
    for (auto& it : items) if (!it.is_dir) { ++j.files; j.raw += it.size; }

    std::vector<uint8_t> out;
    size_t n = 0;
#if defined(SATZIP_HAVE_ZIP)
    if (cfg.format == 1) n = sz::build_zip(items, out, cfg.level, cfg.compress_all, cfg.threads, prog, &j.cancel);
    else
#endif
#if defined(SATZIP_HAVE_7Z)
    if (cfg.format == 3) n = sz::sevenz_write(items, out, cfg.level, prog, &j.cancel);
    else if (cfg.format == 4) n = sz::build_tar_xz(items, out, cfg.level, prog, &j.cancel);
    else
#endif
    if (cfg.format == 2) n = sz::build_tar(items, out, prog);
    else n = sz::build_archive(items, out, accel, cfg.threads, cfg.compress_all, cfg.mode, prog, &j.cancel);

    if (!j.cancel) {
        if (!n) j.error = "Compression failed.";
        else if (!sz::write_file_atomic(j.dest, out.data(), n))
            j.error = "The archive could not be written (disk full or no access to the folder?).";
        else j.packed = n;
    }
    j.finished = true;
}

void headless_extract_job(const char* archive, bool subfolder, HeadlessJob& j) {
    fs::path ap(archive);
    j.dest = sz::default_extract_dir(ap, subfolder);
    sz::ExtractResult r = sz::extract_any(ap, j.dest, [&j](size_t d, size_t t) { j.done = d; j.total = t; }, &j.cancel);
    j.files = r.files;
    j.bad = r.bad;
    j.error = std::move(r.error);
    j.failed = std::move(r.failed);
    j.finished = true;
}

}
