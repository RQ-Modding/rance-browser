/* What the files of rance_3d share past rb3d.h: a view's plugin and its model's instance (rb3d.c),
 * for the export (rb3d_export.c) and the textures (rb3d_textures.c). Only for files that include the
 * copy's 3d_internal.h. */
#ifndef RB3D_PRIVATE_H
#define RB3D_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>

struct rb3d_view;
struct RE_plugin;
struct RE_instance;

struct RE_plugin *rb3d_view_plugin(struct rb3d_view *v);
/* The model's instance, or NULL when the view shows none. */
struct RE_instance *rb3d_view_instance(struct rb3d_view *v);

/* The picture a try gives the texture name of the model at path, loaded as kind (model.c's
 * question to RE_mod_texture), as a new 8-bit RGBA cg, or NULL for the game's (rb3d_textures.c). */
struct cg;
struct archive_data;
struct rb3d_tries;
struct cg *rb3d_tries_answer(struct rb3d_tries *t, struct archive_data *game, const char *path, const char *name, int kind);

/* --check: every UV map of the model the view shows drawn, each group's triangles in it against the
 * engine's mesh made from that group (its nr_vertices / 3). False with a message at the first that
 * differs. */
struct rb3d_uv_counts {
	int maps;                       /* UV maps drawn */
	int parts;                      /* a mesh's group drawn on a map */
	int triangles;                  /* triangles drawn */
	int skipped;                    /* triangles left out: an index past its list */
	int light_meshes_without_uvs;   /* meshes on a light-mapped material without light UVs */
	int light_models_without_uvs;   /* the models that have such meshes */
};
bool rb3d_view_uv_check(struct rb3d_view *v, struct rb3d_uv_counts *c, char *why, size_t n);

#endif /* RB3D_PRIVATE_H */
