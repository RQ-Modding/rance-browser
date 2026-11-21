/* What the files of rance_3d share past rb3d.h: a view's plugin and its model's instance (rb3d.c),
 * for the export (rb3d_export.c). Only for files that include the copy's 3d_internal.h. */
#ifndef RB3D_PRIVATE_H
#define RB3D_PRIVATE_H

struct rb3d_view;
struct RE_plugin;
struct RE_instance;

struct RE_plugin *rb3d_view_plugin(struct rb3d_view *v);
/* The model's instance, or NULL when the view shows none. */
struct RE_instance *rb3d_view_instance(struct rb3d_view *v);

#endif /* RB3D_PRIVATE_H */
