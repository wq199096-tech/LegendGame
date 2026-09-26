#include "Engine/Math/Matrix4x4.h"

namespace legend::math {

Matrix4x4 Matrix4x4::Ortho(float left, float right, float bottom, float top,
                           float nearZ, float farZ) {
    Matrix4x4 result;
    for (int i = 0; i < 16; ++i) result.m[i] = 0.0f;

    result.m[0] = 2.0f / (right - left);
    result.m[5] = 2.0f / (top - bottom);
    result.m[10] = -2.0f / (farZ - nearZ);
    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = -(farZ + nearZ) / (farZ - nearZ);
    result.m[15] = 1.0f;
    return result;
}

Matrix4x4 Matrix4x4::Translation(float x, float y) {
    Matrix4x4 result;
    result.m[12] = x;
    result.m[13] = y;
    return result;
}

Matrix4x4 Matrix4x4::Scale(float scaleX, float scaleY) {
    Matrix4x4 result;
    result.m[0] = scaleX;
    result.m[5] = scaleY;
    return result;
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& rhs) const {
    Matrix4x4 result;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m[k * 4 + row] * rhs.m[col * 4 + k];
            }
            result.m[col * 4 + row] = sum;
        }
    }
    return result;
}

} // namespace legend::math
