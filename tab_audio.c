/* The Music and Sounds tabs: the game's tracks (RanceQuestBA.ald, with the loops of
 * RanceQuestBA.bgi) and sounds (RanceQuestWA.ald), a player, and the Lua lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "rqb.h"
#include "lua_lines.h"

struct atab {
	bool music;
	char search[96];
	int *idx, n;
	bool dirty, scroll;
	int sel, loaded;
	bool loop;
	size_t size;
	uint8_t *data;        /* the selected file, for Save */
};

static struct atab AM = { .music = true, .dirty = true, .sel = -1, .loaded = -1, .loop = true };
static struct atab AS = { .music = false, .dirty = true, .sel = -1, .loaded = -1 };

static struct audio_entry *entries(struct atab *t) { return t->music ? G.music : G.sounds; }
static int nentries(struct atab *t) { return t->music ? G.nmusic : G.nsounds; }
static struct archive *archive_of(struct atab *t) { return t->music ? G.h.ba : G.h.wa; }

static void title_of(struct atab *t, int i, char *buf, size_t n)
{
	struct audio_entry *a = &entries(t)[i];
	if (a->name)
		snprintf(buf, n, "%s %d: %s", t->music ? "Music" : "Sound", a->no, a->name);
	else
		snprintf(buf, n, "%s %d", t->music ? "Music" : "Sound", a->no);
}

static void refilter(struct atab *t)
{
	if (!t->dirty)
		return;
	int total = nentries(t);
	free(t->idx);
	t->idx = xmalloc((total ? total : 1) * sizeof *t->idx);
	t->n = 0;
	for (int i = 0; i < total; i++) {
		struct audio_entry *a = &entries(t)[i];
		if (!t->search[0] || ui_number_match(a->no, t->search) || ui_match(a->name, t->search) || ui_match(a->file, t->search))
			t->idx[t->n++] = i;
	}
	t->dirty = false;
}

static int pos_of(struct atab *t, int entry)
{
	for (int k = 0; k < t->n; k++)
		if (t->idx[k] == entry)
			return k;
	return -1;
}

static void apply_loop(struct atab *t)
{
	struct audio_entry *a = &entries(t)[t->loaded];
	if (t->music && a->has_loop)
		player_set_loop(t->loop, a->loop_start, a->loop_end);
	else
		player_set_loop(false, 0, 0);
}

/* Loads the selection into the player (not playing yet). */
static void load(struct atab *t)
{
	if (t->sel == t->loaded)
		return;
	t->loaded = t->sel;
	free(t->data);
	t->data = NULL;
	if (t->sel < 0)
		return;
	t->data = game_read_audio(archive_of(t), entries(t)[t->sel].no, &t->size);
	if (!t->data)
		return;
	char title[256];
	title_of(t, t->sel, title, sizeof title);
	if (player_load(t->data, t->size, title))
		apply_loop(t);
}

static bool mine(struct atab *t)
{
	if (t->loaded < 0)
		return false;
	char title[256];
	title_of(t, t->loaded, title, sizeof title);
	return !strcmp(player_title(), title);
}

static void fmt_time(char *buf, size_t n, int64_t frames, int rate)
{
	double s = rate ? (double)frames / rate : 0;
	snprintf(buf, n, "%d:%05.2f", (int)(s / 60), s - 60 * (int)(s / 60));
}

static void left_panel(struct atab *t)
{
	char before[96];
	memcpy(before, t->search, sizeof before);
	ui_search_box("##search", NULL, t->search, sizeof t->search);
	if (strcmp(before, t->search))
		t->dirty = true;
	refilter(t);
	igTextDisabled("%d shown%s", t->n, t->music ? " · names from rq.ids.music" : "");
	igBeginChild_Str("rows", v2(0, 0), ImGuiChildFlags_None, 0);
	ImGuiListClipper *clip = ImGuiListClipper_ImGuiListClipper();
	ImGuiListClipper_Begin(clip, t->n, igGetTextLineHeightWithSpacing());
	int want = t->scroll ? pos_of(t, t->sel) : -1;
	if (want >= 0)
		ImGuiListClipper_IncludeItemByIndex(clip, want);
	while (ImGuiListClipper_Step(clip)) {
		for (int k = clip->DisplayStart; k < clip->DisplayEnd; k++) {
			int i = t->idx[k];
			struct audio_entry *a = &entries(t)[i];
			char label[256];
			/* The number right-aligned in its own column, so the names line up. */
			snprintf(label, sizeof label, "##row%d", i);
			V2 row = igGetCursorScreenPos();
			bool clicked = igSelectable_Bool(label, t->sel == i, ImGuiSelectableFlags_AllowDoubleClick, v2(0, 0));
			char num[16];
			snprintf(num, sizeof num, "%d", a->no);
			V2 nw = igCalcTextSize(num, NULL, false, -1);
			ImDrawList *rdl = igGetWindowDrawList();
			ImU32 col = igGetColorU32_Col(ImGuiCol_Text, 1);
			ImDrawList_AddText_Vec2(rdl, v2(row.x + S(40) - nw.x, row.y), col, num, NULL);
			if (a->name)
				ImDrawList_AddText_Vec2(rdl, v2(row.x + S(54), row.y), col, a->name, NULL);
			if (clicked) {
				t->sel = i;
				if (igIsMouseDoubleClicked_Nil(0)) {
					load(t);
					player_play();
				}
			}
			if (k == want) {
				igSetScrollHereY(0.5f);
				t->scroll = false;
			}
		}
	}
	ImGuiListClipper_End(clip);
	ImGuiListClipper_destroy(clip);
	igEndChild();
}

/* The seek bar: the loop's span shaded, the play head, click or drag to move. */
static void seek_bar(struct atab *t, bool here)
{
	V2 p0 = igGetCursorScreenPos();
	float w = igGetContentRegionAvail().x, h = S(28);
	igInvisibleButton("seek", v2(w, h), ImGuiButtonFlags_MouseButtonLeft);
	ImDrawList *dl = igGetWindowDrawList();
	ImDrawList_AddRectFilled(dl, p0, v2(p0.x + w, p0.y + h), 0xff303030, 4, 0);
	int64_t frames = here ? player_frames() : 0;
	if (!frames)
		return;
	struct audio_entry *a = &entries(t)[t->loaded];
	if (t->music && a->has_loop) {
		int64_t ls = a->loop_start, le = a->loop_end > 0 && a->loop_end <= frames ? a->loop_end : frames;
		float x0 = p0.x + w * (float)ls / frames, x1 = p0.x + w * (float)le / frames;
		ImDrawList_AddRectFilled(dl, v2(x0, p0.y + S(4)), v2(x1, p0.y + h - S(4)), t->loop ? 0xff6a4a2a : 0xff404040, 2, 0);
	}
	float x = p0.x + w * (float)player_pos() / frames;
	ImDrawList_AddRectFilled(dl, v2(x - S(2), p0.y), v2(x + S(2), p0.y + h), 0xffe0e0e0, 1, 0);
	if (igIsItemActive()) {
		ImGuiIO *io = igGetIO_Nil();
		float f = (io->MousePos.x - p0.x) / w;
		if (f < 0) f = 0;
		if (f > 1) f = 1;
		player_seek((int64_t)(f * frames));
	}
}

static void middle(struct atab *t)
{
	if (t->sel < 0) {
		igTextDisabled(t->music ? "Pick a track on the left. Double-click plays it." : "Pick a sound on the left. Double-click plays it.");
		return;
	}
	load(t);
	bool here = mine(t);
	char title[256];
	title_of(t, t->sel, title, sizeof title);
	igSeparatorText(title);
	if (!t->data) {
		igTextDisabled("This file could not be read.");
		return;
	}
	if (!here) {
		/* Another tab played something since: load this one again. */
		if (igButton("Load", v2(S(120), 0))) {
			t->loaded = -1;
			load(t);
		}
		return;
	}
	bool playing = player_playing();
	if (igButton(playing ? "Pause" : "Play", v2(S(120), S(36))))
		playing ? player_pause() : player_play();
	igSameLine(0, -1);
	if (igButton("Stop", v2(S(120), S(36))))
		player_stop();
	ImGuiIO *io = igGetIO_Nil();
	if (!io->WantTextInput && igIsKeyPressed_Bool(ImGuiKey_Space, false))
		playing ? player_pause() : player_play();
	igSameLine(0, S(24));
	char pos[32], len[32];
	fmt_time(pos, sizeof pos, player_pos(), player_rate());
	fmt_time(len, sizeof len, player_frames(), player_rate());
	igText("%s / %s", pos, len);
	seek_bar(t, here);
	float vol = player_volume() * 100;
	igSetNextItemWidth(S(260));
	if (igSliderFloat("Volume", &vol, 0, 100, "%.0f", 0))
		player_set_volume(vol / 100);
	struct audio_entry *a = &entries(t)[t->sel];
	if (t->music) {
		if (a->has_loop) {
			if (igCheckbox("Loop as the game does", &t->loop))
				apply_loop(t);
			igTextDisabled("The shaded span is the loop: at its end the game goes back to its start.");
		} else {
			igTextDisabled("This track has no loop in RanceQuestBA.bgi.");
		}
	}
	igSpacing();
	igTextDisabled("Space: play or pause   Up and Down: the next one in the list");
}

static void right(struct atab *t)
{
	if (t->sel < 0)
		return;
	struct audio_entry *a = &entries(t)[t->sel];
	char title[256], buf[64];
	title_of(t, t->sel, title, sizeof title);
	igSeparatorText(title);
	igTextDisabled("Number");
	igSameLine(S(130), -1);
	igText("%d", a->no);
	if (a->name) {
		igTextDisabled("Name");
		igSameLine(S(130), -1);
		igTextWrapped("%s", a->name);
	}
	igTextDisabled("File");
	igSameLine(S(130), -1);
	igText("%s (OGG)", a->file);
	igTextDisabled("Size");
	igSameLine(S(130), -1);
	igText("%.1f KB", a->size / 1024.0);
	if (mine(t) && player_frames()) {
		fmt_time(buf, sizeof buf, player_frames(), player_rate());
		igTextDisabled("Length");
		igSameLine(S(130), -1);
		igText("%s", buf);
		igTextDisabled("Format");
		igSameLine(S(130), -1);
		igText("%d Hz, %s", player_rate(), player_channels() == 2 ? "stereo" : "mono");
	}
	if (t->music && a->has_loop) {
		int rate = mine(t) && player_rate() ? player_rate() : 44100;
		igTextDisabled("Loop start");
		igSameLine(S(130), -1);
		fmt_time(buf, sizeof buf, a->loop_start, rate);
		igText("%s (frame %d)", buf, a->loop_start);
		igTextDisabled("Loop end");
		igSameLine(S(130), -1);
		fmt_time(buf, sizeof buf, a->loop_end, rate);
		igText("%s (frame %d)", buf, a->loop_end);
		igTextDisabled("Loops");
		igSameLine(S(130), -1);
		igText("%s", a->loop_count == 0 ? "for ever" : "a set number of times");
	}
	char line[256];
	if (t->music) {
		igSeparatorText("Replace it from a mod");
		snprintf(line, sizeof line, LUA_MUSIC_REPLACE, a->no, a->no);
		ui_lua_line("replace", line);
		igPushTextWrapPos(0);
		igTextDisabled("Your file plays wherever the game plays this track. OGG, WAV, FLAC, Opus or MP3; the Music guide explains loops and moments.");
		igPopTextWrapPos();
		igSeparatorText("Play it from a mod");
		snprintf(line, sizeof line, LUA_MUSIC_PLAY, a->no);
		ui_lua_line("play", line);
	} else {
		igSeparatorText("Play it from a mod");
		snprintf(line, sizeof line, LUA_SOUND_PLAY, a->no);
		ui_lua_line("play", line);
	}
	igSeparatorText("Save");
	if (igButton("Save as OGG...", v2(-1, 0)) && t->data && t->sel == t->loaded) {
		char path[2048], name[64];
		snprintf(name, sizeof name, "%d.ogg", a->no);
		if (win_pick_save("Save the file as the game stores it", name, "ogg", path, sizeof path))
			ui_status(save_bytes(path, t->data, t->size) ? "Saved %s" : "Could not write %s", path);
	}
}

static void tab(struct atab *t)
{
	refilter(t);
	int pos = pos_of(t, t->sel);
	if (pos < 0 && t->n)
		pos = 0;
	if (ui_nav_keys(&pos, t->n)) {
		bool was = player_playing() && mine(t);
		t->sel = t->idx[pos];
		t->scroll = true;
		load(t);
		if (was)
			player_play();
	}
	igBeginChild_Str("left", v2(S(380), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX, 0);
	left_panel(t);
	igEndChild();
	igSameLine(0, -1);
	V2 av = igGetContentRegionAvail();
	float rw = S(380);
	igBeginChild_Str("middle", v2(av.x - rw - S(8), 0), ImGuiChildFlags_Borders, 0);
	middle(t);
	igEndChild();
	igSameLine(0, -1);
	igBeginChild_Str("right", v2(0, 0), ImGuiChildFlags_Borders, 0);
	right(t);
	igEndChild();
}

void tab_music(void) { tab(&AM); }
void tab_sounds(void) { tab(&AS); }

static void select_no(struct atab *t, int no)
{
	int total = nentries(t);
	for (int i = 0; i < total; i++) {
		if (entries(t)[i].no == no || (no < 0 && i == 0)) {
			t->sel = i;
			t->scroll = true;
			return;
		}
	}
	if (total) {
		t->sel = 0;
		t->scroll = true;
	}
}

void music_select(int no) { select_no(&AM, no); }
void sounds_select(int no) { select_no(&AS, no); }
