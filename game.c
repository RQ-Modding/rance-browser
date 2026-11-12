/* The game folder: its archives opened read-only, and the lists the tabs show. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/afa.h"
#include "system4/ald.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "cJSON.h"

#include "rqb.h"

struct game G;

extern const char rqb_ids_json[];
extern const unsigned rqb_ids_json_len;
extern const char rqb_pictures_tsv[];
extern const unsigned rqb_pictures_tsv_len;

const char *const flash_folder_names[FLASH_NFOLDERS] = { "Battle", "Monster", "System", "Other" };
const char *const tex_folder_names[TEX_NFOLDERS] = { "Chara", "Map", "BG", "Etc", "Texture", "Other" };

/* The CG archive's folders, as the game names them, with our labels. */
static const struct { const char *jp, *en; } cg_labels[] = {
	{ "01_スプライト", "Sprites (standing pictures)" },
	{ "02_顔", "Faces" },
	{ "03_ビジュアル", "Scene CGs" },
	{ "04_ミニビジュアル", "Small scene CGs" },
	{ "05_背景", "Backgrounds" },
	{ "06_システム", "Interface" },
	{ "08_サムネイル", "Thumbnails" },
	{ "09_女の子モンスター捕獲用", "Capture pictures" },
	{ "10_コンフィグ", "Config" },
	{ "エイプリル企画", "April Fools' event" },
	{ "追加キット", "Added kits" },
	{ "２１０用", "For 2.10" },
};

void game_error(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	size_t n = strlen(G.errors);
	if (n + strlen(line) + 3 < sizeof G.errors)
		snprintf(G.errors + n, sizeof G.errors - n, "%s%s", n ? "; " : "", line);
}

static char *join(const char *dir, const char *name)
{
	size_t n = strlen(dir) + strlen(name) + 2;
	char *p = xmalloc(n);
	snprintf(p, n, "%s/%s", dir, name);
	return p;
}

bool game_dir_ok(const char *dir)
{
	if (!dir || !*dir)
		return false;
	char *p = join(dir, "RanceQuestCG.afa");
	bool ok = file_exists(p);
	free(p);
	return ok;
}

static char *utf(const char *sjis, size_t len)
{
	return sjis2utf(sjis, len);
}

static struct afa_archive *open_afa(const char *dir, const char *name, bool quiet)
{
	char *p = join(dir, name);
	int err = 0;
	struct afa_archive *ar = afa_open(p, 0, &err);
	if (!ar && !quiet)
		game_error("%s: %s", name, archive_strerror(err));
	free(p);
	return ar;
}

static struct archive *open_ald(const char *dir, const char *name, bool quiet)
{
	char *p = join(dir, name);
	int err = 0;
	struct archive *ar = NULL;
	if (file_exists(p))
		ar = ald_open(&p, 1, 0, &err);
	if (!ar && !quiet)
		game_error("%s: %s", name, err ? archive_strerror(err) : "missing");
	free(p);
	return ar;
}

bool handles_open(struct handles *h, const char *dir)
{
	bool quiet = h != &G.h;
	memset(h, 0, sizeof *h);
	h->cg = open_afa(dir, "RanceQuestCG.afa", quiet);
	h->aff = open_afa(dir, "RanceQuestAFF.afa", quiet);
	h->ba = open_ald(dir, "RanceQuestBA.ald", quiet);
	h->wa = open_ald(dir, "RanceQuestWA.ald", quiet);
	char *p = join(dir, "Data/ReignData.red");
	int err = 0;
	if (file_exists(p))
		h->red = aar_open(p, 0, &err);
	if (!h->red && !quiet)
		game_error("Data/ReignData.red: %s", err ? archive_strerror(err) : "missing");
	free(p);
	return h->cg != NULL;
}

void handles_close(struct handles *h)
{
	if (h->cg) archive_free(&h->cg->ar);
	if (h->aff) archive_free(&h->aff->ar);
	if (h->red) archive_free(&h->red->ar);
	if (h->ba) archive_free(h->ba);
	if (h->wa) archive_free(h->wa);
	memset(h, 0, sizeof *h);
}

/* ---- CGs ---- */

static int folder_index(const char *jp)
{
	for (int i = 0; i < G.ncg_folders; i++)
		if (!strcmp(G.cg_folders[i].jp, jp))
			return i;
	if (G.ncg_folders >= (int)(sizeof G.cg_folders / sizeof G.cg_folders[0]))
		return G.ncg_folders - 1;
	struct cg_folder *f = &G.cg_folders[G.ncg_folders];
	f->jp = xstrdup(jp);
	f->en = f->jp;
	for (size_t k = 0; k < sizeof cg_labels / sizeof cg_labels[0]; k++)
		if (!strcmp(cg_labels[k].jp, jp))
			f->en = cg_labels[k].en;
	return G.ncg_folders++;
}

static int cmp_cg(const void *a, const void *b)
{
	const struct cg_entry *x = a, *y = b;
	return (x->no > y->no) - (x->no < y->no);
}

static int cmp_folder_jp(const void *a, const void *b)
{
	return strcmp(((const struct cg_folder *)a)->jp, ((const struct cg_folder *)b)->jp);
}

static void list_cgs(void)
{
	struct afa_archive *ar = G.h.cg;
	if (!ar)
		return;
	G.cgs = xcalloc(ar->nr_files, sizeof *G.cgs);
	/* The folders first, sorted by the game's names (they start with a number). */
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		char *name = utf(ar->files[i].name->text, ar->files[i].name->size);
		char *sep = strchr(name, '\\');
		if (sep) {
			*sep = 0;
			folder_index(name);
		}
		free(name);
	}
	qsort(G.cg_folders, G.ncg_folders, sizeof G.cg_folders[0], cmp_folder_jp);
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		struct afa_entry *e = &ar->files[i];
		struct cg_entry *c = &G.cgs[G.ncg++];
		c->name = utf(e->name->text, e->name->size);
		c->no = e->no + 1;
		c->afa_index = (int)i;
		c->size = e->size;
		char *sep = strchr(c->name, '\\');
		if (sep) {
			*sep = 0;
			c->folder = folder_index(c->name);
			*sep = '\\';
		} else {
			c->folder = folder_index("(top)");
		}
		G.cg_folders[c->folder].count++;
	}
	qsort(G.cgs, G.ncg, sizeof *G.cgs, cmp_cg);
}

int game_find_cg(int no)
{
	int lo = 0, hi = G.ncg - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (G.cgs[mid].no == no)
			return mid;
		if (G.cgs[mid].no < no)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

struct cg *game_load_cg(struct handles *h, int i)
{
	if (!h->cg || i < 0 || i >= G.ncg)
		return NULL;
	struct afa_entry *e = &h->cg->files[G.cgs[i].afa_index];
	struct archive_data *d = afa_entry_to_descriptor(h->cg, e);
	if (!d)
		return NULL;
	struct cg *cg = NULL;
	if (archive_load_file(d))
		cg = cg_load_data(d);
	archive_free_data(d);
	return cg;
}

/* ---- Flash files ---- */

static int cmp_flash(const void *a, const void *b)
{
	const struct flash_entry *x = a, *y = b;
	if (x->folder != y->folder)
		return x->folder - y->folder;
	return strcmp(x->base, y->base);
}

static void list_flash(void)
{
	struct afa_archive *ar = G.h.aff;
	if (!ar)
		return;
	G.flash = xcalloc(ar->nr_files, sizeof *G.flash);
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		struct afa_entry *e = &ar->files[i];
		struct flash_entry *f = &G.flash[G.nflash++];
		f->path = utf(e->name->text, e->name->size);
		f->afa_index = (int)i;
		f->size = e->size;
		f->folder = FLASH_OTHER;
		for (int k = 0; k < FLASH_OTHER; k++) {
			char key[64];
			snprintf(key, sizeof key, "\\Flash\\%s\\", flash_folder_names[k]);
			if (strstr(f->path, key))
				f->folder = k;
		}
		const char *slash = strrchr(f->path, '\\');
		f->base = xstrdup(slash ? slash + 1 : f->path);
		char *dot = strrchr(f->base, '.');
		if (dot)
			*dot = 0;
	}
	qsort(G.flash, G.nflash, sizeof *G.flash, cmp_flash);
}

uint8_t *game_read_flash(struct handles *h, int i, size_t *size)
{
	if (!h->aff || i < 0 || i >= G.nflash)
		return NULL;
	struct archive_data *d = afa_entry_to_descriptor(h->aff, &h->aff->files[G.flash[i].afa_index]);
	if (!d)
		return NULL;
	uint8_t *out = NULL;
	if (archive_load_file(d)) {
		out = xmalloc(d->size);
		memcpy(out, d->data, d->size);
		*size = d->size;
	}
	archive_free_data(d);
	return out;
}

/* ---- music and sounds ---- */

struct audio_list {
	struct audio_entry *e;
	int n, cap;
};

static void audio_iter(struct archive_data *d, void *user)
{
	struct audio_list *l = user;
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 64;
		l->e = xrealloc(l->e, l->cap * sizeof *l->e);
	}
	struct audio_entry *a = &l->e[l->n++];
	memset(a, 0, sizeof *a);
	a->no = d->no + 1;
	a->size = (uint32_t)d->size;
	a->file = xstrdup(d->name ? d->name : "");
}

static int cmp_audio(const void *a, const void *b)
{
	const struct audio_entry *x = a, *y = b;
	return (x->no > y->no) - (x->no < y->no);
}

static void list_audio(struct archive *ar, struct audio_entry **out, int *n)
{
	struct audio_list l = { 0 };
	if (ar)
		archive_for_each(ar, audio_iter, &l);
	qsort(l.e, l.n, sizeof *l.e, cmp_audio);
	*out = l.e;
	*n = l.n;
}

/* The .bgi: each track's loop, in sample frames. Its bytes have their halves swapped. */
static void read_bgi(void)
{
	char *p = join(G.dir, "RanceQuestBA.bgi");
	size_t size = 0;
	uint8_t *b = file_read(p, &size);
	free(p);
	if (!b) {
		game_error("RanceQuestBA.bgi: missing");
		return;
	}
	for (size_t i = 0; i < size; i++)
		b[i] = (uint8_t)(b[i] >> 4 | (b[i] & 0xf) << 4);
	char *s = (char *)b, *end = s + size;
	while (s < end) {
		char *nl = memchr(s, '\n', end - s);
		char line[256];
		size_t len = (nl ? nl : end) - s;
		if (len >= sizeof line)
			len = sizeof line - 1;
		memcpy(line, s, len);
		line[len] = 0;
		int no, count, start, stop, channel, volume;
		if (sscanf(line, " %d, %d, %d, %d, %d, %d", &no, &count, &start, &stop, &channel, &volume) == 6) {
			for (int k = 0; k < G.nmusic; k++) {
				if (G.music[k].no == no) {
					G.music[k].has_loop = true;
					G.music[k].loop_count = count;
					G.music[k].loop_start = start;
					G.music[k].loop_end = stop;
					G.music[k].volume = volume;
				}
			}
		}
		s = nl ? nl + 1 : end;
	}
	free(b);
}

uint8_t *game_read_audio(struct archive *ar, int no, size_t *size)
{
	if (!ar)
		return NULL;
	struct archive_data *d = archive_get(ar, no - 1);
	if (!d)
		return NULL;
	uint8_t *out = xmalloc(d->size);
	memcpy(out, d->data, d->size);
	*size = d->size;
	archive_free_data(d);
	return out;
}

/* ---- 3D textures ---- */

static int cmp_tex(const void *a, const void *b)
{
	const struct tex_entry *x = a, *y = b;
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
	G.tex = xcalloc(ar->nr_files, sizeof *G.tex);
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		struct aar_entry *e = &ar->files[i];
		if (e->type == AAR_SYMLINK || !e->name || !picture_ext(e->name))
			continue;
		struct tex_entry *t = &G.tex[G.ntex++];
		t->raw = e->name;
		t->path = utf(e->name, strlen(e->name));
		t->size = e->size;
		t->folder = TEX_OTHER;
		t->model = "";
		/* Data\PolyData\<Chara|Map|BG|Etc>\<model>\file, or Data\Texture\file */
		char *parts[6] = { 0 };
		char *copy = xstrdup(t->path);
		int np = 0;
		for (char *s = copy; np < 6; ) {
			parts[np++] = s;
			char *sep = strchr(s, '\\');
			if (!sep)
				break;
			*sep = 0;
			s = sep + 1;
		}
		if (np >= 3 && !strcasecmp(parts[1], "PolyData")) {
			for (int k = 0; k < TEX_TEXTURE; k++)
				if (!strcasecmp(parts[2], tex_folder_names[k]))
					t->folder = k;
			if (np >= 5)
				t->model = xstrdup(parts[3]);
		} else if (np >= 2 && !strcasecmp(parts[1], "Texture")) {
			t->folder = TEX_TEXTURE;
		}
		free(copy);
	}
	qsort(G.tex, G.ntex, sizeof *G.tex, cmp_tex);
}

struct cg *game_load_tex(struct handles *h, int i)
{
	if (!h->red || i < 0 || i >= G.ntex)
		return NULL;
	struct archive_data *d = archive_get_by_name(&h->red->ar, G.tex[i].raw);
	if (!d)
		return NULL;
	struct cg *cg = cg_load_data(d);
	archive_free_data(d);
	if (cg && !cg->pixels) {
		cg_free(cg);
		cg = NULL;
	}
	return cg;
}

/* ---- the names (rq.ids) and their pictures ---- */

static char *jstr(cJSON *o, const char *key)
{
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
	return xstrdup(cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}

static void read_ids(void)
{
	(void)rqb_ids_json_len;
	cJSON *root = cJSON_Parse(rqb_ids_json);   /* the embedded text ends with a 0 */
	cJSON *kinds = root ? cJSON_GetObjectItemCaseSensitive(root, "kinds") : NULL;
	if (!cJSON_IsArray(kinds)) {
		game_error("the names could not be read");
		cJSON_Delete(root);
		return;
	}
	G.nkinds = cJSON_GetArraySize(kinds);
	G.kinds = xcalloc(G.nkinds, sizeof *G.kinds);
	int k = 0;
	cJSON *kind;
	cJSON_ArrayForEach(kind, kinds) {
		struct id_kind *ik = &G.kinds[k++];
		ik->kind = jstr(kind, "kind");
		ik->title = jstr(kind, "title");
		ik->what = jstr(kind, "what");
		/* The text is Markdown for the docs; the window shows it plain. */
		char *w = ik->what;
		for (char *r = ik->what; *r; r++)
			if (*r != '`')
				*w++ = *r;
		*w = 0;
		cJSON *entries = cJSON_GetObjectItemCaseSensitive(kind, "entries");
		ik->n = cJSON_GetArraySize(entries);
		ik->e = xcalloc(ik->n ? ik->n : 1, sizeof *ik->e);
		int j = 0;
		cJSON *e;
		cJSON_ArrayForEach(e, entries) {
			struct id_entry *ie = &ik->e[j++];
			cJSON *no = cJSON_GetObjectItemCaseSensitive(e, "no");
			ie->no = cJSON_IsNumber(no) ? no->valueint : 0;
			ie->name = jstr(e, "name");
			ie->key = jstr(e, "key");
			ie->detail = jstr(e, "detail");
		}
	}
	cJSON_Delete(root);
}

static struct id_entry *find_id(const char *kind, int no)
{
	for (int k = 0; k < G.nkinds; k++) {
		if (strcmp(G.kinds[k].kind, kind))
			continue;
		for (int j = 0; j < G.kinds[k].n; j++)
			if (G.kinds[k].e[j].no == no)
				return &G.kinds[k].e[j];
	}
	return NULL;
}

/* pictures.tsv: kind, number, CG, use; written by the id extract job (tools/ids/extract.lua). */
static void read_pictures(void)
{
	const char *s = rqb_pictures_tsv, *end = s + rqb_pictures_tsv_len;
	while (s < end) {
		const char *nl = memchr(s, '\n', end - s);
		char line[512];
		size_t len = (nl ? nl : end) - s;
		if (len >= sizeof line)
			len = sizeof line - 1;
		memcpy(line, s, len);
		line[len] = 0;
		s = nl ? nl + 1 : end;
		if (!line[0] || line[0] == '#')
			continue;
		char kind[64], use[256] = "";
		int no, cg;
		if (sscanf(line, "%63[^\t]\t%d\t%d\t%255[^\r\n]", kind, &no, &cg, use) < 3 || cg <= 0)
			continue;
		struct id_entry *ie = find_id(kind, no);
		if (!ie)
			continue;
		/* A name's first line is its picture; each line names its CG in the CGs tab. */
		G.npicture_lines++;
		if (!ie->cg) {
			ie->cg = cg;
			ie->use = xstrdup(use);
			G.npictures++;
		}
		int ci = game_find_cg(cg);
		if (ci < 0)
			G.npicture_missing++;
		if (ci >= 0 && !G.cgs[ci].use) {
			char label[512];
			snprintf(label, sizeof label, "%s: %s", ie->name, use);
			G.cgs[ci].use = xstrdup(label);
		}
	}
}

static void link_music_names(void)
{
	for (int k = 0; k < G.nkinds; k++) {
		if (strcmp(G.kinds[k].kind, "music"))
			continue;
		for (int j = 0; j < G.kinds[k].n; j++)
			for (int m = 0; m < G.nmusic; m++)
				if (G.music[m].no == G.kinds[k].e[j].no)
					G.music[m].name = G.kinds[k].e[j].name;
	}
}

static void find_font(void)
{
	static const char *names[] = { "Data/NotoSansRQM-Regular.ttf" };
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
		char *p = join(G.dir, names[i]);
		if (file_exists(p)) {
			snprintf(G.font, sizeof G.font, "%s", p);
			free(p);
			return;
		}
		free(p);
	}
}

bool game_open(const char *dir)
{
	snprintf(G.dir, sizeof G.dir, "%s", dir);
	if (!handles_open(&G.h, dir))
		return false;
	list_cgs();
	list_flash();
	list_audio(G.h.ba, &G.music, &G.nmusic);
	list_audio(G.h.wa, &G.sounds, &G.nsounds);
	read_bgi();
	list_tex();
	read_ids();
	read_pictures();
	link_music_names();
	find_font();
	return true;
}
