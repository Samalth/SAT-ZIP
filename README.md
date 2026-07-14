# SAT-ZIP

**SAT-ZIP is a fast desktop archiver for Windows**, powered by a custom high-throughput LZ
codec whose decompression beats LZ4. It reads **and** writes `.satz`, `.zip`, `.7z`, `.tar`
and `.tar.xz`, integrates into the Explorer right-click menu, and ships with a built-in
benchmark lab so you can verify the speed yourself.

The app is the product; under the hood it ships the SAT-ZIP codec, a `satzip` command-line
tool, and a C SDK.

## Formats

| Format | Read | Write | Notes |
|---|---|---|---|
| `.satz` | ✔ | ✔ | native format, modes S/R/T, multithreaded, per-file CRC32 |
| `.zip` | ✔ | ✔ | multithreaded DEFLATE - faster than Windows' built-in zipping |
| `.7z` | ✔ | ✔ | LZMA solid; no PPMd/AES archives |
| `.tar` | ✔ | ✔ | ustar + GNU longnames |
| `.tar.xz` | ✔ | ✔ | LZMA2 in a standard xz container |

RAR is not supported (creating RAR archives is not licensable).

## Install

**Windows** - run `SATZ-Setup-<ver>.exe` (Inno Setup: `.satz` association, right-click
menus, optional desktop icon). Everything installs per-user, no elevation needed.

**Build the installer yourself** - needs CMake ≥ 3.18, a C/C++ toolchain and
[Inno Setup](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`):
```
powershell -ExecutionPolicy Bypass -File build_installer.ps1
```
→ `installer/dist/SATZ-Setup-<ver>.exe`. Third-party sources (Dear ImGui, ImPlot, GLFW,
miniz, LZ4, LZMA SDK) are fetched automatically at configure time into `build/`.

A portable build is also available: `cmake --build build --config Release --target package`.

## The app

Four tabs:

- **Archive** - drag files/folders in (or *Browse…*) to stage them, then **Create Archive**
  in any supported format. Open an archive (*Open Archive…*, drag-drop, or double-click a
  `.satz`) to browse it: search, sort, double-click an entry to open it, extract all or a
  selection, add/remove files in place. Options: compression level, threads, skip
  already-compressed files, encrypt via [SAT-OBELISK](https://github.com/Samalth/SAT-OBELISK)
  (if installed), email the result.
- **Benchmark** - live per-block compress/decompress MB/s for SAT-ZIP S/R/T, LZ4, zip
  (deflate) and LZMA on your own files, folders or synthetic corpora, at any level/thread
  count.
- **Settings** - persisted defaults (format, level, threads, mode, theme, window).
- **Info** - version, license, FAQ, repo links.

Long operations run on worker threads with a progress bar and a Cancel button.

### Compression modes
- **Mode S - Fast**: single-probe hash table, skip acceleration. Fastest.
- **Mode R - Better ratio** (default): hash-chain match finder with lazy matching; search
  depth scales with the compression level. Same format and decode speed as S.
- **Mode T - Max ratio**: Mode R plus a split-stream rANS entropy stage. Best ratio,
  slower to compress and extract.

## Command line
```
satzip a <archive> <paths...>   # create (format chosen by extension: .satz/.zip/.tar/.7z/.tar.xz)
satzip x <archive> [destdir]    # extract any supported archive
satzip l <archive>              # list contents
satzip c <in> <out>             # compress a single file to a raw .satz stream
satzip d <in> <out>             # decompress a single stream
satzip --register               # install per-user Explorer integration
satzip --unregister             # remove it
```
Incompressible data is stored raw (framing overhead only). Extraction verifies CRCs and
rejects unsafe (absolute / `..`) paths.

## Explorer integration
One cascading **SAT-ZIP** menu (installed per-user, HKCU):
- on files/folders: **Add to archive**, **Zip now**, **Zip now & email**
- on archives: **Open in SAT-ZIP**, **Extract Here**, **Extract to folder**

On Windows 11 the menu appears under **Show more options**.

## For developers

### Build & test
```
cmake -S . -B build -A x64 -DSATZIP_BUILD_GUI=ON
cmake --build build --config Release
./build/Release/roundtrip               # round-trip + fuzz; expect: ... 0 FAILED
./build/Release/profile <file> [s|r|t]  # per-block ratio/MB/s
./build/Release/benchmark [file]        # CLI head-to-head vs LZ4
```

### Project layout
| Path | What |
|---|---|
| `include/satzip.h` | core codec API, tunables, shared SIMD/scalar helpers |
| `src/satzip_encode.c` / `satzip_encode_r.c` | Mode S / Mode R encoders (shared emit via `satzip_emit.h`) |
| `src/satzip_decode.c` | block decoder (used by every mode) |
| `src/satzip_tans.c` / `satzip_split.c` | rANS entropy coder + split-stream stage (Mode T) |
| `src/satzip_stream.c` / `satzip_archive.c` | framed container + multi-file archive |
| `app/common/` | shared C++ ops: archive build/extract, zip/7z/tar/xz, settings, mail/share, OBELISK |
| `app/cli/satzip.cpp` | CLI + Explorer registration |
| `app/gui/main.cpp` | Dear ImGui + ImPlot desktop UI |
| `test/` | round-trip/fuzz harness, per-block profiler, CLI benchmark |

## Requirements
Windows 10/11, x64 with AVX2 (runtime dispatch for older CPUs is on the roadmap - see
`TODO.md`, "Road to 1.0").

## License
Apache License 2.0 - see [LICENSE](LICENSE).
