/* The 3D view: RQ MOD's 3D renderer (the files beside this one, copied from the engine) drawing a
 * model of the game's ReignData.red into a texture as Rance Quest's TapirEngine draws it, and the
 * list of the game's models with the names its tables give them (rb3d_models.c). The rest of the
 * browser includes only this header, so no header of the copy (its OpenGL, cglm, the engine's
 * types) reaches it.
 *
 * Every view call runs on the thread that owns the GL context, with it current. */
#ifndef RB3D_H
#define RB3D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- the game's models (rb3d_models.c), read without OpenGL ---- */

/* The four folders of Data\PolyData. */
enum { RB3D_CHARA, RB3D_MAP, RB3D_BG, RB3D_ETC, RB3D_NKINDS };
extern const char *const rb3d_kind_folders[RB3D_NKINDS];   /* "Chara", "Map", "BG", "Etc" */
extern const char *const rb3d_kind_titles[RB3D_NKINDS];    /* "Monsters and figures", "Rooms"... */

/* A motion of a MonsterInfo entry, as the file gives it (rb3d_fight_play: as a fight plays it). */
struct rb3d_motion {
	int slot;            /* its number in the entry: 0 the idle (待機), 1 an attack... */
	char *name;          /* the game's name for it, UTF-8 */
	char *data;          /* the MOT it plays, without .MOT (gokiburi_taiki), UTF-8 */
	bool loop;           /* タイプ = ループ */
	int begin, end;      /* フレーム, as MOT frames */
	int loop_begin, loop_end;   /* ループフレーム (the same as フレーム when left out) */
	bool has_loop_range;
};

/* An entry of the game's MonsterInfo.txt (in ReignData.red): the model number 100000 + no. */
struct rb3d_entry {
	int no;
	char *name;          /* 名前, UTF-8 */
	char *poly;          /* データ: the Chara folder it draws, UTF-8; NULL for a number the file skips */
	float scale;         /* スケール, 1 when left out */
	float x, y, z;       /* 位置, in the game's space (z away from the camera) */
	bool make_shadow;    /* 影生成, true when left out: it casts a shadow in a fight */
	float shadow_bone_radius;   /* 影ボーン半径, 0 when left out */
	struct rb3d_motion *motions;
	int nmotions;
};

struct rb3d_model {
	int kind;
	char *folder;        /* gokiburi, UTF-8 */
	char *path;          /* Data\PolyData\Chara\gokiburi in the archive's own bytes: what the view loads */
	char *label;         /* the game's name for it, UTF-8, or "" */
	char *en;            /* an English name, or "" (the program sets it) */
	char *look;          /* RQ MOD's look for a prop (rq.dungeon.place), or NULL */
	int entry;           /* the first MonsterInfo entry that draws it (its no), or -1 */
	int nentries;        /* the entries that draw it */
	int bg_no;           /* its number in BattleBackground.acx, or -1 */
	int object;          /* its number in MapObject.acx, or -1 */
	int obj_state;       /* MapObject.acx: how a map shows its motion (0 still, 1 once, 2 round) */
	int obj_begin, obj_end, obj_loop_begin, obj_loop_end;   /* MapObject.acx: its motion's frames */
	char **mots;         /* the folder's .MOT files, without .MOT, sorted, UTF-8 */
	char **mots_raw;     /* the same in the archive's bytes, for rb3d_view_motion */
	int nmots;
	int ntxa, ntex;      /* its texture animations (.txa) and pictures */
	int other_pols;      /* .POL files not named after the folder */
	int odd;             /* its other files */
	uint32_t pol_size;
	bool has_pol;
	char *raw_folder;
};

struct rb3d_list {
	struct rb3d_model *m;   /* by kind, then by folder */
	int n;
	struct rb3d_entry *e;   /* MonsterInfo's entries, by no */
	int ne;
	int counts[RB3D_NKINDS];
	int nmot, ntxa, nopr, namt;
	int no_pol;             /* folders without a model named after them */
	int entries_without_model;   /* MonsterInfo entries whose データ is no folder */
	int odd;                /* entries of Data\PolyData that are none of the above */
	char errors[512];       /* a table that did not read */
};

/* RQ MOD's look for a prop (rq.dungeon.place's look): its number in MapObject.acx and its name. */
struct rb3d_prop_look {
	int object;
	const char *name;
};

struct aar_archive;

/* The models of red (the game's ReignData.red, opened), with the names of the game's tables:
 * MonsterInfo.txt in the archive, and Data/MapInfo.acx, BattleBackground.acx and MapObject.acx in
 * game_dir. looks name RQ MOD's props (none: NULL, 0). False when red has no 3D models; a table
 * that does not read leaves its names out, with a line in errors. */
bool rb3d_list_read(struct aar_archive *red, const char *game_dir, const struct rb3d_prop_look *looks, int nlooks,
                    struct rb3d_list *out);
/* A model by its folder (any case), in kind (-1: any); its index, or -1. */
int rb3d_list_find(const struct rb3d_list *l, int kind, const char *folder);
/* The entry for model number model (100001...) or entry number no, or NULL. */
const struct rb3d_entry *rb3d_list_entry(const struct rb3d_list *l, int model);
/* A MOT of the model by its name (any case), or -1. */
int rb3d_model_mot(const struct rb3d_model *m, const char *name);

/* How the game plays a motion: the frames and the state its scripts give the engine's motion. */
struct rb3d_play {
	int begin, end;            /* the frames: once, it stops at end; no frame below begin */
	int loop_begin, loop_end;  /* round, it goes on past loop_end from loop_begin (end not read) */
	int start;                 /* the frame the game puts it on */
	bool loop;                 /* round, else once */
};
/* A MonsterInfo motion as a fight plays it (Rance Quest's CMonster): the idle (slot 0) round its loop
 * from the loop's first frame (still on its first frame when its タイプ is not ループ); any other
 * once, from the loop's first frame to its end whatever its タイプ, the fight then going back to
 * the idle. */
void rb3d_fight_play(const struct rb3d_motion *mo, struct rb3d_play *out);
/* A prop's motion as a map shows it (MapObject.acx): from its first frame round (state 2) or once
 * (1), or still on its first frame (0). */
void rb3d_map_play(const struct rb3d_model *m, struct rb3d_play *out);
/* The frames a play shows: from its start to its end (once) or to its loop's end (round). */
void rb3d_play_frames(const struct rb3d_play *p, int *first, int *last);

/* ---- the view ---- */

/* Whether the view can draw with the current GL context: OpenGL 3.1 and GLSL 1.40, each call past
 * GL 1.1 found, and the renderer's three shader programs built and linked as the engine builds them
 * for Rance Quest. Asked once; later calls give the first answer. *why (may be NULL) says what is
 * missing. */
bool rb3d_ready(char *why, size_t n);

/* Runs fn(arg) so that a fatal error of the 3D code (libsys4's ERROR, which ends the engine) comes
 * back as false with its message in err (may be NULL), the browser still running; what the call
 * had built is left as it was (a viewer can afford the leak). Only on the thread that called it; a
 * guard inside a guard runs fn under the outer one. */
bool rb3d_guard(void (*fn)(void *arg), void *arg, char *err, size_t n);

struct rb3d_view;

/* A view of w by h pixels on the archive at red_path (the game's Data/ReignData.red), set as the
 * game's 3D level 3 sets a fight: the outline and the light maps on. NULL, with a message, when the
 * view cannot draw here or the archive does not open. */
struct rb3d_view *rb3d_view_new(const char *red_path, int w, int h, char *err, size_t n);
void rb3d_view_free(struct rb3d_view *v);
bool rb3d_view_resize(struct rb3d_view *v, int w, int h, char *err, size_t n);

/* Loads a model (its path in the archive's bytes: "Data\\PolyData\\Chara\\gokiburi" reads its .POL,
 * .amt and .opr, as the game names a model) in the place of the one shown, and takes away a fight
 * background. False with a message when it fails; the view then shows nothing. */
bool rb3d_view_load(struct rb3d_view *v, const char *path, char *err, size_t n);

/* The model in a fight as the game sets one (CBattleMode): on the fight background at bg_path (a
 * model's path) receiving the shadow, the model in the middle place at scale and offset (MonsterInfo's
 * スケール and 位置, the game's space), casting a shadow when make_shadow says so, with the shadow's
 * bone radius. bg_path NULL takes the background away and puts the model back at the origin. */
bool rb3d_view_fight(struct rb3d_view *v, const char *bg_path, float scale, const float offset[3],
                     bool make_shadow, float shadow_bone_radius, char *err, size_t n);

/* Plays a motion of the model's folder, by its MOT's name in the archive's bytes ("gokiburi_taiki"),
 * with its .txa if the folder has one; NULL or "" stops it: the model in its rest pose. It plays the
 * whole MOT, round and round, until rb3d_view_play sets the game's frames. */
bool rb3d_view_motion(struct rb3d_view *v, const char *name, char *err, size_t n);
/* The motion's frames and state as the game sets them (rb3d_fight_play, rb3d_map_play), the motion
 * put on the play's start. */
void rb3d_view_play(struct rb3d_view *v, const struct rb3d_play *p);
int rb3d_view_mot_frames(const struct rb3d_view *v);   /* the motion's MOT's frames, 0 for none */
bool rb3d_view_has_bones(const struct rb3d_view *v);

/* Puts the motion on a frame (fractions between frames), shown as it is by the next draw, whose clock
 * then moves the motion from there. A stopped motion plays again. */
void rb3d_view_set_frame(struct rb3d_view *v, float frame);
float rb3d_view_frame(const struct rb3d_view *v);      /* the frame the last draw showed */
bool rb3d_view_stopped(const struct rb3d_view *v);     /* a motion that does not go round met its end */

/* What a draw shows. */
enum { RB3D_BACK_BLACK, RB3D_BACK_WHITE, RB3D_BACK_ALPHA };
struct rb3d_draw {
	bool outline;        /* the game's outlines */
	bool light_map;      /* the baked light */
	bool shadow;         /* with a fight background: the shadow */
	bool skeleton;       /* lines from each bone to its parent */
	bool walk;           /* the walk mesh's edges */
	int back;            /* under the model: black (the renderer's own), white, or see-through
	                        (the frame drawn over black and over white, the alpha from the two) */
	bool fight_camera;   /* with a fight background: the game's camera and projection */
	float yaw, pitch;    /* the orbit camera, in degrees, around the model's box (a room's: its walk mesh's) */
	float zoom;          /* 1: the whole box in view; 2: twice as close */
	float pan_x, pan_y;  /* the orbit's centre moved, in the box's radii */
	float fov;           /* the orbit camera's horizontal field of view, in degrees */
};
void rb3d_draw_default(struct rb3d_draw *d, int kind);

/* Draws the model with the view's clock at ms: the motion moves on by the time since the last draw
 * at its 30 frames a second, as the engine moves it, and the UV scroll reads the clock. False with a
 * message when the drawing failed; the model is then dropped. */
bool rb3d_view_draw(struct rb3d_view *v, uint64_t ms, const struct rb3d_draw *d, char *err, size_t n);

/* The texture of the last draw (a GL texture name; its rows run from the top, straight alpha) and its
 * size. */
unsigned rb3d_view_texture(const struct rb3d_view *v, int *w, int *h);
/* The last draw's picture as RGBA, top row first: with a see-through back, the alpha from the two
 * draws. Free it. NULL when it fails. */
uint8_t *rb3d_view_rgba(struct rb3d_view *v, int *w, int *h);

/* What the model loaded is. */
struct rb3d_info {
	bool loaded;
	int pol_version;
	int meshes, triangles, materials, bones;
	int walk_triangles;  /* the walk mesh's, 0 for none */
	int textures;        /* the pictures its materials name */
	float size[3];       /* its box, in the model's units (metres) */
};
void rb3d_view_info(const struct rb3d_view *v, struct rb3d_info *out);

/* ---- --check (rb3d_check.c) ---- */

struct cJSON;
/* Every model of the list checked, as JSON: each one's .POL, .amt, .opr, .MOT and .txa read by the
 * copy's readers, the names its materials give looked for, and MonsterInfo's motions against the
 * MOTs they name; with draw (a GL context current, red_path the archive's file), also every model
 * loaded and drawn, every MOT played through all its frames, each game motion as a fight plays it,
 * and each monster on a fight background with its shadow. Each step runs under the guard; a step
 * that fails calls fail(ctx, text). What a game's own files are known to hold (a motion made for
 * other bones, a picture's name that is no file) goes to the lists under "gaps", not to fail. */
struct cJSON *rb3d_check(struct aar_archive *red, const char *red_path, const struct rb3d_list *l, bool draw,
                         void (*fail)(void *ctx, const char *text), void *ctx);

/* ---- the export (rb3d_export.c) ---- */

/* Writes the model the view shows (m, loaded with rb3d_view_load(m->path); l, may be NULL, gives the
 * game's names and motions) as a glTF 2.0 binary file at path (UTF-8): its meshes, materials and
 * pictures, its skeleton and one animation per MOT made for its bones, glTF's axes being the
 * engine's (y up, metres). generator names the program in the file. report (may be NULL) gets the
 * counts, what was left out and why, and numbers from the engine's own code for a checker: bone
 * heads and points at rest and posed on four frames of each animation, UVs, each picture's CRC and
 * each material's alpha. The view's pose is put back. path NULL makes the whole file and writes
 * nothing (the check's test that the view is left as it was). False with a message in err when it
 * fails; the file is then not written. */
bool rb3d_export_glb(struct rb3d_view *v, const struct rb3d_list *l, const struct rb3d_model *m,
                     const char *path, const char *generator, struct cJSON *report, char *err, size_t n);

#endif /* RB3D_H */
