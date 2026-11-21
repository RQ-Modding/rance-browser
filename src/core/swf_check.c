/* Copyright (C) 2026 Rance Quest modding fork
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

/*
 * The rules of src/swf.c (what it reads) and src/parts/flash.c and render.c (what they play),
 * in two passes. The first reads every tag as swf_load does, sprites' own tags too; what
 * swf_load stops on closes the game as the file is read (frame 0). The second walks the main
 * timeline as parts_flash_seek does, frame by frame, with the characters defined so far and
 * the objects on each depth; what it stops on closes the game when that frame plays. A jump
 * of gotoFrame is not followed: every tag is walked once, in order.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "swf_check.h"

#define FWS_MAX (512u * 1024u * 1024u)
#define NEST_MAX 8
#define SIDE_WARN 8192

enum {
	T_END = 0, T_SHOW_FRAME = 1, T_DEFINE_SHAPE = 2, T_SET_BACKGROUND_COLOR = 9,
	T_DO_ACTION = 12, T_DEFINE_SOUND = 14, T_START_SOUND = 15, T_DEFINE_BITS_LOSSLESS = 20,
	T_PLACE_OBJECT2 = 26, T_REMOVE_OBJECT2 = 28, T_DEFINE_BITS_LOSSLESS2 = 36,
	T_DEFINE_SPRITE = 39, T_SOUND_STREAM_HEAD2 = 45, T_FILE_ATTRIBUTES = 69, T_PLACE_OBJECT3 = 70,
};

enum {
	A_END = 0x00, A_PLAY = 0x06, A_STOP = 0x07, A_POP = 0x17, A_CALL_FUNCTION = 0x3d,
	A_GOTO_FRAME = 0x81, A_CONSTANT_POOL = 0x88, A_PUSH = 0x96,
};

#define PF_MOVE         0x0001
#define PF_CHARACTER    0x0002
#define PF_MATRIX       0x0004
#define PF_CXFORM       0x0008
#define PF_RATIO        0x0010
#define PF_NAME         0x0020
#define PF_CLIP_DEPTH   0x0040
#define PF_CLIP_ACTIONS 0x0080
#define PF_FILTERS      0x0100
#define PF_BLEND        0x0200
#define PF_CACHE        0x0400
#define PF_RESERVED     0xf800

#define CX_ADD        1 /* adds to a colour */
#define CX_MULT       2 /* multiplies a colour */
#define CX_ALPHA_ADD  4 /* adds to alpha */
#define CX_ALPHA_MULT 8 /* multiplies alpha */

#define SAY_ACTIONS "It runs only stop(), play(), and gotoAndPlay or gotoAndStop with a frame number."
#define SAY_BUILD "export the frames as pictures and use rqmod-aff build"

/* ---- names ---- */

static const struct { int code; const char *name; } tag_names[] = {
	{ 0, "End" }, { 1, "ShowFrame" }, { 2, "DefineShape" }, { 4, "PlaceObject" },
	{ 5, "RemoveObject" }, { 6, "DefineBits" }, { 7, "DefineButton" }, { 8, "JPEGTables" },
	{ 9, "SetBackgroundColor" }, { 10, "DefineFont" }, { 11, "DefineText" }, { 12, "DoAction" },
	{ 13, "DefineFontInfo" }, { 14, "DefineSound" }, { 15, "StartSound" },
	{ 17, "DefineButtonSound" }, { 18, "SoundStreamHead" }, { 19, "SoundStreamBlock" },
	{ 20, "DefineBitsLossless" }, { 21, "DefineBitsJPEG2" }, { 22, "DefineShape2" },
	{ 23, "DefineButtonCxform" }, { 24, "Protect" }, { 26, "PlaceObject2" },
	{ 28, "RemoveObject2" }, { 32, "DefineShape3" }, { 33, "DefineText2" },
	{ 34, "DefineButton2" }, { 35, "DefineBitsJPEG3" }, { 36, "DefineBitsLossless2" },
	{ 37, "DefineEditText" }, { 39, "DefineSprite" }, { 41, "ProductInfo" },
	{ 43, "FrameLabel" }, { 45, "SoundStreamHead2" }, { 46, "DefineMorphShape" },
	{ 48, "DefineFont2" }, { 56, "ExportAssets" }, { 57, "ImportAssets" },
	{ 58, "EnableDebugger" }, { 59, "DoInitAction" }, { 60, "DefineVideoStream" },
	{ 61, "VideoFrame" }, { 62, "DefineFontInfo2" }, { 63, "DebugID" },
	{ 64, "EnableDebugger2" }, { 65, "ScriptLimits" }, { 66, "SetTabIndex" },
	{ 69, "FileAttributes" }, { 70, "PlaceObject3" }, { 71, "ImportAssets2" },
	{ 72, "DoABC" }, { 73, "DefineFontAlignZones" }, { 74, "CSMTextSettings" },
	{ 75, "DefineFont3" }, { 76, "SymbolClass" }, { 77, "Metadata" },
	{ 78, "DefineScalingGrid" }, { 82, "DoABC" }, { 83, "DefineShape4" },
	{ 84, "DefineMorphShape2" }, { 86, "DefineSceneAndFrameLabelData" },
	{ 87, "DefineBinaryData" }, { 88, "DefineFontName" }, { 89, "StartSound2" },
	{ 90, "DefineBitsJPEG4" }, { 91, "DefineFont4" }, { 93, "EnableTelemetry" },
	{ 94, "PlaceObject4" },
};

const char *swfc_tag_name(int code)
{
	size_t i;

	for (i = 0; i < sizeof tag_names / sizeof tag_names[0]; i++) {
		if (tag_names[i].code == code)
			return tag_names[i].name;
	}
	return NULL;
}

static const char *tag_name(int code)
{
	const char *n = swfc_tag_name(code);
	return n ? n : "an unknown tag";
}

/* What a tag the game does not play is, and what to do instead. */
static const char *tag_hint(int code)
{
	switch (code) {
	case 22: case 32: case 83: case 46: case 84:
		return "It is a vector shape: Rance Quest plays only rectangles filled with a picture; " SAY_BUILD ".";
	case 6: case 8: case 21: case 35: case 90:
		return "It is a JPEG picture: save the pictures lossless (32-bit), or " SAY_BUILD ".";
	case 10: case 11: case 13: case 33: case 37: case 48: case 62: case 73: case 74: case 75:
	case 88: case 91:
		return "It is text: draw the text into the pictures.";
	case 7: case 17: case 23: case 34:
		return "It is a button: the game's animations take no input.";
	case 18: case 19:
		return "It is a stream sound (Sync: Stream): use an event sound, or rqmod-aff build --sound.";
	case 72: case 76: case 82: case 87:
		return "It is ActionScript 3: publish for ActionScript 2, with only stop(), play() and gotoAndPlay or gotoAndStop by number.";
	case 4: case 5:
		return "It is from an old Flash: publish for Flash Player 8 or later.";
	case 43:
		return "It is a frame label: remove the labels (the game goes to frames by number).";
	case 24: case 41: case 58: case 63: case 64: case 65: case 66: case 77: case 78: case 86: case 93:
		return "It only describes the file: publish without metadata, debugging or protection.";
	case 60: case 61:
		return "It is video: " SAY_BUILD ".";
	case 56: case 57: case 71:
		return "It shares symbols with another file: put everything in this one.";
	case 59:
		return "It is a sprite's own script: remove it.";
	case 89:
		return "It starts a sound by class name: start it by its id (StartSound).";
	}
	return "Rance Quest does not play it.";
}

const char *swfc_blend_name(int mode)
{
	static const char *const names[] = {
		"normal", "normal", "layer", "multiply", "screen", "lighten", "darken", "difference",
		"add", "subtract", "invert", "alpha", "erase", "overlay", "hardlight",
	};
	return mode >= 0 && mode < 15 ? names[mode] : "unknown";
}

static const char *action_name(int code)
{
	switch (code) {
	case 0x04: return "nextFrame";
	case 0x05: return "prevFrame";
	case 0x08: return "toggleHighQuality";
	case 0x09: return "stopAllSounds";
	case 0x1c: return "a variable read";
	case 0x1d: return "a variable set";
	case 0x26: return "trace";
	case 0x83: return "getURL";
	case 0x87: return "a register store";
	case 0x8a: case 0x8d: return "ifFrameLoaded";
	case 0x8b: return "tellTarget";
	case 0x8c: return "a gotoAndPlay by label";
	case 0x8e: case 0x9b: return "a function";
	case 0x94: return "with";
	case 0x99: return "a jump";
	case 0x9a: return "getURL";
	case 0x9d: return "an if";
	case 0x9e: return "a call";
	case 0x9f: return "a gotoAndPlay by expression";
	}
	return "a script";
}

static const char *sound_format_name(int f)
{
	switch (f) {
	case 0: return "uncompressed in the machine's byte order (format 0)";
	case 1: return "ADPCM (format 1)";
	case 2: return "MP3 (format 2)";
	case 3: return "uncompressed PCM (format 3)";
	case 4: case 5: case 6: return "Nellymoser";
	case 11: return "Speex (format 11)";
	}
	return "an unknown sound format";
}

/* ---- the problems ---- */

/* gnu_printf on MinGW: the UCRT's printf takes %zu, which MinGW's ms_printf check does not know. */
#ifdef __MINGW32__
#define SWFC_PRINTF __attribute__((format(gnu_printf, 6, 7)))
#else
#define SWFC_PRINTF __attribute__((format(printf, 6, 7)))
#endif

static void say(struct swfc *c, int level, int frame, int tag, int code, const char *fmt, ...) SWFC_PRINTF;

static void say(struct swfc *c, int level, int frame, int tag, int code, const char *fmt, ...)
{
	char text[sizeof c->items[0].text];
	struct swfc_item *it;
	va_list ap;
	int i;

	va_start(ap, fmt);
	vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	/* The same rule broken again (on every frame, say) is one problem with a count. */
	for (i = 0; i < c->nitems; i++) {
		if (c->items[i].level == level && !strcmp(c->items[i].text, text)) {
			c->items[i].count++;
			return;
		}
	}
	if (level == SWFC_ERROR)
		c->errors++;
	else
		c->warnings++;
	if (c->nitems >= SWFC_ITEMS_MAX) {
		c->more++;
		return;
	}
	if (c->nitems == c->cap) {
		int cap = c->cap ? c->cap * 2 : 16;
		struct swfc_item *n = realloc(c->items, (size_t)cap * sizeof *n);
		if (!n) {
			c->more++;
			return;
		}
		c->items = n;
		c->cap = cap;
	}
	it = &c->items[c->nitems++];
	it->level = level;
	it->frame = frame;
	it->tag = tag;
	it->code = code;
	it->count = 1;
	memcpy(it->text, text, sizeof text);
}

/* ---- reading ---- */

struct rd {
	const uint8_t *p;
	size_t n, i;
	int bad; /* a read past the end */
};

static unsigned u8(struct rd *r)
{
	if (r->i >= r->n) {
		r->bad = 1;
		return 0;
	}
	return r->p[r->i++];
}

static unsigned u16(struct rd *r)
{
	unsigned a = u8(r);
	return a | u8(r) << 8;
}

static uint32_t u32(struct rd *r)
{
	uint32_t a = u16(r);
	return a | (uint32_t)u16(r) << 16;
}

static size_t left(const struct rd *r)
{
	return r->i < r->n ? r->n - r->i : 0;
}

static void skip_string(struct rd *r)
{
	while (r->i < r->n && r->p[r->i])
		r->i++;
	if (r->i >= r->n)
		r->bad = 1;
	else
		r->i++;
}

/* Bit fields, most significant bit first; each structure starts on a byte. */
struct bits {
	struct rd *r;
	unsigned byte;
	int left;
};

static uint32_t ub(struct bits *b, int n)
{
	uint32_t v = 0;

	while (n-- > 0) {
		if (!b->left) {
			b->byte = u8(b->r);
			b->left = 8;
		}
		b->left--;
		v = v << 1 | ((b->byte >> b->left) & 1);
	}
	return v;
}

static int32_t sb(struct bits *b, int n)
{
	uint32_t v = ub(b, n);

	if (n > 0 && n < 32 && (v >> (n - 1) & 1))
		v |= ~0u << n;
	return (int32_t)v;
}

struct matrix {
	int32_t sx, sy, r0, r1, tx, ty;
};

static void read_rect(struct rd *r, int32_t *x0, int32_t *x1, int32_t *y0, int32_t *y1)
{
	struct bits b = { r, 0, 0 };
	int n = (int)ub(&b, 5);

	*x0 = sb(&b, n);
	*x1 = sb(&b, n);
	*y0 = sb(&b, n);
	*y1 = sb(&b, n);
}

static void read_matrix(struct rd *r, struct matrix *m)
{
	struct bits b = { r, 0, 0 };
	int n;

	m->sx = m->sy = 1 << 16;
	m->r0 = m->r1 = 0;
	if (ub(&b, 1)) {
		n = (int)ub(&b, 5);
		m->sx = sb(&b, n);
		m->sy = sb(&b, n);
	}
	if (ub(&b, 1)) {
		n = (int)ub(&b, 5);
		m->r0 = sb(&b, n);
		m->r1 = sb(&b, n);
	}
	n = (int)ub(&b, 5);
	m->tx = sb(&b, n);
	m->ty = sb(&b, n);
}

/* A colour transform with alpha: which of its terms change anything (CX_*). */
static int read_cxform(struct rd *r)
{
	struct bits b = { r, 0, 0 };
	int has_add = (int)ub(&b, 1), has_mult = (int)ub(&b, 1), n = (int)ub(&b, 4), i, cx = 0;

	for (i = 0; has_mult && i < 4; i++) {
		if (sb(&b, n) != 256)
			cx |= i < 3 ? CX_MULT : CX_ALPHA_MULT;
	}
	for (i = 0; has_add && i < 4; i++) {
		if (sb(&b, n) != 0)
			cx |= i < 3 ? CX_ADD : CX_ALPHA_ADD;
	}
	return cx;
}

/* ---- the first pass: every tag, as swf_load reads it ---- */

struct act {
	uint8_t code;
	int arg; /* GotoFrame's frame */
};

struct ptag {
	int code, index;
	int id;                    /* the character it defines; StartSound: the sound it starts */
	int fmt, w, h;             /* a bitmap's format and size; a sound's format */
	int bitmap;                /* a shape's picture */
	int32_t bx0, bx1, by0, by1;
	struct matrix fm;          /* a shape's fill */
	unsigned flags;            /* PlaceObject's; StartSound's */
	int depth, character, blend, cx;
	int frames;                /* a sprite's frame count */
	struct ptag *inner;        /* a sprite's tags */
	int ninner;
	struct act *acts;          /* a DoAction's actions */
	int nacts;
	int rate, bits, chans;     /* a sound */
	uint32_t samples, bytes;
	int bad;                   /* the game closes as it reads this tag (already said) */
};

struct parse {
	struct swfc *c;
	int index;
};

static void free_tags(struct ptag *t, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		free_tags(t[i].inner, t[i].ninner);
		free(t[i].acts);
	}
	free(t);
}

static void read_bitmap(struct parse *ps, struct rd *r, struct ptag *t)
{
	struct swfc *c = ps->c;
	const char *v2 = t->code == T_DEFINE_BITS_LOSSLESS2 ? "2" : "";
	uLongf want, got;
	uint8_t *px;
	size_t i, cells;
	int soft = 0, premul = 1;

	t->id = (int)u16(r);
	t->fmt = (int)u8(r);
	t->w = (int)u16(r);
	t->h = (int)u16(r);
	if (r->bad)
		return;
	c->bitmaps++;
	if (t->fmt != 5) {
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "Bitmap %d (DefineBitsLossless%s) has format %d (%s): the game closes when it reads the file. "
		    "It reads only 32-bit pictures (format 5); save the pictures as 32-bit, or " SAY_BUILD ".",
		    t->id, v2, t->fmt, t->fmt == 3 ? "a palette" : t->fmt == 4 ? "15-bit colour" : "unknown");
		t->bad = 1;
		return;
	}
	cells = (size_t)t->w * t->h;
	if (!cells) {
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "Bitmap %d is %d x %d pixels: the game closes when it reads the file.", t->id, t->w, t->h);
		t->bad = 1;
		return;
	}
	if (t->w > SIDE_WARN || t->h > SIDE_WARN)
		say(c, SWFC_WARNING, 0, t->index, t->code,
		    "Bitmap %d is %d x %d pixels: a picture over %d pixels a side may not draw on every graphics card.",
		    t->id, t->w, t->h, SIDE_WARN);
	c->pixels += cells;
	want = (uLongf)(cells * 4);
	px = malloc(want ? want : 1);
	got = want;
	if (!px || uncompress(px, &got, r->p + r->i, (uLong)left(r)) != Z_OK || got != want) {
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "Bitmap %d's data does not unpack to its %d x %d pixels: the game closes when it reads the file. "
		    "Export it again.", t->id, t->w, t->h);
		t->bad = 1;
		free(px);
		return;
	}
	r->i = r->n;
	/* A Lossless2 holds its colours multiplied by alpha (Flash writes them so, and every file of
	 * the game), and the engine divides them back since 2.63.0. A clear pixel's colour is not
	 * drawn, so only the others count. */
	if (t->code == T_DEFINE_BITS_LOSSLESS2) {
		for (i = 0; i < cells; i++) {
			const uint8_t *p = px + i * 4;
			if (p[0] && p[0] != 255)
				soft = 1;
			if (p[0] && (p[1] > p[0] || p[2] > p[0] || p[3] > p[0]))
				premul = 0;
		}
		if (soft) {
			c->soft_bitmaps++;
			if (premul)
				c->premul_bitmaps++;
			else
				say(c, SWFC_WARNING, 0, t->index, t->code,
				    "Bitmap %d's colours are not multiplied by its alpha, as a SWF holds them: the game "
				    "divides them back, so its soft edges come out too bright. Export it from a Flash "
				    "program, or " SAY_BUILD ".", t->id);
		}
	}
	free(px);
}

static void read_shape(struct parse *ps, struct rd *r, struct ptag *t)
{
	struct swfc *c = ps->c;
	struct bits b;
	struct { int bitmap; struct matrix m; } fills[256];
	unsigned count, i, lines, fill_bits, style, fill;
	int fs0, fs1, edge;

	t->id = (int)u16(r);
	read_rect(r, &t->bx0, &t->bx1, &t->by0, &t->by1);
	count = u8(r);
	for (i = 0; i < count && !r->bad; i++) {
		unsigned type = u8(r);
		if (type != 0x41 && type != 0x43) {
			say(c, SWFC_ERROR, 0, t->index, t->code,
			    "Shape %d is filled with %s: the game closes when it reads the file. It plays only "
			    "shapes filled with a picture that does not repeat; " SAY_BUILD ".",
			    t->id, type == 0 ? "a colour" : type >= 0x10 && type <= 0x13 ? "a gradient"
			    : type == 0x40 || type == 0x42 ? "a repeating picture" : "an unknown fill");
			t->bad = 1;
			return;
		}
		fills[i].bitmap = (int)u16(r);
		read_matrix(r, &fills[i].m);
	}
	lines = u8(r);
	if (r->bad)
		return;
	c->shapes++;
	if (lines) {
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "Shape %d has lines: the game closes when it reads the file. It plays only rectangles "
		    "filled with a picture; draw the lines into the picture.", t->id);
		t->bad = 1;
		return;
	}
	fill_bits = u8(r) >> 4;
	b.r = r;
	b.byte = 0;
	b.left = 0;
	style = ub(&b, 6);
	fs1 = (style >> 2) & 1;
	fs0 = (style >> 1) & 1;
	if (!style || style >> 3 || fs0 == fs1)
		goto not_one_rectangle;
	if (style & 1) {
		int move = (int)ub(&b, 5);
		sb(&b, move);
		sb(&b, move);
	}
	fill = ub(&b, (int)fill_bits);
	if (!fill || fill > count) {
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "Shape %d uses fill %u of its %u: the game closes when it reads the file.", t->id, fill, count);
		t->bad = 1;
		return;
	}
	t->bitmap = fills[fill - 1].bitmap;
	t->fm = fills[fill - 1].m;
	for (edge = 0; edge < 4; edge++) {
		int nbits;
		if (ub(&b, 2) != 3)
			goto not_one_rectangle;
		nbits = (int)ub(&b, 4);
		if (ub(&b, 1))
			goto not_one_rectangle;
		sb(&b, nbits + 3);
	}
	if (ub(&b, 6) != 0)
		goto not_one_rectangle;
	return;

not_one_rectangle:
	if (r->bad)
		return;
	say(c, SWFC_ERROR, 0, t->index, t->code,
	    "Shape %d is not one rectangle filled with a picture: the game closes when it reads the file. "
	    "It plays only that shape; " SAY_BUILD ".", t->id);
	t->bad = 1;
}

static void read_sound(struct parse *ps, struct rd *r, struct ptag *t)
{
	static const int rates[] = { 5512, 11025, 22050, 44100 };
	struct swfc *c = ps->c;
	struct bits b = { r, 0, 0 };

	t->id = (int)u16(r);
	t->fmt = (int)ub(&b, 4);
	t->rate = rates[ub(&b, 2)];
	t->bits = ub(&b, 1) ? 16 : 8;
	t->chans = ub(&b, 1) ? 2 : 1;
	t->samples = u32(r);
	if (r->bad)
		return;
	t->bytes = (uint32_t)left(r);
	r->i = r->n;
	c->sounds++;
	if (t->rate)
		c->sound_ms += t->samples * 1000.0 / t->rate;
	if (t->fmt == 3 || t->fmt == 0) {
		uint64_t want = (uint64_t)t->samples * t->chans * (t->bits / 8);
		if (want != t->bytes)
			say(c, SWFC_WARNING, 0, t->index, t->code,
			    "Sound %d says %u samples but holds %u bytes of them (%llu expected): the game plays "
			    "the bytes.", t->id, t->samples, t->bytes, (unsigned long long)want);
	}
}

static void read_start_sound(struct parse *ps, struct rd *r, struct ptag *t)
{
	struct swfc *c = ps->c;

	t->id = (int)u16(r);
	t->flags = u8(r);
	if (r->bad)
		return;
	c->starts++;
	if (t->flags & 0xc8) {
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "StartSound of sound %d has %s: the game closes when it reads the file. Start the sound "
		    "with no options (Sync: Event, no repeat, no effect).",
		    t->id, t->flags & 0x08 ? "an envelope (a fade or a pan)" : "unknown flags");
		t->bad = 1;
		return;
	}
	if (t->flags & 0x01)
		u32(r);
	if (t->flags & 0x02)
		u32(r);
	if (t->flags & 0x04)
		u16(r);
}

static void read_place(struct parse *ps, struct rd *r, struct ptag *t)
{
	struct swfc *c = ps->c;
	const char *v = t->code == T_PLACE_OBJECT3 ? "3" : "2";

	t->flags = t->code == T_PLACE_OBJECT3 ? u16(r) : u8(r);
	t->depth = (int)u16(r);
	if (r->bad)
		return;
	c->places++;
	if (t->flags & (PF_RESERVED | PF_CLIP_DEPTH | PF_FILTERS | PF_CLIP_ACTIONS)) {
		const char *what =
			t->flags & PF_CLIP_DEPTH ? "is a mask (a clip depth)" :
			t->flags & PF_FILTERS ? "has filters (blur, glow, shadow...)" :
			t->flags & PF_CLIP_ACTIONS ? "has clip events (onClipEvent)" :
			t->flags & 0x0800 ? "has a class name" :
			t->flags & 0x1000 ? "places an image by class" :
			t->flags & 0x2000 ? "sets the visibility" : "sets an opaque background";
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "PlaceObject%s at depth %d %s: the game closes when it reads the file. %s", v, t->depth, what,
		    t->flags & PF_CLIP_DEPTH ? "Draw the masked result into the pictures." :
		    t->flags & PF_FILTERS ? "Draw the filter into the pictures." :
		    t->flags & PF_CLIP_ACTIONS ? "Remove the script." : "Publish for ActionScript 2.");
		t->bad = 1;
		return;
	}
	if (t->flags & PF_CHARACTER)
		t->character = (int)u16(r);
	if (t->flags & PF_MATRIX) {
		struct matrix m;
		read_matrix(r, &m);
	}
	if (t->flags & PF_CXFORM)
		t->cx = read_cxform(r);
	if (t->flags & PF_RATIO)
		u16(r);
	if (t->flags & PF_NAME)
		skip_string(r);
	t->blend = 0;
	if (t->flags & PF_BLEND)
		t->blend = (int)u8(r);
	if (t->flags & PF_CACHE)
		u8(r);
}

static void read_actions(struct parse *ps, struct rd *r, struct ptag *t)
{
	struct swfc *c = ps->c;
	int cap = 0;

	c->doactions++;
	while (left(r) && !r->bad) {
		unsigned code = u8(r), len, k, cnt;
		struct act *a;
		struct rd body;

		if (t->nacts == cap) {
			struct act *n = realloc(t->acts, (size_t)(cap = cap ? cap * 2 : 8) * sizeof *n);
			if (!n)
				return;
			t->acts = n;
		}
		a = &t->acts[t->nacts++];
		a->code = (uint8_t)code;
		a->arg = 0;
		c->actions[code >> 3] |= (uint8_t)(1 << (code & 7));
		if (code < 0x80)
			continue;
		len = u16(r);
		if (r->bad)
			break;
		if (len > left(r)) {
			r->bad = 1;
			break;
		}
		body.p = r->p + r->i;
		body.n = len;
		body.i = 0;
		body.bad = 0;
		r->i += len;
		switch (code) {
		case A_CONSTANT_POOL:
			cnt = u16(&body);
			for (k = 0; k < cnt && !body.bad; k++)
				skip_string(&body);
			break;
		case A_GOTO_FRAME:
			a->arg = (int)u16(&body);
			break;
		case A_PUSH:
			while (left(&body) && !body.bad) {
				unsigned type = u8(&body);
				if (type == 7) {
					u32(&body);
				} else if (type == 8) {
					u8(&body);
				} else {
					say(c, SWFC_ERROR, 0, t->index, t->code,
					    "An ActionScript push of a %s: the game closes when it reads the file. " SAY_ACTIONS,
					    type == 0 ? "string" : type == 1 || type == 6 ? "number with a fraction"
					    : type == 5 ? "true or false" : type == 9 ? "name (constant 16)" : "value it does not read");
					t->bad = 1;
					return;
				}
			}
			break;
		default:
			say(c, SWFC_ERROR, 0, t->index, t->code,
			    "ActionScript action 0x%02x (%s): the game closes when it reads the file. " SAY_ACTIONS,
			    code, action_name((int)code));
			t->bad = 1;
			return;
		}
		if (body.bad)
			r->bad = 1;
	}
}

static int read_list(struct parse *ps, struct rd *r, struct ptag **out, int *nout, int nest);

/* One tag. 0 when the rest of the list cannot be read. */
static int read_tag(struct parse *ps, struct rd *r, struct ptag *t, int nest)
{
	struct swfc *c = ps->c;
	unsigned head;
	uint32_t len;
	struct rd body;

	memset(t, 0, sizeof *t);
	if (left(r) < 2) {
		say(c, SWFC_ERROR, 0, ps->index + 1, -1,
		    "The file ends inside a tag's header: the game closes when it reads the file. Export it again.");
		return 0;
	}
	head = u16(r);
	t->code = (int)(head >> 6);
	len = head & 0x3f;
	if (len == 0x3f) {
		if (left(r) < 4) {
			say(c, SWFC_ERROR, 0, ps->index + 1, t->code,
			    "The file ends inside a tag's header: the game closes when it reads the file. Export it again.");
			return 0;
		}
		len = u32(r);
	}
	t->index = ++ps->index;
	c->tags++;
	if (len > left(r)) {
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "%s is %u bytes long but the file has %zu left: the file is cut short, and the game reads "
		    "past its end. Export it again.", tag_name(t->code), len, left(r));
		return 0;
	}
	body.p = r->p + r->i;
	body.n = len;
	body.i = 0;
	body.bad = 0;
	r->i += len;

	switch (t->code) {
	case T_END:
	case T_SHOW_FRAME:
	case T_FILE_ATTRIBUTES:
		break;
	case T_SET_BACKGROUND_COLOR:
		u8(&body);
		u8(&body);
		u8(&body);
		break;
	case T_DEFINE_BITS_LOSSLESS:
	case T_DEFINE_BITS_LOSSLESS2:
		read_bitmap(ps, &body, t);
		break;
	case T_DEFINE_SHAPE:
		read_shape(ps, &body, t);
		break;
	case T_DEFINE_SOUND:
		read_sound(ps, &body, t);
		break;
	case T_START_SOUND:
		read_start_sound(ps, &body, t);
		break;
	case T_DEFINE_SPRITE:
		t->id = (int)u16(&body);
		t->frames = (int)u16(&body);
		if (body.bad)
			break;
		c->sprites++;
		if (nest >= NEST_MAX) {
			say(c, SWFC_ERROR, 0, t->index, t->code,
			    "Sprite %d is inside %d other sprites: too deep to check. A sprite may hold one shape only.",
			    t->id, nest);
			t->bad = 1;
			return 1;
		}
		read_list(ps, &body, &t->inner, &t->ninner, nest + 1);
		return 1;
	case T_DO_ACTION:
		read_actions(ps, &body, t);
		break;
	case T_PLACE_OBJECT2:
	case T_PLACE_OBJECT3:
		read_place(ps, &body, t);
		break;
	case T_REMOVE_OBJECT2:
		t->depth = (int)u16(&body);
		c->removes++;
		break;
	case T_SOUND_STREAM_HEAD2:
		u8(&body);
		if ((u8(&body) >> 4) == 2) {
			u16(&body);
			u16(&body);
		} else {
			u16(&body);
		}
		break;
	default:
		/* swf_load keeps it as a bare tag; the second pass says where it stops the game. */
		break;
	}
	if (body.bad && !t->bad) {
		say(c, SWFC_ERROR, 0, t->index, t->code,
		    "%s ends before its content does: the game closes when it reads the file. Export it again.",
		    tag_name(t->code));
		t->bad = 1;
	}
	return 1;
}

static int read_list(struct parse *ps, struct rd *r, struct ptag **out, int *nout, int nest)
{
	struct ptag *list = NULL;
	int n = 0, cap = 0, ok = 1;

	while (left(r) > 0) {
		if (n == cap) {
			struct ptag *g = realloc(list, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *g);
			if (!g) {
				ok = 0;
				break;
			}
			list = g;
		}
		if (!read_tag(ps, r, &list[n], nest)) {
			ok = 0;
			break;
		}
		n++;
	}
	*out = list;
	*nout = n;
	return ok;
}

/* ---- the second pass: the main timeline, as parts_flash_seek plays it ---- */

enum { D_NONE, D_BITMAP, D_SHAPE, D_SOUND, D_SPRITE };

struct dent {
	uint8_t kind;
	uint8_t ready;   /* a sprite: its first definition was checked */
	int object;      /* a sprite: its object's character (0: none); a shape: its bitmap */
	int fmt;         /* a sound's format */
	int w, h;        /* a bitmap's size */
	int started;     /* a sound: started */
	int index;       /* the tag that defined it */
};

struct obj {
	int depth, character;
};

struct play {
	struct swfc *c;
	struct dent *dict; /* 65536 */
	struct obj *objs;
	int nobjs, cap;
	int frame;
};

static const char *kind_name(int kind)
{
	switch (kind) {
	case D_BITMAP: return "bitmap";
	case D_SHAPE: return "shape";
	case D_SOUND: return "sound";
	case D_SPRITE: return "sprite";
	}
	return "character";
}

static struct obj *obj_at(struct play *pl, int depth)
{
	int i;

	for (i = 0; i < pl->nobjs; i++) {
		if (pl->objs[i].depth == depth)
			return &pl->objs[i];
	}
	return NULL;
}

static void define(struct play *pl, const struct ptag *t, int kind)
{
	struct dent *d = &pl->dict[t->id & 0xffff];

	if (d->kind != D_NONE)
		say(pl->c, SWFC_WARNING, pl->frame, t->index, t->code,
		    "Character %d is defined twice: the game keeps the first one's picture.", t->id);
	d->kind = (uint8_t)kind;
	d->index = t->index;
	d->ready = 0;
	if (kind == D_BITMAP) {
		d->w = t->w;
		d->h = t->h;
	} else if (kind == D_SHAPE) {
		d->object = t->bitmap;
	} else if (kind == D_SOUND) {
		d->fmt = t->fmt;
	}
}

/* The engine stretches the whole bitmap over the shape's bounds; Flash places it by the
 * fill's matrix. Within a pixel they agree. */
static void check_fill(struct play *pl, const struct ptag *t)
{
	const struct dent *b = &pl->dict[t->bitmap & 0xffff];
	int64_t x1, y1;

	if (t->bad || b->kind != D_BITMAP)
		return;
	x1 = t->fm.tx + (int64_t)b->w * t->fm.sx / 65536;
	y1 = t->fm.ty + (int64_t)b->h * t->fm.sy / 65536;
	if (t->fm.r0 || t->fm.r1 || llabs(t->fm.tx - t->bx0) > 20 || llabs(t->fm.ty - t->by0) > 20
	    || llabs(x1 - t->bx1) > 20 || llabs(y1 - t->by1) > 20) {
		pl->c->fill_off++;
	}
}

static void define_sprite(struct play *pl, const struct ptag *t)
{
	struct swfc *c = pl->c;
	struct dent *d = &pl->dict[t->id & 0xffff];
	int i, object = 0;

	if (d->kind == D_SPRITE && d->ready)
		return; /* flash.c defines a sprite's object once */
	define(pl, t, D_SPRITE);
	d->ready = 1;
	d->object = 0;
	if (t->bad)
		return;
	if (t->frames != 1) {
		say(c, SWFC_ERROR, pl->frame, t->index, t->code,
		    "Sprite %d (a movie clip) has %d frames: the game closes when frame %d plays. Only "
		    "one-frame sprites play; put its frames on the main timeline.", t->id, t->frames, pl->frame);
		return;
	}
	for (i = 0; i < t->ninner; i++) {
		const struct ptag *k = &t->inner[i];
		if (k->code == T_END)
			break;
		if (k->code == T_SOUND_STREAM_HEAD2 || k->code == T_SHOW_FRAME)
			continue;
		if (k->code != T_PLACE_OBJECT2) {
			say(c, SWFC_ERROR, pl->frame, k->index, k->code,
			    "Sprite %d holds %s: the game closes when frame %d plays. A sprite may hold one "
			    "PlaceObject2 of a shape and nothing else. %s", t->id, tag_name(k->code), pl->frame,
			    k->code == T_PLACE_OBJECT3 ? "" : tag_hint(k->code));
			return;
		}
		if (object) {
			say(c, SWFC_ERROR, pl->frame, k->index, k->code,
			    "Sprite %d holds more than one object: the game closes when frame %d plays. A sprite "
			    "may hold one shape; put the others on the main timeline.", t->id, pl->frame);
			return;
		}
		if (k->bad)
			return;
		if (!(k->flags & PF_CHARACTER)) {
			say(c, SWFC_ERROR, pl->frame, k->index, k->code,
			    "Sprite %d moves an object it never placed: the game closes when frame %d plays.",
			    t->id, pl->frame);
			return;
		}
		object = k->character;
		if (!object)
			object = -1;
	}
	d->object = object > 0 ? object : 0;
}

static void run_actions(struct play *pl, const struct ptag *t)
{
	struct swfc *c = pl->c;
	int i;

	if (t->bad)
		return;
	for (i = 0; i < t->nacts; i++) {
		const struct act *a = &t->acts[i];
		switch (a->code) {
		case A_END:
			return;
		case A_PLAY:
		case A_STOP:
		case A_POP:
		case A_CALL_FUNCTION:
		case A_CONSTANT_POOL:
		case A_PUSH:
			break;
		case A_GOTO_FRAME:
			if (c->ngotos < SWFC_GOTOS_MAX) {
				c->gotos[c->ngotos].frame = pl->frame;
				c->gotos[c->ngotos].target = a->arg;
				c->ngotos++;
			}
			if (c->frames > 0 && a->arg + 1 > c->frames)
				say(c, SWFC_WARNING, pl->frame, t->index, t->code,
				    "gotoAndPlay or gotoAndStop to frame %d, past the last frame (%d).", a->arg + 1, c->frames);
			break;
		default:
			say(c, SWFC_ERROR, pl->frame, t->index, t->code,
			    "ActionScript action 0x%02x (%s): the game closes when frame %d plays. " SAY_ACTIONS,
			    a->code, action_name(a->code), pl->frame);
			return;
		}
	}
	say(c, SWFC_ERROR, pl->frame, t->index, t->code,
	    "A DoAction's actions have no end (ActionEndFlag): the game closes when frame %d plays. "
	    "Export it again.", pl->frame);
}

static void place(struct play *pl, const struct ptag *t)
{
	struct swfc *c = pl->c;
	struct obj *o;

	if (t->bad)
		return;
	o = obj_at(pl, t->depth);
	if (!o) {
		if (!(t->flags & PF_CHARACTER)) {
			say(c, SWFC_ERROR, pl->frame, t->index, t->code,
			    "A PlaceObject changes depth %d, where nothing is: the game closes when frame %d plays.",
			    t->depth, pl->frame);
			return;
		}
		if (pl->nobjs == pl->cap) {
			struct obj *n = realloc(pl->objs, (size_t)(pl->cap = pl->cap ? pl->cap * 2 : 16) * sizeof *n);
			if (!n)
				return;
			pl->objs = n;
		}
		o = &pl->objs[pl->nobjs++];
		o->depth = t->depth;
	}
	if (t->flags & PF_CHARACTER)
		o->character = t->character;
	if (t->flags & PF_BLEND) {
		int b = t->blend;
		c->blends[b < 15 ? b : 15]++;
		if (b != 0 && b != 1 && b != 3 && b != 4 && b != 8)
			say(c, SWFC_WARNING, pl->frame, t->index, t->code,
			    "Blend mode %s (%d) is drawn as normal: the game draws normal, multiply, screen and add.",
			    swfc_blend_name(b), b);
	} else {
		c->blends[0]++;
	}
	if (t->flags & PF_CXFORM) {
		if (t->cx & CX_ADD)
			c->cx_add++;
		if (t->cx & CX_MULT)
			c->cx_mult++;
		if ((t->cx & (CX_ADD | CX_MULT)) == (CX_ADD | CX_MULT))
			c->cx_both++;
		if (t->cx & CX_ALPHA_ADD) {
			c->cx_alpha_add++;
			say(c, SWFC_WARNING, pl->frame, t->index, t->code,
			    "A colour transform adds to alpha: the game ignores that term (it multiplies alpha only).");
		}
	}
}

static void unplace(struct play *pl, int depth)
{
	int i;

	for (i = 0; i < pl->nobjs; i++) {
		if (pl->objs[i].depth == depth) {
			pl->objs[i] = pl->objs[--pl->nobjs];
			return;
		}
	}
}

static void start_sound(struct play *pl, const struct ptag *t)
{
	struct swfc *c = pl->c;
	struct dent *d = &pl->dict[t->id & 0xffff];

	if (t->bad)
		return;
	if (t->flags) {
		say(c, SWFC_ERROR, pl->frame, t->index, t->code,
		    "StartSound of sound %d has %s: the game closes when frame %d plays. Start the sound with "
		    "no options (Sync: Event, no repeat, no in or out point).", t->id,
		    t->flags & 0x04 ? "a repeat count" : t->flags & 0x01 ? "an in point" : t->flags & 0x02 ? "an out point"
		    : t->flags & 0x20 ? "Sync: Stop" : "Sync: Start", pl->frame);
		return;
	}
	if (d->kind != D_SOUND) {
		say(c, SWFC_WARNING, pl->frame, t->index, t->code,
		    "StartSound of sound %d, which is not defined: the game skips it.", t->id);
		return;
	}
	d->started = 1;
	if (d->fmt != 3)
		say(c, SWFC_ERROR, pl->frame, t->index, t->code,
		    "Sound %d is %s: the game closes when frame %d starts it. It plays only uncompressed PCM "
		    "(format 3); add the sound from a WAV with rqmod-aff build --sound.",
		    t->id, sound_format_name(d->fmt), pl->frame);
}

/* What a frame draws, as parts_render_flash draws it. */
static void draw_frame(struct play *pl)
{
	struct swfc *c = pl->c;
	int i;

	for (i = 0; i < pl->nobjs; i++) {
		const struct obj *o = &pl->objs[i];
		const struct dent *d = &pl->dict[o->character & 0xffff];
		const struct dent *s;

		switch (d->kind) {
		case D_NONE:
			say(c, SWFC_WARNING, pl->frame, 0, -1,
			    "Depth %d shows character %d, which is not defined: the game draws nothing there.",
			    o->depth, o->character);
			break;
		case D_SHAPE:
			if (pl->dict[d->object & 0xffff].kind != D_BITMAP)
				say(c, SWFC_ERROR, pl->frame, d->index, 2,
				    "Shape %d's picture (bitmap %d) is not defined before frame %d: the game closes "
				    "when it draws that frame.", o->character, d->object, pl->frame);
			break;
		case D_SPRITE:
			if (!d->object)
				break; /* nothing to draw */
			s = &pl->dict[d->object & 0xffff];
			if (s->kind != D_SHAPE) {
				say(c, SWFC_ERROR, pl->frame, d->index, 39,
				    "Sprite %d holds %s %d, not a shape: the game closes when it draws frame %d. A "
				    "sprite may hold one shape only.", o->character,
				    s->kind == D_NONE ? "character" : kind_name(s->kind), d->object, pl->frame);
			} else if (pl->dict[s->object & 0xffff].kind != D_BITMAP) {
				say(c, SWFC_ERROR, pl->frame, s->index, 2,
				    "Shape %d's picture (bitmap %d) is not defined before frame %d: the game closes "
				    "when it draws that frame.", d->object, s->object, pl->frame);
			}
			break;
		default:
			say(c, SWFC_ERROR, pl->frame, d->index, -1,
			    "Depth %d shows %s %d itself: the game closes when it draws frame %d. Only shapes and "
			    "sprites are placed.", o->depth, kind_name(d->kind), o->character, pl->frame);
			break;
		}
	}
}

static void play(struct swfc *c, const struct ptag *tags, int n)
{
	struct play pl = { c, NULL, NULL, 0, 0, 1 };
	int i, ended = 0;

	pl.dict = calloc(65536, sizeof *pl.dict);
	if (!pl.dict)
		return;
	for (i = 0; i < n; i++) {
		const struct ptag *t = &tags[i];
		if (ended) {
			say(c, SWFC_WARNING, 0, t->index, t->code,
			    "%d tags come after the End tag: the game reads them but never plays them.", n - i);
			break;
		}
		switch (t->code) {
		case T_END:
			ended = 1;
			break;
		case T_SHOW_FRAME:
			draw_frame(&pl);
			c->shown++;
			pl.frame++;
			break;
		case T_FILE_ATTRIBUTES:
		case T_SET_BACKGROUND_COLOR:
		case T_SOUND_STREAM_HEAD2:
			break;
		case T_DEFINE_BITS_LOSSLESS:
		case T_DEFINE_BITS_LOSSLESS2:
			if (!t->bad)
				define(&pl, t, D_BITMAP);
			break;
		case T_DEFINE_SHAPE:
			if (!t->bad) {
				define(&pl, t, D_SHAPE);
				check_fill(&pl, t);
			}
			break;
		case T_DEFINE_SOUND:
			define(&pl, t, D_SOUND);
			break;
		case T_DEFINE_SPRITE:
			define_sprite(&pl, t);
			break;
		case T_DO_ACTION:
			run_actions(&pl, t);
			break;
		case T_PLACE_OBJECT2:
		case T_PLACE_OBJECT3:
			place(&pl, t);
			break;
		case T_REMOVE_OBJECT2:
			unplace(&pl, t->depth);
			break;
		case T_START_SOUND:
			start_sound(&pl, t);
			break;
		default:
			say(c, SWFC_ERROR, pl.frame, t->index, t->code,
			    "%s (tag code %d): the game closes when frame %d plays. %s",
			    tag_name(t->code), t->code, pl.frame, tag_hint(t->code));
			break;
		}
	}
	/* A sound in another format that nothing starts is read but never played. */
	for (i = 0; i < 65536; i++) {
		const struct dent *d = &pl.dict[i];
		if (d->kind == D_SOUND && !d->started && d->fmt != 3)
			say(c, SWFC_WARNING, 0, d->index, 14,
			    "Sound %d is %s and nothing starts it: the game would close if something did.",
			    i, sound_format_name(d->fmt));
	}
	free(pl.dict);
	free(pl.objs);
}

/* ---- the file ---- */

static const uint8_t aff_mask[16] = {
	0xC8, 0xBB, 0x8F, 0xB7, 0xED, 0x43, 0x99, 0x4A,
	0xA2, 0x7E, 0x5B, 0xB0, 0x68, 0x18, 0xF8, 0x88
};

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The SWF inside, uncompressed, in c->fws. 0 when it cannot be read (said). */
static int unwrap(const uint8_t *data, size_t size, struct swfc *c)
{
	const uint8_t *swf = data;
	uint8_t *own = NULL;
	size_t n = size;

	c->file_size = size;
	if (size >= 4 && !memcmp(data, "AFF\0", 4)) {
		size_t i;
		snprintf(c->container, sizeof c->container, "aff");
		if (size < 16 + 8) {
			say(c, SWFC_ERROR, 0, 0, -1, "The .aff file is too short to hold a SWF.");
			return 0;
		}
		if (le32(data + 4) != 1 || le32(data + 8) != size || le32(data + 12) != 0x4d2) {
			say(c, SWFC_ERROR, 0, 0, -1,
			    "The .aff header is not the game's (version %u, a size of %u for a file of %zu bytes, "
			    "0x%x): the game does not load it.", le32(data + 4), le32(data + 8), size, le32(data + 12));
			return 0;
		}
		n = size - 16;
		own = malloc(n);
		if (!own) {
			say(c, SWFC_ERROR, 0, 0, -1, "Out of memory.");
			return 0;
		}
		memcpy(own, data + 16, n);
		for (i = 0; i < 0x40 && i < n; i++)
			own[i] ^= aff_mask[i & 15];
		swf = own;
	}
	if (n < 8) {
		say(c, SWFC_ERROR, 0, 0, -1, "Not a Flash file: too short.");
		free(own);
		return 0;
	}
	if (!memcmp(swf, "ZWS", 3)) {
		snprintf(c->container[0] ? c->inner : c->container, 4, "ZWS");
		say(c, SWFC_ERROR, 0, 0, -1,
		    "The SWF is compressed with LZMA (ZWS), which the game does not read: save it compressed "
		    "with zlib (CWS) or not at all (FWS).");
		free(own);
		return 0;
	}
	if (memcmp(swf, "FWS", 3) && memcmp(swf, "CWS", 3)) {
		say(c, SWFC_ERROR, 0, 0, -1,
		    c->container[0] ? "The .aff does not hold a SWF (wrong mask or a damaged file)."
		    : "Not a Flash file: it starts with neither AFF, FWS nor CWS.");
		free(own);
		return 0;
	}
	snprintf(c->container[0] ? c->inner : c->container, 4, "%.3s", (const char *)swf);
	c->version = swf[3];
	if (swf[0] == 'C') {
		uint32_t raw = le32(swf + 4);
		uLongf got;
		uint8_t *out;
		if (raw < 13 || raw > FWS_MAX) {
			say(c, SWFC_ERROR, 0, 0, -1, "The compressed SWF says it unpacks to %u bytes: the game does not load it.", raw);
			free(own);
			return 0;
		}
		out = malloc(raw);
		if (!out) {
			say(c, SWFC_ERROR, 0, 0, -1, "Out of memory.");
			free(own);
			return 0;
		}
		memcpy(out, swf, 8);
		out[0] = 'F';
		got = raw - 8;
		if (uncompress(out + 8, &got, swf + 8, (uLong)(n - 8)) != Z_OK || got != raw - 8) {
			say(c, SWFC_ERROR, 0, 0, -1,
			    "The compressed SWF does not unpack to the %u bytes it says: the game does not load it. "
			    "Export it again.", raw);
			free(out);
			free(own);
			return 0;
		}
		free(own);
		c->fws = out;
		c->fws_size = raw;
		return 1;
	}
	if (le32(swf + 4) != n) {
		say(c, SWFC_ERROR, 0, 0, -1,
		    "The SWF says it is %u bytes long but it is %zu: the game does not load it. Export it again.",
		    le32(swf + 4), n);
		free(own);
		return 0;
	}
	if (own) {
		c->fws = own;
	} else {
		c->fws = malloc(n);
		if (!c->fws) {
			say(c, SWFC_ERROR, 0, 0, -1, "Out of memory.");
			return 0;
		}
		memcpy(c->fws, swf, n);
	}
	c->fws_size = n;
	return 1;
}

int swfc_check(const uint8_t *data, size_t size, struct swfc *c)
{
	struct parse ps;
	struct rd r;
	struct ptag *tags = NULL;
	int ntags = 0;

	memset(c, 0, sizeof *c);
	if (!unwrap(data, size, c))
		return 3;
	r.p = c->fws;
	r.n = c->fws_size;
	r.i = 8;
	r.bad = 0;
	read_rect(&r, &c->x_min, &c->x_max, &c->y_min, &c->y_max);
	c->rate = u16(&r);
	c->frames = (int)u16(&r);
	if (r.bad) {
		say(c, SWFC_ERROR, 0, 0, -1, "The SWF ends inside its header: the game closes when it reads the file.");
		return 3;
	}
	c->width = c->x_max / 20;
	c->height = c->y_max / 20;
	if (c->x_min || c->y_min)
		say(c, SWFC_WARNING, 0, 0, -1,
		    "The stage starts at (%d, %d), not (0, 0): the game sizes the animation from its right and "
		    "bottom edges only.", c->x_min / 20, c->y_min / 20);
	if (c->width <= 0 || c->height <= 0)
		say(c, SWFC_WARNING, 0, 0, -1, "The stage is %d x %d pixels: nothing shows.", c->width, c->height);
	if (!c->rate)
		say(c, SWFC_WARNING, 0, 0, -1,
		    "The header's frame rate is 0: the game's own players count 30 frames a second, but a file "
		    "the engine plays by itself would not move.");
	else if (c->rate != 30 << 8)
		say(c, SWFC_WARNING, 0, 0, -1,
		    "The header's frame rate is %.2f fps: the game plays its Flash files at 30 frames a second "
		    "whatever the header says, so this one lasts %.2f s, not %.2f s.", c->rate / 256.0,
		    c->frames / 30.0, c->frames * 256.0 / c->rate);
	if (!c->frames)
		say(c, SWFC_WARNING, 0, 0, -1, "The header says 0 frames: the game plays nothing.");

	ps.c = c;
	ps.index = 0;
	read_list(&ps, &r, &tags, &ntags, 0);
	play(c, tags, ntags);
	free_tags(tags, ntags);

	if (c->frames && c->shown < c->frames)
		say(c, SWFC_WARNING, 0, 0, -1,
		    "The header says %d frames but the main timeline shows %d: the game holds frame %d to the end.",
		    c->frames, c->shown, c->shown);
	else if (c->frames && c->shown > c->frames)
		say(c, SWFC_WARNING, 0, 0, -1,
		    "The header says %d frames but the main timeline shows %d: the game stops after frame %d.",
		    c->frames, c->shown, c->frames);
	return c->errors ? 1 : 0;
}

void swfc_free(struct swfc *c)
{
	free(c->items);
	free(c->fws);
	c->items = NULL;
	c->fws = NULL;
	c->nitems = c->cap = 0;
}

double swfc_seconds(const struct swfc *c)
{
	return c->frames / 30.0;
}
