#include "Engine/Render/Camera2D.h"

#include <algorithm>

namespace legend::render {

void Camera2D::SetZoom(float zoom) {
    m_zoom = std::clamp(zoom, 0.25f, 4.0f);
}

math::Matrix4x4 Camera2D::GetViewProjection(float viewportWidth, float viewportHeight) const {
    // 投影：屏幕左上角为原点，y 轴向下（视图空间 == 屏幕像素空间）
    const math::Matrix4x4 projection = math::Matrix4x4::Ortho(
        0.0f, viewportWidth, viewportHeight, 0.0f, -1.0f, 1.0f);
    // 视图必须与 WorldToScreen 完全一致：
    //   screen = (world - cameraPos) * zoom + viewportCenter
    // 即：先平移 -cameraPos，再缩放 zoom，最后平移到视口中心
    const math::Matrix4x4 view = math::Matrix4x4::Translation(viewportWidth * 0.5f, viewportHeight * 0.5f) *
                                 math::Matrix4x4::Scale(m_zoom, m_zoom) *
                                 math::Matrix4x4::Translation(-m_position.x, -m_position.y);
    return projection * view;
}

math::Vector2 Camera2D::WorldToScreen(const math::Vector2& worldPosition,
                                      float viewportWidth, float viewportHeight) const {
    const math::Vector2 screenCenter(viewportWidth * 0.5f, viewportHeight * 0.5f);
    return (worldPosition - m_position) * m_zoom + screenCenter;
}

math::Vector2 Camera2D::ScreenToWorld(const math::Vector2& screenPosition,
                                      float viewportWidth, float viewportHeight) const {
    const math::Vector2 screenCenter(viewportWidth * 0.5f, viewportHeight * 0.5f);
    return (screenPosition - screenCenter) / m_zoom + m_position;
}

} // namespace legend::render
