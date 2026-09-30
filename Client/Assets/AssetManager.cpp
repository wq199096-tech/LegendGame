#include "Client/Assets/AssetManager.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Math/Color.h"
#include "Engine/Resource/ResourceManager.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <vector>

// stb 声明（STB_IMAGE_IMPLEMENTATION 仅在 ResourceManager.cpp 定义，链接共享实现）
#include "ThirdParty/stb/stb_image.h"

namespace legend::client {

const char* AssetManager::kFallbackAssetId = "<fallback>";

namespace {
// stb 直接读盘（热重载需要绕过 ResourceManager 缓存拿到新像素）。
bool LoadPixelsFromFile(const std::string& fullPath, int& w, int& h, int& channels,
                        std::vector<unsigned char>& pixels) {
    unsigned char* data = stbi_load(fullPath.c_str(), &w, &h, &channels, 4);
    if (data == nullptr) {
        return false;
    }
    pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    channels = 4;
    return true;
}
} // namespace

void AssetManager::Initialize(legend::resource::ResourceManager& resources,
                              const visual::AssetManifest& manifest) {
    m_resources = &resources;
    m_manifest = &manifest;

    // 统一 Fallback：紫/黑棋盘（指令三十五：可见、可辨识、不崩溃）。
    m_fallbackTexture = resources.CreateCheckerTexture(
        "internal/asset_fallback", 32, 8, legend::math::Color(0.85f, 0.20f, 0.85f, 1.0f),
        legend::math::Color(0.12f, 0.10f, 0.12f, 1.0f));
}

std::string AssetManager::ToResourcePath(const std::string& manifestPath) const {
    // manifest 相对路径以 "Assets/" 开头（相对仓库根）；ResourceManager 以 Assets/ 为根。
    if (manifestPath.rfind("Assets/", 0) == 0) {
        return manifestPath.substr(7);
    }
    if (manifestPath.rfind("Assets\\", 0) == 0) {
        return manifestPath.substr(7);
    }
    return manifestPath;
}

const visual::AssetManifestEntry* AssetManager::FindEntry(const std::string& assetId) const {
    return m_manifest != nullptr ? visual::FindAsset(*m_manifest, assetId) : nullptr;
}

std::shared_ptr<render::Texture> AssetManager::GetFallbackTexture() {
    if (m_fallbackTexture == nullptr && m_resources != nullptr) {
        // Initialize 未调用时兜底（不应发生）。
        m_fallbackTexture = m_resources->CreateCheckerTexture(
            "internal/asset_fallback", 32, 8,
            legend::math::Color(0.85f, 0.20f, 0.85f, 1.0f),
            legend::math::Color(0.12f, 0.10f, 0.12f, 1.0f));
    }
    return m_fallbackTexture;
}

std::shared_ptr<render::Texture> AssetManager::GetTexture(const std::string& assetId,
                                                          TextureInfo* outInfo,
                                                          const char* caller) {
    const auto cached = m_cache.find(assetId);
    if (cached != m_cache.end()) {
        if (outInfo != nullptr) {
            *outInfo = cached->second;
        }
        return cached->second.texture;
    }

    TextureInfo info;
    const visual::AssetManifestEntry* entry = FindEntry(assetId);
    if (entry != nullptr && entry->enabled) {
        // ResourceManager 加载失败返回占位纹理（无法区分），先做文件存在性检查。
        const std::string fullPath = (std::filesystem::path(m_resources->GetAssetRoot()) /
                                      std::filesystem::path(ToResourcePath(entry->path)))
                                         .string();
        std::error_code ec;
        if (std::filesystem::exists(fullPath, ec)) {
            std::shared_ptr<render::Texture> texture =
                m_resources->LoadTexture(ToResourcePath(entry->path));
            if (texture != nullptr && texture->IsValid()) {
                info.texture = std::move(texture);
                info.width = entry->width;
                info.height = entry->height;
                info.pivotX = entry->pivotX;
                info.pivotY = entry->pivotY;
                info.fallback = false;
            }
        }
    }

    if (info.texture == nullptr) {
        // 缺失/禁用：fallback + 去重日志（assetId/path/caller）。
        const std::string path = entry != nullptr ? entry->path : std::string("<unregistered>");
        if (m_fallbackWarned != assetId) {
            LOG_WARN("[AssetManager] missing asset id='" + assetId + "' path='" + path +
                     "' caller=" + (caller != nullptr ? caller : "?") + " -> fallback");
            m_fallbackWarned = assetId;
        }
        ++m_fallbackCount;
        info.texture = GetFallbackTexture();
        info.width = info.texture != nullptr ? info.texture->GetWidth() : 32;
        info.height = info.texture != nullptr ? info.texture->GetHeight() : 32;
        info.pivotX = 0.5f;
        info.pivotY = 0.5f;
        info.fallback = true;
    }

    const auto inserted = m_cache.emplace(assetId, info);
    if (outInfo != nullptr) {
        *outInfo = inserted.first->second;
    }
    return inserted.first->second.texture;
}

int AssetManager::ReloadAll() {
    if (m_resources == nullptr || m_manifest == nullptr) {
        return 0;
    }
    int reloaded = 0;
    for (auto& [assetId, info] : m_cache) {
        const visual::AssetManifestEntry* entry = FindEntry(assetId);
        if (entry == nullptr || !entry->enabled || info.fallback) {
            continue; // 缺失条目热重载不处理（仍走 fallback）
        }
        const std::string fullPath =
            (std::filesystem::path(m_resources->GetAssetRoot()) /
             std::filesystem::path(ToResourcePath(entry->path)))
                .string();
        int w = 0;
        int h = 0;
        int channels = 0;
        std::vector<unsigned char> pixels;
        if (!LoadPixelsFromFile(fullPath, w, h, channels, pixels)) {
            LOG_ERROR("[AssetManager] reload failed for '" + assetId + "' (" + fullPath +
                      ") — keeping old texture.");
            continue;
        }
        auto fresh = std::make_shared<render::Texture>();
        if (!fresh->CreateFromPixels(w, h, pixels.data())) {
            LOG_ERROR("[AssetManager] GPU upload failed for '" + assetId +
                      "' — keeping old texture.");
            continue;
        }
        info.texture = std::move(fresh);
        info.width = w;
        info.height = h;
        ++reloaded;
    }
    ++m_reloadCount;
    LOG_INFO("[AssetManager] ReloadAll: " + std::to_string(reloaded) + " texture(s) reloaded.");
    return reloaded;
}

} // namespace legend::client
