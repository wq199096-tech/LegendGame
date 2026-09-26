#pragma once

#include <memory>

#include "Engine/Math/Color.h"
#include "Engine/Scene/Component.h"

namespace legend::render {
class Texture;
}

namespace legend::scene {

// 负责把 GameObject 以纹理精灵的方式渲染出来
class SpriteRenderer : public Component {
public:
    using Component::Component;

    void SetTexture(std::shared_ptr<render::Texture> texture) { m_texture = std::move(texture); }
    const std::shared_ptr<render::Texture>& GetTexture() const { return m_texture; }

    void SetColor(const math::Color& color) { m_color = color; }
    const math::Color& GetColor() const { return m_color; }

    void SetFlipX(bool flip) { m_flipX = flip; }
    bool GetFlipX() const { return m_flipX; }

    void SetFlipY(bool flip) { m_flipY = flip; }
    bool GetFlipY() const { return m_flipY; }

    // 越大越后绘制（越靠上层）
    void SetRenderOrder(int order) { m_renderOrder = order; }
    int GetRenderOrder() const { return m_renderOrder; }

private:
    std::shared_ptr<render::Texture> m_texture;
    math::Color m_color{1.0f, 1.0f, 1.0f, 1.0f};
    bool m_flipX = false;
    bool m_flipY = false;
    int m_renderOrder = 0;
};

} // namespace legend::scene
