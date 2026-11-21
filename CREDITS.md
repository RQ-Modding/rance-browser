# Credits

Rance Browser is GPL-2.0-or-later (`COPYING`). It is built from these, each under its own license:

| What | By | License | Where |
| --- | --- | --- | --- |
| [libsys4](https://github.com/nunuhara/libsys4) | Nunuhara Cabbage and contributors | GPL-2.0-or-later | `subprojects/libsys4` (submodule): the archives (AFA, ALD, AAR, ALK, DLF) and the picture formats (QNT, PMS, WebP, JPEG, PNG and more) |
| [xsystem4](https://github.com/nunuhara/xsystem4)'s Flash reader | kichikuou | GPL-2.0-or-later | `src/core/swf.c`, `src/core/swf.h`, as in [RQ MOD](https://github.com/RQ-Modding/RQ-Mod)'s engine, with its fixes (each marked `RQ MOD` in the code) |
| xsystem4's Flash player | kichikuou (`parts/flash.c`), Nunuhara Cabbage (`parts/render.c`, the parts' shaders) | GPL-2.0-or-later | followed in `src/core/flash_player.c`, with RQ MOD's fixes to the timeline |
| RQ MOD's Flash checker | RQ Modding | GPL-2.0-or-later | `src/core/swf_check.c`, `src/core/swf_check.h`, a copy of RQ MOD's, so a file is judged as the game judges it |
| xsystem4's ways of finding a game's archives and reading `.bgi` and DTEX files | Nunuhara Cabbage, kichikuou | GPL-2.0-or-later | followed in `src/core/game.c` and `src/core/sets.c` |
| [cJSON](https://github.com/DaveGamble/cJSON) | Dave Gamble and contributors | MIT | `src/core/cJSON.c`, `src/core/cJSON.h` |
| [Dear ImGui 1.92.9](https://github.com/ocornut/imgui) | Omar Cornut and contributors | MIT | `third_party/cimgui/imgui` |
| [cimgui 1.92.9b](https://github.com/cimgui/cimgui) | Stephan Dilly and contributors | MIT | `third_party/cimgui` |
| [SDL2](https://www.libsdl.org/) | Sam Lantinga and contributors | zlib | linked in |
| [libsndfile](https://libsndfile.github.io/libsndfile/), with libogg, libvorbis, FLAC, Opus, mpg123 and LAME | their authors | LGPL-2.1-or-later and others | linked in |
| libjpeg-turbo, libwebp, libpng, zlib, libdeflate | their authors | BSD-style, zlib, MIT | linked in, through libsys4 |

The release zip carries each linked library's license in `licenses/`, as MSYS2 installs them.
MSYS2 installs no license file for libvorbis and LAME, so this repository carries them in
`licenses/`.

Rance Browser began as RQ Browser in [RQ MOD](https://github.com/RQ-Modding/RQ-Mod) (loader 2.43.0
to 2.55.0); its history came along with it.
