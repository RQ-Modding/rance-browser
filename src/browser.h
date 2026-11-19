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

/* ---- Windows: dialogs, paths, settings (win.c) ---- */

void win_init(void);
bool win_exe_dir(char *out, size_t n);
bool win_pick_folder(const char *title, char *out, size_t n);
bool win_pick_save(const char *title, const char *name, const char *ext, char *out, size_t n);
void win_message(const char *title, const char *text);
void win_attach_console(void);
bool win_relaunch(const char *game_dir);   /* start the program again on another game */
const char *settings_get(const char *key);
void settings_set(const char *key, const char *value);
void win_last_dir(const char *path);   /* remember the folder of a saved file */

/* ---- the tabs ---- */

enum tab { TAB_CGS, TAB_FLASH, TAB_MUSIC, TAB_SOUNDS, TAB_TEXTURES, TAB_PICTURES, TAB_DUNGEONS,
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
void music_select(int no);
void sounds_select(int no);
void names_select(int kind);

/* A batch save, a few files per frame with a progress window (tab_pictures.c). */
typedef bool (*batch_fn)(int i, const char *dir);
void batch_start(const char *what, int count, batch_fn fn, const char *dir);
void batch_frame(void);
bool batch_running(void);

#endif /* BROWSER_H */
