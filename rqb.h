/* RQ Browser: the game's files for mod creators (phase P1, loader 2.43.0).
 *
 * It opens the player's own game folder read-only: the CGs, the Flash files, the music, the sounds,
 * the 3D textures and the names of rq.ids, each with the Lua line that uses it. It reads them with
 * libsys4 and the engine's own readers, so a picture or a loop point reads as the game reads it.
 * Nothing of the game ships with it. Dear ImGui (through cimgui) draws the window. */
#ifndef RQB_H
#define RQB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_USE_SDL2
#define CIMGUI_USE_OPENGL3
#ifndef CIMGUI_NO_EXPORT
#define CIMGUI_NO_EXPORT
#endif
#include <SDL.h>
#include "cimgui.h"
#include "cimgui_impl.h"

struct afa_archive;
struct aar_archive;
struct archive;
struct cg;

typedef ImVec2_c V2;
static inline ImVec2_c v2(float x, float y) { ImVec2_c r = { x, y }; return r; }
static inline ImVec4_c v4(float x, float y, float z, float w) { ImVec4_c r = { x, y, z, w }; return r; }

/* ---- the game folder (game.c) ---- */

struct cg_folder {
	char *jp;          /* the archive's folder name, UTF-8 */
	const char *en;    /* our English label */
	int count;
};

struct cg_entry {
	int no;            /* the CG number, as rq.image.replace takes it */
	int folder;        /* index in G.cg_folders */
	int afa_index;     /* index in the archive's table */
	uint32_t size;
	char *name;        /* the file's name in the archive, UTF-8 */
	const char *use;   /* "Rance: face", from the names' pictures, or NULL */
};

enum { FLASH_BATTLE, FLASH_MONSTER, FLASH_SYSTEM, FLASH_OTHER, FLASH_NFOLDERS };

struct flash_entry {
	char *path;        /* Data\Flash\Battle\x.aff, UTF-8 */
	char *base;        /* x */
	int folder;
	int afa_index;
	uint32_t size;
};

struct audio_entry {
	int no;
	uint32_t size;
	char *file;        /* 00012.ogg */
	const char *name;  /* rq.ids.music's name, or NULL */
	bool has_loop;     /* a line in the .bgi */
	int loop_count, loop_start, loop_end, volume;
};

enum { TEX_CHARA, TEX_MAP, TEX_BG, TEX_ETC, TEX_TEXTURE, TEX_OTHER, TEX_NFOLDERS };

struct tex_entry {
	char *path;        /* Data\PolyData\Chara\x\tex1.png, UTF-8 */
	char *raw;         /* the same in Shift-JIS, the archive's key */
	char *model;       /* the model's folder (x), or "" */
	int folder;
	uint32_t size;
};

struct id_entry {
	int no;
	char *name, *key, *detail;
	int cg;            /* a picture from pictures.tsv, or 0 */
	char *use;         /* what that picture is ("face"), or NULL */
};

struct id_kind {
	char *kind, *title, *what;
	struct id_entry *e;
	int n;
};

/* One set of open archives. The thumbnail thread has its own, since an archive reads through one
 * FILE*. */
struct handles {
	struct afa_archive *cg, *aff;
	struct aar_archive *red;
	struct archive *ba, *wa;
};

struct game {
	char dir[1024];
	struct handles h;
	struct cg_entry *cgs; int ncg;
	struct cg_folder cg_folders[32]; int ncg_folders;
	struct flash_entry *flash; int nflash;
	struct audio_entry *music; int nmusic;
	struct audio_entry *sounds; int nsounds;
	struct tex_entry *tex; int ntex;
	struct id_kind *kinds; int nkinds;
	int npictures;       /* names with a picture */
	int npicture_lines;  /* pictures.tsv lines read */
	int npicture_missing; /* lines whose CG the game does not have */
	char font[1024];   /* the game's font, or "" */
	char errors[2048]; /* what failed to open, for the status line */
};

extern struct game G;
extern const char *const flash_folder_names[FLASH_NFOLDERS];
extern const char *const tex_folder_names[TEX_NFOLDERS];

bool game_dir_ok(const char *dir);
bool game_open(const char *dir);
bool handles_open(struct handles *h, const char *dir);
void handles_close(struct handles *h);
struct cg *game_load_cg(struct handles *h, int i);
struct cg *game_load_tex(struct handles *h, int i);
uint8_t *game_read_flash(struct handles *h, int i, size_t *size);
uint8_t *game_read_audio(struct archive *ar, int no, size_t *size);
int game_find_cg(int no);
void game_error(const char *fmt, ...);

/* ---- pictures and thumbnails (pics.c) ---- */

enum pic_kind { PK_CG, PK_TEX, PK_NKINDS };

struct thumb {
	unsigned tex;      /* GL texture, or 0 */
	int w, h;          /* the thumbnail's size */
	int state;         /* 0 none, 1 asked, 2 ready, 3 failed */
	unsigned last;     /* the frame it was last drawn */
};

void pics_init(void);
void pics_frame(void);                 /* upload what the thread made; forget the oldest */
bool pics_idle(void);
struct thumb *pics_thumb(int kind, int i);
unsigned tex_from_rgba(const uint8_t *rgba, int w, int h);
void tex_free(unsigned tex);
void draw_checker(ImDrawList *dl, V2 a, V2 b, float cell, unsigned dark, unsigned light);
void draw_sampler_nearest(ImDrawList *dl, bool nearest);
bool save_png(const char *path, const uint8_t *rgba, int w, int h);
bool save_cg_png(const char *path, struct cg *cg);
bool save_bytes(const char *path, const uint8_t *data, size_t size);
void draw_fit_image(unsigned tex, int w, int h, V2 pos, float box);
ImTextureRef_c texref(unsigned tex);

/* ---- the player (player.c) ---- */

void player_init(void);
bool player_load(const uint8_t *data, size_t size, const char *title);
bool player_load_pcm(const uint8_t *pcm, size_t len, int rate, int channels, int bits, const char *title);
void player_close(void);
void player_set_loop(bool on, int start, int end);
void player_play(void);
void player_pause(void);
void player_stop(void);
void player_seek(int64_t frame);
bool player_playing(void);
bool player_loaded(void);
int64_t player_pos(void);
int64_t player_frames(void);
int player_rate(void);
int player_channels(void);
void player_set_volume(float v);
float player_volume(void);
const char *player_title(void);
bool player_probe(const uint8_t *data, size_t size, bool full, int64_t *frames_out, int *rate_out, int *channels_out);

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
const char *settings_get(const char *key);
void settings_set(const char *key, const char *value);
void win_last_dir(const char *path);   /* remember the folder of a saved file */

/* ---- the tabs ---- */

enum tab { TAB_CGS, TAB_FLASH, TAB_MUSIC, TAB_SOUNDS, TAB_TEXTURES, TAB_NAMES, TAB_COUNT };
extern const char *const tab_keys[TAB_COUNT];

struct ui {
	unsigned frame;
	float scale;       /* the display's scale: 1 at 100 %, 1.5 at 150 % (1 for --shot) */
	int want_tab;      /* a tab to open next frame, or -1 */
	int shot_select;   /* --select for --shot, or -1 */
	char status[512];  /* a message for the status line */
	double status_until;
};
extern struct ui UI;

/* A size in the layout at the display's scale, so the window is as large on a 150 % screen as on a
 * 100 % one, drawn sharp instead of stretched by Windows. */
#define S(x) ((float)(x) * UI.scale)

void ui_status(const char *fmt, ...);
void ui_copy(const char *text);
void ui_lua_line(const char *id, const char *line);
void ui_search_box(const char *id, const char *hint, char *buf, size_t n);
bool ui_match(const char *text, const char *needle);
bool ui_number_match(int no, const char *needle);
bool ui_nav_keys(int *sel, int count);

void tab_cgs(void);
void tab_textures(void);
void tab_flash(void);
void tab_music(void);
void tab_sounds(void);
void tab_names(void);
void tab_cgs_select(int no);           /* from the Names tab: open CG no in the CGs tab */
/* What --shot --select picks; with -1 the first one. */
void cgs_select_first(int no);
void textures_select(int i);
void flash_select(int i);
void music_select(int no);
void sounds_select(int no);
void names_select(int kind);

/* A batch save, a few files per frame with a progress window (tab_pictures.c). */
typedef bool (*batch_fn)(int i, const char *dir);
void batch_start(const char *what, int count, batch_fn fn, const char *dir);
void batch_frame(void);
bool batch_running(void);

#endif
