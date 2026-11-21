# Rance Browser

<img width="1920" height="1020" alt="image" src="https://github.com/user-attachments/assets/57e9454a-48ce-4995-a33e-8c70f4307eca" />


A window over the files of AliceSoft's System 4 games, for players, wiki writers and mod creators.
Point it at a game you own and it lists the game's CGs, Flash animations, music, sounds and other
pictures. You can look at each one, play it, and save it as PNG or OGG. It reads the game's archives without changing them
and needs neither the game running nor anything installed.

It began as RQ Browser, the file browser of [RQ MOD](https://github.com/RQ-Modding/RQ-Mod) (a mod
loader for Rance Quest Magnum), and RQ MOD's package carries it too.

Rance Browser is a fan project. It is not made or endorsed by AliceSoft or MangaGamer, and it
contains nothing of their games: it reads the copy on your own computer.

## Games

| Game | State |
| --- | --- |
| Rance Quest Magnum | Tested. Also shows RQ MOD's names for characters, items and music, and the Lua line a mod uses for each file. |
| Rance VI - Collapse of Zeth (MangaGamer's English release) | Tested. |
| Other System 4 games (Sengoku Rance, Rance 01, Rance 03, Rance IX, Rance X, and AliceSoft's other System 4 games) | Open, marked **not tested**: the browser finds their files by the rules every System 4 game follows, but nobody has checked them yet. Some kinds of file may be missing or read wrong. |

Games on older engines (System 3 and before: Rance 1 to 5D, Kichikuou Rance) do not open.

`games/README.md` says how a game becomes tested.

## What it shows

Each tab appears when the game has that kind of file.

| Tab | What | Where the game keeps it |
| --- | --- | --- |
| **CGs** | Every picture of the CG archive by its number: characters, scenes, backgrounds, the interface, effects. A grid or one at a time, with zoom. | `<game>CG.afa`, or `<game>GA.ald` to `<game>GZ.ald` |
| **Flash** | The game's Flash animations (Rance Quest's battle effects, marks and cut-ins), played as the game plays them, with what the game makes of each file and the pictures and sounds inside it. A `.swf` or `.aff` from disk too. | `<game>AFF.afa` |
| **Music** | The tracks, with a player that loops each one where the game does. | `<game>BA.ald` or `.afa`, with the `.bgi` |
| **Sounds** | The sound effects. | `<game>WA.ald`, `WA.afa` or `Sound.afa` |
| **3D textures** | The textures of the 3D models (Rance Quest). | `Data\ReignData.red` |
| **Other pictures** | Pictures kept in `.alk` files (Rance VI's event pictures, the AliceSoft logo). | `*.alk` |
| **Dungeon textures** | Each dungeon's walls, floors, ceilings, stairs, doors, sky and light maps (Rance VI). | a `.dlf` file |
| **Names** | Rance Quest only: RQ MOD's names, with a picture where one is known. | built in |

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
  folders or ranges of a thousand numbers.

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

### On the command line

```
rance-browser [--game DIR] [--scale F] [FILE.aff|FILE.swf]
rance-browser --check [--game DIR] [--out FILE]
rance-browser --shot TAB FILE.png [--game DIR] [--select N|NAME] [--grid] [--frame N] [--file PATH]
              [--size WxH] [--scale F]
```

- A `.aff` or `.swf` path opens that file in the Flash tab.
- `--check` opens every archive, decodes the first, middle and last picture of each group, reads
  every track's loop and every sound, checks every Flash file and plays it to its end, and prints the
  counts as JSON (exit 1 when something failed).
- `--shot` draws one tab without a window and saves it: `cgs`, `flash`, `music`, `sounds`,
  `textures`, `pictures`, `dungeons` or `names`. In the Flash tab, `--select` also takes a file's name,
  `--frame` the frame to draw (from 1) and `--file` a file from disk.
- `--scale` sets the size of the text and the layout (1 to 4; by default the screen's scale).

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
as a meson subproject, with cimgui in it:

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
| `games` | The game profiles (`games/README.md`) and the copy of RQ MOD's names (`games/rq-ids`). |
| `third_party/cimgui` | Dear ImGui and cimgui, which draw the window (MIT). |
| `subprojects/libsys4` | libsys4, a git submodule (GPL-2.0-or-later). |
| `tools` | Small Python scripts the build runs. |

## License

GPL-2.0-or-later, as libsys4 and the parts of xsystem4 it uses (`COPYING`). The other code and what
each part is under: `CREDITS.md`. The games' files belong to their makers and are not part of this
repository or its releases.
