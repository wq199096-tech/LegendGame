#pragma once

#include <string>

#include "Engine/Math/Matrix4x4.h"

namespace legend::render {

// OpenGL 着色器程序封装（顶点 + 片段）
class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool Compile(const std::string& vertexSource, const std::string& fragmentSource);
    void Destroy();

    void Use() const;

    void SetInt(const std::string& name, int value) const;
    void SetFloat(const std::string& name, float value) const;
    void SetVec2(const std::string& name, float x, float y) const;
    void SetVec4(const std::string& name, float x, float y, float z, float w) const;
    void SetMatrix4(const std::string& name, const math::Matrix4x4& matrix) const;

    bool IsValid() const { return m_program != 0; }

private:
    unsigned int m_program = 0;
};

} // namespace legend::render
