/* The Flash tab: the game's Flash files (<code>AFF.afa), each one's bitmaps with their ids and its
 * sounds, read with xsystem4's own swf.c, and for Rance Quest Magnum RQ MOD's Lua lines for
 * rq.image.flash_bitmap and rq.sound.play_flash. Playing a file's motion is left for later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "swf.h"

#include "browser.h"
#include "lua_lines.h"

struct fbmp {
	int id, w, h;
	unsigned tex;
	const uint8_t *rgba;   /* inside the swf */
};

struct fsnd {
	int index;             /* from 1, as rq.sound.play_flash's index */
	int format, rate, channels, bits;
	uint32_t samples, len;
	const uint8_t *data;   /* inside the swf */
	bool playable;
};

static struct {
	int folder;
	char search[96];
	int *idx, n;
	bool dirty, scroll;
	int sel, loaded;
	uint8_t *raw;
	struct swf *swf;
	struct fbmp *bm;
	int nbm, bsel;
	struct fsnd *snd;
	int nsnd;
	char key[512];
} F = { .folder = -1, .dirty = true, .sel = -1, .loaded = -1, .bsel = -1 };

static void unload(void)
{
	for (int i = 0; i < F.nbm; i++)
		tex_free(F.bm[i].tex);
	free(F.bm);
	free(F.snd);
	F.bm = NULL;
	F.snd = NULL;
	F.nbm = F.nsnd = 0;
	F.bsel = -1;
	if (F.swf)
		swf_free(F.swf);
	F.swf = NULL;
	free(F.raw);
	F.raw = NULL;
}

static void make_key(int i)
{
	struct flash_entry *f = &G.flash[i];
	if (f->folder == FLASH_SYSTEM)
		snprintf(F.key, sizeof F.key, "%s", f->base);
	else if (f->folder == FLASH_OTHER)
		snprintf(F.key, sizeof F.key, "%s", f->path);
	else
		snprintf(F.key, sizeof F.key, "Data/Flash/%s/%s.swf", flash_folder_names[f->folder], f->base);
}

static void load(void)
{
	if (F.sel == F.loaded)
		return;
	unload();
	F.loaded = F.sel;
	if (F.sel < 0)
		return;
	make_key(F.sel);
	size_t size = 0;
	F.raw = game_read_flash(&G.h, F.sel, &size);
	if (!F.raw)
		return;
	F.swf = aff_load(F.raw, size);
	if (!F.swf)
		return;
	int nb = 0, ns = 0;
	for (struct swf_tag *t = F.swf->tags; t; t = t->next) {
		if (t->type == TAG_DEFINE_BITS_LOSSLESS || t->type == TAG_DEFINE_BITS_LOSSLESS2)
			nb++;
		else if (t->type == TAG_DEFINE_SOUND)
			ns++;
	}
	F.bm = xcalloc(nb ? nb : 1, sizeof *F.bm);
	F.snd = xcalloc(ns ? ns : 1, sizeof *F.snd);
	for (struct swf_tag *t = F.swf->tags; t; t = t->next) {
		if (t->type == TAG_DEFINE_BITS_LOSSLESS || t->type == TAG_DEFINE_BITS_LOSSLESS2) {
			struct swf_tag_define_bits_lossless *b = (struct swf_tag_define_bits_lossless *)t;
			struct fbmp *m = &F.bm[F.nbm++];
			m->id = b->character_id;
			m->w = b->width;
			m->h = b->height;
			m->rgba = b->data;
			m->tex = tex_from_rgba(b->data, b->width, b->height);
		} else if (t->type == TAG_DEFINE_SOUND) {
			struct swf_tag_define_sound *s = (struct swf_tag_define_sound *)t;
			struct fsnd *d = &F.snd[F.nsnd];
			d->index = ++F.nsnd;
			d->format = s->format;
			d->rate = s->spec.rate;
			d->channels = s->spec.channels;
			d->bits = s->spec.sample_size;
			d->samples = s->sample_count;
			d->data = s->data;
			d->len = s->data_len;
			d->playable = s->format == SWF_SOUND_UNCOMPRESSED_LE || s->format == 0;
		}
	}
	if (F.nbm)
		F.bsel = 0;
}

static bool matches(int i)
{
	struct flash_entry *f = &G.flash[i];
	if (F.folder >= 0 && f->folder != F.folder)
		return false;
	return ui_match(f->path, F.search);
}

static void refilter(void)
{
	if (!F.dirty)
		return;
	free(F.idx);
	F.idx = xmalloc((G.nflash ? G.nflash : 1) * sizeof *F.idx);
	F.n = 0;
	for (int i = 0; i < G.nflash; i++)
		if (matches(i))
			F.idx[F.n++] = i;
	F.dirty = false;
}

static int pos_of(int entry)
{
	for (int k = 0; k < F.n; k++)
		if (F.idx[k] == entry)
			return k;
	return -1;
}

static void left_panel(void)
{
	static int counts[FLASH_NFOLDERS];
	static bool counted;
	if (!counted) {
		for (int i = 0; i < G.nflash; i++)
			counts[G.flash[i].folder]++;
		counted = true;
	}
	char preview[128];
	if (F.folder < 0)
		snprintf(preview, sizeof preview, "All folders (%d)", G.nflash);
	else
		snprintf(preview, sizeof preview, "%s (%d)", flash_folder_names[F.folder], counts[F.folder]);
	igSetNextItemWidth(-1);
	if (igBeginCombo("##folder", preview, 0)) {
		if (igSelectable_Bool("All folders", F.folder < 0, 0, v2(0, 0))) {
			F.folder = -1;
			F.dirty = true;
		}
		for (int f = 0; f < FLASH_NFOLDERS; f++) {
			if (!counts[f])
				continue;
			char label[64];
			snprintf(label, sizeof label, "%s (%d)", flash_folder_names[f], counts[f]);
			if (igSelectable_Bool(label, F.folder == f, 0, v2(0, 0))) {
				F.folder = f;
				F.dirty = true;
			}
		}
		igEndCombo();
	}
	char before[96];
	memcpy(before, F.search, sizeof before);
	ui_search_box("##search", "Search: a name", F.search, sizeof F.search);
	if (strcmp(before, F.search))
		F.dirty = true;
	refilter();
	igTextDisabled("%d shown", F.n);
	igBeginChild_Str("rows", v2(0, 0), ImGuiChildFlags_None, 0);
	float rh = igGetTextLineHeightWithSpacing() * 2;
	ImGuiListClipper *clip = ImGuiListClipper_ImGuiListClipper();
	ImGuiListClipper_Begin(clip, F.n, rh);
	int want = F.scroll ? pos_of(F.sel) : -1;
	if (want >= 0)
		ImGuiListClipper_IncludeItemByIndex(clip, want);
	ImDrawList *dl = igGetWindowDrawList();
	while (ImGuiListClipper_Step(clip)) {
		for (int k = clip->DisplayStart; k < clip->DisplayEnd; k++) {
			int i = F.idx[k];
			struct flash_entry *f = &G.flash[i];
			igPushID_Int(i);
			V2 pos = igGetCursorScreenPos();
			if (igSelectable_Bool("##row", F.sel == i, 0, v2(0, rh - S(4)))) {
				F.sel = i;
				F.scroll = true;
			}
			igPopID();
			ImDrawList_AddText_Vec2(dl, v2(pos.x + S(4), pos.y + S(2)), igGetColorU32_Col(ImGuiCol_Text, 1), f->base, NULL);
			char sub[64];
			snprintf(sub, sizeof sub, "%s · %.0f KB", flash_folder_names[f->folder], f->size / 1024.0);
			ImDrawList_AddText_Vec2(dl, v2(pos.x + S(4), pos.y + rh / 2), igGetColorU32_Col(ImGuiCol_TextDisabled, 1), sub, NULL);
			if (k == want) {
				igSetScrollHereY(0.5f);
				F.scroll = false;
			}
		}
	}
	ImGuiListClipper_End(clip);
	ImGuiListClipper_destroy(clip);
	igEndChild();
}

static void middle(void)
{
	if (F.sel < 0) {
		igTextDisabled("Pick a Flash file on the left.");
		return;
	}
	if (!F.swf) {
		igTextDisabled("This file could not be read.");
		return;
	}
	igText("%s", G.flash[F.sel].base);
	igTextDisabled("%d pictures · %d sounds · %d frames at %.0f a second · %d x %d", F.nbm, F.nsnd,
		F.swf->frame_count, F.swf->frame_rate / 256.0, (F.swf->frame_size.x_max - F.swf->frame_size.x_min) / 20,
		(F.swf->frame_size.y_max - F.swf->frame_size.y_min) / 20);
	igSeparator();
	igBeginChild_Str("bitmaps", v2(0, 0), ImGuiChildFlags_None, 0);
	const float cw = S(184), chh = S(208);
	V2 av = igGetContentRegionAvail();
	int cols = (int)(av.x / cw);
	if (cols < 1)
		cols = 1;
	ImDrawList *dl = igGetWindowDrawList();
	for (int i = 0; i < F.nbm; i++) {
		struct fbmp *m = &F.bm[i];
		if (i % cols)
			igSameLine(0, S(4));
		V2 pos = igGetCursorScreenPos();
		igPushID_Int(i);
		if (igSelectable_Bool("##bm", F.bsel == i, 0, v2(cw - S(4), chh - S(4))))
			F.bsel = i;
		igPopID();
		draw_fit_image(m->tex, m->w, m->h, v2(pos.x + S(8), pos.y + S(6)), cw - S(20));
		char label[64];
		snprintf(label, sizeof label, "id %d · %d x %d", m->id, m->w, m->h);
		ImDrawList_AddText_Vec2(dl, v2(pos.x + S(8), pos.y + chh - S(30)), igGetColorU32_Col(ImGuiCol_Text, 1), label, NULL);
	}
	if (!F.nbm)
		igTextDisabled("No pictures in this file.");
	igEndChild();
}

static bool save_bitmap(int k, const char *dir)
{
	char path[2048];
	struct fbmp *m = &F.bm[k];
	snprintf(path, sizeof path, "%s/%d_%dx%d.png", dir, m->id, m->w, m->h);
	return save_png(path, m->rgba, m->w, m->h);
}

static void right(void)
{
	if (F.sel < 0 || !F.swf)
		return;
	struct flash_entry *f = &G.flash[F.sel];
	igSeparatorText(f->base);
	igTextDisabled("Folder");
	igSameLine(S(110), -1);
	igText("%s", flash_folder_names[f->folder]);
	igTextDisabled("Path");
	igSameLine(S(110), -1);
	igTextWrapped("%s", f->path);
	igTextDisabled("Size");
	igSameLine(S(110), -1);
	igText("%.1f KB", f->size / 1024.0);
	char line[768];
	igSeparatorText(G.rq ? "Its pictures, from Lua" : "Its pictures");
	if (G.rq) {
		snprintf(line, sizeof line, LUA_FLASH_BITMAPS, F.key);
		ui_lua_line("bitmaps", line);
	}
	if (F.bsel >= 0 && F.bsel < F.nbm) {
		struct fbmp *m = &F.bm[F.bsel];
		igText("Picture id %d, %d x %d", m->id, m->w, m->h);
		V2 pos = igGetCursorScreenPos();
		igDummy(v2(S(160), S(160)));
		draw_fit_image(m->tex, m->w, m->h, pos, S(160));
		if (G.rq) {
			igTextDisabled("Replace it from a mod:");
			snprintf(line, sizeof line, LUA_FLASH_BITMAP, F.key, m->id, m->id);
			ui_lua_line("bitmap", line);
		}
		if (igButton("Save this picture as PNG...", v2(-1, 0))) {
			char name[64], path[2048];
			snprintf(name, sizeof name, "%d_%dx%d.png", m->id, m->w, m->h);
			if (win_pick_save("Save the picture as PNG", name, "png", path, sizeof path))
				ui_status(save_png(path, m->rgba, m->w, m->h) ? "Saved %s" : "Could not write %s", path);
		}
	}
	if (F.nbm) {
		char label[64];
		snprintf(label, sizeof label, "Save all %d pictures as PNG...", F.nbm);
		char dir[1024];
		if (igButton(label, v2(-1, 0)) && win_pick_folder("Save this file's pictures into this folder", dir, sizeof dir))
			batch_start("Saving the file's pictures as PNG", F.nbm, save_bitmap, dir);
	}
	igSeparatorText("Its sounds");
	if (!F.nsnd)
		igTextDisabled("No sounds in this file.");
	for (int i = 0; i < F.nsnd; i++) {
		struct fsnd *s = &F.snd[i];
		igPushID_Int(i);
		double ms = s->rate ? s->samples * 1000.0 / s->rate : 0;
		igText("Sound %d: %.2f s, %d Hz, %s, %d-bit", s->index, ms / 1000, s->rate, s->channels == 2 ? "stereo" : "mono", s->bits);
		if (s->playable) {
			char title[256];
			snprintf(title, sizeof title, "Flash %s, sound %d", f->base, s->index);
			bool mine = !strcmp(player_title(), title);
			if (igButton(mine && player_playing() ? "Stop" : "Play", v2(64, 0))) {
				if (mine && player_playing())
					player_stop();
				else if (player_load_pcm(s->data, s->len, s->rate, s->channels, s->bits, title))
					player_play();
			}
			if (G.rq) {
				igSameLine(0, -1);
				snprintf(line, sizeof line, LUA_FLASH_SOUND, F.key, s->index);
				ui_lua_line("snd", line);
			}
		} else {
			igTextDisabled(G.rq ? "Compressed (MP3 or ADPCM): rq.sound.play_flash cannot play it."
			                    : "Compressed (MP3 or ADPCM): the browser cannot play it.");
		}
		igPopID();
	}
}

void tab_flash(void)
{
	refilter();
	int pos = pos_of(F.sel);
	if (pos < 0 && F.n)
		pos = 0;
	if (ui_nav_keys(&pos, F.n)) {
		F.sel = F.idx[pos];
		F.scroll = true;
	}
	load();
	igBeginChild_Str("left", v2(S(340), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX, 0);
	left_panel();
	igEndChild();
	igSameLine(0, -1);
	V2 av = igGetContentRegionAvail();
	float rw = S(420);
	igBeginChild_Str("middle", v2(av.x - rw - S(8), 0), ImGuiChildFlags_Borders, 0);
	middle();
	igEndChild();
	igSameLine(0, -1);
	igBeginChild_Str("right", v2(0, 0), ImGuiChildFlags_Borders, 0);
	right();
	igEndChild();
}

void flash_select(int i)
{
	if (i < 0 || i >= G.nflash)
		i = 0;
	if (G.nflash) {
		F.sel = i;
		F.scroll = true;
	}
}
