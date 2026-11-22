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
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/hashtable.h"

#include "3d_internal.h"
#include "asset_manager.h"
#include "audio.h"
#include "reign.h"
#include "sact.h"
#include "vm.h"
#include "xsystem4.h"

#include "rb3d.h"
#include "rb3d_private.h"

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

/* While a view draws over white (white_back), its first background picture is plain white. */
static bool white_back;

/* Draws a background picture the game's code sets under a 3D view. The browser's view sets one
 * only to draw over white: the renderer calls this between its clear and the models, into the
 * frame being drawn. */
void gfx_copy_stretch_blend_amap_alpha(struct texture *dst, int dx, int dy, int dw, int dh,
                                       struct texture *src, int sx, int sy, int sw, int sh, int a)
{
	if (!white_back)
		return;
	glClearColor(1.f, 1.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.f, 0.f, 0.f, 1.f);
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

/* The game's 3D options (reign.h): the views draw without anti-aliasing, as a game whose Config
 * leaves it off. */
struct RE_options RE_options;

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

/* The tries of the load under way (rb3d_view_load sets them around its guarded load), or NULL. */
static struct rb3d_tries *load_tries;

/* The engine's modloader_texture.c (left out): a mod's picture on one of a model's textures, here
 * the view's tries (rb3d_textures.c), by the engine's rule. */
struct cg *RE_mod_texture(struct archive_data *game, const char *path, const char *name, int kind)
{
	return load_tries ? rb3d_tries_answer(load_tries, game, path, name, kind) : NULL;
}

/* The engine's debug.c (left out): its JSON of a sprite and its debugger's hooks. */
void re_debug_init(void)
{
}

cJSON *RE_to_json(struct sact_sprite *sp, bool verbose)
{
	return NULL;
}

/* ---- the browser's own drawing: the see-through picture and the lines ---- */

/* The alpha from a frame drawn over black and the same frame over white: what lets the white
 * through is see-through by that much, and the colour over black is the colour times the alpha. */
static const char matte_vs[] =
	"#version 140\n"
	"void main() {\n"
	"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
	"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";
static const char matte_fs[] =
	"#version 140\n"
	"uniform sampler2D black_tex;\n"
	"uniform sampler2D white_tex;\n"
	"out vec4 color;\n"
	"void main() {\n"
	"	ivec2 p = ivec2(gl_FragCoord.xy);\n"
	"	vec3 b = texelFetch(black_tex, p, 0).rgb;\n"
	"	vec3 w = texelFetch(white_tex, p, 0).rgb;\n"
	"	float a = clamp(1.0 - max(max(w.r - b.r, w.g - b.g), w.b - b.b), 0.0, 1.0);\n"
	"	color = a > 0.0 ? vec4(clamp(b / a, 0.0, 1.0), a) : vec4(0.0);\n"
	"}\n";
static const char line_vs[] =
	"#version 140\n"
	"in vec3 pos;\n"
	"uniform mat4 mvp;\n"
	"void main() { gl_Position = mvp * vec4(pos, 1.0); }\n";
static const char line_fs[] =
	"#version 140\n"
	"uniform vec4 color;\n"
	"out vec4 out_color;\n"
	"void main() { out_color = color; }\n";

static struct {
	bool ok;
	GLuint matte, line;      /* the programs */
	GLint line_mvp, line_color;
	GLuint vao, line_vao, line_vbo, fbo;
} own;

static GLuint own_shader(GLenum type, const char *src, char *log, size_t n)
{
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(sh, (GLsizei)n, NULL, log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

static GLuint own_program(const char *vs, const char *fs, char *log, size_t n)
{
	GLuint v = own_shader(GL_VERTEX_SHADER, vs, log, n);
	GLuint f = v ? own_shader(GL_FRAGMENT_SHADER, fs, log, n) : 0;
	if (!f) {
		if (v)
			glDeleteShader(v);
		return 0;
	}
	GLuint p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	glBindAttribLocation(p, 0, "pos");
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		glGetProgramInfoLog(p, (GLsizei)n, NULL, log);
		glDeleteProgram(p);
		return 0;
	}
	return p;
}

/* False when a program does not build: the view then draws over black only, with no lines. */
static bool own_init(char *log, size_t n)
{
	GLint prog = 0, vao = 0, buf = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buf);
	own.matte = own_program(matte_vs, matte_fs, log, n);
	own.line = own.matte ? own_program(line_vs, line_fs, log, n) : 0;
	if (!own.line)
		return false;
	glUseProgram(own.matte);
	glUniform1i(glGetUniformLocation(own.matte, "black_tex"), 0);
	glUniform1i(glGetUniformLocation(own.matte, "white_tex"), 1);
	own.line_mvp = glGetUniformLocation(own.line, "mvp");
	own.line_color = glGetUniformLocation(own.line, "color");
	glGenVertexArrays(1, &own.vao);
	glGenVertexArrays(1, &own.line_vao);
	glGenBuffers(1, &own.line_vbo);
	glBindVertexArray(own.line_vao);
	glBindBuffer(GL_ARRAY_BUFFER, own.line_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), NULL);
	glGenFramebuffers(1, &own.fbo);
	glUseProgram((GLuint)prog);
	glBindVertexArray((GLuint)vao);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buf);
	return true;
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
			if (rb3d_guard(build_renderer, NULL, err, sizeof err)) {
				state = 1;
				char log[512] = "";
				own.ok = own_init(log, sizeof log);
				if (!own.ok)
					WARNING("The 3D view's own shaders do not build here (no see-through pictures, no lines): %s", log);
			} else {
				snprintf(ready_why, sizeof ready_why, "The 3D view's shaders do not build here: %s", err);
			}
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
	int bg;                   /* the fight background's instance, or -1 */
	char *bg_path;            /* its path, kept in the plugin's cache when another model loads */
	/* the motion */
	int play_state;           /* how it plays: RE_MOTION_STATE_LOOP or _NOLOOP */
	bool hold;                /* the next draw shows the frame as it is */
	float frame;              /* the frame the last draw showed */
	uint32_t last_ms;         /* the clock at the last draw */
	bool clock_set;
	/* the pictures */
	GLuint tex_white, tex_matte;
	int tw, th;
	unsigned shown;           /* the texture of the last draw */
	/* what the model is */
	struct rb3d_info info;
	float *walk;              /* the walk mesh's edges, 6 floats each, in the model's space */
	int nwalk;
	float *heads;             /* each bone's head in the rest pose, 3 floats each */
	vec3 (*bone_boxes)[2];    /* each bone's box: the vertices it moves, at rest (invalid: none) */
	vec3 motion_box[2];       /* the box the model fills through its motion's frames, in its space */
	bool has_motion_box;
	struct rb3d_tries *tries; /* PNGs tried on the model's textures (rb3d_view_set_tries), or NULL */
};

static struct RE_instance *instance_at(const struct rb3d_view *v, int i)
{
	return i >= 0 && i < v->plugin->nr_instances ? v->plugin->instances[i] : NULL;
}

static struct RE_instance *view_inst(const struct rb3d_view *v)
{
	return instance_at(v, v->inst);
}

struct RE_plugin *rb3d_view_plugin(struct rb3d_view *v)
{
	return v->plugin;
}

struct RE_instance *rb3d_view_instance(struct rb3d_view *v)
{
	return view_inst(v);
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
	/* The game's 3D level 3 (its default): the light maps on, the outline on (the plugin's own
	 * default), the shadow straight down with its bias (a fight's SetShadowParam). */
	v->plugin->light_map_mode = 1;
	v->plugin->shadow_bias = 0.01f;
	glm_vec3_copy((vec3){ 0.f, -1.f, 0.f }, v->plugin->shadow_map_light_dir);
	view_ticks = 0;
	sprite_init_color(&sprites[v->sprite], a->w, a->h, 0, 0, 0, 255);
	RE_plugin_bind(v->plugin, v->sprite);
	RE_set_viewport(v->plugin, 0, 0, a->w, a->h);
}

struct rb3d_view *rb3d_view_new(const char *red_path_in, int w, int h, char *err, size_t n)
{
	if (!rb3d_ready(err, n))
		return NULL;
	if (w < 1) w = 1;
	if (h < 1) h = 1;
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
	v->inst = v->bg = -1;
	v->play_state = RE_MOTION_STATE_LOOP;
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

static void forget_overlays(struct rb3d_view *v)
{
	free(v->walk);
	free(v->heads);
	free(v->bone_boxes);
	v->walk = v->heads = NULL;
	v->bone_boxes = NULL;
	v->nwalk = 0;
}

void rb3d_view_free(struct rb3d_view *v)
{
	if (!v)
		return;
	RE_plugin_free(v->plugin);
	gfx_delete_texture(&sprites[v->sprite].texture);
	sprite_taken[v->sprite] = false;
	if (v->tex_white)
		glDeleteTextures(1, &v->tex_white);
	if (v->tex_matte)
		glDeleteTextures(1, &v->tex_matte);
	forget_overlays(v);
	free(v->bg_path);
	rb3d_tries_free(v->tries);
	free(v);
}

void rb3d_view_set_tries(struct rb3d_view *v, struct rb3d_tries *t)
{
	if (v->tries != t)
		rb3d_tries_free(v->tries);
	v->tries = t;
}

struct rb3d_tries *rb3d_view_tries(struct rb3d_view *v)
{
	return v->tries;
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
	bool ok = rb3d_guard(resize_fn, &a, err, n);
	v->shown = 0;
	return ok;
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

static void drop_background(struct rb3d_view *v)
{
	if (v->bg >= 0)
		RE_release_instance(v->plugin, v->bg);
	v->bg = -1;
}

static void drop_model(struct rb3d_view *v)
{
	if (v->inst >= 0)
		RE_release_instance(v->plugin, v->inst);
	v->inst = -1;
	forget_overlays(v);
	memset(&v->info, 0, sizeof v->info);
	v->has_motion_box = false;
}

/* The box the model fills through the frames first to last of its motion, for the orbit camera, so
 * a motion that carries the model away (a monster's leap at the camera) stays in the frame: each
 * frame's bones posed as the engine's update_bones poses them, each bone's box moved by its skin
 * matrix (a vertex lies between the places its bones move it to, so their boxes hold it), and the
 * model's own box at rest. */
static void set_motion_box(struct rb3d_view *v, struct RE_instance *inst, int first, int last)
{
	v->has_motion_box = false;
	if (!inst || !inst->model || !inst->motion || !inst->motion->mot || !v->bone_boxes)
		return;
	struct model *mdl = inst->model;
	struct mot *mot = inst->motion->mot;
	int nb = mdl->nr_bones;
	if (nb <= 0 || nb > MAX_BONES || (int)mot->nr_bones != nb || mot->nr_frames == 0)
		return;
	if (first < 0)
		first = 0;
	if (last > (int)mot->nr_frames - 1)
		last = (int)mot->nr_frames - 1;
	glm_vec3_copy(mdl->aabb[0], v->motion_box[0]);
	glm_vec3_copy(mdl->aabb[1], v->motion_box[1]);
	static mat4 posed[MAX_BONES];
	for (int f = first; f <= last; f++) {
		for (int i = 0; i < nb; i++) {
			const struct mot_frame *mf = &mot->motions[i]->frames[f];
			mat4 t;
			glm_translate_make(t, (float *)mf->pos);
			glm_quat_rotate(t, (float *)mf->rotq, t);
			int parent = mdl->bones[i].parent;
			if (parent >= 0 && parent < i)
				glm_mat4_mul(posed[parent], t, t);
			glm_mat4_copy(t, posed[i]);
			if (!glm_aabb_isvalid(v->bone_boxes[i]))
				continue;
			mat4 skin;
			vec3 moved[2];
			glm_mat4_mul(t, mdl->bones[i].inverse_bind_matrix, skin);
			glm_aabb_transform(v->bone_boxes[i], skin, moved);
			glm_aabb_merge(v->motion_box, moved, v->motion_box);
		}
	}
	v->has_motion_box = true;
}

struct keep_model {
	struct model *keep;
};

static void free_cached_model(struct ht_slot *slot, void *data)
{
	struct keep_model *k = data;
	if (slot->value && slot->value != k->keep)
		model_free(slot->value);
}

/* The plugin caches each model it loads until it is freed (every model at once would be gigabytes
 * of textures): before another model loads, the cache keeps only the fight background's. */
static void forget_models(struct rb3d_view *v)
{
	struct RE_plugin *p = v->plugin;
	struct keep_model k = { NULL };
	if (v->bg_path)
		k.keep = ht_get(p->model_cache, v->bg_path, NULL);
	ht_foreach(p->model_cache, free_cached_model, &k);
	ht_free(p->model_cache);
	p->model_cache = ht_create(32);
	if (k.keep)
		ht_put(p->model_cache, v->bg_path, NULL)->value = k.keep;
}

static void count_texture(const char ***names, int *n, const char *name)
{
	if (!name || !name[0])
		return;
	for (int i = 0; i < *n; i++)
		if (!strcmp((*names)[i], name))
			return;
	*names = xrealloc(*names, (*n + 1) * sizeof **names);
	(*names)[(*n)++] = name;
}

static void count_material_textures(const char ***names, int *n, const struct pol_material_group *g, int depth)
{
	for (int t = 0; t < MAX_TEXTURE_TYPE; t++)
		count_texture(names, n, g->m.textures[t]);
	if (depth < 4)
		for (uint32_t c = 0; c < g->nr_children; c++)
			count_material_textures(names, n, &g->children[c], depth + 1);
}

/* What the POL holds beyond the drawn model: its version, its triangles, the walk mesh and the
 * pictures its materials name. */
static void read_pol_facts(struct rb3d_view *v, const char *path)
{
	const char *base = strrchr(path, '\\');
	base = base ? base + 1 : path;
	struct archive_data *d = RE_get_aar_entry(v->plugin->aar, path, base, ".POL");
	if (!d)
		return;
	struct pol *pol = pol_parse(d->data, d->size);
	archive_free_data(d);
	if (!pol)
		return;
	v->info.pol_version = pol->version;
	struct RE_instance *inst = view_inst(v);
	struct model *mdl = inst ? inst->model : NULL;
	if (mdl && mdl->bone_map && mdl->nr_bones > 0) {
		v->bone_boxes = xmalloc((size_t)mdl->nr_bones * sizeof *v->bone_boxes);
		for (int b = 0; b < mdl->nr_bones; b++)
			glm_aabb_invalidate(v->bone_boxes[b]);
	}
	int lines = 0;
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		struct pol_mesh *m = pol->meshes[i];
		if (!m)
			continue;
		if (strcmp(m->name, "collision")) {
			v->info.triangles += (int)m->nr_triangles;
			/* each bone's box: the vertices it has a weight on, as the model's bone map names it */
			for (uint32_t k = 0; v->bone_boxes && k < m->nr_vertices; k++) {
				const struct pol_vertex *pv = &m->vertices[k];
				for (uint32_t w = 0; w < pv->nr_weights; w++) {
					struct bone *b = pv->weights[w].weight > 0 ? ht_get_int(mdl->bone_map, (int)pv->weights[w].bone, NULL) : NULL;
					if (b && b->index >= 0 && b->index < mdl->nr_bones) {
						glm_vec3_minv(v->bone_boxes[b->index][0], (float *)pv->pos, v->bone_boxes[b->index][0]);
						glm_vec3_maxv(v->bone_boxes[b->index][1], (float *)pv->pos, v->bone_boxes[b->index][1]);
					}
				}
			}
			continue;
		}
		if (m->nr_triangles == 0)
			continue;   /* xrealloc to 0 bytes would free the lines and call it a failure */
		v->info.walk_triangles += (int)m->nr_triangles;
		v->walk = xrealloc(v->walk, ((size_t)lines + 3 * (size_t)m->nr_triangles) * 6 * sizeof(float));
		for (uint32_t t = 0; t < m->nr_triangles; t++) {
			for (int e = 0; e < 3; e++) {
				uint32_t a = m->triangles[t].vert_index[e], b = m->triangles[t].vert_index[(e + 1) % 3];
				if (a >= m->nr_vertices || b >= m->nr_vertices)
					continue;
				float *out = &v->walk[(size_t)lines * 6];
				memcpy(out, m->vertices[a].pos, 3 * sizeof(float));
				memcpy(out + 3, m->vertices[b].pos, 3 * sizeof(float));
				lines++;
			}
		}
	}
	v->nwalk = lines;
	const char **names = NULL;
	int nnames = 0;
	for (uint32_t i = 0; i < pol->nr_materials; i++)
		count_material_textures(&names, &nnames, &pol->materials[i], 0);
	v->info.textures = nnames;
	free(names);
	pol_free(pol);
}

struct load_args {
	struct rb3d_view *v;
	const char *path;
	bool ok;
};

/* A model with bones as a skinned instance, else as a static one, as the game's code makes them. */
static void load_fn(void *arg)
{
	struct load_args *a = arg;
	struct rb3d_view *v = a->v;
	struct RE_plugin *p = v->plugin;
	forget_models(v);
	int no = RE_create_instance(p);
	struct RE_instance *inst = p->instances[no];
	v->inst = no;
	RE_instance_set_type(inst, RE_ITYPE_SKINNED);
	if (!RE_instance_load(inst, a->path) || !inst->model)
		return;
	if (inst->model->nr_bones == 0) {
		RE_release_instance(p, no);
		no = RE_create_instance(p);
		inst = p->instances[no];
		v->inst = no;
		RE_instance_set_type(inst, RE_ITYPE_STATIC);
		if (!RE_instance_load(inst, a->path) || !inst->model)
			return;
	}
	inst->draw = true;
	rest_bones(inst);
	struct model *m = inst->model;
	v->info.loaded = true;
	v->info.meshes = m->nr_meshes;
	v->info.materials = m->nr_materials;
	v->info.bones = m->nr_bones;
	for (int k = 0; k < 3; k++)
		v->info.size[k] = m->aabb[1][k] - m->aabb[0][k];
	if (m->nr_bones > 0) {
		v->heads = xmalloc((size_t)m->nr_bones * 3 * sizeof(float));
		for (int i = 0; i < m->nr_bones; i++) {
			mat4 inv;
			glm_mat4_inv(m->bones[i].inverse_bind_matrix, inv);
			memcpy(&v->heads[i * 3], inv[3], 3 * sizeof(float));
		}
	}
	read_pol_facts(v, a->path);
	a->ok = true;
}

bool rb3d_view_load(struct rb3d_view *v, const char *path, char *err, size_t n)
{
	drop_background(v);
	drop_model(v);
	v->plugin->shadow_mode = 0;
	v->play_state = RE_MOTION_STATE_LOOP;
	v->hold = true;
	v->frame = 0;
	v->shown = 0;
	struct load_args a = { v, path, false };
	/* bg_path goes only after the load, so the cache keeps the background's model for the fight
	 * that is set again on the next model. The tries are the model's only: set just around its load
	 * (a fight background loads without them). */
	load_tries = rb3d_tries_for(v->tries, path) ? v->tries : NULL;
	bool ok = rb3d_guard(load_fn, &a, err, n);
	load_tries = NULL;
	free(v->bg_path);
	v->bg_path = NULL;
	if (!ok) {
		drop_model(v);
		return false;
	}
	if (!a.ok) {
		drop_model(v);
		snprintf(err, n, "%s: no model there, or it does not read", path);
		return false;
	}
	return true;
}

struct fight_args {
	struct rb3d_view *v;
	const char *bg_path;
	float scale;
	vec3 offset;
	bool make_shadow;
	float radius;
	bool ok;
};

/* As CBattleMode sets a fight with one monster: the background a static instance that receives the
 * shadow and casts none, with no outline at level 3; the monster in the middle place, (0, 0.004, 0)
 * with its offset and 0.004 more on y, angle 0, at its scale, receiving no shadow. The engine's
 * SetInstancePos turns the game's z over. */
static void fight_fn(void *arg)
{
	struct fight_args *a = arg;
	struct rb3d_view *v = a->v;
	struct RE_plugin *p = v->plugin;
	struct RE_instance *inst = view_inst(v);
	drop_background(v);
	free(v->bg_path);
	v->bg_path = NULL;
	if (inst) {
		glm_vec3_zero(inst->pos);
		glm_vec3_one(inst->scale);
		inst->pitch = inst->yaw = inst->roll = 0;
		inst->draw_shadow = false;
		inst->make_shadow = false;
		inst->shadow_volume_bone_radius = 0.1f;
		inst->local_transform_needs_update = true;
	}
	if (!a->bg_path) {
		a->ok = true;
		return;
	}
	int no = RE_create_instance(p);
	struct RE_instance *bg = p->instances[no];
	v->bg = no;
	RE_instance_set_type(bg, RE_ITYPE_STATIC);
	if (!RE_instance_load(bg, a->bg_path) || !bg->model)
		return;
	v->bg_path = xstrdup(a->bg_path);
	bg->draw = true;
	bg->draw_shadow = true;
	bg->make_shadow = false;
	bg->draw_edge = false;
	glm_vec3_zero(bg->ambient);
	if (inst) {
		inst->pos[0] = a->offset[0];
		inst->pos[1] = 0.004f + a->offset[1] + 0.004f;
		inst->pos[2] = -a->offset[2];
		glm_vec3_broadcast(a->scale, inst->scale);
		inst->make_shadow = a->make_shadow;
		inst->shadow_volume_bone_radius = a->radius;
		inst->local_transform_needs_update = true;
	}
	a->ok = true;
}

bool rb3d_view_fight(struct rb3d_view *v, const char *bg_path, float scale, const float offset[3],
                     bool make_shadow, float shadow_bone_radius, char *err, size_t n)
{
	struct fight_args a = { v, bg_path, scale > 0 ? scale : 1, { 0, 0, 0 }, make_shadow, shadow_bone_radius, false };
	if (offset)
		glm_vec3_copy((float *)offset, a.offset);
	if (err && n)
		err[0] = 0;
	if (!rb3d_guard(fight_fn, &a, err, n) || !a.ok) {
		drop_background(v);
		if (err && n && !err[0])
			snprintf(err, n, "%s: no fight background there, or it does not read", bg_path);
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
	struct rb3d_view *v = a->v;
	struct RE_instance *inst = view_inst(v);
	if (!inst || !inst->model)
		return;
	v->has_motion_box = false;
	if (!a->name || !a->name[0]) {
		if (inst->motion)
			motion_free(inst->motion);
		inst->motion = NULL;
		rest_bones(inst);
		a->ok = true;
		return;
	}
	if (inst->type != RE_ITYPE_SKINNED)
		return;   /* a model without bones has no motion to play */
	if (!RE_instance_load_motion(inst, a->name) || !inst->motion)
		return;
	float last = inst->motion->mot->nr_frames > 0 ? (float)(inst->motion->mot->nr_frames - 1) : 0.0f;
	RE_motion_set_frame_range(inst->motion, 0, last);
	RE_motion_set_loop_frame_range(inst->motion, 0, last);
	RE_motion_set_frame(inst->motion, 0);
	RE_motion_set_state(inst->motion, RE_MOTION_STATE_LOOP);
	v->play_state = RE_MOTION_STATE_LOOP;
	v->hold = true;
	set_motion_box(v, inst, 0, (int)last);
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

void rb3d_view_play(struct rb3d_view *v, const struct rb3d_play *p)
{
	struct RE_instance *inst = view_inst(v);
	if (!inst || !inst->motion)
		return;
	RE_motion_set_frame_range(inst->motion, (float)p->begin, (float)p->end);
	RE_motion_set_loop_frame_range(inst->motion, (float)p->loop_begin, (float)p->loop_end);
	v->play_state = p->loop ? RE_MOTION_STATE_LOOP : RE_MOTION_STATE_NOLOOP;
	RE_motion_set_state(inst->motion, v->play_state);
	RE_motion_set_frame(inst->motion, (float)p->start);
	v->hold = true;
	int first, last;
	rb3d_play_frames(p, &first, &last);
	if (p->loop && p->loop_begin < first)
		first = p->loop_begin;
	set_motion_box(v, inst, first, last);
}

int rb3d_view_mot_frames(const struct rb3d_view *v)
{
	struct RE_instance *inst = view_inst(v);
	return inst && inst->motion && inst->motion->mot ? (int)inst->motion->mot->nr_frames : 0;
}

bool rb3d_view_has_bones(const struct rb3d_view *v)
{
	struct RE_instance *inst = view_inst(v);
	return inst && inst->model && inst->model->nr_bones > 0;
}

void rb3d_view_set_frame(struct rb3d_view *v, float frame)
{
	struct RE_instance *inst = view_inst(v);
	if (!inst || !inst->motion)
		return;
	RE_motion_set_state(inst->motion, v->play_state);
	RE_motion_set_frame(inst->motion, frame);
	v->hold = true;
}

float rb3d_view_frame(const struct rb3d_view *v)
{
	return v->frame;
}

bool rb3d_view_stopped(const struct rb3d_view *v)
{
	struct RE_instance *inst = view_inst(v);
	return inst && inst->motion && inst->motion->state == RE_MOTION_STATE_STOP;
}

void rb3d_draw_default(struct rb3d_draw *d, int kind)
{
	memset(d, 0, sizeof *d);
	d->outline = true;
	d->light_map = true;
	d->shadow = true;
	d->back = RB3D_BACK_BLACK;
	d->zoom = 1;
	d->fov = 30;
	switch (kind) {
	case RB3D_CHARA: d->yaw = 0; d->pitch = -8; break;           /* from the front, as a fight sees it */
	case RB3D_MAP: d->yaw = 30; d->pitch = -50; break;           /* from above, at an angle */
	case RB3D_BG: d->yaw = 0; d->pitch = -5; d->fight_camera = true; break;   /* where a fight sees it from */
	default: d->yaw = 30; d->pitch = -20; break;
	}
}

/* The model's box in the world: its own box through its place, its angles and its scale. */
static void world_box(struct RE_instance *inst, vec3 out[2])
{
	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);
	glm_aabb_transform(inst->model->aabb, inst->local_transform, out);
}

/* The box the orbit camera frames: a room's walk mesh when it has one, as a room's model can hold
 * a ground far wider than the room (380 units across for some), else the model's own box. */
static bool walk_box(struct rb3d_view *v, struct RE_instance *inst, vec3 out[2])
{
	if (v->nwalk <= 0)
		return false;
	vec3 box[2];
	glm_aabb_invalidate(box);
	for (int i = 0; i < v->nwalk * 2; i++) {
		glm_vec3_minv(box[0], &v->walk[(size_t)i * 3], box[0]);
		glm_vec3_maxv(box[1], &v->walk[(size_t)i * 3], box[1]);
	}
	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);
	glm_aabb_transform(box, inst->local_transform, out);
	return true;
}

/* A moving model's box through its motion (set_motion_box), in the world. */
static bool moving_box(struct rb3d_view *v, struct RE_instance *inst, vec3 out[2])
{
	if (!v->has_motion_box || !inst->motion)
		return false;
	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);
	glm_aabb_transform(v->motion_box, inst->local_transform, out);
	return true;
}

/* The clip planes widened so a box the frame leaves out is drawn whole: far enough for all of it,
 * near enough for what comes closer than the framed part. */
static void clip_for(const vec3 pos, vec3 box[2], float dist, float *znear, float *zfar)
{
	vec3 c;
	glm_vec3_center(box[0], box[1], c);
	float far = glm_vec3_distance((float *)pos, c) + glm_vec3_distance(box[0], box[1]) / 2;
	if (far > *zfar)
		*zfar = far;
	if (*znear > dist * 0.05f)
		*znear = dist * 0.05f;
}

/* The orbit camera around the model's box, or the game's own camera for a fight. */
static void place_camera(struct rb3d_view *v, const struct rb3d_draw *d, int w, int h)
{
	struct RE_plugin *p = v->plugin;
	if (d->fight_camera) {
		/* CBattleMode's InitCamera: (0, 0.7, -3.4) in the game's space, yaw 0, pitch -4.8, and its
		 * projection: 40 degrees across, from 0.5 to 50. */
		glm_vec3_copy((vec3){ 0.f, 0.7f, 3.4f }, p->camera.pos);
		p->camera.pitch = -4.8f;
		p->camera.yaw = 0;
		p->camera.roll = 0;
		RE_set_projection(p, (float)w, (float)h, 0.5f, 50.f, 40.f);
		return;
	}
	struct RE_instance *inst = view_inst(v), *bg = instance_at(v, v->bg);
	vec3 box[2] = { { -1, 0, -1 }, { 1, 2, 1 } };
	bool by_walk = false;
	if (inst && inst->model) {
		by_walk = walk_box(v, inst, box);
		if (!by_walk && !moving_box(v, inst, box))
			world_box(inst, box);
	}
	vec3 c;
	glm_vec3_center(box[0], box[1], c);
	float r = glm_vec3_distance(box[0], box[1]) / 2;
	if (r < 0.01f)
		r = 0.01f;
	float fov = d->fov > 1 ? d->fov : 30;
	float hfov = glm_rad(fov);
	float vfov = 2 * atanf(tanf(hfov / 2) * h / w);
	float zoom = d->zoom > 0.05f ? d->zoom : 0.05f;
	float dist = r / sinf((hfov < vfov ? hfov : vfov) / 2) * 1.05f / zoom;
	vec3 euler = { glm_rad(d->pitch), glm_rad(d->yaw), 0 };
	mat4 rot;
	glm_euler_yxz(euler, rot);
	vec3 front = { 0, 0, -1 }, right = { 1, 0, 0 }, up = { 0, 1, 0 };
	glm_mat4_mulv3(rot, front, 0.0f, front);
	glm_mat4_mulv3(rot, right, 0.0f, right);
	glm_mat4_mulv3(rot, up, 0.0f, up);
	glm_vec3_muladds(right, d->pan_x * r, c);
	glm_vec3_muladds(up, d->pan_y * r, c);
	p->camera.pitch = d->pitch;
	p->camera.yaw = d->yaw;
	p->camera.roll = 0;
	vec3 back;
	glm_vec3_scale(front, -dist, back);
	glm_vec3_add(c, back, p->camera.pos);
	float zfar = dist + 1.2f * r;
	float znear = dist - 1.2f * r;
	if (by_walk) {
		vec3 whole[2];
		world_box(inst, whole);
		clip_for(p->camera.pos, whole, dist, &znear, &zfar);
	}
	if (bg && bg->model) {
		vec3 bbox[2];
		world_box(bg, bbox);
		clip_for(p->camera.pos, bbox, dist, &znear, &zfar);
	}
	if (znear < zfar * 0.0005f)
		znear = zfar * 0.0005f;
	RE_set_projection(p, (float)w, (float)h, znear, zfar, fov);
}

static void make_texture(GLuint *t, int w, int h)
{
	if (!*t)
		glGenTextures(1, t);
	glBindTexture(GL_TEXTURE_2D, *t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static bool bind_own_fbo(GLuint tex, int w, int h)
{
	glBindFramebuffer(GL_FRAMEBUFFER, own.fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		return false;
	glViewport(0, 0, w, h);
	return true;
}

/* The see-through picture into tex_matte, from black (the frame over black) and tex_white. */
static void matte(struct rb3d_view *v, GLuint black, int w, int h)
{
	if (!bind_own_fbo(v->tex_matte, w, h))
		ERROR("Incomplete framebuffer");
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glUseProgram(own.matte);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, v->tex_white);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, black);
	glBindVertexArray(own.vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glUseProgram(0);
}

static void place_point(mat4 m, const float *in, float *out)
{
	vec3 p;
	glm_mat4_mulv3(m, (float *)in, 1.0f, p);
	memcpy(out, p, sizeof p);
}

/* The skeleton and the walk mesh as lines over the picture tex, in the frame's camera. */
static void overlays(struct rb3d_view *v, const struct rb3d_draw *d, GLuint tex, int w, int h)
{
	struct RE_instance *inst = view_inst(v);
	if (!own.ok || !inst || !inst->model)
		return;
	bool skel = d->skeleton && v->heads && inst->model->nr_bones > 0;
	bool walk = d->walk && v->nwalk > 0;
	if (!skel && !walk)
		return;
	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);
	int nb = inst->model->nr_bones;
	size_t cap = (size_t)(skel ? nb : 0) * 6 + (size_t)(walk ? v->nwalk : 0) * 6;
	float *pts = xmalloc((cap ? cap : 6) * sizeof(float));
	int nskel = 0, nwalk = 0;
	if (skel) {
		/* Each bone's head now: its skin matrix (row-major, from the frame or the rest pose's
		 * identity) applied to its head in the rest pose. */
		float *now = xmalloc((size_t)nb * 3 * sizeof(float));
		for (int i = 0; i < nb; i++) {
			float h3[3];
			const float *rest = &v->heads[i * 3];
			const float (*bt)[4] = (const float (*)[4])inst->bone_transforms[i];
			for (int r = 0; r < 3; r++)
				h3[r] = bt[r][0] * rest[0] + bt[r][1] * rest[1] + bt[r][2] * rest[2] + bt[r][3];
			place_point(inst->local_transform, h3, &now[i * 3]);
		}
		for (int i = 0; i < nb; i++) {
			int parent = inst->model->bones[i].parent;
			if (parent < 0)
				continue;
			memcpy(&pts[(size_t)nskel * 6], &now[parent * 3], 3 * sizeof(float));
			memcpy(&pts[(size_t)nskel * 6 + 3], &now[i * 3], 3 * sizeof(float));
			nskel++;
		}
		free(now);
	}
	if (walk) {
		for (int i = 0; i < v->nwalk; i++) {
			float *out = &pts[(size_t)(nskel + i) * 6];
			place_point(inst->local_transform, &v->walk[(size_t)i * 6], out);
			place_point(inst->local_transform, &v->walk[(size_t)i * 6 + 3], out + 3);
		}
		nwalk = v->nwalk;
	}
	mat4 view, proj, mvp;
	RE_calc_view_matrix(&v->plugin->camera, NULL, view);
	glm_mat4_copy(v->plugin->proj_transform, proj);
	proj[1][1] *= -1;   /* the picture's rows run from the top, as the renderer draws them */
	glm_mat4_mul(proj, view, mvp);
	if (!bind_own_fbo(tex, w, h))
		ERROR("Incomplete framebuffer");
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(own.line);
	glUniformMatrix4fv(own.line_mvp, 1, GL_FALSE, mvp[0]);
	glBindVertexArray(own.line_vao);
	glBindBuffer(GL_ARRAY_BUFFER, own.line_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)(nskel + nwalk) * 6 * sizeof(float)), pts, GL_STREAM_DRAW);
	if (nwalk) {
		glUniform4f(own.line_color, 0.35f, 0.85f, 1.0f, 0.75f);
		glDrawArrays(GL_LINES, nskel * 2, nwalk * 2);
	}
	if (nskel) {
		glUniform4f(own.line_color, 1.0f, 0.82f, 0.25f, 1.0f);
		glDrawArrays(GL_LINES, 0, nskel * 2);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	free(pts);
}

/* A boned model in its rest pose casts its shadow from the sphere the engine works out while a
 * motion plays (its bones' box, at its scale, and the bone radius). */
static void rest_sphere(struct rb3d_view *v, struct RE_instance *inst)
{
	if (!inst || !inst->model || inst->motion || inst->type != RE_ITYPE_SKINNED || !v->heads)
		return;
	vec3 aabb[2];
	glm_aabb_invalidate(aabb);
	for (int i = 0; i < inst->model->nr_bones; i++) {
		glm_vec3_minv(aabb[0], &v->heads[i * 3], aabb[0]);
		glm_vec3_maxv(aabb[1], &v->heads[i * 3], aabb[1]);
	}
	vec3 center;
	glm_aabb_center(aabb, center);
	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);
	glm_mat4_mulv3(inst->local_transform, center, 1.0f, inst->bounding_sphere);
	inst->bounding_sphere[3] = glm_aabb_radius(aabb) * inst->scale[0] + inst->shadow_volume_bone_radius;
}

struct draw_args {
	struct rb3d_view *v;
	uint64_t ms;
	const struct rb3d_draw *d;
};

static void draw_fn(void *arg)
{
	struct draw_args *a = arg;
	struct rb3d_view *v = a->v;
	const struct rb3d_draw *d = a->d;
	struct RE_plugin *p = v->plugin;
	struct sact_sprite *sp = &sprites[v->sprite];
	int w = sp->texture.w, h = sp->texture.h;
	p->draw_options[RE_DRAW_OPTION_EDGE] = d->outline ? 1 : 0;
	p->light_map_mode = d->light_map ? 1 : 0;
	p->shadow_mode = d->shadow && v->bg >= 0 ? 2 : 0;

	/* The clock moves the motion by the time since the last draw, as the engine's RE_update_model
	 * does; a draw whose clock stands still shows the frame as it is. */
	uint32_t now = (uint32_t)a->ms;
	int delta = !v->clock_set || v->hold || now < v->last_ms ? 0 : (int)(now - v->last_ms);
	struct RE_instance *inst = view_inst(v);
	struct motion *m = inst ? inst->motion : NULL;
	int saved = -1;
	float saved_end = 0;
	if (m && !delta && m->state != RE_MOTION_STATE_STOP) {
		/* Held for this update: once, with its end past the frame, so the engine's update neither
		 * wraps the frame round the loop nor stops it at the end (a loop may run past it). */
		saved = m->state;
		saved_end = m->frame_end;
		m->state = RE_MOTION_STATE_NOLOOP;
		if (m->frame_end < m->current_frame)
			m->frame_end = m->current_frame;
	}
	view_ticks = now;
	p->renderer->last_frame_timestamp = now - (uint32_t)delta;
	RE_update_model(p);
	if (m && saved >= 0) {
		m->state = saved;
		m->frame_end = saved_end;
	}
	v->hold = false;
	v->clock_set = true;
	v->last_ms = now;
	v->frame = m ? m->current_frame : 0;
	if (v->bg >= 0)
		rest_sphere(v, inst);

	place_camera(v, d, w, h);
	GLuint black = sp->texture.handle;
	if (d->back == RB3D_BACK_ALPHA && own.ok) {
		if (v->tw != w || v->th != h) {
			make_texture(&v->tex_white, w, h);
			make_texture(&v->tex_matte, w, h);
			v->tw = w;
			v->th = h;
		}
		sp->texture.handle = v->tex_white;
		white_back = true;
		RE_render(sp);
		white_back = false;
		sp->texture.handle = black;
		RE_render(sp);
		matte(v, black, w, h);
		v->shown = v->tex_matte;
	} else {
		white_back = d->back == RB3D_BACK_WHITE;
		RE_render(sp);
		white_back = false;
		v->shown = black;
	}
	overlays(v, d, v->shown, w, h);
}

bool rb3d_view_draw(struct rb3d_view *v, uint64_t ms, const struct rb3d_draw *d, char *err, size_t n)
{
	GLint fb = 0, vp[4], vao = 0, prog = 0, tex = 0, active = 0, buf = 0;
	GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean depth_mask = GL_TRUE;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
	glGetIntegerv(GL_VIEWPORT, vp);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buf);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
	glDisable(GL_SCISSOR_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* A background picture makes the renderer call the shim between its clear and the models (see
	 * gfx_copy_stretch_blend_amap_alpha); it is taken away after the draw whatever happens, so the
	 * plugin never frees it. */
	struct RE_back_cg *bcg = &v->plugin->back_cg[0];
	struct sact_sprite *sp = &sprites[v->sprite];
	GLuint black = sp->texture.handle;
	bool white = d->back == RB3D_BACK_WHITE || (d->back == RB3D_BACK_ALPHA && own.ok);
	if (white) {
		bcg->show = true;
		bcg->texture.handle = 1;
		bcg->texture.w = bcg->texture.h = 1;
	}
	struct draw_args a = { v, ms, d };
	bool ok = rb3d_guard(draw_fn, &a, err, n);
	white_back = false;
	bcg->show = false;
	bcg->texture.handle = 0;
	bcg->texture.w = bcg->texture.h = 0;
	sp->texture.handle = black;

	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	glBindVertexArray((GLuint)vao);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buf);
	glUseProgram((GLuint)prog);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
	glActiveTexture((GLenum)active);
	glDepthMask(depth_mask);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	if (blend)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
	if (scissor)
		glEnable(GL_SCISSOR_TEST);
	if (!ok) {
		drop_background(v);
		drop_model(v);
		v->shown = 0;
	}
	return ok;
}

unsigned rb3d_view_texture(const struct rb3d_view *v, int *w, int *h)
{
	const struct texture *t = &sprites[v->sprite].texture;
	if (w)
		*w = t->w;
	if (h)
		*h = t->h;
	return v->shown ? v->shown : t->handle;
}

uint8_t *rb3d_view_rgba(struct rb3d_view *v, int *w, int *h)
{
	const struct texture *t = &sprites[v->sprite].texture;
	int tw = t->w, th = t->h;
	GLuint tex = v->shown ? v->shown : t->handle;
	if (!own.ok || !tex || tw < 1 || th < 1)
		return NULL;
	GLint fb = 0, vp[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
	glGetIntegerv(GL_VIEWPORT, vp);
	uint8_t *px = NULL;
	if (bind_own_fbo(tex, tw, th)) {
		px = xmalloc((size_t)tw * th * 4);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, px);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	if (px) {
		*w = tw;
		*h = th;
	}
	return px;
}

void rb3d_view_info(const struct rb3d_view *v, struct rb3d_info *out)
{
	*out = v->info;
}
