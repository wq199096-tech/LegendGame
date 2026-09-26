#include "Engine/Render/Texture.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Render/GLApi.h"

namespace legend::render {

Texture::~Texture() {
    Destroy();
}

void Texture::Destroy() {
    if (m_handle != 0) {
        glDeleteTextures(1, &m_handle);
        m_handle = 0;
    }
    m_width = 0;
    m_height = 0;
}

bool Texture::CreateFromPixels(int width, int height, const unsigned char* rgbaPixels) {
    if (width <= 0 || height <= 0 || rgbaPixels == nullptr) {
        LOG_ERROR("Texture::CreateFromPixels called with invalid parameters.");
        return false;
    }

    Destroy();

    GLuint handle = 0;
    glGenTextures(1, &handle);
    if (handle == 0) {
        LOG_ERROR("glGenTextures failed.");
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, handle);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_handle = handle;
    m_width = width;
    m_height = height;
    return true;
}

} // namespace legend::render
