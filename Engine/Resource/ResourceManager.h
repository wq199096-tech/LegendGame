#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace legend::math {
struct Color;
}

namespace legend::render {
class Texture;
}

namespace legend::resource {

// 资源管理器：负责纹理加载、缓存、路径管理与安全释放
class ResourceManager {
public:
    bool Initialize(const std::string& assetRoot = "Assets");
    void Shutdown();

    // 从 Assets 根目录加载纹理（相对路径，如 "Characters/player.png"）。
    // 相同路径第二次请求直接返回缓存，不会重复读盘。
    // 加载失败时记录错误日志并返回占位纹理，保证程序不崩溃。
    std::shared_ptr<render::Texture> LoadTexture(const std::string& relativePath);

    // 占位纹理：资源缺失时的安全替代品
    std::shared_ptr<render::Texture> GetPlaceholderTexture();

    // 程序生成棋盘格纹理（测试用），同样走缓存
    std::shared_ptr<render::Texture> CreateCheckerTexture(const std::string& cacheKey,
                                                          int size, int cellSize,
                                                          const math::Color& colorA,
                                                          const math::Color& colorB);

    // 程序生成纯色纹理（测试用），同样走缓存
    std::shared_ptr<render::Texture> CreateSolidTexture(const std::string& cacheKey,
                                                        int size, const math::Color& color);

    const std::string& GetAssetRoot() const { return m_assetRoot; }

private:
    std::shared_ptr<render::Texture> CacheTexture(const std::string& key,
                                                  std::shared_ptr<render::Texture> texture);

    std::string m_assetRoot;
    std::unordered_map<std::string, std::shared_ptr<render::Texture>> m_textureCache;
};

} // namespace legend::resource
