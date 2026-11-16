/* Pictures: GL textures, the thumbnail thread, and saving PNGs.
 *
 * Thumbnails are made on a thread of their own with its own archive handles, newest request
 * first, so the rows on screen come first while the list scrolls. The main thread uploads them
 * and forgets the ones not drawn for the longest when there are too many. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_opengl.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"

#include "browser.h"

/* Thumbnails are at most THUMB pixels, 96 at the display's 100 % (set by pics_init). */
static int THUMB = 96;
#define MAX_REQ 256
#define MAX_THUMBS 1500

struct result {
	int kind, i, w, h;
	uint8_t *rgba;        /* NULL: it could not be read */
	struct result *next;
};

static struct thumb *thumbs[PK_NKINDS];
static int nthumbs[PK_NKINDS];
static int live;

static SDL_mutex *lock;
static SDL_cond *wake;
static int req_kind[MAX_REQ], req_i[MAX_REQ], nreq;
static struct result *done;
static int busy;
static struct handles wh;   /* the thread's own archives */

ImTextureRef_c texref(unsigned tex)
{
	ImTextureRef_c r = { NULL, (ImTextureID)tex };
	return r;
}

unsigned tex_from_rgba(const uint8_t *rgba, int w, int h)
{
	GLuint t = 0;
	if (!rgba || w <= 0 || h <= 0)
		return 0;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glBindTexture(GL_TEXTURE_2D, 0);
	return t;
}

void tex_free(unsigned tex)
{
	GLuint t = tex;
	if (t)
		glDeleteTextures(1, &t);
}

/* Two greys in a 2 by 2 pattern, repeated under see-through pictures. */
/* A checkerboard under see-through pixels, drawn as rectangles inside the draw list's clip: the
 * OpenGL 3 backend binds its own sampler (smooth, clamped at the edges) for every texture, so a
 * repeating checker texture comes out as one blur. */
void draw_checker(ImDrawList *dl, V2 a, V2 b, float cell, unsigned dark, unsigned light)
{
	V2 cmin = ImDrawList_GetClipRectMin(dl), cmax = ImDrawList_GetClipRectMax(dl);
	float x0 = fmaxf(a.x, cmin.x), y0 = fmaxf(a.y, cmin.y);
	float x1 = fminf(b.x, cmax.x), y1 = fminf(b.y, cmax.y);
	if (x1 <= x0 || y1 <= y0)
		return;
	ImDrawList_AddRectFilled(dl, v2(x0, y0), v2(x1, y1), dark, 0, 0);
	int i0 = (int)((x0 - a.x) / cell), j0 = (int)((y0 - a.y) / cell);
	for (int j = j0; a.y + j * cell < y1; j++) {
		for (int i = i0 + ((i0 + j) & 1); a.x + i * cell < x1; i += 2) {
			V2 p = v2(fmaxf(a.x + i * cell, x0), fmaxf(a.y + j * cell, y0));
			V2 q = v2(fminf(a.x + (i + 1) * cell, x1), fminf(a.y + (j + 1) * cell, y1));
			if (q.x > p.x && q.y > p.y)
				ImDrawList_AddRectFilled(dl, p, q, light, 0, 0);
		}
	}
}

/* Sharp pixels from here on in this draw list (a picture zoomed in), and back to smooth. */
void draw_sampler_nearest(ImDrawList *dl, bool nearest)
{
	ImGuiPlatformIO *pio = igGetPlatformIO_Nil();
	ImDrawCallback cb = nearest ? pio->DrawCallback_SetSamplerNearest : pio->DrawCallback_SetSamplerLinear;
	if (cb)
		ImDrawList_AddCallback(dl, cb, NULL, 0);
}

/* A picture inside a box x box square at pos, kept to its shape and centred. */
void draw_fit_image(unsigned tex, int w, int h, V2 pos, float box)
{
	if (!tex || w <= 0 || h <= 0)
		return;
	float s = box / (float)(w > h ? w : h);
	if (s > 1.0f && w >= 24 && h >= 24)
		s = s > 2.0f ? 2.0f : s;
	float dw = w * s, dh = h * s;
	V2 a = v2(pos.x + (box - dw) / 2, pos.y + (box - dh) / 2);
	V2 b = v2(a.x + dw, a.y + dh);
	ImDrawList *dl = igGetWindowDrawList();
	draw_checker(dl, a, b, S(6), 0x40666666, 0x40999999);
	ImDrawList_AddImage(dl, texref(tex), a, b, v2(0, 0), v2(1, 1), 0xffffffff);
}

/* A smaller copy, at most THUMB on its longer side. Colours are weighted by alpha, so a
 * see-through edge does not darken. */
static uint8_t *shrink(const uint8_t *src, int w, int h, int *ow, int *oh)
{
	float s = (float)(w > h ? w : h) / THUMB;
	if (s < 1)
		s = 1;
	int dw = (int)(w / s + 0.5f), dh = (int)(h / s + 0.5f);
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	uint8_t *out = xmalloc((size_t)dw * dh * 4);
	for (int y = 0; y < dh; y++) {
		int y0 = (int)((int64_t)y * h / dh), y1 = (int)((int64_t)(y + 1) * h / dh);
		if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < dw; x++) {
			int x0 = (int)((int64_t)x * w / dw), x1 = (int)((int64_t)(x + 1) * w / dw);
			if (x1 <= x0) x1 = x0 + 1;
			uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
			for (int yy = y0; yy < y1; yy++) {
				const uint8_t *p = src + ((size_t)yy * w + x0) * 4;
				for (int xx = x0; xx < x1; xx++, p += 4) {
					r += p[0] * p[3];
					g += p[1] * p[3];
					b += p[2] * p[3];
					a += p[3];
					n++;
				}
			}
			uint8_t *d = out + ((size_t)y * dw + x) * 4;
			d[0] = a ? (uint8_t)(r / a) : 0;
			d[1] = a ? (uint8_t)(g / a) : 0;
			d[2] = a ? (uint8_t)(b / a) : 0;
			d[3] = (uint8_t)(a / n);
		}
	}
	*ow = dw;
	*oh = dh;
	return out;
}

static int worker(void *arg)
{
	(void)arg;
	handles_open(&wh, G.dir);
	for (;;) {
		SDL_LockMutex(lock);
		while (!nreq)
			SDL_CondWait(wake, lock);
		nreq--;
		int kind = req_kind[nreq], i = req_i[nreq];
		busy = 1;
		SDL_UnlockMutex(lock);

		struct result *r = xcalloc(1, sizeof *r);
		r->kind = kind;
		r->i = i;
		struct cg *cg = game_load_pic(&wh, kind, i);
		if (cg && cg->pixels && cg->metrics.w > 0 && cg->metrics.h > 0)
			r->rgba = shrink(cg->pixels, cg->metrics.w, cg->metrics.h, &r->w, &r->h);
		if (cg)
			cg_free(cg);

		SDL_LockMutex(lock);
		r->next = done;
		done = r;
		busy = 0;
		SDL_UnlockMutex(lock);
	}
	return 0;
}

void pics_init(void)
{
	THUMB = (int)(96 * (UI.scale > 1 ? UI.scale : 1) + 0.5f);
	if (THUMB > 192)
		THUMB = 192;
	for (int k = 0; k < PK_NKINDS; k++)
		nthumbs[k] = pic_count(k);
	for (int k = 0; k < PK_NKINDS; k++)
		thumbs[k] = xcalloc(nthumbs[k] ? nthumbs[k] : 1, sizeof **thumbs);
	lock = SDL_CreateMutex();
	wake = SDL_CreateCond();
	SDL_DetachThread(SDL_CreateThread(worker, "thumbs", NULL));
}

struct thumb *pics_thumb(int kind, int i)
{
	static struct thumb none;
	if (kind < 0 || kind >= PK_NKINDS || i < 0 || i >= nthumbs[kind])
		return &none;
	struct thumb *t = &thumbs[kind][i];
	t->last = UI.frame;
	if (t->state == 0) {
		SDL_LockMutex(lock);
		if (nreq == MAX_REQ) {
			/* The oldest request is likely off screen by now: drop it. */
			thumbs[req_kind[0]][req_i[0]].state = 0;
			memmove(req_kind, req_kind + 1, (MAX_REQ - 1) * sizeof *req_kind);
			memmove(req_i, req_i + 1, (MAX_REQ - 1) * sizeof *req_i);
			nreq--;
		}
		req_kind[nreq] = kind;
		req_i[nreq] = i;
		nreq++;
		t->state = 1;
		SDL_CondSignal(wake);
		SDL_UnlockMutex(lock);
	}
	return t;
}

void pics_frame(void)
{
	SDL_LockMutex(lock);
	struct result *r = done;
	done = NULL;
	SDL_UnlockMutex(lock);
	while (r) {
		struct result *next = r->next;
		struct thumb *t = &thumbs[r->kind][r->i];
		if (r->rgba) {
			t->tex = tex_from_rgba(r->rgba, r->w, r->h);
			t->w = r->w;
			t->h = r->h;
			t->state = 2;
			live++;
			free(r->rgba);
		} else {
			t->state = 3;
		}
		free(r);
		r = next;
	}
	/* Too many: forget the ones drawn longest ago, a few each frame. */
	for (int pass = 0; live > MAX_THUMBS && pass < 64; pass++) {
		struct thumb *old = NULL;
		for (int k = 0; k < PK_NKINDS; k++)
			for (int i = 0; i < nthumbs[k]; i++) {
				struct thumb *t = &thumbs[k][i];
				if (t->state == 2 && t->last + 2 < UI.frame && (!old || t->last < old->last))
					old = t;
			}
		if (!old)
			break;
		tex_free(old->tex);
		old->tex = 0;
		old->state = 0;
		live--;
	}
}

bool pics_idle(void)
{
	SDL_LockMutex(lock);
	bool idle = !nreq && !busy && !done;
	SDL_UnlockMutex(lock);
	return idle;
}

bool save_png(const char *path, const uint8_t *rgba, int w, int h)
{
	struct cg cg = { 0 };
	cg.metrics.w = w;
	cg.metrics.h = h;
	cg.metrics.bpp = 32;
	cg.metrics.has_pixel = true;
	cg.metrics.has_alpha = true;
	cg.metrics.pixel_pitch = w * 4;
	cg.pixels = (void *)rgba;
	FILE *f = file_open_utf8(path, "wb");
	if (!f)
		return false;
	int ok = cg_write(&cg, ALCG_PNG, f);
	fclose(f);
	return ok != 0;
}

bool save_cg_png(const char *path, struct cg *cg)
{
	return cg && save_png(path, cg->pixels, cg->metrics.w, cg->metrics.h);
}

bool save_bytes(const char *path, const uint8_t *data, size_t size)
{
	FILE *f = file_open_utf8(path, "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, size, f) == size;
	return fclose(f) == 0 && ok;
}
