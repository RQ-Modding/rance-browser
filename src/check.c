/* rance-browser --check: opens every archive and reads a sample of each kind, the way the tabs do
 * (every Flash file played, every 3D model drawn in a hidden window and its motions played), then
 * prints the counts as JSON (exit 1 when something failed), for a script to compare with each tested
 * game's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "swf.h"
#include "swf_check.h"
#include "rq_pixels.h"
#include "cJSON.h"

#include "browser.h"

extern const int rq_lua_lines_checked;

static cJSON *failures;

static void fail(const char *fmt, const char *what, int n)
{
	char buf[512];
	snprintf(buf, sizeof buf, fmt, what, n);
	cJSON_AddItemToArray(failures, cJSON_CreateString(buf));
}

/* The first, the middle and the last entry of a range. */
static int sample(int lo, int hi, int k)
{
	return k == 0 ? lo : k == 1 ? (lo + hi) / 2 : hi;
}

static cJSON *check_cgs(void)
{
	cJSON *o = cJSON_CreateObject();
	int decoded = 0;
	cJSON *folders = cJSON_AddArrayToObject(o, "folders");
	for (int f = 0; f < G.ncg_folders; f++) {
		int lo = -1, hi = -1;
		for (int i = 0; i < G.ncg; i++)
			if (G.cgs[i].folder == f) {
				if (lo < 0) lo = i;
				hi = i;
			}
		cJSON *fo = cJSON_CreateObject();
		cJSON_AddStringToObject(fo, "name", G.cg_folders[f].jp);
		cJSON_AddStringToObject(fo, "label", G.cg_folders[f].en);
		cJSON_AddNumberToObject(fo, "count", G.cg_folders[f].count);
		cJSON_AddItemToArray(folders, fo);
		if (lo < 0)
			continue;
		int last = -1;
		for (int k = 0; k < 3; k++) {
			int i = sample(lo, hi, k);
			/* the middle of a folder may fall on another folder's entry: find this one's */
			while (i <= hi && G.cgs[i].folder != f)
				i++;
			if (i > hi || i == last)
				continue;
			last = i;
			struct cg *cg = game_load_cg(&G.h, i);
			if (cg && cg->pixels && cg->metrics.w > 0)
				decoded++;
			else
				fail("%s %d could not be read", "CG", G.cgs[i].no);
			if (cg)
				cg_free(cg);
		}
	}
	cJSON_AddNumberToObject(o, "count", G.ncg);
	cJSON_AddNumberToObject(o, "decoded", decoded);
	return o;
}

static cJSON *check_flash(void)
{
	cJSON *o = cJSON_CreateObject();
	int counts[FLASH_NFOLDERS] = { 0 }, decoded = 0, bitmaps = 0, sounds = 0;
	for (int i = 0; i < G.nflash; i++)
		counts[G.flash[i].folder]++;
	for (int f = 0; f < FLASH_NFOLDERS; f++) {
		if (counts[f])
			cJSON_AddNumberToObject(o, flash_folder_names[f], counts[f]);
		int lo = -1, hi = -1;
		for (int i = 0; i < G.nflash; i++)
			if (G.flash[i].folder == f) {
				if (lo < 0) lo = i;
				hi = i;
			}
		if (lo < 0)
			continue;
		int last = -1;
		for (int k = 0; k < 3; k++) {
			int i = sample(lo, hi, k);
			if (i == last)
				continue;
			last = i;
			size_t size = 0;
			uint8_t *raw = game_read_flash(&G.h, i, &size);
			struct swf *swf = raw ? aff_load(raw, size) : NULL;
			if (!swf) {
				fail("Flash %s (%d) could not be read", G.flash[i].base, i);
			} else {
				decoded++;
				for (struct swf_tag *t = swf->tags; t; t = t->next) {
					if (t->type == TAG_DEFINE_BITS_LOSSLESS || t->type == TAG_DEFINE_BITS_LOSSLESS2)
						bitmaps++;
					else if (t->type == TAG_DEFINE_SOUND)
						sounds++;
				}
				swf_free(swf);
			}
			free(raw);
		}
	}
	cJSON_AddNumberToObject(o, "count", G.nflash);
	cJSON_AddNumberToObject(o, "decoded", decoded);
	cJSON_AddNumberToObject(o, "bitmaps_in_decoded", bitmaps);
	cJSON_AddNumberToObject(o, "sounds_in_decoded", sounds);

	/* Every file by the engine's rules (swf_check.c), then played to its last frame as the Flash
	 * tab plays it, and back to its first. */
	int plays = 0, warned = 0, played = 0, back = 0, frames = 0, started = 0, jumps = 0, most = 0;
	for (int i = 0; i < G.nflash; i++) {
		size_t size = 0;
		uint8_t *raw = game_read_flash(&G.h, i, &size);
		if (!raw) {
			fail("Flash %s (%d) could not be read", G.flash[i].base, i);
			continue;
		}
		struct swfc c;
		int code = 3;
		struct fplay *p = fplay_open(raw, size, &c, &code);
		if (code == 0)
			plays++;
		else
			fail("Flash %s (%d): the engine's rules refuse it", G.flash[i].base, i);
		if (c.warnings)
			warned++;
		if (p) {
			int n = fplay_frames(p);
			for (int k = 1; k <= n + 8 && fplay_frame(p) < n; k++) {
				fplay_seek(p, k);
				if (fplay_objects(p) > most)
					most = fplay_objects(p);
			}
			if (fplay_frame(p) == n)
				played++;
			else
				fail("Flash %s (%d) did not play to its last frame", G.flash[i].base, i);
			frames += fplay_frame(p);
			started += fplay_started(p);
			jumps += fplay_jumps(p);
			if (fplay_seek(p, 1), fplay_frame(p) == 1)
				back++;
			fplay_close(p);
		} else if (code == 0) {
			fail("Flash %s (%d): the player did not open it", G.flash[i].base, i);
		}
		swfc_free(&c);
		free(raw);
	}
	cJSON_AddNumberToObject(o, "checked", G.nflash);
	cJSON_AddNumberToObject(o, "plays", plays);
	cJSON_AddNumberToObject(o, "with_warnings", warned);
	cJSON_AddNumberToObject(o, "played", played);
	cJSON_AddNumberToObject(o, "back_to_first", back);
	cJSON_AddNumberToObject(o, "frames_played", frames);
	cJSON_AddNumberToObject(o, "sounds_started", started);
	cJSON_AddNumberToObject(o, "jumps", jumps);
	cJSON_AddNumberToObject(o, "most_objects", most);
	return o;
}

static cJSON *check_music(void)
{
	cJSON *o = cJSON_CreateObject();
	int loops = 0, loops_ok = 0, probed = 0;
	for (int i = 0; i < G.nmusic; i++) {
		struct audio_entry *a = &G.music[i];
		size_t size = 0;
		uint8_t *data = game_read_audio(G.h.ba, a->no, &size);
		int64_t frames = 0;
		/* the first track is decoded to its end, the others read their header */
		if (data && player_probe(data, size, i == 0, &frames, NULL, NULL))
			probed++;
		else
			fail("%s %d could not be read", "music", a->no);
		if (a->has_loop) {
			loops++;
			if (a->loop_start >= 0 && a->loop_start < a->loop_end && a->loop_end <= frames + 1)
				loops_ok++;
			else if (a->loop_end > 0 || a->loop_start > 0)
				fail("%s %d: its loop is outside the track", "music", a->no);
			else
				loops_ok++;   /* 0, 0: the whole track */
		}
		free(data);
	}
	int named = 0;
	for (int i = 0; i < G.nmusic; i++)
		if (G.music[i].name)
			named++;
	cJSON_AddNumberToObject(o, "count", G.nmusic);
	cJSON_AddNumberToObject(o, "read", probed);
	cJSON_AddNumberToObject(o, "loops", loops);
	cJSON_AddNumberToObject(o, "loops_ok", loops_ok);
	cJSON_AddNumberToObject(o, "named", named);
	return o;
}

static cJSON *check_sounds(void)
{
	cJSON *o = cJSON_CreateObject();
	int probed = 0;
	for (int i = 0; i < G.nsounds; i++) {
		size_t size = 0;
		uint8_t *data = game_read_audio(G.h.wa, G.sounds[i].no, &size);
		if (data && player_probe(data, size, i == 0 || i == G.nsounds - 1, NULL, NULL, NULL))
			probed++;
		else
			fail("%s %d could not be read", "sound", G.sounds[i].no);
		free(data);
	}
	cJSON_AddNumberToObject(o, "count", G.nsounds);
	cJSON_AddNumberToObject(o, "read", probed);
	return o;
}

/* A set of pictures (3D textures, other pictures, dungeon textures): its count by folder, and the
 * first, middle and last picture of each folder decoded. */
static cJSON *check_set(int kind)
{
	struct pic_set *s = &G.sets[kind];
	cJSON *o = cJSON_CreateObject();
	int decoded = 0;
	cJSON *folders = cJSON_AddObjectToObject(o, "folders");
	for (int f = 0; f < s->nfolders; f++) {
		if (!s->counts[f])
			continue;
		cJSON_AddNumberToObject(folders, s->folders[f], s->counts[f]);
		int lo = -1, hi = -1;
		for (int i = 0; i < s->n; i++)
			if (s->e[i].folder == f) {
				if (lo < 0) lo = i;
				hi = i;
			}
		int last = -1;
		for (int k = 0; k < 3; k++) {
			int i = sample(lo, hi, k);
			while (i <= hi && s->e[i].folder != f)
				i++;
			if (i > hi || i == last)
				continue;
			last = i;
			struct cg *cg = game_load_pic(&G.h, kind, i);
			if (cg && cg->metrics.w > 0)
				decoded++;
			else
				fail("%s could not be read (%d)", s->e[i].path, i);
			if (cg)
				cg_free(cg);
		}
	}
	cJSON_AddNumberToObject(o, "count", s->n);
	cJSON_AddNumberToObject(o, "decoded", decoded);
	return o;
}

#ifdef RB_3D
#include <SDL.h>
#include "rb3d.h"

static void fail_3d(void *ctx, const char *text)
{
	(void)ctx;
	cJSON_AddItemToArray(failures, cJSON_CreateString(text));
}

/* The model at path loaded and drawn on its rest pose, from where the 3D tab first shows a monster:
 * its RGBA, or NULL with why. */
static uint8_t *load_and_draw(struct rb3d_view *v, const char *path, int *w, int *h, char *why, size_t n)
{
	if (!rb3d_view_load(v, path, why, n))
		return NULL;
	struct rb3d_draw d;
	rb3d_draw_default(&d, RB3D_CHARA);
	d.back = RB3D_BACK_ALPHA;
	if (!rb3d_view_draw(v, 0, &d, why, n))
		return NULL;
	uint8_t *px = rb3d_view_rgba(v, w, h);
	if (!px)
		snprintf(why, n, "the frame could not be read back");
	return px;
}

/* A tried file gone at a load, as the engine reads a mod's picture again at each load: the
 * Cockroach drawn plain, with a PNG of ours (written to the temporary folder) tried on gokiburi.png,
 * then with that file deleted and loaded again, twice. The game's picture again, the warning once,
 * nothing closed. */
static cJSON *check_gone_file(const struct rb3d_list *l)
{
	cJSON *o = cJSON_CreateObject();
	char dir[1024], png[1200], why[1200] = "", text[1400];
	int k = rb3d_list_find(l, RB3D_CHARA, "gokiburi");
	if (k < 0 || !win_temp_dir(dir, sizeof dir)) {
		fail_3d(NULL, "3D tries: no Cockroach (gokiburi) or no temporary folder");
		return o;
	}
	const char *path = l->m[k].path;
	snprintf(png, sizeof png, "%s\\rance-browser-check-%llu.png", dir, (unsigned long long)SDL_GetPerformanceCounter());
	enum { SIDE = 64 };
	static uint8_t magenta[SIDE * SIDE * 4];
	for (int i = 0; i < SIDE * SIDE; i++) {
		magenta[i * 4 + 0] = 255;
		magenta[i * 4 + 1] = 0;
		magenta[i * 4 + 2] = 255;
		magenta[i * 4 + 3] = 255;
	}
	if (!save_png(png, magenta, SIDE, SIDE)) {
		snprintf(text, sizeof text, "3D tries: could not write %s", png);
		fail_3d(NULL, text);
		return o;
	}
	struct rb3d_view *v = rb3d_view_new(G.f.red, 96, 96, why, sizeof why);
	uint8_t *plain = NULL, *tried = NULL, *gone = NULL, *again = NULL;
	int w = 0, h = 0, warnings = -1, after = -1;
	struct rb3d_tries *t = NULL;
	if (v && (plain = load_and_draw(v, path, &w, &h, why, sizeof why))) {
		t = rb3d_tries_new(G.h.red, path);
		const char *files[1] = { png };
		if (rb3d_tries_png(t, "gokiburi.png", files, NULL, 1, why, sizeof why)) {
			rb3d_view_set_tries(v, t);
			tried = load_and_draw(v, path, &w, &h, why, sizeof why);
			remove_utf8(png);
			if (tried && (gone = load_and_draw(v, path, &w, &h, why, sizeof why))) {
				warnings = rb3d_tries_warnings(t);
				again = load_and_draw(v, path, &w, &h, why, sizeof why);
				after = rb3d_tries_warnings(t);
				cJSON_AddStringToObject(o, "warning", rb3d_tries_warning(t));
			}
		} else {
			rb3d_tries_free(t);
		}
	}
	remove_utf8(png);
	size_t bytes = (size_t)w * h * 4;
	bool differs = plain && tried && memcmp(plain, tried, bytes) != 0;
	bool back = plain && gone && again && !memcmp(plain, gone, bytes) && !memcmp(plain, again, bytes);
	cJSON_AddBoolToObject(o, "tried_drawn", differs);
	cJSON_AddBoolToObject(o, "gone_drawn_as_the_game", back);
	cJSON_AddNumberToObject(o, "warnings", warnings);
	cJSON_AddNumberToObject(o, "warnings_after_another_load", after);
	if (!plain || !tried || !gone || !again) {
		snprintf(text, sizeof text, "3D tries: the Cockroach did not load or draw: %s", why);
		fail_3d(NULL, text);
	} else {
		if (!differs)
			fail_3d(NULL, "3D tries: a PNG tried on gokiburi.png drew the game's picture");
		if (!back)
			fail_3d(NULL, "3D tries: with the tried file gone, the Cockroach is not drawn as the game draws it");
		if (warnings != 1 || after != 1) {
			snprintf(text, sizeof text, "3D tries: %d warnings for the file gone, then %d after another load (1 and 1)", warnings,
			         after);
			fail_3d(NULL, text);
		}
	}
	free(plain);
	free(tried);
	free(gone);
	free(again);
	if (v)
		rb3d_view_free(v);
	return o;
}

/* The 3D models (rb3d_check): their files read, then, in a hidden window as --shot makes one, each
 * drawn and each motion played. NULL when the game has none. */
static cJSON *check_3d(void)
{
	if (!G.h.red)
		return NULL;
	struct rb3d_prop_look looks[32];
	int nl = 0;
	if (G.rq && G.prof)
		for (int i = 0; i < G.prof->nlooks && nl < 32; i++, nl++) {
			looks[nl].object = G.prof->looks[i].object;
			looks[nl].name = G.prof->looks[i].name;
		}
	static struct rb3d_list l;
	if (!rb3d_list_read(G.h.red, G.dir, looks, nl, &l))
		return NULL;
	char why[512] = "";
	SDL_Window *win = NULL;
	SDL_GLContext gl = NULL;
	if (SDL_Init(SDL_INIT_VIDEO) == 0) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		win = SDL_CreateWindow(APP_NAME " --check", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 64, 64,
		                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
		gl = win ? SDL_GL_CreateContext(win) : NULL;
		if (gl)
			SDL_GL_MakeCurrent(win, gl);
	}
	if (!gl)
		snprintf(why, sizeof why, "%s", SDL_GetError());
	cJSON *o = rb3d_check(G.h.red, G.f.red, &l, gl != NULL, fail_3d, NULL);
	if (!gl) {
		char text[640];
		snprintf(text, sizeof text, "3D: no OpenGL window to draw the models in (%s)", why);
		fail_3d(NULL, text);
		cJSON_AddStringToObject(o, "draw_skipped", why);
	}
	/* The Names tab's 3D pictures: each model of models.tsv drawn by a MonsterInfo entry whose folder is
	 * in Chara or Etc, as the tab finds it (the drawing itself is every model's, above). */
	if (G.rq) {
		int named = 0, lost = 0;
		for (int k = 0; k < G.nkinds; k++)
			for (int j = 0; j < G.kinds[k].n; j++) {
				const struct id_entry *e = &G.kinds[k].e[j];
				if (!e->model)
					continue;
				named++;
				const struct rb3d_entry *en = rb3d_list_entry(&l, e->model);
				bool found = en && en->poly && (rb3d_list_find(&l, RB3D_CHARA, en->poly) >= 0 ||
				                                rb3d_list_find(&l, RB3D_ETC, en->poly) >= 0);
				if (!found && ++lost <= 5) {
					char text[512];
					snprintf(text, sizeof text, "names: %s %d (%s): its model %d is drawn by no folder of Chara or Etc",
					         G.kinds[k].kind, e->no, e->name, e->model);
					fail_3d(NULL, text);
				}
			}
		cJSON_AddNumberToObject(o, "names_with_a_model", named);
		cJSON_AddNumberToObject(o, "names_whose_model_has_no_folder", lost);
		if (gl)
			cJSON_AddItemToObject(o, "tries", check_gone_file(&l));
	}
	if (gl)
		SDL_GL_DeleteContext(gl);
	if (win)
		SDL_DestroyWindow(win);
	SDL_Quit();
	return o;
}
#endif

static cJSON *check_names(void)
{
	cJSON *o = cJSON_CreateObject();
	int entries = 0;
	for (int k = 0; k < G.nkinds; k++)
		entries += G.kinds[k].n;
	cJSON_AddNumberToObject(o, "kinds", G.nkinds);
	cJSON_AddNumberToObject(o, "entries", entries);
	cJSON_AddNumberToObject(o, "pictures", G.npictures);
	cJSON_AddNumberToObject(o, "picture_lines", G.npicture_lines);
	int missing = G.npicture_missing;
	cJSON_AddNumberToObject(o, "pictures_not_in_game", missing);
	if (missing)
		fail("%s: %d pictures name a CG the game does not have", "names", missing);
	cJSON_AddNumberToObject(o, "models", G.nmodels);
	return o;
}

/* --decode and --decode-libsys4, for the checks: a PNG read as RQ MOD reads a mod's picture on a 3D
 * texture (rq_pixels, with its limit of 4096 a side), or as libsys4 reads one of the game's. */
int run_decode(const char *in, const char *out, bool libsys4)
{
	size_t size = 0;
	uint8_t *data = file_exists(in) && file_size(in) == 0 ? xcalloc(16, 1) : file_read(in, &size);
	if (!data) {
		fprintf(stderr, APP_EXE ": %s cannot be read\n", in);
		return 1;
	}
	char why[300] = "";
	struct cg *cg = libsys4 ? (cg_check_format(data) == ALCG_PNG ? cg_load_buffer(data, size) : NULL)
	                        : rq_pixels_png(data, size, RQ_PIXELS_TEXTURE_SIDE, why, sizeof why);
	free(data);
	if (!cg || !cg->pixels) {
		fprintf(stderr, APP_EXE ": %s: %s\n", in, libsys4 ? "libsys4 does not read it" : why);
		cg_free(cg);
		return 1;
	}
	bool ok = save_bytes(out, cg->pixels, (size_t)cg->metrics.w * cg->metrics.h * 4);
	if (ok)
		printf("%dx%d alpha=%d\n", cg->metrics.w, cg->metrics.h, cg->metrics.has_alpha ? 1 : 0);
	else
		fprintf(stderr, APP_EXE ": could not write %s\n", out);
	fflush(stdout);
	cg_free(cg);
	return ok ? 0 : 1;
}

int run_check(const char *dir, const char *out_path)
{
	failures = cJSON_CreateArray();
	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "version", BROWSER_VERSION);
	cJSON_AddStringToObject(root, "dir", dir);
	bool opened = game_open(dir);
	if (opened) {
		cJSON *game = cJSON_AddObjectToObject(root, "game");
		cJSON_AddStringToObject(game, "code", G.code);
		cJSON_AddStringToObject(game, "name", G.name);
		cJSON_AddStringToObject(game, "title", G.title);
		cJSON_AddStringToObject(game, "ini", G.f.ini);
		cJSON_AddBoolToObject(game, "profile", G.prof != NULL);
		cJSON_AddBoolToObject(game, "tested", G.tested);
		cJSON_AddBoolToObject(game, "rq", G.rq);
		cJSON_AddStringToObject(game, "cg_archive", G.h.cg_afa ? "afa" : "ald");
		cJSON_AddItemToObject(root, "cgs", check_cgs());
		cJSON_AddItemToObject(root, "flash", check_flash());
		cJSON_AddItemToObject(root, "music", check_music());
		cJSON_AddItemToObject(root, "sounds", check_sounds());
		cJSON_AddItemToObject(root, "textures", check_set(PK_TEX));
		cJSON_AddItemToObject(root, "pictures", check_set(PK_ALK));
		cJSON *dungeons = check_set(PK_DTX);
		cJSON_AddNumberToObject(dungeons, "dungeons", G.ndungeons);
		cJSON_AddItemToObject(root, "dungeons", dungeons);
#ifdef RB_3D
		cJSON *models = check_3d();
		if (models)
			cJSON_AddItemToObject(root, "3d", models);
#endif
		if (G.rq)
			cJSON_AddItemToObject(root, "names", check_names());
		cJSON *font = cJSON_AddObjectToObject(root, "font");
		cJSON_AddStringToObject(font, "file", G.font);
		bool jp = G.font[0] && font_has_japanese(G.font);
		cJSON_AddBoolToObject(font, "japanese", jp);
		if (G.rq)
			cJSON_AddNumberToObject(root, "lua_lines_checked", rq_lua_lines_checked);
	} else {
		fail("%s: not a System 4 game folder (no System40.ini or AliceStart.ini, or no CG archive)", dir, 0);
	}
	if (G.errors[0])
		cJSON_AddStringToObject(root, "open_errors", G.errors);
	bool ok = opened && cJSON_GetArraySize(failures) == 0 && !G.errors[0];
	cJSON_AddBoolToObject(root, "ok", ok);
	cJSON_AddItemToObject(root, "failures", failures);
	char *text = cJSON_Print(root);
	if (out_path && *out_path) {
		save_bytes(out_path, (const uint8_t *)text, strlen(text));
	} else {
		fputs(text, stdout);
		fputc('\n', stdout);
		fflush(stdout);
	}
	free(text);
	cJSON_Delete(root);
	return ok ? 0 : 1;
}
