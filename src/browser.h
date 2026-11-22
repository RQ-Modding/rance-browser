/* Rance Browser: a window over the files of AliceSoft's System 4 games.
 *
 * It opens the player's own game folder read-only: the CGs, the Flash files, the music, the sounds,
 * the textures and other pictures. It reads them with libsys4 and xsystem4's own readers, so a
 * picture or a loop point reads as the game reads it. Nothing of a game ships with it. For Rance
 * Quest Magnum it also shows RQ MOD's names (rq.ids) and the Lua line that uses each file. Dear ImGui
 * (through cimgui) draws the window. The readers are a library of their own (core/core.h). */
#ifndef BROWSER_H
#define BROWSER_H

#include "core.h"

#define APP_NAME "Rance Browser"
#define APP_EXE "rance-browser"

/* ---- --check (check.c) ---- */

int run_check(const char *dir, const char *out_path);
bool font_has_japanese(const char *path);
/* --decode FILE.png OUT.rgba: RQ MOD's PNG reader (rq_pixels), or libsys4's (libsys4 true); the raw
 * RGBA written to out and "WxH alpha=0|1" printed. 0, or 1 with the reason on stderr. */
int run_decode(const char *in, const char *out, bool libsys4);
/* --recolor FILE.png OUT.rgba HUE,SATURATION,LIGHTNESS: the PNG read by RQ MOD's reader, then its
 * colours turned by RQ MOD's rule, written raw; 1 with RQ MOD's text for numbers out of range. */
int run_recolor(const char *in, const char *out, double hue, double saturation, double lightness);

/* ---- --export, --uv-map, --save-texture and the tries of the command line (export.c) ---- */

/* --texture NAME=FILE.png[,FILE2.png,...]: PNGs tried on a model's texture, and --colors
 * NAME=HUE[,SATURATION[,LIGHTNESS]]: colours by numbers on it ("*": every colour texture), for
 * --shot 3d and --export. */
struct try_arg {
	const char *arg;      /* as given */
	char *name;           /* the texture, UTF-8 */
	char **files;         /* the PNGs, in order */
	int nfiles;
	bool colors;          /* --colors: the numbers instead of files */
	double hue, saturation, lightness;
};
/* Splits a --texture argument; false when it is not NAME=FILE[,FILE...]. */
bool try_arg_parse(const char *arg, struct try_arg *out);
/* Reads HUE[,SATURATION[,LIGHTNESS]] (saturation and lightness 1 when left out); false when it is not
 * one to three numbers. */
bool colors_parse(const char *s, double *hue, double *saturation, double *lightness);
/* Splits a --colors argument; false when it is not NAME=HUE[,SATURATION[,LIGHTNESS]]. */
bool colors_arg_parse(const char *arg, struct try_arg *out);
struct rb3d_tries;
/* The tries for the model at path (its folder in the archive's bytes), made from the arguments by
 * RQ MOD's rules; NULL with RQ MOD's text in why for the first it would refuse. */
struct rb3d_tries *tries_from_args(const char *path, const struct try_arg *a, int n, char *why, size_t wn);

struct export_args {
	const char *out;      /* a .glb file (one model), else a folder */
	const char *select;   /* a model's folder, or Kind/folder */
	const char *kind;     /* chara, map, bg or etc */
	bool all;
	const struct try_arg *tries;   /* --texture, with one model only */
	int ntries;
};
int run_export(const char *dir, const struct export_args *a);
/* --uv-map NAME OUT.png or --save-texture NAME OUT.png (uv false: the game's picture) of the model
 * select names (a folder, or Kind/folder), no window. 0, or 1 with the reason on stderr. */
int run_texture_file(const char *dir, const char *select, const char *name, const char *out, bool uv);

/* ---- Windows: dialogs, paths, settings (win.c) ---- */

void win_init(void);
bool win_exe_dir(char *out, size_t n);
bool win_temp_dir(char *out, size_t n);   /* the user's temporary folder (UTF-8), without a \ at its end */
bool win_pick_folder(const char *title, char *out, size_t n);
bool win_pick_save(const char *title, const char *name, const char *ext, char *out, size_t n);
/* A file to open: what (the filter's name) and patterns ("*.aff;*.swf"). */
bool win_pick_open(const char *title, const char *what, const char *patterns, char *out, size_t n);
/* Several files to open, as for win_pick_open: their paths (UTF-8) in *out, sorted by name in
 * natural order (numbers by their value: 2 before 10), *n of them. Free each and the array. */
bool win_pick_open_many(const char *title, const char *what, const char *patterns, char ***out, int *n);
/* Whether a goes before b by name in natural order. */
int natural_cmp(const char *a, const char *b);
void win_message(const char *title, const char *text);
void win_attach_console(void);
bool win_relaunch(const char *game_dir);   /* start the program again on another game */
const char *settings_get(const char *key);
void settings_set(const char *key, const char *value);
void win_last_dir(const char *path);   /* remember the folder of a saved file */

/* ---- the tabs ---- */

enum tab { TAB_CGS, TAB_FLASH, TAB_MUSIC, TAB_SOUNDS, TAB_3D, TAB_TEXTURES, TAB_PICTURES, TAB_DUNGEONS,
           TAB_NAMES, TAB_COUNT };
extern const char *const tab_keys[TAB_COUNT];
bool tab_shown(int tab);   /* the game has this kind of file */

struct ui {
	unsigned frame;
	int want_tab;      /* a tab to open next frame, or -1 */
	int shot_select;   /* --select for --shot, or -1 */
	bool shot_grid;    /* --grid for --shot: the pictures' grid instead of one picture */
	char status[512];  /* a message for the status line */
	double status_until;
};
extern struct ui UI;

/* A size in the layout at the display's scale, so the window is as large on a 150 % screen as on a
 * 100 % one, drawn sharp instead of stretched by Windows. */
#define S(x) ((float)(x) * CORE.scale)   /* CORE.scale is 1 for --shot */

void ui_status(const char *fmt, ...);
void ui_copy(const char *text);
void ui_lua_line(const char *id, const char *line);
void ui_search_box(const char *id, const char *hint, char *buf, size_t n);
bool ui_match(const char *text, const char *needle);
bool ui_number_match(int no, const char *needle);
bool ui_nav_keys(int *sel, int count);

void tab_cgs(void);
void tab_textures(void);
void tab_alk(void);
void tab_dungeons(void);
void tab_flash(void);
void tab_music(void);
void tab_sounds(void);
void tab_names(void);
void tab_cgs_select(int no);           /* from the Names tab: open CG no in the CGs tab */
/* What --shot --select picks; with -1 the first one. */
void cgs_select_first(int no);
void set_select(int kind, int i);
void pictures_grid(int kind, bool on);
void flash_select(int i);
bool flash_select_name(const char *name);  /* by the file's name, without its folder; false: none */
void flash_shot(int frame, bool grid);     /* --shot: paused on frame (from 1), or the pictures */
bool flash_open_file(const char *path);    /* a .swf or .aff from disk, shown in the Flash tab */
bool flash_has_file(void);                 /* one was opened */
void flash_leave(void);                    /* another tab: its playing and its sounds stop */
void music_select(int no);
void sounds_select(int no);
void names_select(int kind);
/* The 3D tab (tab_3d.c): the game's 3D models, drawn by the 3D view when the build has it. */
void tab_3d(void);
int tab3d_count(void);                     /* the game's 3D models; 0 without the 3D view */
void tab3d_select(int i);                  /* by the place in the list */
/* By the model's folder ("gokiburi"), with its kind's folder when two kinds have one of that name
 * ("Etc/takarabako"); false: none. */
bool tab3d_select_name(const char *name);
/* What --shot draws in the 3D tab. */
struct shot3d {
	const char *motion;   /* a MOT's name or the game's name for one, "none": the rest pose, NULL: the first the game plays */
	int frame;            /* the motion's frame when frame_given, else the one the game puts it on */
	bool frame_given;
	const char *fight;    /* a fight background (a BG folder) under a monster's model, or NULL */
	bool fight_camera;    /* the game's fight camera */
	bool walk, bones;     /* the walk mesh's and the skeleton's lines */
	const struct try_arg *tries;   /* --texture: PNGs tried on the model's textures */
	int ntries;
};
/* Checked against the model picked; false with a message. */
bool tab3d_shot(const struct shot3d *s, char *err, size_t n);
const char *tab3d_error(void);             /* what failed for the model shown, or "" */
/* --view-only: the 3D view's frame saved as "Save this frame as PNG..." saves it; false with a
 * message. */
bool tab3d_save_view(const char *path, char *err, size_t n);
/* For the 3D textures tab: the Lua line that puts a mod's PNG on the picture at raw (its archive key,
 * Shift-JIS), when a model draws it; false (out empty) when no model's .POL names it, or without the
 * 3D view. */
bool tab3d_texture_line(const char *raw, char *out, size_t n);
/* Whether this build has the 3D view (the meson option 3d), so tab3d_texture_line can answer. */
bool tab3d_has_view(void);
/* For the Names tab, a model by its number (rq.Chara's model: 100000 plus its MonsterInfo entry):
 * its folder ("gokiburi"), or NULL when no folder of Chara or Etc draws it (or no 3D view). */
const char *tab3d_model_folder(int model);
/* Its picture at px by px pixels: the model on nothing, on the first frame of its idle as a fight plays
 * it, seen as the 3D tab first shows a monster. Asked here, drawn by tab3d_pics_make (the thumbnail's
 * tex is 0 until then) and kept; NULL when it has none. */
struct thumb *tab3d_model_pic(int model, int px);
void tab3d_pics_make(void);                /* draws pictures asked for, about 12 ms of them a call */
bool tab3d_pics_idle(void);                /* none asked and not yet drawn */
void tab3d_open_model(int model);          /* the 3D tab on the model, with its entry's motions */

/* A batch save, a few files per frame with a progress window (tab_pictures.c). */
typedef bool (*batch_fn)(int i, const char *dir);
void batch_start(const char *what, int count, batch_fn fn, const char *dir);
void batch_frame(void);
bool batch_running(void);

#endif /* BROWSER_H */
