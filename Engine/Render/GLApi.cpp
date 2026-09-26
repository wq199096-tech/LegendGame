#include "Engine/Render/GLApi.h"

#include "Engine/Debug/Logger.h"

#include <SDL3/SDL.h>
#include <string>

namespace legend::gl {

void (*glActiveTexture)(GLenum texture) = nullptr;

GLuint (*glCreateShader)(GLenum shaderType) = nullptr;
void (*glShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) = nullptr;
void (*glCompileShader)(GLuint shader) = nullptr;
void (*glGetShaderiv)(GLuint shader, GLenum pname, GLint* params) = nullptr;
void (*glGetShaderInfoLog)(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog) = nullptr;
void (*glDeleteShader)(GLuint shader) = nullptr;

GLuint (*glCreateProgram)(void) = nullptr;
void (*glAttachShader)(GLuint program, GLuint shader) = nullptr;
void (*glLinkProgram)(GLuint program) = nullptr;
void (*glGetProgramiv)(GLuint program, GLenum pname, GLint* params) = nullptr;
void (*glGetProgramInfoLog)(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog) = nullptr;
void (*glDeleteProgram)(GLuint program) = nullptr;
void (*glUseProgram)(GLuint program) = nullptr;

GLint (*glGetUniformLocation)(GLuint program, const GLchar* name) = nullptr;
void (*glUniform1i)(GLint location, GLint v0) = nullptr;
void (*glUniform1f)(GLint location, GLfloat v0) = nullptr;
void (*glUniform2f)(GLint location, GLfloat v0, GLfloat v1) = nullptr;
void (*glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) = nullptr;
void (*glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) = nullptr;

void (*glGenBuffers)(GLsizei n, GLuint* buffers) = nullptr;
void (*glBindBuffer)(GLenum target, GLuint buffer) = nullptr;
void (*glDeleteBuffers)(GLsizei n, const GLuint* buffers) = nullptr;
void (*glBufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage) = nullptr;
void (*glBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) = nullptr;
void (*glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) = nullptr;
void (*glEnableVertexAttribArray)(GLuint index) = nullptr;

void (*glGenVertexArrays)(GLsizei n, GLuint* arrays) = nullptr;
void (*glBindVertexArray)(GLuint array) = nullptr;
void (*glDeleteVertexArrays)(GLsizei n, const GLuint* arrays) = nullptr;

namespace {

void* ResolveProc(const char* name, bool& ok) {
    void* proc = reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    if (proc == nullptr) {
        LOG_ERROR(std::string("Failed to resolve OpenGL function: ") + name);
        ok = false;
    }
    return proc;
}

} // namespace

bool LoadGLFunctions() {
    bool ok = true;

    glActiveTexture = reinterpret_cast<void (*)(GLenum)>(ResolveProc("glActiveTexture", ok));

    glCreateShader = reinterpret_cast<GLuint (*)(GLenum)>(ResolveProc("glCreateShader", ok));
    glShaderSource = reinterpret_cast<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>(ResolveProc("glShaderSource", ok));
    glCompileShader = reinterpret_cast<void (*)(GLuint)>(ResolveProc("glCompileShader", ok));
    glGetShaderiv = reinterpret_cast<void (*)(GLuint, GLenum, GLint*)>(ResolveProc("glGetShaderiv", ok));
    glGetShaderInfoLog = reinterpret_cast<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>(ResolveProc("glGetShaderInfoLog", ok));
    glDeleteShader = reinterpret_cast<void (*)(GLuint)>(ResolveProc("glDeleteShader", ok));

    glCreateProgram = reinterpret_cast<GLuint (*)(void)>(ResolveProc("glCreateProgram", ok));
    glAttachShader = reinterpret_cast<void (*)(GLuint, GLuint)>(ResolveProc("glAttachShader", ok));
    glLinkProgram = reinterpret_cast<void (*)(GLuint)>(ResolveProc("glLinkProgram", ok));
    glGetProgramiv = reinterpret_cast<void (*)(GLuint, GLenum, GLint*)>(ResolveProc("glGetProgramiv", ok));
    glGetProgramInfoLog = reinterpret_cast<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>(ResolveProc("glGetProgramInfoLog", ok));
    glDeleteProgram = reinterpret_cast<void (*)(GLuint)>(ResolveProc("glDeleteProgram", ok));
    glUseProgram = reinterpret_cast<void (*)(GLuint)>(ResolveProc("glUseProgram", ok));

    glGetUniformLocation = reinterpret_cast<GLint (*)(GLuint, const GLchar*)>(ResolveProc("glGetUniformLocation", ok));
    glUniform1i = reinterpret_cast<void (*)(GLint, GLint)>(ResolveProc("glUniform1i", ok));
    glUniform1f = reinterpret_cast<void (*)(GLint, GLfloat)>(ResolveProc("glUniform1f", ok));
    glUniform2f = reinterpret_cast<void (*)(GLint, GLfloat, GLfloat)>(ResolveProc("glUniform2f", ok));
    glUniform4f = reinterpret_cast<void (*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat)>(ResolveProc("glUniform4f", ok));
    glUniformMatrix4fv = reinterpret_cast<void (*)(GLint, GLsizei, GLboolean, const GLfloat*)>(ResolveProc("glUniformMatrix4fv", ok));

    glGenBuffers = reinterpret_cast<void (*)(GLsizei, GLuint*)>(ResolveProc("glGenBuffers", ok));
    glBindBuffer = reinterpret_cast<void (*)(GLenum, GLuint)>(ResolveProc("glBindBuffer", ok));
    glDeleteBuffers = reinterpret_cast<void (*)(GLsizei, const GLuint*)>(ResolveProc("glDeleteBuffers", ok));
    glBufferData = reinterpret_cast<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>(ResolveProc("glBufferData", ok));
    glBufferSubData = reinterpret_cast<void (*)(GLenum, GLintptr, GLsizeiptr, const void*)>(ResolveProc("glBufferSubData", ok));
    glVertexAttribPointer = reinterpret_cast<void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)>(ResolveProc("glVertexAttribPointer", ok));
    glEnableVertexAttribArray = reinterpret_cast<void (*)(GLuint)>(ResolveProc("glEnableVertexAttribArray", ok));

    glGenVertexArrays = reinterpret_cast<void (*)(GLsizei, GLuint*)>(ResolveProc("glGenVertexArrays", ok));
    glBindVertexArray = reinterpret_cast<void (*)(GLuint)>(ResolveProc("glBindVertexArray", ok));
    glDeleteVertexArrays = reinterpret_cast<void (*)(GLsizei, const GLuint*)>(ResolveProc("glDeleteVertexArrays", ok));

    return ok;
}

} // namespace legend::gl
