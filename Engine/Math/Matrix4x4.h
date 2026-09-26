#pragma once

namespace legend::math {

// 4x4 列主序矩阵（与 OpenGL 约定一致）
class Matrix4x4 {
public:
    // 默认为单位矩阵
    float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    Matrix4x4() = default;

    static Matrix4x4 Ortho(float left, float right, float bottom, float top,
                           float nearZ, float farZ);
    static Matrix4x4 Translation(float x, float y);
    static Matrix4x4 Scale(float scaleX, float scaleY);

    Matrix4x4 operator*(const Matrix4x4& rhs) const;

    const float* Data() const { return m; }
};

} // namespace legend::math
