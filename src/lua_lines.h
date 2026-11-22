/* RQ MOD's Lua lines the browser offers to copy, for Rance Quest Magnum. RQ MOD's build checks each
 * rq.* call here against its rq.lua (tools/check_lua_lines.py, meson option rq_lua), so a renamed
 * call fails that build instead of leaving a stale line, and each key of a table given to a call
 * against its parameter's fields. A comment starts with its call and the call's parameters; a part of
 * a line the browser puts together starts with the class its keys belong to; "name: rq.Class" (or
 * "name[*]: rq.Class" for the tables among a table's values) has a table inside a line checked too. */
#ifndef RQB_LUA_LINES_H
#define RQB_LUA_LINES_H

/* rq.image.replace(cg, path) */
#define LUA_CG_REPLACE      "rq.image.replace(%d, \"pictures/%d.png\")"
/* rq.image.flash_bitmap(file, id, path) */
#define LUA_FLASH_BITMAP    "rq.image.flash_bitmap(\"%s\", %d, \"pictures/bitmap_%d.png\")"
/* rq.image.flash_bitmaps(file) */
#define LUA_FLASH_BITMAPS   "rq.image.flash_bitmaps(\"%s\")"
/* rq.sound.play_flash(key, opts) */
#define LUA_FLASH_SOUND     "rq.sound.play_flash(\"%s\", { index = %d })"
/* rq.image.add_flash(key, path): a file of the mod's in the place of one of the game's, or its own */
#define LUA_FLASH_ADD       "rq.image.add_flash(\"%s\", \"flash/%s\")"
/* rq.image.play_flash(key, opts) */
#define LUA_FLASH_PLAY      "rq.image.play_flash(\"%s\", { x = %d, y = %d })"
/* rq.music.replace(no, path) */
#define LUA_MUSIC_REPLACE   "rq.music.replace(%d, \"music/%d.ogg\")"
/* rq.music.play(no) */
#define LUA_MUSIC_PLAY      "rq.music.play(%d)"
/* rq.sound.play(src) */
#define LUA_SOUND_PLAY      "rq.sound.play(%d)"
/* rq.ids.<kind>.<KEY>: a table, not a call */
#define LUA_IDS             "rq.ids.%s.%s"
/* rq.monster.model(def): a model of the game's, for a mod's monster */
#define LUA_MONSTER_MODEL   "rq.monster.model({ id = \"mymod:%s\", base = %d })"
/* rq.battle.start(opts): a fight on one of the game's backgrounds */
#define LUA_BATTLE_BG       "rq.battle.start({ enemies = { 0, rq.ids.monster.COCKROACH, 0 }, background = \"%s\" })"
/* rq.dungeon.place(kind, x, z, opts): a mod's own object, with a prop's look */
#define LUA_PLACE_LOOK      "rq.dungeon.place(\"custom\", x, z, { id = \"mymod:my_object\", look = \"%s\" })"
/* rq.image.texture(texture, picture): a mod's PNG on one of the game's 3D textures, wherever a model
 * draws it */
#define LUA_TEXTURE_PNG     "rq.image.texture(\"%s\", \"textures/%s\")"
/* rq.image.texture(texture, picture): a texture's frames, a PNG each (the items: LUA_TEXTURE_ITEM) */
#define LUA_TEXTURE_LIST    "rq.image.texture(\"%s\", { %s })"
/* a PNG of the mod's, in a list of frames or as a model's own texture */
#define LUA_TEXTURE_ITEM    "\"textures/%s\""
/* rq.monster.model(def): a mod's monster's model with textures of its own (the entries:
 * LUA_MODEL_TEXTURE) */
#define LUA_MONSTER_TEXTURES "rq.monster.model({ id = \"mymod:%s\", base = %d, textures = { %s } })"
/* an entry of rq.monster.model's textures: a texture's name and its picture (LUA_TEXTURE_ITEM, or a
 * list of them) */
#define LUA_MODEL_TEXTURE   "[\"%s\"] = %s"

#endif
