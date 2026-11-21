# Rance Browser: a window over the files of AliceSoft's System 4 games (CGs, Flash files, music,
# sounds, textures), read-only. Built on Windows with MSYS2 UCRT64; see README.md.
#
# Alone:            meson setup build && ninja -C build
# Inside RQ MOD:    the engine calls subproject('rance-browser') with rq_ids_dir and rq_lua, so the
#                   names and the Lua lines are the loader's own (meson then shares its libsys4).
# Its readers:      the static library rance_core (src/core), which another program (a scene
#                   editor) takes as subproject('rance-browser').get_variable('rance_core_dep'),
#                   with cimgui in it.
# Its 3D view:      the static library rance_3d (src/core/3d), apart, on cglm (the option 3d).
project('rance-browser', 'c', 'cpp',
        version : '1.1.0',
        meson_version : '>= 0.59',
        default_options : ['c_std=c11', 'cpp_std=c++17', 'warning_level=1'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')

static_libs = host_machine.system() == 'windows'
zlib = dependency('zlib', static : static_libs)
sdl2 = dependency('sdl2', static : static_libs)
sndfile = dependency('sndfile', static : static_libs)
gl = dependency('GL')
libm = meson.get_compiler('c').find_library('m', required : false)
libsys4_dep = subproject('libsys4').get_variable('libsys4_dep')

py = find_program('python3', 'python', required : true)

# ---- Dear ImGui through cimgui (MIT), vendored in third_party/cimgui ----

cimgui_inc = include_directories('third_party/cimgui', 'third_party/cimgui/imgui',
                                 'third_party/cimgui/imgui/backends')
cimgui_lib = static_library('cimgui',
    ['third_party/cimgui/cimgui.cpp',
     'third_party/cimgui/imgui/imgui.cpp',
     'third_party/cimgui/imgui/imgui_demo.cpp',
     'third_party/cimgui/imgui/imgui_draw.cpp',
     'third_party/cimgui/imgui/imgui_tables.cpp',
     'third_party/cimgui/imgui/imgui_widgets.cpp',
     'src/cimgui_glue.cpp'],
    cpp_args : ['-DCIMGUI_NO_EXPORT', '-DCIMGUI_USE_SDL2', '-DCIMGUI_USE_OPENGL3',
                '-DIMGUI_DISABLE_DEMO_WINDOWS', '-w'],
    include_directories : cimgui_inc,
    dependencies : [sdl2])

# ---- data built into the program ----

# The games' profiles (games/*.tsv): titles, labels for the CG folders or number ranges.
game_tsvs = files(
    'games/Rance6.tsv',
    'games/RanceQuest.tsv',
)
games_c = custom_target('games_c',
    input : game_tsvs,
    output : 'games_embed.c',
    command : [py, files('tools/embed_games.py'), '@OUTPUT@', '@INPUT@'])

# RQ MOD's names (rq.ids) and their pictures, for Rance Quest Magnum's Names tab. Inside RQ MOD the
# engine's data/ids; alone, the copy in games/rq-ids (scripts in RQ MOD keep it the same). The paths
# go to the scripts as plain arguments, since a subproject may not take files from outside itself as
# inputs; each script writes a depfile so the build runs again when they change.
rq_ids_dir = get_option('rq_ids_dir')
if rq_ids_dir == ''
    rq_ids_dir = meson.current_source_dir() / 'games/rq-ids'
endif
embed = files('tools/embed.py')
rq_ids_c = custom_target('rq_ids_json',
    output : 'rq_ids_json.c',
    depfile : 'rq_ids_json.c.d',
    command : [py, embed, rq_ids_dir / 'ids.json', '@OUTPUT@', 'rq_ids_json', '@DEPFILE@'])
rq_pictures_c = custom_target('rq_pictures_tsv',
    output : 'rq_pictures_tsv.c',
    depfile : 'rq_pictures_tsv.c.d',
    command : [py, embed, rq_ids_dir / 'pictures.tsv', '@OUTPUT@', 'rq_pictures_tsv', '@DEPFILE@'])

# The Lua lines for RQ MOD, checked against the loader's rq.lua when it is given, so a renamed call
# fails the build instead of leaving a stale line.
rq_lua_lines_c = custom_target('rq_lua_lines',
    input : 'src/lua_lines.h',
    output : 'rq_lua_lines.c',
    depfile : 'rq_lua_lines.c.d',
    command : [py, files('tools/check_lua_lines.py'), '@OUTPUT@', '@DEPFILE@', '@INPUT@', get_option('rq_lua')])

# ---- the readers: the rance_core library (src/core/core.h) ----

c_warnings = ['-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-missing-field-initializers']
core_inc = include_directories('src/core')
core_deps = [sdl2, sndfile, zlib, libm, gl, libsys4_dep]

rance_core_lib = static_library('rance_core',
    ['src/core/core.c', 'src/core/game.c', 'src/core/games.c', 'src/core/sets.c',
     'src/core/pics.c', 'src/core/player.c', 'src/core/mixer.c', 'src/core/swf.c',
     'src/core/swf_check.c', 'src/core/flash_player.c', 'src/core/cJSON.c',
     games_c, rq_ids_c, rq_pictures_c],
    c_args : c_warnings + ['-DCIMGUI_NO_EXPORT'],
    include_directories : [core_inc, cimgui_inc],
    dependencies : core_deps)

# What a program over the readers takes: their header, cimgui's, and the libraries in link order.
rance_core_dep = declare_dependency(
    link_with : [rance_core_lib, cimgui_lib],
    include_directories : [core_inc, cimgui_inc],
    compile_args : ['-DCIMGUI_NO_EXPORT'],
    dependencies : core_deps)

# ---- the 3D view: the rance_3d library (src/core/3d/rb3d.h) ----

# A copy of RQ MOD's 3D renderer (src/core/3d: the engine's files unchanged, src/core/COPIES says
# which) on cglm (MIT), from the engine's own wrap; inside RQ MOD meson shares the engine's cglm.
# Apart from rance_core, so a program over the readers takes no 3D code. Built when cglm is found,
# and always with -D3d=enabled, as the releases build.
cglm_proj = subproject('cglm', required : get_option('3d'),
                       default_options : ['default_library=static', 'install=false'])
have_3d = cglm_proj.found()
if have_3d
    rb3d_shaders = []
    foreach s : ['reign.v', 'reign.f', 'reign_outline.v', 'reign_outline.f', 'reign_shadow.v',
                 'reign_shadow.f']
        sym = 'rb3d_' + s.replace('.', '_') + '_glsl'
        rb3d_shaders += custom_target(sym,
            input : 'src/core/3d/shaders' / (s + '.glsl'),
            output : sym + '.c',
            command : [py, embed, '@INPUT@', '@OUTPUT@', sym])
    endforeach
    rance_3d_lib = static_library('rance_3d',
        ['src/core/3d/rb3d.c', 'src/core/3d/rb3d_models.c', 'src/core/3d/rb3d_check.c', 'src/core/3d/parser.c',
         'src/core/3d/model.c', 'src/core/3d/mpr.c',
         'src/core/3d/collision.c', 'src/core/3d/reign.c', 'src/core/3d/renderer.c',
         'src/core/3d/particle.c', 'src/core/3d/s3de.c'] + rb3d_shaders,
        # -Wno-maybe-uninitialized: the later engines' effects (s3de.c) set vectors in a switch.
        c_args : c_warnings + ['-Wno-maybe-uninitialized'],
        include_directories : include_directories('src/core/3d', 'src/core/3d/include'),
        dependencies : [sdl2, gl, libm, libsys4_dep, cglm_proj.get_variable('cglm_dep')])
    rance_3d_dep = declare_dependency(
        link_with : rance_3d_lib,
        include_directories : include_directories('src/core/3d'),
        compile_args : ['-DRB_3D'],
        dependencies : [sdl2, gl, libm, libsys4_dep])
else
    rance_3d_dep = declare_dependency()
endif

# ---- the program ----

browser_version = meson.project_version()
build_note = get_option('build_note')

browser_src = ['src/main.c', 'src/ui.c', 'src/win.c', 'src/check.c',
     'src/tab_pictures.c', 'src/tab_flash.c', 'src/tab_audio.c', 'src/tab_names.c', 'src/tab_3d.c',
     rq_lua_lines_c]
# The exe's icon (res/rance-browser.ico, from res/rance-browser.png).
if host_machine.system() == 'windows'
    browser_src += import('windows').compile_resources('res/rance-browser.rc',
        include_directories : include_directories('res'))
endif

executable('rance-browser',
    browser_src,
    c_args : c_warnings + [
              '-DBROWSER_VERSION="' + browser_version + '"',
              '-DBROWSER_BUILD_NOTE="' + build_note + '"'],
    include_directories : include_directories('src'),
    dependencies : [rance_core_dep, rance_3d_dep],
    link_args : ['-static', '-lstdc++', '-lcomdlg32', '-lole32', '-lshell32', '-luuid'],
    win_subsystem : 'windows',
    install : true)
