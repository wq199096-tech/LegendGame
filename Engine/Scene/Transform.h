#pragma once

#include "Engine/Math/Vector2.h"
#include "Engine/Scene/Component.h"

namespace legend::scene {

// 位置 / 旋转 / 缩放
class Transform : public Component {
public:
    using Component::Component;

    const math::Vector2& GetPosition() const { return m_position; }
    void SetPosition(const math::Vector2& position) { m_position = position; }
    void Translate(const math::Vector2& delta) { m_position += delta; }

    float GetRotation() const { return m_rotation; }
    void SetRotation(float rotationDegrees) { m_rotation = rotationDegrees; }

    const math::Vector2& GetScale() const { return m_scale; }
    void SetScale(const math::Vector2& scale) { m_scale = scale; }
    void SetScale(float uniformScale) { m_scale = {uniformScale, uniformScale}; }

private:
    math::Vector2 m_position{0.0f, 0.0f};
    float m_rotation = 0.0f; // 角度制
    math::Vector2 m_scale{1.0f, 1.0f};
};

} // namespace legend::scene
