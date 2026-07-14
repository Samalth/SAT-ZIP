Road to 1.0 (public-ready ordered by pain):
- [1] mtime in the .satz format + restore timestamps on extract (all formats), MUST land before the 1.0 format freeze
- [2] AVX2 crash on older CPUs: runtime dispatch or baseline build + clean CPU check at startup
- [3] Unicode paths end-to-end (W dialogs, UTF-8/UTF-16 correct through every layer)
- [4] Overwrite prompt on extract (overwrite/skip/rename) instead of silently overwriting
- [5] Clear message for encrypted archives ("password-protected not supported yet"); AES reading = post-1.0
- [6] Memory scaling: limit + clean error on oversized inputs (streaming = later); enable zip64 for >4GB zips
- [7] RAR extract-only (unrar freeware license?), 1.1 candidate
- update check in Info tab once the public release repo exists


- Always upgradable: UI changes to make it feel like it fits in win11 like; tab-blocks bigger, tool tips, open satz+add file+ add folder in 1 browse button
- Always upgradable: speed for zipping
