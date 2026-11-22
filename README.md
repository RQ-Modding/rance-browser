# Rance Browser

<img width="1920" height="1020" alt="image" src="https://github.com/user-attachments/assets/57e9454a-48ce-4995-a33e-8c70f4307eca" />


A window over the files of AliceSoft's System 4 games, for players, wiki writers and mod creators.
Point it at a game you own and it lists the game's CGs, Flash animations, 3D models, music, sounds
and other pictures. You can look at each one, play it, and save it as PNG or OGG, or a 3D model as a
glTF file for Blender. It reads the game's archives without changing them and needs neither the game
running nor anything installed.

It began as RQ Browser, the file browser of [RQ MOD](https://github.com/RQ-Modding/RQ-Mod) (a mod
loader for Rance Quest Magnum), and RQ MOD's package carries it too.

Rance Browser is a fan project. It is not made or endorsed by AliceSoft or MangaGamer, and it
contains nothing of their games: it reads the copy on your own computer.

## Games

| Game | State |
| --- | --- |
| Rance Quest Magnum | Tested. Also shows RQ MOD's names for characters, items and music, and the Lua line a mod uses for each file. |
| Rance VI - Collapse of Zeth (MangaGamer's English release) | Tested. |
| Sengoku Rance | Tested. |
| Rance 01: Quest for Hikari | Tested. |
| Rance 02: The Rebellious Maidens | Tested. |
| Rance 03: Leazas Kanraku | Tested. Its music with each track's loop, and its voices. |
| Other System 4 games (Rance IX, Rance X, and AliceSoft's other System 4 games) | Open, marked **not tested**: the browser finds their files by the rules every System 4 game follows, but nobody has checked them yet. Some kinds of file may be missing or read wrong. |

Games on older engines (System 3 and before: Rance 1 to 5D, Kichikuou Rance) do not open.

`games/README.md` says how a game becomes tested.

## What it shows

Each tab appears when the game has that kind of file.

| Tab | What | Where the game keeps it |
| --- | --- | --- |
| **CGs** | Every picture of the CG archive by its number: characters, scenes, backgrounds, the interface, effects. A grid or one at a time, with zoom. | `<game>CG.afa`, or `<game>GA.ald` to `<game>GZ.ald` |
| **Flash** | The game's Flash animations (Rance Quest's battle effects, marks and cut-ins), played as the game plays them, with what the game makes of each file and the pictures and sounds inside it. A `.swf` or `.aff` from disk too. | `<game>AFF.afa` |
| **3D models** | Rance Quest's 3D models (its monsters and figures, rooms, fight backgrounds and props), drawn as the game draws them, with their motions; a monster on one of the game's fight backgrounds with its shadow. Each one exports as a glTF file (`.glb`). | `Data\ReignData.red` |
| **Music** | The tracks, with a player that loops each one where the game does. | `<game>BA.ald` or `.afa`, with the `.bgi`; or `Sound.afa` with its table of tracks (Rance 03) |
| **Sounds** | The sound effects. | `<game>WA.ald`, `WA.afa` or `Sound.afa` |
| **Voices** | The voices, by number (Rance 03's). | `<game>Voice.afa` |
| **3D textures** | The textures of the 3D models (Rance Quest), with RQ MOD's line that changes one. | `Data\ReignData.red` |
| **Other pictures** | Pictures kept in `.alk` files (Rance VI's event pictures, the AliceSoft logo). | `*.alk` |
| **Dungeon textures** | Each dungeon's walls, floors, ceilings, stairs, doors, sky and light maps (Rance VI). | a `.dlf` file |
| **Names** | Rance Quest only: RQ MOD's names, with a picture where one is known (a monster's is its 3D model, which opens in the 3D tab). | built in |

Every picture saves as PNG, alone or all the ones a search shows at once. Music and sounds save as the
OGG the game stores. A picture whose see-through is kept in a separate mask (Rance VI's battle
effects) shows and saves with that mask, so its see-through parts stay see-through.

## Using it

1. Download `RanceBrowser-<version>-windows.zip` from the
   [Releases](https://github.com/RQ-Modding/rance-browser/releases) page and unzip it anywhere.
2. Run `rance-browser.exe` and pick the game's folder (the one with `System40.ini` or
   `AliceStart.ini`). It remembers the folder; **Open another game...** at the bottom switches.

It also finds the game without asking when it sits in the game's folder, or in a folder inside it
(RQ MOD puts it in `xsystem4\`).

- **Keys:** Up and Down (or Left and Right) step through a list, Page Up and Page Down by ten, Home
  and End to its ends; Space plays or pauses a track, a sound or a Flash file; Comma and Period step
  a Flash file a frame back or on.
- **Search:** a number, a part of a file name, or a group's name.
- **Groups:** the box above each list. For a game with a profile, the CGs' groups have English labels
  (Rance VI's "Standing pictures", "Scene CGs", "Backgrounds"...); otherwise they are the archive's own
  folders or ranges of a thousand numbers. Music, sounds and voices have the archive's folders when
  it has some (Rance 03's), and voices a folder per thousand.

Nothing is written into the game's folder. The program keeps a small `rance-browser.json` beside
itself, with the last game folder and the last folder you saved into.

### Flash files

The Flash tab plays a file as [RQ MOD](https://github.com/RQ-Modding/RQ-Mod)'s engine plays it: the
frames, the actions (stop, play, gotoFrame), the sounds at their frames, the blend modes and the colour
changes. Rance Quest plays every Flash file at 30 frames a second, whatever the file says, and so does
the tab; another game's files play at their own rate.

On the right, **What the game makes of it** checks the file by RQ MOD's rules: whether the game plays
it, plays it with warnings, or would not play it (the engine closes on some files), each problem with
its frame, its tag and what to do. A file the game would not play is not played here either.

**Open a .swf or .aff file...** above the list, or a file dropped on the window, opens a file from
disk: a mod's own animation, made with RQ MOD's `rqmod-aff`, can be checked and watched before it goes
into a mod. **Save this frame as PNG...** saves the frame shown.

### 3D models

The 3D tab draws Rance Quest's models with a copy of RQ MOD's 3D renderer, so a model looks as it does
in the game: its textures and light maps, its outline, and its motions at the game's 30 frames a
second. A monster plays the motions a fight plays it with, by the game's own table; every motion file
of its folder also plays whole. Left drag turns the camera, right drag moves it, the wheel comes
closer. **Bones** and **Walk mesh** draw the skeleton and the floor a room's characters walk on. A
monster can stand on one of the game's fight backgrounds, with its shadow and, with **Fight camera**,
seen as a fight sees it. **Save this frame as PNG...** saves the view, see-through where nothing is
drawn.

**Its textures**, on the right, lists the textures the model's materials use, as RQ MOD lists them:
each one's maps (colour, alpha, light...), size, alpha and frames. **Save as PNG...** saves the game's
picture and **Save the UV map...** the triangles the model draws with it, white on see-through at the
picture's size, to paint over. **Try a PNG...** draws a PNG of yours on the model as RQ MOD would
(a texture with frames takes one per frame, picked together, in their names' order), keeping the
camera and the motion; a file RQ MOD would refuse is refused there, in RQ MOD's words, and **The
game's** takes the try away. **Colours...** turns the game's own picture by three numbers, hue,
saturation and lightness, as RQ MOD's colours by numbers do, and loads the model again when a slider
is let go; **Every colour texture: Colours...** puts them on each colour texture of the model. For Rance Quest the Lua lines follow: `rq.image.texture` for every model
that draws the texture, and `rq.monster.model` with `textures` for a monster of a mod's own. The 3D
textures tab shows the `rq.image.texture` line of each picture a model draws. RQ MOD's guide
[A monster in new colours](https://rq-modding.github.io/rq-modding-docs/guide/model-textures) paints
one, step by step.

**Export the model as glTF (.glb)...** saves the model for Blender and other 3D programs: its meshes,
its textures (as PNG, inside the file), its materials, its skeleton and each point's weights, each
motion as an animation, a room's walk mesh as an object of its own, and what glTF has no place for as
properties named `rq_*`. RQ MOD's guide
[Game models in Blender](https://rq-modding.github.io/rq-modding-docs/guide/game-models) opens one.

The tab needs OpenGL 3.1; on a PC with less it says so, and the other tabs work. It shows when the
game has Rance Quest's 3D files.

### On the command line

```
rance-browser [--game DIR] [--scale F] [FILE.aff|FILE.swf]
rance-browser --check [--game DIR] [--out FILE]
rance-browser --shot TAB FILE.png [--game DIR] [--select N|NAME] [--grid] [--frame N] [--file PATH]
              [--motion NAME] [--fight BG] [--fight-camera] [--walk] [--bones] [--size WxH] [--scale F]
              [--texture NAME=FILE.png[,...]]... [--colors NAME=H[,S[,L]]]... [--view-only]
rance-browser --export OUT.glb --select NAME [--game DIR] [--texture NAME=FILE.png[,...]]...
              [--colors NAME=H[,S[,L]]]...
rance-browser --export DIR (--select NAME | --kind KIND | --all) [--game DIR]
rance-browser --uv-map NAME OUT.png --select MODEL [--game DIR]
rance-browser --save-texture NAME OUT.png --select MODEL [--game DIR]
rance-browser --decode FILE.png OUT.rgba     (or --decode-libsys4)
rance-browser --recolor FILE.png OUT.rgba HUE[,SATURATION[,LIGHTNESS]]
```

- A `.aff` or `.swf` path opens that file in the Flash tab.
- `--check` opens every archive, decodes the first, middle and last picture of each group, reads
  every track's loop and every sound, the first, middle and last voice of each folder, checks every
  Flash file and plays it to its end, reads every 3D model and motion and draws each model through
  its motions and each texture's UV map, and prints the counts as JSON (exit 1 when something
  failed).
- `--shot` draws one tab without a window and saves it: `cgs`, `flash`, `music`, `sounds`, `voices`,
  `3d`, `textures`, `pictures`, `dungeons` or `names`. In the Flash tab, `--select` also takes a file's name,
  `--frame` the frame to draw (from 1) and `--file` a file from disk. In the 3D tab, `--select` takes
  a model's folder (`gokiburi`, or `Chara/gokiburi` when two kinds have one of that name) or a
  monster's model number from the game's data (`100122`, with the motions of its entry in the game's
  monster table), `--motion` a motion file of its folder (or `none`), `--frame` the motion's
  frame, `--fight` a fight background's folder (`bgA_00_douk`) and `--fight-camera` the fight's camera;
  `--walk` and `--bones` draw the walk mesh and the skeleton. `--texture NAME=FILE.png` tries a PNG
  on the model's texture NAME (a texture with frames: a PNG per frame, in order, after the `=`), as
  often as there are textures to try (64 `--texture` and `--colors` in all); `--colors NAME=HUE[,SATURATION[,LIGHTNESS]]` puts colours by
  numbers on it (hue -360 to 360 degrees, saturation and lightness 0 to 4, each 1 when left out; NAME
  `*` for every colour texture of the model); `--view-only` saves the 3D view's frame alone,
  see-through where nothing is drawn, instead of the window.
- `--export` saves a model as a `.glb` file, or a folder of them: `--kind` takes `chara`, `map`, `bg`
  or `etc` (or a list, `chara,map`) and `--all` takes every model, each written as
  `DIR\<Kind>\<folder>.glb`, with `DIR\export.json`: what each file holds and points of each model as
  the game's renderer places them, to check the files against in another program. With one model,
  `--texture` and `--colors` try PNGs and colours on it as for `--shot 3d`.
- `--uv-map NAME OUT.png` saves the UV map of the texture NAME of the model `--select` names (a folder
  or `Kind/folder`), and `--save-texture NAME OUT.png` the game's picture (its first frame), without a
  window; each prints the picture's size.
- `--decode FILE.png OUT.rgba` reads a PNG with RQ MOD's reader and writes its pixels raw (8-bit RGBA,
  rows from the top), printing `WxH alpha=0|1`; `--decode-libsys4` reads it with libsys4's; `--recolor
  FILE.png OUT.rgba HUE,SATURATION,LIGHTNESS` reads it with RQ MOD's reader and turns its colours by
  RQ MOD's rule. For checks.
- `--scale` sets the size of the text and the layout (1 to 4; by default the screen's scale).
- The exit code is 0 when it worked, 1 when it did not (a PNG RQ MOD would refuse, with RQ MOD's text
  on stderr, as for colours out of range) and 2 for a command line that does not make sense.

## How a game is found

A System 4 game's ini (`System40.ini` or `AliceStart.ini`) names its code file, `CodeName =
"Rance6.ain"`. The game's archives start with that name: `Rance6GA.ald` (the CGs, volume A),
`Rance6BA.ald` (the music), `Rance6WA.ald` (the sounds), and so on. The browser finds them the same
way the engine does ([xsystem4](https://github.com/nunuhara/xsystem4)'s asset manager), and reads them
with [libsys4](https://github.com/nunuhara/libsys4), the library xsystem4 reads them with. A picture or
a loop point therefore reads as the game reads it.

## Building

Windows, with [MSYS2](https://www.msys2.org/) in its UCRT64 shell:

```
pacman -S --needed base-devel git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-meson \
    mingw-w64-ucrt-x86_64-pkg-config mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-SDL2 \
    mingw-w64-ucrt-x86_64-libsndfile mingw-w64-ucrt-x86_64-libjpeg-turbo mingw-w64-ucrt-x86_64-libwebp \
    mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-libdeflate
git clone --recurse-submodules https://github.com/RQ-Modding/rance-browser.git
cd rance-browser
meson setup build --buildtype=release
ninja -C build
```

The program is `build/rance-browser.exe`, linked statically: it needs only Windows' own DLLs.

The 3D tab draws with a copy of RQ MOD's 3D renderer (`src/core/3d`) on
[cglm](https://github.com/recp/cglm), which `meson setup` fetches from its wrap
(`subprojects/cglm.wrap`). With `-D3d=disabled`, or when the default `auto` finds no cglm, the
browser builds without the 3D tab; the releases build with `-D3d=enabled`.

libjpeg-turbo 3.2.1 cannot be linked statically beside zlib (both define `z_errmsg`). Until that is
fixed, install 3.2.0:
`pacman -U https://repo.msys2.org/mingw/ucrt64/mingw-w64-ucrt-x86_64-libjpeg-turbo-3.2.0-1-any.pkg.tar.zst`.

**Inside RQ MOD.** RQ MOD has this repository as a submodule and builds it as a meson subproject
with two options: `rq_ids_dir` (its own names, `data/ids`) and `rq_lua` (its `rq.lua`, to check that
each Lua line the browser offers names a real call). Built alone, the browser uses the copy of the
names in `games/rq-ids` and checks no Lua lines.

**Its readers as a library.** What reads a game (its profile, the game folder, the CGs, the Flash
files, the music and sounds, the other pictures, RQ MOD's names, the thumbnails), the player, the mixer
and the Flash player (with RQ MOD's Flash checker) are the static library `rance_core`, in `src/core`,
described by `src/core/core.h`. Another program takes it
as a meson subproject, with cimgui in it (the 3D renderer is apart, the library `rance_3d`, so it
takes no 3D code):

```
rance_core_dep = subproject('rance-browser').get_variable('rance_core_dep')
```

The program gives the readers the display's scale and where their messages go (`CORE` in `core.h`),
and calls `pics_frame()` once a frame.

## The repository

| Folder | What |
| --- | --- |
| `src` | The window, in C. `browser.h` describes its parts. |
| `src/core` | The readers, the library `rance_core`. `core.h` describes them. |
| `src/core/3d` | The 3D view, the library `rance_3d`: a copy of RQ MOD's 3D renderer, and `rb3d.c` in the place of the rest of the engine. `src/core/COPIES` names each file copied from RQ MOD. |
| `games` | The game profiles (`games/README.md`) and the copy of RQ MOD's names (`games/rq-ids`). |
| `third_party/cimgui` | Dear ImGui and cimgui, which draw the window (MIT). |
| `subprojects/libsys4` | libsys4, a git submodule (GPL-2.0-or-later). |
| `subprojects/cglm.wrap` | cglm (MIT), which meson fetches for the 3D view. |
| `tools` | Small Python scripts the build runs. |

## License

GPL-2.0-or-later, as libsys4 and the parts of xsystem4 it uses (`COPYING`). The other code and what
each part is under: `CREDITS.md`. The games' files belong to their makers and are not part of this
repository or its releases.
