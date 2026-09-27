#include "Engine/Animation/SpriteSheet.h"

#include "Engine/Debug/Logger.h"

namespace legend::animation {

bool SpriteSheet::Initialize(std::shared_ptr<render::Texture> texture, int frameWidth,
                             int frameHeight) {
    if (!texture || !texture->IsValid() || frameWidth <= 0 || frameHeight <= 0) {
        LOG_ERROR("SpriteSheet: invalid texture or frame size.");
        return false;
    }
    const int texWidth = texture->GetWidth();
    const int texHeight = texture->GetHeight();
    if (texWidth % frameWidth != 0 || texHeight % frameHeight != 0) {
        LOG_ERROR("SpriteSheet: texture size " + std::to_string(texWidth) + "x" +
                  std::to_string(texHeight) + " is not divisible by frame size " +
                  std::to_string(frameWidth) + "x" + std::to_string(frameHeight) + ".");
        return false;
    }
    m_texture = std::move(texture);
    m_frameWidth = frameWidth;
    m_frameHeight = frameHeight;
    m_columns = texWidth / frameWidth;
    m_rows = texHeight / frameHeight;

    LOG_INFO("SpriteSheet initialized: " + std::to_string(m_columns) + "x" +
             std::to_string(m_rows) + " frames (" + std::to_string(GetFrameCount()) + " total).");
    return true;
}

void SpriteSheet::GetFrameUV(int frameIndex, float& u0, float& v0, float& u1, float& v1) const {
    // 越界安全：非法帧索引使用 Frame 0 兜底，禁止越界 UV
    if (frameIndex < 0 || frameIndex >= GetFrameCount()) {
        LOG_WARN("SpriteSheet: frame index " + std::to_string(frameIndex) +
                 " out of range [0," + std::to_string(GetFrameCount() - 1) + "], using frame 0.");
        frameIndex = 0;
    }
    const int column = frameIndex % m_columns;
    const int row = frameIndex / m_columns;
    const float texW = static_cast<float>(m_texture->GetWidth());
    const float texH = static_cast<float>(m_texture->GetHeight());
    u0 = static_cast<float>(column * m_frameWidth) / texW;
    v0 = static_cast<float>(row * m_frameHeight) / texH;
    u1 = static_cast<float>((column + 1) * m_frameWidth) / texW;
    v1 = static_cast<float>((row + 1) * m_frameHeight) / texH;
}

} // namespace legend::animation
