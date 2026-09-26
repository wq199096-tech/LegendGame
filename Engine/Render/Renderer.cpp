#include "Engine/Render/Renderer.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/GLApi.h"
#include "Engine/Render/Texture.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <string>

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
        return false;
    }

    if (!SDL_GL_SetSwapInterval(1)) {
        LOG_WARN(std::string("Failed to enable vsync: ") + SDL_GetError());
    }

    if (!gl::LoadGLFunctions()) {
        LOG_ERROR("Failed to load OpenGL entry points.");
        return false;
    }

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* gpuName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    LOG_INFO(std::string("OpenGL version: ") + (version ? version : "unknown"));
    LOG_INFO(std::string("GPU: ") + (gpuName ? gpuName : "unknown"));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    if (!CreateGraphicsResources()) {
        LOG_ERROR("Renderer graphics resources creation failed.");
        return false;
    }

    LOG_INFO("Renderer initialized.");
    return true;
}

void Renderer::Shutdown() {
    DestroyGraphicsResources();
    if (m_glContext != nullptr) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_glContext));
        m_glContext = nullptr;
    }
    m_window = nullptr;
    LOG_INFO("Renderer shutdown.");
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
    int width = 1;
    int height = 1;
    if (m_window != nullptr) {
        SDL_GetWindowSize(m_window, &width, &height);
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

void Renderer::DrawSprite(const Texture& texture, const math::Vector2& centerPosition,
                          const SpriteDrawParams& params) {
    if (!texture.IsValid()) {
        return;
    }

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

} // namespace legend::render
