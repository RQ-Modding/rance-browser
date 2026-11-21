/* The Flash tab: the game's Flash files (<code>AFF.afa), and a .swf or .aff opened from disk, each
 * played as RQ MOD's engine plays it (the core's flash_player.c: the timeline with its actions and
 * sounds, drawn as the engine draws) at the game's 30 frames a second, with what the game makes of
 * it by the engine's rules (swf_check.c); each file's bitmaps with their ids and its sounds; and for
 * Rance Quest Magnum RQ MOD's Lua lines for rq.image.add_flash, play_flash, flash_bitmap and
 * rq.sound.play_flash. */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/file.h"
#include "swf.h"
#include "swf_check.h"

#include "browser.h"
#include "lua_lines.h"

#define SEL_DISK (-2)

struct fbmp {
	int id, w, h;
	unsigned tex;
	const uint8_t *rgba;   /* inside the player's swf */
};

struct fsnd {
	int index;             /* from 1, as rq.sound.play_flash's index */
	int format, rate, channels, bits;
	uint32_t samples, len;
	const uint8_t *data;   /* inside the player's swf */
	bool playable;
};

enum { VIEW_PLAY, VIEW_PICTURES };

static const char *const speed_names[] = { "Speed 1", "Speed 1/2", "Speed 1/4", "Speed 1/10" };
static const double speeds[] = { 1, 0.5, 0.25, 0.1 };
static const char *const back_names[] = { "Under it: squares", "Under it: black", "Under it: white" };
static const int backs[] = { FPLAY_CHECKER, FPLAY_BLACK, FPLAY_WHITE };
static const char *const zoom_names[] = { "Fit", "50%", "100%", "200%" };
static const float zooms[] = { 0, 0.5f, 1, 2 };

static struct {
	int folder;
	char search[96];
	int *idx, n;
	bool dirty, scroll;
	int sel, loaded;       /* a game file's index, SEL_DISK, or -1 */
	/* the file shown */
	char name[256];        /* its name, without the folder */
	char key[512];         /* its key in Lua: the game's name for it, or one for a mod's own file */
	char file[256];        /* the file's name in a mod's folder, for the Lua line */
	size_t size;
	struct fplay *p;
	struct swfc rep;
	bool have_rep;
	int code;              /* swfc_check's answer: 0 the game plays it */
	struct fbmp *bm;
	int nbm, bsel;
	struct fsnd *snd;
	int nsnd;
	/* playing */
	int view;
	bool playing, loop, sound;
	int speed, back, zoom;
	double clock;          /* ms since frame 1, at speed 1 */
	double last;           /* igGetTime at the last frame */
	int asked;             /* the frame last asked by the clock, as the game's T_PartsFlash keeps it */
	bool shot;             /* --shot: paused on shot_frame */
	int shot_frame;
	bool prefs;
	/* a file from disk */
	char disk[1024];       /* its path, or "" */
} F = { .folder = -1, .dirty = true, .sel = -1, .loaded = -1, .bsel = -1, .loop = true, .sound = true };

/* ---- the file shown ---- */

static double play_rate(void)
{
	/* Rance Quest's T_PartsFlash asks for frame 30 x seconds + 1, whatever the file says. */
	double r = fplay_rate(F.p);
	return G.rq || !(r > 0) ? 30.0 : r;
}

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
	fplay_close(F.p);
	F.p = NULL;
	if (F.have_rep)
		swfc_free(&F.rep);
	F.have_rep = false;
	F.code = 3;
	F.playing = false;
	F.clock = 0;
	F.asked = 0;
	F.size = 0;
	mixer_stop(0);
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
	snprintf(F.name, sizeof F.name, "%s", f->base);
	snprintf(F.file, sizeof F.file, "%s.aff", f->base);
}

/* A mod's own file: its key from its name, "mymod_" and the name in small letters. */
static void make_disk_key(const char *path)
{
	const char *base = base_name(path);
	snprintf(F.file, sizeof F.file, "%s", base);
	snprintf(F.name, sizeof F.name, "%s", base);
	char *dot = strrchr(F.name, '.');
	if (dot)
		*dot = 0;
	char k[200];
	size_t n = 0;
	for (const char *c = F.name; *c && n < sizeof k - 1; c++)
		k[n++] = isalnum((unsigned char)*c) && !((unsigned char)*c & 0x80) ? (char)tolower((unsigned char)*c) : '_';
	k[n] = 0;
	snprintf(F.key, sizeof F.key, "mymod_%s", n ? k : "animation");
}

static void lists(void)
{
	const struct swf *swf = fplay_swf(F.p);
	if (!swf)
		return;
	int nb = 0, ns = 0;
	for (struct swf_tag *t = swf->tags; t; t = t->next) {
		if (t->type == TAG_DEFINE_BITS_LOSSLESS || t->type == TAG_DEFINE_BITS_LOSSLESS2)
			nb++;
		else if (t->type == TAG_DEFINE_SOUND)
			ns++;
	}
	F.bm = xcalloc(nb ? nb : 1, sizeof *F.bm);
	F.snd = xcalloc(ns ? ns : 1, sizeof *F.snd);
	for (struct swf_tag *t = swf->tags; t; t = t->next) {
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

static void step_to(int n, bool sound);

/* The bytes of the file shown (the game's or a disk file's), kept by nobody after this. */
static void open_bytes(const uint8_t *data, size_t size)
{
	F.size = size;
	F.p = fplay_open(data, size, &F.rep, &F.code);
	F.have_rep = true;
	if (!F.p)
		return;
	lists();
	if (F.shot) {
		step_to(F.shot_frame > 0 ? F.shot_frame : 1, false);
	} else {
		step_to(1, F.sound);
		F.playing = true;
	}
	F.last = igGetTime();
}

static void load(void)
{
	if (F.sel == F.loaded)
		return;
	unload();
	F.loaded = F.sel;
	if (F.sel == SEL_DISK) {
		make_disk_key(F.disk);
		size_t size = 0;
		uint8_t *data = file_read(F.disk, &size);
		if (data) {
			open_bytes(data, size);
			free(data);
		}
		return;
	}
	if (F.sel < 0)
		return;
	make_key(F.sel);
	size_t size = 0;
	uint8_t *raw = game_read_flash(&G.h, F.sel, &size);
	if (!raw)
		return;
	open_bytes(raw, size);
	free(raw);
}

bool flash_open_file(const char *path)
{
	size_t n = strlen(path);
	if (n < 5 || (strcasecmp(path + n - 4, ".aff") && strcasecmp(path + n - 4, ".swf")))
		return false;
	if (!file_exists(path))
		return false;
	snprintf(F.disk, sizeof F.disk, "%s", path);
	F.sel = SEL_DISK;
	F.loaded = -1;   /* read it again, even when it was the one shown */
	return true;
}

bool flash_has_file(void)
{
	return F.disk[0] != 0;
}

/* ---- the player ---- */

static void step_to(int n, bool sound)
{
	if (!F.p)
		return;
	int frames = fplay_frames(F.p);
	if (n < 1)
		n = 1;
	if (n > frames)
		n = frames;
	fplay_sound(F.p, sound, player_volume());
	fplay_seek(F.p, n);
	F.asked = n;
	F.clock = (n - 1) * 1000.0 / play_rate();
	F.playing = false;
}

/* T_PartsFlash::update: the frame of the clock, 30 x seconds + 1; past the last, frame 1 again
 * with the clock from 0, or the end. */
static void advance(void)
{
	double now = igGetTime(), dt = (now - F.last) * 1000.0;
	F.last = now;
	if (!F.p || !F.playing)
		return;
	if (dt > 250)
		dt = 250;   /* a pause of the window does not jump ahead */
	F.clock += dt * speeds[F.speed];
	int frames = fplay_frames(F.p);
	int want = (int)(play_rate() * F.clock / 1000.0) + 1;
	if (want > frames) {
		if (F.loop) {
			want = 1;
			F.clock = 0;
		} else {
			want = frames;
			F.playing = false;
		}
	}
	if (want == F.asked)
		return;
	fplay_sound(F.p, F.sound, player_volume());
	fplay_seek(F.p, want);
	F.asked = want;
}

static void toggle_play(void)
{
	if (!F.p)
		return;
	if (F.playing) {
		F.playing = false;
		return;
	}
	if (!F.loop && F.asked >= fplay_frames(F.p))
		step_to(1, F.sound);
	F.playing = true;
	F.last = igGetTime();
}

static void load_prefs(void)
{
	if (F.prefs)
		return;
	F.prefs = true;
	const char *s = settings_get("flash_sound");
	if (s)
		F.sound = strcmp(s, "0") != 0;
	s = settings_get("flash_under");
	if (s && atoi(s) >= 0 && atoi(s) < (int)(sizeof backs / sizeof backs[0]))
		F.back = atoi(s);
}

/* A line of facts, dim, cut between words at the panel's edge. */
static void dim(const char *fmt, ...)
{
	char buf[600];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	igPushStyleColor_U32(ImGuiCol_Text, igGetColorU32_Col(ImGuiCol_TextDisabled, 1));
	igTextWrapped("%s", buf);
	igPopStyleColor(1);
}

static const char *plural(int n, const char *one, const char *many)
{
	return n == 1 ? one : many;
}

static void controls(void)
{
	int frames = fplay_frames(F.p), shown = fplay_frame(F.p);
	if (igButton(F.playing ? "Pause" : "Play", v2(S(70), 0)))
		toggle_play();
	if (igIsItemHovered(0))
		igSetTooltip("Space");
	igSameLine(0, S(4));
	if (igButton("|<", v2(0, 0)))
		step_to(1, F.sound);
	if (igIsItemHovered(0))
		igSetTooltip("The first frame");
	igSameLine(0, S(4));
	if (igButton("<", v2(0, 0)))
		step_to(F.asked - 1, F.sound);
	if (igIsItemHovered(0))
		igSetTooltip("One frame back (comma): the game walks the file again from its start, quiet before this frame");
	igSameLine(0, S(4));
	if (igButton(">", v2(0, 0)))
		step_to(F.asked + 1, F.sound);
	if (igIsItemHovered(0))
		igSetTooltip("One frame on (period)");
	igSameLine(0, S(4));
	if (igButton(">|", v2(0, 0)))
		step_to(frames, F.sound);
	if (igIsItemHovered(0))
		igSetTooltip("The last frame");
	igSameLine(0, S(12));
	igCheckbox("Loop", &F.loop);
	igSameLine(0, S(12));
	if (igCheckbox("Sounds", &F.sound)) {
		settings_set("flash_sound", F.sound ? "1" : "0");
		if (!F.sound)
			mixer_stop(0);
	}
	if (igIsItemHovered(0))
		igSetTooltip("Each sound starts at its frame, over the others, as in the game");
	igSetNextItemWidth(S(110));
	if (igBeginCombo("##speed", speed_names[F.speed], 0)) {
		for (int i = 0; i < (int)(sizeof speeds / sizeof speeds[0]); i++)
			if (igSelectable_Bool(speed_names[i], F.speed == i, 0, v2(0, 0)))
				F.speed = i;
		igEndCombo();
	}
	igSameLine(0, S(8));
	igSetNextItemWidth(S(150));
	if (igBeginCombo("##under", back_names[F.back], 0)) {
		for (int i = 0; i < (int)(sizeof backs / sizeof backs[0]); i++) {
			if (igSelectable_Bool(back_names[i], F.back == i, 0, v2(0, 0))) {
				F.back = i;
				char v[8];
				snprintf(v, sizeof v, "%d", i);
				settings_set("flash_under", v);
			}
		}
		igEndCombo();
	}
	igSameLine(0, S(8));
	igSetNextItemWidth(S(80));
	if (igBeginCombo("##zoom", zoom_names[F.zoom], 0)) {
		for (int i = 0; i < (int)(sizeof zooms / sizeof zooms[0]); i++)
			if (igSelectable_Bool(zoom_names[i], F.zoom == i, 0, v2(0, 0)))
				F.zoom = i;
		igEndCombo();
	}

	igSetNextItemWidth(-1);
	int v = shown < 1 ? 1 : shown;
	char fmt[64];
	snprintf(fmt, sizeof fmt, "Frame %%d of %d", frames);
	if (igSliderInt("##frame", &v, 1, frames > 1 ? frames : 1, fmt, ImGuiSliderFlags_AlwaysClamp))
		step_to(v, false);   /* dragged: no sound for each frame passed */

	double rate = play_rate();
	igTextDisabled("%.2f s of %.2f s, at %s%.0f frames a second%s", (shown > 0 ? shown - 1 : 0) / rate, frames / rate,
	               G.rq ? "the game's " : "", rate, fplay_ended(F.p) ? " · a stop or the file's end was met" : "");
	double own = fplay_rate(F.p);
	if (G.rq && own > 0 && fabs(own - 30) > 0.01) {
		igPushStyleColor_Vec4(ImGuiCol_Text, v4(1.0f, 0.75f, 0.35f, 1));
		igTextWrapped("The file says %.0f frames a second; the game plays every Flash file at 30.", own);
		igPopStyleColor(1);
	}

	ImGuiIO *io = igGetIO_Nil();
	if (!io->WantTextInput && !batch_running()) {
		if (igIsKeyPressed_Bool(ImGuiKey_Space, false))
			toggle_play();
		if (igIsKeyPressed_Bool(ImGuiKey_Comma, true))
			step_to(F.asked - 1, F.sound);
		if (igIsKeyPressed_Bool(ImGuiKey_Period, true))
			step_to(F.asked + 1, F.sound);
	}
}

static void stage(void)
{
	int w = fplay_width(F.p), h = fplay_height(F.p);
	igBeginChild_Str("stage", v2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	V2 av = igGetContentRegionAvail();
	float k = zooms[F.zoom] * CORE.scale;
	if (!(k > 0)) {
		k = fminf(av.x / w, av.y / h);
		if (k > 4 * CORE.scale)
			k = 4 * CORE.scale;
	}
	if (k < 0.05f)
		k = 0.05f;
	float dw = w * k, dh = h * k;
	float ox = dw < av.x ? floorf((av.x - dw) / 2) : 0, oy = dh < av.y ? floorf((av.y - dh) / 2) : 0;
	V2 pos = igGetCursorScreenPos();
	igDummy(v2(dw + ox, dh + oy));
	V2 a = v2(floorf(pos.x + ox), floorf(pos.y + oy)), b = v2(a.x + dw, a.y + dh);
	ImDrawList *dl = igGetWindowDrawList();
	unsigned tex = fplay_render(F.p, k, backs[F.back]);
	if (tex)
		ImDrawList_AddImage(dl, texref(tex), a, b, v2(0, 0), v2(1, 1), 0xffffffff);
	else
		ImDrawList_AddText_Vec2(dl, v2(a.x + S(8), a.y + S(8)), igGetColorU32_Col(ImGuiCol_TextDisabled, 1),
		                        "This window cannot draw the frames (no OpenGL 3).", NULL);
	ImDrawList_AddRect(dl, v2(a.x - 1, a.y - 1), v2(b.x + 1, b.y + 1), 0x50ffffff, 0, 1, 0);
	igEndChild();
}

static void pictures(void)
{
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

/* ---- the list ---- */

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
	if (igButton("Open a .swf or .aff file...", v2(-1, 0))) {
		char path[1024];
		if (win_pick_open("Open a Flash file to play it as the game does", "Flash files", "*.aff;*.swf", path, sizeof path) &&
		    !flash_open_file(path))
			ui_status("Not a .swf or .aff file: %s", path);
	}
	if (igIsItemHovered(0))
		igSetTooltip("A Flash file of a mod's, or one you made: played as the game plays it, with what the game\n"
		             "makes of it. Dropping a file on the window opens it too.");
	if (F.disk[0]) {
		V2 pos = igGetCursorScreenPos();
		float rh = igGetTextLineHeightWithSpacing() * 2;
		if (igSelectable_Bool("##disk", F.sel == SEL_DISK, 0, v2(0, rh - S(4))))
			F.sel = SEL_DISK;
		ImDrawList *dl = igGetWindowDrawList();
		ImDrawList_AddText_Vec2(dl, v2(pos.x + S(4), pos.y + S(2)), igGetColorU32_Col(ImGuiCol_Text, 1), base_name(F.disk), NULL);
		ImDrawList_AddText_Vec2(dl, v2(pos.x + S(4), pos.y + rh / 2), igGetColorU32_Col(ImGuiCol_TextDisabled, 1), "From disk", NULL);
	}
	if (!G.nflash)
		return;
	igSeparator();
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

/* ---- the middle: the player, or the pictures ---- */

static void middle(void)
{
	if (F.sel == -1) {
		igTextDisabled(G.nflash ? "Pick a Flash file on the left." : "Open a .swf or .aff file, or drop one on the window.");
		return;
	}
	igText("%s", F.name);
	if (!F.p) {
		if (F.have_rep && F.code == 1)
			igTextColored(v4(1, 0.45f, 0.4f, 1), "The game would not play this file. What it would refuse is on the right.");
		else if (F.have_rep && F.code == 3)
			igTextColored(v4(1, 0.45f, 0.4f, 1), "This is not a Flash file the game reads. Why is on the right.");
		else
			igTextDisabled("This file could not be read.");
		return;
	}
	int frames = fplay_frames(F.p);
	dim("%d %s · %d %s · %d %s · %d x %d", F.nbm, plural(F.nbm, "picture", "pictures"), F.nsnd,
	    plural(F.nsnd, "sound", "sounds"), frames, plural(frames, "frame", "frames"), fplay_width(F.p),
	    fplay_height(F.p));
	if (igRadioButton_Bool("Play it", F.view == VIEW_PLAY))
		F.view = VIEW_PLAY;
	igSameLine(0, S(12));
	if (igRadioButton_Bool("Its pictures", F.view == VIEW_PICTURES))
		F.view = VIEW_PICTURES;
	igSeparator();
	if (F.view == VIEW_PLAY) {
		controls();
		stage();
	} else {
		pictures();
	}
}

/* ---- the right: the file, what the game makes of it, the Lua lines, the pictures and sounds ---- */

static bool save_bitmap(int k, const char *dir)
{
	char path[2048];
	struct fbmp *m = &F.bm[k];
	snprintf(path, sizeof path, "%s/%d_%dx%d.png", dir, m->id, m->w, m->h);
	return save_png(path, m->rgba, m->w, m->h);
}

static void report(void)
{
	const struct swfc *c = &F.rep;
	igSeparatorText("What the game makes of it");
	if (F.code == 0 && !c->warnings)
		igTextColored(v4(0.5f, 0.85f, 0.5f, 1), "The game plays it.");
	else if (F.code == 0)
		igTextColored(v4(0.95f, 0.8f, 0.4f, 1), "The game plays it, with %d warning%s.", c->warnings, c->warnings == 1 ? "" : "s");
	else if (F.code == 1)
		igTextColored(v4(1, 0.45f, 0.4f, 1), "The game would not play it: %d problem%s.", c->errors, c->errors == 1 ? "" : "s");
	else
		igTextColored(v4(1, 0.45f, 0.4f, 1), "Not a Flash file the game reads.");
	if (c->container[0]) {
		if (c->inner[0])
			dim("An .aff around a SWF %d (%s), %d x %d, %d %s: %.2f s at the game's 30 a second.", c->version,
			    c->inner, c->width, c->height, c->frames, plural(c->frames, "frame", "frames"), swfc_seconds(c));
		else
			dim("A SWF %d (%s), %d x %d, %d %s: %.2f s at the game's 30 a second.", c->version, c->container,
			    c->width, c->height, c->frames, plural(c->frames, "frame", "frames"), swfc_seconds(c));
		dim("%d %s (%d with soft edges), %d %s, %d %s; %d %s, started %d %s.", c->bitmaps,
		    plural(c->bitmaps, "bitmap", "bitmaps"), c->soft_bitmaps, c->shapes, plural(c->shapes, "shape", "shapes"),
		    c->sprites, plural(c->sprites, "sprite", "sprites"), c->sounds, plural(c->sounds, "sound", "sounds"),
		    c->starts, plural(c->starts, "time", "times"));
		char modes[256] = "";
		for (int m = 0; m < 16; m++) {
			if (!c->blends[m])
				continue;
			size_t len = strlen(modes);
			snprintf(modes + len, sizeof modes - len, "%s%s %d", len ? ", " : "", m < 15 ? swfc_blend_name(m) : "other", c->blends[m]);
		}
		if (modes[0])
			dim("Placed, by blend mode: %s.", modes);
		if (c->cx_add || c->cx_mult || c->cx_both)
			dim("Colours changed: %d multiplied, %d added.", c->cx_mult + c->cx_both, c->cx_add + c->cx_both);
		for (int i = 0; i < c->ngotos; i++)
			dim("Frame %d jumps to frame %d (gotoFrame %d).", c->gotos[i].frame, c->gotos[i].target + 1, c->gotos[i].target);
	}
	for (int i = 0; i < c->nitems; i++) {
		const struct swfc_item *it = &c->items[i];
		char where[160] = "";
		if (it->frame > 0)
			snprintf(where, sizeof where, "frame %d", it->frame);
		if (it->tag > 0) {
			size_t len = strlen(where);
			const char *tn = it->code >= 0 ? swfc_tag_name(it->code) : NULL;
			if (tn)
				snprintf(where + len, sizeof where - len, "%stag %d (%s)", len ? ", " : "", it->tag, tn);
			else
				snprintf(where + len, sizeof where - len, "%stag %d", len ? ", " : "", it->tag);
		}
		ImVec4_c col = it->level == SWFC_ERROR ? v4(1, 0.45f, 0.4f, 1) : v4(0.95f, 0.8f, 0.4f, 1);
		igPushStyleColor_Vec4(ImGuiCol_Text, col);
		if (it->count > 1)
			igTextWrapped("%s%s%s%s (%d times)", it->level == SWFC_ERROR ? "Refused: " : "Warning: ", where, where[0] ? ": " : "", it->text, it->count);
		else
			igTextWrapped("%s%s%s%s", it->level == SWFC_ERROR ? "Refused: " : "Warning: ", where, where[0] ? ": " : "", it->text);
		igPopStyleColor(1);
	}
	if (c->more)
		igTextDisabled("... and %d more", c->more);
}

static void right(void)
{
	if (F.sel == -1)
		return;
	igSeparatorText(F.name);
	if (F.sel == SEL_DISK) {
		igTextDisabled("Path");
		igSameLine(S(110), -1);
		igTextWrapped("%s", F.disk);
	} else if (F.sel >= 0) {
		struct flash_entry *f = &G.flash[F.sel];
		igTextDisabled("Folder");
		igSameLine(S(110), -1);
		igText("%s", flash_folder_names[f->folder]);
		igTextDisabled("Path");
		igSameLine(S(110), -1);
		igTextWrapped("%s", f->path);
	}
	igTextDisabled("Size");
	igSameLine(S(110), -1);
	igText("%.1f KB", F.size / 1024.0);
	if (F.have_rep)
		report();
	if (!F.p)
		return;

	char line[1024];
	if (G.rq) {
		if (F.sel == SEL_DISK) {
			igSeparatorText("From a mod, as its own file");
			dim("In the mod's folder as flash/%s:", F.file);
			snprintf(line, sizeof line, LUA_FLASH_ADD, F.key, F.file);
			ui_lua_line("add", line);
		} else {
			igSeparatorText("From a mod");
			dim("Replace it, with a file of the same %d x %d (the game places it by its size):",
			    fplay_width(F.p), fplay_height(F.p));
			snprintf(line, sizeof line, LUA_FLASH_ADD, F.key, F.file);
			ui_lua_line("add", line);
		}
		dim("Play it, its top left at a point of the screen:");
		snprintf(line, sizeof line, LUA_FLASH_PLAY, F.key, 0, 0);
		ui_lua_line("play", line);
	}
	if (igButton("Save this frame as PNG...", v2(-1, 0))) {
		char name[300], path[2048];
		snprintf(name, sizeof name, "%s_%d.png", F.name, fplay_frame(F.p));
		int pw = 0, ph = 0;
		uint8_t *px = fplay_rgba(F.p, &pw, &ph);
		if (!px)
			ui_status("Could not draw the frame");
		else if (win_pick_save("Save the frame as PNG", name, "png", path, sizeof path))
			ui_status(save_png(path, px, pw, ph) ? "Saved %s" : "Could not write %s", path);
		free(px);
	}
	if (igIsItemHovered(0))
		igSetTooltip("The frame shown at the file's size, clear around it, as the game draws it");

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
			char title[300];
			snprintf(title, sizeof title, "Flash %s, sound %d", F.name, s->index);
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
	load_prefs();
	if (G.nflash) {
		refilter();
		/* The arrows pick a file; the comma and the period step through the one shown. While a file
		 * from disk is shown, a click on the list goes back to it. */
		int pos = pos_of(F.sel);
		if (pos < 0 && F.n)
			pos = 0;
		if (F.sel != SEL_DISK && ui_nav_keys(&pos, F.n)) {
			F.sel = F.idx[pos];
			F.scroll = true;
		}
	}
	load();
	advance();
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

void flash_leave(void)
{
	mixer_stop(0);
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

bool flash_select_name(const char *name)
{
	for (int i = 0; i < G.nflash; i++) {
		if (!strcmp(G.flash[i].base, name)) {
			flash_select(i);
			return true;
		}
	}
	flash_select(0);
	return false;
}

void flash_shot(int frame, bool grid)
{
	F.shot = true;
	F.shot_frame = frame > 0 ? frame : 1;
	F.view = grid ? VIEW_PICTURES : VIEW_PLAY;
	F.sound = false;
	F.prefs = true;   /* the settings file's choices do not change a shot */
}
