/* The picture tabs (CGs, 3D textures, Other pictures, Dungeon textures): a list with thumbnails on
 * the left, the picture in the middle (zoom with the wheel, drag to move) or a grid, and the details
 * on the right, with RQ MOD's Lua line for Rance Quest Magnum's CGs. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"

#include "browser.h"
#include "lua_lines.h"

#define ROW_H S(52)
#define CELL_W S(124)
#define CELL_H S(144)

struct plist {
	int kind;
	int folder;          /* -1: all */
	char search[96];
	int *idx, n;
	bool dirty;
	int sel;             /* an entry, or -1 */
	bool grid;
	bool scroll;         /* bring the selection into view in the list */
	bool scroll_grid;    /* and in the grid */
	int loaded;          /* the entry in the view, or -1 */
	struct cg *cg;
	unsigned tex;
	float zoom, panx, pany;
	bool fit;
};

#define PLIST(k) { .kind = (k), .folder = -1, .dirty = true, .sel = -1, .loaded = -1, .fit = true, .zoom = 1 }
static struct plist PL[PK_NKINDS] = { PLIST(PK_CG), PLIST(PK_TEX), PLIST(PK_ALK), PLIST(PK_DTX) };

static struct pic_entry *entry(struct plist *p, int i)
{
	return &G.sets[p->kind].e[i];
}

static void title_of(struct plist *p, int i, char *buf, size_t n)
{
	if (p->kind == PK_CG)
		snprintf(buf, n, "CG %d", G.cgs[i].no);
	else
		snprintf(buf, n, "%s", entry(p, i)->title);
}

static void sub_of(struct plist *p, int i, char *buf, size_t n)
{
	if (p->kind == PK_CG) {
		struct cg_entry *c = &G.cgs[i];
		snprintf(buf, n, "%s", c->use ? c->use : G.cg_folders[c->folder].en);
	} else {
		struct pic_entry *e = entry(p, i);
		const char *folder = G.sets[p->kind].folders[e->folder];
		if (e->group[0])
			snprintf(buf, n, "%s · %s", folder, e->group);
		else
			snprintf(buf, n, "%s", folder);
	}
}

static bool matches(struct plist *p, int i)
{
	const char *s = p->search;
	if (p->kind == PK_CG) {
		struct cg_entry *c = &G.cgs[i];
		if (p->folder >= 0 && c->folder != p->folder)
			return false;
		if (!*s || ui_number_match(c->no, s))
			return true;
		return ui_match(c->use, s) || ui_match(c->name, s) || ui_match(G.cg_folders[c->folder].en, s);
	}
	struct pic_entry *e = entry(p, i);
	if (p->folder >= 0 && e->folder != p->folder)
		return false;
	return ui_match(e->path, s) || ui_match(e->title, s);
}

static void refilter(struct plist *p)
{
	if (!p->dirty)
		return;
	int total = pic_count(p->kind);
	free(p->idx);
	p->idx = xmalloc((total ? total : 1) * sizeof *p->idx);
	p->n = 0;
	for (int i = 0; i < total; i++)
		if (matches(p, i))
			p->idx[p->n++] = i;
	p->dirty = false;
}

static int pos_of(struct plist *p, int entry_no)
{
	for (int k = 0; k < p->n; k++)
		if (p->idx[k] == entry_no)
			return k;
	return -1;
}

static void select_entry(struct plist *p, int i)
{
	p->sel = i;
	p->scroll = true;
	p->scroll_grid = true;
}

static void load_view(struct plist *p)
{
	if (p->sel == p->loaded)
		return;
	tex_free(p->tex);
	p->tex = 0;
	if (p->cg)
		cg_free(p->cg);
	p->cg = NULL;
	p->loaded = p->sel;
	p->fit = true;
	p->panx = p->pany = 0;
	if (p->sel < 0)
		return;
	p->cg = game_load_pic(&G.h, p->kind, p->sel);
	if (p->cg && p->cg->pixels)
		p->tex = tex_from_rgba(p->cg->pixels, p->cg->metrics.w, p->cg->metrics.h);
}

/* ---- left: the list ---- */

static void folder_combo(struct plist *p)
{
	char preview[256];
	struct pic_set *set = &G.sets[p->kind];
	if (p->folder < 0)
		snprintf(preview, sizeof preview, "All folders (%d)", pic_count(p->kind));
	else if (p->kind == PK_CG)
		snprintf(preview, sizeof preview, "%s (%d)", G.cg_folders[p->folder].en, G.cg_folders[p->folder].count);
	else
		snprintf(preview, sizeof preview, "%s (%d)", set->folders[p->folder], set->counts[p->folder]);
	igSetNextItemWidth(-1);
	if (!igBeginCombo("##folder", preview, ImGuiComboFlags_HeightLarge))
		return;
	if (igSelectable_Bool("All folders", p->folder < 0, 0, v2(0, 0))) {
		p->folder = -1;
		p->dirty = true;
	}
	if (p->kind == PK_CG) {
		for (int f = 0; f < G.ncg_folders; f++) {
			struct cg_folder *cf = &G.cg_folders[f];
			char label[320];
			if (strcmp(cf->en, cf->jp))
				snprintf(label, sizeof label, "%s  ·  %s (%d)", cf->en, cf->jp, cf->count);
			else
				snprintf(label, sizeof label, "%s (%d)", cf->jp, cf->count);
			if (igSelectable_Bool(label, p->folder == f, 0, v2(0, 0))) {
				p->folder = f;
				p->dirty = true;
			}
		}
	} else {
		for (int f = 0; f < set->nfolders; f++) {
			if (!set->counts[f])
				continue;
			char label[256];
			snprintf(label, sizeof label, "%s (%d)", set->folders[f], set->counts[f]);
			if (igSelectable_Bool(label, p->folder == f, 0, v2(0, 0))) {
				p->folder = f;
				p->dirty = true;
			}
		}
	}
	igEndCombo();
}

static void list_row(struct plist *p, int i)
{
	char title[256], sub[512];
	title_of(p, i, title, sizeof title);
	sub_of(p, i, sub, sizeof sub);
	igPushID_Int(i);
	V2 pos = igGetCursorScreenPos();
	if (igSelectable_Bool("##row", p->sel == i, 0, v2(0, ROW_H - S(4))))
		select_entry(p, i);
	igPopID();
	ImDrawList *dl = igGetWindowDrawList();
	struct thumb *t = pics_thumb(p->kind, i);
	if (t->tex)
		draw_fit_image(t->tex, t->w, t->h, v2(pos.x + S(2), pos.y + S(2)), ROW_H - S(8));
	else if (t->state == 3)
		ImDrawList_AddText_Vec2(dl, v2(pos.x + S(10), pos.y + S(14)), igGetColorU32_Col(ImGuiCol_TextDisabled, 1), "?", NULL);
	ImDrawList_AddText_Vec2(dl, v2(pos.x + ROW_H + S(4), pos.y + S(5)), igGetColorU32_Col(ImGuiCol_Text, 1), title, NULL);
	ImDrawList_AddText_Vec2(dl, v2(pos.x + ROW_H + S(4), pos.y + S(26)), igGetColorU32_Col(ImGuiCol_TextDisabled, 1), sub, NULL);
}

static const char *search_hint(int kind)
{
	switch (kind) {
	case PK_TEX: return "Search: a name or a model";
	case PK_ALK: return "Search: a file or a number";
	case PK_DTX: return "Search: wall, floor, sky...";
	}
	return NULL;
}

static void left_panel(struct plist *p)
{
	folder_combo(p);
	char before[96];
	memcpy(before, p->search, sizeof before);
	ui_search_box("##search", search_hint(p->kind), p->search, sizeof p->search);
	if (strcmp(before, p->search))
		p->dirty = true;
	refilter(p);
	igTextDisabled("%d shown", p->n);
	igBeginChild_Str("rows", v2(0, 0), ImGuiChildFlags_None, 0);
	ImGuiListClipper *clip = ImGuiListClipper_ImGuiListClipper();
	ImGuiListClipper_Begin(clip, p->n, ROW_H);
	int want = p->scroll ? pos_of(p, p->sel) : -1;
	if (want >= 0)
		ImGuiListClipper_IncludeItemByIndex(clip, want);
	while (ImGuiListClipper_Step(clip)) {
		for (int k = clip->DisplayStart; k < clip->DisplayEnd; k++) {
			list_row(p, p->idx[k]);
			if (k == want) {
				igSetScrollHereY(0.5f);
				p->scroll = false;
			}
		}
	}
	ImGuiListClipper_End(clip);
	ImGuiListClipper_destroy(clip);
	igEndChild();
}

/* ---- middle: the picture or the grid ---- */

static void grid(struct plist *p)
{
	V2 avail = igGetContentRegionAvail();
	int cols = (int)(avail.x / CELL_W);
	if (cols < 1)
		cols = 1;
	int rows = (p->n + cols - 1) / cols;
	ImGuiListClipper *clip = ImGuiListClipper_ImGuiListClipper();
	ImGuiListClipper_Begin(clip, rows, CELL_H);
	int want = p->scroll_grid ? pos_of(p, p->sel) : -1;
	if (want >= 0)
		ImGuiListClipper_IncludeItemByIndex(clip, want / cols);
	ImDrawList *dl = igGetWindowDrawList();
	while (ImGuiListClipper_Step(clip)) {
		for (int r = clip->DisplayStart; r < clip->DisplayEnd; r++) {
			for (int c = 0; c < cols; c++) {
				int k = r * cols + c;
				if (k >= p->n)
					break;
				int i = p->idx[k];
				if (c)
					igSameLine(0, S(4));
				V2 pos = igGetCursorScreenPos();
				igPushID_Int(i);
				if (igSelectable_Bool("##cell", p->sel == i, ImGuiSelectableFlags_AllowDoubleClick, v2(CELL_W - S(4), CELL_H - S(4)))) {
					select_entry(p, i);
					if (igIsMouseDoubleClicked_Nil(0))
						p->grid = false;
				}
				igPopID();
				struct thumb *t = pics_thumb(p->kind, i);
				if (t->tex)
					draw_fit_image(t->tex, t->w, t->h, v2(pos.x + S(10), pos.y + S(6)), CELL_W - S(24));
				char title[256];
				title_of(p, i, title, sizeof title);
				ImDrawList_PushClipRect(dl, pos, v2(pos.x + CELL_W - S(6), pos.y + CELL_H), true);
				ImDrawList_AddText_Vec2(dl, v2(pos.x + S(6), pos.y + CELL_H - S(30)), igGetColorU32_Col(ImGuiCol_Text, 1), title, NULL);
				ImDrawList_PopClipRect(dl);
			}
			if (want >= 0 && r == want / cols) {
				igSetScrollHereY(0.5f);
				p->scroll_grid = false;
			}
		}
	}
	ImGuiListClipper_End(clip);
	ImGuiListClipper_destroy(clip);
}

static void view(struct plist *p)
{
	load_view(p);
	ImGuiIO *io = igGetIO_Nil();
	int w = p->cg ? p->cg->metrics.w : 0, h = p->cg ? p->cg->metrics.h : 0;
	if (igButton("Fit", v2(0, 0)))
		p->fit = true;
	igSameLine(0, -1);
	if (igButton("100%", v2(0, 0))) {
		p->fit = false;
		p->zoom = 1;
		p->panx = p->pany = 0;
	}
	igSameLine(0, -1);
	igTextDisabled("%d%%   Wheel: zoom   Drag: move   Arrows: next", (int)lroundf(p->zoom * 100));
	V2 p0 = igGetCursorScreenPos();
	V2 av = igGetContentRegionAvail();
	if (av.x < S(16) || av.y < S(16))
		return;
	igInvisibleButton("canvas", av, ImGuiButtonFlags_MouseButtonLeft);
	bool hovered = igIsItemHovered(0), active = igIsItemActive();
	ImDrawList *dl = igGetWindowDrawList();
	ImDrawList_AddRectFilled(dl, p0, v2(p0.x + av.x, p0.y + av.y), 0xff202020, 0, 0);
	if (p->sel < 0) {
		ImDrawList_AddText_Vec2(dl, v2(p0.x + S(16), p0.y + S(16)), igGetColorU32_Col(ImGuiCol_TextDisabled, 1), "Pick a picture on the left.", NULL);
		return;
	}
	if (!p->tex) {
		ImDrawList_AddText_Vec2(dl, v2(p0.x + S(16), p0.y + S(16)), igGetColorU32_Col(ImGuiCol_TextDisabled, 1), "This picture could not be read.", NULL);
		return;
	}
	if (p->fit) {
		float z = fminf(av.x / w, av.y / h);
		p->zoom = z > 2 ? 2 : z;   /* a small picture at most twice its size */
		p->panx = p->pany = 0;
	}
	float cx = p0.x + av.x / 2 + p->panx, cy = p0.y + av.y / 2 + p->pany;
	if (hovered && io->MouseWheel != 0) {
		float nz = p->zoom * powf(1.15f, io->MouseWheel);
		if (nz < 0.05f) nz = 0.05f;
		if (nz > 32) nz = 32;
		float ix = (io->MousePos.x - cx) / p->zoom, iy = (io->MousePos.y - cy) / p->zoom;
		p->panx = io->MousePos.x - p0.x - av.x / 2 - ix * nz;
		p->pany = io->MousePos.y - p0.y - av.y / 2 - iy * nz;
		p->zoom = nz;
		p->fit = false;
	}
	if (active && (io->MouseDelta.x || io->MouseDelta.y)) {
		p->panx += io->MouseDelta.x;
		p->pany += io->MouseDelta.y;
		p->fit = false;
	}
	cx = p0.x + av.x / 2 + p->panx;
	cy = p0.y + av.y / 2 + p->pany;
	float dw = w * p->zoom, dh = h * p->zoom;
	V2 a = v2(floorf(cx - dw / 2), floorf(cy - dh / 2)), b = v2(a.x + dw, a.y + dh);
	ImDrawList_PushClipRect(dl, p0, v2(p0.x + av.x, p0.y + av.y), true);
	draw_checker(dl, a, b, S(10), 0xff595959, 0xff7a7a7a);
	/* From 200 % up each pixel is a sharp square, to look at pixel art. */
	bool sharp = p->zoom >= 2.0f;
	if (sharp)
		draw_sampler_nearest(dl, true);
	ImDrawList_AddImage(dl, texref(p->tex), a, b, v2(0, 0), v2(1, 1), 0xffffffff);
	if (sharp)
		draw_sampler_nearest(dl, false);
	ImDrawList_PopClipRect(dl);
}

/* ---- right: the details ---- */

static struct plist *batch_p;
static int *batch_idx;

static void save_name(struct plist *p, int i, char *out, size_t n)
{
	if (p->kind == PK_CG)
		snprintf(out, n, "%d.png", G.cgs[i].no);
	else
		snprintf(out, n, "%s.png", entry(p, i)->save);
}

static bool save_one(int k, const char *dir)
{
	struct plist *p = batch_p;
	int i = batch_idx[k];
	struct cg *cg = game_load_pic(&G.h, p->kind, i);
	if (!cg)
		return false;
	char sub[1024], name[512], path[2048];
	snprintf(sub, sizeof sub, "%s", dir);
	if (p->kind != PK_CG && entry(p, i)->save_dir[0]) {
		snprintf(sub, sizeof sub, "%s/%s", dir, entry(p, i)->save_dir);
		mkdir_p(sub);
	}
	save_name(p, i, name, sizeof name);
	snprintf(path, sizeof path, "%s/%s", sub, name);
	bool ok = save_cg_png(path, cg);
	cg_free(cg);
	return ok;
}

static void save_shown(struct plist *p)
{
	char dir[1024];
	if (!p->n || !win_pick_folder("Save the pictures shown as PNG into this folder", dir, sizeof dir))
		return;
	free(batch_idx);
	batch_idx = xmalloc(p->n * sizeof *batch_idx);
	memcpy(batch_idx, p->idx, p->n * sizeof *batch_idx);
	batch_p = p;
	char what[128];
	snprintf(what, sizeof what, "Saving %d %s as PNG", p->n, p->kind == PK_CG ? "CGs" : "pictures");
	batch_start(what, p->n, save_one, dir);
}

static void row(const char *label, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	igTextDisabled("%s", label);
	igSameLine(S(110), -1);
	igTextWrapped("%s", buf);
}

static void note(const char *text)
{
	igPushTextWrapPos(0);
	igTextDisabled("%s", text);
	igPopTextWrapPos();
}

/* The picture's format: from the file's name, else from what it decoded as. */
static const char *format_of(struct plist *p, const char *name)
{
	const char *dot = name ? strrchr(name, '.') : NULL;
	if (dot && !strchr(dot, ','))
		return dot + 1;
	if (p->cg)
		return cg_file_extension(p->cg->type);
	return "?";
}

static void details(struct plist *p)
{
	load_view(p);   /* the size and format, in the grid too */
	if (p->sel < 0) {
		igTextDisabled("Nothing picked.");
		return;
	}
	char title[256];
	title_of(p, p->sel, title, sizeof title);
	igSeparatorText(title);
	if (p->kind == PK_CG) {
		struct cg_entry *c = &G.cgs[p->sel];
		struct cg_folder *f = &G.cg_folders[c->folder];
		row("Folder", "%s", f->en);
		if (strcmp(f->en, f->jp))
			row("", "%s", f->jp);
		row("File", "%s", base_name(c->name));
		row("Format", "%s", format_of(p, c->name));
	} else {
		struct pic_entry *e = entry(p, p->sel);
		row("Folder", "%s", G.sets[p->kind].folders[e->folder]);
		if (e->group[0])
			row("Model", "%s", e->group);
		row("Where", "%s", e->path);
		row("Format", "%s", format_of(p, p->kind == PK_TEX ? e->path : NULL));
	}
	if (p->cg) {
		row("Size", "%d x %d", p->cg->metrics.w, p->cg->metrics.h);
		if (p->cg->metrics.x || p->cg->metrics.y)
			row("Offset", "%d, %d", p->cg->metrics.x, p->cg->metrics.y);
		int mask = p->kind == PK_CG ? game_mask_of(G.cgs[p->sel].no) : 0;
		if (mask && p->cg->metrics.has_alpha)
			row("See-through", "from CG %d, its mask", mask);
		else
			row("See-through", "%s", p->cg->metrics.has_alpha ? "yes" : "no");
	}
	row("In the archive", "%.1f KB", (p->kind == PK_CG ? G.cgs[p->sel].size : entry(p, p->sel)->size) / 1024.0);
	if (p->kind == PK_CG && G.rq) {
		struct cg_entry *c = &G.cgs[p->sel];
		row("Used as", "%s", c->use ? c->use : "not known");
		igSeparatorText("Replace it from a mod");
		char line[256];
		snprintf(line, sizeof line, LUA_CG_REPLACE, c->no, c->no);
		ui_lua_line("cg", line);
		note("Put your PNG at that path in your mod's folder. The Pictures guide on RQ MOD's docs site explains the fit and moving pictures.");
	} else if (p->kind == PK_TEX && G.rq) {
		char line[2048];
		if (tab3d_texture_line(entry(p, p->sel)->raw, line, sizeof line)) {
			igSeparatorText("Change it from a mod");
			ui_lua_line("tex", line);
			note("Put your PNG (a PNG per frame for a list) at that path in your mod's folder: every model that draws this texture "
			     "draws it, from the model's next load. The 3D tab tries one on a model first (Its textures).");
		} else {
			igSeparatorText("What it is");
			note("A picture in a 3D folder that no model draws by this name, so RQ MOD takes no picture for it. Save it to look at it.");
		}
	} else if (p->kind == PK_TEX) {
		igSeparatorText("What it is");
		note("A 3D model's texture, from the 3D engine's archive.");
	} else if (p->kind == PK_ALK) {
		igSeparatorText("What it is");
		note("A picture in an .alk file, which the game reads by its position in the file (the number after the name).");
	} else if (p->kind == PK_DTX) {
		igSeparatorText("What it is");
		note("A texture the dungeon view draws: the walls, floors, ceilings, stairs and doors of one dungeon, its sky and its light maps. The number is the texture's place in its row.");
	}
	igSeparatorText("Save");
	if (igButton("Save as PNG...", v2(-1, 0)) && p->cg) {
		char name[512], path[2048];
		save_name(p, p->sel, name, sizeof name);
		if (win_pick_save("Save the picture as PNG", name, "png", path, sizeof path))
			ui_status(save_cg_png(path, p->cg) ? "Saved %s" : "Could not write %s", path);
	}
	char label[64];
	snprintf(label, sizeof label, "Save the %d shown as PNG...", p->n);
	if (igButton(label, v2(-1, 0)))
		save_shown(p);
}

static void tab(struct plist *p)
{
	refilter(p);
	int pos = pos_of(p, p->sel);
	if (pos < 0 && p->n)
		pos = 0;
	if (ui_nav_keys(&pos, p->n))
		select_entry(p, p->idx[pos]);

	igBeginChild_Str("left", v2(S(380), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX, 0);
	left_panel(p);
	igEndChild();
	igSameLine(0, -1);
	float right = S(360);
	V2 av = igGetContentRegionAvail();
	igBeginChild_Str("middle", v2(av.x - right - S(8), 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
	if (igCheckbox("Grid", &p->grid))
		p->scroll = p->scroll_grid = true;
	igSameLine(0, S(16));
	if (p->grid) {
		igTextDisabled("Double-click a picture to open it");
		igBeginChild_Str("grid", v2(0, 0), ImGuiChildFlags_None, 0);
		grid(p);
		igEndChild();
	} else {
		view(p);
	}
	igEndChild();
	igSameLine(0, -1);
	igBeginChild_Str("right", v2(0, 0), ImGuiChildFlags_Borders, 0);
	details(p);
	igEndChild();
}

void tab_cgs(void) { tab(&PL[PK_CG]); }
void tab_textures(void) { tab(&PL[PK_TEX]); }
void tab_alk(void) { tab(&PL[PK_ALK]); }
void tab_dungeons(void) { tab(&PL[PK_DTX]); }

void tab_cgs_select(int no)
{
	struct plist *p = &PL[PK_CG];
	int i = game_find_cg(no);
	if (i < 0) {
		ui_status("CG %d is not in the game", no);
		return;
	}
	p->folder = -1;
	p->search[0] = 0;
	p->dirty = true;
	p->grid = false;
	select_entry(p, i);
	UI.want_tab = TAB_CGS;
}

void set_select(int kind, int i)
{
	struct plist *p = &PL[kind];
	int n = pic_count(kind);
	if (i >= 0 && i < n)
		select_entry(p, i);
	else if (n)
		select_entry(p, 0);
}

void pictures_grid(int kind, bool on)
{
	PL[kind].grid = on;
}

void cgs_select_first(int no)
{
	struct plist *p = &PL[PK_CG];
	int i = no > 0 ? game_find_cg(no) : -1;
	/* --select N for a number the game lacks: the next one it has, to show a range in the grid */
	if (i < 0 && no > 0)
		for (int k = 0; k < G.ncg; k++)
			if (G.cgs[k].no >= no) {
				i = k;
				break;
			}
	if (i < 0 && G.ncg)
		i = 0;
	if (i >= 0)
		select_entry(p, i);
}
