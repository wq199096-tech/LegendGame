#pragma once

#include <cstddef>
#include <vector>

#include "Engine/Math/Color.h"
#include "Engine/Math/Vector2.h"

struct SDL_GLContextState;
namespace legend::render {
class Shader;
class Texture;
class Camera2D;
}

namespace legend::render {

// 基础批渲染：同一纹理连续提交、一次 DrawCall。
// 顶点布局与 Renderer 精灵一致：pos(2) + uv(2) + color(4)， stride 32 字节。
class SpriteBatch {
public:
    bool Initialize(size_t maxQuads = 32768);
    void Shutdown();

    // 每帧开始：设置相机矩阵并重置批状态
    void Begin(Shader& shader, const Camera2D& camera, float viewportWidth, float viewportHeight);

    // 提交一个四边形（6 顶点 × 8 float = 48 float，三角顺序 0,1,2,0,2,3）
    void PushQuad(const Texture& texture, const float* quad48);
    // 批量提交 N 个四边形（每个 48 float）
    void PushVertices(const Texture& texture, const float* vertices, size_t quadCount);

    // 便捷接口：中心点/缩放/旋转/翻转/UV（为 Tileset Atlas 预留）
    void DrawQuad(const Texture& texture, const math::Vector2& centerPosition,
                  const math::Vector2& scale, float rotationDegrees, const math::Color& tint,
                  bool flipX = false, bool flipY = false,
                  float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);

    // 把当前缓冲提交到 GPU（纹理切换或帧结束时调用）
    void Flush();
    void End() { Flush(); }

    int GetDrawCallCount() const { return m_drawCalls; }
    int GetQuadCount() const { return m_quads; }

private:
    void EnsureCapacity(size_t quadCount);
    void GrowCapacity(size_t newQuadCapacity);

    Shader* m_shader = nullptr;
    bool m_active = false;

    const Texture* m_currentTexture = nullptr;
    std::vector<float> m_vertices;
    size_t m_quadCapacity = 0;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;

    int m_drawCalls = 0;
    int m_quads = 0;
};

} // namespace legend::render
