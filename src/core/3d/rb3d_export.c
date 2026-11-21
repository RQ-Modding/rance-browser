/* The export: the model a view shows (rb3d.c) written as a glTF 2.0 binary file (.glb) that Blender
 * and other 3D programs open. It comes from the copy's own load (the bones in the engine's order and
 * their inverse binds, the meshes and materials as the engine splits and judges them) and from the
 * POL read again with its .opr, for what the engine keeps only in GL buffers (points, UVs, normals,
 * colours, triangles, weights). The pictures are decoded again and kept in the file as PNG. What the
 * game draws and glTF cannot say (the light map, the outline, a UV scroll, the frames the game plays)
 * goes into extras named rq_*.
 *
 * glTF's axes are the engine's (y up; the POL reader turned the game's left-handed space around),
 * its units metres, its front faces counter-clockwise, as the engine's are before it turns its
 * picture upside down (renderer.c's glFrontFace(GL_CW)).
 *
 * Beside the file, a report for a checker that opens it in Blender: the counts, what was left out and
 * why, and numbers from the engine's own code, so a fault of the export's shows there: each bone's
 * head and some points at rest and posed by the engine (RE_build_model, the points' weights read back
 * from the engine's vertex buffers) on four frames of each animation, UVs, each picture's CRC, each
 * material's alpha. */
#include <png.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <cglm/cglm.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/utfsjis.h"

#include "3d_internal.h"
#include "reign.h"
#include "../cJSON.h"
#include "rb3d.h"
#include "rb3d_private.h"

#define FPS 30.0
#define NAME_BYTES 63          /* Blender's longest name, in bytes */
#define TOLERANCE 0.001        /* metres: what the checker lets a sample be off by */

enum { C_FLOAT = 5126, C_USHORT = 5123, C_UINT = 5125 };
enum { T_ARRAY = 34962, T_INDEX = 34963 };
enum { MODE_OPAQUE, MODE_MASK, MODE_BLEND };
static const char *const mode_names[] = { "OPAQUE", "MASK", "BLEND" };
/* How a picture is written: as it is; its alpha times its alpha map's red, or that red alone (the
 * shader's ALPHA_MAP_BLEND and ALPHA_MAP_TEST); a normal map with its green turned over (the
 * engine's bitangent points down the picture, glTF's up). */
enum { BAKE_PLAIN, BAKE_TIMES_MAP, BAKE_FROM_MAP, BAKE_GREEN };

/* ---- bytes ---- */

struct bytes {
	uint8_t *p;
	size_t n, cap;
};

static void bytes_add(struct bytes *b, const void *data, size_t n)
{
	if (b->n + n > b->cap) {
		size_t cap = b->cap ? b->cap : 1 << 16;
		while (cap < b->n + n)
			cap *= 2;
		b->p = xrealloc(b->p, cap);
		b->cap = cap;
	}
	if (n)
		memcpy(b->p + b->n, data, n);
	b->n += n;
}

static void bytes_pad(struct bytes *b, uint8_t fill)
{
	while (b->n % 4)
		bytes_add(b, &fill, 1);
}

/* ---- names ---- */

struct names {
	char **v;
	int n;
};

static bool names_has(const struct names *ns, const char *s)
{
	for (int i = 0; i < ns->n; i++)
		if (!strcmp(ns->v[i], s))
			return true;
	return false;
}

static void names_free(struct names *ns)
{
	for (int i = 0; i < ns->n; i++)
		free(ns->v[i]);
	free(ns->v);
	ns->v = NULL;
	ns->n = 0;
}

/* How many of s's bytes fit in max, cut on a UTF-8 character's edge. */
static size_t utf8_fit(const char *s, size_t max)
{
	size_t n = strlen(s);
	if (n <= max)
		return n;
	n = max;
	while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
		n--;
	return n;
}

/* want (UTF-8) as a name not yet among ns: without control characters (a few of the game's bone
 * names start with a line break) and the spaces around it, at most NAME_BYTES bytes with its ending
 * end (".png" or ""), cut on a character's edge, then " #2", " #3"... Kept in ns; *changed (may be
 * NULL) says whether it is not want plus end. */
static const char *name_take(struct names *ns, const char *want, const char *end, bool *changed)
{
	const char *given = want ? want : "";
	char *clean = xmalloc(strlen(given) + 1);
	size_t c = 0;
	for (const unsigned char *p = (const unsigned char *)given; *p; p++)
		if (*p >= 0x20 && *p != 0x7F && !(*p == ' ' && c == 0))
			clean[c++] = (char)*p;
	while (c > 0 && clean[c - 1] == ' ')
		c--;
	clean[c] = 0;
	const char *base = clean[0] ? clean : "unnamed";
	char buf[NAME_BYTES + 1];
	for (int k = 1; ; k++) {
		char tail[32];
		if (k == 1)
			snprintf(tail, sizeof tail, "%s", end);
		else
			snprintf(tail, sizeof tail, " #%d%s", k, end);
		size_t n = utf8_fit(base, NAME_BYTES - strlen(tail));
		snprintf(buf, sizeof buf, "%.*s%s", (int)n, base, tail);
		if (!names_has(ns, buf)) {
			if (changed)
				*changed = k > 1 || n < strlen(base) || strcmp(base, given) != 0;
			free(clean);
			ns->v = xrealloc(ns->v, (ns->n + 1) * sizeof *ns->v);
			ns->v[ns->n++] = xstrdup(buf);
			return ns->v[ns->n - 1];
		}
	}
}

/* A text of the game's files (Shift-JIS) as UTF-8; free it. */
static char *utf(const char *sjis)
{
	return sjis && sjis[0] ? sjis2utf(sjis, 0) : xstrdup("");
}

/* ---- numbers ---- */

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
	static uint32_t table[256];
	static bool made;
	if (!made) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		made = true;
	}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++)
		c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

/* A number of the game's (a float) for the JSON: the shortest decimal that reads back as the same
 * float (0.4, not 0.4000000059604645), never NaN or infinite, which cJSON writes as null. */
static double fin(double x)
{
	if (!isfinite(x))
		return 0;
	float f = (float)x;
	if ((double)f != x)
		return x;   /* not a float's value: as it is */
	char buf[32];
	for (int digits = 6; digits <= 9; digits++) {
		snprintf(buf, sizeof buf, "%.*g", digits, f);
		double d = strtod(buf, NULL);
		if ((float)d == f)
			return d;
	}
	return x;
}

/* A sample's number, to the micrometre. */
static double um(double x)
{
	return isfinite(x) ? round(x * 1e6) / 1e6 : 0;
}

static cJSON *nums(const float *v, int n)
{
	double d[16];
	for (int i = 0; i < n && i < 16; i++)
		d[i] = fin(v[i]);
	return cJSON_CreateDoubleArray(d, n < 16 ? n : 16);
}

/* An accessor's min or max: each float's own value, as a reader compares them with the data. */
static cJSON *nums_exact(const float *v, int n)
{
	double d[16];
	for (int i = 0; i < n && i < 16; i++)
		d[i] = isfinite(v[i]) ? v[i] : 0;
	return cJSON_CreateDoubleArray(d, n < 16 ? n : 16);
}

static cJSON *sample_vec(const float *v, int n)
{
	double d[4];
	for (int i = 0; i < n && i < 4; i++)
		d[i] = um(v[i]);
	return cJSON_CreateDoubleArray(d, n < 4 ? n : 4);
}

/* -0 as 0, so equal points weld. */
static float tidy(float f)
{
	return f == 0.f ? 0.f : f;
}

/* ---- the file's JSON and its binary chunk ---- */

struct glb {
	cJSON *doc;
	cJSON *views, *accessors, *images, *textures, *materials, *meshes, *nodes, *skins, *anims;
	int nviews, naccessors, nimages, ntextures, nmaterials, nmeshes, nnodes, nanims;
	struct bytes bin;
};

static int glb_view(struct glb *g, const void *data, size_t n, int target)
{
	bytes_pad(&g->bin, 0);
	cJSON *v = cJSON_CreateObject();
	cJSON_AddNumberToObject(v, "buffer", 0);
	cJSON_AddNumberToObject(v, "byteOffset", (double)g->bin.n);
	cJSON_AddNumberToObject(v, "byteLength", (double)n);
	if (target)
		cJSON_AddNumberToObject(v, "target", target);
	bytes_add(&g->bin, data, n);
	cJSON_AddItemToArray(g->views, v);
	return g->nviews++;
}

/* An accessor over a buffer view of its own: count elements of type ("SCALAR", "VEC3"...), each of
 * ncomp components of ctype; min and max (may be NULL), ncomp numbers each. */
static int glb_accessor(struct glb *g, const void *data, int count, int ctype, const char *type, int ncomp,
                        int target, const float *min, const float *max)
{
	size_t csize = ctype == C_USHORT ? 2 : 4;
	int view = glb_view(g, data, (size_t)count * ncomp * csize, target);
	cJSON *a = cJSON_CreateObject();
	cJSON_AddNumberToObject(a, "bufferView", view);
	cJSON_AddNumberToObject(a, "componentType", ctype);
	cJSON_AddNumberToObject(a, "count", count);
	cJSON_AddStringToObject(a, "type", type);
	if (min && max) {
		cJSON_AddItemToObject(a, "min", nums_exact(min, ncomp));
		cJSON_AddItemToObject(a, "max", nums_exact(max, ncomp));
	}
	cJSON_AddItemToArray(g->accessors, a);
	return g->naccessors++;
}

/* ---- PNG into memory ---- */

static void png_put(png_structp png, png_bytep data, png_size_t n)
{
	bytes_add(png_get_io_ptr(png), data, n);
}

static void png_flush_none(png_structp png)
{
	(void)png;
}

/* RGBA pixels, top row first, as a PNG added to out. */
static bool png_encode(const uint8_t *rgba, int w, int h, struct bytes *out)
{
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!info) {
		png_destroy_write_struct(&png, NULL);
		return false;
	}
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		return false;
	}
	png_set_write_fn(png, out, png_put, png_flush_none);
	png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
	             PNG_FILTER_TYPE_BASE);
	png_write_info(png, info);
	for (int y = 0; y < h; y++)
		png_write_row(png, (png_bytep)rgba + (size_t)y * w * 4);
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	return true;
}

/* ---- the export's state ---- */

struct vkey {                     /* a point: what welds two triangle corners into one */
	float pos[3], nrm[3], uv[2], luv[2], col[4];
	uint16_t joints[4];
	float weights[4];
};

struct prim {
	int pol_mesh, group;
	int engine_mesh;              /* its mesh in the engine's model */
	int material;                 /* the file's */
	bool luv, colors, skinned;
	struct vkey *v;
	int nv, capv;
	int *corner;                  /* each point's first corner in the engine's buffer */
	uint32_t *idx;
	int nidx, capidx;
	int base;                     /* its first point's number in its mesh, as Blender numbers them */
	const char *node;             /* its node's name */
	float min_alpha;              /* its points' smallest alpha */
	uint8_t *engine;              /* the engine's buffer, read back for the samples */
	int stride, bones_off;        /* its layout (model.c's add_mesh) */
};

struct image {
	char *key;                    /* what it is made from */
	const char *name;             /* its name in the file */
	int index;                    /* in images */
	int texture;                  /* its texture, or -1 */
	int w, h;
	uint32_t crc;                 /* of the RGBA written */
	int min_alpha;
};

struct gmat {
	int material, mode;           /* the engine's material, and the alpha glTF gives it */
	double cutoff;
	bool both, colors;
	int index;
	const char *name;
	const char *image;            /* its colour picture's name, or NULL */
};

struct anim {
	char *file;                   /* the MOT, UTF-8 */
	const char *name;
	struct motion *mo;            /* the engine's, for the samples */
	int frames;
};

struct counts {
	int tri_index, tri_nan, tri_same, tri_copied;
	int weights_unknown, weights_twice, points_past_four, points_unweighted, normals_zero;
	int faces, faces_against;
	int pictures_missing;
};

struct export {
	struct rb3d_view *v;
	const struct rb3d_list *l;
	const struct rb3d_model *m;
	const char *generator;
	struct RE_plugin *p;
	struct RE_instance *inst;
	struct model *mdl;
	struct archive *aar;
	struct pol *pol;
	int *mat_off;                 /* each POL material's first engine material */
	struct glb g;
	cJSON *root, *root_kids;
	const char *root_name;
	struct image *img;
	int nimg;
	struct gmat *mat;
	int nmat;
	int walk_mat;
	struct names node_names, mesh_names, mat_names, img_names, anim_names, joint_names;
	struct prim *prim;
	int nprim;
	int *joint_node;
	const char **joint_name;
	uint32_t *joint_id;           /* each bone's id in the POL */
	struct anim *anim;
	int nanim;
	cJSON *skipped, *missing;
	struct counts c;
	int meshes, tris, verts, walk_meshes, walk_tris, walk_verts;
	cJSON *report;
	bool ok;
	char why[512];
	/* what posing the model for the samples changes on its instance, put back after */
	mat3x4 *saved_bones;
	vec4 saved_sphere;
	int saved_texture_index;
	vec3 saved_shadow_pos, saved_shadow_scale;
};

/* ---- pictures ---- */

static struct cg *read_cg(struct export *x, const char *name)
{
	if (!name || !name[0])
		return NULL;
	struct archive_data *d = RE_get_aar_entry(x->aar, x->mdl->path, name, "");
	if (!d)
		return NULL;
	struct cg *cg = cg_load_data(d);
	archive_free_data(d);
	if (cg && (!cg->pixels || cg->metrics.w <= 0 || cg->metrics.h <= 0)) {
		cg_free(cg);
		cg = NULL;
	}
	return cg;
}

static void note_missing(struct export *x, const char *name)
{
	char *u = utf(name);
	for (cJSON *it = x->missing->child; it; it = it->next)
		if (!strcmp(it->valuestring, u)) {
			free(u);
			return;
		}
	cJSON_AddItemToArray(x->missing, cJSON_CreateString(u[0] ? u : "(an empty name)"));
	x->c.pictures_missing++;
	free(u);
}

/* The red of picture c at the centre of texel (px, py) of a w by h picture: bilinear, round the
 * edges, as the engine's sampler reads a map of another size at the same UV. */
static int red_at(const struct cg *c, int px, int py, int w, int h)
{
	const uint8_t *p = c->pixels;
	int cw = c->metrics.w, ch = c->metrics.h;
	if (cw == w && ch == h)
		return p[((size_t)py * w + px) * 4];
	float fx = (px + 0.5f) * cw / w - 0.5f, fy = (py + 0.5f) * ch / h - 0.5f;
	int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
	float ax = fx - x0, ay = fy - y0;
	float r = 0;
	for (int dy = 0; dy < 2; dy++)
		for (int dx = 0; dx < 2; dx++) {
			int xx = ((x0 + dx) % cw + cw) % cw, yy = ((y0 + dy) % ch + ch) % ch;
			float wgt = (dx ? ax : 1 - ax) * (dy ? ay : 1 - ay);
			r += wgt * p[((size_t)yy * cw + xx) * 4];
		}
	int v = (int)lrintf(r);
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* name without its extension, UTF-8; free it. */
static char *stem_of(const char *sjis)
{
	char *u = utf(sjis);
	char *dot = strrchr(u, '.');
	if (dot && dot != u)
		*dot = 0;
	return u;
}

/* The picture name of the model's folder as the file holds it (bake: BAKE_*, amap the alpha map
 * for BAKE_TIMES_MAP and BAKE_FROM_MAP). Each made once; -1 when the file is not there or does not
 * read. */
static int get_image(struct export *x, const char *name, const char *amap, int bake)
{
	char key[800];
	snprintf(key, sizeof key, "%d|%s|%s", bake, name ? name : "", amap ? amap : "");
	for (int i = 0; i < x->nimg; i++)
		if (!strcmp(x->img[i].key, key))
			return i;
	struct cg *cg = read_cg(x, name);
	if (!cg) {
		note_missing(x, name);
		return -1;
	}
	int w = cg->metrics.w, h = cg->metrics.h;
	uint8_t *px = cg->pixels;
	size_t npx = (size_t)w * h;
	bool baked = false;
	if (bake == BAKE_TIMES_MAP || bake == BAKE_FROM_MAP) {
		struct cg *am = read_cg(x, amap);
		if (!am) {
			note_missing(x, amap);
		} else {
			for (int yy = 0; yy < h; yy++)
				for (int xx = 0; xx < w; xx++) {
					uint8_t *q = px + ((size_t)yy * w + xx) * 4;
					int r = red_at(am, xx, yy, w, h);
					q[3] = (uint8_t)(bake == BAKE_TIMES_MAP ? (q[3] * r + 127) / 255 : r);
				}
			cg_free(am);
			baked = true;
		}
	} else if (bake == BAKE_GREEN) {
		for (size_t i = 0; i < npx; i++)
			px[i * 4 + 1] = (uint8_t)(255 - px[i * 4 + 1]);
	}
	int min_alpha = 255;
	for (size_t i = 0; i < npx; i++)
		if (px[i * 4 + 3] < min_alpha)
			min_alpha = px[i * 4 + 3];
	struct bytes png = { 0 };
	if (!png_encode(px, w, h, &png)) {
		cg_free(cg);
		free(png.p);
		note_missing(x, name);
		return -1;
	}
	uint32_t crc = crc32_of(px, npx * 4);
	cg_free(cg);

	char want[600];
	char *s = stem_of(name);
	if (baked) {
		char *a = stem_of(amap);
		snprintf(want, sizeof want, "%s+%s", s, a);
		free(a);
	} else {
		snprintf(want, sizeof want, "%s", s);
	}
	free(s);
	const char *nm = name_take(&x->img_names, want, ".png", NULL);
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, "name", nm);
	cJSON_AddStringToObject(o, "mimeType", "image/png");
	cJSON_AddNumberToObject(o, "bufferView", glb_view(&x->g, png.p, png.n, 0));
	cJSON_AddItemToArray(x->g.images, o);
	free(png.p);

	x->img = xrealloc(x->img, (x->nimg + 1) * sizeof *x->img);
	struct image *im = &x->img[x->nimg];
	im->key = xstrdup(key);
	im->name = nm;
	im->index = x->g.nimages++;
	im->texture = -1;
	im->w = w;
	im->h = h;
	im->crc = crc;
	im->min_alpha = min_alpha;
	return x->nimg++;
}

static int image_texture(struct export *x, int i)
{
	if (x->img[i].texture < 0) {
		cJSON *t = cJSON_CreateObject();
		cJSON_AddNumberToObject(t, "sampler", 0);
		cJSON_AddNumberToObject(t, "source", x->img[i].index);
		cJSON_AddItemToArray(x->g.textures, t);
		x->img[i].texture = x->g.ntextures++;
	}
	return x->img[i].texture;
}

/* ---- materials ---- */

/* The POL material the engine's material mi was made from, for group child j of POL material pm
 * (model.c's model_load). */
static const struct pol_material *leaf_material(const struct pol *pol, uint32_t pm, int j)
{
	const struct pol_material_group *g = &pol->materials[pm];
	if (g->nr_children == 0)
		return &g->m;
	const struct pol_material_group *c = &g->children[j];
	return c->nr_children >= 2 ? &c->children[0].m : &c->m;
}

static void flag_word(char *buf, size_t n, const char *word)
{
	size_t len = strlen(buf);
	snprintf(buf + len, n - len, "%s%s", len ? " " : "", word);
}

/* The file's material for a primitive: one per engine material and what changes it in glTF (its
 * alpha, both sides, vertex colours: Blender makes one material per glTF material and presence of
 * COLOR_0). Blended or cut is the engine's call (renderer.c: an alpha map's red blended on a
 * transparent mesh, else cut under 0.1; the colour's alpha blended, else cut under 0.75). */
static int get_material(struct export *x, int mi, const struct pol_material *pm, const struct mesh *em,
                        bool colors, float min_vertex_alpha)
{
	const struct material *mat = &x->mdl->materials[mi];
	bool amap = mat->alpha_map != 0;
	bool blend = em->is_transparent;
	int bake = amap ? (blend ? BAKE_TIMES_MAP : BAKE_FROM_MAP) : BAKE_PLAIN;
	const char *cname = pm->textures[COLOR_MAP];
	int img = cname && mat->nr_color_maps > 0 ? get_image(x, cname, amap ? pm->textures[ALPHA_MAP] : NULL, bake) : -1;
	if (!cname)
		note_missing(x, "");
	int mode;
	double cutoff = 0;
	if (blend) {
		mode = MODE_BLEND;
	} else if (amap) {
		mode = MODE_MASK;
		cutoff = 0.1;
	} else if ((img >= 0 ? x->img[img].min_alpha / 255.0 : 1.0) * min_vertex_alpha >= 0.75) {
		mode = MODE_OPAQUE;   /* the engine's test at 0.75 cuts nothing */
	} else {
		mode = MODE_MASK;
		cutoff = 0.75;
	}
	bool both = em->flags & MESH_BOTH;
	for (int i = 0; i < x->nmat; i++) {
		struct gmat *g = &x->mat[i];
		if (g->material == mi && g->mode == mode && g->cutoff == cutoff && g->both == both && g->colors == colors)
			return g->index;
	}

	cJSON *o = cJSON_CreateObject();
	char *u = utf(pm->name);
	bool renamed = false;
	const char *nm = name_take(&x->mat_names, u[0] ? u : "material", "", &renamed);
	cJSON_AddStringToObject(o, "name", nm);
	cJSON *pbr = cJSON_AddObjectToObject(o, "pbrMetallicRoughness");
	if (img >= 0) {
		cJSON *t = cJSON_AddObjectToObject(pbr, "baseColorTexture");
		cJSON_AddNumberToObject(t, "index", image_texture(x, img));
	} else {
		double grey[4] = { 0.5, 0.5, 0.5, 1 };
		cJSON_AddItemToObject(pbr, "baseColorFactor", cJSON_CreateDoubleArray(grey, 4));
	}
	cJSON_AddNumberToObject(pbr, "metallicFactor", 0);
	cJSON_AddNumberToObject(pbr, "roughnessFactor", 1);
	cJSON_AddStringToObject(o, "alphaMode", mode_names[mode]);
	if (mode == MODE_MASK)
		cJSON_AddNumberToObject(o, "alphaCutoff", cutoff);
	if (both)
		cJSON_AddBoolToObject(o, "doubleSided", 1);
	if (pm->textures[NORMAL_MAP] && mat->normal_map) {
		int ni = get_image(x, pm->textures[NORMAL_MAP], NULL, BAKE_GREEN);
		if (ni >= 0) {
			cJSON *t = cJSON_AddObjectToObject(o, "normalTexture");
			cJSON_AddNumberToObject(t, "index", image_texture(x, ni));
		}
	}
	/* Rance Quest's TapirEngine lights nothing: its shader's diffuse is 1 and its ambient 0. */
	cJSON *ext = cJSON_AddObjectToObject(o, "extensions");
	cJSON_AddObjectToObject(ext, "KHR_materials_unlit");

	cJSON *ex = cJSON_AddObjectToObject(o, "extras");
	cJSON_AddStringToObject(ex, "rq_material", u);
	free(u);
	char flags[64] = "";
	if (mat->flags & MATERIAL_ALPHA)
		flag_word(flags, sizeof flags, "alpha");
	if (mat->flags & MATERIAL_SPRITE)
		flag_word(flags, sizeof flags, "sprite");
	if (flags[0])
		cJSON_AddStringToObject(ex, "rq_flags", flags);
	const char *alpha = mode == MODE_BLEND ? (amap ? "blended by the picture's alpha times its alpha map's red, baked into the picture"
	                                              : "blended by the picture's alpha")
	                  : amap ? "cut where its alpha map's red is under 0.1, baked into the picture's alpha"
	                  : mode == MODE_MASK ? "cut where the alpha is under 0.75"
	                  : "cut where the alpha is under 0.75, which is nowhere";
	cJSON_AddStringToObject(ex, "rq_alpha", alpha);
	if (amap) {
		char *a = utf(pm->textures[ALPHA_MAP]);
		cJSON_AddStringToObject(ex, "rq_alpha_map", a);
		free(a);
	}
	if (pm->textures[LIGHT_MAP] && mat->light_map) {
		int li = get_image(x, pm->textures[LIGHT_MAP], NULL, BAKE_PLAIN);
		if (li >= 0)
			cJSON_AddStringToObject(ex, "rq_light_map", x->img[li].name);
	}
	if (pm->textures[SPECULAR_MAP] && mat->specular_map) {
		int si = get_image(x, pm->textures[SPECULAR_MAP], NULL, BAKE_PLAIN);
		if (si >= 0)
			cJSON_AddStringToObject(ex, "rq_specular_map", x->img[si].name);
	}
	/* The colour's other pictures (face[1].png...), which a .txa's indices pick. */
	if (cname && mat->nr_color_maps > 1) {
		cJSON *fr = cJSON_AddObjectToObject(ex, "rq_frames");
		const char *dot = strrchr(cname, '.');
		for (int k = 1; dot && k < mat->nr_color_maps; k++) {
			char fname[300], key[16];
			snprintf(fname, sizeof fname, "%.*s[%d]%s", (int)(dot - cname), cname, k, dot);
			int fi = get_image(x, fname, amap ? pm->textures[ALPHA_MAP] : NULL, bake);
			snprintf(key, sizeof key, "%d", k);
			if (fi >= 0)
				cJSON_AddStringToObject(fr, key, x->img[fi].name);
		}
	}
	if (mat->specular_strength != 0 || mat->specular_shininess != 0) {
		float sp[2] = { mat->specular_strength, mat->specular_shininess };
		cJSON_AddItemToObject(ex, "rq_specular", nums(sp, 2));
	}
	if (mat->shadow_darkness != 1.0f)
		cJSON_AddNumberToObject(ex, "rq_shadow_darkness", fin(mat->shadow_darkness));
	cJSON_AddItemToArray(x->g.materials, o);

	x->mat = xrealloc(x->mat, (x->nmat + 1) * sizeof *x->mat);
	struct gmat *g = &x->mat[x->nmat++];
	g->material = mi;
	g->mode = mode;
	g->cutoff = cutoff;
	g->both = both;
	g->colors = colors;
	g->index = x->g.nmaterials++;
	g->name = nm;
	g->image = img >= 0 ? x->img[img].name : NULL;
	return g->index;
}

/* The walk mesh's material: see-through green, both sides. */
static int walk_material(struct export *x)
{
	if (x->walk_mat >= 0)
		return x->walk_mat;
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, "name", name_take(&x->mat_names, "walk mesh", "", NULL));
	cJSON *pbr = cJSON_AddObjectToObject(o, "pbrMetallicRoughness");
	double c[4] = { 0.2, 0.85, 0.35, 0.35 };
	cJSON_AddItemToObject(pbr, "baseColorFactor", cJSON_CreateDoubleArray(c, 4));
	cJSON_AddNumberToObject(pbr, "metallicFactor", 0);
	cJSON_AddNumberToObject(pbr, "roughnessFactor", 1);
	cJSON_AddStringToObject(o, "alphaMode", "BLEND");
	cJSON_AddBoolToObject(o, "doubleSided", 1);
	cJSON *ext = cJSON_AddObjectToObject(o, "extensions");
	cJSON_AddObjectToObject(ext, "KHR_materials_unlit");
	cJSON *ex = cJSON_AddObjectToObject(o, "extras");
	cJSON_AddNumberToObject(ex, "rq_walk_mesh", 1);
	cJSON_AddItemToArray(x->g.materials, o);
	return x->walk_mat = x->g.nmaterials++;
}

/* ---- weights, as the engine takes them (model.c's sort_and_normalize_bone_weights) ---- */

static int cmp_weight_desc(const void *lhs, const void *rhs)
{
	float l = ((const struct pol_bone_weight *)lhs)->weight;
	float r = ((const struct pol_bone_weight *)rhs)->weight;
	return (l < r) - (l > r);
}

/* A point's four weights: sorted, the four largest divided by their sum (a zero sum, which the
 * engine would turn into NaN, left at 0); a bone id the model does not have dropped as the engine's
 * shader skips it, the others not made up to 1 for it; a bone named twice added up. */
static void point_weights(struct export *x, const struct pol_vertex *pv, uint16_t joints[4], float weights[4])
{
	memset(joints, 0, 4 * sizeof *joints);
	memset(weights, 0, 4 * sizeof *weights);
	uint32_t n = pv->nr_weights;
	if (n == 0) {
		x->c.points_unweighted++;
		return;
	}
	if (n > 4)
		x->c.points_past_four++;
	struct pol_bone_weight w[64];
	struct pol_bone_weight *ws = n <= 64 ? w : xmalloc(n * sizeof *ws);
	memcpy(ws, pv->weights, n * sizeof *ws);
	qsort(ws, n, sizeof *ws, cmp_weight_desc);
	float total = 0;
	for (uint32_t i = 0; i < n && i < 4; i++)
		total += ws[i].weight;
	int k = 0;
	for (uint32_t i = 0; i < n && i < 4; i++) {
		float wt = total != 0 ? ws[i].weight / total : 0;
		struct bone *b = ht_get_int(x->mdl->bone_map, (int)ws[i].bone, NULL);
		if (!b) {
			x->c.weights_unknown++;
			continue;
		}
		if (!(wt != 0) || !isfinite(wt))
			continue;
		int d;
		for (d = 0; d < k; d++)
			if (joints[d] == b->index)
				break;
		if (d < k) {
			weights[d] += wt;
			x->c.weights_twice++;
			continue;
		}
		joints[k] = (uint16_t)b->index;
		weights[k] = wt;
		k++;
	}
	if (ws != w)
		free(ws);
	if (k == 0)
		x->c.points_unweighted++;
}

/* ---- the meshes ---- */

struct weld {
	int *slot;
	uint32_t mask;
	uint32_t *face;               /* the triangles kept, as sorted triples, 3 per slot */
	int *face_used;
	uint32_t face_mask;
};

static uint32_t hash_bytes(const void *p, size_t n)
{
	const uint8_t *b = p;
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; i++) {
		h ^= b[i];
		h *= 16777619u;
	}
	return h;
}

static uint32_t pow2_at_least(uint32_t n)
{
	uint32_t c = 16;
	while (c < n)
		c *= 2;
	return c;
}

static int prim_add_point(struct prim *pr, const struct vkey *k, int corner)
{
	if (pr->nv == pr->capv) {
		pr->capv = pr->capv ? pr->capv * 2 : 256;
		pr->v = xrealloc(pr->v, pr->capv * sizeof *pr->v);
		pr->corner = xrealloc(pr->corner, pr->capv * sizeof *pr->corner);
	}
	pr->v[pr->nv] = *k;
	pr->corner[pr->nv] = corner;
	return pr->nv++;
}

/* The point for key k: an equal point already made, else a new one (weld NULL: always new). */
static int weld_point(struct prim *pr, struct weld *w, const struct vkey *k, int corner, bool make)
{
	if (!w)
		return prim_add_point(pr, k, corner);
	uint32_t h = hash_bytes(k, sizeof *k) & w->mask;
	while (w->slot[h] >= 0) {
		if (!memcmp(&pr->v[w->slot[h]], k, sizeof *k))
			return w->slot[h];
		h = (h + 1) & w->mask;
	}
	if (!make)
		return -1;
	return w->slot[h] = prim_add_point(pr, k, corner);
}

/* Whether the triangle over points a, b, c (any order) was kept already; if not, it is noted. */
static bool weld_face_seen(struct weld *w, uint32_t a, uint32_t b, uint32_t c)
{
	uint32_t t[3] = { a, b, c }, s;
	if (t[0] > t[1]) { s = t[0]; t[0] = t[1]; t[1] = s; }
	if (t[1] > t[2]) { s = t[1]; t[1] = t[2]; t[2] = s; }
	if (t[0] > t[1]) { s = t[0]; t[0] = t[1]; t[1] = s; }
	uint32_t h = hash_bytes(t, sizeof t) & w->face_mask;
	while (w->face_used[h]) {
		if (!memcmp(&w->face[h * 3], t, sizeof t))
			return true;
		h = (h + 1) & w->face_mask;
	}
	w->face_used[h] = 1;
	memcpy(&w->face[h * 3], t, sizeof t);
	return false;
}

static void weld_init(struct weld *w, uint32_t corners)
{
	uint32_t cap = pow2_at_least(corners * 2 + 1);
	w->slot = xmalloc(cap * sizeof *w->slot);
	for (uint32_t i = 0; i < cap; i++)
		w->slot[i] = -1;
	w->mask = cap - 1;
	uint32_t fcap = pow2_at_least(corners / 3 * 2 + 1);
	w->face = xmalloc((size_t)fcap * 3 * sizeof *w->face);
	w->face_used = xcalloc(fcap, sizeof *w->face_used);
	w->face_mask = fcap - 1;
}

static void weld_free(struct weld *w)
{
	free(w->slot);
	free(w->face);
	free(w->face_used);
}

static void prim_add_index(struct prim *pr, uint32_t i)
{
	if (pr->nidx == pr->capidx) {
		pr->capidx = pr->capidx ? pr->capidx * 2 : 768;
		pr->idx = xrealloc(pr->idx, pr->capidx * sizeof *pr->idx);
	}
	pr->idx[pr->nidx++] = i;
}

static bool finite_key(const struct vkey *k)
{
	const float *f = &k->pos[0];
	for (int i = 0; i < 14; i++)
		if (!isfinite(f[i]))
			return false;
	for (int i = 0; i < 4; i++)
		if (!isfinite(k->weights[i]))
			return false;
	return true;
}

/* The triangles of POL mesh m in its material group j, as the engine draws them (model.c's
 * add_mesh: each corner its own vertex, in the triangles' order), welded into points. A triangle
 * with an index past its list or a number that is not one is left out, and one whose welded corners
 * repeat (Blender would drop it); one over the same three points as a kept one gets its own copies
 * of them (Blender's validate would drop it), so the triangles in Blender are these. walk: positions
 * only. Each point keeps the engine's corner it came from (q * 3 + c: q counts every triangle of the
 * group, kept or not, as the engine's buffer does). */
static void build_points(struct export *x, struct prim *pr, const struct pol_mesh *m, int j, bool walk,
                         uint16_t (*vj)[4], float (*vw)[4], const float tiling[2])
{
	uint32_t corners = 0;
	for (uint32_t t = 0; t < m->nr_triangles; t++)
		if (walk || m->triangles[t].material_group_index == (uint32_t)j)
			corners += 3;
	struct weld w;
	weld_init(&w, corners);
	int q = 0;
	pr->min_alpha = 1.f;
	for (uint32_t t = 0; t < m->nr_triangles; t++) {
		const struct pol_triangle *tr = &m->triangles[t];
		if (!walk && tr->material_group_index != (uint32_t)j)
			continue;
		int qq = q++;
		bool bad = false;
		for (int c = 0; c < 3 && !bad; c++) {
			if (tr->vert_index[c] >= m->nr_vertices)
				bad = true;
			if (walk)
				continue;
			if (tr->uv_index[c] >= m->nr_uvs)
				bad = true;
			if (pr->luv && tr->light_uv_index[c] >= m->nr_light_uvs)
				bad = true;
			if (m->nr_colors > 0 && tr->color_index[c] >= m->nr_colors)
				bad = true;
			if (m->nr_alphas > 0 && tr->alpha_index[c] >= m->nr_alphas)
				bad = true;
		}
		if (bad) {
			x->c.tri_index++;
			continue;
		}
		struct vkey k[3];
		memset(k, 0, sizeof k);
		for (int c = 0; c < 3; c++) {
			const struct pol_vertex *pv = &m->vertices[tr->vert_index[c]];
			for (int i = 0; i < 3; i++)
				k[c].pos[i] = tidy(pv->pos[i]);
			if (walk)
				continue;
			vec3 nrm;
			glm_vec3_copy((float *)tr->normals[c], nrm);
			float len = glm_vec3_norm(nrm);
			if (len > 1e-12f && isfinite(len)) {
				glm_vec3_scale(nrm, 1.f / len, nrm);
			} else {
				nrm[0] = 0, nrm[1] = 1, nrm[2] = 0;
				x->c.normals_zero++;
			}
			for (int i = 0; i < 3; i++)
				k[c].nrm[i] = tidy(nrm[i]);
			k[c].uv[0] = tidy(m->uvs[tr->uv_index[c]][0] * tiling[0]);
			k[c].uv[1] = tidy(m->uvs[tr->uv_index[c]][1] * tiling[1]);
			if (pr->luv) {
				k[c].luv[0] = tidy(m->light_uvs[tr->light_uv_index[c]][0]);
				k[c].luv[1] = tidy(m->light_uvs[tr->light_uv_index[c]][1]);
			}
			if (pr->colors) {
				for (int i = 0; i < 3; i++)
					k[c].col[i] = m->nr_colors > 0 ? tidy(m->colors[tr->color_index[c]][i]) : 1.f;
				k[c].col[3] = m->nr_alphas > 0 ? tidy(m->alphas[tr->alpha_index[c]]) : 1.f;
			}
			if (pr->skinned) {
				memcpy(k[c].joints, vj[tr->vert_index[c]], sizeof k[c].joints);
				memcpy(k[c].weights, vw[tr->vert_index[c]], sizeof k[c].weights);
			}
		}
		if (!finite_key(&k[0]) || !finite_key(&k[1]) || !finite_key(&k[2])) {
			x->c.tri_nan++;
			continue;
		}
		if (!memcmp(&k[0], &k[1], sizeof *k) || !memcmp(&k[1], &k[2], sizeof *k) || !memcmp(&k[0], &k[2], sizeof *k)) {
			x->c.tri_same++;
			continue;
		}
		int p[3];
		for (int c = 0; c < 3; c++)
			p[c] = weld_point(pr, &w, &k[c], qq * 3 + c, true);
		if (weld_face_seen(&w, p[0], p[1], p[2])) {
			for (int c = 0; c < 3; c++)
				p[c] = weld_point(pr, NULL, &k[c], qq * 3 + c, true);
			x->c.tri_copied++;
		}
		for (int c = 0; c < 3; c++)
			prim_add_index(pr, (uint32_t)p[c]);
		if (pr->colors)
			for (int c = 0; c < 3; c++)
				if (k[c].col[3] < pr->min_alpha)
					pr->min_alpha = k[c].col[3];
		/* which way the triangle faces against its corners' normals (a check of the winding) */
		if (!walk) {
			vec3 e1, e2, g, s;
			glm_vec3_sub(k[1].pos, k[0].pos, e1);
			glm_vec3_sub(k[2].pos, k[0].pos, e2);
			glm_vec3_cross(e1, e2, g);
			glm_vec3_add(k[0].nrm, k[1].nrm, s);
			glm_vec3_add(s, k[2].nrm, s);
			x->c.faces++;
			if (glm_vec3_dot(g, s) < 0)
				x->c.faces_against++;
		}
	}
	weld_free(&w);
}

/* The primitive's accessors and its JSON. */
static cJSON *prim_json(struct export *x, const struct prim *pr)
{
	int nv = pr->nv;
	float *buf = xmalloc((size_t)nv * 4 * sizeof(float));
	cJSON *o = cJSON_CreateObject();
	cJSON *at = cJSON_AddObjectToObject(o, "attributes");

	float mn[3] = { INFINITY, INFINITY, INFINITY }, mx[3] = { -INFINITY, -INFINITY, -INFINITY };
	for (int i = 0; i < nv; i++)
		for (int c = 0; c < 3; c++) {
			float f = pr->v[i].pos[c];
			buf[i * 3 + c] = f;
			if (f < mn[c]) mn[c] = f;
			if (f > mx[c]) mx[c] = f;
		}
	cJSON_AddNumberToObject(at, "POSITION", glb_accessor(&x->g, buf, nv, C_FLOAT, "VEC3", 3, T_ARRAY, mn, mx));
	bool walk = pr->engine_mesh < 0;
	if (!walk) {
		for (int i = 0; i < nv; i++)
			memcpy(&buf[i * 3], pr->v[i].nrm, 3 * sizeof(float));
		cJSON_AddNumberToObject(at, "NORMAL", glb_accessor(&x->g, buf, nv, C_FLOAT, "VEC3", 3, T_ARRAY, NULL, NULL));
		for (int i = 0; i < nv; i++)
			memcpy(&buf[i * 2], pr->v[i].uv, 2 * sizeof(float));
		cJSON_AddNumberToObject(at, "TEXCOORD_0", glb_accessor(&x->g, buf, nv, C_FLOAT, "VEC2", 2, T_ARRAY, NULL, NULL));
		if (pr->luv) {
			for (int i = 0; i < nv; i++)
				memcpy(&buf[i * 2], pr->v[i].luv, 2 * sizeof(float));
			cJSON_AddNumberToObject(at, "TEXCOORD_1", glb_accessor(&x->g, buf, nv, C_FLOAT, "VEC2", 2, T_ARRAY, NULL, NULL));
		}
		if (pr->colors) {
			for (int i = 0; i < nv; i++)
				memcpy(&buf[i * 4], pr->v[i].col, 4 * sizeof(float));
			cJSON_AddNumberToObject(at, "COLOR_0", glb_accessor(&x->g, buf, nv, C_FLOAT, "VEC4", 4, T_ARRAY, NULL, NULL));
		}
		if (pr->skinned) {
			uint16_t *jb = xmalloc((size_t)nv * 4 * sizeof *jb);
			for (int i = 0; i < nv; i++)
				memcpy(&jb[i * 4], pr->v[i].joints, 4 * sizeof *jb);
			cJSON_AddNumberToObject(at, "JOINTS_0", glb_accessor(&x->g, jb, nv, C_USHORT, "VEC4", 4, T_ARRAY, NULL, NULL));
			free(jb);
			for (int i = 0; i < nv; i++)
				memcpy(&buf[i * 4], pr->v[i].weights, 4 * sizeof(float));
			cJSON_AddNumberToObject(at, "WEIGHTS_0", glb_accessor(&x->g, buf, nv, C_FLOAT, "VEC4", 4, T_ARRAY, NULL, NULL));
		}
	}
	free(buf);
	if (nv < 65535) {
		uint16_t *ib = xmalloc((size_t)pr->nidx * sizeof *ib);
		for (int i = 0; i < pr->nidx; i++)
			ib[i] = (uint16_t)pr->idx[i];
		cJSON_AddNumberToObject(o, "indices", glb_accessor(&x->g, ib, pr->nidx, C_USHORT, "SCALAR", 1, T_INDEX, NULL, NULL));
		free(ib);
	} else {
		cJSON_AddNumberToObject(o, "indices", glb_accessor(&x->g, pr->idx, pr->nidx, C_UINT, "SCALAR", 1, T_INDEX, NULL, NULL));
	}
	cJSON_AddNumberToObject(o, "material", pr->material);
	cJSON_AddNumberToObject(o, "mode", 4);
	return o;
}

static const struct { uint32_t flag; const char *word; } mesh_flag_words[] = {
	{ MESH_NOLIGHTING, "nolighting" }, { MESH_NOMAKESHADOW, "nomakeshadow" }, { MESH_ENVMAP, "env" },
	{ MESH_BOTH, "both" }, { MESH_SPRITE, "sprite" }, { MESH_BLEND_ADDITIVE, "additive" },
	{ MESH_NO_EDGE, "no_edge" }, { MESH_NO_HEIGHT_DETECTION, "no_height_detection" }, { MESH_ALPHA, "alpha" },
	{ MESH_HAS_LIGHT_UV, "light_uv" }, { MESH_NO_ZWRITE, "no_zwrite" }, { MESH_HEIGHT_DETECTION, "height_detection" },
	{ MESH_HAS_SPECULAR_COLOR, "specular_color" }, { MESH_HAS_SPECULAR_POWER, "specular_power" },
	{ MESH_NO_DRAWSHADOW, "no_drawshadow" },
};

/* The node for a POL mesh, as children of the root, with what the game keeps per mesh in extras. */
static int mesh_node(struct export *x, const char *game_name, int mesh, bool skinned, const struct mesh *em,
                     bool walk, const char **node_name)
{
	cJSON *n = cJSON_CreateObject();
	bool renamed = false;
	const char *nm = name_take(&x->node_names, game_name, "", &renamed);
	*node_name = nm;
	cJSON_AddStringToObject(n, "name", nm);
	cJSON_AddNumberToObject(n, "mesh", mesh);
	if (skinned)
		cJSON_AddNumberToObject(n, "skin", 0);
	cJSON *ex = cJSON_AddObjectToObject(n, "extras");
	cJSON_AddStringToObject(ex, "rq_name", game_name);
	if (walk) {
		cJSON_AddNumberToObject(ex, "rq_walk_mesh", 1);
	} else if (em) {
		char flags[300] = "";
		for (size_t i = 0; i < sizeof mesh_flag_words / sizeof mesh_flag_words[0]; i++)
			if (em->flags & mesh_flag_words[i].flag)
				flag_word(flags, sizeof flags, mesh_flag_words[i].word);
		if (flags[0])
			cJSON_AddStringToObject(ex, "rq_flags", flags);
		if (!(em->flags & MESH_NO_EDGE)) {
			float e[4] = { em->outline_color[0], em->outline_color[1], em->outline_color[2], em->outline_thickness };
			cJSON_AddItemToObject(ex, "rq_edge", nums(e, 4));
		}
		if (em->uv_scroll[0] != 0 || em->uv_scroll[1] != 0)
			cJSON_AddItemToObject(ex, "rq_uv_scroll", nums(em->uv_scroll, 2));
		if (em->flags & MESH_HAS_SPECULAR_COLOR)
			cJSON_AddItemToObject(ex, "rq_specular_color", nums(em->specular_color, 3));
		if (em->flags & MESH_HAS_SPECULAR_POWER)
			cJSON_AddNumberToObject(ex, "rq_specular_power", fin(em->specular_power));
	}
	cJSON_AddItemToArray(x->g.nodes, n);
	cJSON_AddItemToArray(x->root_kids, cJSON_CreateNumber(x->g.nnodes));
	return x->g.nnodes++;
}

static void add_prim(struct export *x, const struct prim *pr)
{
	x->prim = xrealloc(x->prim, (x->nprim + 1) * sizeof *x->prim);
	x->prim[x->nprim++] = *pr;
}

/* The engine's vertex layout for a mesh (model.c's add_mesh): its stride and where the bones are. */
static void engine_layout(const struct model *mdl, const struct pol_mesh *m, int mi, int *stride, int *bones_off)
{
	const struct material *mat = &mdl->materials[mi];
	int off = 32;                                   /* position, normal, UV */
	if (m->light_uvs && mat->light_map)
		off += 8;
	if (m->nr_colors > 0 || m->nr_alphas > 0)
		off += 16;
	if (mat->normal_map)
		off += 16;
	*bones_off = off;
	if (mdl->bone_map)
		off += 32;
	if (m->blend_weights && mat->blend_texture)
		off += 12;
	*stride = off;
}

/* Every POL mesh: its primitives as the engine splits it (one per material of its group the
 * triangles use, model.c's model_load), each matched with the engine's mesh made from it; the walk
 * meshes as nodes of their own. */
static bool build_meshes(struct export *x)
{
	struct pol *pol = x->pol;
	bool skinned = x->mdl->bone_map != NULL;
	int em = 0;
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		struct pol_mesh *m = pol->meshes[i];
		if (!m)
			continue;
		char *game_name = utf(m->name);
		bool walk = !strcmp(m->name, "collision");
		uint16_t (*vj)[4] = NULL;
		float (*vw)[4] = NULL;
		if (skinned && !walk && m->nr_vertices) {
			vj = xmalloc(m->nr_vertices * sizeof *vj);
			vw = xmalloc(m->nr_vertices * sizeof *vw);
			for (uint32_t k = 0; k < m->nr_vertices; k++)
				point_weights(x, &m->vertices[k], vj[k], vw[k]);
		}
		int ngroups = 1;
		if (!walk) {
			if (m->material >= pol->nr_materials) {
				snprintf(x->why, sizeof x->why, "a mesh's material is past the POL's list");
				free(game_name);
				free(vj);
				free(vw);
				return false;
			}
			const struct pol_material_group *g = &pol->materials[m->material];
			ngroups = g->nr_children ? (int)g->nr_children : 1;
		}
		cJSON *prims = cJSON_CreateArray();
		int nprims = 0, base = 0, first = x->nprim;
		const struct mesh *first_em = NULL;
		for (int j = 0; j < ngroups; j++) {
			struct prim pr;
			memset(&pr, 0, sizeof pr);
			pr.pol_mesh = (int)i;
			pr.group = j;
			pr.engine_mesh = -1;
			uint32_t ntris = 0;
			for (uint32_t t = 0; t < m->nr_triangles; t++)
				if (walk || m->triangles[t].material_group_index == (uint32_t)j)
					ntris++;
			if (!ntris)
				continue;
			float tiling[2] = { 1, 1 };
			int mi = -1;
			const struct pol_material *pm = NULL;
			if (!walk) {
				mi = x->mat_off[m->material] + j;
				pm = leaf_material(pol, m->material, j);
				if (em >= x->mdl->nr_meshes || strcmp(x->mdl->meshes[em].name, m->name)
				    || x->mdl->meshes[em].material != mi || x->mdl->meshes[em].nr_vertices != (int)ntris * 3) {
					snprintf(x->why, sizeof x->why, "the engine's meshes are not the POL's (at its mesh %d)", em);
					free(game_name);
					free(vj);
					free(vw);
					cJSON_Delete(prims);
					return false;
				}
				pr.engine_mesh = em++;
				glm_vec2_copy(x->mdl->materials[mi].uv_tiling, tiling);
				pr.luv = m->light_uvs != NULL;
				pr.colors = m->nr_colors > 0 || m->nr_alphas > 0;
				pr.skinned = skinned;
				engine_layout(x->mdl, m, mi, &pr.stride, &pr.bones_off);
				if (!first_em)
					first_em = &x->mdl->meshes[pr.engine_mesh];
			}
			build_points(x, &pr, m, j, walk, vj, vw, tiling);
			if (!pr.nidx) {
				free(pr.v);
				free(pr.corner);
				free(pr.idx);
				continue;
			}
			pr.material = walk ? walk_material(x)
			            : get_material(x, mi, pm, &x->mdl->meshes[pr.engine_mesh], pr.colors, pr.min_alpha);
			pr.base = base;
			base += pr.nv;
			cJSON_AddItemToArray(prims, prim_json(x, &pr));
			if (walk) {
				x->walk_tris += pr.nidx / 3;
				x->walk_verts += pr.nv;
			} else {
				x->tris += pr.nidx / 3;
				x->verts += pr.nv;
			}
			add_prim(x, &pr);
			nprims++;
		}
		free(vj);
		free(vw);
		if (!nprims) {
			cJSON_Delete(prims);
			free(game_name);
			continue;
		}
		cJSON *mesh = cJSON_CreateObject();
		cJSON_AddStringToObject(mesh, "name", name_take(&x->mesh_names, game_name, "", NULL));
		cJSON_AddItemToObject(mesh, "primitives", prims);
		cJSON_AddItemToArray(x->g.meshes, mesh);
		const char *node_name;
		mesh_node(x, game_name, x->g.nmeshes++, skinned && !walk, first_em, walk, &node_name);
		for (int k = first; k < x->nprim; k++)
			x->prim[k].node = node_name;
		if (walk)
			x->walk_meshes++;
		else
			x->meshes++;
		free(game_name);
	}
	if (em != x->mdl->nr_meshes) {
		snprintf(x->why, sizeof x->why, "the engine has %d meshes, the POL gives %d", x->mdl->nr_meshes, em);
		return false;
	}
	return true;
}

/* ---- the skeleton ---- */

/* Each bone a node under the root, its rest from the engine's inverse binds (local = the parent's
 * inverse bind times the inverse of its own: a translation and a rotation, never a matrix); the skin's
 * joints in the engine's order with those inverse binds as they are. */
static void build_skeleton(struct export *x)
{
	struct model *mdl = x->mdl;
	int nb = mdl->nr_bones;
	if (nb <= 0 || !mdl->bone_map)
		return;
	x->joint_node = xcalloc(nb, sizeof *x->joint_node);
	x->joint_name = xcalloc(nb, sizeof *x->joint_name);
	x->joint_id = xcalloc(nb, sizeof *x->joint_id);
	for (uint32_t k = 0; k < x->pol->nr_bones && k < (uint32_t)nb; k++)
		x->joint_id[mdl->bones_by_pol_index[k]->index] = x->pol->bones[k].id;
	cJSON **node = xcalloc(nb, sizeof *node);
	cJSON **kids = xcalloc(nb, sizeof *kids);
	float *ibm = xmalloc((size_t)nb * 16 * sizeof(float));
	for (int i = 0; i < nb; i++) {
		struct bone *b = &mdl->bones[i];
		mat4 inv, local;
		glm_mat4_inv(b->inverse_bind_matrix, inv);
		if (b->parent >= 0)
			glm_mat4_mul(mdl->bones[b->parent].inverse_bind_matrix, inv, local);
		else
			glm_mat4_copy(inv, local);
		versor q;
		glm_mat4_quat(local, q);
		glm_quat_normalize(q);
		float t[3] = { local[3][0], local[3][1], local[3][2] };
		char *u = utf(b->name);
		bool renamed = false;
		const char *nm = name_take(&x->joint_names, u, "", &renamed);
		cJSON *n = cJSON_CreateObject();
		cJSON_AddStringToObject(n, "name", nm);
		cJSON_AddItemToObject(n, "translation", nums(t, 3));
		cJSON_AddItemToObject(n, "rotation", nums(q, 4));
		cJSON *ex = cJSON_AddObjectToObject(n, "extras");
		cJSON_AddNumberToObject(ex, "rq_bone_id", x->joint_id[i]);
		if (renamed)
			cJSON_AddStringToObject(ex, "rq_name", u);
		free(u);
		cJSON_AddItemToArray(x->g.nodes, n);
		node[i] = n;
		x->joint_name[i] = nm;
		x->joint_node[i] = x->g.nnodes++;
		memcpy(&ibm[i * 16], b->inverse_bind_matrix, 16 * sizeof(float));
		if (b->parent >= 0 && b->parent < i) {
			if (!kids[b->parent])
				kids[b->parent] = cJSON_AddArrayToObject(node[b->parent], "children");
			cJSON_AddItemToArray(kids[b->parent], cJSON_CreateNumber(x->joint_node[i]));
		} else {
			cJSON_AddItemToArray(x->root_kids, cJSON_CreateNumber(x->joint_node[i]));
		}
	}
	cJSON *skin = cJSON_CreateObject();
	cJSON_AddStringToObject(skin, "name", "skeleton");
	cJSON_AddNumberToObject(skin, "inverseBindMatrices", glb_accessor(&x->g, ibm, nb, C_FLOAT, "MAT4", 16, 0, NULL, NULL));
	cJSON_AddNumberToObject(skin, "skeleton", 0);
	cJSON *joints = cJSON_AddArrayToObject(skin, "joints");
	for (int i = 0; i < nb; i++)
		cJSON_AddItemToArray(joints, cJSON_CreateNumber(x->joint_node[i]));
	cJSON_AddItemToArray(x->g.skins, skin);
	free(ibm);
	free(node);
	free(kids);
}

/* ---- the animations ---- */

static uint32_t le32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void skip_mot(struct export *x, const char *file, const char *why)
{
	cJSON *s = cJSON_CreateObject();
	cJSON_AddStringToObject(s, "file", file);
	cJSON_AddStringToObject(s, "why", why);
	cJSON_AddItemToArray(x->skipped, s);
}

/* One animation per MOT of the folder made for the model's bones, named by its stem without the
 * folder's name (gokiburi_taiki is taiki; the whole stem when the folder's name is not there or two
 * would be the same): a key on each of its frames (frame / 30 s, from 0), the translation and rotation
 * of every joint, LINEAR, each quaternion on the side of the one before (the engine's nlerp takes the
 * short way: so does Blender's lerp of the keys then). The MOT's track i drives bone i (the engine's
 * mot_load reorders them by array index, never by name). */
static void build_animations(struct export *x)
{
	const struct rb3d_model *m = x->m;
	int nb = x->mdl->nr_bones;
	size_t flen = strlen(m->folder);
	for (int k = 0; k < m->nmots; k++) {
		char file[300];
		snprintf(file, sizeof file, "%s.MOT", m->mots[k]);
		struct archive_data *d = RE_get_aar_entry(x->aar, x->mdl->path, m->mots_raw[k], ".MOT");
		int bones = -1, frames = 0;
		if (d) {
			if (d->size >= 16 && !memcmp(d->data, "MOT\0", 4)) {
				frames = (int)le32(d->data + 8);
				bones = (int)le32(d->data + 12);
			}
			archive_free_data(d);
		}
		char why[160];
		if (bones < 0) {
			skip_mot(x, file, "it does not read");
			continue;
		}
		if (nb <= 0 || !x->mdl->mot_cache) {
			skip_mot(x, file, "the model has no bones");
			continue;
		}
		if (bones != nb) {
			snprintf(why, sizeof why, "made for %d bones, the model has %d", bones, nb);
			skip_mot(x, file, why);
			continue;
		}
		if (frames <= 0) {
			skip_mot(x, file, "it has no frames");
			continue;
		}
		struct motion *mo = motion_load(m->mots_raw[k], x->inst, x->aar);
		if (!mo || !mo->mot || (int)mo->mot->nr_bones != nb || mo->mot->nr_frames == 0) {
			if (mo)
				motion_free(mo);
			skip_mot(x, file, "the engine does not load it");
			continue;
		}
		struct mot *mot = mo->mot;
		int nf = (int)mot->nr_frames;

		const char *stem = m->mots[k];
		const char *want = stem;
		if (flen && !strncasecmp(stem, m->folder, flen) && stem[flen] == '_' && stem[flen + 1]) {
			want = stem + flen + 1;
			if (names_has(&x->anim_names, want))
				want = stem;
		}
		const char *name = name_take(&x->anim_names, want, "", NULL);

		float *times = xmalloc(nf * sizeof *times);
		for (int f = 0; f < nf; f++)
			times[f] = (float)(f / FPS);
		float tmin = 0, tmax = times[nf - 1];
		int input = glb_accessor(&x->g, times, nf, C_FLOAT, "SCALAR", 1, 0, &tmin, &tmax);
		free(times);
		cJSON *an = cJSON_CreateObject();
		cJSON_AddStringToObject(an, "name", name);
		cJSON *samplers = cJSON_AddArrayToObject(an, "samplers");
		cJSON *channels = cJSON_AddArrayToObject(an, "channels");
		float *tr = xmalloc((size_t)nf * 3 * sizeof *tr), *ro = xmalloc((size_t)nf * 4 * sizeof *ro);
		int ns = 0;
		for (int i = 0; i < nb; i++) {
			const struct mot_frame *fr = mot->motions[i]->frames;
			for (int f = 0; f < nf; f++) {
				memcpy(&tr[f * 3], fr[f].pos, 3 * sizeof(float));
				float q[4];
				memcpy(q, fr[f].rotq, sizeof q);
				if (f > 0 && q[0] * ro[(f - 1) * 4] + q[1] * ro[(f - 1) * 4 + 1] + q[2] * ro[(f - 1) * 4 + 2]
				                 + q[3] * ro[(f - 1) * 4 + 3] < 0)
					for (int c = 0; c < 4; c++)
						q[c] = -q[c];
				memcpy(&ro[f * 4], q, sizeof q);
			}
			for (int path = 0; path < 2; path++) {
				cJSON *s = cJSON_CreateObject();
				cJSON_AddNumberToObject(s, "input", input);
				cJSON_AddNumberToObject(s, "output", path ? glb_accessor(&x->g, ro, nf, C_FLOAT, "VEC4", 4, 0, NULL, NULL)
				                                          : glb_accessor(&x->g, tr, nf, C_FLOAT, "VEC3", 3, 0, NULL, NULL));
				cJSON_AddStringToObject(s, "interpolation", "LINEAR");
				cJSON_AddItemToArray(samplers, s);
				cJSON *c = cJSON_CreateObject();
				cJSON_AddNumberToObject(c, "sampler", ns++);
				cJSON *t = cJSON_AddObjectToObject(c, "target");
				cJSON_AddNumberToObject(t, "node", x->joint_node[i]);
				cJSON_AddStringToObject(t, "path", path ? "rotation" : "translation");
				cJSON_AddItemToArray(channels, c);
			}
		}
		free(tr);
		free(ro);
		cJSON_AddItemToArray(x->g.anims, an);
		x->g.nanims++;

		x->anim = xrealloc(x->anim, (x->nanim + 1) * sizeof *x->anim);
		struct anim *a = &x->anim[x->nanim++];
		a->file = xstrdup(file);
		a->name = name;
		a->mo = mo;
		a->frames = nf;
	}
}

/* ---- the root's extras: the game's names and how it plays the motions ---- */

static void play_text(const struct rb3d_play *p, char *buf, size_t n)
{
	int first, last;
	rb3d_play_frames(p, &first, &last);
	if (!p->loop && p->end <= p->start)
		snprintf(buf, n, "still on %d", p->start);
	else
		snprintf(buf, n, "%d to %d, %s", first, last, p->loop ? "in a loop" : "once");
}

static void add_play(cJSON *o, const struct rb3d_play *p)
{
	char text[80];
	play_text(p, text, sizeof text);
	cJSON_AddStringToObject(o, "plays", text);
	int first, last;
	rb3d_play_frames(p, &first, &last);
	cJSON_AddNumberToObject(o, "first", first);
	cJSON_AddNumberToObject(o, "last", last);
}

static void root_extras(struct export *x, cJSON *ex)
{
	const struct rb3d_model *m = x->m;
	cJSON_AddStringToObject(ex, "rq_folder", m->folder);
	cJSON_AddStringToObject(ex, "rq_kind", rb3d_kind_folders[m->kind]);
	if (m->label && m->label[0])
		cJSON_AddStringToObject(ex, "rq_label", m->label);
	if (m->en && m->en[0])
		cJSON_AddStringToObject(ex, "rq_label_en", m->en);
	if (m->look)
		cJSON_AddStringToObject(ex, "rq_look", m->look);
	if (m->bg_no >= 0)
		cJSON_AddNumberToObject(ex, "rq_background", m->bg_no);
	if (m->object >= 0) {
		cJSON *o = cJSON_AddObjectToObject(ex, "rq_object");
		cJSON_AddNumberToObject(o, "no", m->object);
		cJSON_AddNumberToObject(o, "state", m->obj_state);
		struct rb3d_play p;
		rb3d_map_play(m, &p);
		add_play(o, &p);
	}
	/* MonsterInfo's entries that draw it */
	cJSON *ents = NULL;
	for (int i = 0; x->l && i < x->l->ne; i++) {
		const struct rb3d_entry *e = &x->l->e[i];
		if (!e->poly || strcasecmp(e->poly, m->folder))
			continue;
		if (!ents)
			ents = cJSON_AddObjectToObject(ex, "rq_entries");
		char key[16];
		snprintf(key, sizeof key, "%d", e->no);
		cJSON *o = cJSON_AddObjectToObject(ents, key);
		cJSON_AddStringToObject(o, "name", e->name ? e->name : "");
		cJSON_AddNumberToObject(o, "model", 100000 + e->no);
		cJSON_AddNumberToObject(o, "scale", fin(e->scale));
		float pos[3] = { e->x, e->y, e->z };
		cJSON_AddItemToObject(o, "position", nums(pos, 3));
		cJSON_AddNumberToObject(o, "shadow", e->make_shadow ? 1 : 0);
		cJSON_AddNumberToObject(o, "shadow_bone_radius", fin(e->shadow_bone_radius));
	}
	/* each MOT: its animation, its frames, its texture indices, and how the game plays it */
	cJSON *mots = cJSON_AddObjectToObject(ex, "rq_motions");
	for (int k = 0; k < m->nmots; k++) {
		char file[300];
		snprintf(file, sizeof file, "%s.MOT", m->mots[k]);
		cJSON *o = cJSON_AddObjectToObject(mots, file);
		const struct anim *a = NULL;
		for (int i = 0; i < x->nanim; i++)
			if (!strcmp(x->anim[i].file, file))
				a = &x->anim[i];
		if (a) {
			cJSON_AddStringToObject(o, "animation", a->name);
			cJSON_AddNumberToObject(o, "frames", a->frames);
			const struct mot *mot = a->mo->mot;
			if (mot->nr_texture_indices) {
				cJSON *t = cJSON_CreateArray();
				uint32_t most = 0;
				for (uint32_t f = 0; f < mot->nr_texture_indices; f++) {
					cJSON_AddItemToArray(t, cJSON_CreateNumber(mot->texture_indices[f]));
					if (mot->texture_indices[f] > most)
						most = mot->texture_indices[f];
				}
				cJSON_AddItemToObject(o, "txa", t);
				for (int mi = 0; mi < x->mdl->nr_materials; mi++)
					if (x->mdl->materials[mi].nr_color_maps > 0 && most >= (uint32_t)x->mdl->materials[mi].nr_color_maps) {
						cJSON_AddStringToObject(o, "txa_note", "an index past a material's pictures shows its first");
						break;
					}
			}
		} else {
			for (cJSON *s = x->skipped->child; s; s = s->next)
				if (!strcmp(cJSON_GetObjectItem(s, "file")->valuestring, file))
					cJSON_AddStringToObject(o, "left_out", cJSON_GetObjectItem(s, "why")->valuestring);
		}
		cJSON *game = NULL;
		for (int i = 0; x->l && i < x->l->ne; i++) {
			const struct rb3d_entry *e = &x->l->e[i];
			if (!e->poly || strcasecmp(e->poly, m->folder))
				continue;
			for (int s = 0; s < e->nmotions; s++) {
				const struct rb3d_motion *mo = &e->motions[s];
				if (!mo->data || strcasecmp(mo->data, m->mots[k]))
					continue;
				if (!game)
					game = cJSON_AddObjectToObject(o, "game");
				char key[32];
				snprintf(key, sizeof key, "%d/%d", e->no, mo->slot);
				cJSON *g = cJSON_AddObjectToObject(game, key);
				cJSON_AddStringToObject(g, "name", mo->name ? mo->name : "");
				cJSON_AddStringToObject(g, "type", mo->loop ? "loop" : "once");
				int fr[4] = { mo->begin, mo->end, mo->loop_begin, mo->loop_end };
				cJSON_AddItemToObject(g, "frames", cJSON_CreateIntArray(fr, 2));
				cJSON_AddItemToObject(g, "loop_frames", cJSON_CreateIntArray(fr + 2, 2));
				struct rb3d_play p;
				rb3d_fight_play(mo, &p);
				add_play(g, &p);
			}
		}
	}
}

/* ---- the engine's numbers, for the checker ---- */

/* The engine's pose of the model on frame f of motion mo: RE_build_model with the motion stopped
 * there, as update_bones then leaves the instance's skin matrices (row-major 3 by 4). */
static void pose_engine(struct export *x, struct motion *mo, float f)
{
	mo->state = RE_MOTION_STATE_STOP;
	mo->current_frame = f;
	mo->frame_begin = mo->loop_frame_begin = 0;
	mo->frame_end = mo->loop_frame_end = (float)(mo->mot->nr_frames - 1);
	x->inst->motion = mo;
	RE_build_model(x->p, 0);
}

static void skin_point(const mat3x4 *bt, int nb, const int ids[4], const float w[4], const float p[3], float out[3])
{
	out[0] = out[1] = out[2] = 0;
	for (int k = 0; k < 4; k++) {
		if (ids[k] < 0 || ids[k] >= nb)
			continue;
		const vec4 *s = bt[ids[k]];
		for (int r = 0; r < 3; r++)
			out[r] += w[k] * (s[r][0] * p[0] + s[r][1] * p[1] + s[r][2] * p[2] + s[r][3]);
	}
}

/* A point of the engine's buffer of primitive pr: its position, UV, light UV (when the engine has
 * them) and its bones as the engine's shader reads them. */
struct epoint {
	float pos[3], uv[2], luv[2];
	bool has_luv;
	int ids[4];
	float w[4];
	bool has_bones;
};

static void engine_point(struct export *x, struct prim *pr, int point, struct epoint *out)
{
	memset(out, 0, sizeof *out);
	for (int k = 0; k < 4; k++)
		out->ids[k] = -1;
	const struct mesh *em = &x->mdl->meshes[pr->engine_mesh];
	if (!pr->engine) {
		size_t size = (size_t)em->nr_vertices * pr->stride;
		pr->engine = xmalloc(size);
		glBindBuffer(GL_ARRAY_BUFFER, em->attr_buffer);
		glGetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)size, pr->engine);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	const uint8_t *v = pr->engine + (size_t)pr->corner[point] * pr->stride;
	memcpy(out->pos, v, 3 * sizeof(float));
	memcpy(out->uv, v + 24, 2 * sizeof(float));
	const struct pol_mesh *m = x->pol->meshes[pr->pol_mesh];
	const struct material *mat = &x->mdl->materials[em->material];
	if (m->light_uvs && mat->light_map) {
		memcpy(out->luv, v + 32, 2 * sizeof(float));
		out->has_luv = true;
	}
	if (x->mdl->bone_map) {
		memcpy(out->ids, v + pr->bones_off, 4 * sizeof(int));
		memcpy(out->w, v + pr->bones_off + 16, 4 * sizeof(float));
		out->has_bones = true;
	}
}

struct pick {
	int prim, point;
};

static void pick_add(struct pick **p, int *n, int prim, int point)
{
	for (int i = 0; i < *n; i++)
		if ((*p)[i].prim == prim && (*p)[i].point == point)
			return;
	*p = xrealloc(*p, (*n + 1) * sizeof **p);
	(*p)[*n].prim = prim;
	(*p)[*n].point = point;
	(*n)++;
}

/* The points the samples follow: each primitive's first; on a skinned model, for each joint the point
 * it weighs most on, and points with 3 or 4 weights and on a joint past 255. */
static struct pick *pick_points(struct export *x, int *np)
{
	struct pick *p = NULL;
	int n = 0;
	for (int i = 0; i < x->nprim; i++)
		if (x->prim[i].engine_mesh >= 0 && x->prim[i].nv)
			pick_add(&p, &n, i, 0);
	int nb = x->mdl->nr_bones;
	if (nb > 0 && x->mdl->bone_map) {
		int *best_prim = xmalloc(nb * sizeof(int)), *best_point = xmalloc(nb * sizeof(int));
		float *best = xmalloc(nb * sizeof(float));
		for (int b = 0; b < nb; b++)
			best[b] = 0, best_prim[b] = -1, best_point[b] = 0;
		int many = 0, high = 0;
		for (int i = 0; i < x->nprim; i++) {
			const struct prim *pr = &x->prim[i];
			if (pr->engine_mesh < 0 || !pr->skinned)
				continue;
			for (int v = 0; v < pr->nv; v++) {
				int nw = 0;
				bool past = false;
				for (int k = 0; k < 4; k++) {
					float w = pr->v[v].weights[k];
					int j = pr->v[v].joints[k];
					if (w <= 0)
						continue;
					nw++;
					if (j > 255)
						past = true;
					if (j < nb && w > best[j]) {
						best[j] = w;
						best_prim[j] = i;
						best_point[j] = v;
					}
				}
				if (nw >= 3 && many < 8 && v % 7 == 0) {
					pick_add(&p, &n, i, v);
					many++;
				}
				if (past && high < 8) {
					pick_add(&p, &n, i, v);
					high++;
				}
			}
		}
		for (int b = 0; b < nb; b++)
			if (best_prim[b] >= 0)
				pick_add(&p, &n, best_prim[b], best_point[b]);
		free(best_prim);
		free(best_point);
		free(best);
	}
	*np = n;
	return p;
}

static cJSON *point_ref(const struct prim *pr, int point)
{
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, "mesh", pr->node);
	cJSON_AddNumberToObject(o, "index", pr->base + point);
	return o;
}

static void samples(struct export *x, cJSON *out)
{
	struct model *mdl = x->mdl;
	int nb = mdl->bone_map ? mdl->nr_bones : 0;
	int np = 0;
	struct pick *pk = pick_points(x, &np);

	/* at rest: each bone's head (the inverse of its inverse bind), each picked point */
	cJSON *rest = cJSON_AddObjectToObject(out, "rest");
	float (*heads)[3] = nb ? xmalloc(nb * sizeof *heads) : NULL;
	if (nb) {
		cJSON *h = cJSON_AddArrayToObject(rest, "heads");
		for (int i = 0; i < nb; i++) {
			mat4 inv;
			glm_mat4_inv(mdl->bones[i].inverse_bind_matrix, inv);
			memcpy(heads[i], inv[3], 3 * sizeof(float));
			cJSON *o = cJSON_CreateObject();
			cJSON_AddStringToObject(o, "bone", x->joint_name[i]);
			cJSON_AddItemToObject(o, "pos", sample_vec(heads[i], 3));
			cJSON_AddItemToArray(h, o);
		}
	}
	cJSON *pts = cJSON_AddArrayToObject(rest, "points");
	cJSON *uvs = cJSON_AddArrayToObject(out, "uvs");
	struct epoint *ep = xmalloc((np ? np : 1) * sizeof *ep);
	for (int i = 0; i < np; i++) {
		struct prim *pr = &x->prim[pk[i].prim];
		engine_point(x, pr, pk[i].point, &ep[i]);
		cJSON *o = point_ref(pr, pk[i].point);
		cJSON_AddItemToObject(o, "pos", sample_vec(ep[i].pos, 3));
		cJSON_AddItemToArray(pts, o);
		/* the UVs Blender should hold: the engine's, the colour's tiling applied as the export did */
		const float *tiling = mdl->materials[mdl->meshes[pr->engine_mesh].material].uv_tiling;
		float uv[2] = { ep[i].uv[0] * tiling[0], ep[i].uv[1] * tiling[1] };
		cJSON *u = point_ref(pr, pk[i].point);
		cJSON_AddItemToObject(u, "uv", sample_vec(uv, 2));
		if (ep[i].has_luv)
			cJSON_AddItemToObject(u, "uv1", sample_vec(ep[i].luv, 2));
		cJSON_AddItemToArray(uvs, u);
	}

	/* posed by the engine: four frames of each animation, every bone's head and the picked points */
	cJSON *posed = cJSON_AddArrayToObject(out, "posed");
	for (int a = 0; nb && a < x->nanim; a++) {
		struct anim *an = &x->anim[a];
		int nf = an->frames;
		float mid = (float)((nf - 1) / 2);
		float frames[4] = { 0, mid, (float)(nf - 1), mid + 0.5f };
		int nfr = nf >= 2 ? 4 : 1;
		for (int k = 0; k < nfr; k++) {
			if (k == 3 && frames[3] > nf - 1)
				continue;
			pose_engine(x, an->mo, frames[k]);
			const mat3x4 *bt = (const mat3x4 *)x->inst->bone_transforms;
			cJSON *o = cJSON_CreateObject();
			cJSON_AddStringToObject(o, "animation", an->name);
			cJSON_AddNumberToObject(o, "frame", frames[k]);
			cJSON *h = cJSON_AddArrayToObject(o, "heads");
			for (int i = 0; i < nb; i++) {
				int id[4] = { i, -1, -1, -1 };
				float w[4] = { 1, 0, 0, 0 }, hp[3];
				skin_point(bt, nb, id, w, heads[i], hp);
				cJSON_AddItemToArray(h, sample_vec(hp, 3));
			}
			cJSON *ps = cJSON_AddArrayToObject(o, "points");
			for (int i = 0; i < np; i++) {
				if (!ep[i].has_bones)
					continue;
				float pp[3];
				skin_point(bt, nb, ep[i].ids, ep[i].w, ep[i].pos, pp);
				cJSON *r = point_ref(&x->prim[pk[i].prim], pk[i].point);
				cJSON_AddItemToObject(r, "pos", sample_vec(pp, 3));
				cJSON_AddItemToArray(ps, r);
			}
			cJSON_AddItemToArray(posed, o);
		}
	}
	free(heads);
	free(ep);
	free(pk);

	/* each material's alpha and picture, each picture's pixels */
	cJSON *mats = cJSON_AddObjectToObject(out, "materials");
	for (int i = 0; i < x->nmat; i++) {
		const struct gmat *g = &x->mat[i];
		cJSON *o = cJSON_AddObjectToObject(mats, g->name);
		cJSON_AddStringToObject(o, "alpha_mode", mode_names[g->mode]);
		if (g->mode == MODE_MASK)
			cJSON_AddNumberToObject(o, "cutoff", g->cutoff);
		cJSON_AddNumberToObject(o, "double_sided", g->both ? 1 : 0);
		cJSON_AddNumberToObject(o, "unlit", 1);
		cJSON_AddStringToObject(o, "image", g->image ? g->image : "");
	}
	cJSON *imgs = cJSON_AddObjectToObject(out, "images");
	for (int i = 0; i < x->nimg; i++) {
		cJSON *o = cJSON_AddObjectToObject(imgs, x->img[i].name);
		cJSON_AddNumberToObject(o, "crc", x->img[i].crc);
		cJSON_AddNumberToObject(o, "w", x->img[i].w);
		cJSON_AddNumberToObject(o, "h", x->img[i].h);
	}
}

/* ---- the whole ---- */

static void export_fn(void *arg)
{
	struct export *x = arg;
	const struct rb3d_model *m = x->m;
	struct model *mdl = x->mdl;

	/* the POL again, with its .opr, as model_load reads them */
	const char *base = strrchr(mdl->path, '\\');
	base = base ? base + 1 : mdl->path;
	struct archive_data *d = RE_get_aar_entry(x->aar, mdl->path, base, ".POL");
	if (!d) {
		snprintf(x->why, sizeof x->why, "%s.POL is not there", m->folder);
		return;
	}
	x->pol = pol_parse(d->data, d->size);
	archive_free_data(d);
	if (!x->pol) {
		snprintf(x->why, sizeof x->why, "%s.POL does not read", m->folder);
		return;
	}
	struct archive_data *opr = RE_get_aar_entry(x->aar, mdl->path, base, ".opr");
	if (opr) {
		opr_load(opr->data, opr->size, x->pol);
		archive_free_data(opr);
	}
	if (mdl->bone_map && (int)x->pol->nr_bones != mdl->nr_bones) {
		snprintf(x->why, sizeof x->why, "the POL has %u bones, the engine's model %d", x->pol->nr_bones, mdl->nr_bones);
		return;
	}
	x->mat_off = xmalloc((x->pol->nr_materials + 1) * sizeof *x->mat_off);
	int off = 0;
	for (uint32_t i = 0; i < x->pol->nr_materials; i++) {
		x->mat_off[i] = off;
		off += x->pol->materials[i].nr_children ? (int)x->pol->materials[i].nr_children : 1;
	}
	if (off != mdl->nr_materials) {
		snprintf(x->why, sizeof x->why, "the POL gives %d materials, the engine's model has %d", off, mdl->nr_materials);
		return;
	}

	struct glb *g = &x->g;
	g->doc = cJSON_CreateObject();
	cJSON *asset = cJSON_AddObjectToObject(g->doc, "asset");
	cJSON_AddStringToObject(asset, "version", "2.0");
	cJSON_AddStringToObject(asset, "generator", x->generator ? x->generator : "Rance Browser");
	cJSON *used = cJSON_AddArrayToObject(g->doc, "extensionsUsed");
	cJSON_AddItemToArray(used, cJSON_CreateString("KHR_materials_unlit"));
	cJSON_AddNumberToObject(g->doc, "scene", 0);
	cJSON *scenes = cJSON_AddArrayToObject(g->doc, "scenes");
	cJSON *scene = cJSON_CreateObject();
	cJSON_AddStringToObject(scene, "name", m->folder);
	cJSON *sn = cJSON_AddArrayToObject(scene, "nodes");
	cJSON_AddItemToArray(sn, cJSON_CreateNumber(0));
	cJSON_AddItemToArray(scenes, scene);
	g->nodes = cJSON_AddArrayToObject(g->doc, "nodes");
	g->meshes = cJSON_AddArrayToObject(g->doc, "meshes");
	g->materials = cJSON_AddArrayToObject(g->doc, "materials");
	g->textures = cJSON_AddArrayToObject(g->doc, "textures");
	g->images = cJSON_AddArrayToObject(g->doc, "images");
	cJSON *samplers = cJSON_AddArrayToObject(g->doc, "samplers");
	cJSON *smp = cJSON_CreateObject();
	cJSON_AddNumberToObject(smp, "magFilter", 9729);       /* LINEAR, as the engine's textures */
	cJSON_AddNumberToObject(smp, "minFilter", 9987);       /* LINEAR_MIPMAP_LINEAR */
	cJSON_AddNumberToObject(smp, "wrapS", 10497);          /* REPEAT */
	cJSON_AddNumberToObject(smp, "wrapT", 10497);
	cJSON_AddItemToArray(samplers, smp);
	g->skins = cJSON_AddArrayToObject(g->doc, "skins");
	g->anims = cJSON_AddArrayToObject(g->doc, "animations");
	g->accessors = cJSON_AddArrayToObject(g->doc, "accessors");
	g->views = cJSON_AddArrayToObject(g->doc, "bufferViews");

	/* the root: the folder's name, with the game's names in extras */
	x->root = cJSON_CreateObject();
	x->root_name = name_take(&x->node_names, m->folder, "", NULL);
	cJSON_AddStringToObject(x->root, "name", x->root_name);
	x->root_kids = cJSON_AddArrayToObject(x->root, "children");
	cJSON_AddItemToArray(g->nodes, x->root);
	g->nnodes = 1;

	build_skeleton(x);
	if (!build_meshes(x))
		return;
	build_animations(x);
	root_extras(x, cJSON_AddObjectToObject(x->root, "extras"));

	/* glTF wants no empty list */
	static const char *const lists[] = { "meshes", "materials", "textures", "images", "skins", "animations",
	                                     "accessors", "bufferViews" };
	for (size_t i = 0; i < sizeof lists / sizeof lists[0]; i++) {
		cJSON *a = cJSON_GetObjectItem(g->doc, lists[i]);
		if (a && !a->child)
			cJSON_DeleteItemFromObject(g->doc, lists[i]);
	}
	if (!x->root_kids->child)
		cJSON_DeleteItemFromObject(x->root, "children");
	if (!g->nimages)
		cJSON_DeleteItemFromObject(g->doc, "samplers");
	if (g->bin.n) {
		bytes_pad(&g->bin, 0);
		cJSON *buffers = cJSON_AddArrayToObject(g->doc, "buffers");
		cJSON *b = cJSON_CreateObject();
		cJSON_AddNumberToObject(b, "byteLength", (double)g->bin.n);
		cJSON_AddItemToArray(buffers, b);
	}

	/* the report */
	if (x->report) {
		cJSON *r = x->report;
		cJSON_AddStringToObject(r, "folder", m->folder);
		cJSON_AddStringToObject(r, "kind", rb3d_kind_folders[m->kind]);
		cJSON_AddStringToObject(r, "root", x->root_name);
		cJSON_AddNumberToObject(r, "meshes", x->meshes);
		int nprims = 0;
		for (int i = 0; i < x->nprim; i++)
			if (x->prim[i].engine_mesh >= 0)
				nprims++;
		cJSON_AddNumberToObject(r, "primitives", nprims);
		cJSON_AddNumberToObject(r, "triangles", x->tris);
		cJSON_AddNumberToObject(r, "vertices", x->verts);
		if (x->walk_meshes) {
			cJSON *w = cJSON_AddObjectToObject(r, "walk");
			cJSON_AddNumberToObject(w, "meshes", x->walk_meshes);
			cJSON_AddNumberToObject(w, "triangles", x->walk_tris);
			cJSON_AddNumberToObject(w, "vertices", x->walk_verts);
		}
		cJSON_AddNumberToObject(r, "materials", g->nmaterials);
		cJSON_AddNumberToObject(r, "images", g->nimages);
		cJSON_AddNumberToObject(r, "bones", mdl->bone_map ? mdl->nr_bones : 0);
		cJSON *an = cJSON_AddArrayToObject(r, "animations");
		for (int i = 0; i < x->nanim; i++) {
			cJSON *o = cJSON_CreateObject();
			cJSON_AddStringToObject(o, "name", x->anim[i].name);
			cJSON_AddStringToObject(o, "file", x->anim[i].file);
			cJSON_AddNumberToObject(o, "frames", x->anim[i].frames);
			cJSON_AddItemToArray(an, o);
		}
		cJSON_AddItemToObject(r, "skipped_motions", cJSON_Duplicate(x->skipped, 1));
		cJSON_AddItemToObject(r, "pictures_missing", cJSON_Duplicate(x->missing, 1));
		cJSON *dr = cJSON_AddObjectToObject(r, "dropped");
		cJSON_AddNumberToObject(dr, "triangles_with_an_index_past_its_list", x->c.tri_index);
		cJSON_AddNumberToObject(dr, "triangles_with_a_number_that_is_not_one", x->c.tri_nan);
		cJSON_AddNumberToObject(dr, "triangles_whose_corners_weld_into_one", x->c.tri_same);
		cJSON_AddNumberToObject(dr, "triangles_given_their_own_points", x->c.tri_copied);
		cJSON_AddNumberToObject(dr, "weights_on_a_bone_the_model_does_not_have", x->c.weights_unknown);
		cJSON_AddNumberToObject(dr, "weights_on_a_bone_named_twice_added", x->c.weights_twice);
		cJSON_AddNumberToObject(dr, "points_with_more_than_four_weights", x->c.points_past_four);
		cJSON_AddNumberToObject(dr, "points_without_a_weight", x->c.points_unweighted);
		cJSON_AddNumberToObject(dr, "normals_of_no_length", x->c.normals_zero);
		cJSON *fc = cJSON_AddObjectToObject(r, "faces");
		cJSON_AddNumberToObject(fc, "checked", x->c.faces);
		cJSON_AddNumberToObject(fc, "against_their_normals", x->c.faces_against);
		cJSON_AddNumberToObject(r, "tolerance", TOLERANCE);
		samples(x, cJSON_AddObjectToObject(r, "samples"));
		/* custom properties the checker finds in Blender */
		cJSON *props = cJSON_AddArrayToObject(r, "properties");
		cJSON *p = cJSON_CreateObject();
		cJSON_AddStringToObject(p, "key", "rq_folder");
		cJSON_AddStringToObject(p, "value", m->folder);
		cJSON_AddItemToArray(props, p);
		if (mdl->bone_map && mdl->nr_bones > 0) {
			p = cJSON_CreateObject();
			cJSON_AddStringToObject(p, "bone", x->joint_name[0]);
			cJSON_AddStringToObject(p, "key", "rq_bone_id");
			cJSON_AddNumberToObject(p, "value", x->joint_id[0]);
			cJSON_AddItemToArray(props, p);
			/* each bone the export renamed keeps the game's name */
			for (int i = 0; i < mdl->nr_bones; i++) {
				cJSON *jn = cJSON_GetArrayItem(g->nodes, x->joint_node[i]);
				cJSON *jx = jn ? cJSON_GetObjectItem(jn, "extras") : NULL;
				cJSON *gn = jx ? cJSON_GetObjectItem(jx, "rq_name") : NULL;
				if (!cJSON_IsString(gn))
					continue;
				p = cJSON_CreateObject();
				cJSON_AddStringToObject(p, "bone", x->joint_name[i]);
				cJSON_AddStringToObject(p, "key", "rq_name");
				cJSON_AddStringToObject(p, "value", gn->valuestring);
				cJSON_AddItemToArray(props, p);
			}
		}
		for (int i = 0; i < x->nprim; i++)
			if (x->prim[i].engine_mesh >= 0) {
				p = cJSON_CreateObject();
				cJSON_AddStringToObject(p, "object", x->prim[i].node);
				cJSON_AddStringToObject(p, "key", "rq_name");
				char *gn = utf(x->pol->meshes[x->prim[i].pol_mesh]->name);
				cJSON_AddStringToObject(p, "value", gn);
				free(gn);
				cJSON_AddItemToArray(props, p);
				break;
			}
		if (x->nmat) {
			p = cJSON_CreateObject();
			cJSON_AddStringToObject(p, "material", x->mat[0].name);
			cJSON_AddStringToObject(p, "key", "rq_material");
			cJSON *mo = cJSON_GetArrayItem(g->materials, x->mat[0].index);
			cJSON *ex = mo ? cJSON_GetObjectItem(mo, "extras") : NULL;
			cJSON *v = ex ? cJSON_GetObjectItem(ex, "rq_material") : NULL;
			cJSON_AddStringToObject(p, "value", v && v->valuestring ? v->valuestring : "");
			cJSON_AddItemToArray(props, p);
		}
	}
	x->ok = true;
}

static bool write_glb(const char *path, const char *json, const struct bytes *bin, char *err, size_t n)
{
	size_t jlen = strlen(json), jpad = (4 - jlen % 4) % 4;
	size_t blen = bin->n, bpad = (4 - blen % 4) % 4;
	uint64_t total = 12 + 8 + (uint64_t)jlen + jpad + (blen ? 8 + (uint64_t)blen + bpad : 0);
	if (total > 0xFFFFFFFFu) {
		snprintf(err, n, "%s: the model is too big for a .glb file", path);
		return false;
	}
	FILE *f = file_open_utf8(path, "wb");
	if (!f) {
		snprintf(err, n, "%s: could not open it to write", path);
		return false;
	}
	uint32_t head[3] = { 0x46546C67u, 2, (uint32_t)total };
	uint32_t jh[2] = { (uint32_t)(jlen + jpad), 0x4E4F534Au };
	fwrite(head, 4, 3, f);
	fwrite(jh, 4, 2, f);
	fwrite(json, 1, jlen, f);
	fwrite("   ", 1, jpad, f);
	if (blen) {
		uint32_t bh[2] = { (uint32_t)(blen + bpad), 0x004E4942u };
		fwrite(bh, 4, 2, f);
		fwrite(bin->p, 1, blen, f);
		fwrite("\0\0\0", 1, bpad, f);
	}
	bool ok = !ferror(f);
	if (fclose(f) != 0)
		ok = false;
	if (!ok)
		snprintf(err, n, "%s: could not write it all", path);
	return ok;
}

/* What RE_build_model changes on the model's instance (update_bones: its skin matrices, its sphere
 * and its shadow volume's place; its texture index), kept before the samples pose it. */
static void save_pose(struct export *x)
{
	struct RE_instance *inst = x->inst;
	int nb = inst->model->nr_bones;
	if (nb > 0 && inst->bone_transforms) {
		x->saved_bones = xmalloc(nb * sizeof(mat3x4));
		memcpy(x->saved_bones, inst->bone_transforms, nb * sizeof(mat3x4));
	}
	glm_vec4_copy(inst->bounding_sphere, x->saved_sphere);
	x->saved_texture_index = inst->texture_animation_index;
	if (inst->shadow_volume_instance) {
		glm_vec3_copy(inst->shadow_volume_instance->pos, x->saved_shadow_pos);
		glm_vec3_copy(inst->shadow_volume_instance->scale, x->saved_shadow_scale);
	}
}

/* The instance back as the view had it: the same pose drawn, whether a motion plays or not. */
static void restore_fn(void *arg)
{
	struct export *x = arg;
	struct RE_instance *inst = x->inst;
	int nb = inst->model->nr_bones;
	if (x->saved_bones) {
		memcpy(inst->bone_transforms, x->saved_bones, nb * sizeof(mat3x4));
		if (inst->bone_transforms_ubo) {
			glBindBuffer(GL_UNIFORM_BUFFER, inst->bone_transforms_ubo);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, nb * sizeof(mat3x4), inst->bone_transforms);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}
	}
	glm_vec4_copy(x->saved_sphere, inst->bounding_sphere);
	inst->texture_animation_index = x->saved_texture_index;
	struct RE_instance *sh = inst->shadow_volume_instance;
	if (sh) {
		glm_vec3_copy(x->saved_shadow_pos, sh->pos);
		glm_vec3_copy(x->saved_shadow_scale, sh->scale);
		RE_instance_update_local_transform(sh);
	}
}

static void export_free(struct export *x)
{
	for (int i = 0; i < x->nprim; i++) {
		free(x->prim[i].v);
		free(x->prim[i].corner);
		free(x->prim[i].idx);
		free(x->prim[i].engine);
	}
	free(x->prim);
	for (int i = 0; i < x->nimg; i++)
		free(x->img[i].key);
	free(x->img);
	free(x->mat);
	for (int i = 0; i < x->nanim; i++) {
		free(x->anim[i].file);
		motion_free(x->anim[i].mo);
	}
	free(x->anim);
	free(x->joint_node);
	free(x->joint_name);
	free(x->joint_id);
	free(x->mat_off);
	free(x->saved_bones);
	if (x->pol)
		pol_free(x->pol);
	cJSON_Delete(x->g.doc);
	cJSON_Delete(x->skipped);
	cJSON_Delete(x->missing);
	free(x->g.bin.p);
	names_free(&x->node_names);
	names_free(&x->mesh_names);
	names_free(&x->mat_names);
	names_free(&x->img_names);
	names_free(&x->anim_names);
	names_free(&x->joint_names);
}

bool rb3d_export_glb(struct rb3d_view *v, const struct rb3d_list *l, const struct rb3d_model *m,
                     const char *path, const char *generator, cJSON *report, char *err, size_t n)
{
	struct RE_instance *inst = rb3d_view_instance(v);
	if (!inst || !inst->model) {
		snprintf(err, n, "no model is loaded");
		return false;
	}
	struct export x;
	memset(&x, 0, sizeof x);
	x.v = v;
	x.l = l;
	x.m = m;
	x.generator = generator;
	x.p = rb3d_view_plugin(v);
	x.inst = inst;
	x.mdl = inst->model;
	x.aar = x.p->aar;
	x.walk_mat = -1;
	x.report = report;
	x.skipped = cJSON_CreateArray();
	x.missing = cJSON_CreateArray();
	save_pose(&x);
	struct motion *saved = inst->motion;
	struct motion *saved_next = inst->next_motion;
	inst->next_motion = NULL;
	bool ok = rb3d_guard(export_fn, &x, err, n);
	inst->motion = saved;
	inst->next_motion = saved_next;
	char err2[256];
	rb3d_guard(restore_fn, &x, err2, sizeof err2);
	if (!ok) {
		/* a fatal error of the 3D code: what the export holds is whole as far as it went */
		export_free(&x);
		return false;
	}
	if (!x.ok) {
		snprintf(err, n, "%s", x.why[0] ? x.why : "it could not be exported");
		export_free(&x);
		return false;
	}
	char *json = cJSON_PrintUnformatted(x.g.doc);
	ok = json && (!path || write_glb(path, json, &x.g.bin, err, n));
	if (!json)
		snprintf(err, n, "the file's JSON could not be made");
	free(json);
	export_free(&x);
	return ok;
}
