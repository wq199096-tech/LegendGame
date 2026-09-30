#pragma once

// ---------------------------------------------------------------------------
// 阶段24 指令三十一：TTF 字体加载 + UTF-8 文本渲染（stb_truetype，禁止自己解析字体）。
//
// - 动态字形图集：按需烘焙码点（ASCII + CJK 按需），CPU 持有图集位图，
//   脏标记每帧至多一次纹理上传
// - UTF-8 -> 码点解码正确（中文管线就绪）；缺字形绘制空心方框
// - 字体查找顺序：Assets/Fonts/*.(ttf|otf) → C:\Windows\Fonts\msyh.ttc →
//   simhei.ttf → arial.ttf（开发环境回退；正式接入时把字体放入 Assets/Fonts）
// ---------------------------------------------------------------------------

#include "Engine/Math/Color.h"
#include "Engine/Math/Vector2.h"
#include "Engine/Render/Texture.h"

#include <map>
#include <string>
#include <vector>

struct stbtt_fontinfo;

namespace legend::render {
class SpriteBatch;
}

namespace legend::client {

class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // fontPath 为空时走内置查找链；失败返回 false（调用方禁用文本层，不得崩溃）。
    bool Initialize(const std::string& fontPath = std::string(), int bakePixelHeight = 32);
    void Shutdown();
    bool IsReady() const { return m_ready; }
    const std::string& FontSource() const { return m_fontSource; }

    // 宽度（像素，按 pixelHeight 缩放）。
    float MeasureText(const std::string& utf8Text, float pixelHeight) const;

    // 位置语义：左上角基线框（position = 文本包围盒左上）。
    // alignRight/alignCenter 相对 position.x 的水平对齐。
    // 命名注意：不能用 DrawText——Windows GDI 宏（DrawTextA/W）会重命名成员。
    void DrawString(render::SpriteBatch& batch, const math::Vector2& position,
                    const std::string& utf8Text, float pixelHeight, const math::Color& color,
                    bool alignRight = false, bool alignCenter = false) const;

    // 带阴影版本（可读性：名字板/飘字用）。
    void DrawStringShadow(render::SpriteBatch& batch, const math::Vector2& position,
                          const std::string& utf8Text, float pixelHeight,
                          const math::Color& color, bool alignRight = false,
                          bool alignCenter = false) const;

    // 每帧开始时调用（上传脏图集；与 SpriteBatch 相机无关）。
    void BeginFrame();

    int GlyphCount() const { return static_cast<int>(m_glyphs.size()); }

private:
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // 图集 UV
        int width = 0;                        // 烘焙像素尺寸
        int height = 0;
        int bearingX = 0;                     // 相对笔位置的偏移（bake 坐标系）
        int bearingY = 0;
        float advance = 0.0f;                 // 烘焙像素尺度下的步进
    };

    const Glyph* EnsureGlyph(int codepoint);
    bool BakeGlyph(int codepoint, Glyph& outGlyph);
    void UploadIfDirty() const;
    static int DecodeUtf8(const std::string& text, size_t& i);

    std::vector<unsigned char> m_fontData;      // TTF 文件字节
    stbtt_fontinfo* m_fontInfo = nullptr;       // new 出来（头文件前向声明）
    int m_fontOffset = 0;                       // .ttc offset
    float m_bakeScale = 0.0f;                   // stbtt_ScaleForPixelHeight
    int m_bakePixelHeight = 32;
    int m_ascent = 0;
    int m_descent = 0;
    int m_lineGap = 0;

    static constexpr int kAtlasSize = 1024;
    std::vector<unsigned char> m_atlasCpu;      // kAtlasSize*kAtlasSize（单通道 alpha）
    int m_atlasCursorX = 1;
    int m_atlasCursorY = 1;
    int m_atlasRowHeight = 0;
    mutable bool m_dirty = false;
    mutable render::Texture m_atlasTexture;     // const 方法内惰性上传 → mutable
    mutable render::Texture m_whiteTexture;     // 缺字形占位小方框
    mutable bool m_atlasUploaded = false;

    std::map<int, Glyph> m_glyphs;
    bool m_ready = false;
    std::string m_fontSource;
};

} // namespace legend::client
