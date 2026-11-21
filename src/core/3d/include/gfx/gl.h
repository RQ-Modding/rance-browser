/* In the place of RQ MOD's include/gfx/gl.h, which takes OpenGL from GLEW: the GL 1.1 calls come
 * from opengl32 through SDL's headers, and each later call the 3D renderer makes is a pointer that
 * rb3d_gl_load() (rb3d.c) fills once through SDL_GL_GetProcAddress, so the browser needs no GLEW.
 * The header guard is the engine's, so the copied files take this one. */

#ifndef SYSTEM4_GFX_GL_H
#define SYSTEM4_GFX_GL_H

/* As GLEW does: with APIENTRY known, SDL's header does not take in windows.h, whose names
 * (Rectangle, near, far) the renderer uses for its own. */
#if defined(_WIN32) && !defined(APIENTRY)
#define APIENTRY __stdcall
#endif
#include <SDL_opengl.h>

/* Each call past GL 1.1: its pointer type and its name. */
#define RB3D_GL_CALLS(X) \
	X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
	X(PFNGLATTACHSHADERPROC, glAttachShader) \
	X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
	X(PFNGLBINDBUFFERPROC, glBindBuffer) \
	X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase) \
	X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
	X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
	X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
	X(PFNGLBLENDEQUATIONPROC, glBlendEquation) \
	X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
	X(PFNGLBUFFERDATAPROC, glBufferData) \
	X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) \
	X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
	X(PFNGLCOMPILESHADERPROC, glCompileShader) \
	X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
	X(PFNGLCREATESHADERPROC, glCreateShader) \
	X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
	X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
	X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
	X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
	X(PFNGLDELETESHADERPROC, glDeleteShader) \
	X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
	X(PFNGLDETACHSHADERPROC, glDetachShader) \
	X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
	X(PFNGLDRAWBUFFERSPROC, glDrawBuffers) \
	X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
	X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
	X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
	X(PFNGLGENBUFFERSPROC, glGenBuffers) \
	X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap) \
	X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
	X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) \
	X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
	X(PFNGLGETBUFFERSUBDATAPROC, glGetBufferSubData) \
	X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
	X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
	X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
	X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
	X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex) \
	X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
	X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
	X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) \
	X(PFNGLSHADERSOURCEPROC, glShaderSource) \
	X(PFNGLUNIFORM1FPROC, glUniform1f) \
	X(PFNGLUNIFORM1IPROC, glUniform1i) \
	X(PFNGLUNIFORM2FPROC, glUniform2f) \
	X(PFNGLUNIFORM2FVPROC, glUniform2fv) \
	X(PFNGLUNIFORM3FPROC, glUniform3f) \
	X(PFNGLUNIFORM3FVPROC, glUniform3fv) \
	X(PFNGLUNIFORM4FPROC, glUniform4f) \
	X(PFNGLUNIFORM4FVPROC, glUniform4fv) \
	X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding) \
	X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv) \
	X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv) \
	X(PFNGLUSEPROGRAMPROC, glUseProgram) \
	X(PFNGLVERTEXATTRIB1FPROC, glVertexAttrib1f) \
	X(PFNGLVERTEXATTRIB2FPROC, glVertexAttrib2f) \
	X(PFNGLVERTEXATTRIB3FPROC, glVertexAttrib3f) \
	X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f) \
	X(PFNGLVERTEXATTRIBI4IPROC, glVertexAttribI4i) \
	X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer) \
	X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

#define RB3D_GL_EXTERN(type, name) extern type rb3d_##name;
RB3D_GL_CALLS(RB3D_GL_EXTERN)
#undef RB3D_GL_EXTERN

#define glActiveTexture rb3d_glActiveTexture
#define glAttachShader rb3d_glAttachShader
#define glBindAttribLocation rb3d_glBindAttribLocation
#define glBindBuffer rb3d_glBindBuffer
#define glBindBufferBase rb3d_glBindBufferBase
#define glBindFramebuffer rb3d_glBindFramebuffer
#define glBindRenderbuffer rb3d_glBindRenderbuffer
#define glBindVertexArray rb3d_glBindVertexArray
#define glBlendEquation rb3d_glBlendEquation
#define glBlendFuncSeparate rb3d_glBlendFuncSeparate
#define glBufferData rb3d_glBufferData
#define glBufferSubData rb3d_glBufferSubData
#define glCheckFramebufferStatus rb3d_glCheckFramebufferStatus
#define glCompileShader rb3d_glCompileShader
#define glCreateProgram rb3d_glCreateProgram
#define glCreateShader rb3d_glCreateShader
#define glDeleteBuffers rb3d_glDeleteBuffers
#define glDeleteFramebuffers rb3d_glDeleteFramebuffers
#define glDeleteProgram rb3d_glDeleteProgram
#define glDeleteRenderbuffers rb3d_glDeleteRenderbuffers
#define glDeleteShader rb3d_glDeleteShader
#define glDeleteVertexArrays rb3d_glDeleteVertexArrays
#define glDetachShader rb3d_glDetachShader
#define glDisableVertexAttribArray rb3d_glDisableVertexAttribArray
#define glDrawBuffers rb3d_glDrawBuffers
#define glEnableVertexAttribArray rb3d_glEnableVertexAttribArray
#define glFramebufferRenderbuffer rb3d_glFramebufferRenderbuffer
#define glFramebufferTexture2D rb3d_glFramebufferTexture2D
#define glGenBuffers rb3d_glGenBuffers
#define glGenerateMipmap rb3d_glGenerateMipmap
#define glGenFramebuffers rb3d_glGenFramebuffers
#define glGenRenderbuffers rb3d_glGenRenderbuffers
#define glGenVertexArrays rb3d_glGenVertexArrays
#define glGetBufferSubData rb3d_glGetBufferSubData
#define glGetProgramInfoLog rb3d_glGetProgramInfoLog
#define glGetProgramiv rb3d_glGetProgramiv
#define glGetShaderInfoLog rb3d_glGetShaderInfoLog
#define glGetShaderiv rb3d_glGetShaderiv
#define glGetUniformBlockIndex rb3d_glGetUniformBlockIndex
#define glGetUniformLocation rb3d_glGetUniformLocation
#define glLinkProgram rb3d_glLinkProgram
#define glRenderbufferStorage rb3d_glRenderbufferStorage
#define glShaderSource rb3d_glShaderSource
#define glUniform1f rb3d_glUniform1f
#define glUniform1i rb3d_glUniform1i
#define glUniform2f rb3d_glUniform2f
#define glUniform2fv rb3d_glUniform2fv
#define glUniform3f rb3d_glUniform3f
#define glUniform3fv rb3d_glUniform3fv
#define glUniform4f rb3d_glUniform4f
#define glUniform4fv rb3d_glUniform4fv
#define glUniformBlockBinding rb3d_glUniformBlockBinding
#define glUniformMatrix3fv rb3d_glUniformMatrix3fv
#define glUniformMatrix4fv rb3d_glUniformMatrix4fv
#define glUseProgram rb3d_glUseProgram
#define glVertexAttrib1f rb3d_glVertexAttrib1f
#define glVertexAttrib2f rb3d_glVertexAttrib2f
#define glVertexAttrib3f rb3d_glVertexAttrib3f
#define glVertexAttrib4f rb3d_glVertexAttrib4f
#define glVertexAttribI4i rb3d_glVertexAttribI4i
#define glVertexAttribIPointer rb3d_glVertexAttribIPointer
#define glVertexAttribPointer rb3d_glVertexAttribPointer

#endif /* SYSTEM4_GFX_GL_H */
