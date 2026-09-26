#pragma once

// 精简 OpenGL 函数加载器：
// - GL 1.1 及以下函数直接链接 opengl32.lib（Windows 的 wglGetProcAddress 不提供这些函数）
// - GL 1.2+ 函数在运行时通过 SDL_GL_GetProcAddress 解析
// 必须在拥有当前 OpenGL 上下文后调用 LoadGLFunctions()。

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif
#include <GL/gl.h>

// Windows 的 GL/gl.h 只覆盖 GL 1.1，缺少 GLchar（GL 2.0 才引入）
using GLchar = char;

namespace legend::gl {

using GLsizeiptr = long long;
using GLintptr = long long;

// ---- GL 1.2+ 入口（运行时解析） ----
extern void (*glActiveTexture)(GLenum texture);

extern GLuint (*glCreateShader)(GLenum shaderType);
extern void (*glShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
extern void (*glCompileShader)(GLuint shader);
extern void (*glGetShaderiv)(GLuint shader, GLenum pname, GLint* params);
extern void (*glGetShaderInfoLog)(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
extern void (*glDeleteShader)(GLuint shader);

extern GLuint (*glCreateProgram)(void);
extern void (*glAttachShader)(GLuint program, GLuint shader);
extern void (*glLinkProgram)(GLuint program);
extern void (*glGetProgramiv)(GLuint program, GLenum pname, GLint* params);
extern void (*glGetProgramInfoLog)(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
extern void (*glDeleteProgram)(GLuint program);
extern void (*glUseProgram)(GLuint program);

extern GLint (*glGetUniformLocation)(GLuint program, const GLchar* name);
extern void (*glUniform1i)(GLint location, GLint v0);
extern void (*glUniform1f)(GLint location, GLfloat v0);
extern void (*glUniform2f)(GLint location, GLfloat v0, GLfloat v1);
extern void (*glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
extern void (*glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

extern void (*glGenBuffers)(GLsizei n, GLuint* buffers);
extern void (*glBindBuffer)(GLenum target, GLuint buffer);
extern void (*glDeleteBuffers)(GLsizei n, const GLuint* buffers);
extern void (*glBufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
extern void (*glBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
extern void (*glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
extern void (*glEnableVertexAttribArray)(GLuint index);

extern void (*glGenVertexArrays)(GLsizei n, GLuint* arrays);
extern void (*glBindVertexArray)(GLuint array);
extern void (*glDeleteVertexArrays)(GLsizei n, const GLuint* arrays);

// 解析全部入口点；失败时记录错误日志并返回 false
bool LoadGLFunctions();

} // namespace legend::gl

// ---- GL 1.1 头文件中不存在的常量 ----
#ifndef GL_CLAMP_TO_EDGE
    #define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE0
    #define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_ARRAY_BUFFER
    #define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_DYNAMIC_DRAW
    #define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_VERTEX_SHADER
    #define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
    #define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
    #define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
    #define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
    #define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_UNPACK_ROW_LENGTH
    #define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
