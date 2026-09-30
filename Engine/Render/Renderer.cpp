#include "Engine/Render/Renderer.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/GLApi.h"
#include "Engine/Render/Texture.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace legend::render {

namespace {

const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

uniform mat4 uMVP;

out vec2 vUV;
out vec4 vColor;

void main()
{
    vUV = aUV;
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
})";

const char* kFragmentShaderSource = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTexture;

out vec4 fragColor;

void main()
{
    vec4 texColor = texture(uTexture, vUV);
    fragColor = texColor * vColor;
})";

constexpr int kQuadVertexStride = 8; // pos(2) + uv(2) + color(4)
constexpr int kQuadVertexCount = 6;

// 写 24 位 BMP（自底向上行序，与 glReadPixels 的行序一致，无需翻转）
bool WriteBmpFile(const std::string& path, int width, int height,
                  const std::vector<unsigned char>& rgbaPixels) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    const int rowBytes = width * 3;
    const int rowPadded = (rowBytes + 3) & ~3;
    const unsigned int pixelDataSize = static_cast<unsigned int>(rowPadded) * static_cast<unsigned int>(height);
    const unsigned int fileSize = 54u + pixelDataSize;

    unsigned char header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    auto writeU32 = [&header](int offset, unsigned int value) {
        header[offset + 0] = static_cast<unsigned char>(value & 0xFFu);
        header[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
        header[offset + 2] = static_cast<unsigned char>((value >> 16) & 0xFFu);
        header[offset + 3] = static_cast<unsigned char>((value >> 24) & 0xFFu);
    };
    writeU32(2, fileSize);                    // 文件大小
    writeU32(10, 54);                         // 像素数据偏移
    writeU32(14, 40);                         // BITMAPINFOHEADER 大小
    writeU32(18, static_cast<unsigned int>(width));
    writeU32(22, static_cast<unsigned int>(height));
    header[26] = 1;                           // planes
    header[28] = 24;                          // bpp
    writeU32(30, 0);                          // 无压缩
    writeU32(34, pixelDataSize);
    writeU32(38, 2835);                       // 水平分辨率 ppm
    writeU32(42, 2835);                       // 垂直分辨率 ppm

    file.write(reinterpret_cast<const char*>(header), sizeof(header));

    std::vector<unsigned char> row(static_cast<size_t>(rowPadded), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t src = (static_cast<size_t>(y) * width + x) * 4;
            row[static_cast<size_t>(x) * 3 + 0] = rgbaPixels[src + 2]; // B
            row[static_cast<size_t>(x) * 3 + 1] = rgbaPixels[src + 1]; // G
            row[static_cast<size_t>(x) * 3 + 2] = rgbaPixels[src + 0]; // R
        }
        file.write(reinterpret_cast<const char*>(row.data()), rowPadded);
    }
    return file.good();
}

} // namespace

bool Renderer::Initialize(SDL_Window* window) {
    if (window == nullptr) {
        LOG_ERROR("Renderer::Initialize: window handle is null.");
        return false;
    }
    m_window = window;

    m_glContext = SDL_GL_CreateContext(window);
    if (m_glContext == nullptr) {
        LOG_ERROR(std::string("OpenGL context creation failed: ") + SDL_GetError());
        m_window = nullptr;
        return false;
    }

    // 软件渲染冒烟模式（LEGEND_CLIENT_VISUAL_SMOKE=1）禁用 vsync：
    // llvmpipe 上 Swap 可能长时间阻塞，导致主循环饿死（Update 无法推进）。
    const bool visualSmoke = SDL_getenv("LEGEND_CLIENT_VISUAL_SMOKE") != nullptr;
    if (visualSmoke) {
        SDL_GL_SetSwapInterval(0);
        LOG_INFO("[VisualSmoke] vsync disabled (software rendering).");
    } else if (!SDL_GL_SetSwapInterval(1)) {
        LOG_WARN(std::string("Failed to enable vsync: ") + SDL_GetError());
    }

    if (!gl::LoadGLFunctions()) {
        LOG_ERROR("Failed to load OpenGL entry points.");
        DestroyGraphicsResources();
        DestroyContext();
        return false;
    }
    // 阶段24 指令四十七：Client Smoke 里程碑标记（窗口已创建 + GL 上下文就绪）。
    LOG_INFO("[VisualSmoke] gl-context-ready");

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* gpuName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    LOG_INFO(std::string("OpenGL version: ") + (version ? version : "unknown"));
    LOG_INFO(std::string("GPU: ") + (gpuName ? gpuName : "unknown"));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    if (!CreateGraphicsResources()) {
        LOG_ERROR("Renderer graphics resources creation failed.");
        DestroyGraphicsResources();
        DestroyContext();
        return false;
    }
    // 阶段24 指令四十七：Client Smoke 里程碑标记（Sprite Shader 编译成功）。
    LOG_INFO("[VisualSmoke] shader-compiled");

    LOG_INFO("Renderer initialized.");
    return true;
}

void Renderer::Shutdown() {
    DestroyGraphicsResources();
    DestroyContext();
    LOG_INFO("Renderer shutdown.");
}

void Renderer::DestroyContext() {
    if (m_glContext != nullptr) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_glContext));
        m_glContext = nullptr;
    }
    m_window = nullptr;
}

bool Renderer::CreateGraphicsResources() {
    if (!m_spriteShader.Compile(kVertexShaderSource, kFragmentShaderSource)) {
        LOG_ERROR("Sprite shader compile failed.");
        return false;
    }

    gl::glGenVertexArrays(1, &m_vao);
    gl::glGenBuffers(1, &m_vbo);
    if (m_vao == 0 || m_vbo == 0) {
        LOG_ERROR("Failed to create VAO/VBO.");
        return false;
    }

    gl::glBindVertexArray(m_vao);
    gl::glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl::glBufferData(GL_ARRAY_BUFFER, sizeof(m_quadVertices), nullptr, GL_DYNAMIC_DRAW);

    const GLsizei stride = kQuadVertexStride * sizeof(float);
    gl::glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(8));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));
    gl::glEnableVertexAttribArray(2);

    gl::glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl::glBindVertexArray(0);
    return true;
}

void Renderer::DestroyGraphicsResources() {
    if (m_vbo != 0) {
        gl::glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        gl::glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_spriteShader.Destroy();
}

void Renderer::BeginFrame(const Camera2D& camera) {
    // 使用 framebuffer 像素尺寸（HiDPI 下与逻辑窗口尺寸不同）
    int width = 1;
    int height = 1;
    if (m_window != nullptr) {
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
    }
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glViewport(0, 0, width, height);
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT);

    m_spriteShader.Use();
    m_spriteShader.SetMatrix4("uMVP", camera.GetViewProjection(
        static_cast<float>(width), static_cast<float>(height)));
    m_spriteShader.SetInt("uTexture", 0);
    gl::glActiveTexture(GL_TEXTURE0);

    gl::glBindVertexArray(m_vao);
    gl::glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
}

void Renderer::EndFrame() {
    if (m_window != nullptr) {
        SDL_GL_SwapWindow(m_window);
    }
}

void Renderer::QueryViewportSize(int& outWidth, int& outHeight) const {
    int width = 1;
    int height = 1;
    if (m_window != nullptr) {
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
    }
    outWidth = width < 1 ? 1 : width;
    outHeight = height < 1 ? 1 : height;
}

void Renderer::DrawSprite(const Texture& texture, const math::Vector2& centerPosition,
                          const SpriteDrawParams& params) {
    if (!texture.IsValid()) {
        return;
    }

    // 显式绑定自身 VAO/VBO：可与 SpriteBatch 混用而互不干扰
    gl::glBindVertexArray(m_vao);
    gl::glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    const float halfWidth = texture.GetWidth() * params.scale.x * 0.5f;
    const float halfHeight = texture.GetHeight() * params.scale.y * 0.5f;
    const float radians = params.rotationDegrees * 3.14159265358979f / 180.0f;
    const float cosR = std::cos(radians);
    const float sinR = std::sin(radians);

    // UV：v=0 是图片顶部（与 y 向下的世界坐标一致）
    const float u0 = params.flipX ? 1.0f : 0.0f;
    const float u1 = params.flipX ? 0.0f : 1.0f;
    const float v0 = params.flipY ? 1.0f : 0.0f;
    const float v1 = params.flipY ? 0.0f : 1.0f;

    const float corners[4][4] = {
        {-halfWidth, -halfHeight, u0, v0},
        { halfWidth, -halfHeight, u1, v0},
        { halfWidth,  halfHeight, u1, v1},
        {-halfWidth,  halfHeight, u0, v1},
    };
    const int indices[kQuadVertexCount] = {0, 1, 2, 0, 2, 3};

    for (int i = 0; i < kQuadVertexCount; ++i) {
        const int idx = indices[i];
        const float localX = corners[idx][0];
        const float localY = corners[idx][1];
        const float rotatedX = localX * cosR - localY * sinR;
        const float rotatedY = localX * sinR + localY * cosR;

        float* vertex = &m_quadVertices[i * kQuadVertexStride];
        vertex[0] = centerPosition.x + rotatedX;
        vertex[1] = centerPosition.y + rotatedY;
        vertex[2] = corners[idx][2];
        vertex[3] = corners[idx][3];
        vertex[4] = params.tint.r;
        vertex[5] = params.tint.g;
        vertex[6] = params.tint.b;
        vertex[7] = params.tint.a;
    }

    glBindTexture(GL_TEXTURE_2D, texture.GetHandle());
    gl::glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(m_quadVertices), m_quadVertices);
    glDrawArrays(GL_TRIANGLES, 0, kQuadVertexCount);
}

bool Renderer::CaptureScreenshot(const std::string& filePath) {
    if (m_window == nullptr) {
        return false;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        LOG_ERROR("CaptureScreenshot: glReadPixels failed.");
        return false;
    }
    return WriteBmpFile(filePath, width, height, pixels);
}

} // namespace legend::render
