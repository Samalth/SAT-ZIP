Road to 1.0 (public-ready ordered by pain):
- [2b] Verify the AVX2/SSE2 runtime dispatch under Intel SDE (-wsm/-snb/-hsw) once SDE is downloaded
- [4] Overwrite prompt on extract (overwrite/skip/rename) instead of silently overwriting
- [5] Clear message for encrypted archives ("password-protected not supported yet"); AES reading = post-1.0
- [6] Memory scaling: limit + clean error on oversized inputs (streaming = later); enable zip64 for >4GB zips
- [7] RAR extract-only (unrar freeware license?), 1.1 candidate
- update check in Info tab once the public release repo exists

Now:
- Mode T: consistently better ratio than LZMA (at 64 KiB blocks T reaches 78-81% of LZMA; whole-file LZMA is far ahead on logs/json)
    . gap sources measured: optimal parsing ~10%, entropy coding (adaptive, rep-offsets, matched literals) ~12%, window size 3-5x on logs/json
    . plan: large window (MBs) with long offsets, price-based optimal parsing, adaptive context-modelled range coder


Long Run:
- Native ARM64 build + NEON codec variant (x64 build already runs under Windows-on-ARM emulation)
- "Modified" column in the archive list (times are now stored in every format)
- Always upgradable: UI changes to make it feel like it fits in win11 like; tab-blocks bigger, tool tips, open satz+add file+ add folder in 1 browse button
- Always upgradable: speed for zipping
