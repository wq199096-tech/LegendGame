#pragma once

#include "Engine/Math/Color.h"
#include "Engine/Math/Vector2.h"
#include "Engine/Render/Shader.h"

struct SDL_Window;
struct SDL_GLContextState;

namespace legend::render {

class Camera2D;
class Texture;

struct SpriteDrawParams {
    math::Vector2 scale{1.0f, 1.0f};
    float rotationDegrees = 0.0f;
    bool flipX = false;
    bool flipY = false;
    math::Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};

// OpenGL 3.3 Core 2D 渲染器
class Renderer {
public:
    bool Initialize(SDL_Window* window);
    void Shutdown();

    // 清屏并设置摄像机矩阵，每帧渲染前调用
    void BeginFrame(const Camera2D& camera);
    // 交换缓冲区
    void EndFrame();

    // 以 centerPosition 为中心绘制一个纹理精灵
    void DrawSprite(const Texture& texture, const math::Vector2& centerPosition,
                    const SpriteDrawParams& params);

    void SetClearColor(const math::Color& color) { m_clearColor = color; }

private:
    bool CreateGraphicsResources();
    void DestroyGraphicsResources();

    SDL_Window* m_window = nullptr;
    SDL_GLContextState* m_glContext = nullptr;

    Shader m_spriteShader;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    math::Color m_clearColor{0.12f, 0.13f, 0.15f, 1.0f};

    // 6 个顶点 * (pos2 + uv2 + color4)
    float m_quadVertices[6 * 8] = {};
};

} // namespace legend::render
