/* rance-browser --check: opens every archive and reads a sample of each kind, the way the tabs do,
 * then prints the counts as JSON (exit 1 when something failed). RQ MOD's scripts\check-browser.ps1
 * compares the counts with each tested game's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "swf.h"
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
	return o;
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
