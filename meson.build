# Rance Browser: a window over the files of AliceSoft's System 4 games (CGs, Flash files, music,
# sounds, textures), read-only. Built on Windows with MSYS2 MINGW64; see README.md.
#
# Alone:            meson setup build && ninja -C build
# Inside RQ MOD:    the engine calls subproject('rance-browser') with rq_ids_dir and rq_lua, so the
#                   names and the Lua lines are the loader's own (meson then shares its libsys4).
project('rance-browser', 'c', 'cpp',
        version : '1.0.0',
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

# ---- the program ----

browser_version = meson.project_version()
build_note = get_option('build_note')

executable('rance-browser',
    ['src/main.c', 'src/game.c', 'src/games.c', 'src/sets.c', 'src/pics.c', 'src/player.c',
     'src/ui.c', 'src/win.c', 'src/check.c',
     'src/tab_pictures.c', 'src/tab_flash.c', 'src/tab_audio.c', 'src/tab_names.c',
     'src/swf.c', 'src/cJSON.c',
     games_c, rq_ids_c, rq_pictures_c, rq_lua_lines_c],
    c_args : ['-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-missing-field-initializers',
              '-DBROWSER_VERSION="' + browser_version + '"',
              '-DBROWSER_BUILD_NOTE="' + build_note + '"',
              '-DCIMGUI_NO_EXPORT'],
    include_directories : [include_directories('src'), cimgui_inc],
    dependencies : [sdl2, sndfile, zlib, libm, gl, libsys4_dep],
    link_with : cimgui_lib,
    link_args : ['-static', '-lstdc++', '-lcomdlg32', '-lole32', '-lshell32', '-luuid'],
    win_subsystem : 'windows',
    install : true)
