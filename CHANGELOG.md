# Changelog

Each version, newest first. A version in RQ MOD's package is named in RQ MOD's own CHANGELOG.

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
