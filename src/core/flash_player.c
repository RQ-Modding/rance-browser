/* The Flash player: a Flash file played as RQ MOD's engine plays it.
 *
 * After xsystem4's src/parts/flash.c (Copyright (C) 2023 kichikuou) and src/parts/render.c and
 * shaders/parts.*.glsl (Copyright (C) 2019 Nunuhara Cabbage), both GPL-2.0-or-later, with RQ MOD's
 * fixes: the timeline's tags walked frame by frame into a display list by depth; sprites of one
 * object; a frame's actions before its ShowFrame (stop, play, gotoFrame: a jump forward walks the
 * frames between, a jump back plays the tags again from the start with the sounds before its frame
 * quiet); each StartSound at its frame, through the mixer. A frame is drawn with OpenGL as the
 * engine draws it: each shape is the whole of its bitmap over the shape's bounds, turned by the
 * matrices; its colour is (colour + add) * multiply and its alpha the bitmap's times the multiply
 * term; the blend modes are the engine's (normal, add, multiply, screen; any other as normal).
 *
 * A file is opened only when swf_check.c says the game plays it, since the reader and the engine
 * close the game on what they do not handle; what the engine would refuse is passed over here. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include "system4.h"

#include "core.h"
#include "swf.h"
#include "swf_check.h"

#define JUMPS_MAX 64     /* gotoFrame actions followed in one seek: a loop of jumps stops there */
#define FBO_MAX 4096

enum { BLEND_MULTIPLY = 3, BLEND_SCREEN = 4, BLEND_ADD = 8 };

struct fp_obj {
	int depth, id;
	float m[6];          /* x' = m0 x + m2 y + m4, y' = m1 x + m3 y + m5 */
	struct swf_cxform_with_alpha cx;
	int blend;
};

struct fp_char {
	int id;
	struct swf_tag *tag;                        /* the last definition of this id */
	struct swf_tag_define_bits_lossless *bmp;   /* the first bitmap of this id: the one drawn */
	unsigned tex;                               /* its texture, made when it is first drawn */
	bool sprite_read, has_obj;                  /* a sprite's one object, from its first definition */
	struct fp_obj obj;
};

struct fplay {
	struct swf *swf;
	int w, h, frames;
	double rate;
	struct fp_char *chars;
	int nchars, cchars;
	struct fp_obj *list;   /* by depth */
	int nlist, clist;
	struct swf_tag *tag;   /* the next tag of the timeline */
	int frame;             /* frames shown so far: the frame shown, from 1 */
	int quiet_before;      /* after a jump back, the sounds of the frames before this one stay quiet */
	bool ended;            /* a stop action, or the file's end */
	bool sound;
	float volume;
	int started, jumps;
	/* drawing */
	unsigned fbo, ftex;
	int fw, fh;
	int drawn_frame, drawn_back;
	unsigned drawn_gen, gen;
};

/* ---- the characters and the display list ---- */

static struct fp_char *find(struct fplay *p, int id)
{
	for (int i = 0; i < p->nchars; i++)
		if (p->chars[i].id == id)
			return &p->chars[i];
	return NULL;
}

static struct fp_char *define(struct fplay *p, int id, struct swf_tag *t)
{
	struct fp_char *c = find(p, id);
	if (!c) {
		if (p->nchars == p->cchars) {
			p->cchars = p->cchars ? p->cchars * 2 : 64;
			p->chars = xrealloc(p->chars, (size_t)p->cchars * sizeof *p->chars);
		}
		c = &p->chars[p->nchars++];
		memset(c, 0, sizeof *c);
		c->id = id;
	}
	c->tag = t;
	return c;
}

/* The engine's update_object: a new character resets the matrix and the colours. */
static void obj_set(struct fp_obj *o, const struct swf_tag_place_object *t)
{
	if (t->flags & PLACE_FLAG_HAS_CHARACTER) {
		o->id = t->character_id;
		o->m[0] = 1; o->m[1] = 0; o->m[2] = 0; o->m[3] = 1; o->m[4] = 0; o->m[5] = 0;
		memset(&o->cx, 0, sizeof o->cx);
		for (int i = 0; i < 4; i++)
			o->cx.mult_terms[i] = 256;
	}
	if (t->flags & PLACE_FLAG_HAS_MATRIX) {
		o->m[0] = fixed32_to_float(t->matrix.scale_x);
		o->m[1] = fixed32_to_float(t->matrix.rotate_skew_0);
		o->m[2] = fixed32_to_float(t->matrix.rotate_skew_1);
		o->m[3] = fixed32_to_float(t->matrix.scale_y);
		o->m[4] = twips_to_float(t->matrix.translate_x);
		o->m[5] = twips_to_float(t->matrix.translate_y);
	}
	if (t->flags & PLACE_FLAG_HAS_COLOR_TRANSFORM)
		o->cx = t->color_transform;
	if (t->flags & PLACE_FLAG_HAS_BLEND_MODE)
		o->blend = t->blend_mode;
}

static void place(struct fplay *p, const struct swf_tag_place_object *t)
{
	int i = 0;
	while (i < p->nlist && p->list[i].depth < t->depth)
		i++;
	if (i < p->nlist && p->list[i].depth == t->depth) {
		obj_set(&p->list[i], t);
		return;
	}
	/* The engine closes on a move with nothing at that depth. */
	if (!(t->flags & PLACE_FLAG_HAS_CHARACTER))
		return;
	if (p->nlist == p->clist) {
		p->clist = p->clist ? p->clist * 2 : 32;
		p->list = xrealloc(p->list, (size_t)p->clist * sizeof *p->list);
	}
	memmove(&p->list[i + 1], &p->list[i], (size_t)(p->nlist - i) * sizeof *p->list);
	p->nlist++;
	memset(&p->list[i], 0, sizeof p->list[i]);
	p->list[i].depth = t->depth;
	obj_set(&p->list[i], t);
}

static void remove_at(struct fplay *p, int depth)
{
	for (int i = 0; i < p->nlist; i++) {
		if (p->list[i].depth == depth) {
			memmove(&p->list[i], &p->list[i + 1], (size_t)(p->nlist - i - 1) * sizeof *p->list);
			p->nlist--;
			return;
		}
	}
}

/* The engine's define_sprite: its first PlaceObject2, read once. */
static void define_sprite(struct fplay *p, struct swf_tag_define_sprite *t)
{
	struct fp_char *c = define(p, t->sprite_id, &t->t);
	if (c->sprite_read)
		return;
	c->sprite_read = true;
	for (struct swf_tag *s = t->tags; s && s->type != TAG_END; s = s->next) {
		if (s->type != TAG_PLACE_OBJECT2)
			continue;
		const struct swf_tag_place_object *po = (const struct swf_tag_place_object *)s;
		if (!(po->flags & PLACE_FLAG_HAS_CHARACTER))
			continue;
		memset(&c->obj, 0, sizeof c->obj);
		obj_set(&c->obj, po);
		c->has_obj = true;
		break;
	}
}

static void start_sound(struct fplay *p, const struct swf_tag_start_sound *t)
{
	/* The engine closes on a StartSound with flags (swf_check says so). */
	if (t->flags)
		return;
	if (p->frame + 1 < p->quiet_before)
		return;
	struct fp_char *c = find(p, t->sound_id);
	if (!c || !c->tag || c->tag->type != TAG_DEFINE_SOUND)
		return;
	const struct swf_tag_define_sound *s = (const struct swf_tag_define_sound *)c->tag;
	if (s->format != SWF_SOUND_UNCOMPRESSED_LE)
		return;
	p->started++;
	if (p->sound)
		mixer_pcm(s->data, s->data_len, s->spec.rate, s->spec.channels, s->spec.sample_size, p->volume);
}

/* A frame's actions: the frame of a gotoFrame (from 0, as written), or -1. */
static int do_action(struct fplay *p, const struct swf_tag_do_action *t)
{
	int go = -1;
	for (struct swf_action *a = t->actions; a && a->code != ACTION_END; a = a->next) {
		if (a->code == ACTION_PLAY)
			p->ended = false;
		else if (a->code == ACTION_STOP)
			p->ended = true;
		else if (a->code == ACTION_GOTO_FRAME)
			go = ((struct swf_action_goto_frame *)a)->frame;
	}
	return go;
}

static void rewind_to(struct fplay *p, int frame)
{
	p->frame = 0;
	p->tag = p->swf->tags;
	p->quiet_before = frame;
	p->nlist = 0;
}

/* The engine's parts_flash_seek. */
static bool seek(struct fplay *p, int frame, int depth)
{
	if (p->frame == frame)
		return false;
	if (p->frame > frame)
		rewind_to(p, frame);
	p->gen++;
	struct swf_tag *t;
	for (t = p->tag; t && p->frame < frame; t = t->next) {
		switch (t->type) {
		case TAG_END:
			p->ended = true;
			break;
		case TAG_SHOW_FRAME:
			p->frame++;
			break;
		case TAG_DEFINE_BITS_LOSSLESS:
		case TAG_DEFINE_BITS_LOSSLESS2: {
			struct swf_tag_define_bits_lossless *b = (struct swf_tag_define_bits_lossless *)t;
			struct fp_char *c = define(p, b->character_id, t);
			if (!c->bmp)
				c->bmp = b;
			break;
		}
		case TAG_DEFINE_SHAPE:
			define(p, ((struct swf_tag_define_shape *)t)->shape_id, t);
			break;
		case TAG_DEFINE_SOUND:
			define(p, ((struct swf_tag_define_sound *)t)->sound_id, t);
			break;
		case TAG_DEFINE_SPRITE:
			define_sprite(p, (struct swf_tag_define_sprite *)t);
			break;
		case TAG_DO_ACTION: {
			int go = do_action(p, (struct swf_tag_do_action *)t);
			if (go >= 0 && depth < JUMPS_MAX) {
				/* gotoFrame counts from 0, the seek from 1. Forward goes on after the
				 * action; back plays from the start. */
				int target = go + 1;
				p->jumps++;
				if (target <= p->frame)
					rewind_to(p, target);
				else
					p->tag = t->next;
				return seek(p, target, depth + 1);
			}
			break;
		}
		case TAG_PLACE_OBJECT2:
		case TAG_PLACE_OBJECT3:
			place(p, (struct swf_tag_place_object *)t);
			break;
		case TAG_REMOVE_OBJECT2:
			remove_at(p, ((struct swf_tag_remove_object2 *)t)->depth);
			break;
		case TAG_START_SOUND:
			start_sound(p, (struct swf_tag_start_sound *)t);
			break;
		default:
			break;
		}
	}
	p->tag = t;
	return true;
}

/* ---- opening ---- */

struct fplay *fplay_open(const uint8_t *data, size_t size, struct swfc *c, int *code)
{
	int r = swfc_check(data, size, c);
	if (code)
		*code = r;
	if (r != 0 || !c->fws)
		return NULL;
	/* swf_load reads an FWS buffer of its own size and keeps nothing of it. */
	struct swf *swf = swf_load(c->fws, c->fws_size);
	if (!swf)
		return NULL;
	struct fplay *p = xcalloc(1, sizeof *p);
	p->swf = swf;
	p->w = swf->frame_size.x_max / 20;
	p->h = swf->frame_size.y_max / 20;
	if (p->w < 1)
		p->w = 1;
	if (p->h < 1)
		p->h = 1;
	p->frames = swf->frame_count > 0 ? swf->frame_count : 1;
	p->rate = swf->frame_rate / 256.0;
	p->volume = 1;
	p->tag = swf->tags;
	p->drawn_frame = -1;
	return p;
}

static void gl_free_target(struct fplay *p);

void fplay_close(struct fplay *p)
{
	if (!p)
		return;
	for (int i = 0; i < p->nchars; i++)
		tex_free(p->chars[i].tex);
	gl_free_target(p);
	free(p->chars);
	free(p->list);
	swf_free(p->swf);
	free(p);
}

const struct swf *fplay_swf(const struct fplay *p) { return p ? p->swf : NULL; }
int fplay_width(const struct fplay *p) { return p ? p->w : 0; }
int fplay_height(const struct fplay *p) { return p ? p->h : 0; }
int fplay_frames(const struct fplay *p) { return p ? p->frames : 0; }
double fplay_rate(const struct fplay *p) { return p ? p->rate : 0; }
int fplay_frame(const struct fplay *p) { return p ? p->frame : 0; }
bool fplay_ended(const struct fplay *p) { return p && p->ended; }
int fplay_objects(const struct fplay *p) { return p ? p->nlist : 0; }
int fplay_started(const struct fplay *p) { return p ? p->started : 0; }
int fplay_jumps(const struct fplay *p) { return p ? p->jumps : 0; }

bool fplay_seek(struct fplay *p, int frame)
{
	if (!p || frame < 1)
		return false;
	return seek(p, frame, 0);
}

int fplay_goto(struct fplay *p, int frame)
{
	if (!p)
		return 0;
	p->ended = false;
	fplay_seek(p, frame);
	return p->frame;
}

int fplay_next(struct fplay *p)
{
	if (!p)
		return 0;
	seek(p, p->frame + 1, 0);
	return p->frame;
}

void fplay_sound(struct fplay *p, bool on, float volume)
{
	if (!p)
		return;
	p->sound = on;
	p->volume = volume < 0 ? 0 : volume;
}

/* ---- OpenGL past 1.1, through SDL ---- */

static struct {
	PFNGLGENFRAMEBUFFERSPROC gen_fb;
	PFNGLDELETEFRAMEBUFFERSPROC delete_fb;
	PFNGLBINDFRAMEBUFFERPROC bind_fb;
	PFNGLFRAMEBUFFERTEXTURE2DPROC fb_tex;
	PFNGLCHECKFRAMEBUFFERSTATUSPROC fb_status;
	PFNGLCREATESHADERPROC create_shader;
	PFNGLSHADERSOURCEPROC shader_source;
	PFNGLCOMPILESHADERPROC compile_shader;
	PFNGLGETSHADERIVPROC get_shader;
	PFNGLGETSHADERINFOLOGPROC shader_log;
	PFNGLDELETESHADERPROC delete_shader;
	PFNGLCREATEPROGRAMPROC create_program;
	PFNGLATTACHSHADERPROC attach;
	PFNGLBINDATTRIBLOCATIONPROC bind_attrib;
	PFNGLBINDFRAGDATALOCATIONPROC bind_frag;
	PFNGLLINKPROGRAMPROC link;
	PFNGLGETPROGRAMIVPROC get_program;
	PFNGLGETPROGRAMINFOLOGPROC program_log;
	PFNGLUSEPROGRAMPROC use;
	PFNGLGETUNIFORMLOCATIONPROC uniform_at;
	PFNGLUNIFORM1IPROC u1i;
	PFNGLUNIFORM1FPROC u1f;
	PFNGLUNIFORM2FPROC u2f;
	PFNGLUNIFORM3FPROC u3f;
	PFNGLUNIFORMMATRIX3FVPROC um3;
	PFNGLACTIVETEXTUREPROC active_tex;
	PFNGLGENBUFFERSPROC gen_buffers;
	PFNGLBINDBUFFERPROC bind_buffer;
	PFNGLBUFFERDATAPROC buffer_data;
	PFNGLGENVERTEXARRAYSPROC gen_vao;
	PFNGLBINDVERTEXARRAYPROC bind_vao;
	PFNGLENABLEVERTEXATTRIBARRAYPROC enable_attrib;
	PFNGLVERTEXATTRIBPOINTERPROC attrib_ptr;
	PFNGLBLENDFUNCSEPARATEPROC blend_sep;
	PFNGLBLENDEQUATIONPROC blend_eq;
} gl;

static struct {
	bool tried, ok;
	GLuint shape, back, vao, vbo;
	GLint s_world, s_stage, s_tex, s_rate, s_add, s_mult, s_premul;
	GLint b_world, b_stage, b_cell, b_dark, b_light;
} R;

static bool load_gl(void)
{
#define L(field, type, name) if (!(gl.field = (type)SDL_GL_GetProcAddress(name))) return false
	L(gen_fb, PFNGLGENFRAMEBUFFERSPROC, "glGenFramebuffers");
	L(delete_fb, PFNGLDELETEFRAMEBUFFERSPROC, "glDeleteFramebuffers");
	L(bind_fb, PFNGLBINDFRAMEBUFFERPROC, "glBindFramebuffer");
	L(fb_tex, PFNGLFRAMEBUFFERTEXTURE2DPROC, "glFramebufferTexture2D");
	L(fb_status, PFNGLCHECKFRAMEBUFFERSTATUSPROC, "glCheckFramebufferStatus");
	L(create_shader, PFNGLCREATESHADERPROC, "glCreateShader");
	L(shader_source, PFNGLSHADERSOURCEPROC, "glShaderSource");
	L(compile_shader, PFNGLCOMPILESHADERPROC, "glCompileShader");
	L(get_shader, PFNGLGETSHADERIVPROC, "glGetShaderiv");
	L(shader_log, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog");
	L(delete_shader, PFNGLDELETESHADERPROC, "glDeleteShader");
	L(create_program, PFNGLCREATEPROGRAMPROC, "glCreateProgram");
	L(attach, PFNGLATTACHSHADERPROC, "glAttachShader");
	L(bind_attrib, PFNGLBINDATTRIBLOCATIONPROC, "glBindAttribLocation");
	L(bind_frag, PFNGLBINDFRAGDATALOCATIONPROC, "glBindFragDataLocation");
	L(link, PFNGLLINKPROGRAMPROC, "glLinkProgram");
	L(get_program, PFNGLGETPROGRAMIVPROC, "glGetProgramiv");
	L(program_log, PFNGLGETPROGRAMINFOLOGPROC, "glGetProgramInfoLog");
	L(use, PFNGLUSEPROGRAMPROC, "glUseProgram");
	L(uniform_at, PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation");
	L(u1i, PFNGLUNIFORM1IPROC, "glUniform1i");
	L(u1f, PFNGLUNIFORM1FPROC, "glUniform1f");
	L(u2f, PFNGLUNIFORM2FPROC, "glUniform2f");
	L(u3f, PFNGLUNIFORM3FPROC, "glUniform3f");
	L(um3, PFNGLUNIFORMMATRIX3FVPROC, "glUniformMatrix3fv");
	L(active_tex, PFNGLACTIVETEXTUREPROC, "glActiveTexture");
	L(gen_buffers, PFNGLGENBUFFERSPROC, "glGenBuffers");
	L(bind_buffer, PFNGLBINDBUFFERPROC, "glBindBuffer");
	L(buffer_data, PFNGLBUFFERDATAPROC, "glBufferData");
	L(gen_vao, PFNGLGENVERTEXARRAYSPROC, "glGenVertexArrays");
	L(bind_vao, PFNGLBINDVERTEXARRAYPROC, "glBindVertexArray");
	L(enable_attrib, PFNGLENABLEVERTEXATTRIBARRAYPROC, "glEnableVertexAttribArray");
	L(attrib_ptr, PFNGLVERTEXATTRIBPOINTERPROC, "glVertexAttribPointer");
	L(blend_sep, PFNGLBLENDFUNCSEPARATEPROC, "glBlendFuncSeparate");
	L(blend_eq, PFNGLBLENDEQUATIONPROC, "glBlendEquation");
#undef L
	return true;
}

/* The unit square, turned into the stage by `world` (a shape's bounds and matrices); the stage's
 * top row goes to the texture's first, as the engine's do. */
static const char VERTEX[] =
	"#version 130\n"
	"uniform mat3 world;\n"
	"uniform vec2 stage;\n"
	"in vec2 pos;\n"
	"out vec2 tex_coord;\n"
	"void main() {\n"
	"    vec3 p = world * vec3(pos, 1.0);\n"
	"    tex_coord = pos;\n"
	"    gl_Position = vec4(p.xy / stage * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

/* shaders/parts.f.glsl as the engine runs it for a Flash shape (draw filter 0, no clipper); with
 * premul, the colour multiplied by the alpha for a target that starts clear. */
static const char SHAPE[] =
	"#version 130\n"
	"uniform sampler2D tex;\n"
	"uniform float blend_rate;\n"
	"uniform vec3 add_color;\n"
	"uniform vec3 multiply_color;\n"
	"uniform int premul;\n"
	"in vec2 tex_coord;\n"
	"out vec4 frag_color;\n"
	"void main() {\n"
	"    vec4 tex_color = texture(tex, tex_coord);\n"
	"    vec3 mod_color = (tex_color.rgb + add_color) * multiply_color;\n"
	"    float alpha = tex_color.a * blend_rate;\n"
	"    frag_color = premul != 0 ? vec4(mod_color * alpha, alpha) : vec4(mod_color, alpha);\n"
	"}\n";

/* A checkerboard under the stage, in the framebuffer's pixels. */
static const char BACK[] =
	"#version 130\n"
	"uniform float cell;\n"
	"uniform vec3 dark;\n"
	"uniform vec3 light;\n"
	"in vec2 tex_coord;\n"
	"out vec4 frag_color;\n"
	"void main() {\n"
	"    vec2 q = floor(gl_FragCoord.xy / cell);\n"
	"    frag_color = vec4(mix(dark, light, mod(q.x + q.y, 2.0)), 1.0);\n"
	"}\n";

static GLuint compile(GLenum type, const char *src)
{
	GLuint s = gl.create_shader(type);
	gl.shader_source(s, 1, &src, NULL);
	gl.compile_shader(s);
	GLint ok = 0;
	gl.get_shader(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = "";
		gl.shader_log(s, sizeof log, NULL, log);
		fprintf(stderr, "the Flash player's shader did not compile: %s\n", log);
		gl.delete_shader(s);
		return 0;
	}
	return s;
}

static GLuint program(const char *frag)
{
	GLuint v = compile(GL_VERTEX_SHADER, VERTEX), f = compile(GL_FRAGMENT_SHADER, frag);
	if (!v || !f)
		return 0;
	GLuint p = gl.create_program();
	gl.attach(p, v);
	gl.attach(p, f);
	gl.bind_attrib(p, 0, "pos");
	gl.bind_frag(p, 0, "frag_color");
	gl.link(p);
	gl.delete_shader(v);
	gl.delete_shader(f);
	GLint ok = 0;
	gl.get_program(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024] = "";
		gl.program_log(p, sizeof log, NULL, log);
		fprintf(stderr, "the Flash player's shader did not link: %s\n", log);
		return 0;
	}
	return p;
}

static bool gl_init(void)
{
	if (R.tried)
		return R.ok;
	R.tried = true;
	if (!SDL_GL_GetCurrentContext() || !load_gl())
		return false;
	if (!(R.shape = program(SHAPE)) || !(R.back = program(BACK)))
		return false;
	R.s_world = gl.uniform_at(R.shape, "world");
	R.s_stage = gl.uniform_at(R.shape, "stage");
	R.s_tex = gl.uniform_at(R.shape, "tex");
	R.s_rate = gl.uniform_at(R.shape, "blend_rate");
	R.s_add = gl.uniform_at(R.shape, "add_color");
	R.s_mult = gl.uniform_at(R.shape, "multiply_color");
	R.s_premul = gl.uniform_at(R.shape, "premul");
	R.b_world = gl.uniform_at(R.back, "world");
	R.b_stage = gl.uniform_at(R.back, "stage");
	R.b_cell = gl.uniform_at(R.back, "cell");
	R.b_dark = gl.uniform_at(R.back, "dark");
	R.b_light = gl.uniform_at(R.back, "light");
	static const float quad[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
	GLint prev_vao = 0, prev_buf = 0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buf);
	gl.gen_vao(1, &R.vao);
	gl.bind_vao(R.vao);
	gl.gen_buffers(1, &R.vbo);
	gl.bind_buffer(GL_ARRAY_BUFFER, R.vbo);
	gl.buffer_data(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
	gl.enable_attrib(0);
	gl.attrib_ptr(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	gl.bind_vao((GLuint)prev_vao);
	gl.bind_buffer(GL_ARRAY_BUFFER, (GLuint)prev_buf);
	R.ok = true;
	return true;
}

static void gl_free_target(struct fplay *p)
{
	if (p->fbo && gl.delete_fb)
		gl.delete_fb(1, &p->fbo);
	p->fbo = 0;
	tex_free(p->ftex);
	p->ftex = 0;
	p->fw = p->fh = 0;
}

static bool target(struct fplay *p, int w, int h)
{
	if (p->fbo && p->fw == w && p->fh == h)
		return true;
	gl_free_target(p);
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	p->ftex = tex;
	GLint prev = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
	gl.gen_fb(1, &p->fbo);
	gl.bind_fb(GL_FRAMEBUFFER, p->fbo);
	gl.fb_tex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	bool ok = gl.fb_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	gl.bind_fb(GL_FRAMEBUFFER, (GLuint)prev);
	if (!ok) {
		gl_free_target(p);
		return false;
	}
	p->fw = w;
	p->fh = h;
	return true;
}

/* ---- drawing a frame ---- */

/* The engine's blend functions for a Flash object (render.c), the alpha kept at 1 over a
 * background; over a clear target, the same sums on colours multiplied by their alpha. */
static void blend_for(int mode, bool clear)
{
	switch (mode) {
	case BLEND_MULTIPLY:
		gl.blend_sep(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
		break;
	case BLEND_SCREEN:
		gl.blend_sep(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
		break;
	case BLEND_ADD:
		if (clear)
			gl.blend_sep(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
		else
			gl.blend_sep(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	default:
		if (clear)
			gl.blend_sep(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		else
			gl.blend_sep(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		break;
	}
}

/* parts_render_flash_shape: the whole bitmap over the shape's bounds, then the matrix m. */
static void draw_shape(struct fplay *p, const struct swf_tag_define_shape *s, const float *m,
                       const struct swf_cxform_with_alpha *cx, int blend, bool clear)
{
	struct fp_char *b = find(p, s->fill_style.bitmap_id);
	if (!b || !b->bmp)
		return;
	if (!b->tex)
		b->tex = tex_from_rgba(b->bmp->data, b->bmp->width, b->bmp->height);
	if (!b->tex)
		return;
	float x0 = twips_to_float(s->bounds.x_min), y0 = twips_to_float(s->bounds.y_min);
	float bw = twips_to_float(s->bounds.x_max - s->bounds.x_min);
	float bh = twips_to_float(s->bounds.y_max - s->bounds.y_min);
	const float world[9] = {
		m[0] * bw, m[1] * bw, 0,
		m[2] * bh, m[3] * bh, 0,
		m[0] * x0 + m[2] * y0 + m[4], m[1] * x0 + m[3] * y0 + m[5], 1,
	};
	gl.um3(R.s_world, 1, GL_FALSE, world);
	gl.u1f(R.s_rate, fixed16_to_float(cx->mult_terms[3]));
	gl.u3f(R.s_add, cx->add_terms[0] / 255.0f, cx->add_terms[1] / 255.0f, cx->add_terms[2] / 255.0f);
	gl.u3f(R.s_mult, fixed16_to_float(cx->mult_terms[0]), fixed16_to_float(cx->mult_terms[1]),
	       fixed16_to_float(cx->mult_terms[2]));
	/* Multiply and screen take the colour alone, as the engine's do. */
	gl.u1i(R.s_premul, clear && blend != BLEND_MULTIPLY && blend != BLEND_SCREEN);
	blend_for(blend, clear);
	glBindTexture(GL_TEXTURE_2D, b->tex);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void draw_list(struct fplay *p, bool clear)
{
	for (int i = 0; i < p->nlist; i++) {
		const struct fp_obj *o = &p->list[i];
		struct fp_char *c = find(p, o->id);
		if (!c || !c->tag)
			continue;
		if (c->tag->type == TAG_DEFINE_SHAPE) {
			draw_shape(p, (const struct swf_tag_define_shape *)c->tag, o->m, &o->cx, o->blend, clear);
		} else if (c->tag->type == TAG_DEFINE_SPRITE && c->has_obj) {
			/* parts_render_flash_sprite: the sprite's object under the placement. */
			struct fp_char *child = find(p, c->obj.id);
			if (!child || !child->tag || child->tag->type != TAG_DEFINE_SHAPE)
				continue;
			const float *a = o->m, *b = c->obj.m;
			float m[6] = {
				a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
				a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
				a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5],
			};
			struct swf_cxform_with_alpha cx;
			for (int k = 0; k < 4; k++) {
				cx.add_terms[k] = o->cx.add_terms[k] + c->obj.cx.add_terms[k];
				cx.mult_terms[k] = fixed16_mul(o->cx.mult_terms[k], c->obj.cx.mult_terms[k]);
			}
			draw_shape(p, (const struct swf_tag_define_shape *)child->tag, m, &cx, o->blend, clear);
		}
	}
}

unsigned fplay_render(struct fplay *p, float scale, int back)
{
	if (!p || !gl_init())
		return 0;
	if (!(scale > 0))
		scale = 1;
	int w = (int)lroundf(p->w * scale), h = (int)lroundf(p->h * scale);
	w = w < 1 ? 1 : w > FBO_MAX ? FBO_MAX : w;
	h = h < 1 ? 1 : h > FBO_MAX ? FBO_MAX : h;
	if (p->ftex && p->fw == w && p->fh == h && p->drawn_frame == p->frame && p->drawn_back == back &&
	    p->drawn_gen == p->gen)
		return p->ftex;
	if (!target(p, w, h))
		return 0;
	GLint prev_fb = 0, vp[4], prev_vao = 0, prev_prog = 0, prev_tex = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
	glGetIntegerv(GL_VIEWPORT, vp);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST), blend = glIsEnabled(GL_BLEND);
	gl.active_tex(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);

	gl.bind_fb(GL_FRAMEBUFFER, p->fbo);
	glViewport(0, 0, w, h);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	gl.bind_vao(R.vao);
	bool clear = back == FPLAY_CLEAR;
	if (back == FPLAY_CHECKER) {
		glDisable(GL_BLEND);
		gl.use(R.back);
		const float whole[9] = { (float)p->w, 0, 0, 0, (float)p->h, 0, 0, 0, 1 };
		gl.um3(R.b_world, 1, GL_FALSE, whole);
		gl.u2f(R.b_stage, (float)p->w, (float)p->h);
		gl.u1f(R.b_cell, 8 * CORE.scale);
		gl.u3f(R.b_dark, 0.40f, 0.40f, 0.42f);
		gl.u3f(R.b_light, 0.60f, 0.60f, 0.62f);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	} else {
		float v = back == FPLAY_WHITE ? 1.0f : 0.0f;
		glClearColor(v, v, v, clear ? 0.0f : 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glEnable(GL_BLEND);
	gl.blend_eq(GL_FUNC_ADD);
	gl.use(R.shape);
	gl.u2f(R.s_stage, (float)p->w, (float)p->h);
	gl.u1i(R.s_tex, 0);
	draw_list(p, clear);

	gl.bind_vao((GLuint)prev_vao);
	gl.use((GLuint)prev_prog);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
	gl.blend_sep(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	if (scissor)
		glEnable(GL_SCISSOR_TEST);
	if (!blend)
		glDisable(GL_BLEND);
	gl.bind_fb(GL_FRAMEBUFFER, (GLuint)prev_fb);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	p->drawn_frame = p->frame;
	p->drawn_back = back;
	p->drawn_gen = p->gen;
	return p->ftex;
}

uint8_t *fplay_rgba(struct fplay *p, int *pw, int *ph)
{
	if (!p || !fplay_render(p, 1, FPLAY_CLEAR))
		return NULL;
	int w = p->fw, h = p->fh;
	*pw = w;
	*ph = h;
	uint8_t *px = xmalloc((size_t)w * h * 4);
	GLint prev = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
	gl.bind_fb(GL_FRAMEBUFFER, p->fbo);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	gl.bind_fb(GL_FRAMEBUFFER, (GLuint)prev);
	/* The colours divided back from the alpha: a picture as a PNG holds it. */
	for (size_t i = 0; i < (size_t)w * h; i++) {
		uint8_t *q = px + i * 4;
		unsigned a = q[3];
		if (a == 255)
			continue;
		for (int k = 0; k < 3; k++) {
			unsigned v = a ? (q[k] * 255u + a / 2) / a : 0;
			q[k] = v > 255 ? 255 : (uint8_t)v;
		}
	}
	return px;
}

/* ---- a clear frame over what is drawn under it ---- */

static void premul_blend(const ImDrawList *dl, const ImDrawCmd *cmd)
{
	(void)dl;
	(void)cmd;
	if (gl.blend_sep)
		gl.blend_sep(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void fplay_image(ImDrawList *dl, unsigned tex, V2 a, V2 b)
{
	ImDrawCallback reset = igGetPlatformIO_Nil()->DrawCallback_ResetRenderState;
	if (!tex || !reset)
		return;
	ImDrawList_AddCallback(dl, premul_blend, NULL, 0);
	ImDrawList_AddImage(dl, texref(tex), a, b, v2(0, 0), v2(1, 1), 0xffffffff);
	ImDrawList_AddCallback(dl, reset, NULL, 0);
}
