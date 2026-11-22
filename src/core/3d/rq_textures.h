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
 * rq_textures: the textures one of the game's 3D models uses, as its .POL names them and as
 * model.c loads them, read without GL; the checks a mod's picture for one of them passes, with
 * the texts both programs show; and the mark of a mod's model with textures of its own. Rance
 * Browser keeps a copy (src/core/3d/rq_textures.c and .h): change both together.
 */

#ifndef RQ_TEXTURES_H
#define RQ_TEXTURES_H

#include <stdbool.h>
#include <stddef.h>

struct archive;

/* How a model's materials use a texture (bits). */
#define RQ_TEXTURE_COLOR    (1u << 0)
#define RQ_TEXTURE_ALPHA    (1u << 1)
#define RQ_TEXTURE_LIGHT    (1u << 2)
#define RQ_TEXTURE_NORMAL   (1u << 3)
#define RQ_TEXTURE_SPECULAR (1u << 4)

struct rq_texture {
	char *name;     /* as the model's .POL names it (Shift-JIS) */
	unsigned kinds; /* RQ_TEXTURE_* bits */
	bool alpha_map; /* a material uses this colour file as its own alpha map */
	int frames;     /* 1, or for a colour map the pictures the game animates (name[1].png...) */
	bool missing;   /* the archive has no such file: no w, h or alpha */
	int w, h;       /* the game's picture's size, in pixels */
	bool alpha;     /* the game's picture has alpha */
};

struct rq_texture_list {
	int n;
	struct rq_texture *rows; /* in the order the .POL first names them, no empty name */
};

/* The textures of the model in `folder` (the archive's path, \ or /), or NULL when the folder has
 * no .POL (or one that cannot be read). Free with rq_textures_free. */
struct rq_texture_list *rq_textures_list(struct archive *aar, const char *folder);
void rq_textures_free(struct rq_texture_list *l);

/* The row named `name` (Shift-JIS, any ASCII case, as the archive matches names), or NULL. */
const struct rq_texture *rq_textures_find(const struct rq_texture_list *l, const char *name);

/* Whether two texture names name the same file of the archive (ASCII case and / ignored). */
bool rq_textures_same_name(const char *a, const char *b);

/* "color" for a row any material uses as a colour map, else "alpha", "light", "normal" or
 * "specular". */
const char *rq_textures_kind(unsigned kinds);

/* For a frame's name as model.c makes it ("face[2].png"): its number (2), with the base's name
 * ("face.png") in `base`. 0 for any other name. */
int rq_textures_frame(const char *name, char *base, size_t base_len);

/* rq_textures_answer's answers other than a picture's index. */
#define RQ_TEXTURES_GAME   (-1)
#define RQ_TEXTURES_COLORS (-2)

/*
 * What a change answers when a model loads one of its textures, the rule both programs' hooks
 * follow: `frame` is rq_textures_frame's number for the name asked (0 for the base itself), `kind`
 * the POL's texture kind it is loaded as (COLOR_MAP, ALPHA_MAP and so on), `colors` whether the
 * change is colors by numbers and `pictures` its number of pictures. Colors answer a colour map
 * only (RQ_TEXTURES_COLORS: the game's own picture, recoloured); pictures answer any kind, with the
 * frame-th picture's index. RQ_TEXTURES_GAME (the game's own picture) for anything else.
 */
int rq_textures_answer(int frame, int kind, bool colors, int pictures);

/*
 * Whether a mod may change the texture `name` (Shift-JIS) of the model `l` lists (NULL: the folder
 * has no model) with `pictures` pictures (a PNG, or a list of them), or with colors (`pictures`
 * 0), or only whether the texture is one a mod may change (`pictures` -1, as for putting the
 * game's back). On a refusal `why` holds the reason, with the texture shown as `shown` (the path or the name
 * the mod gave, UTF-8) and the model as `folder` (its folder with /, UTF-8).
 */
bool rq_textures_check(const struct rq_texture_list *l, const char *folder, const char *name,
		       const char *shown, int pictures, char *why, size_t why_len);

/*
 * A mod's model with textures of its own loads from a folder marked "\*<number>\" (its model
 * number; no name in an archive holds a '*'), so the game keeps a copy of it apart; its files are
 * the folder's without the mark. rq_textures_mark gives the number in a path, or 0;
 * rq_textures_strip_mark takes the mark out of a path in place.
 */
int rq_textures_mark(const char *path);
void rq_textures_strip_mark(char *path);

#endif /* RQ_TEXTURES_H */
