# RQ MOD's names, a copy

`ids.json` (the names of `rq.ids`: characters, items, skills, monsters, quests, music),
`pictures.tsv` (the CG each name shows) and `models.tsv` (each monster's 3D model, as a number:
100000 plus its entry in the game's model table) come from RQ MOD's `data/ids`, where its id extract
job writes them. The Names tab of Rance Quest Magnum and the CGs' "Used as" line read them.

RQ MOD's own build passes its `data/ids` (meson option `rq_ids_dir`), so RQ MOD's package always has
its current names. This copy is for the browser built alone. RQ MOD's `scripts\check-browser.ps1`
reports when they differ; copy the files over these then.
