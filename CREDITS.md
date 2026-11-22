# Credits

Rance Browser is GPL-2.0-or-later (`COPYING`). It is built from these, each under its own license:

| What | By | License | Where |
| --- | --- | --- | --- |
| [libsys4](https://github.com/nunuhara/libsys4) | Nunuhara Cabbage and contributors | GPL-2.0-or-later | `subprojects/libsys4` (submodule): the archives (AFA, ALD, AAR, ALK, DLF) and the picture formats (QNT, PMS, WebP, JPEG, PNG and more) |
| [xsystem4](https://github.com/nunuhara/xsystem4)'s Flash reader | kichikuou | GPL-2.0-or-later | `src/core/swf.c`, `src/core/swf.h`, as in [RQ MOD](https://github.com/RQ-Modding/RQ-Mod)'s engine, with its fixes (each marked `RQ MOD` in the code) |
| xsystem4's Flash player | kichikuou (`parts/flash.c`), Nunuhara Cabbage (`parts/render.c`, the parts' shaders) | GPL-2.0-or-later | followed in `src/core/flash_player.c`, with RQ MOD's fixes to the timeline |
| RQ MOD's Flash checker | RQ Modding | GPL-2.0-or-later | `src/core/swf_check.c`, `src/core/swf_check.h`, a copy of RQ MOD's, so a file is judged as the game judges it |
| RQ MOD's PNG reader and its rules for a mod's picture on a 3D texture | RQ Modding | GPL-2.0-or-later | `src/core/rq_pixels.c`, `src/core/rq_pixels.h`, `src/core/3d/rq_textures.c`, `src/core/3d/rq_textures.h`, copies of RQ MOD's, so a PNG tried on a model is read, checked and drawn as the game would |
| xsystem4's ways of finding a game's archives and reading `.bgi` and DTEX files | Nunuhara Cabbage, kichikuou | GPL-2.0-or-later | followed in `src/core/game.c` and `src/core/sets.c` |
| xsystem4's 3D renderer (ReignEngine, TapirEngine) | kichikuou, with Nunuhara Cabbage; `gfx/types.h` Masaki Chikama and Nunuhara Cabbage | GPL-2.0-or-later | `src/core/3d`, a copy of RQ MOD's engine (the files `src/core/COPIES` names), so a model is drawn as the game draws it; the browser's own files there stand in for the rest of the engine |
| [cglm 0.9.7](https://github.com/recp/cglm) | Recep Aslantas and contributors | MIT | `subprojects/cglm.wrap`, which meson fetches: the 3D renderer's maths |
| [cJSON](https://github.com/DaveGamble/cJSON) | Dave Gamble and contributors | MIT | `src/core/cJSON.c`, `src/core/cJSON.h` |
| [Dear ImGui 1.92.9](https://github.com/ocornut/imgui) | Omar Cornut and contributors | MIT | `third_party/cimgui/imgui` |
| [cimgui 1.92.9b](https://github.com/cimgui/cimgui) | Stephan Dilly and contributors | MIT | `third_party/cimgui` |
| [SDL2](https://www.libsdl.org/) | Sam Lantinga and contributors | zlib | linked in |
| [libsndfile](https://libsndfile.github.io/libsndfile/), with libogg, libvorbis, FLAC, Opus, mpg123 and LAME | their authors | LGPL-2.1-or-later and others | linked in |
| libpng | its authors | libpng license | linked in, for RQ MOD's PNG reader (`src/core/rq_pixels.c`) as well as through libsys4 |
| libjpeg-turbo, libwebp, zlib, libdeflate | their authors | BSD-style, zlib, MIT | linked in, through libsys4 |

The release zip carries each linked library's license in `licenses/`, as MSYS2 installs them, and
cglm's from its checkout. MSYS2 installs no license file for libvorbis and LAME, so this repository
carries them in `licenses/`.

Rance Browser began as RQ Browser in [RQ MOD](https://github.com/RQ-Modding/RQ-Mod) (loader 2.43.0
to 2.55.0); its history came along with it.
