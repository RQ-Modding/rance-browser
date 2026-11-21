# Game profiles

Rance Browser opens any System 4 game by the rules every one of them follows (section "How a game is
found" in the main README). A profile adds what a game's files do not say: its title, whether the
browser was checked on it, and labels for its CGs. The program carries the profiles inside it; a new
or changed profile needs a new build.

One file per game, named after the game's `.ain` without `.ain`, as the game's ini names it
(`CodeName = "Rance6.ain"` gives `Rance6.tsv`). Add the file to `game_tsvs` in `meson.build`.

| Game | Profile | Checked |
| --- | --- | --- |
| Rance Quest Magnum | `RanceQuest.tsv` | yes: every tab, with RQ MOD's names and Lua lines |
| Rance VI - Collapse of Zeth (MangaGamer's release) | `Rance6.tsv` | yes: CGs, music, sounds, other pictures, dungeon textures |

## The format

A text file in UTF-8. Each line is a key, a tab, and its values separated by tabs. A line that starts
with `#` and an empty line are skipped.

| Key | Values | What it does |
| --- | --- | --- |
| `title` | the name | The window's title and the status line. Without it, the ini's `GameName`. |
| `tested` | `yes` or `no` | `yes` takes away the "not tested" mark. Say `yes` only after the checks below. |
| `font` | a path in the game's folder | A font for the window, used when it is there (Rance Quest's `Data/NotoSansRQM-Regular.ttf`). Without it, Windows' Segoe UI, with a Japanese font of Windows for the glyphs it lacks. |
| `folder` | the archive's folder, a label | For a game whose CG archive (`.afa`) has folders: an English label beside the folder's own name. |
| `range` | first number, last number, a label | For a game whose CG archive (`.ald`) has no folders: the CGs from the first to the last number in one group with that label. A CG in no range goes in its thousand ("7000-7999"). |
| `mask` | first number, last number, offset | The CGs in that range take their see-through from the CG `offset` higher, when that one exists and has the same size (Rance VI's JPEG effects, each with a PMS alpha map of its size 10000 higher). Read from the numbers and sizes, not from the game's script. |
| `rq` | `yes` | RQ MOD's game: the Names tab and the Lua lines. For Rance Quest Magnum only. |
| `look` | a number, a name | A look of RQ MOD's for a map's prop (`rq.dungeon.place`'s `look`): the prop's number in the game's `Data/MapObject.acx` and the look's name. The 3D tab names each prop's model by it and gives its Lua line. For Rance Quest Magnum only. |

## Checking a game

1. Run `rance-browser --check --game "<the game's folder>"`. It prints the counts as JSON and ends
   with `"ok": true` when every sample read. Note each count.
2. Open the window on the game and go through each tab. Look at the first, a middle and the last
   picture of each group, play a few tracks with "Loop as the game does", and a few sounds.
3. For CG labels, tick **Grid** and walk the numbers: a label names what most of the range holds.
4. Set `tested` to `yes`, and add the game to the table above with what was checked.
