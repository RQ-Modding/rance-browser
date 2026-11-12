# rq-browser.exe: the file browser for mod creators (phase P1, loader 2.43.0). It reads the
# player's own game folder: the CGs, the Flash files, the music, the sounds, the 3D textures and the
# names of rq.ids. Dear ImGui through cimgui draws the window (third_party/cimgui, MIT).
add_languages('cpp', native : false)

cimgui_inc = include_directories('../../third_party/cimgui', '../../third_party/cimgui/imgui',
                                 '../../third_party/cimgui/imgui/backends')
cimgui_lib = static_library('cimgui',
    ['../../third_party/cimgui/cimgui.cpp',
     '../../third_party/cimgui/imgui/imgui.cpp',
     '../../third_party/cimgui/imgui/imgui_demo.cpp',
     '../../third_party/cimgui/imgui/imgui_draw.cpp',
     '../../third_party/cimgui/imgui/imgui_tables.cpp',
     '../../third_party/cimgui/imgui/imgui_widgets.cpp',
     'cimgui_glue.cpp'],
    cpp_args : ['-DCIMGUI_NO_EXPORT', '-DCIMGUI_USE_SDL2', '-DCIMGUI_USE_OPENGL3',
                '-DIMGUI_DISABLE_DEMO_WINDOWS', '-w'],
    include_directories : cimgui_inc,
    dependencies : [sdl2])

rqb_py = find_program('python3', 'python', required : true)
rqb_version = run_command(rqb_py, '-c',
    'import re,sys; print(re.search(r"#define RQ_VERSION \"([^\"]+)\"", open(sys.argv[1], encoding="utf-8").read()).group(1))',
    files('../../include/modloader.h'), check : true).stdout().strip()

rqb_ids = custom_target('rqb_ids_json',
    input : '../../data/ids/ids.json',
    output : 'rqb_ids_json.c',
    command : [rqb_py, meson.project_source_root() / 'src/gen_lua_embed.py', '@INPUT@', '@OUTPUT@', 'rqb_ids_json'])
rqb_pictures = custom_target('rqb_pictures_tsv',
    input : '../../data/ids/pictures.tsv',
    output : 'rqb_pictures_tsv.c',
    command : [rqb_py, meson.project_source_root() / 'src/gen_lua_embed.py', '@INPUT@', '@OUTPUT@', 'rqb_pictures_tsv'])
rqb_lua_lines = custom_target('rqb_lua_lines',
    input : ['lua_lines.h', '../../dist/lua/rq.lua'],
    output : 'rqb_lua_lines.c',
    command : [rqb_py, files('check_lua_lines.py'), '@INPUT0@', '@INPUT1@', '@OUTPUT@'])

executable('rq-browser',
    ['main.c', 'game.c', 'pics.c', 'player.c', 'ui.c', 'win.c', 'check.c',
     'tab_pictures.c', 'tab_flash.c', 'tab_audio.c', 'tab_names.c',
     '../../src/swf.c', '../../src/cJSON.c',
     rqb_ids, rqb_pictures, rqb_lua_lines],
    c_args : ['-Wno-unused-parameter', '-DRQB_VERSION="' + rqb_version + '"', '-DCIMGUI_NO_EXPORT'],
    include_directories : [incdir, cimgui_inc],
    dependencies : [sdl2, sndfile, zlib, libm, gl, libsys4_dep],
    link_with : cimgui_lib,
    link_args : ['-static', '-lstdc++', '-lcomdlg32', '-lole32', '-lshell32', '-luuid'],
    win_subsystem : 'windows',
    install : true)
