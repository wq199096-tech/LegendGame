#pragma once

#include "Client/Visuals/VisualAssetData.h"

#include "Engine/Render/Texture.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace legend::resource {
class ResourceManager;
}

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段24 指令四：AssetManager —— manifest 驱动的纹理加载/缓存/查找/fallback。
// - 同一 assetId 只加载一次（TextureCache），禁止每帧重复加载
// - 缺失资源 → 紫/棋盘 Fallback + 日志（assetId/path/caller），绝不崩溃/黑屏
// - Reload()（F10 热重载）：失败保留旧资源并打印错误，禁止崩溃（指令三十四）
// ---------------------------------------------------------------------------
class AssetManager {
public:
    struct TextureInfo {
        std::shared_ptr<render::Texture> texture;
        int width = 0;
        int height = 0;
        float pivotX = 0.5f;
        float pivotY = 0.5f;
        bool fallback = false; // true = 命中 fallback（资源缺失）
    };

    void Initialize(legend::resource::ResourceManager& resources,
                    const visual::AssetManifest& manifest);

    // manifest 中的 assetId → 纹理（缓存）。info 可空。
    // 返回的纹理永不为 nullptr（缺失时返回 fallback 纹理）。
    std::shared_ptr<render::Texture> GetTexture(const std::string& assetId,
                                                TextureInfo* outInfo = nullptr,
                                                const char* caller = "");

    // F10：重新从磁盘加载全部已缓存条目；失败保留旧资源 + LOG_ERROR。
    // 返回成功重载的条目数。
    int ReloadAll();

    // 统计（F9 面板）
    int LoadedCount() const { return static_cast<int>(m_cache.size()); }
    int FallbackCount() const { return m_fallbackCount; }
    int ReloadCount() const { return m_reloadCount; }

    static const char* kFallbackAssetId; // 日志用

private:
    const visual::AssetManifestEntry* FindEntry(const std::string& assetId) const;
    // manifest path（"Assets/..."）→ ResourceManager 相对路径（去 "Assets/" 前缀）。
    std::string ToResourcePath(const std::string& manifestPath) const;
    std::shared_ptr<render::Texture> GetFallbackTexture();

    legend::resource::ResourceManager* m_resources = nullptr;
    const visual::AssetManifest* m_manifest = nullptr;
    std::unordered_map<std::string, TextureInfo> m_cache;
    std::shared_ptr<render::Texture> m_fallbackTexture;
    int m_fallbackCount = 0;
    int m_reloadCount = 0;
    std::string m_fallbackWarned; // 已警告过的缺失 assetId（去重日志）
};

} // namespace legend::client
