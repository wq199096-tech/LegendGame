#pragma once

#include <memory>
#include <vector>

#include "Engine/Render/Texture.h"

namespace legend::animation {

// SpriteSheet：单张纹理中的规则帧网格（cols x rows）
class SpriteSheet {
public:
    bool Initialize(std::shared_ptr<render::Texture> texture, int frameWidth, int frameHeight);

    int GetColumns() const { return m_columns; }
    int GetRows() const { return m_rows; }
    int GetFrameCount() const { return m_columns * m_rows; }
    int GetFrameWidth() const { return m_frameWidth; }
    int GetFrameHeight() const { return m_frameHeight; }

    // 帧序号（行优先） -> UV Rect
    void GetFrameUV(int frameIndex, float& u0, float& v0, float& u1, float& v1) const;

    const render::Texture& GetTexture() const { return *m_texture; }
    bool IsValid() const { return m_texture && m_texture->IsValid(); }

private:
    std::shared_ptr<render::Texture> m_texture;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    int m_columns = 0;
    int m_rows = 0;
};

} // namespace legend::animation
