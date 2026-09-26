#pragma once

#include <cmath>

namespace legend::math {

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    Vector2() = default;
    Vector2(float x_, float y_) : x(x_), y(y_) {}

    Vector2 operator-() const { return {-x, -y}; }
    Vector2 operator+(const Vector2& o) const { return {x + o.x, y + o.y}; }
    Vector2 operator-(const Vector2& o) const { return {x - o.x, y - o.y}; }
    Vector2 operator*(float s) const { return {x * s, y * s}; }
    Vector2 operator/(float s) const { return {x / s, y / s}; }

    Vector2& operator+=(const Vector2& o) { x += o.x; y += o.y; return *this; }
    Vector2& operator-=(const Vector2& o) { x -= o.x; y -= o.y; return *this; }
    Vector2& operator*=(float s) { x *= s; y *= s; return *this; }

    float LengthSq() const { return x * x + y * y; }
    float Length() const { return std::sqrt(LengthSq()); }

    // 返回单位向量；零向量安全（返回零向量，不产生 NaN）
    Vector2 Normalized() const {
        const float len = Length();
        if (len <= 1e-8f) {
            return {0.0f, 0.0f};
        }
        return {x / len, y / len};
    }
};

inline Vector2 operator*(float s, const Vector2& v) { return v * s; }

} // namespace legend::math
