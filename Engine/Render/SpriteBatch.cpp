#include "Engine/Render/SpriteBatch.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/GLApi.h"
#include "Engine/Render/Shader.h"
#include "Engine/Render/Texture.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>

namespace legend::render {

// 每个 quad = 6 顶点（三角列表）× 8 float = 48 float = 192 字节
constexpr size_t kFloatsPerQuad = 48;
constexpr size_t kBytesPerQuad = kFloatsPerQuad * sizeof(float);

bool SpriteBatch::Initialize(size_t maxQuads) {
    m_quadCapacity = maxQuads;
    m_vertices.reserve(m_quadCapacity * kFloatsPerQuad);

    gl::glGenVertexArrays(1, &m_vao);
    gl::glGenBuffers(1, &m_vbo);
    if (m_vao == 0 || m_vbo == 0) {
        LOG_ERROR("SpriteBatch: failed to create VAO/VBO.");
        return false;
    }

    gl::glBindVertexArray(m_vao);
    gl::glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl::glBufferData(GL_ARRAY_BUFFER,
                     static_cast<gl::GLsizeiptr>(m_quadCapacity * kBytesPerQuad), nullptr,
                     GL_DYNAMIC_DRAW);

    const GLsizei stride = 8 * sizeof(float);
    gl::glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(8));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));
    gl::glEnableVertexAttribArray(2);

    gl::glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl::glBindVertexArray(0);
    LOG_INFO("SpriteBatch initialized (capacity: " + std::to_string(m_quadCapacity) + " quads).");
    return true;
}

void SpriteBatch::Shutdown() {
    if (m_vbo != 0) {
        gl::glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        gl::glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_vertices.clear();
}

void SpriteBatch::Begin(Shader& shader, const Camera2D& camera, float viewportWidth,
                        float viewportHeight) {
    m_shader = &shader;
    m_active = true;
    m_currentTexture = nullptr;
    m_vertices.clear();
    m_drawCalls = 0;
    m_quads = 0;

    shader.Use();
    shader.SetMatrix4("uMVP", camera.GetViewProjection(viewportWidth, viewportHeight));
    shader.SetInt("uTexture", 0);
    gl::glActiveTexture(GL_TEXTURE0);
}

void SpriteBatch::EnsureCapacity(size_t quadCount) {
    if (m_vertices.size() + quadCount * kFloatsPerQuad > m_quadCapacity * kFloatsPerQuad) {
        Flush();
    }
    if (quadCount > m_quadCapacity) {
        GrowCapacity(quadCount * 2);
    }
}

void SpriteBatch::GrowCapacity(size_t newQuadCapacity) {
    m_quadCapacity = newQuadCapacity;
    m_vertices.reserve(m_quadCapacity * kFloatsPerQuad);
    gl::glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl::glBufferData(GL_ARRAY_BUFFER,
                     static_cast<gl::GLsizeiptr>(m_quadCapacity * kBytesPerQuad), nullptr,
                     GL_DYNAMIC_DRAW);
}

void SpriteBatch::PushQuad(const Texture& texture, const float* quad48) {
    if (!m_active || !texture.IsValid()) {
        return;
    }
    EnsureCapacity(1);
    if (m_currentTexture != &texture) {
        Flush();
        m_currentTexture = &texture;
    }
    m_vertices.insert(m_vertices.end(), quad48, quad48 + kFloatsPerQuad);
    ++m_quads;
}

void SpriteBatch::PushVertices(const Texture& texture, const float* vertices, size_t quadCount) {
    if (!m_active || !texture.IsValid() || quadCount == 0) {
        return;
    }
    EnsureCapacity(quadCount);
    if (m_currentTexture != &texture) {
        Flush();
        m_currentTexture = &texture;
    }
    m_vertices.insert(m_vertices.end(), vertices, vertices + quadCount * kFloatsPerQuad);
    m_quads += static_cast<int>(quadCount);
}

void SpriteBatch::DrawQuad(const Texture& texture, const math::Vector2& centerPosition,
                           const math::Vector2& scale, float rotationDegrees,
                           const math::Color& tint, bool flipX, bool flipY,
                           float u0, float v0, float u1, float v1) {
    if (!texture.IsValid()) {
        return;
    }

    const float halfWidth = texture.GetWidth() * scale.x * 0.5f;
    const float halfHeight = texture.GetHeight() * scale.y * 0.5f;
    const float radians = rotationDegrees * 3.14159265358979f / 180.0f;
    const float cosR = std::cos(radians);
    const float sinR = std::sin(radians);

    const float cu0 = flipX ? u1 : u0;
    const float cu1 = flipX ? u0 : u1;
    const float cv0 = flipY ? v1 : v0;
    const float cv1 = flipY ? v0 : v1;

    const float corners[4][4] = {
        {-halfWidth, -halfHeight, cu0, cv0},
        { halfWidth, -halfHeight, cu1, cv0},
        { halfWidth,  halfHeight, cu1, cv1},
        {-halfWidth,  halfHeight, cu0, cv1},
    };
    const int indices[6] = {0, 1, 2, 0, 2, 3};

    float quad[kFloatsPerQuad]; // 6 顶点 × 8 float = 48
    for (int i = 0; i < 6; ++i) {
        const int idx = indices[i];
        const float localX = corners[idx][0];
        const float localY = corners[idx][1];
        float* vertex = &quad[i * 8];
        vertex[0] = centerPosition.x + localX * cosR - localY * sinR;
        vertex[1] = centerPosition.y + localX * sinR + localY * cosR;
        vertex[2] = corners[idx][2];
        vertex[3] = corners[idx][3];
        vertex[4] = tint.r;
        vertex[5] = tint.g;
        vertex[6] = tint.b;
        vertex[7] = tint.a;
    }
    PushQuad(texture, quad);
}

void SpriteBatch::Flush() {
    if (m_vertices.empty() || m_shader == nullptr) {
        return;
    }
    if (m_currentTexture == nullptr || !m_currentTexture->IsValid()) {
        m_vertices.clear();
        return;
    }

    gl::glBindVertexArray(m_vao);
    gl::glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl::glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<gl::GLsizeiptr>(m_vertices.size() * sizeof(float)),
                        m_vertices.data());
    glBindTexture(GL_TEXTURE_2D, m_currentTexture->GetHandle());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size() / 8));
    ++m_drawCalls;
    m_vertices.clear();
}

} // namespace legend::render
