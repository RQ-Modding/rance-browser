/* The browser's side of the copy of RQ MOD's 3D renderer (the other files of this folder, the
 * engine's own, unchanged): the calls the copied files make into the rest of the engine (OpenGL
 * past 1.1, the shaders by path, a sprite's texture, the clock, the game's folder), a guard that
 * turns the engine's fatal errors into messages, and the view of rb3d.h. */
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/cg.h"

#include "3d_internal.h"
#include "asset_manager.h"
#include "audio.h"
#include "reign.h"
#include "sact.h"
#include "vm.h"
#include "xsystem4.h"

#include "rb3d.h"

/* ---- OpenGL past 1.1 (gfx/gl.h) ---- */

#define RB3D_GL_DEFINE(type, name) type rb3d_##name;
RB3D_GL_CALLS(RB3D_GL_DEFINE)
#undef RB3D_GL_DEFINE

static bool gl_load(char *why, size_t n)
{
#define RB3D_GL_LOAD(type, name) \
	if (!(rb3d_##name = (type)SDL_GL_GetProcAddress(#name))) { \
		snprintf(why, n, "OpenGL has no %s", #name); \
		return false; \
	}
	RB3D_GL_CALLS(RB3D_GL_LOAD)
#undef RB3D_GL_LOAD
	return true;
}

/* ---- the guard ---- */

static struct {
	jmp_buf *jmp;
	SDL_threadID thread;
	void (*prev)(const char *msg);
	char msg[1024];
} guard;

static void guard_on_error(const char *msg)
{
	if (guard.jmp && SDL_ThreadID() == guard.thread) {
		snprintf(guard.msg, sizeof guard.msg, "%s", msg);
		longjmp(*guard.jmp, 1);
	}
	if (guard.prev)
		guard.prev(msg);
}

/* libsys4's "*ERROR*(path:function:line): text\n" as "text (file.c:line)". */
static void tidy_error(const char *msg, char *out, size_t n)
{
	const char *text = msg, *end;
	char where[160] = "";
	if (!strncmp(msg, "*ERROR*(", 8) && (end = strstr(msg, "): "))) {
		const char *in = msg + 8;
		const char *line = end, *func;
		while (line > in && line[-1] != ':')
			line--;
		func = line > in ? line - 1 : in;
		while (func > in && func[-1] != ':')
			func--;
		if (line > in && func > in) {
			const char *file = in, *s;
			for (s = in; s < func; s++)
				if (*s == '/' || *s == '\\')
					file = s + 1;
			snprintf(where, sizeof where, " (%.*s:%.*s)", (int)(func - file - 1), file,
			         (int)(end - line), line);
		}
		text = end + 3;
	}
	size_t len = strlen(text);
	while (len && (text[len - 1] == '\n' || text[len - 1] == '\r'))
		len--;
	snprintf(out, n, "%.*s%s", (int)len, text, where);
}

bool rb3d_guard(void (*fn)(void *arg), void *arg, char *err, size_t n)
{
	if (guard.jmp) {
		fn(arg);
		return true;
	}
	jmp_buf jmp;
	guard.prev = sys_error_handler;
	guard.thread = SDL_ThreadID();
	guard.msg[0] = 0;
	if (setjmp(jmp)) {
		guard.jmp = NULL;
		sys_error_handler = guard.prev;
		if (err && n)
			tidy_error(guard.msg, err, n);
		return false;
	}
	guard.jmp = &jmp;
	sys_error_handler = guard_on_error;
	fn(arg);
	guard.jmp = NULL;
	sys_error_handler = guard.prev;
	return true;
}

/* ---- the shaders (gfx/gfx.h), built into the program ---- */

extern const char rb3d_reign_v_glsl[], rb3d_reign_f_glsl[];
extern const char rb3d_reign_outline_v_glsl[], rb3d_reign_outline_f_glsl[];
extern const char rb3d_reign_shadow_v_glsl[], rb3d_reign_shadow_f_glsl[];

static const struct { const char *path, *text; } shader_files[] = {
	{ "shaders/reign.v.glsl", rb3d_reign_v_glsl },
	{ "shaders/reign.f.glsl", rb3d_reign_f_glsl },
	{ "shaders/reign_outline.v.glsl", rb3d_reign_outline_v_glsl },
	{ "shaders/reign_outline.f.glsl", rb3d_reign_outline_f_glsl },
	{ "shaders/reign_shadow.v.glsl", rb3d_reign_shadow_v_glsl },
	{ "shaders/reign_shadow.f.glsl", rb3d_reign_shadow_f_glsl },
};

/* As the engine's video.c builds a shader: its GLSL version, the defines, the file. */
GLuint gfx_load_shader_file(const char *path, GLenum type, const char *defines)
{
	const char *text = NULL;
	for (size_t i = 0; i < sizeof shader_files / sizeof shader_files[0]; i++)
		if (!strcmp(shader_files[i].path, path))
			text = shader_files[i].text;
	if (!text)
		ERROR("Failed to load shader file %s", path);
	const GLchar *source[3] = { "#version 140\n", defines ? defines : "", text };
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 3, source, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		GLint len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		char *log = xmalloc(len + 1);
		log[0] = 0;
		glGetShaderInfoLog(shader, len, NULL, log);
		ERROR("Failed to compile shader %s: %s", path, log);
	}
	return shader;
}

/* ---- textures and framebuffers (gfx/gfx.h) ---- */

static GLuint draw_fbo;
static GLint saved_fb, saved_vp[4];

/* Draws into t, as the engine's does; gfx_reset_framebuffer puts back the framebuffer and the
 * viewport that were bound before. */
GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h)
{
	glGetIntegerv(target == GL_READ_FRAMEBUFFER ? GL_READ_FRAMEBUFFER_BINDING : GL_DRAW_FRAMEBUFFER_BINDING, &saved_fb);
	glGetIntegerv(GL_VIEWPORT, saved_vp);
	if (!draw_fbo)
		glGenFramebuffers(1, &draw_fbo);
	glBindFramebuffer(target, draw_fbo);
	glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->handle, 0);
	if (glCheckFramebufferStatus(target) != GL_FRAMEBUFFER_COMPLETE)
		ERROR("Incomplete framebuffer");
	glViewport(x, y, w, h);
	return draw_fbo;
}

void gfx_reset_framebuffer(GLenum target, GLuint fbo)
{
	glBindFramebuffer(target, (GLuint)saved_fb);
	glViewport(saved_vp[0], saved_vp[1], saved_vp[2], saved_vp[3]);
}

void gfx_init_texture_with_cg(struct texture *t, struct cg *cg)
{
	GLint prev = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
	glGenTextures(1, &t->handle);
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
	t->w = cg->metrics.w;
	t->h = cg->metrics.h;
}

void gfx_delete_texture(struct texture *t)
{
	if (t->handle)
		glDeleteTextures(1, &t->handle);
	t->handle = 0;
	t->w = t->h = 0;
}

/* Draws the background pictures the game's code sets under a 3D view; the browser's view sets none,
 * so there is nothing to draw. */
void gfx_copy_stretch_blend_amap_alpha(struct texture *dst, int dx, int dy, int dw, int dh,
                                       struct texture *src, int sx, int sy, int sw, int sh, int a)
{
}

/* ---- the sprites (sact.h): one per view, numbered from 1 as the engine's are ---- */

#define MAX_VIEWS 8

static struct sact_sprite sprites[MAX_VIEWS + 1];
static bool sprite_taken[MAX_VIEWS + 1];

struct sact_sprite *sact_try_get_sprite(int sp)
{
	return sp >= 1 && sp <= MAX_VIEWS && sprite_taken[sp] ? &sprites[sp] : NULL;
}

void sprite_bind_plugin(struct sact_sprite *sp, struct draw_plugin *plugin)
{
	sp->plugin = plugin;
}

struct texture *sprite_get_texture(struct sact_sprite *sp)
{
	return &sp->texture;
}

void sprite_init_color(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a)
{
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	uint8_t *px = xmalloc((size_t)w * h * 4);
	for (size_t i = 0; i < (size_t)w * h; i++) {
		px[i * 4 + 0] = (uint8_t)r;
		px[i * 4 + 1] = (uint8_t)g;
		px[i * 4 + 2] = (uint8_t)b;
		px[i * 4 + 3] = (uint8_t)a;
	}
	GLint prev = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
	if (sp->texture.handle)
		glDeleteTextures(1, &sp->texture.handle);
	glGenTextures(1, &sp->texture.handle);
	glBindTexture(GL_TEXTURE_2D, sp->texture.handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
	free(px);
	sp->texture.w = w;
	sp->texture.h = h;
}

void sprite_set_pos(struct sact_sprite *sp, int x, int y)
{
	sp->x = x;
	sp->y = y;
}

/* ---- the rest of the engine ---- */

/* The clock of the view being drawn, in milliseconds (vm.h). */
static uint64_t view_ticks;

uint64_t vm_game_ticks(void)
{
	return view_ticks;
}

/* The archive a view opens (xsystem4.h): the renderer asks for Data/ReignData.red by name. */
static const char *red_path;

char *gamedir_path(const char *path)
{
	return strdup(red_path ? red_path : path);
}

const char *display_utf0(const char *utf)
{
	return utf;
}

struct cg *asset_cg_load(int no)
{
	return NULL;
}

int wav_unprepare(int id) { return 0; }
int wav_play(int id) { return 0; }
int wav_stop(int id) { return 0; }
int wav_seek(int id, int pos) { return 0; }
int wav_get_unused_channel(void) { return -1; }
int wav_prepare_from_archive_data(int id, int metadata_no, struct archive_data *dfile) { return 0; }

/* The engine's debug.c (left out): its JSON of a sprite and its debugger's hooks. */
void re_debug_init(void)
{
}

cJSON *RE_to_json(struct sact_sprite *sp, bool verbose)
{
	return NULL;
}

/* ---- whether the view can draw ---- */

static void build_renderer(void *arg)
{
	re_plugin_version = RE_TAPIR_PLUGIN;
	RE_renderer_free(RE_renderer_new());
}

static bool version_at_least(const char *s, int major, int minor)
{
	int a = 0, b = 0;
	if (!s || sscanf(s, "%d.%d", &a, &b) < 2)
		return false;
	return a > major || (a == major && b >= minor);
}

bool rb3d_ready(char *why, size_t n)
{
	static int state;  /* 0: not asked; 1: yes; -1: no, for ready_why */
	static char ready_why[512];
	if (!state) {
		const char *ver = (const char *)glGetString(GL_VERSION);
		const char *glsl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
		if (!version_at_least(ver, 3, 1)) {
			snprintf(ready_why, sizeof ready_why, "The 3D view needs OpenGL 3.1; this PC's is %s",
			         ver ? ver : "unknown");
		} else if (!version_at_least(glsl, 1, 40)) {
			snprintf(ready_why, sizeof ready_why, "The 3D view needs GLSL 1.40; this PC's is %s",
			         glsl ? glsl : "unknown");
		} else if (gl_load(ready_why, sizeof ready_why)) {
			char err[400];
			if (rb3d_guard(build_renderer, NULL, err, sizeof err))
				state = 1;
			else
				snprintf(ready_why, sizeof ready_why, "The 3D view's shaders do not build here: %s", err);
		}
		if (!state)
			state = -1;
	}
	if (state < 0 && why && n)
		snprintf(why, n, "%s", ready_why);
	return state > 0;
}

/* ---- the view ---- */

struct rb3d_view {
	int sprite;
	struct RE_plugin *plugin;
	int inst;                 /* the model's instance, or -1 */
	uint64_t clock;           /* ms */
	float yaw, pitch;         /* the camera, in degrees, around the model's centre */
	float fov;                /* horizontal, in degrees */
};

static struct RE_instance *view_inst(const struct rb3d_view *v)
{
	return v->inst >= 0 && v->inst < v->plugin->nr_instances ? v->plugin->instances[v->inst] : NULL;
}

struct view_new_args {
	struct rb3d_view *v;
	const char *red;
	int w, h;
};

static void view_new_fn(void *arg)
{
	struct view_new_args *a = arg;
	struct rb3d_view *v = a->v;
	red_path = a->red;
	v->plugin = RE_plugin_new(RE_TAPIR_PLUGIN);
	red_path = NULL;
	if (!v->plugin)
		return;
	view_ticks = v->clock;
	sprite_init_color(&sprites[v->sprite], a->w, a->h, 0, 0, 0, 255);
	RE_plugin_bind(v->plugin, v->sprite);
	RE_set_viewport(v->plugin, 0, 0, a->w, a->h);
}

struct rb3d_view *rb3d_view_new(const char *red_path_in, int w, int h, char *err, size_t n)
{
	if (!rb3d_ready(err, n))
		return NULL;
	int sp = 0;
	for (int i = 1; i <= MAX_VIEWS && !sp; i++)
		if (!sprite_taken[i])
			sp = i;
	if (!sp) {
		snprintf(err, n, "Too many 3D views");
		return NULL;
	}
	struct rb3d_view *v = xcalloc(1, sizeof *v);
	v->sprite = sp;
	v->inst = -1;
	v->yaw = 30.0f;
	v->pitch = -15.0f;
	v->fov = 30.0f;
	sprite_taken[sp] = true;
	memset(&sprites[sp], 0, sizeof sprites[sp]);
	struct view_new_args a = { v, red_path_in, w, h };
	bool ok = rb3d_guard(view_new_fn, &a, err, n);
	red_path = NULL;
	if (ok && !v->plugin)
		snprintf(err, n, "Could not open %s", red_path_in);
	if (!ok || !v->plugin) {
		/* A plugin half bound is left as it is; its sprite stays taken, so nothing draws with it. */
		if (!v->plugin) {
			gfx_delete_texture(&sprites[sp].texture);
			sprite_taken[sp] = false;
		}
		free(v);
		return NULL;
	}
	return v;
}

void rb3d_view_free(struct rb3d_view *v)
{
	if (!v)
		return;
	RE_plugin_free(v->plugin);
	gfx_delete_texture(&sprites[v->sprite].texture);
	sprite_taken[v->sprite] = false;
	free(v);
}

struct resize_args {
	struct rb3d_view *v;
	int w, h;
};

static void resize_fn(void *arg)
{
	struct resize_args *a = arg;
	RE_set_viewport(a->v->plugin, 0, 0, a->w, a->h);
}

bool rb3d_view_resize(struct rb3d_view *v, int w, int h, char *err, size_t n)
{
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	struct texture *t = &sprites[v->sprite].texture;
	if (t->w == w && t->h == h)
		return true;
	struct resize_args a = { v, w, h };
	return rb3d_guard(resize_fn, &a, err, n);
}

/* The bones of a model with no motion, as identity: its rest pose. The renderer's outline reads them
 * for a boned model whether a motion plays or not. */
static void rest_bones(struct RE_instance *inst)
{
	if (!inst->model || inst->model->nr_bones <= 0 || !inst->bone_transforms_ubo)
		return;
	for (int i = 0; i < inst->model->nr_bones; i++) {
		memset(inst->bone_transforms[i], 0, sizeof(mat3x4));
		inst->bone_transforms[i][0][0] = inst->bone_transforms[i][1][1] = inst->bone_transforms[i][2][2] = 1.0f;
	}
	glBindBuffer(GL_UNIFORM_BUFFER, inst->bone_transforms_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, inst->model->nr_bones * sizeof(mat3x4), inst->bone_transforms);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static void drop_model(struct rb3d_view *v)
{
	if (v->inst >= 0)
		RE_release_instance(v->plugin, v->inst);
	v->inst = -1;
}

struct load_args {
	struct rb3d_view *v;
	const char *folder;
	bool ok;
};

/* A model with bones as a skinned instance, else as a static one, as the game's code makes them;
 * the second load of the same folder is the plugin's cached model. */
static void load_fn(void *arg)
{
	struct load_args *a = arg;
	struct rb3d_view *v = a->v;
	struct RE_plugin *p = v->plugin;
	int no = RE_create_instance(p);
	struct RE_instance *inst = p->instances[no];
	v->inst = no;
	RE_instance_set_type(inst, RE_ITYPE_SKINNED);
	if (!RE_instance_load(inst, a->folder) || !inst->model)
		return;
	if (inst->model->nr_bones == 0) {
		RE_release_instance(p, no);
		no = RE_create_instance(p);
		inst = p->instances[no];
		v->inst = no;
		RE_instance_set_type(inst, RE_ITYPE_STATIC);
		if (!RE_instance_load(inst, a->folder) || !inst->model)
			return;
	}
	inst->draw = true;
	rest_bones(inst);
	a->ok = true;
}

bool rb3d_view_load(struct rb3d_view *v, const char *folder, char *err, size_t n)
{
	drop_model(v);
	struct load_args a = { v, folder, false };
	if (!rb3d_guard(load_fn, &a, err, n)) {
		drop_model(v);
		return false;
	}
	if (!a.ok) {
		drop_model(v);
		snprintf(err, n, "%s: no model there, or it does not read", folder);
		return false;
	}
	return true;
}

struct motion_args {
	struct rb3d_view *v;
	const char *name;
	bool ok;
};

static void motion_fn(void *arg)
{
	struct motion_args *a = arg;
	struct RE_instance *inst = view_inst(a->v);
	if (!inst || !inst->model)
		return;
	if (!a->name || !a->name[0]) {
		if (inst->motion)
			motion_free(inst->motion);
		inst->motion = NULL;
		rest_bones(inst);
		a->ok = true;
		return;
	}
	if (!RE_instance_load_motion(inst, a->name) || !inst->motion)
		return;
	float last = inst->motion->mot->nr_frames > 0 ? (float)(inst->motion->mot->nr_frames - 1) : 0.0f;
	RE_motion_set_frame_range(inst->motion, 0, last);
	RE_motion_set_loop_frame_range(inst->motion, 0, last);
	RE_motion_set_frame(inst->motion, 0);
	RE_motion_set_state(inst->motion, RE_MOTION_STATE_LOOP);
	a->ok = true;
}

bool rb3d_view_motion(struct rb3d_view *v, const char *name, char *err, size_t n)
{
	struct motion_args a = { v, name, false };
	if (!rb3d_guard(motion_fn, &a, err, n)) {
		/* RE_instance_load_motion frees the motion before it reads the new one: what is left is
		 * the rest pose. */
		struct RE_instance *inst = view_inst(v);
		if (inst) {
			inst->motion = NULL;
			rest_bones(inst);
		}
		return false;
	}
	if (!a.ok) {
		snprintf(err, n, "%s: no motion there, or it does not read", name ? name : "");
		return false;
	}
	return true;
}

/* The camera around the model's box: its centre, far enough for the whole box to show. */
static void place_camera(struct rb3d_view *v, int w, int h)
{
	struct RE_plugin *p = v->plugin;
	struct RE_instance *inst = view_inst(v);
	vec3 c = { 0, 1, 0 };
	float r = 1.0f;
	if (inst && inst->model) {
		glm_vec3_center(inst->model->aabb[0], inst->model->aabb[1], c);
		r = glm_vec3_distance(inst->model->aabb[0], inst->model->aabb[1]) / 2;
		if (r < 0.01f)
			r = 0.01f;
	}
	float hfov = glm_rad(v->fov);
	float vfov = 2 * atanf(tanf(hfov / 2) * h / w);
	float d = r / sinf((hfov < vfov ? hfov : vfov) / 2) * 1.05f;
	p->camera.pitch = v->pitch;
	p->camera.yaw = v->yaw;
	p->camera.roll = 0;
	vec3 front = { 0, 0, -1 }, euler = { glm_rad(v->pitch), glm_rad(v->yaw), 0 };
	mat4 rot;
	glm_euler_yxz(euler, rot);
	glm_mat4_mulv3(rot, front, 0.0f, front);
	glm_vec3_scale(front, -d, front);
	glm_vec3_add(c, front, p->camera.pos);
	float znear = d - 2 * r;
	if (znear < d * 0.02f)
		znear = d * 0.02f;
	RE_set_projection(p, (float)w, (float)h, znear, d + 2 * r, v->fov);
}

struct draw_args {
	struct rb3d_view *v;
	int ms;
};

static void draw_fn(void *arg)
{
	struct draw_args *a = arg;
	struct rb3d_view *v = a->v;
	struct sact_sprite *sp = &sprites[v->sprite];
	v->clock += a->ms > 0 ? (uint64_t)a->ms : 0;
	view_ticks = v->clock;
	RE_update_model(v->plugin);
	place_camera(v, sp->texture.w, sp->texture.h);
	RE_render(sp);
}

bool rb3d_view_draw(struct rb3d_view *v, int ms, char *err, size_t n)
{
	GLint fb = 0, vp[4], vao = 0, prog = 0, tex = 0, active = 0;
	GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean depth_mask = GL_TRUE;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
	glGetIntegerv(GL_VIEWPORT, vp);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
	glDisable(GL_SCISSOR_TEST);

	struct draw_args a = { v, ms };
	bool ok = rb3d_guard(draw_fn, &a, err, n);

	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	glBindVertexArray((GLuint)vao);
	glUseProgram((GLuint)prog);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
	glActiveTexture((GLenum)active);
	glDepthMask(depth_mask);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	if (blend)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
	if (scissor)
		glEnable(GL_SCISSOR_TEST);
	if (!ok)
		drop_model(v);
	return ok;
}

unsigned rb3d_view_texture(const struct rb3d_view *v, int *w, int *h)
{
	const struct texture *t = &sprites[v->sprite].texture;
	if (w)
		*w = t->w;
	if (h)
		*h = t->h;
	return t->handle;
}
