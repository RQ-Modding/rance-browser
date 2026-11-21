/* In the place of RQ MOD's include/asset_manager.h: the renderer asks it for a CG by number only for
 * a background picture or a billboard the game's code sets by number, which the browser's view never
 * does, so there is none (rb3d.c answers NULL, which the renderer handles). */
#ifndef SYSTEM4_ASSET_MANAGER_H
#define SYSTEM4_ASSET_MANAGER_H

struct cg;

struct cg *asset_cg_load(int no);

#endif /* SYSTEM4_ASSET_MANAGER_H */
