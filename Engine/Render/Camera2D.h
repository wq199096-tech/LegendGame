#pragma once

#include "Engine/Math/Matrix4x4.h"
#include "Engine/Math/Vector2.h"

namespace legend::render {

// 2D 正交摄像机：位置为画面中心的世界坐标，y 轴向下，支持缩放与坐标转换
class Camera2D {
public:
    void SetPosition(const math::Vector2& position) { m_position = position; }
    const math::Vector2& GetPosition() const { return m_position; }
    void Move(const math::Vector2& delta) { m_position += delta; }

    void SetZoom(float zoom);
    float GetZoom() const { return m_zoom; }

    math::Matrix4x4 GetViewProjection(float viewportWidth, float viewportHeight) const;

    math::Vector2 WorldToScreen(const math::Vector2& worldPosition,
                                float viewportWidth, float viewportHeight) const;
    math::Vector2 ScreenToWorld(const math::Vector2& screenPosition,
                                float viewportWidth, float viewportHeight) const;

private:
    math::Vector2 m_position{0.0f, 0.0f};
    float m_zoom = 1.0f;
};

} // namespace legend::render
