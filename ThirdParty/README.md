# Third-party dependencies

This directory contains source snapshots used by XM8 builds. Normal builds do
not download these dependencies. Exact versions, checksums, compile definitions,
and the rcheevos source manifest are recorded in `versions.json`.

## rcheevos

- Upstream: https://github.com/RetroAchievements/rcheevos
- Version: v12.3.0
- Commit: `e9ca3694c862b61235595176dac4b22677848c93`
- License: MIT
- Local changes: none

The complete upstream `include/` and `src/` trees are retained. The build uses
the source list in `versions.json` and does not compile `rc_libretro.c` or
`rc_client_external.c`.

## SQLite

- Upstream: https://www.sqlite.org/
- Version: 3.53.0
- Fossil source ID: `2026-04-09 11:41:38 4525003a53a7fc63ca75c59b22c79608659ca12f0131f52c18637f829977f20b`
- License: Public Domain
- Local changes: none

Only the official `sqlite3.c` and `sqlite3.h` amalgamation files are retained.

## stb_image

- Upstream: https://github.com/nothings/stb
- Version: stb_image 2.30
- Commit: `31c1ad37456438565541f4919958214b6e762fb4`
- License: MIT OR Public Domain
- Local changes: none

Only `stb_image.h` is retained. XM8 compiles one implementation translation unit
with PNG and JPEG enabled and file I/O, HDR, and linear-float APIs disabled.

## License copies

`licenses/` contains the notices shipped in RA-enabled macOS packages:

- `XM8-GPL-2.0.txt`: repository root license text
- `SDL2-zlib.txt`: SDL2 license text
- `xBRZ-GPL-3.0.txt`: bundled xBRZ license text
- `rcheevos-MIT.txt`: rcheevos license text
- `stb-MIT-or-Public-Domain.txt`: stb dual-license text
- `SQLite-Public-Domain.txt`: SQLite public-domain notice and source reference

The repository contains both a GPLv2 root license and GPLv3 notices on xBRZ.
This manifest preserves both notices and does not assert that the combined
distribution is GPLv2-only. Resolving the distribution-wide legal
characterization requires project-owner or legal review.
