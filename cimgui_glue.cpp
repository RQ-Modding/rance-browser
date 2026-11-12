// ImGui's SDL2 and OpenGL 3 backends with C linkage, so the browser's C code can call them through
// cimgui_impl.h. cimgui's own build passes -DIMGUI_IMPL_API='extern "C"'; one file here does the
// same without quotes on the command line. cimgui_impl.cpp is left out: it only adds Vulkan
// helpers, and in 1.92.9b its header redeclares the OpenGL 3 backend's inline
// ImGui_ImplOpenGL3_GetRenderState, which C++ refuses (the C side never calls it).
#define IMGUI_IMPL_API extern "C"
#include "imgui_impl_sdl2.cpp"
#include "imgui_impl_opengl3.cpp"
