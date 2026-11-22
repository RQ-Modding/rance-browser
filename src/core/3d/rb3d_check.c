/* --check's 3D part: every model's files read by the readers of the copy beside this file (no
 * OpenGL), then, with a GL context, every model loaded and drawn, the UV map of each of its textures
 * drawn and counted against the engine's meshes, every motion of its folder played
 * through all its frames, the game's own motions (MonsterInfo's) as a fight plays them, and each
 * monster on a fight background with its shadow, each step under the guard. What a game's files are
 * known to hold (motions made for another model's bones, names of files that are not there) is
 * listed by name, for the caller to compare with what it expects. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/archive.h"

#include "3d_internal.h"
#include "../cJSON.h"
#include "rb3d.h"
#include "rb3d_private.h"

struct report {
	void (*fail)(void *ctx, const char *text);
	void *ctx;
};

static void failf(const struct report *r, const char *fmt, ...)
{
	char buf[768];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (r->fail)
		r->fail(r->ctx, buf);
}

static void listf(cJSON *list, const char *fmt, ...)
{
	char buf[768];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	cJSON_AddItemToArray(list, cJSON_CreateString(buf));
}

static double now_ms(void)
{
	return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/* "Chara/gokiburi": a model as the lists name it. */
static const char *model_name(const struct rb3d_model *m, char *buf, size_t n)
{
	snprintf(buf, n, "%s/%s", rb3d_kind_folders[m->kind], m->folder);
	return buf;
}

/* What the reader pass learned of a model, for the drawing and the game's motions. */
struct facts {
	bool pol;           /* its POL read */
	int bones;          /* the POL's bones */
	int *mot_frames;    /* each MOT's frames, -1: it did not read */
	int *mot_bones;     /* each MOT's bones, -1: it did not read */
};

static bool bones_differ(const struct facts *f, int k)
{
	return f->pol && f->mot_bones[k] >= 0 && f->mot_bones[k] != f->bones;
}

/* ---- the reader pass ---- */

struct read_counts {
	int pols, pol_v1, pol_v2, pol_other;
	int meshes, triangles, materials, bones;
	int amts, oprs, mots, mot_frames, txas;
	int textures_named, meshes_over_4_weights;
};

struct read_job {
	struct archive *ar;
	const struct rb3d_model *m;
	struct facts *f;
	struct read_counts *c;
	cJSON *missing;          /* texture names that are no file */
	struct pol *pol;         /* the POL, while its MOTs are read */
	int k;                   /* the MOT read */
	struct mot *mot;
	const char **seen;       /* the texture names already counted for this model */
	int nseen;
	char file[640];          /* the file being read */
};

static struct archive_data *get(struct read_job *j, const char *stem, const char *ext)
{
	snprintf(j->file, sizeof j->file, "%s\\%s%s", j->m->path, stem, ext);
	return archive_get_by_name(j->ar, j->file);
}

/* A texture a material names, looked for as the engine looks for it (the model's folder, the name
 * as it is), once per model. */
static void check_texture(struct read_job *j, const char *name)
{
	if (!name)
		return;
	for (int i = 0; i < j->nseen; i++)
		if (!strcmp(j->seen[i], name))
			return;
	j->seen = xrealloc(j->seen, (j->nseen + 1) * sizeof *j->seen);
	j->seen[j->nseen++] = name;
	j->c->textures_named++;
	snprintf(j->file, sizeof j->file, "%s\\%s", j->m->path, name);
	if (!name[0] || !archive_exists_by_name(j->ar, j->file, NULL)) {
		char nm[300];
		listf(j->missing, "%s: %s", model_name(j->m, nm, sizeof nm), name[0] ? name : "(an empty name)");
	}
}

static void check_material(struct read_job *j, const struct pol_material_group *g, int depth)
{
	for (int t = 0; t < MAX_TEXTURE_TYPE; t++)
		check_texture(j, g->m.textures[t]);
	if (depth < 4)
		for (uint32_t c = 0; c < g->nr_children; c++)
			check_material(j, &g->children[c], depth + 1);
}

/* The POL, the names of its pictures, its .amt and its .opr, as the engine's model_load reads them. */
static void pol_fn(void *arg)
{
	struct read_job *j = arg;
	const struct rb3d_model *m = j->m;
	struct read_counts *c = j->c;
	struct archive_data *d = get(j, m->raw_folder, ".POL");
	if (!d)
		return;
	j->pol = pol_parse(d->data, d->size);
	archive_free_data(d);
	struct pol *pol = j->pol;
	if (!pol)
		return;
	j->f->pol = true;
	j->f->bones = (int)pol->nr_bones;
	c->pols++;
	if (pol->version == 1)
		c->pol_v1++;
	else if (pol->version == 2)
		c->pol_v2++;
	else
		c->pol_other++;
	c->meshes += (int)pol->nr_meshes;
	c->materials += (int)pol->nr_materials;
	c->bones += (int)pol->nr_bones;
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		struct pol_mesh *mesh = pol->meshes[i];
		if (!mesh)
			continue;
		c->triangles += (int)mesh->nr_triangles;
		for (uint32_t v = 0; v < mesh->nr_vertices; v++)
			if (mesh->vertices[v].nr_weights > 4) {
				c->meshes_over_4_weights++;
				break;
			}
	}
	for (uint32_t i = 0; i < pol->nr_materials; i++)
		check_material(j, &pol->materials[i], 0);
	d = get(j, m->raw_folder, ".amt");
	if (d) {
		struct amt *amt = amt_parse(d->data, d->size);
		archive_free_data(d);
		if (!amt)
			ERROR("%s: the .amt does not read", j->file);
		c->amts++;
		amt_free(amt);
	}
	d = get(j, m->raw_folder, ".opr");
	if (d) {
		opr_load(d->data, d->size, pol);
		archive_free_data(d);
		c->oprs++;
	}
}

/* A MOT and its .txa, as the engine's mot_load reads them. */
static void mot_fn(void *arg)
{
	struct read_job *j = arg;
	const struct rb3d_model *m = j->m;
	struct archive_data *d = get(j, m->mots_raw[j->k], ".MOT");
	if (!d)
		ERROR("%s: not in the archive", j->file);
	j->mot = mot_parse(d->data, d->size, m->mots_raw[j->k]);
	archive_free_data(d);
	if (!j->mot)
		ERROR("%s: does not read", j->file);
	j->f->mot_frames[j->k] = (int)j->mot->nr_frames;
	j->f->mot_bones[j->k] = (int)j->mot->nr_bones;
	j->c->mots++;
	j->c->mot_frames += (int)j->mot->nr_frames;
	d = get(j, m->mots_raw[j->k], ".txa");
	if (d) {
		txa_load(d->data, d->size, j->mot);
		archive_free_data(d);
		j->c->txas++;
	}
}

static cJSON *read_pass(struct archive *ar, const struct rb3d_list *l, struct facts *f, cJSON *gaps,
                        const struct report *r)
{
	double t0 = now_ms();
	struct read_counts c = { 0 };
	cJSON *missing = cJSON_AddArrayToObject(gaps, "textures_not_found");
	cJSON *differ = cJSON_AddArrayToObject(gaps, "motions_with_other_bones");
	for (int i = 0; i < l->n; i++) {
		const struct rb3d_model *m = &l->m[i];
		char nm[300], err[512];
		model_name(m, nm, sizeof nm);
		f[i].mot_frames = xmalloc((m->nmots ? m->nmots : 1) * sizeof(int));
		f[i].mot_bones = xmalloc((m->nmots ? m->nmots : 1) * sizeof(int));
		for (int k = 0; k < m->nmots; k++)
			f[i].mot_frames[k] = f[i].mot_bones[k] = -1;
		struct read_job j = { ar, m, &f[i], &c, missing, NULL, 0, NULL, NULL, 0, "" };
		if (!rb3d_guard(pol_fn, &j, err, sizeof err)) {
			failf(r, "%s: %s", nm, err);
			f[i].pol = false;   /* a jump after the POL read leaves it half set */
		} else if (!f[i].pol) {
			failf(r, "%s: its POL does not read (%s)", nm, j.file);
		}
		free(j.seen);
		if (!f[i].pol)
			continue;   /* the POL is left as the jump left it */
		for (int k = 0; k < m->nmots; k++) {
			j.k = k;
			j.mot = NULL;
			if (!rb3d_guard(mot_fn, &j, err, sizeof err)) {
				failf(r, "%s: %s", nm, err);
				f[i].mot_frames[k] = f[i].mot_bones[k] = -1;
				continue;   /* the MOT is left as the jump left it */
			}
			if (bones_differ(&f[i], k))
				listf(differ, "%s/%s: %d bones for the model's %d", nm, m->mots[k], f[i].mot_bones[k], f[i].bones);
			mot_free(j.mot);
		}
		pol_free(j.pol);
	}
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "pols", c.pols);
	cJSON_AddNumberToObject(o, "pol_v1", c.pol_v1);
	cJSON_AddNumberToObject(o, "pol_v2", c.pol_v2);
	cJSON_AddNumberToObject(o, "pol_other_versions", c.pol_other);
	cJSON_AddNumberToObject(o, "meshes", c.meshes);
	cJSON_AddNumberToObject(o, "triangles", c.triangles);
	cJSON_AddNumberToObject(o, "materials", c.materials);
	cJSON_AddNumberToObject(o, "bones", c.bones);
	cJSON_AddNumberToObject(o, "textures_named", c.textures_named);
	cJSON_AddNumberToObject(o, "meshes_over_4_weights", c.meshes_over_4_weights);
	cJSON_AddNumberToObject(o, "amts", c.amts);
	cJSON_AddNumberToObject(o, "oprs", c.oprs);
	cJSON_AddNumberToObject(o, "mots", c.mots);
	cJSON_AddNumberToObject(o, "mot_frames", c.mot_frames);
	cJSON_AddNumberToObject(o, "txas", c.txas);
	cJSON_AddNumberToObject(o, "ms", (int)(now_ms() - t0));
	return o;
}

/* ---- the game's motions (MonsterInfo), against the files ---- */

/* The model an entry draws, as the list links it: Chara, then Etc. */
static int entry_model(const struct rb3d_list *l, const struct rb3d_entry *e)
{
	if (!e->poly)
		return -1;
	int i = rb3d_list_find(l, RB3D_CHARA, e->poly);
	return i >= 0 ? i : rb3d_list_find(l, RB3D_ETC, e->poly);
}

static cJSON *entries_pass(const struct rb3d_list *l, const struct facts *f, cJSON *gaps)
{
	cJSON *no_model = cJSON_AddArrayToObject(gaps, "entries_without_model");
	cJSON *no_mot = cJSON_AddArrayToObject(gaps, "entry_motions_without_mot");
	cJSON *outside = cJSON_AddArrayToObject(gaps, "entry_frames_outside_mot");
	cJSON *differ = cJSON_AddArrayToObject(gaps, "entry_motions_with_other_bones");
	cJSON *unnamed = cJSON_AddArrayToObject(gaps, "chara_without_entry");
	int entries = 0, motions = 0, in_etc = 0;
	for (int no = 0; no < l->ne; no++) {
		const struct rb3d_entry *e = &l->e[no];
		if (!e->poly)
			continue;
		entries++;
		int i = entry_model(l, e);
		if (i < 0) {
			listf(no_model, "%d %s: %s", e->no, e->name ? e->name : "", e->poly);
			continue;
		}
		const struct rb3d_model *m = &l->m[i];
		if (m->kind == RB3D_ETC)
			in_etc++;
		for (int k = 0; k < e->nmotions; k++) {
			const struct rb3d_motion *mo = &e->motions[k];
			motions++;
			int t = rb3d_model_mot(m, mo->data);
			if (t < 0) {
				listf(no_mot, "%d %s, %s: %s", e->no, e->name ? e->name : "", mo->name ? mo->name : "", mo->data ? mo->data : "");
				continue;
			}
			if (bones_differ(&f[i], t))
				listf(differ, "%d %s, %s: %s", e->no, e->name ? e->name : "", mo->name ? mo->name : "", m->mots[t]);
			int n = f[i].mot_frames[t];
			if (n < 0)
				continue;
			bool bad = mo->begin < 0 || mo->end < mo->begin || mo->end > n - 1;
			if (mo->loop)
				bad = bad || mo->loop_begin < 0 || mo->loop_end < mo->loop_begin || mo->loop_end > n - 1;
			if (bad)
				listf(outside, "%d %s, %s: %s, frames %d to %d, loop %d to %d, of a MOT of %d", e->no, e->name ? e->name : "",
				      mo->name ? mo->name : "", m->mots[t], mo->begin, mo->end, mo->loop_begin, mo->loop_end, n);
		}
	}
	for (int i = 0; i < l->n; i++)
		if (l->m[i].kind == RB3D_CHARA && l->m[i].nentries == 0)
			listf(unnamed, "%s", l->m[i].folder);
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "entries", entries);
	cJSON_AddNumberToObject(o, "entries_in_etc", in_etc);
	cJSON_AddNumberToObject(o, "motions", motions);
	return o;
}

/* ---- the drawing (a GL context current) ---- */

struct draw_counts {
	int loaded, drawn, motions, frames, refused_for_bones, game_motions, game_frames, game_stopped, game_loops,
	    on_background, exports_kept;
	bool export_room, export_fight;   /* the export's test done on a room, on a monster in a fight */
	struct rb3d_uv_counts uv;         /* the UV maps (rb3d_view_uv_check) */
	int uv_models;
};

struct drawing {
	struct rb3d_view *v;
	const struct report *r;
	struct draw_counts c;
	double ms;              /* the view's clock */
	char err[512];
};

static bool draw(struct drawing *g, const struct rb3d_draw *d, const char *nm, const char *what)
{
	if (rb3d_view_draw(g->v, (uint64_t)g->ms, d, g->err, sizeof g->err))
		return true;
	failf(g->r, "%s: %s: %s", nm, what, g->err);
	return false;
}

/* A game motion as a fight plays it (rb3d_fight_play), by the clock at 30 frames a second: once, to
 * its end and past it, where it must stop on its end; in a loop, through the loop and round it once
 * more. The frame never leaves the frames the play shows. */
static bool play_game_motion(struct drawing *g, const struct rb3d_draw *d, const char *nm, const struct rb3d_entry *e,
                             const struct rb3d_motion *mo)
{
	char what[300];
	snprintf(what, sizeof what, "%d %s, %s", e->no, e->name ? e->name : "", mo->name ? mo->name : "");
	struct rb3d_play p;
	rb3d_fight_play(mo, &p);
	rb3d_view_play(g->v, &p);
	int lo, hi;
	rb3d_play_frames(&p, &lo, &hi);
	if (p.loop && p.loop_begin < lo)
		lo = p.loop_begin;
	int steps = (hi - lo) + (p.loop ? abs(p.loop_end - p.loop_begin) + 1 : 0) + 3;
	if (steps > 4000)
		steps = 4000;
	double base = g->ms;
	float fr = 0;
	for (int s = 0; s <= steps; s++) {
		g->ms = base + s * 1000.0 / 30.0;
		if (!draw(g, d, nm, what))
			return false;
		fr = rb3d_view_frame(g->v);
		if (fr < lo - 0.01f || fr > hi + 0.01f) {
			failf(g->r, "%s: %s: frame %.2f, outside its frames %d to %d", nm, what, fr, lo, hi);
			return true;
		}
		g->c.game_frames++;
	}
	g->c.game_motions++;
	if (p.loop) {
		g->c.game_loops++;
	} else if (!rb3d_view_stopped(g->v)) {
		failf(g->r, "%s: %s: played once, it did not stop", nm, what);
	} else if (fr < hi - 0.01f) {
		failf(g->r, "%s: %s: played once, it stopped on frame %.2f before its end %d", nm, what, fr, hi);
	} else {
		g->c.game_stopped++;
	}
	return true;
}

/* The 3D tab's Export button exports the model on show: the view must be as it was after it, the
 * same frame drawn the same (the clock standing still), the bones' pose, the shadow and the frame
 * put back. The file is made whole and not written, with the report, whose samples pose the model
 * through the engine (the button asks for none, so this is the export's most). */
static bool export_keeps_view(struct drawing *g, const struct rb3d_list *l, const struct rb3d_model *m,
                              const struct rb3d_draw *d, const char *nm, const char *what)
{
	int w0 = 0, h0 = 0, w1 = 0, h1 = 0;
	if (!draw(g, d, nm, what))
		return false;
	uint8_t *before = rb3d_view_rgba(g->v, &w0, &h0);
	float f0 = rb3d_view_frame(g->v);
	char err[512];
	cJSON *report = cJSON_CreateObject();
	bool ok = rb3d_export_glb(g->v, l, m, NULL, "check", report, err, sizeof err);
	cJSON_Delete(report);
	if (!ok)
		failf(g->r, "%s: %s: the export: %s", nm, what, err);
	uint8_t *after = ok && draw(g, d, nm, what) ? rb3d_view_rgba(g->v, &w1, &h1) : NULL;
	float f1 = rb3d_view_frame(g->v);
	bool same = before && after && w0 == w1 && h0 == h1 && !memcmp(before, after, (size_t)w0 * h0 * 4);
	if (ok && after && !same)
		failf(g->r, "%s: %s: the view is not drawn the same after the export", nm, what);
	else if (ok && after && f0 != f1)
		failf(g->r, "%s: %s: the export moved the frame from %.2f to %.2f", nm, what, f0, f1);
	else if (same)
		g->c.exports_kept++;
	free(before);
	free(after);
	return ok && same && f0 == f1;
}

static void draw_model(struct drawing *g, const struct rb3d_list *l, int i, const struct facts *f, int bg)
{
	const struct rb3d_model *m = &l->m[i];
	char nm[300];
	model_name(m, nm, sizeof nm);
	struct rb3d_draw d;
	rb3d_draw_default(&d, m->kind);
	d.skeleton = d.walk = true;
	if (!rb3d_view_load(g->v, m->path, g->err, sizeof g->err)) {
		failf(g->r, "%s: %s", nm, g->err);
		return;
	}
	g->c.loaded++;
	/* every texture's UV map, each group's triangles in it against the engine's mesh */
	char why[512];
	if (rb3d_view_uv_check(g->v, &g->c.uv, why, sizeof why))
		g->c.uv_models++;
	else
		failf(g->r, "%s: %s", nm, why);
	if (!draw(g, &d, nm, "at rest"))
		return;
	g->c.drawn++;
	if (m->kind == RB3D_MAP && !g->c.export_room) {
		g->c.export_room = true;
		export_keeps_view(g, l, m, &d, nm, "a room, exported");
	}
	/* each MOT of the folder, through all its frames */
	for (int k = 0; k < m->nmots; k++) {
		if (f->mot_frames[k] < 0)
			continue;   /* it did not read: counted by the reader pass */
		bool differ = bones_differ(f, k);
		if (!rb3d_view_motion(g->v, m->mots_raw[k], g->err, sizeof g->err)) {
			if (differ)
				g->c.refused_for_bones++;   /* the engine's own ERROR, as the game would meet it */
			else
				failf(g->r, "%s/%s: %s", nm, m->mots[k], g->err);
			continue;
		}
		if (differ)
			failf(g->r, "%s/%s: played, though its bones are not the model's", nm, m->mots[k]);
		int n = rb3d_view_mot_frames(g->v);
		for (int fr = 0; fr < n; fr++) {
			rb3d_view_set_frame(g->v, (float)fr);
			char what[200];
			snprintf(what, sizeof what, "%s, frame %d", m->mots[k], fr);
			if (!draw(g, &d, nm, what))
				return;   /* a failed draw drops the model */
			g->c.frames++;
		}
		g->c.motions++;
	}
	/* the game's motions, as a fight plays them */
	for (int no = 0; no < l->ne; no++) {
		const struct rb3d_entry *e = &l->e[no];
		if (entry_model(l, e) != i)
			continue;
		for (int k = 0; k < e->nmotions; k++) {
			const struct rb3d_motion *mo = &e->motions[k];
			int t = rb3d_model_mot(m, mo->data);
			if (t < 0 || f->mot_frames[t] < 0 || bones_differ(f, t))
				continue;   /* listed by the passes before */
			if (!rb3d_view_motion(g->v, m->mots_raw[t], g->err, sizeof g->err)) {
				failf(g->r, "%s: %d %s, %s: %s", nm, e->no, e->name ? e->name : "", mo->name ? mo->name : "", g->err);
				continue;
			}
			if (!play_game_motion(g, &d, nm, e, mo))
				return;
		}
	}
	/* a monster on a fight background with its shadow, playing and at rest */
	if (m->kind != RB3D_CHARA || bg < 0)
		return;
	const struct rb3d_entry *e = rb3d_list_entry(l, m->entry);
	float off[3] = { e ? e->x : 0, e ? e->y : 0, e ? e->z : 0 };
	if (!rb3d_view_fight(g->v, l->m[bg].path, e ? e->scale : 1, off, e ? e->make_shadow : true,
	                     e ? e->shadow_bone_radius : 0, g->err, sizeof g->err)) {
		failf(g->r, "%s: on %s: %s", nm, l->m[bg].folder, g->err);
		return;
	}
	d.shadow = true;
	d.fight_camera = true;
	if (!draw(g, &d, nm, "on a fight background"))
		return;
	/* once, on the first monster with bones and a motion: exported while it moves (its frame held
	 * half-way), then at rest, where the next draw does not pose the bones again */
	bool export_here = !g->c.export_fight && rb3d_view_mot_frames(g->v) > 2;
	if (export_here) {
		g->c.export_fight = true;
		rb3d_view_set_frame(g->v, (float)(rb3d_view_mot_frames(g->v) / 2) + 0.5f);
		export_keeps_view(g, l, m, &d, nm, "on a fight background, moving, exported");
	}
	if (!rb3d_view_motion(g->v, NULL, g->err, sizeof g->err)) {
		failf(g->r, "%s: the rest pose: %s", nm, g->err);
		return;
	}
	if (!draw(g, &d, nm, "on a fight background, at rest"))
		return;
	if (export_here)
		export_keeps_view(g, l, m, &d, nm, "on a fight background, at rest, exported");
	g->c.on_background++;
}

static cJSON *draw_pass(const char *red_path, const struct rb3d_list *l, const struct facts *f, const struct report *r)
{
	double t0 = now_ms();
	cJSON *o = cJSON_CreateObject();
	struct drawing g = { NULL, r, { 0 }, 0, "" };
	if (!rb3d_ready(g.err, sizeof g.err)) {
		failf(r, "The 3D view cannot draw here: %s", g.err);
		cJSON_AddStringToObject(o, "skipped", g.err);
		return o;
	}
	g.v = rb3d_view_new(red_path, 96, 72, g.err, sizeof g.err);
	if (!g.v) {
		failf(r, "No 3D view: %s", g.err);
		cJSON_AddStringToObject(o, "skipped", g.err);
		return o;
	}
	int bg = -1;
	for (int i = 0; i < l->n && bg < 0; i++)
		if (l->m[i].kind == RB3D_BG && f[i].pol)
			bg = i;
	for (int i = 0; i < l->n; i++)
		if (f[i].pol)
			draw_model(&g, l, i, &f[i], bg);
	rb3d_view_free(g.v);
	cJSON_AddNumberToObject(o, "loaded", g.c.loaded);
	cJSON_AddNumberToObject(o, "drawn", g.c.drawn);
	cJSON_AddNumberToObject(o, "motions", g.c.motions);
	cJSON_AddNumberToObject(o, "frames", g.c.frames);
	cJSON_AddNumberToObject(o, "refused_for_bones", g.c.refused_for_bones);
	cJSON_AddNumberToObject(o, "game_motions", g.c.game_motions);
	cJSON_AddNumberToObject(o, "game_frames", g.c.game_frames);
	cJSON_AddNumberToObject(o, "game_once_stopped_at_end", g.c.game_stopped);
	cJSON_AddNumberToObject(o, "game_in_a_loop", g.c.game_loops);
	cJSON_AddNumberToObject(o, "on_background", g.c.on_background);
	cJSON_AddNumberToObject(o, "exports_kept_the_view", g.c.exports_kept);
	cJSON *uv = cJSON_AddObjectToObject(o, "uv_maps");
	cJSON_AddNumberToObject(uv, "models", g.c.uv_models);
	cJSON_AddNumberToObject(uv, "maps", g.c.uv.maps);
	cJSON_AddNumberToObject(uv, "parts", g.c.uv.parts);
	cJSON_AddNumberToObject(uv, "triangles", g.c.uv.triangles);
	cJSON_AddNumberToObject(uv, "triangles_skipped", g.c.uv.skipped);
	cJSON_AddNumberToObject(uv, "light_meshes_without_light_uvs", g.c.uv.light_meshes_without_uvs);
	cJSON_AddNumberToObject(uv, "light_models_without_light_uvs", g.c.uv.light_models_without_uvs);
	if (bg >= 0)
		cJSON_AddStringToObject(o, "background", l->m[bg].folder);
	cJSON_AddNumberToObject(o, "ms", (int)(now_ms() - t0));
	return o;
}

struct cJSON *rb3d_check(struct aar_archive *red, const char *red_path, const struct rb3d_list *l, bool draw,
                         void (*fail)(void *ctx, const char *text), void *ctx)
{
	struct report r = { fail, ctx };
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "models", l->n);
	for (int k = 0; k < RB3D_NKINDS; k++)
		cJSON_AddNumberToObject(o, rb3d_kind_folders[k], l->counts[k]);
	cJSON *files = cJSON_AddObjectToObject(o, "files");
	int other_pols = 0;
	for (int i = 0; i < l->n; i++)
		other_pols += l->m[i].other_pols;
	cJSON_AddNumberToObject(files, "mot", l->nmot);
	cJSON_AddNumberToObject(files, "txa", l->ntxa);
	cJSON_AddNumberToObject(files, "opr", l->nopr);
	cJSON_AddNumberToObject(files, "amt", l->namt);
	cJSON_AddNumberToObject(files, "other_pols", other_pols);
	cJSON_AddNumberToObject(files, "folders_without_model", l->no_pol);
	cJSON_AddNumberToObject(files, "odd", l->odd);
	if (l->errors[0]) {
		cJSON_AddStringToObject(o, "table_errors", l->errors);
		failf(&r, "3D: %s", l->errors);
	}
	cJSON *gaps = cJSON_CreateObject();
	struct facts *f = xcalloc(l->n ? l->n : 1, sizeof *f);
	cJSON_AddItemToObject(o, "read", read_pass(&red->ar, l, f, gaps, &r));
	cJSON_AddItemToObject(o, "game", entries_pass(l, f, gaps));
	cJSON_AddItemToObject(o, "gaps", gaps);
	if (draw)
		cJSON_AddItemToObject(o, "draw", draw_pass(red_path, l, f, &r));
	for (int i = 0; i < l->n; i++) {
		free(f[i].mot_frames);
		free(f[i].mot_bones);
	}
	free(f);
	return o;
}
