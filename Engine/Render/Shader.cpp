#include "Engine/Render/Shader.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Render/GLApi.h"

namespace legend::render {

Shader::~Shader() {
    Destroy();
}

void Shader::Destroy() {
    if (m_program != 0) {
        gl::glDeleteProgram(m_program);
        m_program = 0;
    }
}

bool Shader::Compile(const std::string& vertexSource, const std::string& fragmentSource) {
    Destroy();

    GLuint vertexShader = gl::glCreateShader(GL_VERTEX_SHADER);
    GLuint fragmentShader = gl::glCreateShader(GL_FRAGMENT_SHADER);
    if (vertexShader == 0 || fragmentShader == 0) {
        LOG_ERROR("Shader compile failed: could not create shader objects.");
        return false;
    }

    const GLchar* vertexPtr = vertexSource.c_str();
    const GLchar* fragmentPtr = fragmentSource.c_str();
    const GLint vertexLength = static_cast<GLint>(vertexSource.size());
    const GLint fragmentLength = static_cast<GLint>(fragmentSource.size());

    gl::glShaderSource(vertexShader, 1, &vertexPtr, &vertexLength);
    gl::glCompileShader(vertexShader);

    GLint compiled = 0;
    gl::glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[2048] = {};
        gl::glGetShaderInfoLog(vertexShader, sizeof(log), nullptr, log);
        LOG_ERROR(std::string("Vertex shader compile failed: ") + log);
        gl::glDeleteShader(vertexShader);
        gl::glDeleteShader(fragmentShader);
        return false;
    }

    gl::glShaderSource(fragmentShader, 1, &fragmentPtr, &fragmentLength);
    gl::glCompileShader(fragmentShader);

    compiled = 0;
    gl::glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[2048] = {};
        gl::glGetShaderInfoLog(fragmentShader, sizeof(log), nullptr, log);
        LOG_ERROR(std::string("Fragment shader compile failed: ") + log);
        gl::glDeleteShader(vertexShader);
        gl::glDeleteShader(fragmentShader);
        return false;
    }

    GLuint program = gl::glCreateProgram();
    gl::glAttachShader(program, vertexShader);
    gl::glAttachShader(program, fragmentShader);
    gl::glLinkProgram(program);
    gl::glDeleteShader(vertexShader);
    gl::glDeleteShader(fragmentShader);

    GLint linked = 0;
    gl::glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048] = {};
        gl::glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        LOG_ERROR(std::string("Shader program link failed: ") + log);
        gl::glDeleteProgram(program);
        return false;
    }

    m_program = program;
    return true;
}

void Shader::Use() const {
    gl::glUseProgram(m_program);
}

void Shader::SetInt(const std::string& name, int value) const {
    gl::glUniform1i(gl::glGetUniformLocation(m_program, name.c_str()), value);
}

void Shader::SetFloat(const std::string& name, float value) const {
    gl::glUniform1f(gl::glGetUniformLocation(m_program, name.c_str()), value);
}

void Shader::SetVec2(const std::string& name, float x, float y) const {
    gl::glUniform2f(gl::glGetUniformLocation(m_program, name.c_str()), x, y);
}

void Shader::SetVec4(const std::string& name, float x, float y, float z, float w) const {
    gl::glUniform4f(gl::glGetUniformLocation(m_program, name.c_str()), x, y, z, w);
}

void Shader::SetMatrix4(const std::string& name, const math::Matrix4x4& matrix) const {
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(m_program, name.c_str()),
                           1, GL_FALSE, matrix.Data());
}

} // namespace legend::render
