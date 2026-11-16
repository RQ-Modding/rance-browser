# Dear ImGui through cimgui

Rance Browser draws its window with [Dear ImGui](https://github.com/ocornut/imgui), called from C
through [cimgui](https://github.com/cimgui/cimgui). Both are MIT (`LICENSE`, `imgui/LICENSE.txt`).

| | Version | Commit |
| --- | --- | --- |
| cimgui | tag `1.92.9b` | `bb4d4848b759e037a6cc26d23be0387a148ac39f` |
| Dear ImGui (cimgui's `imgui` submodule at that tag) | 1.92.9 | `f1cc2ae15e53a861a874c3034aae6798fde194ab` |

## What is here

- From cimgui: `cimgui.cpp`, `cimgui.h`, `cimgui_impl.cpp`, `cimgui_impl.h`, `cimconfig.h`, `LICENSE`.
- From Dear ImGui: the core (`imgui*.cpp`, `imgui*.h`, `imstb_*.h`, `imconfig.h`, `LICENSE.txt`) and two
  backends in `imgui/backends`: SDL2 (`imgui_impl_sdl2.*`) and OpenGL 3 (`imgui_impl_opengl3.*` with
  its loader).

The files are as they come from those commits, unchanged.

## How the browser builds them

`meson.build` builds a static library from the ImGui sources, `cimgui.cpp` and
`src/cimgui_glue.cpp`, with `-DCIMGUI_NO_EXPORT -DCIMGUI_USE_SDL2 -DCIMGUI_USE_OPENGL3
-DIMGUI_DISABLE_DEMO_WINDOWS`. The glue file compiles the two backends with
`IMGUI_IMPL_API extern "C"` so the browser's C code calls them through `cimgui_impl.h`.
`cimgui_impl.cpp` is left out: it only adds Vulkan helpers, and in 1.92.9b its header redeclares the
OpenGL 3 backend's inline `ImGui_ImplOpenGL3_GetRenderState`, which C++ refuses.

ImGui 1.92.9's OpenGL 3 backend binds its own sampler (smooth, clamped) for every texture, so the
browser draws its checkerboard as rectangles and switches to the backend's nearest sampler
(`DrawCallback_SetSamplerNearest`) for a picture zoomed to 200 % or more.

## Updating

Take a cimgui tag and its `imgui` submodule, copy the same files over these, update the table above,
build, and run `rance-browser --check` and a `--shot` of each tab. cimgui's C names carry
suffixes (`igBeginChild_Str`, `igGetIO_Nil`) that can change between versions.
