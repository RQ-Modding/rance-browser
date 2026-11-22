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
 * A mod's pictures as RQ MOD reads them (see rq_pixels.h). libsys4's PNG reader takes 8-bit RGB
 * and RGBA only, and ends the program on any libpng error (it sets no error handler), on a file
 * cut off (its ERROR) and on a 16-bit RGBA file (its assert). This one reads every PNG through
 * libpng's own transforms, with an error handler of its own, and gives a reason instead.
 */

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

#include "system4/cg.h"

#include "rq_pixels.h"

struct reader {
	const uint8_t *data;
	size_t size, at;
	char first[200]; /* libpng's first error, or ours */
};

static void say(char *why, size_t len, const char *fmt, ...)
{
	va_list ap;

	if (!why || !len)
		return;
	va_start(ap, fmt);
	vsnprintf(why, len, fmt, ap);
	va_end(ap);
}

static void on_error(png_structp png, png_const_charp msg)
{
	struct reader *r = png_get_error_ptr(png);

	if (r && !r->first[0])
		snprintf(r->first, sizeof r->first, "%s", msg && msg[0] ? msg : "a broken PNG");
	png_longjmp(png, 1);
}

static void on_warning(png_structp png, png_const_charp msg)
{
	(void)png;
	(void)msg;
}

static void on_read(png_structp png, png_bytep out, size_t n)
{
	struct reader *r = png_get_io_ptr(png);

	if (r->size - r->at < n)
		png_error(png, "the file is cut off");
	memcpy(out, r->data + r->at, n);
	r->at += n;
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

bool rq_pixels_is_png(const uint8_t *data, size_t size)
{
	return data && size >= 8 && !png_sig_cmp((png_const_bytep)data, 0, 8);
}

bool rq_pixels_png_size(const uint8_t *data, size_t size, int *w, int *h)
{
	uint32_t hw, hh;

	if (!rq_pixels_is_png(data, size) || size < 24 || memcmp(data + 12, "IHDR", 4))
		return false;
	hw = be32(data + 16);
	hh = be32(data + 20);
	*w = hw > INT32_MAX ? INT32_MAX : (int)hw;
	*h = hh > INT32_MAX ? INT32_MAX : (int)hh;
	return true;
}

struct cg *rq_pixels_png(const uint8_t *data, size_t size, int max_side, char *why, size_t why_len)
{
	struct reader r = { data, size, 8, "" };
	png_structp png;
	png_infop info;
	png_uint_32 w = 0, h = 0;
	int depth = 0, color = 0, interlace = 0;
	struct cg *volatile cg = NULL;
	png_bytep *volatile rows = NULL;
	bool alpha;

	if (why && why_len)
		why[0] = '\0';
	if (!rq_pixels_is_png(data, size)) {
		say(why, why_len, "not a PNG");
		return NULL;
	}
	/* The size from the header (IHDR is the first chunk), before libpng allocates anything. */
	if (size >= 24 && !memcmp(data + 12, "IHDR", 4)) {
		uint32_t hw = be32(data + 16), hh = be32(data + 20);
		if (hw > (uint32_t)max_side || hh > (uint32_t)max_side) {
			say(why, why_len, "%lux%lu (max %d a side)", (unsigned long)hw, (unsigned long)hh, max_side);
			return NULL;
		}
	}
	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &r, on_error, on_warning);
	if (!png) {
		say(why, why_len, "out of memory");
		return NULL;
	}
	info = png_create_info_struct(png);
	if (!info) {
		png_destroy_read_struct(&png, NULL, NULL);
		say(why, why_len, "out of memory");
		return NULL;
	}
	if (setjmp(png_jmpbuf(png))) {
		say(why, why_len, "%s", r.first[0] ? r.first : "a broken PNG");
		if (cg)
			cg_free(cg);
		free(rows);
		png_destroy_read_struct(&png, &info, NULL);
		return NULL;
	}
	png_set_user_limits(png, (png_uint_32)max_side, (png_uint_32)max_side);
	png_set_read_fn(png, &r, on_read);
	png_set_sig_bytes(png, 8);
	png_read_info(png, info);
	png_get_IHDR(png, info, &w, &h, &depth, &color, &interlace, NULL, NULL);
	if (w < 1 || h < 1 || w > (png_uint_32)max_side || h > (png_uint_32)max_side) {
		snprintf(r.first, sizeof r.first, "%lux%lu (max %d a side)", (unsigned long)w, (unsigned long)h, max_side);
		png_error(png, r.first);
	}
	alpha = (color & PNG_COLOR_MASK_ALPHA) || png_get_valid(png, info, PNG_INFO_tRNS);
	png_set_expand(png); /* a palette to RGB, grey under 8 bits to 8, tRNS to alpha */
#ifdef PNG_READ_SCALE_16_TO_8_SUPPORTED
	png_set_scale_16(png);
#else
	png_set_strip_16(png);
#endif
	png_set_gray_to_rgb(png);
	if (!alpha)
		png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
	(void)png_set_interlace_handling(png);
	png_read_update_info(png, info);
	if (png_get_rowbytes(png, info) != (size_t)w * 4)
		png_error(png, "an unexpected row size");

	cg = calloc(1, sizeof *cg);
	if (!cg)
		png_error(png, "out of memory");
	cg->pixels = malloc((size_t)w * h * 4);
	rows = malloc((size_t)h * sizeof *rows);
	if (!cg->pixels || !rows)
		png_error(png, "out of memory");
	for (png_uint_32 y = 0; y < h; y++)
		rows[y] = (png_bytep)cg->pixels + (size_t)y * w * 4;
	png_read_image(png, rows);

	/* The metrics libsys4's reader gives a PNG. */
	cg->type = ALCG_PNG;
	cg->metrics.w = (int)w;
	cg->metrics.h = (int)h;
	cg->metrics.bpp = 24;
	cg->metrics.has_pixel = true;
	cg->metrics.has_alpha = alpha;
	cg->metrics.pixel_pitch = 3;
	cg->metrics.alpha_pitch = 1;
	free(rows);
	png_destroy_read_struct(&png, &info, NULL);
	return cg;
}

bool rq_pixels_colors_ok(double hue, double saturation, double lightness, char *why, size_t why_len)
{
	if (!(hue >= -RQ_PIXELS_HUE_MAX && hue <= RQ_PIXELS_HUE_MAX)) {
		say(why, why_len, "texture colors: hue must be a number from -360 to 360");
		return false;
	}
	if (!(saturation >= 0.0 && saturation <= RQ_PIXELS_FACTOR_MAX)) {
		say(why, why_len, "texture colors: saturation must be a number from 0 to 4");
		return false;
	}
	if (!(lightness >= 0.0 && lightness <= RQ_PIXELS_FACTOR_MAX)) {
		say(why, why_len, "texture colors: lightness must be a number from 0 to 4");
		return false;
	}
	return true;
}

static uint8_t to_byte(double v)
{
	double t = floor(v * 255.0 + 0.5);

	return t < 0.0 ? 0 : t > 255.0 ? 255 : (uint8_t)t;
}

void rq_pixels_recolor(struct cg *cg, double hue, double saturation, double lightness)
{
	uint8_t *p = cg->pixels;
	size_t n = (size_t)cg->metrics.w * cg->metrics.h;

	for (size_t i = 0; i < n; i++, p += 4) {
		double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
		double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
		double l = (mx + mn) / 2.0, d = mx - mn, h = 0.0, s = 0.0;
		double c, x, m, r1, g1, b1;

		if (d > 0.0) {
			s = d / (1.0 - fabs(2.0 * l - 1.0));
			if (mx == r)
				h = 60.0 * fmod((g - b) / d, 6.0);
			else if (mx == g)
				h = 60.0 * ((b - r) / d + 2.0);
			else
				h = 60.0 * ((r - g) / d + 4.0);
		}
		h = fmod(h + hue, 360.0);
		if (h < 0.0)
			h += 360.0;
		if (h >= 360.0)
			h = 0.0;
		s = fmin(1.0, s * saturation);
		l = fmin(1.0, l * lightness);
		c = (1.0 - fabs(2.0 * l - 1.0)) * s;
		x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
		m = l - c / 2.0;
		switch ((int)floor(h / 60.0)) {
		case 0: r1 = c; g1 = x; b1 = 0.0; break;
		case 1: r1 = x; g1 = c; b1 = 0.0; break;
		case 2: r1 = 0.0; g1 = c; b1 = x; break;
		case 3: r1 = 0.0; g1 = x; b1 = c; break;
		case 4: r1 = x; g1 = 0.0; b1 = c; break;
		default: r1 = c; g1 = 0.0; b1 = x; break;
		}
		p[0] = to_byte(r1 + m);
		p[1] = to_byte(g1 + m);
		p[2] = to_byte(b1 + m);
	}
}

void rq_pixels_game_alpha(struct cg *mod, bool game_has_alpha, const struct cg *game)
{
	uint8_t *p = mod->pixels;
	int w = mod->metrics.w, h = mod->metrics.h;

	if (!game_has_alpha) {
		size_t n = (size_t)w * h;
		for (size_t i = 0; i < n; i++)
			p[i * 4 + 3] = 0xff;
	} else if (!mod->metrics.has_alpha && game && game->pixels && game->metrics.w > 0 && game->metrics.h > 0) {
		const uint8_t *g = game->pixels;
		size_t gw = (size_t)game->metrics.w, gh = (size_t)game->metrics.h;
		for (int y = 0; y < h; y++) {
			size_t gy = ((size_t)(2 * y + 1) * gh) / ((size_t)2 * h);
			for (int x = 0; x < w; x++) {
				size_t gx = ((size_t)(2 * x + 1) * gw) / ((size_t)2 * w);
				p[((size_t)y * w + x) * 4 + 3] = g[(gy * gw + gx) * 4 + 3];
			}
		}
	}
	mod->metrics.has_alpha = game_has_alpha;
}
