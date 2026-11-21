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
 * swf_check: the Flash files Rance Quest's engine plays, as a walk over the bytes that lists
 * every problem instead of closing the program. src/swf.c and src/parts/flash.c stop the game
 * on what they do not handle; a mod's file goes through this first (rq.image.add_flash), and
 * rqmod-aff and Rance Browser use the same rules. Only zlib and the C library, so the same
 * file builds in each of them. Rance Browser keeps a copy (src/core/swf_check.c and .h):
 * change both together.
 */

#ifndef RQ_SWF_CHECK_H
#define RQ_SWF_CHECK_H

#include <stddef.h>
#include <stdint.h>

#define SWFC_ITEMS_MAX 200
#define SWFC_GOTOS_MAX 64

enum swfc_level {
	SWFC_WARNING = 1, /* the game plays it, but not as Flash would, or not all of it */
	SWFC_ERROR = 2,   /* the game would not load it, or would close */
};

struct swfc_item {
	int level;
	int frame;  /* the main timeline's frame (from 1) where the game meets it; 0: when it reads the file */
	int tag;    /* the tag's place in the file, from 1 (a sprite's own tags counted too); 0: none */
	int code;   /* the tag's code; -1: none */
	int count;  /* how many times the same problem was met */
	char text[360];
};

struct swfc_goto {
	int frame;  /* the frame (from 1) whose actions jump */
	int target; /* as written in the file: Flash counts frames from 0 */
};

struct swfc {
	/* The file. */
	char container[4];  /* "aff", "FWS", "CWS", "ZWS", or "" when not a Flash file */
	char inner[4];      /* inside an aff: "FWS" or "CWS" */
	int version;
	size_t file_size;
	size_t fws_size;    /* the SWF uncompressed */
	/* The header. */
	int32_t x_min, x_max, y_min, y_max; /* the stage, in twips (1/20 pixel) */
	int width, height;  /* the size the engine gives the animation: x_max / 20, y_max / 20 */
	unsigned rate;      /* the header's frame rate, 8.8 fixed point */
	int frames;         /* the header's frame count: the game's players play this many */
	int shown;          /* ShowFrame tags on the main timeline */
	/* What is in it. */
	int tags, bitmaps, shapes, sprites, sounds, starts, places, removes, doactions;
	int blends[16];     /* placements per blend mode, 0 to 14; [15]: a mode past 14 */
	int cx_add, cx_mult, cx_both, cx_alpha_add; /* placements with colour add terms, multiply terms, both, an alpha add */
	uint64_t pixels;    /* every bitmap's pixels */
	int soft_bitmaps;   /* bitmaps with pixels neither clear nor solid */
	int premul_bitmaps; /* of those, the ones whose colours never pass their alpha (premultiplied, as Flash writes) */
	int fill_off;       /* shapes whose fill puts the picture elsewhere than over the shape */
	double sound_ms;
	uint8_t actions[32]; /* a bit per ActionScript action code met */
	struct swfc_goto gotos[SWFC_GOTOS_MAX];
	int ngotos;
	/* The problems. */
	struct swfc_item *items;
	int nitems, cap;
	int errors, warnings; /* distinct problems */
	int more;             /* problems past SWFC_ITEMS_MAX, counted but not kept */
	/* The SWF uncompressed (FWS), owned; NULL when it could not be read. */
	uint8_t *fws;
};

/* Checks a Flash file: an .aff, or a SWF uncompressed (FWS) or compressed with zlib (CWS).
 * Returns 0 when the game plays it, 1 when it would not (c->errors), 3 when it cannot be
 * read (the first error says why). Call swfc_free after, whatever it returns. */
int swfc_check(const uint8_t *data, size_t size, struct swfc *c);
void swfc_free(struct swfc *c);

/* A tag's name in the SWF format ("DefineShape3"), or NULL for a code with no name. */
const char *swfc_tag_name(int code);
/* A blend mode's name ("multiply"). */
const char *swfc_blend_name(int mode);
/* The length at the game's 30 frames a second, in seconds. */
double swfc_seconds(const struct swfc *c);

#endif /* RQ_SWF_CHECK_H */
