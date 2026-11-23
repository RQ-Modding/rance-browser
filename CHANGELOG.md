# Changelog

Each version, newest first. A version in RQ MOD's package is named in RQ MOD's own CHANGELOG.

## 1.4.0 (2026-11-23)

Four more games with profiles: Sengoku Rance, Rance 01, Rance 02 and Rance 03, each with English
labels for its CG groups. The newer games' archives are read by their folders, Rance 03's music is
found in its sound archive with each track's loop, and voices have a tab of their own.

- **The newer games' folders** (Rance 01 and 03): their archives name each file by a path whose
  folders are split by a full-width slash (`イベント／...`), which the browser now reads as folders, as
  it reads `\` and `/`. Each CG goes in its first folder (Rance 03's 12, Rance 01's 13), and a CG
  named in words is listed by its name after that folder instead of the folder's name alone. The
  sounds of such an archive are listed by their names in words, in their folders, and **Save as
  OGG...** names the file as the archive does.
- **Rance 03's music:** the game keeps its music and its sounds in one archive (`Sound.afa`) with a
  table of the tracks and their loops in it (`timemap_sound2.ex`). The Music tab lists the table's
  50 tracks, each with its loop, in a folder by its kind (ADV, ダンジョン, 戦闘...); the Sounds tab
  the other 268. A game with such an archive and no table gets the folder `音楽` ("music") as its
  music.
- **Voices** (a new tab, after Sounds): the game's voice archive (`<game>Voice.afa`, Rance 03's
  17,913), each voice by its file's number, in a folder per thousand, with the player and **Save as
  OGG...**. A folder box above the list, as on the Music and Sounds tabs, which now have one too
  when their files have folders.
- **The profiles:** `games/Rance7.tsv` (Sengoku Rance), `rance02.tsv`, `Rance01.tsv` and
  `Rance03.tsv`, each with the game's title and labels for its CG groups, made by looking at the
  pictures: number ranges for Sengoku Rance and Rance 02, the archive's folders for Rance 01 and 03.
- **On the command line:** `--shot voices`; `--check` reads the first, middle and last voice of each
  folder, and its report gains `voices`.
- **The copy of RQ MOD's 3D renderer** follows RQ MOD 2.65.3, which draws the 3D with the game's
  anti-aliasing when the game's 3D setting asks for it, and 2.66.1, which filters the models'
  textures as the game's Texture Filtering asks (point sampling, bilinear, or anisotropic 4X, 8X or
  16X), and 2.66.2, which sizes the shadow map and softens the shadow's edge as the game's shadow
  settings ask, and smooths the edges of shapes cut out of their pictures under anti-aliasing, and
  2.67.0, which can draw the game window's 3D at the window's own size. The browser's views keep
  anti-aliasing off, the textures bilinear and medium shadows on a map of 512, and are drawn at their
  own size, so nothing they draw changes.

## 1.3.0 (2026-11-22)

Rance Quest's 3D textures as RQ MOD changes them: a model's textures listed as RQ MOD lists them, a
PNG or colours by numbers tried on one and drawn as the game would draw them, and each texture's UV
map.

- **Its textures** (the 3D tab) lists what the model's materials use, in the model's order, as RQ
  MOD's `rq.image.textures` does: each texture's name, the maps it is used as (colour, alpha, light,
  normal, specular), its size, whether it has alpha, its frames (a texture the game animates, as
  `face.png`, `face[1].png`...), "its own alpha map" where a material cuts the picture by its own red,
  and "not in the folder" for a name the archive lacks. A picture opens in the 3D textures tab; the
  folder's other pictures, which no material names, are shown under the list.
- **Save as PNG...** saves the game's picture, **Save the UV map...** the triangles the model draws
  with that file: white lines over a faint fill, see-through elsewhere, at the picture's size, to lay
  over it in a paint program (drawn without a window, the same bytes each time; a light map's by the
  light UVs).
- **Try a PNG...** puts a PNG on the texture and loads the model again, keeping the camera, the motion
  and the fight: drawn as RQ MOD's engine draws a mod's picture, read by a copy of its PNG reader and
  given the game's alpha kind (a solid PNG on a see-through texture keeps the game's holes). A
  texture with frames takes a PNG per frame, picked together and taken in their names' order (frame2
  before frame10), the order shown under it. A file RQ MOD would refuse is refused under its row with
  RQ MOD's own words (not a PNG, too large, cut off, a frame's name, a count other than the game's).
  **The game's** takes the try away; another model starts with none. A file gone or broken at a later
  load, or when the model is exported, draws the game's picture, each file said once in the status
  line (several at once on one line), as the engine logs each. Two tried PNGs of one name from two
  folders, which the lines would name as one file, are pointed out under the lines.
- **Colours...** on a colour texture opens three sliders, hue (-360 to 360 degrees), saturation and
  lightness (factors, 0 to 4), and loads the model again when one is let go: the game's own picture,
  each frame, turned in HSL by RQ MOD's own rule, as `rq.image.texture`'s `{ hue = ... }` does (grey,
  white and black stay grey). **Every colour texture: Colours...**, above the rows, puts the same
  numbers on each colour texture of the model, as `rq.monster.model`'s `textures["*"]`; a texture
  tried by its name keeps its own try. Colours on another kind of texture, and `*` on a model with
  no colour texture or given a PNG, are refused, as RQ MOD refuses them.
- **The Lua lines** follow the tries: `rq.image.texture` for every model that draws each texture (its
  frames as a list, colours as `{ hue = 200, saturation = 1.5 }`, the numbers not at their defaults),
  and for a monster's folder `rq.monster.model` with `textures`, a model of the mod's own (colours on
  every colour texture as `["*"] = { ... }`, which `rq.image.texture` does not take: there, a line per
  texture); each PNG named in the mod's `textures` folder. The 3D textures tab shows the
  `rq.image.texture` line of a picture a model draws (a frame's as the list on its base), and says so
  of a picture no model draws by its name (a build without the 3D view says neither).
- **The export** writes a tried PNG or tried colours as the engine draws them, each frame its own
  picture, and the game's alpha maps as before.
- **On the command line:** `--texture NAME=FILE.png[,FILE2.png,...]` and `--colors
  NAME=HUE[,SATURATION[,LIGHTNESS]]` (`*` as NAME: every colour texture), 64 of them in all, with
  `--shot 3d` and with `--export` of one model, checked before any window opens (a refusal exits 1
  with RQ MOD's text); `--view-only`, for `--shot 3d` to save the 3D view's frame alone; `--uv-map
  NAME OUT.png` and `--save-texture NAME OUT.png` with `--select MODEL`, without a window; `--decode
  FILE.png OUT.rgba` (RQ MOD's reader) and `--decode-libsys4`, the raw pixels for a check, and
  `--recolor FILE.png OUT.rgba HUE,SATURATION,LIGHTNESS`, the same turned by RQ MOD's colours. Exit
  codes stay 0, 1 and 2.
- **`--check`** also draws the UV map of every texture of every model and counts its triangles against
  the renderer's meshes, and tries a PNG of its own on the Cockroach, deletes it and loads the model
  again (the game's picture, the warning once). The report's `3d` gains `draw.uv_maps` and `tries`.
- **The copies** of RQ MOD's code gain its PNG reader (`src/core/rq_pixels.c`) and its rules for a
  mod's picture on a 3D texture (`src/core/3d/rq_textures.c`), with the renderer's `model.c` and
  `3d_internal.h` as RQ MOD changed them; `src/core/COPIES` names them. libpng is linked in for the
  reader as well as through libsys4.

## 1.2.0 (not released)

Rance Quest's 3D models: a tab that draws them as the game draws them, and each one as a glTF file
for Blender.

- **The 3D models tab.** The models of `Data\ReignData.red`: the monsters and figures, the rooms, the
  fight backgrounds and the props, by kind, each by its folder and the game's name (a monster's with
  the English names of the monsters that wear it), with a search over them. Each one is drawn by a
  copy of RQ MOD's 3D renderer, so as the game draws it: its textures and light maps, its outline,
  its texture animations. Left drag turns the camera, right drag moves it, the wheel comes closer;
  the camera frames the model through its whole motion. **Outline** and **Light map** turn the
  game's off; **Bones** and **Walk mesh** draw the skeleton and the floor a room's characters walk
  on; the view lies over black, white or squares.
- **Motions** at the game's 30 frames a second: a monster's motions as a fight plays them, by the
  game's monster table ("攻撃 · gokiburi_kougeki · 1 to 21, once"), a prop's as a map shows it, each
  motion file of the folder whole, or the rest pose. Play and Pause (Space), a frame back or on (Comma
  and Period), a slider over the frames and the speed, as in the Flash tab.
- **In a fight.** A monster stands on one of the game's 40 fight backgrounds, where a fight puts it,
  at its size, with its shadow on the ground (**Shadow**) and, with **Fight camera**, seen through
  the fight's camera.
- **The right side:** the model's facts (its version, meshes, triangles, bones, motions), its
  textures, each opening the 3D textures tab at it, and RQ MOD's Lua lines (`rq.monster.model` for a
  monster's model, `rq.battle.start`'s `background`, `rq.dungeon.place`'s `look`). **Save this frame
  as PNG...** saves the view at its size, see-through where nothing is drawn.
- **Export the model as glTF (.glb)...** writes the model as one glTF binary file: its meshes, its
  textures (as PNG, inside the file), its materials (the game's alpha tests and blending as glTF's,
  each one's choice said in words where glTF cannot do both), its skeleton and each point's weights
  (the four largest, as the game uses), each motion as an animation named by its file, a room's walk
  mesh as an object of its own, and what glTF has no place for as properties named `rq_*`: the game's
  names, its motions' frames as the game plays them, a material's flags, light map and frames. The
  points' colours are written as a glTF reader reads them, and each mesh's UVs are moved by whole
  squares onto their picture (the game keeps most a square away, and its textures repeat, so the
  model draws the same). A file is written under another name and takes its own once whole.
- **`--export`** writes one model (`--select`), a kind's (`--kind chara,map,bg,etc`) or every one
  (`--all`), to a folder with `export.json`: what each file holds and points of each model as the
  renderer places them, to check the files against in another program.
- **The Names tab** shows each monster's 3D model as its picture, with **Open in the 3D tab**.
- **`--shot 3d`** draws the 3D tab: `--select` takes a model's folder, `Kind/folder` or a monster's
  model number, with `--motion`, `--frame`, `--fight`, `--fight-camera`, `--walk` and `--bones`.
- **`--check`** reads every 3D model and the files beside it (motions, texture animations,
  materials), draws each model through each of its motions, plays the game's motions as a fight
  does, stands each monster on a fight background with its shadow, and exports a room and a monster,
  checking that the view draws the same after. The report adds `3d`: the counts, and the gaps the
  game's own files hold (five motion files the engine refuses, as their bones are not the model's).
- **The 3D renderer** is the library `rance_3d` (`src/core/3d`), apart from `rance_core`, so a program
  that takes `rance_core` takes no 3D code: a copy of RQ MOD's engine's (`src/core/COPIES` names each
  file), on cglm 0.9.7 (MIT), which meson fetches from its wrap. The meson option `3d` (`auto`) builds
  it; the releases build with `-D3d=enabled`, and cglm's license is in the zip. A fatal error inside
  the renderer drops that model, not the program. The tab needs OpenGL 3.1; with less it says so, and
  the other tabs work.

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
