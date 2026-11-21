/* The game's 3D models: the folders of Data\PolyData in ReignData.red, each named by the game's
 * tables (MonsterInfo.txt inside the archive; MapInfo.acx, BattleBackground.acx and MapObject.acx
 * in the Data folder beside it) and, for a prop, by RQ MOD's look. Read without OpenGL. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/acx.h"
#include "system4/archive.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "rb3d.h"

const char *const rb3d_kind_folders[RB3D_NKINDS] = { "Chara", "Map", "BG", "Etc" };
const char *const rb3d_kind_titles[RB3D_NKINDS] = { "Monsters and figures", "Rooms", "Fight backgrounds", "Props" };

static char *dupn(const char *s, size_t n)
{
	char *r = xmalloc(n + 1);
	memcpy(r, s, n);
	r[n] = 0;
	return r;
}

/* n bytes of Shift-JIS as UTF-8. libsys4's sjis2utf reads up to a NUL whatever its length (the
 * length only sizes its buffer), so a piece of a longer text is copied out first. */
static char *sjis_n(const char *s, size_t n)
{
	char *c = dupn(s, n);
	char *u = sjis2utf(c, n);
	free(c);
	return u;
}

static void add_error(struct rb3d_list *l, const char *fmt, const char *what)
{
	size_t len = strlen(l->errors);
	snprintf(l->errors + len, sizeof l->errors - len, fmt, len ? "; " : "", what);
}

/* ---- MonsterInfo.txt ---- */

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t' || *s == '\r')
		s++;
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
		s[--n] = 0;
	return s;
}

/* A quoted value without its quotes, as a new string. */
static char *unquote(const char *v)
{
	size_t n = strlen(v);
	if (n >= 2 && v[0] == '"' && v[n - 1] == '"')
		return dupn(v + 1, n - 2);
	return xstrdup(v);
}

/* "<key> [N] = {": N, or -1 when the line is not that block's start. */
static int block_start(const char *line, const char *key)
{
	size_t kl = strlen(key);
	if (strncmp(line, key, kl))
		return -1;
	const char *s = line + kl;
	while (*s == ' ' || *s == '\t')
		s++;
	if (*s != '[')
		return -1;
	char *end;
	long n = strtol(s + 1, &end, 10);
	if (end == s + 1 || *end != ']' || n < 0 || n > 100000)
		return -1;
	return strchr(end, '{') ? (int)n : -1;
}

static void two_ints(const char *v, int *a, int *b)
{
	int x = 0, y = 0;
	if (sscanf(v, "%d , %d", &x, &y) == 2 || sscanf(v, "%d,%d", &x, &y) == 2) {
		*a = x;
		*b = y;
	}
}

/* The table's text (UTF-8): blocks "モンスター [001] = {" with "key = value" lines and motion blocks
 * "モーション [0] = {" inside, comments after //. */
static void parse_monsters(struct rb3d_list *l, char *text)
{
	int cap = 0, depth = 0;
	struct rb3d_entry *e = NULL;
	struct rb3d_motion *m = NULL;
	for (char *line = text; line && *line; ) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		char *s = trim(line);
		line = nl ? nl + 1 : NULL;
		if (!*s || !strncmp(s, "//", 2))
			continue;
		if (!strcmp(s, "}")) {
			if (depth == 2)
				m = NULL;
			else if (depth == 1)
				e = NULL;
			if (depth > 0)
				depth--;
			continue;
		}
		int no;
		if (depth == 0 && (no = block_start(s, "モンスター")) >= 0) {
			if (no >= cap) {
				int ncap = cap ? cap : 512;
				while (ncap <= no)
					ncap *= 2;
				l->e = xrealloc(l->e, ncap * sizeof *l->e);
				memset(l->e + cap, 0, (ncap - cap) * sizeof *l->e);
				cap = ncap;
			}
			if (no + 1 > l->ne)
				l->ne = no + 1;
			e = &l->e[no];
			if (e->name) {
				/* A number the file gives twice: the later block wins, as a table read in order. */
				free(e->motions);
				memset(e, 0, sizeof *e);
			}
			e->no = no;
			e->name = xstrdup("");
			e->scale = 1;
			e->make_shadow = true;
			depth = 1;
			continue;
		}
		if (depth == 1 && e && (no = block_start(s, "モーション")) >= 0) {
			e->motions = xrealloc(e->motions, (e->nmotions + 1) * sizeof *e->motions);
			m = &e->motions[e->nmotions++];
			memset(m, 0, sizeof *m);
			m->slot = no;
			m->name = xstrdup("");
			m->data = xstrdup("");
			depth = 2;
			continue;
		}
		if (strchr(s, '{')) {
			depth++;   /* a block the browser does not read */
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq)
			continue;
		*eq = 0;
		char *key = trim(s), *val = trim(eq + 1);
		if (depth == 1 && e) {
			if (!strcmp(key, "名前")) {
				free(e->name);
				e->name = unquote(val);
			} else if (!strcmp(key, "データ")) {
				free(e->poly);
				e->poly = unquote(val);
			} else if (!strcmp(key, "スケール")) {
				e->scale = (float)atof(val);
			} else if (!strcmp(key, "位置")) {
				float x = 0, y = 0, z = 0;
				if (sscanf(val, " ( %f , %f , %f )", &x, &y, &z) == 3) {
					e->x = x;
					e->y = y;
					e->z = z;
				}
			} else if (!strcmp(key, "影生成")) {
				e->make_shadow = atoi(val) != 0;
			} else if (!strcmp(key, "影ボーン半径")) {
				e->shadow_bone_radius = (float)atof(val);
			}
		} else if (depth == 2 && m) {
			if (!strcmp(key, "名前")) {
				free(m->name);
				m->name = unquote(val);
			} else if (!strcmp(key, "データ")) {
				free(m->data);
				m->data = unquote(val);
			} else if (!strcmp(key, "タイプ")) {
				m->loop = !strcmp(val, "ループ");
			} else if (!strcmp(key, "フレーム")) {
				two_ints(val, &m->begin, &m->end);
			} else if (!strcmp(key, "ループフレーム")) {
				two_ints(val, &m->loop_begin, &m->loop_end);
				m->has_loop_range = true;
			}
		}
	}
	for (int i = 0; i < l->ne; i++) {
		struct rb3d_entry *x = &l->e[i];
		for (int k = 0; k < x->nmotions; k++)
			if (!x->motions[k].has_loop_range) {
				x->motions[k].loop_begin = x->motions[k].begin;
				x->motions[k].loop_end = x->motions[k].end;
			}
	}
}

static void read_monsters(struct rb3d_list *l, struct aar_archive *red)
{
	struct archive_data *d = archive_get_by_name(&red->ar, "Data\\MonsterInfo.txt");
	if (!d) {
		add_error(l, "%sno %s in ReignData.red", "MonsterInfo.txt");
		return;
	}
	char *text = sjis_n((const char *)d->data, d->size);
	archive_free_data(d);
	if (!text) {
		add_error(l, "%s%s does not read", "MonsterInfo.txt");
		return;
	}
	parse_monsters(l, text);
	free(text);
}

/* ---- the .acx tables ---- */

static struct string *acx_utf8(const char *sjis, size_t len)
{
	char *u = sjis_n(sjis, len);
	struct string *s = make_string(u ? u : "", u ? strlen(u) : 0);
	free(u);
	return s;
}

static struct acx *open_acx(struct rb3d_list *l, const char *game_dir, const char *name, int columns)
{
	char path[2048];
	snprintf(path, sizeof path, "%s/Data/%s", game_dir, name);
	int err = 0;
	struct acx *a = acx_load_conv(path, &err, acx_utf8);
	if (!a) {
		add_error(l, "%s%s does not read", name);
		return NULL;
	}
	if (a->nr_columns < columns) {
		add_error(l, "%s%s has fewer columns than the game's", name);
		acx_free(a);
		return NULL;
	}
	return a;
}

/* ---- the archive's folders ---- */

static int kind_of(const char *s, size_t n)
{
	for (int k = 0; k < RB3D_NKINDS; k++)
		if (strlen(rb3d_kind_folders[k]) == n && !strncasecmp(s, rb3d_kind_folders[k], n))
			return k;
	return -1;
}

static int cmp_model(const void *a, const void *b)
{
	const struct rb3d_model *x = a, *y = b;
	if (x->kind != y->kind)
		return x->kind - y->kind;
	return strcasecmp(x->folder, y->folder);
}

/* The model of kind k and folder raw (the archive's bytes), made when it is new; path (pn bytes) is
 * the entry's name up to the folder, in the archive's own spelling. */
static struct rb3d_model *model_for(struct rb3d_list *l, int *cap, int k, const char *path, size_t pn,
                                    const char *raw, size_t n)
{
	/* The entries of a folder come together in the archive, so the last one is most often it. */
	for (int i = l->n - 1; i >= 0 && i >= l->n - 4; i--)
		if (l->m[i].kind == k && strlen(l->m[i].raw_folder) == n && !strncmp(l->m[i].raw_folder, raw, n))
			return &l->m[i];
	for (int i = 0; i < l->n; i++)
		if (l->m[i].kind == k && strlen(l->m[i].raw_folder) == n && !strncmp(l->m[i].raw_folder, raw, n))
			return &l->m[i];
	if (l->n == *cap) {
		*cap = *cap ? *cap * 2 : 256;
		l->m = xrealloc(l->m, *cap * sizeof *l->m);
	}
	struct rb3d_model *m = &l->m[l->n++];
	memset(m, 0, sizeof *m);
	m->kind = k;
	m->path = dupn(path, pn);
	m->raw_folder = dupn(raw, n);
	m->folder = sjis_n(raw, n);
	m->entry = -1;
	m->bg_no = -1;
	m->object = -1;
	return m;
}

static void add_mot(struct rb3d_model *m, const char *raw_stem, size_t n)
{
	m->mots = xrealloc(m->mots, (m->nmots + 1) * sizeof *m->mots);
	m->mots_raw = xrealloc(m->mots_raw, (m->nmots + 1) * sizeof *m->mots_raw);
	m->mots_raw[m->nmots] = dupn(raw_stem, n);
	m->mots[m->nmots] = sjis_n(raw_stem, n);
	m->nmots++;
}

bool rb3d_list_read(struct aar_archive *red, const char *game_dir, const struct rb3d_prop_look *looks, int nlooks,
                    struct rb3d_list *out)
{
	memset(out, 0, sizeof *out);
	if (!red)
		return false;
	static const char prefix[] = "Data\\PolyData\\";
	const size_t plen = sizeof prefix - 1;
	int cap = 0;
	for (uint32_t i = 0; i < red->nr_files; i++) {
		struct aar_entry *ae = &red->files[i];
		if (ae->type == AAR_SYMLINK || !ae->name || strncasecmp(ae->name, prefix, plen))
			continue;
		const char *s = ae->name + plen;
		const char *sep1 = strchr(s, '\\');
		const char *sep2 = sep1 ? strchr(sep1 + 1, '\\') : NULL;
		int k = sep1 ? kind_of(s, sep1 - s) : -1;
		if (k < 0 || !sep2 || strchr(sep2 + 1, '\\')) {
			out->odd++;
			continue;
		}
		const char *folder = sep1 + 1, *file = sep2 + 1;
		size_t fl = sep2 - folder;
		struct rb3d_model *m = model_for(out, &cap, k, ae->name, sep2 - ae->name, folder, fl);
		const char *dot = strrchr(file, '.');
		if (dot && !strcasecmp(dot, ".POL")) {
			if ((size_t)(dot - file) == fl && !strncasecmp(file, folder, fl)) {
				m->has_pol = true;
				m->pol_size = ae->size;
			} else {
				m->other_pols++;
			}
		} else if (dot && !strcasecmp(dot, ".MOT")) {
			add_mot(m, file, dot - file);
			out->nmot++;
		} else if (dot && !strcasecmp(dot, ".txa")) {
			m->ntxa++;
			out->ntxa++;
		} else if (dot && !strcasecmp(dot, ".opr")) {
			out->nopr++;
		} else if (dot && !strcasecmp(dot, ".amt")) {
			out->namt++;
		} else if (dot && (!strcasecmp(dot, ".png") || !strcasecmp(dot, ".bmp") || !strcasecmp(dot, ".tga"))) {
			m->ntex++;
		} else {
			m->odd++;
			out->odd++;
		}
	}
	/* Only a folder with its model (folder\folder.POL) is a model; the others are counted. */
	int keep = 0;
	for (int i = 0; i < out->n; i++) {
		struct rb3d_model *m = &out->m[i];
		if (!m->has_pol) {
			out->no_pol++;
			for (int j = 0; j < m->nmots; j++) {
				free(m->mots[j]);
				free(m->mots_raw[j]);
			}
			free(m->mots);
			free(m->mots_raw);
			free(m->folder);
			free(m->path);
			free(m->raw_folder);
			continue;
		}
		out->m[keep++] = *m;
	}
	out->n = keep;
	if (!out->n)
		return false;
	for (int i = 0; i < out->n; i++) {
		struct rb3d_model *m = &out->m[i];
		/* the MOTs sorted, both spellings together */
		for (int a = 1; a < m->nmots; a++)
			for (int b = a; b > 0 && strcasecmp(m->mots[b - 1], m->mots[b]) > 0; b--) {
				char *t = m->mots[b]; m->mots[b] = m->mots[b - 1]; m->mots[b - 1] = t;
				t = m->mots_raw[b]; m->mots_raw[b] = m->mots_raw[b - 1]; m->mots_raw[b - 1] = t;
			}
		m->label = xstrdup("");
		m->en = xstrdup("");
		out->counts[m->kind]++;
	}
	qsort(out->m, out->n, sizeof *out->m, cmp_model);

	/* MonsterInfo: the monsters drawn by each Chara model, and the map's props (201 to 214: the
	 * chest, the warp...), whose folders are in Etc */
	read_monsters(out, red);
	for (int no = 0; no < out->ne; no++) {
		struct rb3d_entry *e = &out->e[no];
		if (!e->poly)
			continue;
		int i = rb3d_list_find(out, RB3D_CHARA, e->poly);
		if (i < 0)
			i = rb3d_list_find(out, RB3D_ETC, e->poly);
		if (i < 0) {
			out->entries_without_model++;
			continue;
		}
		struct rb3d_model *m = &out->m[i];
		if (m->entry < 0) {
			m->entry = no;
			free(m->label);
			m->label = xstrdup(e->name ? e->name : "");
		}
		m->nentries++;
	}

	/* MapInfo.acx: no, name, x, y, dir, name2 (the room's Japanese name) */
	struct acx *a = open_acx(out, game_dir, "MapInfo.acx", 6);
	if (a) {
		for (int r = 0; r < a->nr_lines; r++) {
			if (a->column_types[1] != ACX_STRING || a->column_types[5] != ACX_STRING)
				break;
			int i = rb3d_list_find(out, RB3D_MAP, acx_get_string(a, r, 1)->text);
			if (i >= 0 && !out->m[i].label[0]) {
				free(out->m[i].label);
				out->m[i].label = xstrdup(acx_get_string(a, r, 5)->text);
			}
		}
		acx_free(a);
	}
	/* BattleBackground.acx: no, name, r, g, b, a, filter, comment */
	a = open_acx(out, game_dir, "BattleBackground.acx", 8);
	if (a) {
		for (int r = 0; r < a->nr_lines; r++) {
			if (a->column_types[1] != ACX_STRING || a->column_types[7] != ACX_STRING)
				break;
			int i = rb3d_list_find(out, RB3D_BG, acx_get_string(a, r, 1)->text);
			if (i >= 0 && out->m[i].bg_no < 0) {
				out->m[i].bg_no = acx_get_int(a, r, 0);
				free(out->m[i].label);
				out->m[i].label = xstrdup(acx_get_string(a, r, 7)->text);
			}
		}
		acx_free(a);
	}
	/* MapObject.acx: no, name, a flag, then the prop's motion: state, begin, end, loop begin, loop
	 * end, fps (read by the game, never given to the motion: 30 frames a second) */
	a = open_acx(out, game_dir, "MapObject.acx", 9);
	if (a) {
		for (int r = 0; r < a->nr_lines; r++) {
			if (a->column_types[1] != ACX_STRING)
				break;
			char *name = xstrdup(acx_get_string(a, r, 1)->text);
			char *t = trim(name);
			int i = rb3d_list_find(out, RB3D_ETC, t);
			if (i >= 0 && out->m[i].object < 0) {
				struct rb3d_model *m = &out->m[i];
				m->object = acx_get_int(a, r, 0);
				m->obj_state = acx_get_int(a, r, 3);
				m->obj_begin = acx_get_int(a, r, 4);
				m->obj_end = acx_get_int(a, r, 5);
				m->obj_loop_begin = acx_get_int(a, r, 6);
				m->obj_loop_end = acx_get_int(a, r, 7);
				for (int k = 0; k < nlooks; k++)
					if (looks[k].object == m->object && !m->look)
						m->look = xstrdup(looks[k].name);
			}
			free(name);
		}
		acx_free(a);
	}
	return true;
}

int rb3d_list_find(const struct rb3d_list *l, int kind, const char *folder)
{
	if (!folder)
		return -1;
	for (int i = 0; i < l->n; i++)
		if ((kind < 0 || l->m[i].kind == kind) && !strcasecmp(l->m[i].folder, folder))
			return i;
	return -1;
}

const struct rb3d_entry *rb3d_list_entry(const struct rb3d_list *l, int model)
{
	int no = model >= 100000 ? model - 100000 : model;
	return no >= 0 && no < l->ne && l->e[no].poly ? &l->e[no] : NULL;
}

int rb3d_model_mot(const struct rb3d_model *m, const char *name)
{
	if (!name)
		return -1;
	for (int i = 0; i < m->nmots; i++)
		if (!strcasecmp(m->mots[i], name))
			return i;
	return -1;
}

/* A monster appears with its idle through CMonster::EndMotionLoading: its state from MonsterInfo's
 * タイプ (ループ the engine's LOOP; the engine reads no other type, so anything else is its STOP),
 * on its loop's first frame when it loops, else on its first. Every other motion comes through
 * CMonster::BeginMotionBlend: once (the engine's NOLOOP) for every motion but 0, on the loop's
 * first frame. */
void rb3d_fight_play(const struct rb3d_motion *mo, struct rb3d_play *out)
{
	out->begin = mo->begin;
	out->end = mo->end;
	out->loop_begin = mo->loop_begin;
	out->loop_end = mo->loop_end;
	if (mo->slot != 0) {
		out->loop = false;
		out->start = mo->loop_begin;
	} else if (mo->loop) {
		out->loop = true;
		out->start = mo->loop_begin;
	} else {
		/* the engine's STOP: still on its first frame */
		out->loop = false;
		out->start = out->end = mo->begin;
	}
}

/* T_MapObject sets a prop's motion with MapObject.acx's state on its first frame; 置物_戻し puts
 * it back the same way. */
void rb3d_map_play(const struct rb3d_model *m, struct rb3d_play *out)
{
	out->begin = out->start = m->obj_begin;
	out->end = m->obj_end;
	out->loop_begin = m->obj_loop_begin;
	out->loop_end = m->obj_loop_end;
	out->loop = m->obj_state == 2;
	if (m->obj_state != 1 && m->obj_state != 2)
		out->end = m->obj_begin;   /* the engine's STOP: still on its first frame */
}

void rb3d_play_frames(const struct rb3d_play *p, int *first, int *last)
{
	*first = p->start < p->begin ? p->begin : p->start;
	*last = p->loop ? p->loop_end : p->end;
	if (*last < *first)
		*last = *first;
}
