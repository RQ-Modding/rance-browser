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
 * rq_pixels: a mod's pictures as RQ MOD reads them (every kind of PNG, refused with a reason
 * instead of closing the program), and the rules a mod's picture on one of the game's 3D textures
 * follows. No file access: each program reads the bytes its own way. Only libpng, libsys4's
 * struct cg and the C library, so the same file builds in the engine and in Rance Browser, which
 * keeps a copy (src/core/rq_pixels.c and .h): change both together.
 */

#ifndef RQ_PIXELS_H
#define RQ_PIXELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cg;

/* The largest side, in pixels, of a mod's picture on a 3D texture. */
#define RQ_PIXELS_TEXTURE_SIDE 4096
/* The largest side of a mod's still picture for rq.image.add, replace and flash_bitmap. */
#define RQ_PIXELS_PICTURE_SIDE 16384

/* Whether the bytes start with PNG's signature. */
bool rq_pixels_is_png(const uint8_t *data, size_t size);

/* A PNG's size from its header (IHDR, the first chunk), with nothing decoded; false when the
 * bytes are not a PNG with a header. Lets a caller name a picture too large apart from one that
 * cannot be read. */
bool rq_pixels_png_size(const uint8_t *data, size_t size, int *w, int *h);

/*
 * Any PNG (1 to 16 bits; RGB, RGBA, grey, grey with alpha or a palette; tRNS; interlaced) as an
 * 8-bit RGBA cg, has_alpha when the file has an alpha channel or tRNS. A side over `max_side` is
 * refused from the header, before any pixel is read. NULL on any failure, never an abort, with
 * the reason in `why` ("not a PNG", "the file is cut off", "5000x16 (max 4096 a side)" or
 * libpng's own words). An 8-bit RGB or RGBA file, not interlaced and without tRNS, gives
 * libsys4's pixels and has_alpha byte for byte. The caller frees the cg with cg_free.
 */
struct cg *rq_pixels_png(const uint8_t *data, size_t size, int max_side, char *why, size_t why_len);

/* The ranges of colors by numbers: hue -360 to 360 degrees, saturation and lightness factors 0 to 4. */
#define RQ_PIXELS_HUE_MAX 360.0
#define RQ_PIXELS_FACTOR_MAX 4.0

/*
 * Whether colors by numbers are in range: false with the reason in `why` ("texture colors: hue must
 * be a number from -360 to 360", the same for saturation and lightness from 0 to 4). A NaN is out.
 */
bool rq_pixels_colors_ok(double hue, double saturation, double lightness, char *why, size_t why_len);

/*
 * Colors by numbers on an 8-bit RGBA cg, in place, alpha untouched: each point's hue moved by `hue`
 * degrees, its saturation and its lightness multiplied by the factors, in HSL with doubles. Grey,
 * white and black stay grey (hue and saturation move coloured points only; lightness scales every
 * point). The docs' recolour script follows the same rule to the byte:
 *   r,g,b = R/255, G/255, B/255; mx, mn = max, min; l = (mx+mn)/2; d = mx-mn;
 *   d = 0: h = 0, s = 0; else s = d / (1 - |2l - 1|), h = 60*fmod((g-b)/d, 6) when mx = r,
 *   60*((b-r)/d + 2) when mx = g, 60*((r-g)/d + 4) otherwise;
 *   h' = fmod(h + hue, 360) in [0, 360), s' = min(1, s*saturation), l' = min(1, l*lightness);
 *   c = (1 - |2l' - 1|)*s', x = c*(1 - |fmod(h'/60, 2) - 1|), m = l' - c/2, by the sector of h'/60;
 *   each channel floor((v + m)*255 + 0.5), clamped to 0..255.
 */
void rq_pixels_recolor(struct cg *cg, double hue, double saturation, double lightness);

/*
 * A mod's picture drawn with the game's alpha kind, so a material stays as see-through as the game
 * made it. `game_has_alpha` is the game's picture's; `game` is that picture decoded, needed only
 * when the game's has alpha and the mod's has none (it may be NULL otherwise).
 *   the game's has no alpha: the mod's is made solid (alpha 255), has_alpha false;
 *   both have alpha: the mod's alpha is kept;
 *   the game's has alpha, the mod's has none: the game's alpha, scaled to the mod's size (the
 *   point nearest each point's centre), so the game's holes stay.
 * has_alpha is always the game's afterwards.
 */
void rq_pixels_game_alpha(struct cg *mod, bool game_has_alpha, const struct cg *game);

#endif /* RQ_PIXELS_H */
