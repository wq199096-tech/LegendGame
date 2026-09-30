#include "Client/Visuals/Font.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Render/SpriteBatch.h"

#include <SDL3/SDL.h>

#include "Engine/Render/GLApi.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "ThirdParty/stb/stb_truetype.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace legend::client {

namespace {
constexpr int kAtlasSize = 1024;
constexpr int kFirstCodepoint = 32;
constexpr int kAsciiCount = 95; // 32..126 预烘焙

std::string FindFontFile() {
    namespace fs = std::filesystem;
    // 1) 仓库 Assets/Fonts 下第一个字体
    std::error_code ec;
    if (fs::exists("Assets/Fonts", ec)) {
        for (const auto& entry : fs::directory_iterator("Assets/Fonts", ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (ext == ".ttf" || ext == ".otf") {
                return entry.path().string();
            }
        }
    }
    // 2) Windows 系统字体（开发回退；优先中文字体——CJK 管线验证）
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",   // 微软雅黑（含 CJK）
        "C:\\Windows\\Fonts\\simhei.ttf", // 黑体
        "C:\\Windows\\Fonts\\msyhbd.ttc",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    for (const char* candidate : candidates) {
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
    }
    return std::string();
}
} // namespace

TextRenderer::~TextRenderer() {
    Shutdown();
}

bool TextRenderer::Initialize(const std::string& fontPath, int bakePixelHeight) {
    Shutdown();

    std::string path = fontPath;
    if (path.empty()) {
        path = FindFontFile();
    }
    if (path.empty()) {
        LOG_WARN("[Font] no TTF font found (Assets/Fonts or system) — text rendering disabled.");
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        LOG_WARN("[Font] cannot open font '" + path + "' — text rendering disabled.");
        return false;
    }
    m_fontData.assign((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    if (m_fontData.empty()) {
        LOG_WARN("[Font] font file empty '" + path + "'.");
        return false;
    }

    m_fontInfo = new stbtt_fontinfo();
    m_fontOffset = stbtt_GetFontOffsetForIndex(m_fontData.data(), 0);
    if (stbtt_InitFont(m_fontInfo, m_fontData.data(), m_fontOffset) == 0) {
        LOG_WARN("[Font] stbtt_InitFont failed for '" + path + "'.");
        delete m_fontInfo;
        m_fontInfo = nullptr;
        return false;
    }

    m_bakePixelHeight = bakePixelHeight > 0 ? bakePixelHeight : 32;
    m_bakeScale = stbtt_ScaleForPixelHeight(m_fontInfo, static_cast<float>(m_bakePixelHeight));
    stbtt_GetFontVMetrics(m_fontInfo, &m_ascent, &m_descent, &m_lineGap);

    m_atlasCpu.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
    m_atlasCursorX = 1;
    m_atlasCursorY = 1;
    m_atlasRowHeight = 0;
    m_dirty = false;
    m_atlasUploaded = false;

    // 缺字形占位用的 4x4 白色纹理（不依赖 ResourceManager）。
    {
        std::vector<unsigned char> white(4 * 4 * 4, 255);
        m_whiteTexture.CreateFromPixels(4, 4, white.data());
    }

    // 预烘焙 ASCII（常用集即时可用；CJK 按需烘焙）。
    for (int cp = kFirstCodepoint; cp < kFirstCodepoint + kAsciiCount; ++cp) {
        EnsureGlyph(cp);
    }

    m_ready = true;
    m_fontSource = path;
    LOG_INFO("[Font] loaded '" + path + "' bake=" + std::to_string(m_bakePixelHeight) +
             "px asciiGlyphs=" + std::to_string(m_glyphs.size()));
    return true;
}

void TextRenderer::Shutdown() {
    m_atlasTexture.Destroy();
    m_whiteTexture.Destroy();
    m_atlasCpu.clear();
    m_atlasCpu.shrink_to_fit();
    m_glyphs.clear();
    delete m_fontInfo;
    m_fontInfo = nullptr;
    m_fontData.clear();
    m_fontData.shrink_to_fit();
    m_ready = false;
    m_atlasUploaded = false;
}

void TextRenderer::UploadIfDirty() const {
    if (!m_dirty) {
        return;
    }
    // 单通道图集 -> RGBA 展开（Texture 只接受 RGBA8）。
    static thread_local std::vector<unsigned char> rgba;
    rgba.resize(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4);
    for (size_t i = 0; i < static_cast<size_t>(kAtlasSize) * kAtlasSize; ++i) {
        const unsigned char a = m_atlasCpu[i];
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = a;
    }
    if (!m_atlasTexture.IsValid()) {
        if (!m_atlasTexture.CreateFromPixels(kAtlasSize, kAtlasSize, rgba.data())) {
            m_dirty = false;
            return;
        }
    } else {
        glBindTexture(GL_TEXTURE_2D, m_atlasTexture.GetHandle());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasSize, kAtlasSize, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba.data());
    }
    m_atlasUploaded = true;
    m_dirty = false;
}

bool TextRenderer::BakeGlyph(int codepoint, Glyph& outGlyph) {
    if (m_fontInfo == nullptr) {
        return false;
    }
    int advance = 0;
    int leftBearing = 0;
    stbtt_GetCodepointHMetrics(m_fontInfo, codepoint, &advance, &leftBearing);

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(m_fontInfo, codepoint, m_bakeScale, m_bakeScale, &x0, &y0, &x1,
                                &y1);
    const int glyphWidth = x1 - x0;
    const int glyphHeight = y1 - y0;

    if (glyphWidth <= 0 || glyphHeight <= 0) {
        // 空白字形（空格等）
        outGlyph.advance = static_cast<float>(advance) * m_bakeScale;
        outGlyph.width = 0;
        outGlyph.height = 0;
        outGlyph.bearingX = static_cast<int>(static_cast<float>(leftBearing) * m_bakeScale);
        outGlyph.bearingY = 0;
        return true;
    }

    // 图集排布（shelf packer）；放不下时换行/失败。
    if (m_atlasCursorX + glyphWidth + 1 > kAtlasSize) {
        m_atlasCursorX = 1;
        m_atlasCursorY += m_atlasRowHeight + 1;
        m_atlasRowHeight = 0;
    }
    if (m_atlasCursorY + glyphHeight + 1 > kAtlasSize) {
        LOG_WARN("[Font] glyph atlas full — codepoint " + std::to_string(codepoint) +
                 " skipped.");
        return false;
    }

    stbtt_MakeCodepointBitmap(m_fontInfo,
                              &m_atlasCpu[static_cast<size_t>(m_atlasCursorY) * kAtlasSize +
                                          m_atlasCursorX],
                              glyphWidth, glyphHeight, kAtlasSize, m_bakeScale, m_bakeScale,
                              codepoint);

    const float inv = 1.0f / static_cast<float>(kAtlasSize);
    outGlyph.u0 = static_cast<float>(m_atlasCursorX) * inv;
    outGlyph.v0 = static_cast<float>(m_atlasCursorY) * inv;
    outGlyph.u1 = static_cast<float>(m_atlasCursorX + glyphWidth) * inv;
    outGlyph.v1 = static_cast<float>(m_atlasCursorY + glyphHeight) * inv;
    outGlyph.width = glyphWidth;
    outGlyph.height = glyphHeight;
    outGlyph.bearingX = x0;
    outGlyph.bearingY = y0;
    outGlyph.advance = static_cast<float>(advance) * m_bakeScale;

    m_atlasCursorX += glyphWidth + 1;
    m_atlasRowHeight = std::max(m_atlasRowHeight, glyphHeight);
    m_dirty = true;
    return true;
}

const TextRenderer::Glyph* TextRenderer::EnsureGlyph(int codepoint) {
    const auto it = m_glyphs.find(codepoint);
    if (it != m_glyphs.end()) {
        return &it->second;
    }
    Glyph glyph;
    if (!BakeGlyph(codepoint, glyph)) {
        return nullptr;
    }
    return &m_glyphs.emplace(codepoint, glyph).first->second;
}

int TextRenderer::DecodeUtf8(const std::string& text, size_t& i) {
    const auto byteAt = [&text](size_t index) -> unsigned int {
        return static_cast<unsigned char>(text[index]);
    };
    const unsigned int c0 = byteAt(i);
    if (c0 < 0x80) {
        ++i;
        return static_cast<int>(c0);
    }
    int codepoint = -1;
    size_t extra = 0;
    if ((c0 & 0xE0) == 0xC0) {
        codepoint = static_cast<int>(c0 & 0x1F);
        extra = 1;
    } else if ((c0 & 0xF0) == 0xE0) {
        codepoint = static_cast<int>(c0 & 0x0F);
        extra = 2;
    } else if ((c0 & 0xF8) == 0xF0) {
        codepoint = static_cast<int>(c0 & 0x07);
        extra = 3;
    }
    if (extra == 0 || i + extra >= text.size()) {
        ++i;
        return -1;
    }
    for (size_t k = 1; k <= extra; ++k) {
        const unsigned int cb = byteAt(i + k);
        if ((cb & 0xC0) != 0x80) {
            i = i + k;
            return -1;
        }
        codepoint = (codepoint << 6) | static_cast<int>(cb & 0x3F);
    }
    i += extra + 1;
    return codepoint;
}

float TextRenderer::MeasureText(const std::string& utf8Text, float pixelHeight) const {
    if (!m_ready) {
        return 0.0f;
    }
    const float scale = pixelHeight / static_cast<float>(m_bakePixelHeight);
    float width = 0.0f;
    size_t i = 0;
    while (i < utf8Text.size()) {
        const int codepoint = DecodeUtf8(utf8Text, i);
        if (codepoint < 0) {
            continue;
        }
        const Glyph* glyph = const_cast<TextRenderer*>(this)->EnsureGlyph(codepoint);
        if (glyph != nullptr) {
            width += glyph->advance;
        }
    }
    return width * scale;
}

void TextRenderer::DrawString(render::SpriteBatch& batch, const math::Vector2& position,
                            const std::string& utf8Text, float pixelHeight,
                            const math::Color& color, bool alignRight,
                            bool alignCenter) const {
    if (!m_ready || utf8Text.empty()) {
        return;
    }
    const_cast<TextRenderer*>(this)->UploadIfDirty();
    if (!m_atlasTexture.IsValid()) {
        return;
    }

    const float scale = pixelHeight / static_cast<float>(m_bakePixelHeight);
    const float totalWidth = MeasureText(utf8Text, pixelHeight);
    float penX = position.x;
    if (alignRight) {
        penX -= totalWidth;
    } else if (alignCenter) {
        penX -= totalWidth * 0.5f;
    }
    // position = 文本包围盒左上角；ascent 决定基线。
    const float ascentPx = static_cast<float>(m_ascent) * m_bakeScale * scale;
    const float baselineY = position.y + ascentPx;

    size_t i = 0;
    while (i < utf8Text.size()) {
        const int codepoint = DecodeUtf8(utf8Text, i);
        if (codepoint < 0) {
            continue;
        }
        const Glyph* glyph = const_cast<TextRenderer*>(this)->EnsureGlyph(codepoint);
        if (glyph == nullptr || (glyph->width <= 0 || glyph->height <= 0)) {
            if (glyph == nullptr) {
            // 缺字形：白色小方框占位（可见、可辨识、不崩溃）。
            const float boxSize = pixelHeight * 0.5f;
            const math::Color dim(color.r * 0.5f, color.g * 0.5f, color.b * 0.5f, color.a * 0.8f);
            batch.DrawQuad(m_whiteTexture, {penX + boxSize * 0.5f, baselineY - boxSize * 0.5f},
                           {boxSize / 4.0f, boxSize / 4.0f}, 0.0f, dim);
            penX += boxSize;
            } else {
                penX += glyph->advance * scale;
            }
            continue;
        }
        const float glyphW = static_cast<float>(glyph->width) * scale;
        const float glyphH = static_cast<float>(glyph->height) * scale;
        const float gx = penX + static_cast<float>(glyph->bearingX) * scale;
        const float gy = baselineY - static_cast<float>(glyph->bearingY) * scale;
        // DrawQuad 以中心点定位；scale 语义 = 纹理尺寸倍数（图集 1024）。
        batch.DrawQuad(m_atlasTexture, {gx + glyphW * 0.5f, gy + glyphH * 0.5f},
                       {glyphW / static_cast<float>(kAtlasSize),
                        glyphH / static_cast<float>(kAtlasSize)},
                       0.0f, color, false, false, glyph->u0, glyph->v0, glyph->u1, glyph->v1);
        penX += glyph->advance * scale;
    }
}

void TextRenderer::DrawStringShadow(render::SpriteBatch& batch, const math::Vector2& position,
                                  const std::string& utf8Text, float pixelHeight,
                                  const math::Color& color, bool alignRight,
                                  bool alignCenter) const {
    if (!m_ready) {
        return;
    }
    const math::Color shadow(0.0f, 0.0f, 0.0f, color.a * 0.75f);
    const float off = std::max(1.0f, pixelHeight / 16.0f);
    DrawString(batch, position + math::Vector2(off, off), utf8Text, pixelHeight, shadow,
             alignRight, alignCenter);
    DrawString(batch, position, utf8Text, pixelHeight, color, alignRight, alignCenter);
}

void TextRenderer::BeginFrame() {
    // 兼容接口：上传在 DrawString 内惰性完成。
}

} // namespace legend::client
