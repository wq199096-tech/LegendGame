#pragma once

namespace legend::render {

// GPU 纹理封装（RGBA8）
class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    bool CreateFromPixels(int width, int height, const unsigned char* rgbaPixels);
    void Destroy();

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    unsigned int GetHandle() const { return m_handle; }
    bool IsValid() const { return m_handle != 0; }

private:
    unsigned int m_handle = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace legend::render
