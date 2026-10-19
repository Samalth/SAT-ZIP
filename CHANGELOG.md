# Changelog

## 0.6.0
- File dates are kept: archives store modification times and extracting restores them, in every format
- .tar archives no longer give your files a 1970 date
- New .satz format version with room for extra metadata. Older archives still open
- Runs on CPUs without AVX2 (picks AVX2 or SSE2 at startup, no slowdown on newer CPUs)
- Unicode file names work everywhere. Japanese names or emoji used to crash the app
- Old zips made by Windows Explorer show accented names correctly
- Extract and Zip now from Explorer show a progress window with Cancel
- .7z and .tar.xz progress follows the real compression and can be cancelled
- Archives are written to a temp file first, so a crash or full disk can't damage an existing one

- Fixed: `satzip a out.satz folder` with a relative path made an unusable archive
- Fixed: right-click extract on .tar and .xz did nothing after installing
- Fixed: folders inside zips showed up as `name//`

## 0.5.0
- Open .tar, .xz and .tar.xz archives
- Open Archive button on the Archive tab
- "Open in SAT-ZIP" in the right-click menu for all supported archives
- The compression level now actually affects Mode R and T
- The save dialog keeps the extension of the format you picked
- Progress only shows 100% once the archive is on disk

## 0.4.0
- Create .7z and .tar.xz archives, so every supported format can now be read and written
- Email works with the new Outlook (through the Windows Share sheet)
- One summary of the files that failed to extract instead of an error per file
- Tab and theme fixes

## 0.3.0
- Better Mode T ratio (each part of the stream gets its own entropy coding)
- Default archive format in Settings
- .zip create and extract use all cores
- Add or remove files in an existing archive
- Search inside an archive, double-click a file to open it
- Cancel button for long operations
- "Zip now & email" in the right-click menu
- Crash log in %APPDATA%\SAT-ZIP
- Mode R finds better matches
- Icon buttons, layout works on small windows

## 0.2.0
- Compression levels and multithreaded compression
- Already compressed files (jpg, mp4, zip...) are stored instead of recompressed
- Mode R: better ratio, same extraction speed. Now the default
- Mode T: smallest files, slower
- Settings tab, dark and light theme
- Open .zip and .7z, create .zip and .tar
- Right-click menu grouped under one SAT-ZIP entry, plus "Zip now"
- Encrypt after creating, through SAT-OBELISK if it is installed
- Email the archive right after creating it
- Benchmark tab: SAT-ZIP against LZ4 and zip on your own files or folders
- Info tab

## 0.1.0
- First release: fast LZ compressor (Mode S) with AVX2
- .satz archives with a CRC32 per file
- Desktop app to create, open and extract archives, plus a benchmark
- Command line tool
- Right-click integration in Windows Explorer
- Windows installer
- Apache-2.0 license