/* In the place of RQ MOD's include/sact.h and sprite.h: the one sprite the 3D view draws into, its
 * texture and the plugin bound to it, with the seven calls the renderer makes on a sprite (rb3d.c).
 * The engine's sprites live in its scene; the browser has none. */
#ifndef SYSTEM4_SACT_H
#define SYSTEM4_SACT_H

#include <stdbool.h>
#include "gfx/gfx.h"
#include "plugin.h"

struct sact_sprite {
	struct draw_plugin *plugin;
	struct texture texture;
	int x, y;
	bool show;
	bool dirty;
};

struct sact_sprite *sact_try_get_sprite(int sp);
void sprite_bind_plugin(struct sact_sprite *sp, struct draw_plugin *plugin);
struct texture *sprite_get_texture(struct sact_sprite *sp);
void sprite_init_color(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a);
void sprite_set_pos(struct sact_sprite *sp, int x, int y);

static inline void sprite_dirty(struct sact_sprite *sp) { sp->dirty = true; }
static inline void sprite_set_show(struct sact_sprite *sp, bool show) { sp->show = show; }

#endif /* SYSTEM4_SACT_H */
