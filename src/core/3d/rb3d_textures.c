/* A model's textures, PNGs and colours tried on them, and their UV maps, without OpenGL (rb3d.h).
 *
 * The rows and the checks are RQ MOD's own, from the copy of its rq_textures.c beside this file, and
 * a tried PNG is read, and tried colours turn the game's picture, by the copy of its reader
 * (../rq_pixels.c): the browser takes the pictures the game takes and refuses the others with the
 * engine's words. While a view loads a model, the copy of
 * the engine's model.c asks RE_mod_texture (rb3d.c) for each texture, which asks rb3d_tries_answer
 * here by the engine's rule (rq_textures_answer); model.c then draws the answer with the game's
 * alpha kind, as in the game. Each load reads the files again, as the engine does. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "3d_internal.h"
#include "reign.h"
#include "rq_pixels.h"
#include "rq_textures.h"
#include "rb3d.h"
#include "rb3d_private.h"

/* The engine's limits (modloader_texture.c): pictures tried in all (a list counts each frame, colours
 * count one, "*" one per colour texture it reaches), and a picture file's bytes. */
#define TRY_PICTURES_MAX 4096
#define TRY_FILE_BYTES (256u << 20)
#define PATH_MAX_ 1024

static void say(char *why, size_t n, const char *fmt, ...)
{
	if (!why || !n)
		return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(why, n, fmt, ap);
	va_end(ap);
}

/* A folder or a name in the archive's terms, in place: ASCII upper case, \, no \ at the end. */
static void norm(char *s)
{
	sjis_normalize_path(s);
	size_t n = strlen(s);
	while (n > 0 && s[n - 1] == '\\')
		s[--n] = 0;
}

/* "Data/PolyData/Chara/gokiburi": a model's folder as RQ MOD's texts show it. */
static char *folder_shown(const char *path)
{
	char *u = sjis2utf(path, 0);
	for (char *p = u; *p; p++)
		if (*p == '\\')
			*p = '/';
	size_t n = strlen(u);
	while (n > 0 && u[n - 1] == '/')
		u[--n] = 0;
	return u;
}

/* ---- the rows ---- */

bool rb3d_textures(struct aar_archive *red, const char *path, struct rb3d_textures *out)
{
	memset(out, 0, sizeof *out);
	struct rq_texture_list *l = red ? rq_textures_list(&red->ar, path) : NULL;
	if (!l)
		return false;
	out->t = xcalloc(l->n ? l->n : 1, sizeof *out->t);
	out->n = l->n;
	for (int i = 0; i < l->n; i++) {
		const struct rq_texture *r = &l->rows[i];
		struct rb3d_texture *t = &out->t[i];
		t->name = sjis2utf(r->name, 0);
		t->raw = xstrdup(r->name);
		t->kinds = r->kinds;
		t->kind = rq_textures_kind(r->kinds);
		t->alpha_map = r->alpha_map;
		t->frames = r->frames;
		t->missing = r->missing;
		t->w = r->w;
		t->h = r->h;
		t->alpha = r->alpha;
	}
	rq_textures_free(l);
	return true;
}

void rb3d_textures_free(struct rb3d_textures *t)
{
	for (int i = 0; i < t->n; i++) {
		free(t->t[i].name);
		free(t->t[i].raw);
	}
	free(t->t);
	memset(t, 0, sizeof *t);
}

int rb3d_texture_frame(const char *name, char *base, size_t n)
{
	return rq_textures_frame(name, base, n);
}

/* ---- the tries ---- */

struct try {
	char *name;     /* as the .POL names it (Shift-JIS), or "*" */
	char *norm;     /* the same in the archive's terms */
	char *name_utf; /* UTF-8 */
	char *shown;    /* as it was given (UTF-8) */
	int nfiles;     /* 0 for colours */
	char **files;   /* UTF-8 paths, one per frame */
	char **given;   /* each as it was given */
	bool colors;    /* colours by numbers, on the game's own picture */
	double hue, saturation, lightness;
	bool warned;
};

struct rb3d_tries {
	char *path;          /* the model's folder in the archive's terms */
	char *shown;         /* Data/PolyData/Chara/gokiburi */
	struct rq_texture_list *list;   /* NULL: no model there */
	struct try *t;
	int n;
	int warnings;
	char warning[1200];
};

struct rb3d_tries *rb3d_tries_new(struct aar_archive *red, const char *path)
{
	struct rb3d_tries *t = xcalloc(1, sizeof *t);
	t->path = xstrdup(path);
	rq_textures_strip_mark(t->path);
	norm(t->path);
	t->shown = folder_shown(path);
	t->list = red ? rq_textures_list(&red->ar, path) : NULL;
	return t;
}

static void free_try(struct try *e)
{
	for (int i = 0; i < e->nfiles; i++) {
		free(e->files[i]);
		free(e->given[i]);
	}
	free(e->files);
	free(e->given);
	free(e->name);
	free(e->norm);
	free(e->name_utf);
	free(e->shown);
	memset(e, 0, sizeof *e);
}

void rb3d_tries_free(struct rb3d_tries *t)
{
	if (!t)
		return;
	for (int i = 0; i < t->n; i++)
		free_try(&t->t[i]);
	free(t->t);
	rq_textures_free(t->list);
	free(t->path);
	free(t->shown);
	free(t);
}

bool rb3d_tries_for(const struct rb3d_tries *t, const char *path)
{
	if (!t || !path || strlen(path) >= PATH_MAX_)
		return false;
	char p[PATH_MAX_];
	snprintf(p, sizeof p, "%s", path);
	rq_textures_strip_mark(p);
	norm(p);
	return !strcmp(p, t->path);
}

/* The try on a name in the archive's terms, or -1. */
static int find_norm(const struct rb3d_tries *t, const char *nm)
{
	for (int i = 0; i < t->n; i++)
		if (!strcmp(t->t[i].norm, nm))
			return i;
	return -1;
}

int rb3d_tries_find(const struct rb3d_tries *t, const char *name)
{
	if (!t || !name)
		return -1;
	char *s = utf2sjis(name, 0);
	norm(s);
	int i = find_norm(t, s);
	free(s);
	return i;
}

/* The colour textures "*" reaches: each colour row the archive has, with no try of its own. */
static bool star_reaches(const struct rb3d_tries *t, const struct rq_texture *r)
{
	if (!(r->kinds & RQ_TEXTURE_COLOR) || r->missing)
		return false;
	char nm[256];
	snprintf(nm, sizeof nm, "%s", r->name);
	norm(nm);
	return find_norm(t, nm) < 0;
}

/* The pictures tried in all, as the engine counts them against its limit. */
static int cost(const struct rb3d_tries *t)
{
	int c = 0;
	bool star = false;
	for (int i = 0; i < t->n; i++) {
		if (!strcmp(t->t[i].norm, "*"))
			star = true;
		else
			c += t->t[i].colors ? 1 : t->t[i].nfiles;
	}
	for (int i = 0; star && t->list && i < t->list->n; i++)
		if (star_reaches(t, &t->list->rows[i]))
			c++;
	return c;
}

/* Puts e in the table, in the place of the try on its name; false (the table as it was, e freed)
 * past the engine's limit. */
static bool put_try(struct rb3d_tries *t, struct try *e, char *why, size_t wn)
{
	int k = find_norm(t, e->norm);
	struct try old;
	if (k >= 0) {
		old = t->t[k];
		t->t[k] = *e;
	} else {
		t->t = xrealloc(t->t, (t->n + 1) * sizeof *t->t);
		t->t[t->n++] = *e;
	}
	if (cost(t) > TRY_PICTURES_MAX) {
		if (k >= 0)
			t->t[k] = old;
		else
			t->n--;
		free_try(e);
		say(why, wn, "too many textures (max %d)", TRY_PICTURES_MAX);
		return false;
	}
	if (k >= 0)
		free_try(&old);
	return true;
}

/* The whole file, read as RQ MOD reads a mod's picture, with its limit: NULL with the engine's
 * reason ("it cannot be read", "the file is too large"); *found false when there is no such file. */
static uint8_t *read_file(const char *path, size_t *size, bool *found, char *why, size_t n)
{
	*size = 0;
	*found = file_exists(path) && !is_directory(path);
	if (!*found) {
		say(why, n, "it cannot be opened");
		return NULL;
	}
	off_t len = file_size(path);
	if (len < 0) {
		say(why, n, "it cannot be read");
		return NULL;
	}
	if ((uint64_t)len > TRY_FILE_BYTES) {
		say(why, n, "the file is too large");
		return NULL;
	}
	if (len == 0)
		return xmalloc(1);   /* libsys4's file_read takes no empty file: an empty PNG is "not a PNG" */
	size_t got = 0;
	uint8_t *data = file_read(path, &got);
	if (!data) {
		say(why, n, "it cannot be read");
		return NULL;
	}
	*size = got;
	return data;
}

/* One picture, checked as the engine's get_pictures checks a mod's: found, read, a PNG, its size from
 * its header, then decoded whole. */
static bool check_picture(const char *path, const char *given, char *why, size_t n)
{
	char r[300];
	size_t size;
	bool found;
	uint8_t *data = read_file(path, &size, &found, r, sizeof r);
	if (!data) {
		if (found)
			say(why, n, "texture picture '%s' cannot be read: %s", given, r);
		else
			say(why, n, "texture picture '%s' not found", given);
		return false;
	}
	int w, h;
	bool ok = false;
	if (!rq_pixels_is_png(data, size)) {
		say(why, n, "texture picture '%s' is not a PNG (textures take PNG only)", given);
	} else if (rq_pixels_png_size(data, size, &w, &h) && (w > RQ_PIXELS_TEXTURE_SIDE || h > RQ_PIXELS_TEXTURE_SIDE)) {
		say(why, n, "texture picture '%s' is %dx%d (max %d a side)", given, w, h, RQ_PIXELS_TEXTURE_SIDE);
	} else {
		struct cg *cg = rq_pixels_png(data, size, RQ_PIXELS_TEXTURE_SIDE, r, sizeof r);
		if (!cg)
			say(why, n, "texture picture '%s' cannot be read: %s", given, r);
		cg_free(cg);
		ok = cg != NULL;
	}
	free(data);
	return ok;
}

bool rb3d_tries_png(struct rb3d_tries *t, const char *name, const char *const *files, const char *const *shown, int n,
                    char *why, size_t wn)
{
	if (why && wn)
		why[0] = 0;
	if (n < 1 || !files) {
		say(why, wn, "texture '%s': give a PNG, a list of PNGs or colors", name);
		return false;
	}
	if (n > TRY_PICTURES_MAX) {
		say(why, wn, "too many textures (max %d)", TRY_PICTURES_MAX);
		return false;
	}
	for (int i = 0; i < n; i++)
		if (!check_picture(files[i], shown && shown[i] ? shown[i] : files[i], why, wn))
			return false;
	/* the texture named as a mod names it: its path, Data/PolyData/Chara/gokiburi/gokiburi.png */
	char *sjis = utf2sjis(name, 0);
	char *full = xmalloc(strlen(t->shown) + strlen(name) + 2);
	sprintf(full, "%s/%s", t->shown, name);
	bool ok = rq_textures_check(t->list, t->shown, sjis, full, n, why, wn);
	free(full);
	if (!ok) {
		free(sjis);
		return false;
	}
	const struct rq_texture *row = rq_textures_find(t->list, sjis);
	free(sjis);
	struct try e;
	memset(&e, 0, sizeof e);
	e.name = xstrdup(row->name);
	e.norm = xstrdup(row->name);
	norm(e.norm);
	e.name_utf = sjis2utf(row->name, 0);
	e.shown = xstrdup(name);
	e.nfiles = n;
	e.files = xcalloc(n, sizeof *e.files);
	e.given = xcalloc(n, sizeof *e.given);
	for (int i = 0; i < n; i++) {
		e.files[i] = xstrdup(files[i]);
		e.given[i] = xstrdup(shown && shown[i] ? shown[i] : files[i]);
	}
	return put_try(t, &e, why, wn);
}

bool rb3d_tries_colors(struct rb3d_tries *t, const char *name, double hue, double saturation, double lightness,
                       char *why, size_t wn)
{
	if (why && wn)
		why[0] = 0;
	if (!rq_pixels_colors_ok(hue, saturation, lightness, why, wn))
		return false;
	struct try e;
	memset(&e, 0, sizeof e);
	if (!strcmp(name, "*")) {
		/* every colour texture of the model, as rq.monster.model's "*" (a name tried beside it wins) */
		if (!t->list) {
			say(why, wn, "no model in '%s' (rq.image.textures lists a model's textures)", t->shown);
			return false;
		}
		e.name = xstrdup("*");
	} else {
		char *sjis = utf2sjis(name, 0);
		char *full = xmalloc(strlen(t->shown) + strlen(name) + 2);
		sprintf(full, "%s/%s", t->shown, name);
		bool ok = rq_textures_check(t->list, t->shown, sjis, full, 0, why, wn);
		free(full);
		if (!ok) {
			free(sjis);
			return false;
		}
		e.name = xstrdup(rq_textures_find(t->list, sjis)->name);
		free(sjis);
	}
	e.norm = xstrdup(e.name);
	norm(e.norm);
	e.name_utf = sjis2utf(e.name, 0);
	e.shown = xstrdup(name);
	e.colors = true;
	e.hue = hue;
	e.saturation = saturation;
	e.lightness = lightness;
	return put_try(t, &e, why, wn);
}

bool rb3d_tries_untry(struct rb3d_tries *t, const char *name)
{
	if (!t)
		return false;
	if (!name) {
		bool any = t->n > 0;
		for (int i = 0; i < t->n; i++)
			free_try(&t->t[i]);
		t->n = 0;
		return any;
	}
	int k = rb3d_tries_find(t, name);
	if (k < 0)
		return false;
	free_try(&t->t[k]);
	memmove(&t->t[k], &t->t[k + 1], (size_t)(t->n - k - 1) * sizeof *t->t);
	t->n--;
	return true;
}

int rb3d_tries_count(const struct rb3d_tries *t)
{
	return t ? t->n : 0;
}

bool rb3d_tries_get(const struct rb3d_tries *t, int i, struct rb3d_try *out)
{
	if (!t || i < 0 || i >= t->n)
		return false;
	const struct try *e = &t->t[i];
	out->name = e->name_utf;
	out->shown = e->shown;
	out->nfiles = e->nfiles;
	out->files = (const char *const *)e->files;
	out->given = (const char *const *)e->given;
	out->colors = e->colors;
	out->hue = e->hue;
	out->saturation = e->saturation;
	out->lightness = e->lightness;
	return true;
}

bool rb3d_tries_reaches(const struct rb3d_tries *t, const char *name)
{
	if (!t || !t->list || find_norm(t, "*") < 0)
		return false;
	char *s = utf2sjis(name, 0);
	const struct rq_texture *r = rq_textures_find(t->list, s);
	free(s);
	return r && star_reaches(t, r);
}

int rb3d_tries_warnings(const struct rb3d_tries *t)
{
	return t ? t->warnings : 0;
}

const char *rb3d_tries_warning(const struct rb3d_tries *t)
{
	return t ? t->warning : "";
}

/* "*"'s answer for a name: the try when the name, or a frame's base, is a colour texture it
 * reaches, as the engine's "*" puts one entry on each (with the frame's number), else -1. */
static int star_for(const struct rb3d_tries *t, const char *nm, int *frame)
{
	int s = find_norm(t, "*");
	if (s < 0 || !t->list)
		return -1;
	const struct rq_texture *r = rq_textures_find(t->list, nm);
	if (r && star_reaches(t, r)) {
		*frame = 0;
		return s;
	}
	char base[256];
	int f = rq_textures_frame(nm, base, sizeof base);
	if (f > 0 && (r = rq_textures_find(t->list, base)) && star_reaches(t, r)) {
		*frame = f;
		return s;
	}
	return -1;
}

/* RE_mod_texture's answer, by the engine's rule (its modloader_texture.c): the try on the name, or
 * on the base of a frame's name, else "*"; rq_textures_answer picks the picture, or the game's own
 * picture recoloured. */
struct cg *rb3d_tries_answer(struct rb3d_tries *t, struct archive_data *game, const char *path, const char *name, int kind)
{
	char nm[256], base[256], why[300];
	if (!t || !t->n || !path || !name || !name[0] || strlen(name) >= sizeof nm || !rb3d_tries_for(t, path))
		return NULL;
	snprintf(nm, sizeof nm, "%s", name);
	sjis_normalize_path(nm);
	int frame = 0;
	int k = find_norm(t, nm);
	if (k < 0 && (frame = rq_textures_frame(nm, base, sizeof base)) > 0)
		k = find_norm(t, base);
	if (k < 0)
		k = star_for(t, nm, &frame);
	if (k < 0)
		return NULL;
	struct try *e = &t->t[k];
	int pick = rq_textures_answer(frame, kind, e->colors, e->nfiles);
	if (pick == RQ_TEXTURES_GAME)
		return NULL;
	if (pick == RQ_TEXTURES_COLORS) {
		/* the game's own picture (a frame's own file for a frame), recoloured */
		if (!game)
			return NULL;
		struct cg *cg = cg_load_data(game);
		if (cg && (!cg->pixels || cg->metrics.w <= 0 || cg->metrics.h <= 0)) {
			cg_free(cg);
			cg = NULL;
		}
		if (!cg) {
			if (!e->warned) {
				e->warned = true;
				t->warnings++;
				char *u = sjis2utf(name, 0);
				snprintf(t->warning, sizeof t->warning, "texture %s/%s: the game's picture cannot be read; drawn as it is",
				         t->shown, u);
				free(u);
			}
			return NULL;
		}
		rq_pixels_recolor(cg, e->hue, e->saturation, e->lightness);
		return cg;
	}
	size_t size;
	bool found;
	uint8_t *data = read_file(e->files[pick], &size, &found, why, sizeof why);
	struct cg *cg = data ? rq_pixels_png(data, size, RQ_PIXELS_TEXTURE_SIDE, why, sizeof why) : NULL;
	free(data);
	if (!cg) {
		if (!e->warned) {
			e->warned = true;
			t->warnings++;
			snprintf(t->warning, sizeof t->warning, "texture %s/%s: %s cannot be read now (%s); the game's picture is drawn",
			         t->shown, e->name_utf, e->given[pick], why);
		}
		return NULL;
	}
	return cg;
}

/* ---- the UV maps ---- */

/* The POL material group j of the POL material pm draws with (model.c's model_load). */
static const struct pol_material *leaf_material(const struct pol *pol, uint32_t pm, int j)
{
	const struct pol_material_group *g = &pol->materials[pm];
	if (g->nr_children == 0)
		return &g->m;
	const struct pol_material_group *c = &g->children[j];
	return c->nr_children >= 2 ? &c->children[0].m : &c->m;
}

/* A part of a map: the triangles of one POL mesh's group drawn with the texture, by one set of UVs. */
struct uv_part {
	uint32_t mesh;
	int group;
	bool light;        /* by the light UVs */
	int tris;          /* the group's triangles, as the engine counts them */
	int drawn;         /* those drawn: a triangle with an index past its list is left out */
};

struct uv_map {
	uint8_t *px;
	int w, h;
	struct uv_part *parts;
	int nparts;
};

static void put(struct uv_map *m, int x, int y, uint8_t a)
{
	if (x < 0 || y < 0 || x >= m->w || y >= m->h)
		return;
	uint8_t *p = m->px + ((size_t)y * m->w + x) * 4;
	p[0] = p[1] = p[2] = 255;
	if (p[3] < a)
		p[3] = a;
}

/* A triangle's inside at the pixels' centres, faint. */
static void fill(struct uv_map *m, const double x[3], const double y[3])
{
	double area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
	if (area == 0)
		return;
	double s = area > 0 ? 1 : -1;
	double lo_x = fmin(x[0], fmin(x[1], x[2])), hi_x = fmax(x[0], fmax(x[1], x[2]));
	double lo_y = fmin(y[0], fmin(y[1], y[2])), hi_y = fmax(y[0], fmax(y[1], y[2]));
	int x0 = (int)floor(lo_x), x1 = (int)ceil(hi_x), y0 = (int)floor(lo_y), y1 = (int)ceil(hi_y);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > m->w - 1) x1 = m->w - 1;
	if (y1 > m->h - 1) y1 = m->h - 1;
	for (int py = y0; py <= y1; py++)
		for (int px = x0; px <= x1; px++) {
			double cx = px + 0.5, cy = py + 0.5;
			bool in = true;
			for (int e = 0; e < 3 && in; e++) {
				int a = e, b = (e + 1) % 3;
				double c = (x[b] - x[a]) * (cy - y[a]) - (y[b] - y[a]) * (cx - x[a]);
				in = c * s >= 0;
			}
			if (in)
				put(m, px, py, 48);
		}
}

/* A line a pixel wide (Bresenham), clipped to the picture. */
static void line(struct uv_map *m, double xa, double ya, double xb, double yb)
{
	long x0 = (long)floor(xa), y0 = (long)floor(ya), x1 = (long)floor(xb), y1 = (long)floor(yb);
	long dx = labs(x1 - x0), dy = -labs(y1 - y0);
	long sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	long err = dx + dy;
	for (;;) {
		put(m, (int)x0, (int)y0, 255);
		if (x0 == x1 && y0 == y1)
			break;
		long e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

#define MAX_SQUARES 16

/* A triangle by its UVs: moved by the floor of its smallest u and v, then drawn at each whole
 * square its extent covers (16 at most each way), as GL_REPEAT draws it. pass 0 fills, 1 draws its
 * edges. */
static void draw_triangle(struct uv_map *m, const float u[3], const float v[3], int pass)
{
	float fu = floorf(fminf(u[0], fminf(u[1], u[2]))), fv = floorf(fminf(v[0], fminf(v[1], v[2])));
	float uu[3], vv[3];
	for (int k = 0; k < 3; k++) {
		uu[k] = u[k] - fu;
		vv[k] = v[k] - fv;
	}
	double eu = ceil(fmax(uu[0], fmax(uu[1], uu[2]))), ev = ceil(fmax(vv[0], fmax(vv[1], vv[2])));
	int nx = eu < 1 ? 1 : eu > MAX_SQUARES ? MAX_SQUARES : (int)eu;
	int ny = ev < 1 ? 1 : ev > MAX_SQUARES ? MAX_SQUARES : (int)ev;
	for (int oy = 0; oy < ny; oy++)
		for (int ox = 0; ox < nx; ox++) {
			double x[3], y[3];
			for (int k = 0; k < 3; k++) {
				x[k] = ((double)uu[k] - ox) * m->w;
				y[k] = ((double)vv[k] - oy) * m->h;
			}
			if (pass == 0)
				fill(m, x, y);
			else
				for (int e = 0; e < 3; e++)
					line(m, x[e], y[e], x[(e + 1) % 3], y[(e + 1) % 3]);
		}
}

/* Draws one part (a mesh's group, by one set of UVs); pass as for draw_triangle. */
static void draw_part(struct uv_map *m, const struct pol_mesh *pm, const struct uv_part *p, const struct pol_material *mat,
                      int pass)
{
	float tu = p->light ? 1.f : mat->uv_tiling[0], tv = p->light ? 1.f : mat->uv_tiling[1];
	const vec2 *uvs = p->light ? pm->light_uvs : pm->uvs;
	uint32_t nuv = p->light ? pm->nr_light_uvs : pm->nr_uvs;
	for (uint32_t i = 0; i < pm->nr_triangles; i++) {
		const struct pol_triangle *t = &pm->triangles[i];
		if (t->material_group_index != (uint32_t)p->group)
			continue;
		const uint32_t *ix = p->light ? t->light_uv_index : t->uv_index;
		bool ok = true;
		for (int k = 0; k < 3; k++)
			if (t->vert_index[k] >= pm->nr_vertices || ix[k] >= nuv)
				ok = false;
		if (!ok)
			continue;
		float u[3], v[3];
		for (int k = 0; k < 3; k++) {
			u[k] = uvs[ix[k]][0] * tu;
			v[k] = uvs[ix[k]][1] * tv;
		}
		draw_triangle(m, u, v, pass);
	}
}

static int group_triangles(const struct pol_mesh *pm, int j, int *drawn, bool light)
{
	int n = 0;
	*drawn = 0;
	uint32_t nuv = light ? pm->nr_light_uvs : pm->nr_uvs;
	for (uint32_t i = 0; i < pm->nr_triangles; i++) {
		const struct pol_triangle *t = &pm->triangles[i];
		if (t->material_group_index != (uint32_t)j)
			continue;
		n++;
		const uint32_t *ix = light ? t->light_uv_index : t->uv_index;
		bool ok = true;
		for (int k = 0; k < 3; k++)
			if (t->vert_index[k] >= pm->nr_vertices || ix[k] >= nuv)
				ok = false;
		if (ok)
			(*drawn)++;
	}
	return n;
}

static bool names(const char *a, const char *norm_name)
{
	if (!a || !a[0])
		return false;
	char s[256];
	snprintf(s, sizeof s, "%s", a);
	sjis_normalize_path(s);
	return !strcmp(s, norm_name);
}

/* The map of the texture norm_name (the archive's terms) at w by h over the POL: the parts first, then
 * the fills, then the lines. */
static void make_map(const struct pol *pol, const char *norm_name, int w, int h, struct uv_map *m)
{
	memset(m, 0, sizeof *m);
	m->w = w;
	m->h = h;
	m->px = xcalloc((size_t)w * h, 4);
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		const struct pol_mesh *pm = pol->meshes[i];
		if (!pm || !strcmp(pm->name, "collision") || pm->material >= pol->nr_materials)
			continue;
		const struct pol_material_group *g = &pol->materials[pm->material];
		int ngroups = g->nr_children ? (int)g->nr_children : 1;
		for (int j = 0; j < ngroups; j++) {
			const struct pol_material *mat = leaf_material(pol, pm->material, j);
			/* the mesh's UVs for the colour, alpha, normal and specular maps; the light UVs for the
			 * light map, on a mesh that has them (model.c's add_mesh) */
			bool mesh_uvs = names(mat->textures[COLOR_MAP], norm_name) || names(mat->textures[ALPHA_MAP], norm_name) ||
			                names(mat->textures[NORMAL_MAP], norm_name) || names(mat->textures[SPECULAR_MAP], norm_name);
			bool light = names(mat->textures[LIGHT_MAP], norm_name);
			for (int s = 0; s < 2; s++) {
				bool by_light = s == 1;
				if (by_light ? !light : !mesh_uvs)
					continue;
				if (by_light && !pm->light_uvs)
					continue;   /* the engine draws no light map there */
				struct uv_part p = { i, j, by_light, 0, 0 };
				p.tris = group_triangles(pm, j, &p.drawn, by_light);
				if (!p.tris)
					continue;
				m->parts = xrealloc(m->parts, (m->nparts + 1) * sizeof *m->parts);
				m->parts[m->nparts++] = p;
			}
		}
	}
	for (int pass = 0; pass < 2; pass++)
		for (int k = 0; k < m->nparts; k++) {
			const struct uv_part *p = &m->parts[k];
			draw_part(m, pol->meshes[p->mesh], p, leaf_material(pol, pol->meshes[p->mesh]->material, p->group), pass);
		}
}

/* The POL of the model at path, as model_load reads it (with its .opr), or NULL. */
static struct pol *read_pol(struct archive *ar, const char *path)
{
	const char *base = strrchr(path, '\\');
	base = base ? base + 1 : path;
	struct archive_data *d = RE_get_aar_entry(ar, path, base, ".POL");
	if (!d)
		return NULL;
	struct pol *pol = pol_parse(d->data, d->size);
	archive_free_data(d);
	return pol;
}

/* The row of name (UTF-8) in the model at path, checked as RQ MOD checks a texture it is asked
 * about (rq.image.texture with nil), its list kept in *l. */
static const struct rq_texture *row_named(struct archive *ar, const char *path, const char *name,
                                          struct rq_texture_list **l, char *why, size_t n)
{
	*l = rq_textures_list(ar, path);
	char *shown = folder_shown(path);
	char *sjis = utf2sjis(name, 0);
	const struct rq_texture *row = NULL;
	if (rq_textures_check(*l, shown, sjis, name, -1, why, n))
		row = rq_textures_find(*l, sjis);
	free(sjis);
	free(shown);
	return row;
}

uint8_t *rb3d_uv_map(struct aar_archive *red, const char *path, const char *name, int *w, int *h, char *why, size_t n)
{
	struct rq_texture_list *l = NULL;
	const struct rq_texture *row = red ? row_named(&red->ar, path, name, &l, why, n) : NULL;
	if (!row || row->w <= 0 || row->h <= 0) {
		if (row)
			say(why, n, "the game's picture '%s' does not read", name);
		rq_textures_free(l);
		return NULL;
	}
	struct pol *pol = read_pol(&red->ar, path);
	if (!pol) {
		say(why, n, "the model's .POL does not read");
		rq_textures_free(l);
		return NULL;
	}
	char nm[256];
	snprintf(nm, sizeof nm, "%s", row->name);
	sjis_normalize_path(nm);
	struct uv_map m;
	make_map(pol, nm, row->w, row->h, &m);
	*w = m.w;
	*h = m.h;
	free(m.parts);
	pol_free(pol);
	rq_textures_free(l);
	return m.px;
}

uint8_t *rb3d_texture_rgba(struct aar_archive *red, const char *path, const char *name, int *w, int *h, char *why,
                           size_t n)
{
	struct rq_texture_list *l = NULL;
	const struct rq_texture *row = red ? row_named(&red->ar, path, name, &l, why, n) : NULL;
	struct archive_data *d = row ? RE_get_aar_entry(&red->ar, path, row->name, "") : NULL;
	struct cg *cg = d ? cg_load_data(d) : NULL;
	if (d)
		archive_free_data(d);
	uint8_t *px = NULL;
	if (cg && cg->pixels && cg->metrics.w > 0 && cg->metrics.h > 0) {
		*w = cg->metrics.w;
		*h = cg->metrics.h;
		px = xmalloc((size_t)*w * *h * 4);
		memcpy(px, cg->pixels, (size_t)*w * *h * 4);
	} else if (row) {
		say(why, n, "the game's picture '%s' does not read", name);
	}
	cg_free(cg);
	rq_textures_free(l);
	return px;
}

/* ---- --check: every UV map of the model the view shows, against the engine's meshes ---- */

bool rb3d_view_uv_check(struct rb3d_view *v, struct rb3d_uv_counts *c, char *why, size_t n)
{
	struct RE_instance *inst = rb3d_view_instance(v);
	struct RE_plugin *p = rb3d_view_plugin(v);
	if (!inst || !inst->model) {
		say(why, n, "no model loaded");
		return false;
	}
	const struct model *mdl = inst->model;
	struct pol *pol = read_pol(p->aar, mdl->path);
	struct rq_texture_list *l = rq_textures_list(p->aar, mdl->path);
	if (!pol || !l) {
		say(why, n, "the model's .POL does not read again");
		if (pol)
			pol_free(pol);
		rq_textures_free(l);
		return false;
	}
	/* the engine's material and mesh for each POL mesh's group, as model_load makes them */
	int *mat_off = xmalloc((pol->nr_materials ? pol->nr_materials : 1) * sizeof *mat_off);
	int nmat = 0;
	for (uint32_t i = 0; i < pol->nr_materials; i++) {
		mat_off[i] = nmat;
		nmat += pol->materials[i].nr_children ? (int)pol->materials[i].nr_children : 1;
	}
	bool ok = true;
	for (int r = 0; r < l->n && ok; r++) {
		const struct rq_texture *row = &l->rows[r];
		if (row->missing || row->w <= 0 || row->h <= 0)
			continue;
		char nm[256];
		snprintf(nm, sizeof nm, "%s", row->name);
		sjis_normalize_path(nm);
		struct uv_map m;
		make_map(pol, nm, row->w, row->h, &m);
		c->maps++;
		for (int k = 0; k < m.nparts && ok; k++) {
			const struct uv_part *pt = &m.parts[k];
			const struct pol_mesh *pm = pol->meshes[pt->mesh];
			/* the engine's mesh for this group: the meshes come in the POL's order, a group with
			 * no triangle making none */
			int em = 0;
			for (uint32_t i = 0; i < pt->mesh; i++) {
				const struct pol_mesh *q = pol->meshes[i];
				if (!q || !strcmp(q->name, "collision") || q->material >= pol->nr_materials)
					continue;
				int ng = pol->materials[q->material].nr_children ? (int)pol->materials[q->material].nr_children : 1;
				for (int j = 0; j < ng; j++) {
					int d;
					if (group_triangles(q, j, &d, false))
						em++;
				}
			}
			for (int j = 0; j < pt->group; j++) {
				int d;
				if (group_triangles(pm, j, &d, false))
					em++;
			}
			int mi = mat_off[pm->material] + pt->group;
			if (em >= mdl->nr_meshes || strcmp(mdl->meshes[em].name, pm->name) || mdl->meshes[em].material != mi) {
				char *u = sjis2utf(row->name, 0);
				say(why, n, "the UV map of %s: mesh %s, group %d, is not the engine's mesh %d", u, pm->name, pt->group, em);
				free(u);
				ok = false;
			} else if (mdl->meshes[em].nr_vertices != pt->tris * 3) {
				char *u = sjis2utf(row->name, 0);
				say(why, n, "the UV map of %s: mesh %s, group %d, has %d triangles, the engine's %d", u, pm->name, pt->group,
				    pt->tris, mdl->meshes[em].nr_vertices / 3);
				free(u);
				ok = false;
			}
			c->parts++;
			c->triangles += pt->drawn;
			c->skipped += pt->tris - pt->drawn;
		}
		free(m.parts);
		free(m.px);
	}
	/* the meshes whose material has a light map the game has, with no light UVs: the engine draws
	 * them without it, and their light map's UV map leaves them out */
	int lw = 0;
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		const struct pol_mesh *pm = pol->meshes[i];
		if (!pm || !strcmp(pm->name, "collision") || pm->material >= pol->nr_materials || pm->light_uvs)
			continue;
		int ng = pol->materials[pm->material].nr_children ? (int)pol->materials[pm->material].nr_children : 1;
		for (int j = 0; j < ng; j++) {
			const char *lm = leaf_material(pol, pm->material, j)->textures[LIGHT_MAP];
			const struct rq_texture *row = lm && lm[0] ? rq_textures_find(l, lm) : NULL;
			int d;
			if (row && !row->missing && group_triangles(pm, j, &d, false)) {
				lw++;
				break;
			}
		}
	}
	c->light_meshes_without_uvs += lw;
	if (lw)
		c->light_models_without_uvs++;
	free(mat_off);
	pol_free(pol);
	rq_textures_free(l);
	return ok;
}
