#pragma once

#include "satzip_ops.hpp"
#include "satzip_zip.hpp"
#include "satzip_7z.hpp"
#include "satzip_tar.hpp"
#include "satzip_xz.hpp"

namespace sz {

inline bool is_tar_or_xz(const fs::path& p) {
    bool xz = false;
#if defined(SATZIP_HAVE_7Z)
    xz = is_xz_file(p);
#endif
    return xz || p.extension() == ".tar";
}

inline bool load_tar_bytes(const fs::path& p, std::vector<uint8_t>& tb) {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    if (ec) return false;
    auto raw = read_file(p, (size_t)n, 0);
#if defined(SATZIP_HAVE_7Z)
    if (is_xz_data(raw.data(), raw.size())) return xz_decompress(raw.data(), raw.size(), tb);
#endif
    tb = std::move(raw);
    return true;
}

inline fs::path default_extract_dir(const fs::path& archive, bool subfolder) {
    fs::path dest = archive.parent_path();
    if (subfolder) {
        fs::path stem = archive.stem();
        if (stem.extension() == ".tar") stem = stem.stem();
        dest /= stem;
    }
    if (dest.empty()) dest = fs::path(".");
    return dest;
}

struct ExtractResult { int files = 0; int bad = 0; std::string error; std::vector<std::string> failed; };

inline ExtractResult extract_any(const fs::path& ap, const fs::path& dest, const Progress& progress = {},
                                 const std::atomic<bool>* cancel = nullptr) {
    ExtractResult r;
#if defined(SATZIP_HAVE_ZIP)
    if (is_zip_file(ap)) {
        r.files = zip_extract(ap, dest, nullptr, 0, progress, &r.bad, cancel, &r.failed);
        return r;
    }
#endif
#if defined(SATZIP_HAVE_7Z)
    if (is_7z_file(ap)) {
        r.files = sevenz_extract(ap, dest, nullptr, progress, &r.bad, cancel, &r.failed);
        return r;
    }
#endif
    if (is_tar_or_xz(ap)) {
        std::vector<uint8_t> tb;
        std::vector<TarEntry> tes;
        if (!load_tar_bytes(ap, tb) || !tar_list(tb.data(), tb.size(), tes)) {
            r.error = "The archive could not be read (damaged or not a tar archive).";
            return r;
        }
        r.files = tar_extract(tb.data(), tb.size(), tes, dest, nullptr, progress, &r.bad, cancel, &r.failed);
        return r;
    }
    Archive a;
    if (!open_archive(ap, a)) {
        r.error = "Not a SAT-ZIP archive.";
        return r;
    }
    r.files = extract(a, dest, nullptr, 0, progress, &r.bad, cancel, &r.failed);
    return r;
}

}
