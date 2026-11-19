/* The sets of pictures beside the CGs, each with a tab of its own when the game has it:
 *
 *   3D textures       Data\ReignData.red (Rance Quest): the PNG, BMP and TGA files of its models
 *   Other pictures    .alk files (Rance VI's event.alk, the AliceSoft logo): pictures by position
 *   Dungeon textures  a .dlf file (Rance VI's dungeondata.dlf): each dungeon's DTEX part, the walls,
 *                     floors, ceilings, stairs, doors, sky and light maps DrawDungeon draws
 *
 * The DTEX layout is the one xsystem4 reads (src/dungeon/dtx.c, by kichikuou): "DTEX", a version, a
 * number of rows and of columns, then each row's count and each texture's size and bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/alk.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/utfsjis.h"

#include "core.h"

const char *const set_titles[PK_NKINDS] = { "CGs", "3D textures", "Other pictures", "Dungeon textures" };
const char *const tex_folder_names[TEX_NFOLDERS] = { "Chara", "Map", "BG", "Etc", "Texture", "Other" };

#define DLF_SLOTS 300   /* (map, textures, TES) for 100 dungeons */

void sets_open(struct handles *h)
{
	bool quiet = h != &G.h;
	for (int i = 0; i < G.f.nalk; i++) {
		int err = 0;
		h->alk[i] = alk_open(G.f.alk[i], 0, &err);
		if (!h->alk[i] && !quiet)
			game_error("%s: %s", base_name(G.f.alk[i]), archive_strerror(err));
	}
	if (G.f.dlf) {
		h->dlf = file_open_utf8(G.f.dlf, "rb");
		if (!h->dlf && !quiet)
			game_error("%s: cannot be read", base_name(G.f.dlf));
	}
}

void sets_close(struct handles *h)
{
	for (int i = 0; i < MAX_ALK; i++)
		if (h->alk[i])
			archive_free(&h->alk[i]->ar);
	if (h->dlf)
		fclose(h->dlf);
}

int pic_count(int kind)
{
	if (kind == PK_CG)
		return G.ncg;
	return kind > PK_CG && kind < PK_NKINDS ? G.sets[kind].n : 0;
}

static struct pic_entry *add(struct pic_set *s, int *cap)
{
	if (s->n == *cap) {
		*cap = *cap ? *cap * 2 : 256;
		s->e = xrealloc(s->e, *cap * sizeof *s->e);
	}
	struct pic_entry *e = &s->e[s->n++];
	memset(e, 0, sizeof *e);
	e->group = "";
	e->save_dir = "";
	return e;
}

static int set_folder(struct pic_set *s, const char *name)
{
	for (int i = 0; i < s->nfolders; i++)
		if (!strcmp(s->folders[i], name))
			return i;
	if (s->nfolders == SET_MAX_FOLDERS)
		return s->nfolders - 1;
	s->folders[s->nfolders] = xstrdup(name);
	return s->nfolders++;
}

static void count_folders(struct pic_set *s)
{
	memset(s->counts, 0, sizeof s->counts);
	for (int i = 0; i < s->n; i++)
		s->counts[s->e[i].folder]++;
}

static char *strip_ext(const char *name)
{
	char *s = xstrdup(name);
	char *dot = strrchr(s, '.');
	if (dot)
		*dot = 0;
	return s;
}

/* ---- 3D textures (ReignData.red) ---- */

static int cmp_tex(const void *a, const void *b)
{
	const struct pic_entry *x = a, *y = b;
	if (x->folder != y->folder)
		return x->folder - y->folder;
	return strcmp(x->path, y->path);
}

static bool picture_ext(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot && (!strcasecmp(dot, ".png") || !strcasecmp(dot, ".bmp") || !strcasecmp(dot, ".tga"));
}

static void list_tex(void)
{
	struct aar_archive *ar = G.h.red;
	if (!ar)
		return;
	struct pic_set *s = &G.sets[PK_TEX];
	for (int k = 0; k < TEX_NFOLDERS; k++)
		set_folder(s, tex_folder_names[k]);
	int cap = 0;
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		struct aar_entry *ae = &ar->files[i];
		if (ae->type == AAR_SYMLINK || !ae->name || !picture_ext(ae->name))
			continue;
		struct pic_entry *t = add(s, &cap);
		t->raw = ae->name;
		t->path = sjis2utf(ae->name, strlen(ae->name));
		t->size = ae->size;
		t->folder = TEX_OTHER;
		t->title = xstrdup(base_name(t->path));
		t->save = strip_ext(t->title);
		/* Data\PolyData\<Chara|Map|BG|Etc>\<model>\file, or Data\Texture\file */
		char *parts[6] = { 0 };
		char *copy = xstrdup(t->path);
		int np = 0;
		for (char *p = copy; np < 6; ) {
			parts[np++] = p;
			char *sep = strchr(p, '\\');
			if (!sep)
				break;
			*sep = 0;
			p = sep + 1;
		}
		if (np >= 3 && !strcasecmp(parts[1], "PolyData")) {
			for (int k = 0; k < TEX_TEXTURE; k++)
				if (!strcasecmp(parts[2], tex_folder_names[k]))
					t->folder = k;
			if (np >= 5)
				t->group = xstrdup(parts[3]);
		} else if (np >= 2 && !strcasecmp(parts[1], "Texture")) {
			t->folder = TEX_TEXTURE;
		}
		free(copy);
		char dir[512];
		snprintf(dir, sizeof dir, "%s%s%s", tex_folder_names[t->folder], t->group[0] ? "/" : "", t->group);
		t->save_dir = xstrdup(dir);
	}
	qsort(s->e, s->n, sizeof *s->e, cmp_tex);
	count_folders(s);
}

/* ---- other pictures (.alk) ---- */

static void list_alk(void)
{
	struct pic_set *s = &G.sets[PK_ALK];
	int cap = 0;
	for (int a = 0; a < G.f.nalk; a++) {
		struct alk_archive *ar = G.h.alk[a];
		if (!ar || !ar->f)
			continue;
		const char *file = base_name(G.f.alk[a]);
		char *stem = strip_ext(file);
		int folder = -1;
		for (int i = 0; i < ar->nr_files; i++) {
			if (!ar->files[i].size)
				continue;
			uint8_t head[32] = { 0 };
			if (fseek(ar->f, ar->files[i].off, SEEK_SET) || fread(head, 1, sizeof head, ar->f) < 16)
				continue;
			if (cg_check_format(head) == ALCG_UNKNOWN)
				continue;   /* a sound (AliceLogoWave.alk) or other data */
			if (folder < 0)
				folder = set_folder(s, file);
			struct pic_entry *e = add(s, &cap);
			char buf[512];
			snprintf(buf, sizeof buf, "%s %d", stem, i);
			e->title = xstrdup(buf);
			const char *rel = G.f.alk[a] + strlen(G.dir) + 1;
			snprintf(buf, sizeof buf, "%s, entry %d", rel, i);
			e->path = xstrdup(buf);
			snprintf(buf, sizeof buf, "%s_%03d", stem, i);
			e->save = xstrdup(buf);
			e->save_dir = stem;
			e->folder = folder;
			e->size = ar->files[i].size;
			e->src = a;
			e->index = i;
		}
	}
	count_folders(s);
}

/* ---- dungeon textures (.dlf) ---- */

/* A row's name, by the DTEX version, as the engine's dtx_create_cg places them. */
static const char *dtx_row_name(uint32_t version, int row)
{
	static const char *const names[] = { "wall", "floor", "ceiling", "stairs", "door", "sky", "light map" };
	if (row >= 0 && row <= 5)
		return names[row];
	if (version == 0 && row == 6)
		return names[6];
	if (version == 1 && row == 7)
		return names[6];
	return NULL;
}

static bool read_u32(FILE *f, uint32_t *out)
{
	uint8_t b[4];
	if (fread(b, 1, 4, f) != 4)
		return false;
	*out = LittleEndian_getDW(b, 0);
	return true;
}

static void list_dtx(void)
{
	FILE *f = G.h.dlf;
	if (!f)
		return;
	uint8_t head[8 + DLF_SLOTS * 8];
	if (fseek(f, 0, SEEK_SET) || fread(head, 1, sizeof head, f) != sizeof head || memcmp(head, "DLF", 4)) {
		game_error("%s: not a DLF file", base_name(G.f.dlf));
		return;
	}
	struct pic_set *s = &G.sets[PK_DTX];
	int cap = 0;
	const char *rel = G.f.dlf + strlen(G.dir) + 1;
	for (int slot = 1; slot < DLF_SLOTS; slot += 3) {
		uint32_t off = LittleEndian_getDW(head, 8 + slot * 8);
		uint32_t size = LittleEndian_getDW(head, 8 + slot * 8 + 4);
		if (!size)
			continue;
		uint8_t dh[16];
		if (fseek(f, off, SEEK_SET) || fread(dh, 1, sizeof dh, f) != sizeof dh || memcmp(dh, "DTEX", 4))
			continue;
		uint32_t version = LittleEndian_getDW(dh, 4);
		uint32_t rows = LittleEndian_getDW(dh, 8), cols = LittleEndian_getDW(dh, 12);
		if (rows > 64 || cols > 4096)
			continue;
		int dungeon = slot / 3;
		char folder_name[64];
		snprintf(folder_name, sizeof folder_name, "Dungeon %d", dungeon);
		int folder = set_folder(s, folder_name);
		G.ndungeons++;
		bool bad = false;
		for (uint32_t r = 0; r < rows && !bad; r++) {
			uint32_t items;
			if (!read_u32(f, &items) || items > 4096) {
				bad = true;
				break;
			}
			for (uint32_t c = 0; c < items; c++) {
				uint32_t tsize;
				if (!read_u32(f, &tsize)) {
					bad = true;
					break;
				}
				long at = ftell(f);
				if (tsize) {
					struct pic_entry *e = add(s, &cap);
					const char *row = dtx_row_name(version, (int)r);
					char rowbuf[32], buf[512];
					if (!row) {
						snprintf(rowbuf, sizeof rowbuf, "row %u", r);
						row = rowbuf;
					}
					snprintf(buf, sizeof buf, "%s %u", row, c);
					e->title = xstrdup(buf);
					snprintf(buf, sizeof buf, "%s, dungeon %d, %s %u", rel, dungeon, row, c);
					e->path = xstrdup(buf);
					snprintf(buf, sizeof buf, "dungeon%02d_%s_%02u", dungeon, row, c);
					for (char *p = buf; *p; p++)
						if (*p == ' ')
							*p = '_';
					e->save = xstrdup(buf);
					snprintf(buf, sizeof buf, "dungeon%02d", dungeon);
					e->save_dir = xstrdup(buf);
					e->folder = folder;
					e->size = tsize;
					e->src = slot;
					e->off = at;
				}
				if (fseek(f, at + tsize, SEEK_SET)) {
					bad = true;
					break;
				}
			}
		}
		if (bad)
			game_error("%s: dungeon %d's textures end early", base_name(G.f.dlf), dungeon);
	}
	count_folders(s);
}

void sets_list(void)
{
	list_tex();
	list_alk();
	list_dtx();
}

/* ---- loading one picture ---- */

static struct cg *checked(struct cg *cg)
{
	if (cg && !cg->pixels) {
		cg_free(cg);
		cg = NULL;
	}
	return cg;
}

struct cg *game_load_pic(struct handles *h, int kind, int i)
{
	if (kind == PK_CG)
		return game_load_cg(h, i);
	if (kind <= PK_CG || kind >= PK_NKINDS || i < 0 || i >= G.sets[kind].n)
		return NULL;
	struct pic_entry *e = &G.sets[kind].e[i];
	struct cg *cg = NULL;
	if (kind == PK_TEX) {
		if (!h->red)
			return NULL;
		struct archive_data *d = archive_get_by_name(&h->red->ar, e->raw);
		if (!d)
			return NULL;
		cg = cg_load_data(d);
		archive_free_data(d);
	} else if (kind == PK_ALK) {
		struct alk_archive *ar = h->alk[e->src];
		struct archive_data *d = ar ? archive_get(&ar->ar, e->index) : NULL;
		if (!d)
			return NULL;
		cg = cg_load_data(d);
		archive_free_data(d);
	} else if (kind == PK_DTX) {
		FILE *f = h->dlf;
		if (!f || fseek(f, (long)e->off, SEEK_SET))
			return NULL;
		uint8_t *buf = xmalloc(e->size);
		if (fread(buf, 1, e->size, f) == e->size)
			cg = cg_load_buffer(buf, e->size);
		free(buf);
	}
	return checked(cg);
}
