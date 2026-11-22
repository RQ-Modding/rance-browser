/* The 3D tab: the game's 3D models (Data\PolyData in ReignData.red), each drawn as RQ MOD's engine
 * draws it for Rance Quest Magnum, by a copy of its 3D renderer (core/3d/rb3d.h): a monster with the
 * motions a fight plays it with, at the game's 30 frames a second, alone or on one of the game's
 * fight backgrounds with its shadow and the fight's camera; a room with its walk mesh; a fight
 * background; a map's prop. Its facts and textures, a frame saved as PNG, and for Rance Quest Magnum
 * RQ MOD's Lua lines: rq.monster.model for a monster's model, rq.battle.start's background and
 * rq.dungeon.place's look. Its textures as RQ MOD lists them (rq.image.textures), each saved as PNG
 * or as its UV map, and a mod's PNG tried on one as the engine draws it (rq.image.texture), with the
 * Lua lines that put it there. Without the 3D view in the build (its option 3d) the tab is not
 * shown. */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/utfsjis.h"

#include "browser.h"
#include "lua_lines.h"

#ifndef RB_3D

int tab3d_count(void) { return 0; }
void tab_3d(void) { }
void tab3d_select(int i) { (void)i; }
bool tab3d_select_name(const char *name) { (void)name; return false; }
bool tab3d_shot(const struct shot3d *s, char *err, size_t n)
{
	(void)s;
	snprintf(err, n, "this build has no 3D view");
	return false;
}
const char *tab3d_error(void) { return ""; }
bool tab3d_save_view(const char *path, char *err, size_t n)
{
	(void)path;
	snprintf(err, n, "this build has no 3D view");
	return false;
}
bool tab3d_texture_line(const char *raw, char *out, size_t n)
{
	(void)raw;
	(void)out;
	(void)n;
	return false;
}
bool tab3d_has_view(void) { return false; }
const char *tab3d_model_folder(int model) { (void)model; return NULL; }
struct thumb *tab3d_model_pic(int model, int px) { (void)model; (void)px; return NULL; }
void tab3d_pics_make(void) { }
bool tab3d_pics_idle(void) { return true; }
void tab3d_open_model(int model) { (void)model; }

#else

#include "rb3d.h"

#define MOT_MISSING (-2)   /* a motion of the game's whose MOT the folder lacks */
#define MAX_ENTRIES 64
#define FPS 30.0           /* the engine moves a motion at 30 frames a second */

/* A motion to play: one of the game's (MonsterInfo's, or a prop's on the map), a MOT whole, or none. */
struct choice {
	char label[320];
	char data[96];     /* the MOT it names, for --motion */
	char name[96];     /* the game's name for it (待機), or "" */
	int mot;           /* the MOT it plays (an index in the model's mots), -1 the rest pose, or MOT_MISSING */
	bool game;         /* played as the game plays it, as below */
	struct rb3d_play play;
};

static const char *const speed_names[] = { "Speed 1", "Speed 1/2", "Speed 1/4", "Speed 1/10" };
static const double speeds[] = { 1, 0.5, 0.25, 0.1 };
enum { UNDER_SQUARES, UNDER_BLACK, UNDER_WHITE, UNDER_COUNT };
static const char *const under_names[UNDER_COUNT] = { "Under it: squares", "Under it: black", "Under it: white" };

static struct {
	bool inited;
	struct rb3d_list l;
	/* the list */
	int kind;               /* the kind shown, or -1 for all */
	char search[96];
	int *idx, n;
	bool dirty, scroll;
	int sel, loaded;        /* a model's index in l.m, or -1 */
	/* the view */
	struct rb3d_view *v;
	bool no_view;           /* it cannot draw here: why says why */
	char why[512];
	int vw, vh;             /* its size */
	bool ok;                /* the model shown is loaded */
	char err[512];          /* what failed for the model shown */
	struct rb3d_info info;
	struct rb3d_draw d;     /* the camera and the toggles */
	bool fight_cam[2];      /* the game's fight camera: [0] a monster on a background, [1] a background */
	int under;
	/* the model shown: its MonsterInfo entries, its motions, its textures */
	int ents[MAX_ENTRIES], nent;
	int entry;              /* the entry shown (its no), or -1 */
	int want_entry;         /* the entry to show when the model picked loads (the Names tab's), or -1 */
	struct choice *ch;
	int nch, cur;
	int *tex, ntex;         /* indexes in G.sets[PK_TEX]: the pictures in its folder */
	/* its textures, as RQ MOD lists them, and what is tried on them (the view's tries) */
	struct rb3d_textures rows;
	bool rows_ok;           /* the folder has a model that reads */
	int *row_pic;           /* each row's picture in G.sets[PK_TEX], or -1 */
	int *other, nother;     /* the folder's pictures no row covers (indexes in G.sets[PK_TEX]) */
	int why_row;            /* the row a refusal is shown under (STAR_ROW: "*"'s), or -1 */
	char refusal[1200];     /* RQ MOD's text for a try it would refuse */
	float col[3];           /* Colours...'s hue, saturation and lightness */
	int warnings_seen;      /* the view's tries' warnings already told */
	struct rb3d_tries *shot_tries;   /* --shot's --texture, handed to the view at the load */
	/* playing */
	bool playing;
	int speed;
	double clock;           /* the view's clock, ms */
	double last;            /* igGetTime at the last frame */
	float frame;            /* the frame the last draw showed */
	/* a fight */
	int fight;              /* the background: a BG model's index, or -1 */
	bool fighting;          /* the view has it */
	/* --shot */
	bool shot;
	int shot_choice, shot_frame;
	bool shot_frame_given;
	bool prefs;
} T = { .kind = -1, .dirty = true, .sel = -1, .loaded = -1, .entry = -1, .want_entry = -1, .cur = -1, .fight = -1,
        .playing = true, .why_row = -1 };

/* ---- helpers ---- */

static void dim(const char *fmt, ...)
{
	char buf[700];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	igPushStyleColor_U32(ImGuiCol_Text, igGetColorU32_Col(ImGuiCol_TextDisabled, 1));
	igTextWrapped("%s", buf);
	igPopStyleColor(1);
}

static void red_text(const char *fmt, ...)
{
	char buf[700];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	igPushStyleColor_Vec4(ImGuiCol_Text, v4(1, 0.45f, 0.4f, 1));
	igTextWrapped("%s", buf);
	igPopStyleColor(1);
}

/* A fact: its name dim, its value beside it. */
static void fact(const char *key, const char *fmt, ...)
{
	char buf[700];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	igTextDisabled("%s", key);
	igSameLine(S(130), -1);
	igTextWrapped("%s", buf);
}

static const char *plural(int n, const char *one, const char *many)
{
	return n == 1 ? one : many;
}

static void init(void)
{
	if (T.inited)
		return;
	T.inited = true;
	rb3d_draw_default(&T.d, RB3D_CHARA);
	T.fight_cam[1] = true;   /* a background is seen from where a fight sees it */
	T.under = UNDER_SQUARES;
	if (!G.h.red)
		return;
	struct rb3d_prop_look looks[32];
	int nl = 0;
	if (G.rq && G.prof)
		for (int i = 0; i < G.prof->nlooks && nl < 32; i++, nl++) {
			looks[nl].object = G.prof->looks[i].object;
			looks[nl].name = G.prof->looks[i].name;
		}
	rb3d_list_read(G.h.red, G.dir, looks, nl, &T.l);
}

static void load_prefs(void)
{
	if (T.prefs)
		return;
	T.prefs = true;
	const char *s = settings_get("3d_under");
	if (s && atoi(s) >= 0 && atoi(s) < UNDER_COUNT)
		T.under = atoi(s);
}

static const struct rb3d_model *shown_model(void)
{
	return T.loaded >= 0 ? &T.l.m[T.loaded] : NULL;
}

static const struct rb3d_entry *shown_entry(void)
{
	return T.entry > 0 ? rb3d_list_entry(&T.l, T.entry) : NULL;
}

/* The warnings the view's tries gave since the last told (a tried file gone or broken since it was
 * tried: the game's picture drawn), each once: on stderr for a shot, else in the status line, after
 * lead when one is given. True when there was one. */
static bool tell_warnings_after(const char *lead)
{
	struct rb3d_tries *t = T.v ? rb3d_view_tries(T.v) : NULL;
	int n = rb3d_tries_warnings(t);
	bool any = n > T.warnings_seen;
	if (any) {
		char text[4096];
		size_t len = 0;
		text[0] = 0;
		if (lead && lead[0])
			len = (size_t)snprintf(text, sizeof text, "%s; ", lead);
		for (int i = T.warnings_seen; i < n; i++) {
			if (T.shot)
				fprintf(stderr, APP_EXE ": warning: %s\n", rb3d_tries_warning(t, i));
			else if (len < sizeof text)
				len += (size_t)snprintf(text + len, sizeof text - len, "%s%s", i > T.warnings_seen ? " · " : "",
				                        rb3d_tries_warning(t, i));
		}
		if (!T.shot)
			ui_status("%s", text);
	}
	T.warnings_seen = n;
	return any;
}

static void tell_warnings(void)
{
	tell_warnings_after(NULL);
}

/* ---- the model's entries, motions and textures (from the list, no GL) ---- */

/* The entries that draw model i: those whose データ is its folder, in Chara first and else in Etc, as
 * the list links them. */
static void find_entries(int i)
{
	const struct rb3d_model *m = &T.l.m[i];
	T.nent = 0;
	for (int no = 0; no < T.l.ne && T.nent < MAX_ENTRIES; no++) {
		const char *poly = T.l.e[no].poly;
		if (!poly || strcasecmp(poly, m->folder))
			continue;
		int k = rb3d_list_find(&T.l, RB3D_CHARA, poly);
		if (k < 0)
			k = rb3d_list_find(&T.l, RB3D_ETC, poly);
		if (k == i)
			T.ents[T.nent++] = no;
	}
}

/* play: as the game plays it, or NULL for the whole MOT round and round. */
static void add_choice(const char *label, const char *data, const char *name, int mot, const struct rb3d_play *play)
{
	struct choice *c = &T.ch[T.nch++];
	snprintf(c->label, sizeof c->label, "%s", label);
	snprintf(c->data, sizeof c->data, "%s", data ? data : "");
	snprintf(c->name, sizeof c->name, "%s", name ? name : "");
	c->mot = mot;
	c->game = play != NULL;
	if (play)
		c->play = *play;
}

/* "攻撃 · gokiburi_kougeki · 1 to 21, once": the frames the game shows and how. */
static void play_label(char *out, size_t n, const char *head, const struct rb3d_play *p, const char *tail)
{
	int first, last;
	rb3d_play_frames(p, &first, &last);
	if (!p->loop && first == last)
		snprintf(out, n, "%s · still on %d%s", head, first, tail);
	else
		snprintf(out, n, "%s · %d to %d, %s%s", head, first, last, p->loop ? "in a loop" : "once", tail);
}

/* The entry's motions as a fight plays them (or a prop's as the map shows it), each MOT of the folder
 * whole, and the rest pose. */
static void make_choices(const struct rb3d_model *m)
{
	const struct rb3d_entry *e = shown_entry();
	free(T.ch);
	T.ch = xcalloc(m->nmots + (e ? e->nmotions : 0) + 2, sizeof *T.ch);
	T.nch = 0;
	char head[256], label[320];
	struct rb3d_play p;
	if (e) {
		for (int i = 0; i < e->nmotions; i++) {
			const struct rb3d_motion *mo = &e->motions[i];
			int k = rb3d_model_mot(m, mo->data);
			rb3d_fight_play(mo, &p);
			snprintf(head, sizeof head, "%s · %s", mo->name[0] ? mo->name : "(no name)", mo->data);
			play_label(label, sizeof label, head, &p, k < 0 ? " (its MOT is not in the folder)" : "");
			add_choice(label, mo->data, mo->name, k < 0 ? MOT_MISSING : k, &p);
		}
	}
	if (!(e && e->nmotions) && m->object >= 0 && m->nmots) {
		int k = rb3d_model_mot(m, m->folder);
		if (k < 0)
			k = 0;
		rb3d_map_play(m, &p);
		snprintf(head, sizeof head, "On the map · %s", m->mots[k]);
		play_label(label, sizeof label, head, &p, "");
		add_choice(label, m->mots[k], "", k, &p);
	}
	for (int i = 0; i < m->nmots; i++) {
		snprintf(label, sizeof label, "%s · the whole file", m->mots[i]);
		add_choice(label, m->mots[i], "", i, NULL);
	}
	add_choice(m->nmots ? "None: the rest pose" : "None", "none", "", -1, NULL);
}

static int first_playable(void)
{
	for (int c = 0; c < T.nch; c++)
		if (T.ch[c].mot != MOT_MISSING)
			return c;
	return T.nch - 1;
}

/* --motion: a MOT's name or the game's name for a motion; the game's first, then the MOTs whole. */
static int choice_named(const char *name)
{
	for (int c = 0; c < T.nch; c++)
		if (T.ch[c].game && (!strcasecmp(T.ch[c].data, name) || (T.ch[c].name[0] && !strcmp(T.ch[c].name, name))))
			return c;
	for (int c = 0; c < T.nch; c++)
		if (!T.ch[c].game && !strcasecmp(T.ch[c].data, name))
			return c;
	return -1;
}

/* The leaf of a path (after its last \ or /). */
static const char *leaf_of(const char *path)
{
	const char *a = strrchr(path, '\\'), *b = strrchr(path, '/');
	const char *s = a > b ? a : b;
	return s ? s + 1 : path;
}

/* Whether the folder's picture pe (a file of the model's folder) is row r's file, or one of its frames. */
static bool row_covers(const struct rb3d_texture *r, const struct pic_entry *pe, bool *frame)
{
	const char *leaf = leaf_of(pe->raw);
	*frame = false;
	if (!strcasecmp(leaf, r->raw))
		return true;
	char base[256];
	int k = rb3d_texture_frame(leaf, base, sizeof base);
	if (k > 0 && k < r->frames && !strcasecmp(base, r->raw)) {
		*frame = true;
		return true;
	}
	return false;
}

static void list_textures(const struct rb3d_model *m)
{
	/* The 3D textures' folders are Data\PolyData's four, in the same order (TEX_CHARA... and
	 * RB3D_CHARA...). */
	struct pic_set *s = &G.sets[PK_TEX];
	free(T.tex);
	T.tex = xmalloc((s->n ? s->n : 1) * sizeof *T.tex);
	T.ntex = 0;
	for (int i = 0; i < s->n; i++)
		if (s->e[i].folder == m->kind && s->e[i].group && !strcasecmp(s->e[i].group, m->folder))
			T.tex[T.ntex++] = i;
	/* the rows, RQ MOD's (no GL), and the picture each one's file is */
	rb3d_textures_free(&T.rows);
	T.rows_ok = G.h.red && rb3d_textures(G.h.red, m->path, &T.rows);
	free(T.row_pic);
	T.row_pic = xmalloc((T.rows.n ? T.rows.n : 1) * sizeof *T.row_pic);
	free(T.other);
	T.other = xmalloc((T.ntex ? T.ntex : 1) * sizeof *T.other);
	T.nother = 0;
	for (int r = 0; r < T.rows.n; r++)
		T.row_pic[r] = -1;
	for (int k = 0; k < T.ntex; k++) {
		const struct pic_entry *pe = &s->e[T.tex[k]];
		bool covered = false;
		for (int r = 0; r < T.rows.n; r++) {
			bool frame;
			if (row_covers(&T.rows.t[r], pe, &frame)) {
				covered = true;
				if (!frame && T.row_pic[r] < 0)
					T.row_pic[r] = T.tex[k];
			}
		}
		if (!covered)
			T.other[T.nother++] = T.tex[k];
	}
	T.why_row = -1;
	T.refusal[0] = 0;
}

/* ---- the view ---- */

static bool ensure_view(int w, int h)
{
	if (T.v)
		return true;
	if (T.no_view || !G.f.red)
		return false;
	T.v = rb3d_view_new(G.f.red, w, h, T.why, sizeof T.why);
	if (!T.v) {
		T.no_view = true;
		return false;
	}
	T.vw = w;
	T.vh = h;
	return true;
}

static void reset_camera(void)
{
	const struct rb3d_model *m = shown_model();
	struct rb3d_draw d;
	rb3d_draw_default(&d, m ? m->kind : RB3D_CHARA);
	T.d.yaw = d.yaw;
	T.d.pitch = d.pitch;
	T.d.zoom = d.zoom;
	T.d.pan_x = d.pan_x;
	T.d.pan_y = d.pan_y;
	T.d.fov = d.fov;
	if (m && m->kind == RB3D_BG)
		T.fight_cam[1] = true;
}

/* Which fight camera applies: 0 a monster on a background, 1 a background alone, -1 none. */
static int fight_ctx(void)
{
	const struct rb3d_model *m = shown_model();
	if (!m)
		return -1;
	if (m->kind == RB3D_BG)
		return 1;
	if (m->kind == RB3D_CHARA && T.fighting)
		return 0;
	return -1;
}

static bool fight_camera_now(void)
{
	int c = fight_ctx();
	return c >= 0 && T.fight_cam[c];
}

static bool has_motion(void)
{
	return T.ok && T.cur >= 0 && T.cur < T.nch && T.ch[T.cur].mot >= 0 && rb3d_view_mot_frames(T.v) > 0;
}

/* The frames the motion shown runs through. */
static void frame_range(int *lo, int *hi)
{
	*lo = *hi = 0;
	if (!has_motion())
		return;
	const struct choice *c = &T.ch[T.cur];
	if (c->game) {
		rb3d_play_frames(&c->play, lo, hi);
	} else {
		*hi = rb3d_view_mot_frames(T.v) - 1;
	}
	if (*hi < *lo)
		*hi = *lo;
}

static void go_frame(float f)
{
	if (!has_motion())
		return;
	int lo, hi;
	frame_range(&lo, &hi);
	if (f < lo)
		f = (float)lo;
	if (f > hi)
		f = (float)hi;
	rb3d_view_set_frame(T.v, f);
	T.frame = f;
	T.playing = false;
}

static int frame_now(void)
{
	return (int)floorf(T.frame + 0.001f);
}

static void toggle_play(void)
{
	if (T.playing) {
		T.playing = false;
		return;
	}
	if (has_motion() && rb3d_view_stopped(T.v)) {
		int lo, hi;
		frame_range(&lo, &hi);
		rb3d_view_set_frame(T.v, (float)lo);
	}
	T.playing = true;
	T.last = igGetTime();
}

static void play_choice(int c)
{
	if (c < 0 || c >= T.nch)
		return;
	T.cur = c;
	const struct rb3d_model *m = shown_model();
	if (!T.ok || !m || T.ch[c].mot == MOT_MISSING)
		return;
	const struct choice *ch = &T.ch[c];
	T.err[0] = 0;
	if (!rb3d_view_motion(T.v, ch->mot >= 0 ? m->mots_raw[ch->mot] : NULL, T.err, sizeof T.err))
		return;   /* the model stays, in its rest pose; err says why */
	if (ch->game)
		rb3d_view_play(T.v, &ch->play);
	int lo, hi;
	frame_range(&lo, &hi);
	T.frame = (float)lo;
}

/* A monster stands on the fight background chosen, as a fight sets it, at its entry's size. */
static void apply_fight(void)
{
	T.fighting = false;
	const struct rb3d_model *m = shown_model();
	if (!T.ok || !m || m->kind != RB3D_CHARA)
		return;
	const struct rb3d_entry *e = shown_entry();
	float off[3] = { e ? e->x : 0, e ? e->y : 0, e ? e->z : 0 };
	const char *bg = T.fight >= 0 ? T.l.m[T.fight].path : NULL;
	char err[512] = "";
	if (rb3d_view_fight(T.v, bg, e ? e->scale : 1, off, e ? e->make_shadow : true, e ? e->shadow_bone_radius : 0,
	                    err, sizeof err))
		T.fighting = bg != NULL;
	else
		snprintf(T.err, sizeof T.err, "%s", err);
}

static void set_entry(int no)
{
	const struct rb3d_model *m = shown_model();
	if (!m)
		return;
	T.entry = no;
	make_choices(m);
	play_choice(first_playable());
	apply_fight();
}

static void unload(void)
{
	free(T.ch);
	T.ch = NULL;
	T.nch = 0;
	T.cur = -1;
	free(T.tex);
	T.tex = NULL;
	T.ntex = 0;
	rb3d_textures_free(&T.rows);
	T.rows_ok = false;
	free(T.row_pic);
	T.row_pic = NULL;
	free(T.other);
	T.other = NULL;
	T.nother = 0;
	T.why_row = -1;
	T.refusal[0] = 0;
	T.nent = 0;
	T.entry = -1;
	T.ok = false;
	T.fighting = false;
	T.err[0] = 0;
	T.frame = 0;
	memset(&T.info, 0, sizeof T.info);
}

static void load(void)
{
	if (T.sel == T.loaded)
		return;
	unload();
	T.loaded = T.sel;
	if (T.sel < 0)
		return;
	const struct rb3d_model *m = &T.l.m[T.sel];
	find_entries(T.sel);
	T.entry = T.want_entry > 0 ? T.want_entry : m->entry;
	T.want_entry = -1;
	make_choices(m);
	T.cur = T.shot ? T.shot_choice : first_playable();
	list_textures(m);
	reset_camera();
	T.last = igGetTime();
	if (!ensure_view(T.vw > 0 ? T.vw : 640, T.vh > 0 ? T.vh : 480))
		return;
	/* the tries are a model's: another model starts with none (a shot with its --texture) */
	rb3d_view_set_tries(T.v, T.shot_tries);
	T.shot_tries = NULL;
	T.warnings_seen = 0;
	bool loaded = rb3d_view_load(T.v, m->path, T.err, sizeof T.err);
	tell_warnings();
	if (!loaded)
		return;
	T.ok = true;
	rb3d_view_info(T.v, &T.info);
	if (T.fight >= 0)
		apply_fight();
	char fight_err[512];
	snprintf(fight_err, sizeof fight_err, "%s", T.err);
	play_choice(T.cur);
	if (!T.err[0] && fight_err[0])
		snprintf(T.err, sizeof T.err, "%s", fight_err);
	if (T.shot && has_motion()) {
		/* A shot of frame N is always the same picture: the motion on N, and the clock (which the UV
		 * scroll reads) at N's time from the motion's first frame. */
		int lo, hi;
		frame_range(&lo, &hi);
		int f = T.shot_frame_given ? T.shot_frame : lo;
		if (f < lo || f > hi) {
			snprintf(T.err, sizeof T.err, "Frame %d is not in the motion's frames, %d to %d", f, lo, hi);
			f = f < lo ? lo : hi;
		}
		go_frame((float)f);
		T.clock = (f - lo) * 1000.0 / FPS;
	}
}

/* Loads the model shown again (with what is tried on it now), on the same motion, frame and fight. */
static void reload(void)
{
	const struct rb3d_model *m = shown_model();
	if (!m || !T.v)
		return;
	int cur = T.cur;
	float frame = T.frame;
	bool playing = T.playing;
	T.err[0] = 0;
	T.ok = false;
	T.fighting = false;
	bool loaded = rb3d_view_load(T.v, m->path, T.err, sizeof T.err);
	tell_warnings();
	if (!loaded)
		return;
	T.ok = true;
	rb3d_view_info(T.v, &T.info);
	if (T.fight >= 0)
		apply_fight();
	char fight_err[512];
	snprintf(fight_err, sizeof fight_err, "%s", T.err);
	play_choice(cur);
	if (!T.err[0] && fight_err[0])
		snprintf(T.err, sizeof T.err, "%s", fight_err);
	if (!playing && has_motion())
		go_frame(frame);
	T.playing = playing;
}

static void advance(void)
{
	double now = igGetTime(), dt = (now - T.last) * 1000.0;
	T.last = now;
	if (!T.playing || T.shot)
		return;
	if (dt > 250)
		dt = 250;   /* a pause of the window does not jump ahead */
	if (dt < 0)
		dt = 0;
	T.clock += dt * speeds[T.speed];
}

static void draw_params(struct rb3d_draw *d)
{
	*d = T.d;
	d->fight_camera = fight_camera_now();
	d->skeleton = T.d.skeleton && T.info.bones > 0;
	d->walk = T.d.walk && T.info.walk_triangles > 0;
	d->back = T.under == UNDER_WHITE ? RB3D_BACK_WHITE : T.under == UNDER_BLACK ? RB3D_BACK_BLACK : RB3D_BACK_ALPHA;
}

/* Draws the frame; false (the model dropped, err saying why) when the drawing failed. */
static bool draw(const struct rb3d_draw *d)
{
	char err[512] = "";
	if (rb3d_view_draw(T.v, (uint64_t)T.clock, d, err, sizeof err)) {
		T.frame = rb3d_view_frame(T.v);
		return true;
	}
	T.ok = false;
	T.fighting = false;
	snprintf(T.err, sizeof T.err, "%s", err[0] ? err : "The model could not be drawn");
	return false;
}

/* Dragging turns the orbit camera: it leaves the fight's own. */
static void leave_fight_camera(void)
{
	int c = fight_ctx();
	if (c < 0 || !T.fight_cam[c])
		return;
	T.fight_cam[c] = false;
	struct rb3d_draw d;
	rb3d_draw_default(&d, RB3D_CHARA);   /* from the front, a little above, as a fight sees it */
	T.d.yaw = d.yaw;
	T.d.pitch = d.pitch;
	T.d.zoom = d.zoom;
	T.d.pan_x = T.d.pan_y = 0;
}

static void mouse(float w, float h)
{
	ImGuiIO *io = igGetIO_Nil();
	if (igIsItemActive() && (io->MouseDelta.x != 0 || io->MouseDelta.y != 0)) {
		float dx = io->MouseDelta.x, dy = io->MouseDelta.y;
		if (igIsMouseDown_Nil(ImGuiMouseButton_Left)) {
			leave_fight_camera();
			T.d.yaw = fmodf(T.d.yaw - dx * 0.4f, 360.0f);
			T.d.pitch -= dy * 0.4f;
			if (T.d.pitch > 89)
				T.d.pitch = 89;
			if (T.d.pitch < -89)
				T.d.pitch = -89;
		} else if (igIsMouseDown_Nil(ImGuiMouseButton_Right) || igIsMouseDown_Nil(ImGuiMouseButton_Middle)) {
			leave_fight_camera();
			/* A pixel's move in the box's radii, at the camera's distance, so the model follows the mouse. */
			float hf = T.d.fov * 3.14159265f / 180, vf = 2 * atanf(tanf(hf / 2) * h / w);
			float k = 1.05f / (T.d.zoom * sinf(fminf(hf, vf) / 2));
			float per = 2 * k * tanf(hf / 2) / w;
			T.d.pan_x -= dx * per;
			T.d.pan_y += dy * per;
		}
	}
	if (igIsItemHovered(0) && io->MouseWheel != 0) {
		leave_fight_camera();
		T.d.zoom *= powf(1.15f, io->MouseWheel);
		if (T.d.zoom < 0.1f)
			T.d.zoom = 0.1f;
		if (T.d.zoom > 50)
			T.d.zoom = 50;
	}
	if (igIsItemHovered(0) && igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left))
		reset_camera();
}

static void stage(void)
{
	V2 av = igGetContentRegionAvail();
	float w = floorf(av.x), h = floorf(av.y);
	if (w < 16 || h < 16)
		return;
	float bw = w, bh = h;
	if (fight_camera_now()) {
		/* A fight's 3D is drawn in a view of 1024 by 378 (the top of the screen, over the fight's
		 * panels); the camera's 40 degrees run across it. */
		if (w * 378 > h * 1024)
			bw = floorf(h * 1024 / 378);
		else
			bh = floorf(w * 378 / 1024);
	}
	int pw = (int)bw, ph = (int)bh;
	if (T.v && (pw != T.vw || ph != T.vh)) {
		char err[512] = "";
		if (rb3d_view_resize(T.v, pw, ph, err, sizeof err)) {
			T.vw = pw;
			T.vh = ph;
		} else {
			snprintf(T.err, sizeof T.err, "%s", err);
		}
	}
	V2 pos = igGetCursorScreenPos();
	V2 a = v2(pos.x + floorf((w - bw) / 2), pos.y + floorf((h - bh) / 2)), b = v2(a.x + bw, a.y + bh);
	igSetCursorScreenPos(a);
	igInvisibleButton("##view", v2(bw, bh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
	                  ImGuiButtonFlags_MouseButtonMiddle);
	mouse(bw, bh);
	ImDrawList *dl = igGetWindowDrawList();
	if (T.under == UNDER_SQUARES)
		draw_checker(dl, a, b, S(10), 0xff595959, 0xff7a7a7a);
	if (T.ok) {
		struct rb3d_draw d;
		draw_params(&d);
		if (draw(&d)) {
			int tw, th;
			unsigned tex = rb3d_view_texture(T.v, &tw, &th);
			if (tex)
				ImDrawList_AddImage(dl, texref(tex), a, b, v2(0, 0), v2(1, 1), 0xffffffff);
		}
	}
	ImDrawList_AddRect(dl, v2(a.x - 1, a.y - 1), v2(b.x + 1, b.y + 1), 0x50ffffff, 0, 1, 0);
}

/* ---- the middle: the controls and the view ---- */

static void entry_label(char *buf, size_t n, int no)
{
	const struct rb3d_entry *e = no > 0 ? rb3d_list_entry(&T.l, no) : NULL;
	if (e)
		snprintf(buf, n, "Entry %d · %s", e->no, e->name ? e->name : "");
	else
		snprintf(buf, n, "No entry");
}

static void step_buttons(bool moving, int lo, int hi)
{
	igBeginDisabled(!moving);
	igSameLine(0, S(4));
	if (igButton("|<", v2(0, 0)))
		go_frame((float)lo);
	if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		igSetTooltip("The motion's first frame");
	igSameLine(0, S(4));
	if (igButton("<", v2(0, 0)))
		go_frame((float)(frame_now() - 1));
	if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		igSetTooltip("One frame back (comma)");
	igSameLine(0, S(4));
	if (igButton(">", v2(0, 0)))
		go_frame((float)(frame_now() + 1));
	if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		igSetTooltip("One frame on (period)");
	igSameLine(0, S(4));
	if (igButton(">|", v2(0, 0)))
		go_frame((float)hi);
	if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		igSetTooltip("The motion's last frame");
	igEndDisabled();
}

static void motion_controls(void)
{
	bool any = T.nch > 1;   /* something besides the rest pose to choose */
	bool moving = has_motion();
	int lo, hi;
	frame_range(&lo, &hi);
	if (igButton(T.playing ? "Pause" : "Play", v2(S(70), 0)))
		toggle_play();
	if (igIsItemHovered(0))
		igSetTooltip("Space. The clock also moves the textures that scroll.");
	if (any)
		step_buttons(moving, lo, hi);
	igSameLine(0, S(12));
	igSetNextItemWidth(S(110));
	if (igBeginCombo("##speed", speed_names[T.speed], 0)) {
		for (int i = 0; i < (int)(sizeof speeds / sizeof speeds[0]); i++)
			if (igSelectable_Bool(speed_names[i], T.speed == i, 0, v2(0, 0)))
				T.speed = i;
		igEndCombo();
	}
	if (T.nent > 1) {
		igSameLine(0, S(8));
		char prev[200];
		entry_label(prev, sizeof prev, T.entry);
		igSetNextItemWidth(S(200));
		if (igBeginCombo("##entry", prev, 0)) {
			for (int k = 0; k < T.nent; k++) {
				char lab[200];
				entry_label(lab, sizeof lab, T.ents[k]);
				igPushID_Int(k);
				if (igSelectable_Bool(lab, T.ents[k] == T.entry, 0, v2(0, 0)))
					set_entry(T.ents[k]);
				igPopID();
			}
			igEndCombo();
		}
		if (igIsItemHovered(0))
			igSetTooltip("The game's MonsterInfo entries that draw this model, each with its own size and motions");
	}
	if (any) {
		igSameLine(0, S(8));
		igSetNextItemWidth(-1);
		if (igBeginCombo("##motion", T.cur >= 0 && T.cur < T.nch ? T.ch[T.cur].label : "", ImGuiComboFlags_HeightLarge)) {
			for (int c = 0; c < T.nch; c++) {
				igPushID_Int(c);
				if (igSelectable_Bool(T.ch[c].label, c == T.cur, T.ch[c].mot == MOT_MISSING ? ImGuiSelectableFlags_Disabled : 0,
				                      v2(0, 0)))
					play_choice(c);
				igPopID();
			}
			igEndCombo();
		}
		if (igIsItemHovered(0))
			igSetTooltip("The game's motions with the frames a fight plays them with, each MOT file of the folder\n"
			             "whole, or the rest pose");
	}
	if (moving) {
		int f = frame_now();
		char fmt[96];
		snprintf(fmt, sizeof fmt, "Frame %%d of %d to %d", lo, hi);
		igSetNextItemWidth(-1);
		if (igSliderInt("##frame", &f, lo, hi > lo ? hi : lo + 1, fmt, ImGuiSliderFlags_AlwaysClamp))
			go_frame((float)f);
		igTextDisabled("%.2f s of %.2f s, at the game's 30 frames a second%s", (T.frame - lo) / FPS, (hi - lo + 1) / FPS,
		               rb3d_view_stopped(T.v) ? " · stopped at its end" : "");
	}
}

static void view_controls(void)
{
	const struct rb3d_model *m = shown_model();
	igSetNextItemWidth(S(150));
	if (igBeginCombo("##under", under_names[T.under], 0)) {
		for (int i = 0; i < UNDER_COUNT; i++) {
			if (igSelectable_Bool(under_names[i], T.under == i, 0, v2(0, 0))) {
				T.under = i;
				if (!T.shot) {
					char v[8];
					snprintf(v, sizeof v, "%d", i);
					settings_set("3d_under", v);
				}
			}
		}
		igEndCombo();
	}
	if (igIsItemHovered(0))
		igSetTooltip("The renderer clears to black. Squares and white come from the frame drawn over black\n"
		             "and over white: where they differ, the model lets the back show through.");
	igSameLine(0, S(12));
	igCheckbox("Outline", &T.d.outline);
	if (igIsItemHovered(0))
		igSetTooltip("The game's outline, on at its 3D level 3");
	igSameLine(0, S(12));
	igCheckbox("Light map", &T.d.light_map);
	if (igIsItemHovered(0))
		igSetTooltip("The light baked into the model's second textures, on at the game's 3D level 3");
	if (T.info.bones) {
		igSameLine(0, S(12));
		igCheckbox("Bones", &T.d.skeleton);
		if (igIsItemHovered(0))
			igSetTooltip("A line from each bone to its parent, as the motion moves them");
	}
	if (T.info.walk_triangles) {
		igSameLine(0, S(12));
		igCheckbox("Walk mesh", &T.d.walk);
		if (igIsItemHovered(0))
			igSetTooltip("The room's floor where the party can walk (its mesh named collision, not drawn by the game)");
	}
	if (m && m->kind == RB3D_CHARA && T.l.counts[RB3D_BG]) {
		char prev[300];
		if (T.fight >= 0)
			snprintf(prev, sizeof prev, "In a fight on %s%s%s", T.l.m[T.fight].folder, T.l.m[T.fight].label[0] ? " · " : "",
			         T.l.m[T.fight].label);
		else
			snprintf(prev, sizeof prev, "Alone (no fight background)");
		igSetNextItemWidth(S(320));
		if (igBeginCombo("##fight", prev, ImGuiComboFlags_HeightLarge)) {
			if (igSelectable_Bool("Alone (no fight background)", T.fight < 0, 0, v2(0, 0))) {
				T.fight = -1;
				apply_fight();
			}
			for (int i = 0; i < T.l.n; i++) {
				const struct rb3d_model *b = &T.l.m[i];
				if (b->kind != RB3D_BG)
					continue;
				char lab[300];
				snprintf(lab, sizeof lab, "%s%s%s", b->folder, b->label[0] ? " · " : "", b->label);
				igPushID_Int(i);
				if (igSelectable_Bool(lab, T.fight == i, 0, v2(0, 0))) {
					T.fight = i;
					apply_fight();
				}
				igPopID();
			}
			igEndCombo();
		}
		if (igIsItemHovered(0))
			igSetTooltip("The model in a fight as the game sets one: in the middle place at its MonsterInfo size,\n"
			             "casting its shadow straight down on one of the game's fight backgrounds");
		if (T.fighting) {
			igSameLine(0, S(12));
			igCheckbox("Shadow", &T.d.shadow);
			igSameLine(0, S(12));
			igCheckbox("Fight camera", &T.fight_cam[0]);
			if (igIsItemHovered(0))
				igSetTooltip("The game's fight camera, its 40 degrees across the fight's view of 1024 by 378");
		}
	} else if (m && m->kind == RB3D_BG) {
		igSameLine(0, S(12));
		igCheckbox("Fight camera", &T.fight_cam[1]);
		if (igIsItemHovered(0))
			igSetTooltip("The game's fight camera, its 40 degrees across the fight's view of 1024 by 378");
	}
	if (igSmallButton("Reset the camera"))
		reset_camera();
	igSameLine(0, S(10));
	igTextDisabled("Left drag: turn · right drag: move · wheel: closer · double-click: back");
}

static void middle(void)
{
	const struct rb3d_model *m = T.sel >= 0 ? &T.l.m[T.sel] : NULL;
	if (!m) {
		igTextDisabled("Pick a model on the left.");
		return;
	}
	igText("%s", m->folder);
	if (m->label[0]) {
		igSameLine(0, S(10));
		igTextDisabled("%s", m->label);
	}
	if (T.no_view) {
		red_text("This window cannot draw the 3D models: %s", T.why);
		return;
	}
	if (T.ok)
		dim("%d %s · %d %s · %d %s · %d %s", T.info.meshes, plural(T.info.meshes, "mesh", "meshes"), T.info.triangles,
		    plural(T.info.triangles, "triangle", "triangles"), T.info.bones, plural(T.info.bones, "bone", "bones"), m->nmots,
		    plural(m->nmots, "motion", "motions"));
	if (T.err[0])
		red_text("%s", T.err);
	if (!T.ok)
		return;
	igSeparator();
	motion_controls();
	view_controls();
	stage();
}

/* ---- the right: the facts, the Lua lines, the picture, the textures ---- */

static void save_frame(const struct rb3d_model *m)
{
	struct rb3d_draw d;
	draw_params(&d);
	d.back = RB3D_BACK_ALPHA;
	int w = 0, h = 0;
	uint8_t *px = NULL;
	if (draw(&d))
		px = rb3d_view_rgba(T.v, &w, &h);
	if (!px) {
		ui_status("Could not draw the frame%s%s", T.err[0] ? ": " : "", T.err);
		return;
	}
	char name[300], path[2048];
	snprintf(name, sizeof name, "%s_%d.png", m->folder, frame_now());
	if (win_pick_save("Save the frame as PNG", name, "png", path, sizeof path))
		ui_status(save_png(path, px, w, h) ? "Saved %s" : "Could not write %s", path);
	free(px);
}

static void export_model(const struct rb3d_model *m)
{
	char name[300], path[2048], err[1024];
	snprintf(name, sizeof name, "%s.glb", m->folder);
	if (!win_pick_save("Export the model as glTF", name, "glb", path, sizeof path))
		return;
	char said[2300];
	if (rb3d_export_glb(T.v, &T.l, m, path, APP_NAME " " BROWSER_VERSION, NULL, err, sizeof err))
		snprintf(said, sizeof said, "Exported %s", path);
	else
		snprintf(said, sizeof said, "Could not export %s: %s", m->folder, err);
	/* the export reads each tried file again: one gone or broken since is the game's picture, said */
	if (!tell_warnings_after(said))
		ui_status("%s", said);
}

/* A model's folder as a mod's id part: gokiburi. */
static void model_id(const struct rb3d_model *m, char *id, size_t size)
{
	size_t n = 0;
	for (const char *c = m->folder; *c && n < size - 1; c++)
		id[n++] = (char)tolower((unsigned char)*c);
	id[n] = 0;
}

/* The row of the colours on every colour texture ("*"), for a refusal under it. */
#define STAR_ROW (-2)

/* A number as a mod would write it: the shortest text that reads back as the same double. */
static void num_text(double v, char *out, size_t n)
{
	snprintf(out, n, "%.15g", v);
	if (strtod(out, NULL) != v)
		snprintf(out, n, "%.17g", v);
}

/* "hue = 200, saturation = 1.5": a colours try's numbers as rq.TextureColors' fields, those that are
 * not their defaults (hue alone when all are). */
static void colors_fields(const struct rb3d_try *y, char *out, size_t n)
{
	char v[40], f[80];
	size_t len = 0;
	out[0] = 0;
	if (y->hue != 0 || (y->saturation == 1 && y->lightness == 1)) {
		num_text(y->hue, v, sizeof v);
		snprintf(f, sizeof f, LUA_COLOR_HUE, v);
		len += snprintf(out + len, len < n ? n - len : 0, "%s", f);
	}
	if (y->saturation != 1) {
		num_text(y->saturation, v, sizeof v);
		snprintf(f, sizeof f, LUA_COLOR_SATURATION, v);
		len += snprintf(out + len, len < n ? n - len : 0, "%s%s", len ? ", " : "", f);
	}
	if (y->lightness != 1) {
		num_text(y->lightness, v, sizeof v);
		snprintf(f, sizeof f, LUA_COLOR_LIGHTNESS, v);
		snprintf(out + len, len < n ? n - len : 0, "%s%s", len ? ", " : "", f);
	}
}

/* "hue 200, saturation 1.5, lightness 0.8": the numbers, for a row. */
static void colors_text(const struct rb3d_try *y, char *out, size_t n)
{
	char h[40], s[40], l[40];
	num_text(y->hue, h, sizeof h);
	num_text(y->saturation, s, sizeof s);
	num_text(y->lightness, l, sizeof l);
	snprintf(out, n, "hue %s, saturation %s, lightness %s", h, s, l);
}

/* The tries on the model shown, or NULL. */
static struct rb3d_tries *shown_tries(void)
{
	const struct rb3d_model *m = shown_model();
	struct rb3d_tries *t = T.v ? rb3d_view_tries(T.v) : NULL;
	return m && rb3d_tries_for(t, m->path) ? t : NULL;
}

/* "textures/a.png" or { "textures/a.png", "textures/b.png" }: a try's PNGs as a mod would name
 * them, each by its file's name in the mod's textures folder. */
static void try_pictures(const struct rb3d_try *y, bool list, char *out, size_t n)
{
	size_t len = 0;
	out[0] = 0;
	if (y->colors) {
		char f[200];
		colors_fields(y, f, sizeof f);
		snprintf(out, n, LUA_COLORS, f);
		return;
	}
	if (list)
		len += snprintf(out + len, len < n ? n - len : 0, "{ ");
	for (int i = 0; i < y->nfiles && len < n; i++) {
		char item[600];
		snprintf(item, sizeof item, LUA_TEXTURE_ITEM, leaf_of(y->files[i]));
		len += snprintf(out + len, len < n ? n - len : 0, "%s%s", i ? ", " : "", item);
	}
	if (list && len < n)
		snprintf(out + len, n - len, " }");
}

/* A file's name two tried PNGs share while their folders differ (Windows' names: any ASCII case),
 * or NULL. */
static const char *same_leaf(const struct rb3d_tries *t)
{
	int n = rb3d_tries_count(t);
	for (int i = 0; i < n; i++) {
		struct rb3d_try a;
		if (!rb3d_tries_get(t, i, &a))
			continue;
		for (int fa = 0; fa < a.nfiles; fa++) {
			for (int j = i; j < n; j++) {
				struct rb3d_try b;
				if (!rb3d_tries_get(t, j, &b))
					continue;
				for (int fb = j == i ? fa + 1 : 0; fb < b.nfiles; fb++) {
					if (!strcasecmp(leaf_of(a.files[fa]), leaf_of(b.files[fb])) &&
					    strcasecmp(a.files[fa], b.files[fb]))
						return leaf_of(a.files[fa]);
				}
			}
		}
	}
	return NULL;
}

/* The tries' Lua lines: every use of each texture, and for a monster's folder its own model. */
static void try_lines(const struct rb3d_model *m, const struct rb3d_entry *e)
{
	struct rb3d_tries *t = shown_tries();
	int n = rb3d_tries_count(t);
	if (!n)
		return;
	igSeparatorText("What is tried, from a mod");
	/* the lines: one per texture tried by its name, and one per colour texture "*" reaches */
	int lines = 0, files = 0;
	for (int i = 0; i < n; i++) {
		struct rb3d_try y;
		if (!rb3d_tries_get(t, i, &y))
			continue;
		if (strcmp(y.name, "*"))
			lines++;
		files += y.nfiles;
	}
	for (int r = 0; r < T.rows.n; r++)
		if (rb3d_tries_reaches(t, T.rows.t[r].name))
			lines++;
	if (files)
		dim("Every model that draws %s, with your %s in your mod's textures folder:", plural(lines, "this texture", "these textures"),
		    plural(files, "PNG", "PNGs"));
	else
		dim("Every model that draws %s:", plural(lines, "this texture", "these textures"));
	char line[4096], pics[1600], entries[1800];
	size_t elen = 0;
	entries[0] = 0;
	for (int i = 0; i < n; i++) {
		struct rb3d_try y;
		if (!rb3d_tries_get(t, i, &y))
			continue;
		char id[32];
		snprintf(id, sizeof id, "try%d", i);
		char texture[700];
		if (!strcmp(y.name, "*")) {
			/* rq.image.texture takes no "*": a line per colour texture it reaches */
			char f[200];
			colors_fields(&y, f, sizeof f);
			for (int r = 0; r < T.rows.n; r++) {
				if (!rb3d_tries_reaches(t, T.rows.t[r].name))
					continue;
				snprintf(texture, sizeof texture, "Data/PolyData/%s/%s/%s", rb3d_kind_folders[m->kind], m->folder, T.rows.t[r].name);
				snprintf(line, sizeof line, LUA_TEXTURE_COLORS, texture, f);
				snprintf(id, sizeof id, "try%d-%d", i, r);
				ui_lua_line(id, line);
			}
		} else {
			snprintf(texture, sizeof texture, "Data/PolyData/%s/%s/%s", rb3d_kind_folders[m->kind], m->folder, y.name);
			if (y.colors) {
				char f[200];
				colors_fields(&y, f, sizeof f);
				snprintf(line, sizeof line, LUA_TEXTURE_COLORS, texture, f);
			} else if (y.nfiles == 1) {
				snprintf(line, sizeof line, LUA_TEXTURE_PNG, texture, leaf_of(y.files[0]));
			} else {
				try_pictures(&y, false, pics, sizeof pics);
				snprintf(line, sizeof line, LUA_TEXTURE_LIST, texture, pics);
			}
			ui_lua_line(id, line);
		}
		try_pictures(&y, y.nfiles > 1, pics, sizeof pics);
		char entry[1800];
		snprintf(entry, sizeof entry, LUA_MODEL_TEXTURE, y.name, pics);
		elen += snprintf(entries + elen, elen < sizeof entries ? sizeof entries - elen : 0, "%s%s", i ? ", " : "", entry);
	}
	if (m->kind == RB3D_CHARA && e) {
		dim("Or on a mod's monster only, a model of its own:");
		char id[128];
		model_id(m, id, sizeof id);
		snprintf(line, sizeof line, LUA_MONSTER_TEXTURES, id, 100000 + e->no, entries);
		ui_lua_line("try-model", line);
	}
	/* the lines name each PNG by its file's name alone: two files of one name from two folders would
	 * be one file in the mod's textures folder */
	const char *same = same_leaf(t);
	if (same)
		red_text("Two tried PNGs from different folders are both named %s: give one another name in your mod's "
		         "textures folder, and in its line.", same);
}

static void lua_lines(const struct rb3d_model *m)
{
	const struct rb3d_entry *e = shown_entry();
	char line[512];
	if (m->kind == RB3D_CHARA && e) {
		igSeparatorText("From a mod");
		dim("Model %d of the game's table, for a mod's monster; a copy of it, whose size and place can change:",
		    100000 + e->no);
		char id[128];
		model_id(m, id, sizeof id);
		snprintf(line, sizeof line, LUA_MONSTER_MODEL, id, 100000 + e->no);
		ui_lua_line("model", line);
	} else if (m->kind == RB3D_BG && m->bg_no >= 0) {
		igSeparatorText("From a mod");
		dim("A fight of a mod's on this background:");
		snprintf(line, sizeof line, LUA_BATTLE_BG, m->folder);
		ui_lua_line("bg", line);
	} else if (m->kind == RB3D_ETC && m->look) {
		igSeparatorText("From a mod");
		dim("A mod's own object on a quest's map, with this look:");
		snprintf(line, sizeof line, LUA_PLACE_LOOK, m->look);
		ui_lua_line("look", line);
	} else if (m->kind == RB3D_MAP) {
		igSeparatorText("From a mod");
		dim("A room of the game's maps. A mod's quest map (rq.quests.define) takes the game's rooms by their "
		    "tileset, not by this folder.");
	}
	if (T.loaded == T.sel)
		try_lines(m, e);
}

/* A grid of the folder's pictures (indexes in G.sets[PK_TEX]); a click opens one in the 3D textures tab. */
static void picture_grid(const int *pics, int n, const char *id)
{
	const float cell = S(76);
	int cols = (int)(igGetContentRegionAvail().x / (cell + S(4)));
	if (cols < 1)
		cols = 1;
	igPushID_Str(id);
	for (int k = 0; k < n; k++) {
		int i = pics[k];
		const struct pic_entry *pe = &G.sets[PK_TEX].e[i];
		if (k % cols)
			igSameLine(0, S(4));
		V2 pos = igGetCursorScreenPos();
		igPushID_Int(k);
		if (igSelectable_Bool("##tex", false, 0, v2(cell, cell))) {
			set_select(PK_TEX, i);
			UI.want_tab = TAB_TEXTURES;
		}
		if (igIsItemHovered(0))
			igSetTooltip("%s\n%.1f KB", pe->title, pe->size / 1024.0);
		igPopID();
		struct thumb *t = pics_thumb(PK_TEX, i);
		if (t->tex)
			draw_fit_image(t->tex, t->w, t->h, v2(pos.x + S(4), pos.y + S(4)), cell - S(8));
	}
	igPopID();
}

/* "colour map", "colour and alpha map": the kinds a row is used as. */
static void kinds_text(unsigned kinds, char *out, size_t n)
{
	static const struct { unsigned bit; const char *name; } k[] = {
		{ RB3D_TEX_COLOR, "colour" }, { RB3D_TEX_ALPHA, "alpha" }, { RB3D_TEX_LIGHT, "light" },
		{ RB3D_TEX_NORMAL, "normal" }, { RB3D_TEX_SPECULAR, "specular" },
	};
	const char *names[5];
	int c = 0;
	for (int i = 0; i < 5; i++)
		if (kinds & k[i].bit)
			names[c++] = k[i].name;
	size_t len = 0;
	out[0] = 0;
	for (int i = 0; i < c && len < n; i++)
		len += snprintf(out + len, n - len, "%s%s", i == 0 ? "" : i == c - 1 ? " and " : ", ", names[i]);
	if (len < n)
		snprintf(out + len, n - len, "%s", c ? " map" : "texture");
}

static void save_row_png(const struct rb3d_model *m, const struct rb3d_texture *r, bool uv)
{
	char why[1200] = "";
	int w = 0, h = 0;
	uint8_t *px = uv ? rb3d_uv_map(G.h.red, m->path, r->name, &w, &h, why, sizeof why)
	                 : rb3d_texture_rgba(G.h.red, m->path, r->name, &w, &h, why, sizeof why);
	if (!px) {
		ui_status("Could not draw %s: %s", r->name, why);
		return;
	}
	char name[600], path[2048];
	const char *leaf = leaf_of(r->name);
	const char *dot = strrchr(leaf, '.');
	if (uv)
		snprintf(name, sizeof name, "%.*s_uv.png", dot ? (int)(dot - leaf) : (int)strlen(leaf), leaf);
	else
		snprintf(name, sizeof name, "%.*s.png", dot ? (int)(dot - leaf) : (int)strlen(leaf), leaf);
	if (win_pick_save(uv ? "Save the UV map as PNG" : "Save the texture as PNG", name, "png", path, sizeof path))
		ui_status(save_png(path, px, w, h) ? "Saved %s" : "Could not write %s", path);
	free(px);
}

/* Try a PNG (a PNG per frame for a texture with frames, picked together, taken in their names'
 * order) on row r, then load the model again with it; a refusal stays under the row. */
static void try_row(const struct rb3d_model *m, int r)
{
	const struct rb3d_texture *row = &T.rows.t[r];
	char **files = NULL;
	int n = 0;
	char title[600];
	if (row->frames > 1) {
		snprintf(title, sizeof title, "Try %d PNGs on %s, one per frame (taken in their names' order)", row->frames, row->name);
		if (!win_pick_open_many(title, "PNG pictures", "*.png", &files, &n))
			return;
	} else {
		char one[2048];
		snprintf(title, sizeof title, "Try a PNG on %s", row->name);
		if (!win_pick_open(title, "PNG pictures", "*.png", one, sizeof one))
			return;
		files = xmalloc(sizeof *files);
		files[n++] = xstrdup(one);
	}
	struct rb3d_tries *t = shown_tries();
	bool fresh = !t;
	if (fresh)
		t = rb3d_tries_new(G.h.red, m->path);
	if (rb3d_tries_png(t, row->name, (const char *const *)files, NULL, n, T.refusal, sizeof T.refusal)) {
		if (fresh) {
			rb3d_view_set_tries(T.v, t);
			T.warnings_seen = 0;
		}
		T.why_row = -1;
		T.refusal[0] = 0;
		reload();
	} else {
		if (fresh)
			rb3d_tries_free(t);
		T.why_row = r;
	}
	for (int i = 0; i < n; i++)
		free(files[i]);
	free(files);
}

static void untry_name(const char *name)
{
	struct rb3d_tries *t = shown_tries();
	if (rb3d_tries_untry(t, name))
		reload();
	T.why_row = -1;
}

/* Colours by numbers on the texture name ("*": every colour texture), from Colours...'s sliders,
 * then the model loaded again; a refusal stays under row r. */
static void set_colors(const struct rb3d_model *m, const char *name, int r)
{
	/* the numbers as the sliders show them: whole degrees, factors to the hundredth */
	double hue = round(T.col[0]), sat = round(T.col[1] * 100.0) / 100.0, light = round(T.col[2] * 100.0) / 100.0;
	struct rb3d_tries *t = shown_tries();
	bool fresh = !t;
	if (fresh)
		t = rb3d_tries_new(G.h.red, m->path);
	if (rb3d_tries_colors(t, name, hue, sat, light, T.refusal, sizeof T.refusal)) {
		if (fresh) {
			rb3d_view_set_tries(T.v, t);
			T.warnings_seen = 0;
		}
		T.why_row = -1;
		T.refusal[0] = 0;
		reload();
	} else {
		if (fresh)
			rb3d_tries_free(t);
		T.why_row = r;
	}
}

/* Colours...: the sliders start from what is tried on the texture name (or "*"'s on it), else from
 * the game's numbers (0, 1, 1). */
static void open_colors(const char *name)
{
	struct rb3d_tries *t = shown_tries();
	int k = rb3d_tries_find(t, name);
	if (k < 0 && strcmp(name, "*") && rb3d_tries_reaches(t, name))
		k = rb3d_tries_find(t, "*");
	struct rb3d_try y;
	if (k >= 0 && rb3d_tries_get(t, k, &y) && y.colors) {
		T.col[0] = (float)y.hue;
		T.col[1] = (float)y.saturation;
		T.col[2] = (float)y.lightness;
	} else {
		T.col[0] = 0;
		T.col[1] = 1;
		T.col[2] = 1;
	}
	igOpenPopup_Str("colours", 0);
}

/* Colours...'s popup: three sliders, the model loaded again when one is let go. */
static void colors_popup(const struct rb3d_model *m, const char *name, int r)
{
	if (!igBeginPopup("colours", 0))
		return;
	if (!strcmp(name, "*"))
		igTextUnformatted("Colours on every colour texture", NULL);
	else
		igText("Colours on %s", name);
	dim("The game's own picture, each frame, turned as RQ MOD turns it; grey, white and black stay grey.");
	dim("The model loads again when you let go of a slider.");
	bool apply = false;
	igSetNextItemWidth(S(280));
	igSliderFloat("Hue", &T.col[0], -360, 360, "%.0f degrees", ImGuiSliderFlags_AlwaysClamp);
	apply |= igIsItemDeactivatedAfterEdit();
	igSetNextItemWidth(S(280));
	igSliderFloat("Saturation", &T.col[1], 0, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	apply |= igIsItemDeactivatedAfterEdit();
	igSetNextItemWidth(S(280));
	igSliderFloat("Lightness", &T.col[2], 0, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	apply |= igIsItemDeactivatedAfterEdit();
	dim("Ctrl+click a slider to type a number.");
	if (apply)
		set_colors(m, name, r);
	if (T.why_row == r && T.refusal[0])
		red_text("%s", T.refusal);
	igEndPopup();
}

/* The colours on every colour texture ("*"), above the rows: what is tried, Colours... and The game's. */
static void star_row(const struct rb3d_model *m)
{
	bool any = false;
	for (int r = 0; r < T.rows.n && !any; r++)
		any = (T.rows.t[r].kinds & RB3D_TEX_COLOR) && !T.rows.t[r].missing;
	if (!any || !T.ok || T.loaded != T.sel)
		return;
	igPushID_Str("star");
	struct rb3d_tries *t = shown_tries();
	int k = rb3d_tries_find(t, "*");
	igAlignTextToFramePadding();
	dim("Every colour texture:");
	igSameLine(0, S(6));
	if (igSmallButton("Colours..."))
		open_colors("*");
	if (igIsItemHovered(0))
		igSetTooltip("The same colours by numbers on each colour texture of the model, as rq.monster.model's\n"
		             "textures[\"*\"] puts them; a texture tried by its name keeps its own try");
	colors_popup(m, "*", STAR_ROW);
	struct rb3d_try y;
	if (k >= 0 && rb3d_tries_get(t, k, &y)) {
		igSameLine(0, S(6));
		if (igSmallButton("The game's"))
			untry_name("*");
		if (igIsItemHovered(0))
			igSetTooltip("The game's own colours again on the textures it reached");
		char c[160];
		colors_text(&y, c, sizeof c);
		igPushStyleColor_Vec4(ImGuiCol_Text, v4(0.45f, 0.85f, 0.5f, 1));
		igTextWrapped("Tried: colours, %s", c);
		igPopStyleColor(1);
	}
	if (T.why_row == STAR_ROW && T.refusal[0])
		red_text("%s", T.refusal);
	igPopID();
}

static void texture_row(const struct rb3d_model *m, int r)
{
	const struct rb3d_texture *row = &T.rows.t[r];
	const float cell = S(64);
	igPushID_Int(r);
	V2 pos = igGetCursorScreenPos();
	int pic = T.row_pic[r];
	if (pic >= 0) {
		if (igSelectable_Bool("##pic", false, 0, v2(cell, cell))) {
			set_select(PK_TEX, pic);
			UI.want_tab = TAB_TEXTURES;
		}
		if (igIsItemHovered(0))
			igSetTooltip("Open it in the 3D textures tab");
		struct thumb *th = pics_thumb(PK_TEX, pic);
		if (th->tex)
			draw_fit_image(th->tex, th->w, th->h, v2(pos.x + S(3), pos.y + S(3)), cell - S(6));
	} else {
		igDummy(v2(cell, cell));
	}
	igSameLine(0, S(8));
	igBeginGroup();
	igTextWrapped("%s", row->name);
	char kinds[80];
	kinds_text(row->kinds, kinds, sizeof kinds);
	if (row->missing) {
		dim("%s · not in the folder", kinds);
	} else {
		char frames[48] = "";
		if (row->frames > 1)
			snprintf(frames, sizeof frames, " · %d frames", row->frames);
		dim("%s · %d x %d · alpha %s%s%s", kinds, row->w, row->h, row->alpha ? "yes" : "no", frames,
		    row->alpha_map ? " · its own alpha map" : "");
	}
	struct rb3d_tries *t = shown_tries();
	int k = rb3d_tries_find(t, row->name);
	struct rb3d_try y;
	if (k >= 0 && rb3d_tries_get(t, k, &y)) {
		char files[1600];
		size_t len = 0;
		files[0] = 0;
		for (int i = 0; i < y.nfiles && len < sizeof files; i++)
			len += snprintf(files + len, sizeof files - len, "%s%s", i ? ", " : "", leaf_of(y.files[i]));
		igPushStyleColor_Vec4(ImGuiCol_Text, v4(0.45f, 0.85f, 0.5f, 1));
		if (y.colors) {
			colors_text(&y, files, sizeof files);
			igTextWrapped("Tried: colours, %s", files);
		} else if (y.nfiles > 1) {
			igTextWrapped("Tried, frame by frame in this order: %s", files);
		} else {
			igTextWrapped("Tried: %s", files);
		}
		igPopStyleColor(1);
	} else if (rb3d_tries_reaches(t, row->name)) {
		dim("Tried: the colours on every colour texture");
	}
	if (!row->missing) {
		if (igSmallButton("Save as PNG..."))
			save_row_png(m, row, false);
		if (igIsItemHovered(0))
			igSetTooltip("The game's picture%s", row->frames > 1 ? " (its first frame)" : "");
		igSameLine(0, S(6));
		if (igSmallButton("Save the UV map..."))
			save_row_png(m, row, true);
		if (igIsItemHovered(0))
			igSetTooltip("The triangles the model draws with this file, white on see-through at the picture's\n"
			             "size: lay it over the picture in a paint program");
		if (T.ok && T.loaded == T.sel) {
			/* the tries on a line of their own, so the panel's width keeps them in view */
			if (igSmallButton(row->frames > 1 ? "Try PNGs..." : "Try a PNG..."))
				try_row(m, r);
			if (igIsItemHovered(0)) {
				if (row->frames > 1)
					igSetTooltip("A PNG per frame, %d picked together, taken in their names' order (frame2 before\n"
					             "frame10), drawn as RQ MOD draws a mod's pictures", row->frames);
				else
					igSetTooltip("A PNG drawn on the model as RQ MOD draws a mod's picture");
			}
			if (row->kinds & RB3D_TEX_COLOR) {
				igSameLine(0, S(6));
				if (igSmallButton("Colours..."))
					open_colors(row->name);
				if (igIsItemHovered(0))
					igSetTooltip("Colours by numbers on the game's picture%s: hue, saturation and lightness, as\n"
					             "RQ MOD turns them", row->frames > 1 ? ", each frame" : "");
				colors_popup(m, row->name, r);
			}
			if (k >= 0) {
				igSameLine(0, S(6));
				if (igSmallButton("The game's"))
					untry_name(row->name);
				if (igIsItemHovered(0))
					igSetTooltip("The game's own picture again");
			}
		}
	}
	if (T.why_row == r && T.refusal[0])
		red_text("%s", T.refusal);
	igEndGroup();
	igPopID();
}

static void textures(const struct rb3d_model *m)
{
	igSeparatorText("Its textures");
	if (!T.rows_ok) {
		if (!T.ntex) {
			igTextDisabled("No pictures in its folder.");
			return;
		}
		dim("%d %s in its folder; a click opens one in the 3D textures tab.", T.ntex, plural(T.ntex, "picture", "pictures"));
		picture_grid(T.tex, T.ntex, "all");
		return;
	}
	if (!T.rows.n)
		igTextDisabled("Its materials name no texture.");
	else
		dim("%d %s its materials use, as RQ MOD lists them; a picture opens in the 3D textures tab.", T.rows.n,
		    plural(T.rows.n, "texture", "textures"));
	star_row(m);
	for (int r = 0; r < T.rows.n; r++)
		texture_row(m, r);
	if (T.nother) {
		dim("%d other %s in its folder, which no material names:", T.nother, plural(T.nother, "picture", "pictures"));
		picture_grid(T.other, T.nother, "other");
	}
}

static void right(void)
{
	if (T.sel < 0)
		return;
	const struct rb3d_model *m = &T.l.m[T.sel];
	igSeparatorText(m->folder);
	fact("Kind", "%s", rb3d_kind_titles[m->kind]);
	fact("Path", "Data\\PolyData\\%s\\%s", rb3d_kind_folders[m->kind], m->folder);
	if (m->label[0])
		fact("The game's name", "%s", m->label);
	if (m->en[0])
		fact("In English", "%s", m->en);
	if (m->look)
		fact("RQ MOD's look", "%s", m->look);
	if (m->bg_no >= 0)
		fact("Fight background", "number %d of the game's", m->bg_no);
	if (m->object >= 0)
		fact("Map prop", "number %d of the game's map objects", m->object);
	const struct rb3d_entry *e = T.loaded == T.sel ? shown_entry() : NULL;
	if (e) {
		if (T.nent > 1)
			fact("MonsterInfo", "entry %d, model %d (one of %d entries that draw it)", e->no, 100000 + e->no, T.nent);
		else
			fact("MonsterInfo", "entry %d, model %d", e->no, 100000 + e->no);
		fact("In a fight", "scale %.2f, moved by (%.2f, %.2f, %.2f)%s", e->scale, e->x, e->y, e->z,
		     e->make_shadow ? "" : ", casting no shadow");
	}
	if (T.ok && T.loaded == T.sel) {
		igSeparatorText("The model");
		fact("Its file", "POL version %d, %.0f KB", T.info.pol_version, m->pol_size / 1024.0);
		fact("Meshes", "%d, with %d %s and %d %s", T.info.meshes, T.info.triangles, plural(T.info.triangles, "triangle", "triangles"),
		     T.info.materials, plural(T.info.materials, "material", "materials"));
		if (T.info.bones)
			fact("Bones", "%d", T.info.bones);
		if (T.info.walk_triangles)
			fact("Walk mesh", "%d %s", T.info.walk_triangles, plural(T.info.walk_triangles, "triangle", "triangles"));
		fact("Its box", "%.2f across, %.2f up, %.2f deep", T.info.size[0], T.info.size[1], T.info.size[2]);
		fact("Textures", "%d named by its materials", T.info.textures);
		fact("Motions", "%d MOT %s%s", m->nmots, plural(m->nmots, "file", "files"),
		     m->ntxa ? ", with texture animations" : "");
	}
	if (G.rq)
		lua_lines(m);
	if (T.ok && T.loaded == T.sel) {
		igSeparator();
		if (igButton("Save this frame as PNG...", v2(-1, 0)))
			save_frame(m);
		if (igIsItemHovered(0))
			igSetTooltip("The view at its size, see-through where nothing is drawn");
		if (igButton("Export the model as glTF (.glb)...", v2(-1, 0)))
			export_model(m);
		if (igIsItemHovered(0))
			igSetTooltip("Its meshes, materials and pictures, its bones and one animation per MOT, for\n"
			             "Blender and other 3D programs. What glTF cannot say (the light map, the outline,\n"
			             "the frames the game plays) is in the file's extras, named rq_...");
	}
	if (T.loaded == T.sel)
		textures(m);
}

/* ---- the list ---- */

static bool matches(int i)
{
	const struct rb3d_model *m = &T.l.m[i];
	if (T.kind >= 0 && m->kind != T.kind)
		return false;
	if (!T.search[0])
		return true;
	return ui_match(m->folder, T.search) || ui_match(m->label, T.search) || ui_match(m->en, T.search) ||
	       (m->look && ui_match(m->look, T.search)) || (m->entry > 0 && ui_number_match(m->entry, T.search)) ||
	       (m->entry > 0 && ui_number_match(100000 + m->entry, T.search)) || (m->bg_no >= 0 && ui_number_match(m->bg_no, T.search)) ||
	       (m->object >= 0 && ui_number_match(m->object, T.search));
}

static void refilter(void)
{
	if (!T.dirty)
		return;
	free(T.idx);
	T.idx = xmalloc((T.l.n ? T.l.n : 1) * sizeof *T.idx);
	T.n = 0;
	for (int i = 0; i < T.l.n; i++)
		if (matches(i))
			T.idx[T.n++] = i;
	T.dirty = false;
}

static int pos_of(int i)
{
	for (int k = 0; k < T.n; k++)
		if (T.idx[k] == i)
			return k;
	return -1;
}

static void left_panel(void)
{
	char preview[128];
	if (T.kind < 0)
		snprintf(preview, sizeof preview, "All kinds (%d)", T.l.n);
	else
		snprintf(preview, sizeof preview, "%s (%d)", rb3d_kind_titles[T.kind], T.l.counts[T.kind]);
	igSetNextItemWidth(-1);
	if (igBeginCombo("##kind", preview, 0)) {
		if (igSelectable_Bool("All kinds", T.kind < 0, 0, v2(0, 0))) {
			T.kind = -1;
			T.dirty = true;
		}
		for (int k = 0; k < RB3D_NKINDS; k++) {
			if (!T.l.counts[k])
				continue;
			char label[96];
			snprintf(label, sizeof label, "%s (%d)", rb3d_kind_titles[k], T.l.counts[k]);
			if (igSelectable_Bool(label, T.kind == k, 0, v2(0, 0))) {
				T.kind = k;
				T.dirty = true;
			}
		}
		igEndCombo();
	}
	char before[96];
	memcpy(before, T.search, sizeof before);
	ui_search_box("##search", "Search: a name or a number", T.search, sizeof T.search);
	if (strcmp(before, T.search))
		T.dirty = true;
	refilter();
	igTextDisabled("%d shown", T.n);
	if (T.l.errors[0])
		red_text("%s", T.l.errors);
	igBeginChild_Str("rows", v2(0, 0), ImGuiChildFlags_None, 0);
	float rh = igGetTextLineHeightWithSpacing() * 2;
	ImGuiListClipper *clip = ImGuiListClipper_ImGuiListClipper();
	ImGuiListClipper_Begin(clip, T.n, rh);
	int want = T.scroll ? pos_of(T.sel) : -1;
	if (want >= 0)
		ImGuiListClipper_IncludeItemByIndex(clip, want);
	ImDrawList *dl = igGetWindowDrawList();
	while (ImGuiListClipper_Step(clip)) {
		for (int k = clip->DisplayStart; k < clip->DisplayEnd; k++) {
			int i = T.idx[k];
			const struct rb3d_model *m = &T.l.m[i];
			igPushID_Int(i);
			V2 pos = igGetCursorScreenPos();
			if (igSelectable_Bool("##row", T.sel == i, 0, v2(0, rh - S(4)))) {
				T.sel = i;
				T.scroll = true;
			}
			igPopID();
			ImDrawList_AddText_Vec2(dl, v2(pos.x + S(4), pos.y + S(2)), igGetColorU32_Col(ImGuiCol_Text, 1), m->folder, NULL);
			char sub[300];
			snprintf(sub, sizeof sub, "%s", m->label[0] ? m->label : rb3d_kind_titles[m->kind]);
			size_t len = strlen(sub);
			if (m->look)
				snprintf(sub + len, sizeof sub - len, " · %s", m->look);
			len = strlen(sub);
			if (m->nmots)
				snprintf(sub + len, sizeof sub - len, " · %d %s", m->nmots, plural(m->nmots, "motion", "motions"));
			ImDrawList_AddText_Vec2(dl, v2(pos.x + S(4), pos.y + rh / 2), igGetColorU32_Col(ImGuiCol_TextDisabled, 1), sub, NULL);
			if (k == want) {
				igSetScrollHereY(0.5f);
				T.scroll = false;
			}
		}
	}
	ImGuiListClipper_End(clip);
	ImGuiListClipper_destroy(clip);
	igEndChild();
}

/* ---- the tab ---- */

static void keys(void)
{
	ImGuiIO *io = igGetIO_Nil();
	if (io->WantTextInput || batch_running() || igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopup) || !T.ok)
		return;
	if (igIsKeyPressed_Bool(ImGuiKey_Space, false))
		toggle_play();
	if (igIsKeyPressed_Bool(ImGuiKey_Comma, true))
		go_frame((float)(frame_now() - 1));
	if (igIsKeyPressed_Bool(ImGuiKey_Period, true))
		go_frame((float)(frame_now() + 1));
}

void tab_3d(void)
{
	init();
	load_prefs();
	refilter();
	int pos = pos_of(T.sel);
	if (pos < 0 && T.n)
		pos = 0;
	if (ui_nav_keys(&pos, T.n)) {
		T.sel = T.idx[pos];
		T.scroll = true;
	}
	load();
	advance();
	keys();
	igBeginChild_Str("left", v2(S(340), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX, 0);
	left_panel();
	igEndChild();
	igSameLine(0, -1);
	V2 av = igGetContentRegionAvail();
	float rw = S(420);
	igBeginChild_Str("middle", v2(av.x - rw - S(8), 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar |
	                 ImGuiWindowFlags_NoScrollWithMouse);
	middle();
	igEndChild();
	igSameLine(0, -1);
	igBeginChild_Str("right", v2(0, 0), ImGuiChildFlags_Borders, 0);
	right();
	igEndChild();
}

int tab3d_count(void)
{
	init();
	return T.l.n;
}

void tab3d_select(int i)
{
	init();
	if (i < 0 || i >= T.l.n)
		i = 0;
	if (T.l.n) {
		T.sel = i;
		T.scroll = true;
	}
}

/* "gokiburi", or with its kind's folder when two kinds have one of that name: "Etc/takarabako". */
bool tab3d_select_name(const char *name)
{
	init();
	int kind = -1;
	const char *folder = name, *sep = strpbrk(name, "/\\");
	if (sep) {
		for (int k = 0; k < RB3D_NKINDS; k++)
			if (strlen(rb3d_kind_folders[k]) == (size_t)(sep - name) && !strncasecmp(name, rb3d_kind_folders[k], sep - name))
				kind = k;
		if (kind < 0)
			return false;
		folder = sep + 1;
	}
	int i = rb3d_list_find(&T.l, kind, folder);
	if (i < 0)
		return false;
	tab3d_select(i);
	return true;
}

bool tab3d_shot(const struct shot3d *s, char *err, size_t n)
{
	init();
	T.shot = true;
	T.playing = false;
	T.prefs = true;   /* the settings file's choices do not change a shot */
	if (T.sel < 0) {
		snprintf(err, n, "%s has no 3D models", G.title);
		return false;
	}
	const struct rb3d_model *m = &T.l.m[T.sel];
	if (s->fight) {
		if (m->kind != RB3D_CHARA) {
			snprintf(err, n, "--fight takes a model of Chara (a monster's); %s is in %s", m->folder, rb3d_kind_folders[m->kind]);
			return false;
		}
		T.fight = rb3d_list_find(&T.l, RB3D_BG, s->fight);
		if (T.fight < 0) {
			snprintf(err, n, "%s has no fight background named %s", G.title, s->fight);
			return false;
		}
	} else if (s->fight_camera && m->kind != RB3D_BG) {
		snprintf(err, n, "--fight-camera needs --fight BG for a monster's model");
		return false;
	}
	T.fight_cam[0] = s->fight_camera;
	T.d.walk = s->walk;
	T.d.skeleton = s->bones;
	/* The motion is checked here, with the list, before the window opens (in the entry a model's
	 * number picked). */
	T.entry = T.want_entry > 0 ? T.want_entry : m->entry;
	make_choices(m);
	int c = s->motion ? choice_named(s->motion) : first_playable();
	if (c < 0) {
		snprintf(err, n, "%s has no motion named %s", m->folder, s->motion);
		return false;
	}
	if (T.ch[c].mot == MOT_MISSING) {
		snprintf(err, n, "the motion %s names a MOT that is not in %s", s->motion, m->folder);
		return false;
	}
	if (s->frame_given && T.ch[c].mot < 0) {
		snprintf(err, n, "--frame needs a motion; %s has none here", m->folder);
		return false;
	}
	T.shot_choice = c;
	T.shot_frame = s->frame;
	T.shot_frame_given = s->frame_given;
	/* the PNGs tried, checked now as the engine checks a mod's (the view takes them at the load) */
	rb3d_tries_free(T.shot_tries);
	T.shot_tries = NULL;
	if (s->ntries && !(T.shot_tries = tries_from_args(m->path, s->tries, s->ntries, err, n)))
		return false;
	return true;
}

bool tab3d_save_view(const char *path, char *err, size_t n)
{
	if (!T.ok || !T.v) {
		snprintf(err, n, "%s", tab3d_error()[0] ? tab3d_error() : "no model is shown");
		return false;
	}
	struct rb3d_draw d;
	draw_params(&d);
	d.back = RB3D_BACK_ALPHA;
	int w = 0, h = 0;
	uint8_t *px = draw(&d) ? rb3d_view_rgba(T.v, &w, &h) : NULL;
	if (!px) {
		snprintf(err, n, "could not draw the frame%s%s", T.err[0] ? ": " : "", T.err);
		return false;
	}
	bool ok = save_png(path, px, w, h);
	free(px);
	if (!ok)
		snprintf(err, n, "could not write %s", path);
	return ok;
}

/* ---- the 3D textures tab's Lua line ---- */

/* The last folder asked, with its rows. */
static struct {
	char *folder;
	struct rb3d_textures rows;
	bool ok;
} TL;

bool tab3d_has_view(void)
{
	return true;
}

bool tab3d_texture_line(const char *raw, char *out, size_t n)
{
	const char *slash = raw ? strrchr(raw, '\\') : NULL;
	if (!G.h.red || !slash)
		return false;
	size_t fl = (size_t)(slash - raw);
	if (!TL.folder || strlen(TL.folder) != fl || strncmp(TL.folder, raw, fl)) {
		free(TL.folder);
		rb3d_textures_free(&TL.rows);
		TL.folder = xmalloc(fl + 1);
		memcpy(TL.folder, raw, fl);
		TL.folder[fl] = 0;
		TL.ok = rb3d_textures(G.h.red, TL.folder, &TL.rows);
	}
	if (!TL.ok)
		return false;
	const char *leaf = slash + 1;
	const struct rb3d_texture *r = NULL;
	for (int i = 0; i < TL.rows.n && !r; i++)
		if (!strcasecmp(TL.rows.t[i].raw, leaf))
			r = &TL.rows.t[i];
	if (!r) {
		/* a frame of a colour texture: the list on its base (the engine refuses the frame's name) */
		char base[256];
		int k = rb3d_texture_frame(leaf, base, sizeof base);
		for (int i = 0; k > 0 && i < TL.rows.n && !r; i++)
			if (!strcasecmp(TL.rows.t[i].raw, base) && (TL.rows.t[i].kinds & RB3D_TEX_COLOR) && k < TL.rows.t[i].frames)
				r = &TL.rows.t[i];
	}
	if (!r || r->missing)
		return false;
	char *folder = sjis2utf(TL.folder, 0);
	for (char *c = folder; *c; c++)
		if (*c == '\\')
			*c = '/';
	char texture[1200];
	snprintf(texture, sizeof texture, "%s/%s", folder, r->name);
	free(folder);
	if (r->frames <= 1) {
		snprintf(out, n, LUA_TEXTURE_PNG, texture, r->name);
		return true;
	}
	/* the frames as the game names them: face.png, face[1].png, face[2].png... */
	char items[1800];
	size_t len = 0;
	items[0] = 0;
	const char *dot = strrchr(r->name, '.');
	int stem = dot ? (int)(dot - r->name) : (int)strlen(r->name);
	for (int k = 0; k < r->frames && len < sizeof items; k++) {
		char name[400], item[500];
		if (k == 0)
			snprintf(name, sizeof name, "%s", r->name);
		else
			snprintf(name, sizeof name, "%.*s[%d]%s", stem, r->name, k, dot ? dot : "");
		snprintf(item, sizeof item, LUA_TEXTURE_ITEM, name);
		len += snprintf(items + len, sizeof items - len, "%s%s", k ? ", " : "", item);
	}
	snprintf(out, n, LUA_TEXTURE_LIST, texture, items);
	return true;
}

const char *tab3d_error(void)
{
	if (T.no_view)
		return T.why[0] ? T.why : "No 3D view";
	if (T.sel >= 0 && T.loaded == T.sel && !T.ok)
		return T.err[0] ? T.err : "The model did not load";
	return T.err;
}

/* ---- the Names tab's pictures of a model ---- */

/* A picture asked by the Names tab, drawn by a view of its own so the tab's model stays as it is. Each
 * is allocated apart, so a thumbnail handed out stays where it is. */
struct model_pic {
	int model, px;
	struct thumb t;          /* state 1 asked, 2 drawn, 3 none */
};

static struct {
	struct rb3d_view *v;
	int px;                  /* the view's size */
	bool no_view;
	struct model_pic **p;
	int n, cap;
	int asked;               /* pictures in state 1 */
} P;

/* The model of Chara (else Etc) that model's MonsterInfo entry draws, as the list links them; -1. */
static int model_index(int model)
{
	const struct rb3d_entry *e = rb3d_list_entry(&T.l, model);
	if (!e || !e->poly)
		return -1;
	int k = rb3d_list_find(&T.l, RB3D_CHARA, e->poly);
	return k >= 0 ? k : rb3d_list_find(&T.l, RB3D_ETC, e->poly);
}

const char *tab3d_model_folder(int model)
{
	init();
	int k = model_index(model);
	return k >= 0 ? T.l.m[k].folder : NULL;
}

struct thumb *tab3d_model_pic(int model, int px)
{
	init();
	for (int i = 0; i < P.n; i++)
		if (P.p[i]->model == model && P.p[i]->px == px)
			return P.p[i]->t.state == 3 ? NULL : &P.p[i]->t;
	if (P.no_view || !G.f.red || px < 8 || model_index(model) < 0)
		return NULL;
	if (P.n == P.cap) {
		P.cap = P.cap ? P.cap * 2 : 64;
		P.p = xrealloc(P.p, P.cap * sizeof *P.p);
	}
	struct model_pic *mp = xcalloc(1, sizeof *mp);
	mp->model = model;
	mp->px = px;
	mp->t.state = 1;
	P.p[P.n++] = mp;
	P.asked++;
	return &mp->t;
}

/* Draws a picture asked for: the model alone, on the frame a fight puts its idle (slot 0) on, from where
 * the 3D tab first shows a monster, on nothing. False when it does not load or draw. */
static bool draw_pic(struct model_pic *mp)
{
	int k = model_index(mp->model);
	if (k < 0)
		return false;
	const struct rb3d_model *m = &T.l.m[k];
	char err[512];
	if (!P.v) {
		P.v = rb3d_view_new(G.f.red, mp->px, mp->px, err, sizeof err);
		if (!P.v) {
			P.no_view = true;
			return false;
		}
		P.px = mp->px;
	} else if (P.px != mp->px) {
		if (!rb3d_view_resize(P.v, mp->px, mp->px, err, sizeof err))
			return false;
		P.px = mp->px;
	}
	if (!rb3d_view_load(P.v, m->path, err, sizeof err))
		return false;
	const struct rb3d_entry *e = rb3d_list_entry(&T.l, mp->model);
	for (int i = 0; e && i < e->nmotions; i++) {
		const struct rb3d_motion *mo = &e->motions[i];
		int mot = mo->slot == 0 ? rb3d_model_mot(m, mo->data) : -1;
		if (mot < 0)
			continue;
		/* a motion that does not play leaves the rest pose */
		if (rb3d_view_motion(P.v, m->mots_raw[mot], err, sizeof err)) {
			struct rb3d_play play;
			rb3d_fight_play(mo, &play);
			rb3d_view_play(P.v, &play);
		}
		break;
	}
	struct rb3d_draw d;
	rb3d_draw_default(&d, RB3D_CHARA);
	d.back = RB3D_BACK_ALPHA;
	/* the clock stands at 0: the frame the play put it on */
	if (!rb3d_view_draw(P.v, 0, &d, err, sizeof err))
		return false;
	int w, h;
	uint8_t *rgba = rb3d_view_rgba(P.v, &w, &h);
	if (!rgba)
		return false;
	/* Cut to what is drawn, with a pixel round it: the camera frames the sphere round the model's box,
	 * so a monster standing up would leave most of a square empty. Nothing drawn keeps the square. */
	int x0 = w, y0 = h, x1 = -1, y1 = -1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (rgba[((size_t)y * w + x) * 4 + 3] > 8) {
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
			}
	if (x1 >= 0) {
		x0 = x0 > 0 ? x0 - 1 : 0;
		y0 = y0 > 0 ? y0 - 1 : 0;
		x1 = x1 < w - 1 ? x1 + 1 : w - 1;
		y1 = y1 < h - 1 ? y1 + 1 : h - 1;
		int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
		for (int y = 0; y < ch; y++)
			memmove(rgba + (size_t)y * cw * 4, rgba + ((size_t)(y0 + y) * w + x0) * 4, (size_t)cw * 4);
		w = cw;
		h = ch;
	}
	mp->t.tex = tex_from_rgba(rgba, w, h);
	mp->t.w = w;
	mp->t.h = h;
	free(rgba);
	return mp->t.tex != 0;
}

void tab3d_pics_make(void)
{
	if (!P.asked)
		return;
	Uint32 start = SDL_GetTicks();
	/* the newest first: the rows on screen now */
	for (int i = P.n - 1; i >= 0 && P.asked; i--) {
		struct model_pic *mp = P.p[i];
		if (mp->t.state != 1)
			continue;
		mp->t.state = draw_pic(mp) ? 2 : 3;
		P.asked--;
		if (SDL_GetTicks() - start >= 12)
			break;
	}
}

bool tab3d_pics_idle(void)
{
	return P.asked == 0;
}

void tab3d_open_model(int model)
{
	init();
	int k = model_index(model);
	const struct rb3d_entry *e = rb3d_list_entry(&T.l, model);
	if (k < 0 || !e)
		return;
	/* in the list: all kinds, no search */
	T.kind = -1;
	T.search[0] = 0;
	T.dirty = true;
	tab3d_select(k);
	if (T.loaded == k)
		set_entry(e->no);
	else
		T.want_entry = e->no;
	UI.want_tab = TAB_3D;
}

#endif /* RB_3D */
