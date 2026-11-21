/* rance-browser.exe: the window, the tabs, and the command line.
 *
 *   rance-browser [--game DIR] [--scale F] [FILE.aff|FILE.swf]
 *                                            the window (--scale: the size of the text and layout,
 *                                            the display's scale by default; a Flash file from disk
 *                                            opens in the Flash tab)
 *   rance-browser --check [--game DIR] [--out FILE]
 *                                            read a sample of everything, check and play every Flash
 *                                            file, print the counts as JSON
 *   rance-browser --shot TAB FILE.png [--select N|NAME] [--grid] [--frame N] [--file PATH]
 *                 [--motion NAME] [--fight BG] [--fight-camera] [--walk] [--bones] [--size WxH] [--scale F]
 *                                            draw one tab offscreen and save it (the guides' pictures)
 *   rance-browser --export OUT.glb --select NAME
 *   rance-browser --export DIR (--select NAME | --kind KIND | --all)
 *                                            3D models as glTF files, a folder's with export.json
 *
 * The game folder: --game, else the folder above the program's own (RQ MOD puts it in the game's
 * xsystem4 folder), else the program's own folder, else the last one used, else a picker. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_opengl.h>

#include "system4.h"
#include "system4/file.h"

#include "browser.h"

#ifndef BROWSER_BUILD_NOTE
#define BROWSER_BUILD_NOTE ""
#endif

SDL_Window *g_window;

static struct {
	const char *game;
	bool check;
	const char *out;
	int shot_tab;
	const char *shot_path;
	int select;
	const char *select_name;   /* --select with a name: a Flash file's or a 3D model's */
	bool grid;
	int frame;                 /* --frame: the Flash tab's frame, or the 3D tab's motion's */
	bool frame_given;          /* a 3D motion's frame 0 is a frame */
	const char *file;          /* --file, or a .swf or .aff given alone */
	const char *motion;        /* --motion: the 3D tab's motion */
	const char *fight;         /* --fight: a fight background for the 3D tab's monster */
	bool fight_camera;
	bool walk, bones;          /* --walk, --bones: the 3D tab's lines */
	int w, h;
	bool size_given;
	float scale;   /* --scale, or 0 */
	const char *export_out;    /* --export: a .glb file or a folder */
	const char *kind;          /* --kind, for --export */
	bool all;                  /* --all, for --export */
} A = { .shot_tab = -1, .select = -1, .w = 1440, .h = 860 };

static void usage(void)
{
	win_attach_console();
	printf(APP_NAME " " BROWSER_VERSION ": the files of AliceSoft's System 4 games.\n\n"
	       "  " APP_EXE " [--game DIR] [--scale F] [FILE.aff|FILE.swf]\n"
	       "  " APP_EXE " --check [--game DIR] [--out FILE]\n"
	       "  " APP_EXE " --shot TAB FILE.png [--game DIR] [--select N|NAME] [--grid] [--frame N] [--file PATH]\n"
	       "            [--motion NAME] [--fight BG] [--fight-camera] [--walk] [--bones] [--size WxH] [--scale F]\n"
	       "      TAB: cgs, flash, music, sounds, 3d, textures, pictures, dungeons or names\n"
	       "      --select N: the CG number (cgs), else the place in the list; a Flash file's name (flash),\n"
	       "      a model's folder, Kind/folder (3d: Chara/gokiburi, Etc/takarabako) or a model's number\n"
	       "      (3d: 100122, rq.Chara's model, with that MonsterInfo entry's motions)\n"
	       "      --grid: the grid of pictures; --frame N: the Flash file's frame (from 1), or the motion's\n"
	       "      (3d; default: the frame the game puts it on)\n"
	       "      --file PATH: a .swf or .aff from disk in the Flash tab\n"
	       "      --motion NAME: a MOT of the model's folder, or none (3d; default: the first the game plays);\n"
	       "      --fight BG: a monster on a fight background (a BG folder), --fight-camera: the game's camera;\n"
	       "      --walk, --bones: the walk mesh's and the skeleton's lines (3d)\n"
	       "  " APP_EXE " --export OUT.glb --select NAME [--game DIR]\n"
	       "  " APP_EXE " --export DIR (--select NAME | --kind KIND | --all) [--game DIR]\n"
	       "      a 3D model as a glTF file (NAME as for --shot 3d); to a folder, each as DIR\\Kind\\folder.glb\n"
	       "      with DIR\\export.json (KIND: chara, map, bg or etc, or a list: chara,map)\n"
	       "  --scale F: the size of the text and the layout, 1 to 4 (default: the display's scale;\n"
	       "      1 for --shot)\n");
}

static void on_error(const char *msg)
{
	if (A.check || A.shot_path || A.export_out) {
		fprintf(stderr, "%s\n", msg);
		return;
	}
	win_message(APP_NAME, msg);
}

static void parent_of(char *dir)
{
	char *a = strrchr(dir, '/'), *b = strrchr(dir, '\\');
	char *s = a > b ? a : b;
	if (s)
		*s = 0;
}

static bool pick_game(char *out, size_t n)
{
	while (win_pick_folder("Pick a game's folder (the one with System40.ini or AliceStart.ini)", out, n)) {
		if (game_dir_ok(out))
			return true;
		win_message(APP_NAME, "That folder is not a System 4 game's: it needs System40.ini or AliceStart.ini "
		            "and the game's CG archive (a .afa or .ald file named after the game). Pick the game's own folder.");
	}
	return false;
}

static bool find_game(char *out, size_t n)
{
	if (A.game) {
		snprintf(out, n, "%s", A.game);
		return game_dir_ok(out);
	}
	char dir[1024];
	if (win_exe_dir(dir, sizeof dir)) {
		char up[1024];
		snprintf(up, sizeof up, "%s", dir);
		parent_of(up);
		if (game_dir_ok(up)) {
			snprintf(out, n, "%s", up);
			return true;
		}
		if (game_dir_ok(dir)) {
			snprintf(out, n, "%s", dir);
			return true;
		}
	}
	const char *last = settings_get("game");
	if (last && game_dir_ok(last)) {
		snprintf(out, n, "%s", last);
		return true;
	}
	if (A.check || A.shot_path || A.export_out)
		return false;
	return pick_game(out, n);
}

static void parse_args(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--game") && i + 1 < argc) {
			A.game = argv[++i];
		} else if (!strcmp(a, "--check")) {
			A.check = true;
		} else if (!strcmp(a, "--out") && i + 1 < argc) {
			A.out = argv[++i];
		} else if (!strcmp(a, "--shot") && i + 2 < argc) {
			const char *tab = argv[++i];
			A.shot_path = argv[++i];
			for (int t = 0; t < TAB_COUNT; t++)
				if (!strcmp(tab, tab_keys[t]))
					A.shot_tab = t;
		} else if (!strcmp(a, "--select") && i + 1 < argc) {
			const char *v = argv[++i];
			char *end = NULL;
			long n = strtol(v, &end, 10);
			if (end && end != v && !*end)
				A.select = (int)n;
			else
				A.select_name = v;
		} else if (!strcmp(a, "--grid")) {
			A.grid = true;
		} else if (!strcmp(a, "--frame") && i + 1 < argc) {
			long n = strtol(argv[++i], NULL, 10);
			A.frame = n < -1000000 ? -1000000 : n > 1000000 ? 1000000 : (int)n;
			A.frame_given = true;
		} else if (!strcmp(a, "--motion") && i + 1 < argc) {
			A.motion = argv[++i];
		} else if (!strcmp(a, "--fight") && i + 1 < argc) {
			A.fight = argv[++i];
		} else if (!strcmp(a, "--fight-camera")) {
			A.fight_camera = true;
		} else if (!strcmp(a, "--walk")) {
			A.walk = true;
		} else if (!strcmp(a, "--bones")) {
			A.bones = true;
		} else if (!strcmp(a, "--file") && i + 1 < argc) {
			A.file = argv[++i];
		} else if (!strcmp(a, "--export") && i + 1 < argc) {
			A.export_out = argv[++i];
		} else if (!strcmp(a, "--kind") && i + 1 < argc) {
			A.kind = argv[++i];
		} else if (!strcmp(a, "--all")) {
			A.all = true;
		} else if (a[0] != '-' && strlen(a) > 4 && (!strcasecmp(a + strlen(a) - 4, ".aff") || !strcasecmp(a + strlen(a) - 4, ".swf"))) {
			A.file = a;
		} else if (!strcmp(a, "--size") && i + 1 < argc) {
			sscanf(argv[++i], "%dx%d", &A.w, &A.h);
			A.size_given = true;
		} else if (!strcmp(a, "--scale") && i + 1 < argc) {
			A.scale = (float)atof(argv[++i]);
			if (A.scale < 1 || A.scale > 4)
				A.scale = 0;
		} else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
			usage();
			exit(0);
		} else {
			usage();
			exit(2);
		}
	}
}

/* ---- the fonts ---- */

/* The text: the game's own font when its profile names one (Rance Quest's), else Windows' Segoe
 * UI. Glyphs it lacks (Japanese names) come from Windows' Japanese font. */
static const char *ui_fonts[] = {
	"C:/Windows/Fonts/segoeui.ttf",
	"C:/Windows/Fonts/arial.ttf",
};

static const char *jp_fallbacks[] = {
	"C:/Windows/Fonts/YuGothM.ttc",
	"C:/Windows/Fonts/meiryo.ttc",
	"C:/Windows/Fonts/msgothic.ttc",
};

static void load_fonts(float size)
{
	ImGuiIO *io = igGetIO_Nil();
	ImFont *font = NULL;
	if (G.font[0])
		font = ImFontAtlas_AddFontFromFileTTF(io->Fonts, G.font, size, NULL, NULL);
	for (size_t i = 0; !font && i < sizeof ui_fonts / sizeof ui_fonts[0]; i++)
		if (file_exists(ui_fonts[i]))
			font = ImFontAtlas_AddFontFromFileTTF(io->Fonts, ui_fonts[i], size, NULL, NULL);
	if (!font)
		ImFontAtlas_AddFontDefault(io->Fonts, NULL);
	for (size_t i = 0; i < sizeof jp_fallbacks / sizeof jp_fallbacks[0]; i++) {
		if (!file_exists(jp_fallbacks[i]))
			continue;
		ImFontConfig *cfg = ImFontConfig_ImFontConfig();
		cfg->MergeMode = true;
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, jp_fallbacks[i], size, cfg, NULL);
		ImFontConfig_destroy(cfg);
		break;
	}
}

bool font_has_japanese(const char *path)
{
	ImGuiContext *ctx = igCreateContext(NULL);
	ImGuiIO *io = igGetIO_Nil();
	ImFont *f = ImFontAtlas_AddFontFromFileTTF(io->Fonts, path, 18, NULL, NULL);
	bool ok = f && ImFont_IsGlyphInFont(f, 0x30A2) && ImFont_IsGlyphInFont(f, 0x9854);
	igDestroyContext(ctx);
	return ok;
}

static void style(void)
{
	/* From ImGui's defaults each time, so a new scale does not multiply the last one. */
	ImGuiStyle *fresh = ImGuiStyle_ImGuiStyle();
	*igGetStyle() = *fresh;
	ImGuiStyle_destroy(fresh);
	igStyleColorsDark(NULL);
	ImGuiStyle *s = igGetStyle();
	s->WindowRounding = 0;
	s->ChildRounding = 4;
	s->FrameRounding = 4;
	s->GrabRounding = 4;
	s->TabRounding = 4;
	s->WindowPadding = v2(10, 8);
	s->FramePadding = v2(8, 5);
	s->ItemSpacing = v2(8, 6);
	/* The sizes and the text at the display's scale; ImGui 1.92 draws the font's glyphs at that size. */
	ImGuiStyle_ScaleAllSizes(s, CORE.scale);
	s->FontScaleDpi = CORE.scale;
}

/* The display's scale, 1 at 100 % (96 DPI). The program is per-monitor DPI aware, so Windows does
 * not stretch it: it draws at the screen's own pixels, and the layout grows by this. */
static float display_scale(int display)
{
	float s = ImGui_ImplSDL2_GetContentScaleForDisplay(display < 0 ? 0 : display);
	return s >= 1 && s <= 4 ? s : 1;
}

/* ---- one frame ---- */

static const char *tab_labels[TAB_COUNT] = { "CGs", "Flash", "Music", "Sounds", "3D models", "3D textures",
                                             "Other pictures", "Dungeon textures", "Names" };
static void (*const tab_fns[TAB_COUNT])(void) = { tab_cgs, tab_flash, tab_music, tab_sounds, tab_3d, tab_textures,
                                                  tab_alk, tab_dungeons, tab_names };

bool tab_shown(int t)
{
	switch (t) {
	case TAB_CGS: return true;
	case TAB_FLASH: return G.nflash > 0 || flash_has_file();
	case TAB_MUSIC: return G.nmusic > 0;
	case TAB_SOUNDS: return G.nsounds > 0;
	case TAB_3D: return tab3d_count() > 0;
	case TAB_TEXTURES: return G.sets[PK_TEX].n > 0;
	case TAB_PICTURES: return G.sets[PK_ALK].n > 0;
	case TAB_DUNGEONS: return G.sets[PK_DTX].n > 0;
	case TAB_NAMES: return G.rq && G.nkinds > 0;
	}
	return false;
}

static void add_count(char *buf, size_t n, int count, const char *one, const char *many)
{
	if (!count)
		return;
	size_t len = strlen(buf);
	snprintf(buf + len, n - len, "%s%d %s", len ? " · " : "", count, count == 1 ? one : many);
}

static void status_line(void)
{
	igSeparator();
	if (igSmallButton("Open another game...")) {
		char dir[1024];
		if (pick_game(dir, sizeof dir)) {
			settings_set("game", dir);
			if (win_relaunch(dir))
				exit(0);
			ui_status("Could not start the program again; it opens that game next time");
		}
	}
	igSameLine(0, S(12));
	if (UI.status[0] && igGetTime() < UI.status_until) {
		igTextColored(v4(1.0f, 0.85f, 0.4f, 1), "%s", UI.status);
		return;
	}
	char counts[512] = "";
	add_count(counts, sizeof counts, G.ncg, "CG", "CGs");
	add_count(counts, sizeof counts, G.nflash, "Flash file", "Flash files");
	add_count(counts, sizeof counts, G.nmusic, "track", "tracks");
	add_count(counts, sizeof counts, G.nsounds, "sound", "sounds");
	add_count(counts, sizeof counts, tab3d_count(), "3D model", "3D models");
	add_count(counts, sizeof counts, G.sets[PK_TEX].n, "texture", "textures");
	add_count(counts, sizeof counts, G.sets[PK_ALK].n, "other picture", "other pictures");
	add_count(counts, sizeof counts, G.sets[PK_DTX].n, "dungeon texture", "dungeon textures");
	if (G.rq)
		add_count(counts, sizeof counts, G.npictures, "name with a picture", "names with a picture");
	igText("%s", G.title);
	if (!G.tested) {
		igSameLine(0, S(8));
		igTextColored(v4(1.0f, 0.75f, 0.35f, 1), "(not tested)");
		if (igIsItemHovered(0))
			igSetTooltip("The browser opens this game by the rules every System 4 game follows, but nobody has\n"
			             "checked its files with it yet. Some kinds of file may be missing or read wrong.");
	}
	igSameLine(0, S(12));
	igTextDisabled("%s   ·   %s", G.dir, counts);
	if (G.errors[0]) {
		igSameLine(0, S(16));
		igTextColored(v4(1, 0.45f, 0.4f, 1), "%s", G.errors);
	}
}

static void frame_ui(void)
{
	ImGuiIO *io = igGetIO_Nil();
	igSetNextWindowPos(v2(0, 0), ImGuiCond_Always, v2(0, 0));
	igSetNextWindowSize(io->DisplaySize, ImGuiCond_Always);
	igBegin("##root", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus);
	float status_h = igGetFrameHeightWithSpacing() + 4;
	int want = UI.want_tab;
	UI.want_tab = -1;
	static int shown = -1;
	if (igBeginTabBar("tabs", 0)) {
		for (int t = 0; t < TAB_COUNT; t++) {
			if (!tab_shown(t))
				continue;
			char label[64];
			snprintf(label, sizeof label, "%s###%s", tab_labels[t], tab_keys[t]);
			if (igBeginTabItem(label, NULL, want == t ? ImGuiTabItemFlags_SetSelected : 0)) {
				/* Music, a sound or a Flash file's sounds stop when another tab opens. */
				if (shown != t) {
					if (shown >= 0) {
						player_stop();
						flash_leave();
					}
					shown = t;
				}
				igBeginChild_Str("tab", v2(0, -status_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
				tab_fns[t]();
				igEndChild();
				igEndTabItem();
			}
		}
		igEndTabBar();
	}
	status_line();
	batch_frame();
	igEnd();
}

/* ---- --shot: draw into a framebuffer of our own and save it ---- */

static PFNGLGENFRAMEBUFFERSPROC p_gen_fb;
static PFNGLBINDFRAMEBUFFERPROC p_bind_fb;
static PFNGLFRAMEBUFFERTEXTURE2DPROC p_fb_tex;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC p_fb_status;

static GLuint shot_fbo(int w, int h)
{
	p_gen_fb = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
	p_bind_fb = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
	p_fb_tex = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
	p_fb_status = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
	if (!p_gen_fb || !p_bind_fb || !p_fb_tex || !p_fb_status)
		return 0;
	GLuint tex, fbo;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
	p_gen_fb(1, &fbo);
	p_bind_fb(GL_FRAMEBUFFER, fbo);
	p_fb_tex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	bool ok = p_fb_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	p_bind_fb(GL_FRAMEBUFFER, 0);
	return ok ? fbo : 0;
}

static bool shot_save(int w, int h, const char *path)
{
	uint8_t *px = xmalloc((size_t)w * h * 4), *row = xmalloc((size_t)w * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (int y = 0; y < h / 2; y++) {
		memcpy(row, px + (size_t)y * w * 4, (size_t)w * 4);
		memcpy(px + (size_t)y * w * 4, px + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
		memcpy(px + (size_t)(h - 1 - y) * w * 4, row, (size_t)w * 4);
	}
	for (size_t i = 0; i < (size_t)w * h; i++)
		px[i * 4 + 3] = 255;
	bool ok = save_png(path, px, w, h);
	free(px);
	free(row);
	return ok;
}

static void select_for_shot(void)
{
	switch (A.shot_tab) {
	case TAB_CGS: cgs_select_first(A.select); pictures_grid(PK_CG, A.grid); break;
	case TAB_FLASH:
		if (A.file)
			flash_open_file(A.file);
		else if (A.select_name)
			flash_select_name(A.select_name);
		else
			flash_select(A.select);
		flash_shot(A.frame, A.grid);
		break;
	case TAB_MUSIC: music_select(A.select); break;
	case TAB_SOUNDS: sounds_select(A.select); break;
	case TAB_3D: break;   /* picked and checked before the window opened (main) */
	case TAB_TEXTURES: set_select(PK_TEX, A.select); pictures_grid(PK_TEX, A.grid); break;
	case TAB_PICTURES: set_select(PK_ALK, A.select); pictures_grid(PK_ALK, A.grid); break;
	case TAB_DUNGEONS: set_select(PK_DTX, A.select); pictures_grid(PK_DTX, A.grid); break;
	case TAB_NAMES: names_select(A.select); break;
	}
	UI.want_tab = A.shot_tab;
}

/* ---- main ---- */

/* A reader's message (the player's, core.h) goes to the status line. */
static void core_message(const char *text)
{
	ui_status("%s", text);
}

int main(int argc, char **argv)
{
	parse_args(argc, argv);
	if (A.check || A.export_out)
		win_attach_console();
	sys_silent = true;
	sys_error_handler = on_error;
	win_init();
	CORE.status = core_message;

	char dir[1024] = "";
	if (!find_game(dir, sizeof dir)) {
		if (A.check)
			return run_check(dir[0] ? dir : "(none)", A.out);
		if (A.shot_path || A.export_out)
			fprintf(stderr, APP_EXE ": no game folder (use --game DIR)\n");
		return 1;
	}
	if (A.check)
		return run_check(dir, A.out);
	if (A.export_out) {
		if (A.shot_path || A.file) {
			usage();
			return 2;
		}
		/* a number for --select is a folder's name too */
		char num[32];
		const char *select = A.select_name;
		if (!select && A.select >= 0) {
			snprintf(num, sizeof num, "%d", A.select);
			select = num;
		}
		struct export_args ea = { A.export_out, select, A.kind, A.all };
		return run_export(dir, &ea);
	}
	if (A.shot_path && A.shot_tab < 0) {
		usage();
		return 2;
	}

	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		on_error(SDL_GetError());
		return 1;
	}
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		ui_status("No sound: %s", SDL_GetError());
	if (!game_open(dir)) {
		char msg[4200];
		snprintf(msg, sizeof msg, "Could not open the game in %s: %s", dir, G.errors);
		on_error(msg);
		return 1;
	}
	/* A Flash file from disk opens the Flash tab, a game with none of its own too. */
	if (A.file) {
		if (flash_open_file(A.file))
			UI.want_tab = TAB_FLASH;
		else if (A.shot_path) {
			fprintf(stderr, APP_EXE ": %s is not a .swf or .aff file\n", A.file);
			return 1;
		} else
			ui_status("Not a .swf or .aff file: %s", A.file);
	}
	if (A.shot_path && !tab_shown(A.shot_tab)) {
		fprintf(stderr, APP_EXE ": %s has no %s tab\n", G.title, tab_keys[A.shot_tab]);
		return 1;
	}
	/* A name is a Flash file's or a 3D model's: a shot of another would pass for it. */
	if (A.shot_path && A.select_name) {
		bool found = A.shot_tab == TAB_FLASH ? (A.file || flash_select_name(A.select_name))
		           : A.shot_tab == TAB_3D ? tab3d_select_name(A.select_name) : false;
		if (!found) {
			if (A.shot_tab != TAB_FLASH && A.shot_tab != TAB_3D)
				fprintf(stderr, APP_EXE ": --select takes a name only for the flash and 3d tabs\n");
			else if (A.shot_tab == TAB_FLASH)
				fprintf(stderr, APP_EXE ": %s has no Flash file named %s\n", G.title, A.select_name);
			else
				fprintf(stderr, APP_EXE ": %s has no 3D model named %s\n", G.title, A.select_name);
			return 1;
		}
	}
	/* The 3D tab's model, motion, frame and fight, checked before the window opens. */
	if (A.shot_path && A.shot_tab == TAB_3D) {
		char why[512] = "";
		if (!A.select_name && A.select > 100000) {
			/* a model's number, opened as the Names tab's "Open in the 3D tab" opens it */
			if (!tab3d_model_folder(A.select)) {
				fprintf(stderr, APP_EXE ": %s has no 3D model numbered %d\n", G.title, A.select);
				return 1;
			}
			tab3d_open_model(A.select);
		} else if (!A.select_name && A.select >= tab3d_count()) {
			fprintf(stderr, APP_EXE ": --select %d: %s has %d 3D models (0 to %d), or a model's number from 100001\n",
			        A.select, G.title, tab3d_count(), tab3d_count() - 1);
			return 1;
		} else if (!A.select_name) {
			tab3d_select(A.select);
		}
		struct shot3d s = { A.motion, A.frame, A.frame_given, A.fight, A.fight_camera, A.walk, A.bones };
		if (!tab3d_shot(&s, why, sizeof why)) {
			fprintf(stderr, APP_EXE ": %s\n", why);
			return 1;
		}
	}
	settings_set("game", dir);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	char title[1600];
	snprintf(title, sizeof title, APP_NAME " " BROWSER_VERSION "%s%s%s  -  %s%s  -  %s",
	         BROWSER_BUILD_NOTE[0] ? " (" : "", BROWSER_BUILD_NOTE, BROWSER_BUILD_NOTE[0] ? ")" : "",
	         G.title, G.tested ? "" : " (not tested)", dir);
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if (A.shot_path)
		flags |= SDL_WINDOW_HIDDEN;
	int ww = A.w, wh = A.h;
	if (A.shot_path) {
		CORE.scale = A.scale > 0 ? A.scale : 1;
	} else {
		CORE.scale = A.scale > 0 ? A.scale : display_scale(0);
		if (!A.size_given) {
			ww = (int)(A.w * CORE.scale);
			wh = (int)(A.h * CORE.scale);
		}
		SDL_Rect r;
		if (SDL_GetDisplayUsableBounds(0, &r) == 0) {
			if (ww > r.w - 40) ww = r.w - 40;
			if (wh > r.h - 60) wh = r.h - 60;
		}
	}
	g_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
	if (!g_window) {
		on_error(SDL_GetError());
		return 1;
	}
	SDL_GLContext gl = SDL_GL_CreateContext(g_window);
	if (!gl) {
		on_error(SDL_GetError());
		return 1;
	}
	SDL_GL_MakeCurrent(g_window, gl);
	SDL_GL_SetSwapInterval(A.shot_path ? 0 : 1);

	igCreateContext(NULL);
	ImGuiIO *io = igGetIO_Nil();
	io->IniFilename = NULL;
	style();
	load_fonts(18);
	ImGui_ImplSDL2_InitForOpenGL(g_window, gl);
	ImGui_ImplOpenGL3_Init("#version 130");

	pics_init();
	player_init();

	GLuint fbo = 0;
	if (A.shot_path) {
		fbo = shot_fbo(A.w, A.h);
		if (!fbo) {
			fprintf(stderr, APP_EXE ": no framebuffer for --shot\n");
			return 1;
		}
		select_for_shot();
	}

	int code = 0, quiet = 0;
	for (bool run = true; run; UI.frame++) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			ImGui_ImplSDL2_ProcessEvent(&ev);
			if (ev.type == SDL_QUIT)
				run = false;
			if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE)
				run = false;
			/* A .swf or .aff dropped on the window plays in the Flash tab. */
			if (ev.type == SDL_DROPFILE) {
				if (flash_open_file(ev.drop.file))
					UI.want_tab = TAB_FLASH;
				else
					ui_status("Drop a .swf or .aff file to play it: %s", base_name(ev.drop.file));
				SDL_free(ev.drop.file);
			}
			/* Moved to a screen with another scale. */
			if (!A.shot_path && !A.scale && ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
				float s = display_scale(ev.window.data1);
				if (s != CORE.scale) {
					CORE.scale = s;
					style();
				}
			}
		}
		pics_frame();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		igNewFrame();
		frame_ui();
		igRender();
		if (fbo)
			p_bind_fb(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, (int)(io->DisplaySize.x * io->DisplayFramebufferScale.x), (int)(io->DisplaySize.y * io->DisplayFramebufferScale.y));
		glClearColor(0.08f, 0.08f, 0.09f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
		if (fbo) {
			/* Wait until the thumbnails on screen are made (the Names tab's 3D pictures too) and the
			 * frame stays the same. */
			quiet = pics_idle() && tab3d_pics_idle() ? quiet + 1 : 0;
			if ((UI.frame > 30 && quiet > 20) || UI.frame > 1500) {
				code = shot_save(A.w, A.h, A.shot_path) ? 0 : 1;
				if (code)
					fprintf(stderr, APP_EXE ": could not write %s\n", A.shot_path);
				/* A 3D model that did not load or draw: the picture shows why, and the shot fails. */
				if (!code && A.shot_tab == TAB_3D && tab3d_error()[0]) {
					fprintf(stderr, APP_EXE ": %s\n", tab3d_error());
					code = 1;
				}
				run = false;
			}
			p_bind_fb(GL_FRAMEBUFFER, 0);
			SDL_Delay(4);
		} else {
			SDL_GL_SwapWindow(g_window);
			if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED)
				SDL_Delay(50);
		}
	}
	player_close();
	mixer_shutdown();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	igDestroyContext(NULL);
	SDL_GL_DeleteContext(gl);
	SDL_DestroyWindow(g_window);
	SDL_Quit();
	/* The thumbnail thread may be inside an archive read: leave without waiting for it. */
	_Exit(code);
}
