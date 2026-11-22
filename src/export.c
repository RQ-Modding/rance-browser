/* --export: the game's 3D models as glTF binary files (.glb), drawn by nothing: a hidden window as
 * --check makes one, for the 3D code's OpenGL. --uv-map and --save-texture: a texture's UV map or
 * the game's picture, with no window. And the tries of the command line (--texture), for --export and
 * --shot 3d.
 *
 *   rance-browser --export OUT.glb --select NAME [--texture NAME=FILE.png[,...]]...
 *       one model (NAME: its folder, or Kind/folder when two kinds have one of that name), with PNGs
 *       tried on its textures as the engine draws them
 *   rance-browser --export DIR (--select NAME | --kind chara|map|bg|etc[,...] | --all)
 *       each to DIR\<Kind>\<folder>.glb, with DIR\export.json: what the browser wrote in each file
 *       and numbers from the engine's own code (rb3d_export_glb's report), for a checker that opens
 *       the files in Blender
 *   rance-browser --uv-map NAME OUT.png --select MODEL
 *   rance-browser --save-texture NAME OUT.png --select MODEL
 *
 * Exit 0 when every model asked for was written, 1 otherwise, 2 for a command line that does not
 * make sense. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/file.h"
#include "cJSON.h"

#include "browser.h"

static char *dup_n(const char *s, size_t n)
{
	char *d = xmalloc(n + 1);
	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

bool try_arg_parse(const char *arg, struct try_arg *out)
{
	memset(out, 0, sizeof *out);
	out->arg = arg;
	const char *eq = strchr(arg, '=');
	if (!eq || eq == arg || !eq[1])
		return false;
	out->name = dup_n(arg, eq - arg);
	for (const char *s = eq + 1; ; ) {
		const char *end = strchr(s, ',');
		size_t n = end ? (size_t)(end - s) : strlen(s);
		if (!n)
			return false;
		out->files = xrealloc(out->files, (out->nfiles + 1) * sizeof *out->files);
		out->files[out->nfiles++] = dup_n(s, n);
		if (!end)
			break;
		s = end + 1;
	}
	return true;
}

bool colors_parse(const char *s, double *hue, double *saturation, double *lightness)
{
	double v[3] = { 0, 1, 1 };
	int n = 0;
	for (;;) {
		char *end;
		if (n == 3 || !*s || *s == ',' || isspace((unsigned char)*s))
			return false;
		v[n++] = strtod(s, &end);
		if (end == s)
			return false;
		s = end;
		if (!*s)
			break;
		if (*s++ != ',')
			return false;
	}
	*hue = v[0];
	*saturation = v[1];
	*lightness = v[2];
	return true;
}

bool colors_arg_parse(const char *arg, struct try_arg *out)
{
	memset(out, 0, sizeof *out);
	out->arg = arg;
	const char *eq = strchr(arg, '=');
	if (!eq || eq == arg || !colors_parse(eq + 1, &out->hue, &out->saturation, &out->lightness))
		return false;
	out->name = dup_n(arg, eq - arg);
	out->colors = true;
	return true;
}

#ifndef RB_3D

int run_export(const char *dir, const struct export_args *a)
{
	(void)dir;
	(void)a;
	fprintf(stderr, APP_EXE ": --export: this build has no 3D view\n");
	return 1;
}

struct rb3d_tries *tries_from_args(const char *path, const struct try_arg *a, int n, char *why, size_t wn)
{
	(void)path;
	(void)a;
	(void)n;
	snprintf(why, wn, "this build has no 3D view");
	return NULL;
}

int run_texture_file(const char *dir, const char *select, const char *name, const char *out, bool uv)
{
	(void)dir;
	(void)select;
	(void)name;
	(void)out;
	fprintf(stderr, APP_EXE ": --%s: this build has no 3D view\n", uv ? "uv-map" : "save-texture");
	return 1;
}

#else

#include <SDL.h>
#include "rb3d.h"

#define GENERATOR APP_NAME " " BROWSER_VERSION

struct rb3d_tries *tries_from_args(const char *path, const struct try_arg *a, int n, char *why, size_t wn)
{
	struct rb3d_tries *t = rb3d_tries_new(G.h.red, path);
	for (int i = 0; i < n; i++) {
		if (a[i].colors ? !rb3d_tries_colors(t, a[i].name, a[i].hue, a[i].saturation, a[i].lightness, why, wn)
		                : !rb3d_tries_png(t, a[i].name, (const char *const *)a[i].files, NULL, a[i].nfiles, why, wn)) {
			rb3d_tries_free(t);
			return NULL;
		}
	}
	return t;
}

/* A warning the load gave (a tried file gone or broken since it was checked), on stderr once. */
static void tell_warnings(struct rb3d_view *v, int *seen)
{
	struct rb3d_tries *t = rb3d_view_tries(v);
	if (rb3d_tries_warnings(t) > *seen) {
		fprintf(stderr, APP_EXE ": warning: %s\n", rb3d_tries_warning(t));
		*seen = rb3d_tries_warnings(t);
	}
}

static bool ends_with(const char *s, const char *end)
{
	size_t n = strlen(s), k = strlen(end);
	return n >= k && !strcasecmp(s + n - k, end);
}

/* A model by its folder, or Kind/folder; -1 when there is none (or the kind is no kind). */
static int find_model(const struct rb3d_list *l, const char *name)
{
	int kind = -1;
	const char *folder = name, *sep = strpbrk(name, "/\\");
	if (sep) {
		for (int k = 0; k < RB3D_NKINDS; k++)
			if (strlen(rb3d_kind_folders[k]) == (size_t)(sep - name) && !strncasecmp(name, rb3d_kind_folders[k], sep - name))
				kind = k;
		if (kind < 0)
			return -1;
		folder = sep + 1;
	}
	return rb3d_list_find(l, kind, folder);
}

/* A folder and the ones above it (libsys4's mkdir_p splits the path at '/' only). */
static bool make_dirs(const char *path)
{
	char buf[1600];
	snprintf(buf, sizeof buf, "%s", path);
	for (char *p = buf; *p; p++)
		if (*p == '\\')
			*p = '/';
	size_t n = strlen(buf);
	while (n > 3 && buf[n - 1] == '/')
		buf[--n] = 0;
	mkdir_p(buf);
	return is_directory(buf);
}

/* The kinds of a list ("chara,map"), one bit each; -1 when a name is no kind. */
static int kinds_named(const char *s)
{
	int mask = 0;
	while (*s) {
		const char *end = strchr(s, ',');
		size_t n = end ? (size_t)(end - s) : strlen(s);
		int k;
		for (k = 0; k < RB3D_NKINDS; k++)
			if (strlen(rb3d_kind_folders[k]) == n && !strncasecmp(s, rb3d_kind_folders[k], n))
				break;
		if (k == RB3D_NKINDS)
			return -1;
		mask |= 1 << k;
		s += n + (end ? 1 : 0);
	}
	return mask ? mask : -1;
}

static void skip(cJSON *skipped, const struct rb3d_model *m, const char *why)
{
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, "folder", m->folder);
	cJSON_AddStringToObject(o, "kind", rb3d_kind_folders[m->kind]);
	cJSON_AddStringToObject(o, "why", why);
	cJSON_AddItemToArray(skipped, o);
	fprintf(stderr, APP_EXE ": %s\\%s: %s\n", rb3d_kind_folders[m->kind], m->folder, why);
}

/* The hidden window and its GL context, for the 3D code. */
static bool open_gl(SDL_Window **win, SDL_GLContext *gl, char *why, size_t n)
{
	*win = NULL;
	*gl = NULL;
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		snprintf(why, n, "%s", SDL_GetError());
		return false;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	*win = SDL_CreateWindow(APP_NAME " --export", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 64, 64,
	                        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	*gl = *win ? SDL_GL_CreateContext(*win) : NULL;
	if (!*gl) {
		snprintf(why, n, "%s", SDL_GetError());
		return false;
	}
	SDL_GL_MakeCurrent(*win, *gl);
	return true;
}

int run_export(const char *dir, const struct export_args *a)
{
	if (!game_open(dir)) {
		fprintf(stderr, APP_EXE ": could not open the game in %s: %s\n", dir, G.errors);
		return 1;
	}
	if (!G.h.red) {
		fprintf(stderr, APP_EXE ": %s has no 3D models (no Data\\ReignData.red)\n", G.title);
		return 1;
	}
	bool one = ends_with(a->out, ".glb");
	int kinds = 0;
	if (a->kind && (kinds = kinds_named(a->kind)) < 0) {
		fprintf(stderr, APP_EXE ": --kind takes chara, map, bg or etc (or a list: chara,map), not %s\n", a->kind);
		return 2;
	}
	if (one && !a->select) {
		fprintf(stderr, APP_EXE ": --export FILE.glb takes --select NAME (a folder: --kind or --all)\n");
		return 2;
	}
	if (!one && !a->select && !kinds && !a->all) {
		fprintf(stderr, APP_EXE ": --export DIR takes --select NAME, --kind KIND or --all\n");
		return 2;
	}
	if (a->ntries && (!a->select || kinds || a->all)) {
		fprintf(stderr, APP_EXE ": --texture and --colors take one model (--select NAME), not --kind or --all\n");
		return 2;
	}

	struct rb3d_prop_look looks[32];
	int nl = 0;
	if (G.rq && G.prof)
		for (int i = 0; i < G.prof->nlooks && nl < 32; i++, nl++) {
			looks[nl].object = G.prof->looks[i].object;
			looks[nl].name = G.prof->looks[i].name;
		}
	static struct rb3d_list l;
	if (!rb3d_list_read(G.h.red, G.dir, looks, nl, &l)) {
		fprintf(stderr, APP_EXE ": %s has no 3D models\n", G.title);
		return 1;
	}
	int sel = -1;
	if (a->select && (sel = find_model(&l, a->select)) < 0) {
		fprintf(stderr, APP_EXE ": %s has no 3D model named %s\n", G.title, a->select);
		return 1;
	}
	/* PNGs tried on the model's textures, checked before any window opens */
	struct rb3d_tries *tries = NULL;
	if (a->ntries) {
		char twhy[1200];
		tries = tries_from_args(l.m[sel].path, a->tries, a->ntries, twhy, sizeof twhy);
		if (!tries) {
			fprintf(stderr, APP_EXE ": %s\n", twhy);
			return 1;
		}
	}

	char why[512] = "";
	SDL_Window *win;
	SDL_GLContext gl;
	if (!open_gl(&win, &gl, why, sizeof why)) {
		fprintf(stderr, APP_EXE ": no OpenGL window for the 3D code: %s\n", why);
		return 1;
	}
	struct rb3d_view *v = rb3d_view_new(G.f.red, 64, 64, why, sizeof why);
	if (!v) {
		fprintf(stderr, APP_EXE ": %s\n", why);
		return 1;
	}
	rb3d_view_set_tries(v, tries);
	int seen = 0;

	int asked = 0, written = 0;
	char err[1024];
	if (one) {
		const struct rb3d_model *m = &l.m[sel];
		asked = 1;
		bool loaded = rb3d_view_load(v, m->path, err, sizeof err);
		tell_warnings(v, &seen);
		if (!loaded)
			fprintf(stderr, APP_EXE ": %s: %s\n", m->folder, err);
		else if (!rb3d_export_glb(v, &l, m, a->out, GENERATOR, NULL, err, sizeof err))
			fprintf(stderr, APP_EXE ": %s: %s\n", m->folder, err);
		else
			written = 1;
		tell_warnings(v, &seen);
	} else if (!make_dirs(a->out)) {
		fprintf(stderr, APP_EXE ": could not make the folder %s\n", a->out);
		written = -1;
	} else {
		uint64_t t0 = SDL_GetTicks64();
		cJSON *root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "format", 1);
		cJSON_AddStringToObject(root, "browser", BROWSER_VERSION);
		cJSON *game = cJSON_AddObjectToObject(root, "game");
		cJSON_AddStringToObject(game, "code", G.code);
		cJSON_AddStringToObject(game, "title", G.title);
		cJSON *models = cJSON_AddArrayToObject(root, "models");
		cJSON *skipped = cJSON_AddArrayToObject(root, "skipped");
		for (int i = 0; i < l.n; i++) {
			const struct rb3d_model *m = &l.m[i];
			if (sel >= 0 ? i != sel : kinds && !(kinds & 1 << m->kind))
				continue;
			asked++;
			char sub[1200], rel[400], path[1600];
			snprintf(sub, sizeof sub, "%s\\%s", a->out, rb3d_kind_folders[m->kind]);
			snprintf(rel, sizeof rel, "%s/%s.glb", rb3d_kind_folders[m->kind], m->folder);
			snprintf(path, sizeof path, "%s\\%s.glb", sub, m->folder);
			if (!make_dirs(sub)) {
				skip(skipped, m, "its folder could not be made");
				continue;
			}
			bool loaded = rb3d_view_load(v, m->path, err, sizeof err);
			tell_warnings(v, &seen);
			if (!loaded) {
				skip(skipped, m, err);
				continue;
			}
			cJSON *rep = cJSON_CreateObject();
			cJSON_AddStringToObject(rep, "file", rel);
			if (!rb3d_export_glb(v, &l, m, path, GENERATOR, rep, err, sizeof err)) {
				cJSON_Delete(rep);
				skip(skipped, m, err);
				continue;
			}
			cJSON_AddItemToArray(models, rep);
			written++;
		}
		cJSON_AddNumberToObject(root, "seconds", (double)(SDL_GetTicks64() - t0) / 1000.0);
		char jpath[1600];
		snprintf(jpath, sizeof jpath, "%s\\export.json", a->out);
		char *text = cJSON_Print(root);
		if (!text || !save_bytes(jpath, (const uint8_t *)text, strlen(text))) {
			fprintf(stderr, APP_EXE ": could not write %s\n", jpath);
			written = -1;
		}
		free(text);
		cJSON_Delete(root);
	}
	rb3d_view_free(v);
	SDL_GL_DeleteContext(gl);
	SDL_DestroyWindow(win);
	SDL_Quit();
	if (written >= 0)
		printf("Exported %d of %d %s to %s\n", written, asked, asked == 1 ? "model" : "models", a->out);
	fflush(stdout);
	return written == asked && asked > 0 ? 0 : 1;
}

int run_texture_file(const char *dir, const char *select, const char *name, const char *out, bool uv)
{
	const char *opt = uv ? "--uv-map" : "--save-texture";
	if (!select) {
		fprintf(stderr, APP_EXE ": %s takes --select MODEL (a folder, or Kind/folder)\n", opt);
		return 2;
	}
	if (!game_open(dir)) {
		fprintf(stderr, APP_EXE ": could not open the game in %s: %s\n", dir, G.errors);
		return 1;
	}
	static struct rb3d_list l;
	if (!G.h.red || !rb3d_list_read(G.h.red, G.dir, NULL, 0, &l)) {
		fprintf(stderr, APP_EXE ": %s has no 3D models\n", G.title);
		return 1;
	}
	int sel = find_model(&l, select);
	if (sel < 0) {
		fprintf(stderr, APP_EXE ": %s has no 3D model named %s\n", G.title, select);
		return 1;
	}
	char why[1200] = "";
	int w = 0, h = 0;
	uint8_t *px = uv ? rb3d_uv_map(G.h.red, l.m[sel].path, name, &w, &h, why, sizeof why)
	                 : rb3d_texture_rgba(G.h.red, l.m[sel].path, name, &w, &h, why, sizeof why);
	if (!px) {
		fprintf(stderr, APP_EXE ": %s\n", why);
		return 1;
	}
	bool ok = save_png(out, px, w, h);
	free(px);
	if (!ok) {
		fprintf(stderr, APP_EXE ": could not write %s\n", out);
		return 1;
	}
	printf("%dx%d\n", w, h);
	fflush(stdout);
	return 0;
}

#endif
