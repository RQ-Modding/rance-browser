/* The Names tab: rq.ids' kinds as tables, with a picture where the id extract job found one
 * (a character's face, an item's icon), and the key as a Lua line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "browser.h"
#include "lua_lines.h"

static struct {
	int kind;
	char search[96];
	int *idx, n;      /* positions in G.kinds[idx_kind].e that the search keeps */
	int idx_kind;     /* the kind idx was made for */
	bool dirty, scroll, top;
	int sel;
} N = { .dirty = true, .sel = -1, .idx_kind = -1 };

/* idx follows the kind: a row reads G.kinds[N.kind].e[idx[r]], so idx is made again as soon as the
 * kind changes, before any row is drawn. */
static void refilter(void)
{
	if (N.kind < 0 || N.kind >= G.nkinds)
		return;
	if (N.kind != N.idx_kind) {
		N.dirty = true;
		N.top = true;
	}
	if (!N.dirty)
		return;
	struct id_kind *k = &G.kinds[N.kind];
	free(N.idx);
	N.idx = xmalloc((k->n ? k->n : 1) * sizeof *N.idx);
	N.n = 0;
	for (int i = 0; i < k->n; i++) {
		struct id_entry *e = &k->e[i];
		if (!N.search[0] || ui_number_match(e->no, N.search) || ui_match(e->name, N.search) ||
		    ui_match(e->key, N.search) || ui_match(e->detail, N.search))
			N.idx[N.n++] = i;
	}
	N.idx_kind = N.kind;
	N.dirty = false;
}

static int pos_of(int entry)
{
	for (int k = 0; k < N.n; k++)
		if (N.idx[k] == entry)
			return k;
	return -1;
}

static void list(void)
{
	igSetNextItemWidth(-1);
	char preview[160];
	snprintf(preview, sizeof preview, "%s (%d)", G.kinds[N.kind].title, G.kinds[N.kind].n);
	if (igBeginCombo("##kind", preview, ImGuiComboFlags_HeightLarge)) {
		for (int i = 0; i < G.nkinds; i++) {
			char label[160];
			snprintf(label, sizeof label, "%s (%d)   rq.ids.%s", G.kinds[i].title, G.kinds[i].n, G.kinds[i].kind);
			if (igSelectable_Bool(label, N.kind == i, 0, v2(0, 0)) && N.kind != i) {
				N.kind = i;
				N.sel = -1;
			}
		}
		igEndCombo();
	}
	char before[96];
	memcpy(before, N.search, sizeof before);
	ui_search_box("##search", NULL, N.search, sizeof N.search);
	if (strcmp(before, N.search))
		N.dirty = true;
	refilter();
	/* Read after the combo and refilter: the kind may have changed this frame, and the rows below
	 * index its entries. */
	struct id_kind *k = &G.kinds[N.kind];
	igPushTextWrapPos(0);
	igTextDisabled("rq.ids.%s: %s", k->kind, k->what);
	igPopTextWrapPos();
	const float rh = S(34);
	if (!igBeginTable("ids", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
			  v2(0, 0), 0))
		return;
	/* Another kind starts at its first row, not where the last one was scrolled to. */
	if (N.top) {
		igSetScrollY_Float(0);
		N.top = false;
	}
	igTableSetupScrollFreeze(0, 1);
	igTableSetupColumn("No", ImGuiTableColumnFlags_WidthFixed, S(60), 0);
	igTableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, rh, 0);
	igTableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2, 0);
	igTableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 2, 0);
	igTableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch, 1, 0);
	igTableHeadersRow();
	ImGuiListClipper *clip = ImGuiListClipper_ImGuiListClipper();
	ImGuiListClipper_Begin(clip, N.n, rh);
	int want = N.scroll ? pos_of(N.sel) : -1;
	if (want >= 0)
		ImGuiListClipper_IncludeItemByIndex(clip, want);
	while (ImGuiListClipper_Step(clip)) {
		for (int r = clip->DisplayStart; r < clip->DisplayEnd; r++) {
			int i = N.idx[r];
			if (i < 0 || i >= k->n)
				continue;
			struct id_entry *e = &k->e[i];
			igTableNextRow(0, rh);
			igTableNextColumn();
			char label[32];
			snprintf(label, sizeof label, "%d##r%d", e->no, i);
			if (igSelectable_Bool(label, N.sel == i, ImGuiSelectableFlags_SpanAllColumns, v2(0, rh - S(4))))
				N.sel = i;
			if (r == want) {
				igSetScrollHereY(0.5f);
				N.scroll = false;
			}
			igTableNextColumn();
			if (e->cg) {
				int ci = game_find_cg(e->cg);
				struct thumb *t = ci >= 0 ? pics_thumb(PK_CG, ci) : NULL;
				V2 pos = igGetCursorScreenPos();
				if (t && t->tex)
					draw_fit_image(t->tex, t->w, t->h, pos, rh - S(4));
			}
			igTableNextColumn();
			igTextUnformatted(e->name, NULL);
			igTableNextColumn();
			igTextUnformatted(e->key, NULL);
			igTableNextColumn();
			igTextDisabled("%s", e->detail);
		}
	}
	ImGuiListClipper_End(clip);
	ImGuiListClipper_destroy(clip);
	igEndTable();
}

static void details(void)
{
	struct id_kind *k = &G.kinds[N.kind];
	if (N.sel < 0 || N.sel >= k->n) {
		igTextDisabled("Pick a row.");
		return;
	}
	struct id_entry *e = &k->e[N.sel];
	igSeparatorText(e->name);
	igTextDisabled("Number");
	igSameLine(S(110), -1);
	igText("%d", e->no);
	igTextDisabled("Key");
	igSameLine(S(110), -1);
	igText("%s", e->key);
	if (e->detail[0]) {
		igTextDisabled("Detail");
		igSameLine(S(110), -1);
		igTextWrapped("%s", e->detail);
	}
	igSeparatorText("From a mod");
	char line[256];
	snprintf(line, sizeof line, LUA_IDS, k->kind, e->key);
	ui_lua_line("key", line);
	igPushTextWrapPos(0);
	igTextDisabled("The key gives the number, so the code reads as the name. A key that is not there raises an error; rq.ids.find looks a name up.");
	igPopTextWrapPos();
	if (e->cg) {
		igSeparatorText("Its picture");
		int ci = game_find_cg(e->cg);
		igText("CG %d, %s", e->cg, e->use ? e->use : "");
		if (ci >= 0) {
			struct thumb *t = pics_thumb(PK_CG, ci);
			V2 pos = igGetCursorScreenPos();
			igDummy(v2(S(160), S(160)));
			if (t->tex)
				draw_fit_image(t->tex, t->w, t->h, pos, S(160));
			if (igButton("Open in the CGs tab", v2(-1, 0)))
				tab_cgs_select(e->cg);
		}
	}
}

void tab_names(void)
{
	if (!G.nkinds) {
		igTextDisabled("The names could not be read.");
		return;
	}
	refilter();
	int pos = pos_of(N.sel);
	if (pos < 0 && N.n)
		pos = 0;
	if (ui_nav_keys(&pos, N.n)) {
		N.sel = N.idx[pos];
		N.scroll = true;
	}
	V2 av = igGetContentRegionAvail();
	igBeginChild_Str("left", v2(av.x - S(420), 0), ImGuiChildFlags_Borders, 0);
	list();
	igEndChild();
	igSameLine(0, -1);
	igBeginChild_Str("right", v2(0, 0), ImGuiChildFlags_Borders, 0);
	details();
	igEndChild();
}

void names_select(int kind)
{
	if (kind >= 0 && kind < G.nkinds)
		N.kind = kind;
	N.dirty = true;
	N.sel = 0;
}
