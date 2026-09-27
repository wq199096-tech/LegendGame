#include "Engine/Render/CharacterRenderer.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Render/SpriteBatch.h"

namespace legend::render {

math::Vector2 CharacterRenderer::GetSpriteDrawCenter(const entity::Character& character) {
    const auto& visual = character.GetVisual();
    const math::Vector2& feet = character.GetPosition();
    return feet + math::Vector2(visual.width * (0.5f - visual.pivot.x),
                                visual.height * (0.5f - visual.pivot.y));
}

math::Vector2 CharacterRenderer::GetSpriteScale(const entity::Character& character) {
    const auto& sheet = character.GetSpriteSheet();
    if (!sheet || !sheet->IsValid()) {
        return {0.0f, 0.0f};
    }
    const auto& visual = character.GetVisual();
    const float texW = static_cast<float>(sheet->GetTexture().GetWidth());
    const float texH = static_cast<float>(sheet->GetTexture().GetHeight());
    return {visual.width / texW, visual.height / texH};
}

void CharacterRenderer::Draw(render::SpriteBatch& batch, const entity::Character& character) const {
    const auto& sheet = character.GetSpriteSheet();
    if (!sheet || !sheet->IsValid()) {
        return;
    }

    const int frameIndex = character.GetAnimationPlayer().GetCurrentFrameIndex();
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    sheet->GetFrameUV(frameIndex, u0, v0, u1, v1);

    const math::Vector2 drawCenter = GetSpriteDrawCenter(character);
    const math::Vector2 scale = GetSpriteScale(character);

    batch.DrawQuad(sheet->GetTexture(), drawCenter, scale, 0.0f,
                   math::Color(1.0f, 1.0f, 1.0f, 1.0f), false, false, u0, v0, u1, v1);
}

} // namespace legend::render
