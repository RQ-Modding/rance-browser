/* The game folder: its ini, its archives opened read-only, and the lists the tabs show.
 *
 * A System 4 game names its archives after its .ain (the ini's CodeName), and the engine finds them
 * by that name: <code>CG.afa or the volumes <code>GA.ald to <code>GZ.ald for the CGs,
 * <code>BA.ald (or .afa) with a .bgi for the music, <code>WA.ald (or WA.afa, Sound.afa) for the
 * sounds, <code>Voice.afa for the voices, <code>AFF.afa for the Flash files. This file finds them
 * the same way (xsystem4's asset_manager.c), so any System 4 game opens; a game's profile (games.c)
 * only adds labels.
 *
 * The newer games (Rance 01, Rance 03) name their files in words with folders split by a full-width
 * slash ("_立ち絵／アイ／基本.QNT"), and Rance 03 keeps its music in Sound.afa beside the sounds, each
 * track's loop in a table of an .ex file inside the same archive (timemap_sound2.ex's SoundInfo). */
#include <ctype.h>
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
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "cJSON.h"

#include "core.h"

struct game G;

extern const char rq_ids_json[];
extern const unsigned rq_ids_json_len;
extern const char rq_pictures_tsv[];
extern const unsigned rq_pictures_tsv_len;
extern const char rq_models_tsv[];
extern const unsigned rq_models_tsv_len;

const char *const flash_folder_names[FLASH_NFOLDERS] = { "Battle", "Monster", "System", "Other" };

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

char *join(const char *dir, const char *name)
{
	size_t n = strlen(dir) + strlen(name) + 2;
	char *p = xmalloc(n);
	snprintf(p, n, "%s/%s", dir, name);
	return p;
}

const char *base_name(const char *path)
{
	const char *a = strrchr(path, '\\'), *b = strrchr(path, '/');
	const char *s = a > b ? a : b;
	return s ? s + 1 : path;
}

static char *utf(const char *sjis, size_t len)
{
	return sjis2utf(sjis, len);
}

/* The first folder separator in an archive's name: "\", "/" or the full-width "／" (U+FF0F) of the
 * newer games. *len is its length in bytes (UTF-8), 0 when there is none. */
static char *first_sep(char *s, int *len)
{
	char *a = strchr(s, '\\'), *b = strchr(s, '/'), *c = strstr(s, "\xef\xbc\x8f");
	char *best = NULL;
	*len = 0;
	if (a) { best = a; *len = 1; }
	if (b && (!best || b < best)) { best = b; *len = 1; }
	if (c && (!best || c < best)) { best = c; *len = 3; }
	return best;
}

/* After the last separator. */
static const char *last_part(const char *s)
{
	const char *out = s;
	for (const char *p = s; *p; p++) {
		if (*p == '\\' || *p == '/')
			out = p + 1;
		else if (!strncmp(p, "\xef\xbc\x8f", 3))
			out = p + 3;
	}
	return out;
}

/* A file named in words ("_立ち絵／アイ／基本.QNT"): its name after the first folder and without its
 * extension ("アイ／基本"), for a list's line. NULL for a file named by a number ("CG12302.qnt",
 * "10001.ogg"), whose list shows the number. */
static char *word_title(char *name)
{
	const char *base = last_part(name);
	const char *p = base;
	if (!strncasecmp(p, "cg", 2))
		p += 2;
	const char *digits = p;
	while (isdigit((unsigned char)*p))
		p++;
	if (p > digits && (!*p || *p == '.'))
		return NULL;
	int len;
	char *sep = first_sep(name, &len);
	char *t = xstrdup(sep && sep[len] ? sep + len : name);
	char *dot = strrchr(t, '.');
	if (dot && dot > last_part(t))
		*dot = 0;
	return t;
}

/* The first folder of a name, as a new string, or NULL when it has none. */
static char *first_folder(char *name)
{
	int len;
	char *sep = first_sep(name, &len);
	if (!sep)
		return NULL;
	char *f = xmalloc(sep - name + 1);
	memcpy(f, name, sep - name);
	f[sep - name] = 0;
	return f;
}

/* ---- the ini and the archives' names ---- */

static const char *const ini_names[] = { "System40.ini", "AliceStart.ini" };

/* A value of the ini: KEY = "value" (Shift-JIS, as the game writes it). */
static bool ini_value(const char *text, size_t len, const char *key, char *out, size_t n)
{
	size_t klen = strlen(key);
	const char *s = text, *end = text + len;
	while (s < end) {
		const char *nl = memchr(s, '\n', end - s);
		const char *le = nl ? nl : end;
		while (s < le && (*s == ' ' || *s == '\t'))
			s++;
		if ((size_t)(le - s) > klen && !strncasecmp(s, key, klen) &&
		    (s[klen] == ' ' || s[klen] == '\t' || s[klen] == '=')) {
			const char *q = memchr(s, '"', le - s);
			const char *q2 = q ? memchr(q + 1, '"', le - q - 1) : NULL;
			if (q && q2) {
				size_t m = (size_t)(q2 - q - 1);
				if (m >= n)
					m = n - 1;
				memcpy(out, q + 1, m);
				out[m] = 0;
				return true;
			}
		}
		s = nl ? nl + 1 : end;
	}
	return false;
}

static void add_file(char **slot, const char *dir, const char *name)
{
	if (!*slot)
		*slot = join(dir, name);
}

static void add_ald(char **vols, int *count, const char *dir, const char *name, int letter)
{
	int v = toupper(letter) - 'A';
	if (v < 0 || v >= 26 || vols[v])
		return;
	vols[v] = join(dir, name);
	if (*count < v + 1)
		*count = v + 1;
}

/* The .alk and .dlf files, in the game's folder or its data folder. */
static void scan_extras(struct game_files *f, const char *dir)
{
	UDIR *d = opendir_utf8(dir);
	if (!d)
		return;
	char *name;
	while ((name = readdir_utf8(d))) {
		const char *ext = file_extension(name);
		if (ext && !strcasecmp(ext, "alk") && f->nalk < MAX_ALK)
			f->alk[f->nalk++] = join(dir, name);
		else if (ext && !strcasecmp(ext, "dlf") && !f->dlf)
			f->dlf = join(dir, name);
		free(name);
	}
	closedir_utf8(d);
}

static void free_files(struct game_files *f)
{
	free(f->cg_afa);
	free(f->ba_afa);
	free(f->wa_afa);
	free(f->aff);
	free(f->red);
	free(f->bgi);
	free(f->dlf);
	free(f->voice_afa);
	for (int i = 0; i < 26; i++) {
		free(f->cg_ald[i]);
		free(f->ba_ald[i]);
		free(f->wa_ald[i]);
	}
	for (int i = 0; i < f->nalk; i++)
		free(f->alk[i]);
	memset(f, 0, sizeof *f);
}

/* Reads the ini and finds the archives. code: the .ain's name without .ain; name: GameName. */
static bool find_files(const char *dir, struct game_files *f, char *code, size_t ncode, char *name, size_t nname)
{
	memset(f, 0, sizeof *f);
	uint8_t *text = NULL;
	size_t size = 0;
	for (size_t i = 0; i < sizeof ini_names / sizeof ini_names[0] && !text; i++) {
		char *p = join(dir, ini_names[i]);
		if (file_exists(p)) {
			text = file_read(p, &size);
			snprintf(f->ini, sizeof f->ini, "%s", ini_names[i]);
		}
		free(p);
	}
	if (!text)
		return false;
	char ain[256] = "", gname[256] = "";
	ini_value((char *)text, size, "CodeName", ain, sizeof ain);
	ini_value((char *)text, size, "GameName", gname, sizeof gname);
	free(text);
	if (!ain[0])
		return false;
	char *dot = strrchr(ain, '.');
	if (dot)
		*dot = 0;
	char *u = utf(ain, 0);
	snprintf(code, ncode, "%s", u);
	free(u);
	u = utf(gname, 0);
	snprintf(name, nname, "%s", u);
	free(u);

	size_t blen = strlen(code);
	UDIR *d = opendir_utf8(dir);
	if (!d)
		return false;
	char *e, *data = NULL;
	while ((e = readdir_utf8(d))) {
		if (!strcasecmp(e, "data") && !data) {
			data = join(dir, e);   /* its own spelling: "Data" or "data" */
			free(e);
			continue;
		}
		const char *ext = file_extension(e);
		if (!ext || strncasecmp(e, code, blen)) {
			free(e);
			continue;
		}
		const char *rest = e + blen;
		if (!strcasecmp(ext, "ald") && strlen(rest) >= 6) {
			switch (toupper((unsigned char)rest[0])) {
			case 'G': add_ald(f->cg_ald, &f->ncg_ald, dir, e, rest[1]); break;
			case 'B': add_ald(f->ba_ald, &f->nba_ald, dir, e, rest[1]); break;
			case 'W': add_ald(f->wa_ald, &f->nwa_ald, dir, e, rest[1]); break;
			}
		} else if (!strcasecmp(ext, "afa")) {
			if (!strcasecmp(rest, "CG.afa"))
				add_file(&f->cg_afa, dir, e);
			else if (!strcasecmp(rest, "BA.afa"))
				add_file(&f->ba_afa, dir, e);
			else if (!strcasecmp(rest, "WA.afa"))
				add_file(&f->wa_afa, dir, e);
			else if (!strcasecmp(rest, "Sound.afa") && !f->wa_afa) {
				add_file(&f->wa_afa, dir, e);
				f->sound_afa = true;
			} else if (!strcasecmp(rest, "Voice.afa"))
				add_file(&f->voice_afa, dir, e);
			else if (!strcasecmp(rest, "AFF.afa"))
				add_file(&f->aff, dir, e);
		} else if (!strcasecmp(ext, "bgi")) {
			add_file(&f->bgi, dir, e);
		}
		free(e);
	}
	closedir_utf8(d);

	char *red = join(dir, "Data/ReignData.red");
	if (file_exists(red))
		f->red = red;
	else
		free(red);
	scan_extras(f, dir);
	if (data && is_directory(data))
		scan_extras(f, data);
	free(data);
	return f->cg_afa || f->ncg_ald;
}

/* A System 4 game's folder: an ini that names the .ain, and the CGs' archive. */
bool game_dir_ok(const char *dir)
{
	if (!dir || !*dir)
		return false;
	struct game_files f;
	char code[256], name[256];
	bool ok = find_files(dir, &f, code, sizeof code, name, sizeof name);
	free_files(&f);
	return ok;
}

/* ---- the archives ---- */

static struct afa_archive *open_afa(const char *path, bool quiet)
{
	if (!path)
		return NULL;
	int err = 0;
	struct afa_archive *ar = afa_open((char *)path, 0, &err);
	if (!ar && !quiet)
		game_error("%s: %s", base_name(path), archive_strerror(err));
	return ar;
}

static struct archive *open_ald(char **vols, int count, bool quiet)
{
	if (!count)
		return NULL;
	int err = 0;
	struct archive *ar = ald_open(vols, count, 0, &err);
	if (!ar && !quiet) {
		for (int i = 0; i < count; i++)
			if (vols[i]) {
				game_error("%s: %s", base_name(vols[i]), archive_strerror(err));
				break;
			}
	}
	return ar;
}

static struct archive *open_audio(const char *afa, char **vols, int count, bool quiet)
{
	if (count)
		return open_ald(vols, count, quiet);
	struct afa_archive *a = open_afa(afa, quiet);
	return a ? &a->ar : NULL;
}

bool handles_open(struct handles *h, const char *dir)
{
	bool quiet = h != &G.h;
	memset(h, 0, sizeof *h);
	struct game_files *f = &G.f;
	if (f->cg_afa) {
		h->cg_afa = open_afa(f->cg_afa, quiet);
		h->cg = h->cg_afa ? &h->cg_afa->ar : NULL;
	} else {
		h->cg = open_ald(f->cg_ald, f->ncg_ald, quiet);
	}
	h->aff = open_afa(f->aff, quiet);
	h->ba = open_audio(f->ba_afa, f->ba_ald, f->nba_ald, quiet);
	h->wa = open_audio(f->wa_afa, f->wa_ald, f->nwa_ald, quiet);
	if (!h->ba && f->sound_afa)
		h->ba = h->wa;   /* the music is in the sound archive: the same numbers read it */
	struct afa_archive *v = open_afa(f->voice_afa, quiet);
	h->voice = v ? &v->ar : NULL;
	if (f->red) {
		int err = 0;
		h->red = aar_open(f->red, 0, &err);
		if (!h->red && !quiet)
			game_error("Data/ReignData.red: %s", archive_strerror(err));
	}
	sets_open(h);
	return h->cg != NULL;
}

void handles_close(struct handles *h)
{
	if (h->cg) archive_free(h->cg);
	if (h->aff) archive_free(&h->aff->ar);
	if (h->red) archive_free(&h->red->ar);
	if (h->ba && h->ba != h->wa) archive_free(h->ba);
	if (h->wa) archive_free(h->wa);
	if (h->voice) archive_free(h->voice);
	sets_close(h);
	memset(h, 0, sizeof *h);
}

/* ---- CGs ---- */

static int folder_index(const char *jp, const char *label, int lo)
{
	for (int i = 0; i < G.ncg_folders; i++)
		if (!strcmp(G.cg_folders[i].jp, jp))
			return i;
	if (G.ncg_folders >= (int)(sizeof G.cg_folders / sizeof G.cg_folders[0]))
		return G.ncg_folders - 1;
	struct cg_folder *f = &G.cg_folders[G.ncg_folders];
	f->jp = xstrdup(jp);
	f->en = label ? label : f->jp;
	f->lo = lo;
	return G.ncg_folders++;
}

/* An AFA folder's label from the profile. */
static const char *folder_label(const char *jp)
{
	if (G.prof)
		for (int k = 0; k < G.prof->nfolders; k++)
			if (!strcmp(G.prof->folders[k].name, jp))
				return G.prof->folders[k].label;
	return NULL;
}

/* An ALD archive has no folders: a CG goes in the profile's range that holds its number, else in
 * its thousand ("7000-7999"). */
static int range_folder(int no)
{
	char jp[64];
	if (G.prof) {
		for (int k = 0; k < G.prof->nranges; k++) {
			if (no < G.prof->ranges[k].lo || no > G.prof->ranges[k].hi)
				continue;
			snprintf(jp, sizeof jp, "%d-%d", G.prof->ranges[k].lo, G.prof->ranges[k].hi);
			return folder_index(jp, G.prof->ranges[k].label, G.prof->ranges[k].lo);
		}
	}
	int lo = no / 1000 * 1000;
	snprintf(jp, sizeof jp, "%d-%d", lo, lo + 999);
	return folder_index(jp, NULL, lo);
}

static int cmp_cg(const void *a, const void *b)
{
	const struct cg_entry *x = a, *y = b;
	return (x->no > y->no) - (x->no < y->no);
}

static int cmp_folder(const void *a, const void *b)
{
	const struct cg_folder *x = a, *y = b;
	if (x->lo != y->lo)
		return (x->lo > y->lo) - (x->lo < y->lo);
	return strcmp(x->jp, y->jp);
}

/* Sorts the folders (AFA: by the game's names, which start with a number; ALD: by number) and
 * moves each CG to its folder's new place. */
static void sort_folders(void)
{
	struct cg_folder old[256];
	int n = G.ncg_folders;
	memcpy(old, G.cg_folders, n * sizeof old[0]);
	qsort(G.cg_folders, n, sizeof G.cg_folders[0], cmp_folder);
	int map[256];
	for (int i = 0; i < n; i++)
		for (int j = 0; j < n; j++)
			if (!strcmp(old[i].jp, G.cg_folders[j].jp))
				map[i] = j;
	for (int i = 0; i < G.ncg; i++)
		G.cgs[i].folder = map[G.cgs[i].folder];
}

static void list_cgs_afa(void)
{
	struct afa_archive *ar = G.h.cg_afa;
	G.cgs = xcalloc(ar->nr_files ? ar->nr_files : 1, sizeof *G.cgs);
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		struct afa_entry *e = &ar->files[i];
		struct cg_entry *c = &G.cgs[G.ncg++];
		c->name = utf(e->name->text, e->name->size);
		c->no = e->no + 1;
		c->index = (int)i;
		c->size = e->size;
		c->title = word_title(c->name);
		char *folder = first_folder(c->name);
		if (folder) {
			c->folder = folder_index(folder, folder_label(folder), 0);
			free(folder);
		} else {
			c->folder = folder_index("(top)", folder_label("(top)"), 0);
		}
	}
}

struct cg_list {
	int cap;
};

static void ald_cg_iter(struct archive_data *d, void *user)
{
	struct cg_list *l = user;
	if (G.ncg == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 1024;
		G.cgs = xrealloc(G.cgs, l->cap * sizeof *G.cgs);
	}
	struct cg_entry *c = &G.cgs[G.ncg++];
	memset(c, 0, sizeof *c);
	c->no = d->no + 1;
	c->index = d->no;
	c->size = (uint32_t)d->size;
	c->name = utf(d->name ? d->name : "", 0);
	c->folder = range_folder(c->no);
}

static void list_cgs(void)
{
	if (!G.h.cg)
		return;
	if (G.h.cg_afa) {
		list_cgs_afa();
	} else {
		struct cg_list l = { 0 };
		archive_for_each(G.h.cg, ald_cg_iter, &l);
	}
	sort_folders();
	for (int i = 0; i < G.ncg; i++)
		G.cg_folders[G.cgs[i].folder].count++;
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

/* A profile's mask rule (Rance VI): a CG with no see-through of its own takes it from the CG
 * `offset` higher, a PMS alpha map of the same size. The pairs come from the numbers and sizes;
 * the game itself lays a mask on a picture through DrawGraph.CopyAMap in its script. */
int game_mask_of(int no)
{
	if (!G.prof)
		return 0;
	for (int k = 0; k < G.prof->nmasks; k++) {
		if (no < G.prof->masks[k].lo || no > G.prof->masks[k].hi)
			continue;
		int m = no + G.prof->masks[k].offset;
		return game_find_cg(m) >= 0 ? m : 0;
	}
	return 0;
}

static struct cg *load_cg_raw(struct handles *h, int i);

static void apply_mask(struct handles *h, struct cg *cg, int no)
{
	int m = game_mask_of(no);
	if (!m || !cg->pixels || cg->metrics.has_alpha)
		return;
	struct cg *mask = load_cg_raw(h, game_find_cg(m));
	if (mask && mask->pixels && mask->metrics.w == cg->metrics.w && mask->metrics.h == cg->metrics.h) {
		uint8_t *px = cg->pixels, *mp = mask->pixels;
		size_t n = (size_t)cg->metrics.w * cg->metrics.h;
		for (size_t k = 0; k < n; k++)
			px[k * 4 + 3] = mp[k * 4 + 3];
		cg->metrics.has_alpha = true;
	}
	if (mask)
		cg_free(mask);
}

struct cg *game_load_cg(struct handles *h, int i)
{
	struct cg *cg = load_cg_raw(h, i);
	if (cg)
		apply_mask(h, cg, G.cgs[i].no);
	return cg;
}

static struct cg *load_cg_raw(struct handles *h, int i)
{
	if (!h->cg || i < 0 || i >= G.ncg)
		return NULL;
	if (!h->cg_afa)
		return cg_load(h->cg, G.cgs[i].index);
	struct afa_entry *e = &h->cg_afa->files[G.cgs[i].index];
	struct archive_data *d = afa_entry_to_descriptor(h->cg_afa, e);
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
	G.flash = xcalloc(ar->nr_files ? ar->nr_files : 1, sizeof *G.flash);
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
		f->base = xstrdup(base_name(f->path));
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
	a->shown_no = a->no;
	a->size = (uint32_t)d->size;
	a->file = utf(d->name ? d->name : "", 0);
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

static bool is_ex(const char *file)
{
	const char *ext = file_extension(file);
	return ext && !strcasecmp(ext, "ex");
}

/* The file's name without its extension is `stem`. */
static bool same_stem(const char *file, const char *stem)
{
	size_t n = strlen(stem);
	return !strncmp(file, stem, n) && file[n] == '.' && !strchr(file + n + 1, '.');
}

/* A string of an .ex file in UTF-8. Its padding is not always zeros: Rance 03's timemap_sound2.ex
 * ends some names in a 0x01, so control bytes at the end are cut off. */
static struct string *ex_utf(const char *s, size_t n)
{
	char *u = sjis2utf(s, n);
	size_t len = strlen(u);
	while (len && (unsigned char)u[len - 1] < 0x20)
		u[--len] = 0;
	struct string *r = make_string(u, len);
	free(u);
	return r;
}

/* An .ex file read from memory, with its strings in UTF-8; NULL when it is not one. */
static struct ex *read_ex(const uint8_t *data, size_t size)
{
	if (size < 32 || memcmp(data, "HEAD", 4) || memcmp(data + 8, "EXTF", 4) || memcmp(data + 20, "DATA", 4))
		return NULL;
	return ex_read_conv(data, size, ex_utf);
}

/* The table of loops in a sound archive (Rance 03's timemap_sound2.ex): SoundInfo, a row a file
 * by its name without the extension, its Group (1 the music, 2 the looping sounds) and its loop in
 * sample frames. The ex to free, or NULL. */
static struct ex *read_loop_table(struct archive *ar, struct audio_entry *e, int n)
{
	for (int i = 0; i < n; i++) {
		if (!is_ex(e[i].file))
			continue;
		size_t size = 0;
		uint8_t *data = game_read_audio(ar, e[i].no, &size);
		struct ex *ex = data ? read_ex(data, size) : NULL;
		free(data);
		if (ex && ex_get_table(ex, "SoundInfo")) {
			G.loop_table = xstrdup(e[i].file);
			return ex;
		}
		if (ex)
			ex_free(ex);
	}
	return NULL;
}

static int ex_int(struct ex_table *t, unsigned row, int col, int dflt)
{
	struct ex_value *v = col >= 0 ? ex_table_get(t, row, col) : NULL;
	return v && v->type == EX_INT ? v->i : dflt;
}

/* A sound archive that holds the music too (Sound.afa, when the game has no BA archive): the
 * tracks go to the music, each with its loop from the SoundInfo table, the rest stay sounds, and
 * the table's own file is neither. Without the table, the folder 音楽 ("music") is the music. */
static void name_in_words(struct audio_entry *e, int n)
{
	for (int i = 0; i < n; i++) {
		e[i].name = word_title(e[i].file);
		e[i].folder = e[i].name ? first_folder(e[i].file) : NULL;
	}
}

static void split_sound_archive(void)
{
	struct audio_entry *all = G.sounds;
	int n = G.nsounds;
	name_in_words(all, n);
	bool *music = xcalloc(n ? n : 1, sizeof *music);
	bool by_table = false;
	struct ex *ex = read_loop_table(G.h.wa, all, n);
	struct ex_table *t = ex ? ex_get_table(ex, "SoundInfo") : NULL;
	int cn = t ? ex_col_from_name(t, "Name") : -1;
	int cg = t ? ex_col_from_name(t, "Group") : -1;
	int cb = t ? ex_col_from_name(t, "LoopBeginSample") : -1;
	int ce = t ? ex_col_from_name(t, "LoopEndSample") : -1;
	for (unsigned r = 0; t && cn >= 0 && r < t->nr_rows; r++) {
		struct ex_value *vn = ex_table_get(t, r, cn);
		if (!vn || vn->type != EX_STRING)
			continue;
		for (int i = 0; i < n; i++) {
			if (!same_stem(all[i].file, vn->s->text))
				continue;
			if (ex_int(t, r, cg, 0) == 1) {
				music[i] = true;
				by_table = true;
			}
			int b = ex_int(t, r, cb, 0), e = ex_int(t, r, ce, 0);
			if (e > b && b >= 0) {
				all[i].has_loop = true;
				all[i].loop_count = 0;
				all[i].loop_start = b;
				all[i].loop_end = e;
				all[i].volume = 100;
			}
		}
	}
	if (ex)
		ex_free(ex);
	if (!by_table)
		for (int i = 0; i < n; i++)
			if (all[i].folder && !strcmp(all[i].folder, "音楽"))
				music[i] = true;
	G.music = xcalloc(n ? n : 1, sizeof *G.music);
	G.sounds = xcalloc(n ? n : 1, sizeof *G.sounds);
	G.nmusic = G.nsounds = 0;
	for (int i = 0; i < n; i++) {
		if (is_ex(all[i].file))
			continue;
		if (music[i]) {
			/* a track's folder is its second one (ＡＤＶ in 音楽／ＡＤＶ／のんき), for the tab's
			 * folders: the first is 音楽 for all of them */
			struct audio_entry *m = &G.music[G.nmusic++];
			*m = all[i];
			m->folder = m->name ? first_folder((char *)m->name) : NULL;
		} else {
			G.sounds[G.nsounds++] = all[i];
		}
	}
	free(music);
	free(all);
}

/* The voices (Rance 03's Voice.afa: 10001.ogg ...): each shown by its file's number, in a folder
 * by its thousand ("10000-10999"); one named in words in its first folder. The archive's .ex
 * (timemap_sound.ex) is not a voice. */
static void list_voices(void)
{
	struct audio_entry *all = NULL;
	int n = 0;
	list_audio(G.h.voice, &all, &n);
	G.voices = xcalloc(n ? n : 1, sizeof *G.voices);
	G.nvoices = 0;
	for (int i = 0; i < n; i++) {
		struct audio_entry *a = &all[i];
		if (is_ex(a->file))
			continue;
		a->name = word_title(a->file);
		if (a->name) {
			a->folder = first_folder(a->file);
		} else {
			const char *base = last_part(a->file);
			int no = atoi(base + (strncasecmp(base, "cg", 2) ? 0 : 2));
			if (no > 0)
				a->shown_no = no;
			char f[32];
			snprintf(f, sizeof f, "%d-%d", a->shown_no / 1000 * 1000, a->shown_no / 1000 * 1000 + 999);
			a->folder = xstrdup(f);
		}
		G.voices[G.nvoices++] = *a;
	}
	free(all);
}

/* The .bgi: each track's loop, in sample frames. Its bytes have their halves swapped. Two forms of
 * line, as the engine's audio_meta.c reads them: older games (Rance VI) write "no, count, start,
 * end, -1", newer ones (Rance Quest) "no, count, start, end, channel, volume". */
static void read_bgi(void)
{
	if (!G.f.bgi) {
		if (G.nmusic)
			game_error("no .bgi: the music has no loops");
		return;
	}
	size_t size = 0;
	uint8_t *b = file_read(G.f.bgi, &size);
	if (!b) {
		game_error("%s: missing", base_name(G.f.bgi));
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
		s = nl ? nl + 1 : end;
		int v[6];
		int got = sscanf(line, " %d, %d, %d, %d, %d, %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
		int volume;
		if (got == 6)
			volume = v[5];
		else if (got == 5 && v[4] == -1)
			volume = 100;
		else
			continue;
		for (int k = 0; k < G.nmusic; k++) {
			if (G.music[k].no == v[0]) {
				G.music[k].has_loop = true;
				G.music[k].loop_count = v[1];
				G.music[k].loop_start = v[2];
				G.music[k].loop_end = v[3];
				G.music[k].volume = volume;
			}
		}
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

/* ---- RQ MOD's names (rq.ids) and their pictures, for Rance Quest Magnum ---- */

static char *jstr(cJSON *o, const char *key)
{
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
	return xstrdup(cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}

static void read_ids(void)
{
	(void)rq_ids_json_len;
	cJSON *root = cJSON_Parse(rq_ids_json);   /* the embedded text ends with a 0 */
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

/* pictures.tsv: kind, number, CG, use; written by RQ MOD's id extract job (tools/ids/extract.lua). */
static void read_pictures(void)
{
	const char *s = rq_pictures_tsv, *end = s + rq_pictures_tsv_len;
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

/* models.tsv: kind, number, model (rq.Chara's model: 100000 plus its MonsterInfo entry); written by
 * RQ MOD's id extract job. The 3D tab draws the pictures (tab3d_model_pic). */
static void read_models(void)
{
	const char *s = rq_models_tsv, *end = s + rq_models_tsv_len;
	while (s < end) {
		const char *nl = memchr(s, '\n', end - s);
		char line[256];
		size_t len = (nl ? nl : end) - s;
		if (len >= sizeof line)
			len = sizeof line - 1;
		memcpy(line, s, len);
		line[len] = 0;
		s = nl ? nl + 1 : end;
		if (!line[0] || line[0] == '#')
			continue;
		char kind[64];
		int no, model;
		if (sscanf(line, "%63[^\t]\t%d\t%d", kind, &no, &model) < 3 || model <= 100000)
			continue;
		struct id_entry *ie = find_id(kind, no);
		if (ie && !ie->model) {
			ie->model = model;
			G.nmodels++;
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
	if (!G.prof || !G.prof->font)
		return;
	char *p = join(G.dir, G.prof->font);
	if (file_exists(p))
		snprintf(G.font, sizeof G.font, "%s", p);
	free(p);
}

bool game_open(const char *dir)
{
	snprintf(G.dir, sizeof G.dir, "%s", dir);
	if (!find_files(dir, &G.f, G.code, sizeof G.code, G.name, sizeof G.name))
		return false;
	G.prof = profile_find(G.code);
	G.tested = G.prof && G.prof->tested;
	G.rq = G.prof && G.prof->rq;
	snprintf(G.title, sizeof G.title, "%s", G.prof && G.prof->title ? G.prof->title : G.name[0] ? G.name : G.code);
	if (!handles_open(&G.h, dir))
		return false;
	list_cgs();
	list_flash();
	if (G.h.ba == G.h.wa && G.h.wa) {
		list_audio(G.h.wa, &G.sounds, &G.nsounds);
		split_sound_archive();
	} else {
		list_audio(G.h.ba, &G.music, &G.nmusic);
		list_audio(G.h.wa, &G.sounds, &G.nsounds);
		if (G.f.wa_afa)
			name_in_words(G.sounds, G.nsounds);
		read_bgi();
	}
	list_voices();
	sets_list();
	if (G.rq) {
		read_ids();
		read_pictures();
		read_models();
		link_music_names();
	}
	find_font();
	return true;
}
