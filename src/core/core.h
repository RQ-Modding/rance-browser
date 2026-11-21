/* Rance Browser's readers (the rance_core library): a System 4 game's folder and its files, read-only.
 *
 * A game's profile, the game folder found from its ini as the engine finds it, the CGs, the Flash
 * files, the music, the sounds, the textures and other pictures, RQ MOD's names (rq.ids) for Rance
 * Quest Magnum, GL textures and thumbnails of the pictures, a player for the music and sounds, a
 * mixer that plays several sounds at once, and a Flash player that plays a Flash file as RQ MOD's
 * engine does, with the checker of the engine's rules (swf_check.h). They read with libsys4 and
 * xsystem4's own readers, so a picture or a loop point reads as the game reads it. Rance Browser is
 * one program over them; another (a scene editor) can link them too, as the dependency
 * rance_core_dep of this project. The thumbnails and the drawing helpers take Dear ImGui (cimgui)
 * and OpenGL 3, as the program's window does. */
#ifndef RANCE_CORE_H
#define RANCE_CORE_H

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
struct alk_archive;
struct archive;
struct cg;

typedef ImVec2_c V2;
static inline ImVec2_c v2(float x, float y) { ImVec2_c r = { x, y }; return r; }
static inline ImVec4_c v4(float x, float y, float z, float w) { ImVec4_c r = { x, y, z, w }; return r; }

/* ---- what the program gives the readers (core.c) ---- */

struct core_host {
	float scale;                       /* the display's scale: 1 at 100 %, 1.5 at 150 % */
	void (*status)(const char *text);  /* where a reader's message goes (a status line), or NULL */
};
extern struct core_host CORE;

void core_status(const char *fmt, ...);   /* a message to CORE.status */

/* ---- a game's profile (games.c, from games/<code>.tsv) ---- */

struct profile {
	const char *code;   /* the .ain's name without .ain: "RanceQuest", "Rance6" */
	char *title;        /* "Rance Quest Magnum" */
	bool tested;        /* someone checked this game's files with the browser */
	bool rq;            /* RQ MOD's game: its names (rq.ids) and Lua lines */
	char *font;         /* a font in the game's folder for the window, or NULL */
	struct { char *name, *label; } folders[64];        /* labels for a CG archive's folders */
	int nfolders;
	struct { int lo, hi; char *label; } ranges[128];   /* labels for CG number ranges */
	int nranges;
	struct { int lo, hi, offset; } masks[8];   /* CGs whose see-through is another CG (CG + offset) */
	int nmasks;
	struct { int object; char *name; } looks[32];   /* RQ MOD's looks for a map's props: MapObject.acx's number, the look */
	int nlooks;
};

const struct profile *profile_find(const char *code);

/* ---- the game folder (game.c) ---- */

struct cg_folder {
	char *jp;          /* the archive's folder name, UTF-8, or the numbers ("7000-7999") */
	const char *en;    /* our English label */
	int count;
	int lo;            /* a range's first number, to sort by */
};

struct cg_entry {
	int no;            /* the CG number, as the game (and rq.image.replace) takes it */
	int folder;        /* index in G.cg_folders */
	int index;         /* index in the archive: the AFA's table, or the ALD's number - 1 */
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

/* The picture kinds: the CGs, and three sets of other pictures (sets.c). */
enum pic_kind { PK_CG, PK_TEX, PK_ALK, PK_DTX, PK_NKINDS };

enum { TEX_CHARA, TEX_MAP, TEX_BG, TEX_ETC, TEX_TEXTURE, TEX_OTHER, TEX_NFOLDERS };

/* One picture of a set: a 3D texture of ReignData.red, a picture in an .alk file, or a dungeon's
 * texture in a .dlf file. */
struct pic_entry {
	char *path;        /* where it is, shown in the details: Data\PolyData\Chara\x\tex1.png */
	char *title;       /* the list's line: tex1.png */
	char *group;       /* a second level shown beside the folder (a model), or "" */
	char *save;        /* the file name to save as, without .png */
	char *save_dir;    /* a folder under the one picked for "Save the shown", or "" */
	int folder;        /* index in the set's folders */
	uint32_t size;
	char *raw;         /* RED: the archive's key, in Shift-JIS */
	int src;           /* ALK: which file; DTX: the dungeon's slot in the .dlf */
	int index;         /* ALK: the entry */
	int64_t off;       /* DTX: where the texture starts in the .dlf */
};

#define SET_MAX_FOLDERS 128

struct pic_set {
	char *folders[SET_MAX_FOLDERS];
	int counts[SET_MAX_FOLDERS];
	int nfolders;
	struct pic_entry *e;
	int n;
};

struct id_entry {
	int no;
	char *name, *key, *detail;
	int cg;            /* a picture from pictures.tsv, or 0 */
	char *use;         /* what that picture is ("face"), or NULL */
	int model;         /* a 3D model from models.tsv (rq.Chara's model: 100000 plus its MonsterInfo
	                      entry), or 0 */
};

struct id_kind {
	char *kind, *title, *what;
	struct id_entry *e;
	int n;
};

#define MAX_ALK 16

/* Where the game keeps each kind of file, found from its ini as the engine finds them (game.c). */
struct game_files {
	char ini[64];                /* "System40.ini" or "AliceStart.ini" */
	char *cg_afa;                /* RanceQuestCG.afa, or NULL */
	char *cg_ald[26];            /* Rance6GA.ald ... by the volume's letter */
	int ncg_ald;
	char *ba_afa, *wa_afa;
	char *ba_ald[26], *wa_ald[26];
	int nba_ald, nwa_ald;
	char *aff;
	char *red;
	char *bgi;
	char *alk[MAX_ALK];          /* the .alk files that hold pictures */
	int nalk;
	char *dlf;
};

/* One set of open archives. The thumbnail thread has its own, since an archive reads through one
 * FILE*. */
struct handles {
	struct archive *cg;          /* the CGs: an AFA or an ALD archive */
	struct afa_archive *cg_afa;  /* the same when it is an AFA (its table has the folders) */
	struct afa_archive *aff;
	struct aar_archive *red;
	struct archive *ba, *wa;
	struct alk_archive *alk[MAX_ALK];
	void *dlf;                   /* the .dlf's FILE* */
};

struct game {
	char dir[1024];
	char code[256];      /* the .ain's name without .ain */
	char name[256];      /* the ini's GameName, UTF-8 */
	char title[256];     /* the profile's title, else the ini's name */
	const struct profile *prof;   /* NULL: no profile */
	bool tested;         /* the profile says the game was checked */
	bool rq;             /* Rance Quest Magnum: RQ MOD's names and Lua lines */
	struct game_files f;
	struct handles h;
	struct cg_entry *cgs; int ncg;
	struct cg_folder cg_folders[256]; int ncg_folders;
	struct flash_entry *flash; int nflash;
	struct audio_entry *music; int nmusic;
	struct audio_entry *sounds; int nsounds;
	struct pic_set sets[PK_NKINDS];   /* PK_TEX, PK_ALK and PK_DTX; PK_CG's is unused */
	int ndungeons;       /* dungeons with textures */
	struct id_kind *kinds; int nkinds;
	int npictures;       /* names with a picture */
	int npicture_lines;  /* pictures.tsv lines read */
	int npicture_missing; /* lines whose CG the game does not have */
	int nmodels;         /* names with a 3D model (models.tsv) */
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
uint8_t *game_read_flash(struct handles *h, int i, size_t *size);
uint8_t *game_read_audio(struct archive *ar, int no, size_t *size);
int game_find_cg(int no);
int game_mask_of(int no);   /* the CG whose mask gives CG no its see-through, or 0 */
void game_error(const char *fmt, ...);
char *join(const char *dir, const char *name);
const char *base_name(const char *path);   /* after the last \ or / */

/* ---- the picture sets (sets.c) ---- */

extern const char *const set_titles[PK_NKINDS];
void sets_list(void);
int pic_count(int kind);
struct cg *game_load_pic(struct handles *h, int kind, int i);   /* any kind, PK_CG too */
void sets_open(struct handles *h);
void sets_close(struct handles *h);

/* ---- pictures and thumbnails (pics.c) ---- */

struct thumb {
	unsigned tex;      /* GL texture, or 0 */
	int w, h;          /* the thumbnail's size */
	int state;         /* 0 none, 1 asked, 2 ready, 3 failed */
	unsigned last;     /* the frame it was last drawn */
};

void pics_init(void);                  /* after game_open, with CORE.scale set */
void pics_frame(void);                 /* once a frame: upload what the thread made; forget the oldest */
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

/* ---- the mixer: several sounds at once (mixer.c) ---- */

void mixer_sound(int no);                        /* a game sound, over what plays */
void mixer_music(int no, int effect, int ms);    /* as SYS_PlayBGM; effect and ms -1: the game's defaults (at once) */
int mixer_music_now(void);
void mixer_stop(int fade_ms);                    /* everything, over fade_ms */
double mixer_sound_ms(int no);                   /* a sound's length in ms, 0 when unknown */
/* A Flash file's sound: its samples as the file holds them (8 or 16 bits, little-endian), over
 * what plays, at vol (0 to 1). */
void mixer_pcm(const uint8_t *pcm, size_t len, int rate, int channels, int bits, float vol);
int mixer_voices(void);                          /* the sounds playing now */
void mixer_shutdown(void);
int mixer_check(void);                           /* the mixer's own checks, without a device: 0 when every one holds */

/* ---- the Flash player (flash_player.c) ---- */

struct swf;
struct swfc;
struct fplay;

/* What is under a drawn frame: nothing (the colours multiplied by the alpha, for fplay_image), a
 * checkerboard, black or white. */
enum { FPLAY_CLEAR, FPLAY_CHECKER, FPLAY_BLACK, FPLAY_WHITE };

/* Checks a Flash file by the engine's rules into *c (swf_check.h), and opens it when the game plays
 * it: an .aff, or a SWF uncompressed or compressed with zlib. NULL when the game would not (c says
 * why; *code, when given, is swfc_check's answer). Free c with swfc_free whatever this returns; the
 * player keeps nothing of data or c. It starts before its first frame: fplay_seek(p, 1) shows it. */
struct fplay *fplay_open(const uint8_t *data, size_t size, struct swfc *c, int *code);
void fplay_close(struct fplay *p);
const struct swf *fplay_swf(const struct fplay *p);   /* the file as read (swf.h) */
int fplay_width(const struct fplay *p);     /* the stage, as the engine sizes it: x_max / 20 */
int fplay_height(const struct fplay *p);
int fplay_frames(const struct fplay *p);    /* the header's frame count: the game's players play this many */
double fplay_rate(const struct fplay *p);   /* the header's frames a second (the game plays at 30) */
int fplay_frame(const struct fplay *p);     /* the frame shown, from 1; 0 before the first */
bool fplay_ended(const struct fplay *p);    /* a stop action, or the file's end, was met */
/* The engine's seek, as the game's T_PartsFlash calls it for a frame by the clock: a frame ahead
 * walks the tags between (their sounds play), one behind walks again from the start with the
 * sounds before it quiet. A gotoFrame on the way moves the frame shown. False when it was there. */
bool fplay_seek(struct fplay *p, int frame);
int fplay_goto(struct fplay *p, int frame); /* as rq.sprite's: a stop forgotten, then the seek; the frame shown */
int fplay_next(struct fplay *p);            /* the next frame's tags; the frame shown */
void fplay_sound(struct fplay *p, bool on, float volume);   /* each StartSound through the mixer; off at first */
int fplay_objects(const struct fplay *p);   /* on the display list now */
int fplay_started(const struct fplay *p);   /* sounds started so far (with the sound off too) */
int fplay_jumps(const struct fplay *p);     /* gotoFrame actions followed so far */
/* The frame shown, drawn as the engine draws it into a texture of the player's (stage size times
 * scale, the stage's top row first) over `back`. Drawn again only when something changed; 0
 * without OpenGL. */
unsigned fplay_render(struct fplay *p, float scale, int back);
/* The frame shown at the stage's size (*w by *h), clear around it, as straight-alpha RGBA to free. */
uint8_t *fplay_rgba(struct fplay *p, int *w, int *h);
/* A texture of FPLAY_CLEAR drawn over what is under it, as the engine blends it. */
void fplay_image(ImDrawList *dl, unsigned tex, V2 a, V2 b);

#endif /* RANCE_CORE_H */
