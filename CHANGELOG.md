# Changelog

Each version, newest first. A version in RQ MOD's package is named in RQ MOD's own CHANGELOG.

## 1.1.0 (not released)

The Flash tab plays a file as Rance Quest's engine plays it, and says whether the game would play it
at all.

- **The player.** A Flash file plays in the Flash tab as RQ MOD's engine plays it: its frames at the
  game's 30 a second (Rance Quest plays every Flash file at 30, whatever the file says; another game's
  at the file's own rate), its actions (stop, play, gotoFrame), its sounds at their frames over each
  other, its blend modes and colour changes, each frame drawn with OpenGL as the engine draws it.
  Play and Pause (Space), the first and the last frame, a frame back or on (Comma and Period), a
  slider over the frames, Loop, Sounds, the speed (1, 1/2, 1/4, 1/10), what lies under it (squares,
  black or white) and the zoom. **Its pictures** shows the bitmaps' grid as before.
- **What the game makes of it.** Every file is checked by RQ MOD's rules, the engine's reader's and
  its player's: the game plays it, plays it with warnings, would not play it, or cannot read it. Each
  problem and warning comes with its frame, its tag and what to do. Then what the file is: its
  container and version, size, frames and length at the game's rate, its pictures (and how many have
  soft edges), shapes, sprites and sounds, how often the sounds start, its blend modes, colour changes
  and jumps. A file the game would not play is not played here either.
- **A file from disk.** **Open a .swf or .aff file...** above the list, a file dropped on the window,
  or its path on the command line: a mod's own animation is checked and played before it goes into a
  mod, with RQ MOD's Lua lines for it (`rq.image.add_flash`, `rq.image.play_flash`). A game file gets
  the line that replaces it with a file of the same size (the game places it by its size) and the one
  that plays it.
- **Save this frame as PNG...** saves the frame shown, its see-through kept.
- **Soft edges as drawn.** A Flash picture's colours are stored multiplied by its alpha where it has
  soft edges; they are divided back as RQ MOD's engine now does, so glows and soft edges show and save
  brighter, as drawn. 1.0.1 showed them darker than the game's art.
- **`--check`** checks every Flash file by the engine's rules and plays each one to its last frame and
  back to its first. The report adds `plays`, `with_warnings`, `played`, `back_to_first`,
  `frames_played`, `sounds_started`, `jumps` and `most_objects`; a file the rules refuse, or one that
  does not reach its end, fails the check.
- **`--shot flash`** takes `--frame N` (the frame, from 1), `--file PATH` (a file from disk) and
  `--select NAME` (a file by its name; a name the game does not have fails the shot instead of drawing
  the first file).
- **The library** (`rance_core`) adds the mixer, several sounds at once and music with its fades
  (from RQ Scene Editor), and the Flash player (`fplay_*` in `core.h`), which draws into a texture any
  Dear ImGui window shows. With them come RQ MOD's Flash checker (`swf_check.c`, a copy of the
  engine's) and the engine's fixes to the Flash reader: a tag it does not know, a file cut short, an
  empty sprite or a length past the file's end no longer close the program.

## 1.0.1 (2026-11-20)

The readers as a library, for another program to share; nothing changes in the window.

- **`rance_core`.** What reads a game (the profiles, the game folder, the CGs, the Flash files, the
  music and sounds, the other pictures, RQ MOD's names, the thumbnails and the player) moved to
  `src/core` and builds as a static library, described by `src/core/core.h`. The program links it.
  Another program takes it as `subproject('rance-browser').get_variable('rance_core_dep')`, with
  cimgui in it; RQ MOD's coming scene editor is the first.
- The readers no longer reach into the window: the program gives them the display's scale and where
  their messages go (`CORE`), and the thumbnails count their own frames.
- **The licenses of libvorbis and LAME in the zip** (`licenses/libvorbis`, `licenses/lame`): both are
  linked in through libsndfile, and MSYS2 installs no license file for them, so the repository
  carries them. 1.0.0's zip lacked both.
- **An icon of its own**, on the exe in Explorer, on the window and in the taskbar
  (`res/rance-browser.png`, made into `res/rance-browser.ico`).

## 1.0.0 (2026-11-16)

Rance Browser: RQ MOD's file browser in a repository of its own, and for more games than Rance Quest
Magnum.

- **The name.** RQ Browser (`rq-browser.exe`) is now Rance Browser (`rance-browser.exe`), with a
  version of its own. Its settings file is `rance-browser.json`; an `rq-browser.json` beside it is
  read when it is the only one.
- **Any System 4 game.** The game is found from its ini (`System40.ini` or `AliceStart.ini`): the
  code file's name gives the archives' names, as the engine finds them. CGs in `.afa` or in the `.ald`
  volumes A to Z, music and sounds in `.ald` or `.afa`, the Flash files, the 3D textures. A game
  without a profile opens marked "not tested".
- **Profiles** (`games/*.tsv`, built in): a game's title, whether it was checked, labels for its CG
  folders or number ranges, its font, and pictures that take their see-through from a mask.
- **Rance VI - Collapse of Zeth** (MangaGamer's release), tested: 8,944 CGs in 20 labelled groups
  (WebP, JPEG and PMS), the battle effects with their masks, 47 tracks with their loops (the `.bgi`'s
  older five-number lines), 770 sounds, 136 pictures of its `.alk` files and 4,462 textures of its 38
  dungeons (`dungeondata.dlf`).
- **New tabs,** shown when the game has the files: **Other pictures** (`.alk`) and **Dungeon
  textures** (`.dlf`). A tab the game has no files for is hidden.
- **Open another game...** at the bottom starts the program again on another folder.
- **The window's font:** the game's own when its profile names one, else Segoe UI, with Windows'
  Japanese font for Japanese names.
- **`--shot ... --grid`** draws the picture grid; `--select N` in the CGs tab takes the next number the
  game has.
- **RQ MOD's parts stay Rance Quest's:** the Names tab and the Lua lines show for Rance Quest Magnum
  only. Inside RQ MOD's build the names and `rq.lua` are the loader's own (meson options `rq_ids_dir`
  and `rq_lua`); built alone, the copy in `games/rq-ids`.
- **Fixed:** in the grid, a picture picked from the command line or the Names tab is brought into
  view (the list on the left took the request first). The details show a picture's size and format in
  the grid too.

## Before 1.0.0: RQ Browser, in RQ MOD

- **RQ MOD 2.52.1:** the Names tab read a kind's rows with the last kind's list now and then, which
  could close the program when the kind changed.
- **RQ MOD 2.43.0:** RQ Browser, a window over Rance Quest Magnum's files: CGs, Flash files, music,
  sounds, 3D textures and RQ MOD's names, with the Lua line for each.
