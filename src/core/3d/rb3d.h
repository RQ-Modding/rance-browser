/* The 3D view: RQ MOD's 3D renderer (the files beside this one, copied from the engine) drawing a
 * model of the game's ReignData.red into a texture as Rance Quest's TapirEngine draws it. The rest
 * of the browser includes only this header, so no header of the copy (its OpenGL, cglm, the
 * engine's types) reaches it.
 *
 * Every call here runs on the thread that owns the GL context, with it current. */
#ifndef RB3D_H
#define RB3D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Whether the view can draw with the current GL context: OpenGL 3.1 and GLSL 1.40, each call past
 * GL 1.1 found, and the renderer's three shader programs built and linked as the engine builds them
 * for Rance Quest. Asked once; later calls give the first answer. *why (may be NULL) says what is
 * missing. */
bool rb3d_ready(char *why, size_t n);

/* Runs fn(arg) so that a fatal error of the 3D code (libsys4's ERROR, which ends the engine) comes
 * back as false with its message in err (may be NULL), the browser still running; what the call
 * had built is left as it was (a viewer can afford the leak). A guard inside a guard runs fn under
 * the outer one. */
bool rb3d_guard(void (*fn)(void *arg), void *arg, char *err, size_t n);

struct rb3d_view;

/* A view of w by h pixels on the archive at red_path (the game's Data/ReignData.red). NULL, with a
 * message, when the view cannot draw here or the archive does not open. */
struct rb3d_view *rb3d_view_new(const char *red_path, int w, int h, char *err, size_t n);
void rb3d_view_free(struct rb3d_view *v);
bool rb3d_view_resize(struct rb3d_view *v, int w, int h, char *err, size_t n);

/* Loads the model of an archive folder ("Chara\\gokiburi": the folder's .POL, .amt and .opr, as
 * the game names a model), in the place of the one shown. False with a message when it fails; the
 * view then shows nothing. */
bool rb3d_view_load(struct rb3d_view *v, const char *folder, char *err, size_t n);

/* Plays a motion of the model's folder ("gokiburi_wait": its .MOT, with the .TXA beside it if
 * any) from its first frame, looped. NULL or "" stops it: the model in its rest pose. */
bool rb3d_view_motion(struct rb3d_view *v, const char *name, char *err, size_t n);

/* Draws the model, its motion moved on by ms milliseconds of the view's own clock (0: the same
 * frame again). False with a message when the drawing failed; the model is then dropped. */
bool rb3d_view_draw(struct rb3d_view *v, int ms, char *err, size_t n);

/* The texture the view draws into (a GL texture name; its rows run from the top) and its size. */
unsigned rb3d_view_texture(const struct rb3d_view *v, int *w, int *h);

#endif /* RB3D_H */
