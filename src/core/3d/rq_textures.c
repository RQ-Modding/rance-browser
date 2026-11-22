/* Copyright (C) 2026 Rance Quest modding fork
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

/*
 * A model's textures as model.c loads them (see rq_textures.h): a material with no colour map, or
 * whose colour map the archive lacks, loads nothing; a colour map loads its frames name[1],
 * name[2]... until one is missing; the colour file named again as the material's alpha map is
 * not a texture of its own; a blend group's second texture is one more colour picture.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/utfsjis.h"

#include "3d_internal.h"
#include "rq_textures.h"

static void say(char *why, size_t len, const char *fmt, ...)
{
	va_list ap;

	if (!why || !len)
		return;
	va_start(ap, fmt);
	vsnprintf(why, len, fmt, ap);
	va_end(ap);
}

static bool named(const char *s)
{
	return s && s[0];
}

bool rq_textures_same_name(const char *a, const char *b)
{
	char *x = strdup(a), *y = strdup(b);
	bool same;

	if (!x || !y) {
		free(x);
		free(y);
		return false;
	}
	sjis_normalize_path(x);
	sjis_normalize_path(y);
	same = !strcmp(x, y);
	free(x);
	free(y);
	return same;
}

struct builder {
	struct archive *aar;
	const char *folder; /* with \ */
	struct rq_texture_list *l;
};

/* The row for `name`, made (with the game's size and alpha) the first time. An index, as the
 * rows move when they grow. */
static int row_of(struct builder *b, const char *name)
{
	struct rq_texture_list *l = b->l;
	struct rq_texture *t;
	struct archive_data *d;

	for (int i = 0; i < l->n; i++) {
		if (rq_textures_same_name(l->rows[i].name, name))
			return i;
	}
	l->rows = xrealloc(l->rows, (l->n + 1) * sizeof *l->rows);
	t = &l->rows[l->n];
	memset(t, 0, sizeof *t);
	t->name = xstrdup(name);
	t->frames = 1;
	d = RE_get_aar_entry(b->aar, b->folder, name, "");
	if (!d) {
		t->missing = true;
	} else {
		struct cg_metrics m;
		if (cg_get_metrics_data(d, &m)) {
			t->w = m.w;
			t->h = m.h;
			t->alpha = m.has_alpha;
		}
		archive_free_data(d);
	}
	return l->n++;
}

static void add_kind(struct builder *b, const char *name, unsigned kind)
{
	if (named(name))
		b->l->rows[row_of(b, name)].kinds |= kind;
}

/* One material, as model.c's init_material loads it. */
static void add_material(struct builder *b, const struct pol_material *m)
{
	const char *color = m->textures[COLOR_MAP];
	int c;

	if (!named(color))
		return;
	c = row_of(b, color);
	b->l->rows[c].kinds |= RQ_TEXTURE_COLOR;
	if (b->l->rows[c].missing)
		return;
	add_kind(b, m->textures[SPECULAR_MAP], RQ_TEXTURE_SPECULAR);
	if (named(m->textures[ALPHA_MAP])) {
		if (rq_textures_same_name(color, m->textures[ALPHA_MAP]))
			b->l->rows[c].alpha_map = true;
		else
			add_kind(b, m->textures[ALPHA_MAP], RQ_TEXTURE_ALPHA);
	}
	add_kind(b, m->textures[LIGHT_MAP], RQ_TEXTURE_LIGHT);
	add_kind(b, m->textures[NORMAL_MAP], RQ_TEXTURE_NORMAL);
}

/* A colour map's pictures, as model.c's load_texture_list names and counts them. */
static int count_frames(struct builder *b, const char *name)
{
	const char *ext = strrchr(name, '.');
	int n = 1;

	if (!ext)
		return 1;
	for (;;) {
		char next[100];
		snprintf(next, sizeof next, "%.*s[%d]%s", (int)(ext - name), name, n, ext);
		char *path = xmalloc(strlen(b->folder) + strlen(next) + 2);
		sprintf(path, "%s\\%s", b->folder, next);
		bool there = archive_exists_by_name(b->aar, path, NULL);
		free(path);
		if (!there)
			return n;
		n++;
	}
}

struct rq_texture_list *rq_textures_list(struct archive *aar, const char *folder)
{
	char *dir = xstrdup(folder);
	size_t len;
	const char *base;
	struct archive_data *f;
	struct pol *pol;
	struct builder b;

	for (char *p = dir; *p; p++) {
		if (*p == '/')
			*p = '\\';
	}
	len = strlen(dir);
	while (len > 0 && dir[len - 1] == '\\')
		dir[--len] = '\0';
	base = strrchr(dir, '\\');
	base = base ? base + 1 : dir;
	f = base[0] ? RE_get_aar_entry(aar, dir, base, ".POL") : NULL;
	if (!f) {
		free(dir);
		return NULL;
	}
	pol = pol_parse(f->data, f->size);
	archive_free_data(f);
	if (!pol) {
		free(dir);
		return NULL;
	}

	b.aar = aar;
	b.folder = dir;
	b.l = xcalloc(1, sizeof *b.l);
	for (uint32_t i = 0; i < pol->nr_materials; i++) {
		struct pol_material_group *mg = &pol->materials[i];
		if (mg->nr_children == 0) {
			add_material(&b, &mg->m);
			continue;
		}
		for (uint32_t j = 0; j < mg->nr_children; j++) {
			struct pol_material_group *child = &mg->children[j];
			if (child->nr_children >= 2) {
				add_material(&b, &child->children[0].m);
				add_kind(&b, child->children[1].m.textures[COLOR_MAP], RQ_TEXTURE_COLOR);
			} else {
				add_material(&b, &child->m);
			}
		}
	}
	for (int i = 0; i < b.l->n; i++) {
		struct rq_texture *t = &b.l->rows[i];
		if ((t->kinds & RQ_TEXTURE_COLOR) && !t->missing)
			t->frames = count_frames(&b, t->name);
	}
	pol_free(pol);
	free(dir);
	return b.l;
}

void rq_textures_free(struct rq_texture_list *l)
{
	if (!l)
		return;
	for (int i = 0; i < l->n; i++)
		free(l->rows[i].name);
	free(l->rows);
	free(l);
}

const struct rq_texture *rq_textures_find(const struct rq_texture_list *l, const char *name)
{
	if (!l || !name)
		return NULL;
	for (int i = 0; i < l->n; i++) {
		if (rq_textures_same_name(l->rows[i].name, name))
			return &l->rows[i];
	}
	return NULL;
}

const char *rq_textures_kind(unsigned kinds)
{
	if (kinds & RQ_TEXTURE_COLOR)
		return "color";
	if (kinds & RQ_TEXTURE_ALPHA)
		return "alpha";
	if (kinds & RQ_TEXTURE_LIGHT)
		return "light";
	if (kinds & RQ_TEXTURE_NORMAL)
		return "normal";
	return "specular";
}

int rq_textures_frame(const char *name, char *base, size_t base_len)
{
	const char *ext = strrchr(name, '.');
	const char *close, *open;
	int n = 0;

	if (!ext || ext == name || ext[-1] != ']')
		return 0;
	close = ext - 1;
	open = close;
	while (open > name && open[-1] >= '0' && open[-1] <= '9')
		open--;
	if (open == close || open == name || open[-1] != '[')
		return 0;
	for (const char *p = open; p < close; p++) {
		n = n * 10 + (*p - '0');
		if (n > 100000)
			return 0;
	}
	if (n < 1)
		return 0;
	/* The base, and the frame's name made from it as model.c makes it (a byte of a two-byte
	 * Shift-JIS letter can look like a bracket). */
	snprintf(base, base_len, "%.*s%s", (int)(open - 1 - name), name, ext);
	char again[100];
	const char *bext = strrchr(base, '.');
	if (!bext)
		return 0;
	snprintf(again, sizeof again, "%.*s[%d]%s", (int)(bext - base), base, n, bext);
	return strcmp(again, name) ? 0 : n;
}

/* The last part of a path. */
static const char *leaf(const char *path)
{
	const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
	const char *p = a > b ? a : b;
	return p ? p + 1 : path;
}

static const char *kind_with_article(unsigned kinds)
{
	if (kinds & RQ_TEXTURE_ALPHA)
		return "an alpha map";
	if (kinds & RQ_TEXTURE_LIGHT)
		return "a light map";
	if (kinds & RQ_TEXTURE_NORMAL)
		return "a normal map";
	return "a specular map";
}

int rq_textures_answer(int frame, int kind, bool colors, int pictures)
{
	if (frame < 0)
		return RQ_TEXTURES_GAME;
	if (colors)
		return kind == COLOR_MAP ? RQ_TEXTURES_COLORS : RQ_TEXTURES_GAME;
	return frame < pictures ? frame : RQ_TEXTURES_GAME;
}

bool rq_textures_check(const struct rq_texture_list *l, const char *folder, const char *name,
		       const char *shown, int pictures, char *why, size_t why_len)
{
	const struct rq_texture *t;

	if (why && why_len)
		why[0] = '\0';
	if (!l) {
		say(why, why_len, "no model in '%s' (rq.image.textures lists a model's textures)", folder);
		return false;
	}
	t = rq_textures_find(l, name);
	if (!t) {
		char base[256];
		int n = rq_textures_frame(name, base, sizeof base);
		const struct rq_texture *b = n > 0 ? rq_textures_find(l, base) : NULL;
		if (b && (b->kinds & RQ_TEXTURE_COLOR) && n < b->frames) {
			/* The base as the mod wrote it: the frame's "[n]" taken out (brackets and digits
			 * are never part of a longer letter in UTF-8). */
			char shown_base[512];
			const char *ext = strrchr(shown, '.');
			const char *open = ext ? ext - 1 : NULL;
			while (open && open > shown && *open != '[')
				open--;
			if (open && *open == '[')
				snprintf(shown_base, sizeof shown_base, "%.*s%s", (int)(open - shown), shown, ext);
			else
				snprintf(shown_base, sizeof shown_base, "%s", shown);
			say(why, why_len, "texture '%s' is a frame: give the frames as a list on '%s'", shown, shown_base);
		} else {
			say(why, why_len, "texture '%s' is not a texture of the model in '%s' (rq.image.textures lists them)",
			    shown, folder);
		}
		return false;
	}
	if (t->missing) {
		say(why, why_len, "the game has no file '%s' in '%s'", leaf(shown), folder);
		return false;
	}
	if (pictures < 0)
		return true;
	if (pictures == 0) {
		if (!(t->kinds & RQ_TEXTURE_COLOR)) {
			say(why, why_len, "texture '%s' is %s: colors change color textures only", shown,
			    kind_with_article(t->kinds));
			return false;
		}
		return true;
	}
	if (pictures != t->frames) {
		if (t->frames == 1)
			say(why, why_len, "texture '%s' has no frames in the game: give one picture", shown);
		else
			say(why, why_len, "texture '%s' has %d frames in the game: give %d pictures", shown,
			    t->frames, t->frames);
		return false;
	}
	return true;
}

/* The mark "\*<digits>\" in a path: where it starts, and its length up to the second \. */
static const char *find_mark(const char *path, size_t *len)
{
	for (const char *p = path; (p = strstr(p, "\\*")); p++) {
		const char *q = p + 2;
		while (*q >= '0' && *q <= '9')
			q++;
		if (q > p + 2 && *q == '\\') {
			*len = (size_t)(q - p);
			return p;
		}
	}
	return NULL;
}

int rq_textures_mark(const char *path)
{
	size_t len;
	const char *p = path ? find_mark(path, &len) : NULL;
	long n = 0;

	if (!p)
		return 0;
	for (const char *q = p + 2; q < p + len; q++) {
		n = n * 10 + (*q - '0');
		if (n > 1000000000L)
			return 0;
	}
	return (int)n;
}

void rq_textures_strip_mark(char *path)
{
	size_t len;
	char *p;

	while (path && (p = (char *)find_mark(path, &len)))
		memmove(p, p + len, strlen(p + len) + 1);
}
