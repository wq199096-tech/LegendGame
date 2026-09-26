#include "Engine/Resource/ResourceManager.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Math/Color.h"
#include "Engine/Render/Texture.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

// PNG/JPEG/TGA 等图片解码（单头文件，公有领域）
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

namespace legend::resource {

bool ResourceManager::Initialize(const std::string& assetRoot) {
    namespace fs = std::filesystem;

    m_assetRoot = assetRoot;

    // 若当前工作目录下找不到 Assets，则回退到 exe 所在目录
    if (!fs::exists(m_assetRoot)) {
        // SDL3 的 SDL_GetBasePath 返回 const char*，由 SDL 内部持有，无需释放
        const char* basePath = SDL_GetBasePath();
        if (basePath != nullptr) {
            const fs::path candidate = fs::path(basePath) / m_assetRoot;
            if (fs::exists(candidate)) {
                m_assetRoot = candidate.string();
            }
        }
    }

    std::error_code ec;
    fs::create_directories(m_assetRoot, ec);
    LOG_INFO("ResourceManager initialized. Asset root: " + m_assetRoot);
    return true;
}

void ResourceManager::Shutdown() {
    m_textureCache.clear();
    LOG_INFO("ResourceManager shutdown, texture cache cleared.");
}

std::shared_ptr<render::Texture> ResourceManager::CacheTexture(
    const std::string& key, std::shared_ptr<render::Texture> texture) {
    m_textureCache[key] = texture;
    return texture;
}

std::shared_ptr<render::Texture> ResourceManager::LoadTexture(const std::string& relativePath) {
    std::string key = relativePath;
    std::replace(key.begin(), key.end(), '\\', '/');

    const auto it = m_textureCache.find(key);
    if (it != m_textureCache.end() && it->second) {
        return it->second;
    }

    namespace fs = std::filesystem;
    const fs::path fullPath = fs::path(m_assetRoot) / fs::path(key);

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        LOG_ERROR(std::string("Texture not found: ") + relativePath +
                  " (" + (stbi_failure_reason() ? stbi_failure_reason() : "unknown") + ")");
        return GetPlaceholderTexture();
    }

    auto texture = std::make_shared<render::Texture>();
    // 请求 4 通道后 stbi 总是返回紧凑排列的 RGBA8 数据
    const bool uploaded = texture->CreateFromPixels(width, height, pixels);
    stbi_image_free(pixels);

    if (!uploaded) {
        LOG_ERROR("Failed to upload texture to GPU: " + relativePath);
        return GetPlaceholderTexture();
    }

    LOG_INFO("Texture loaded: " + relativePath + " (" +
             std::to_string(width) + "x" + std::to_string(height) + ")");
    return CacheTexture(key, std::move(texture));
}

std::shared_ptr<render::Texture> ResourceManager::GetPlaceholderTexture() {
    return CreateCheckerTexture("internal/placeholder", 32, 8,
                                math::Color::FromRGBA8(230, 0, 230),
                                math::Color::FromRGBA8(20, 20, 20));
}

std::shared_ptr<render::Texture> ResourceManager::CreateCheckerTexture(
    const std::string& cacheKey, int size, int cellSize,
    const math::Color& colorA, const math::Color& colorB) {
    const auto it = m_textureCache.find(cacheKey);
    if (it != m_textureCache.end() && it->second) {
        return it->second;
    }

    if (size <= 0 || cellSize <= 0) {
        LOG_ERROR("CreateCheckerTexture: invalid size parameters.");
        return GetPlaceholderTexture();
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int cell = ((x / cellSize) + (y / cellSize)) % 2;
            const math::Color& color = cell != 0 ? colorB : colorA;
            unsigned char* pixel = pixels.data() + (static_cast<size_t>(y) * size + x) * 4;
            pixel[0] = static_cast<unsigned char>(color.r * 255.0f);
            pixel[1] = static_cast<unsigned char>(color.g * 255.0f);
            pixel[2] = static_cast<unsigned char>(color.b * 255.0f);
            pixel[3] = static_cast<unsigned char>(color.a * 255.0f);
        }
    }

    auto texture = std::make_shared<render::Texture>();
    texture->CreateFromPixels(size, size, pixels.data());
    return CacheTexture(cacheKey, std::move(texture));
}

std::shared_ptr<render::Texture> ResourceManager::CreateSolidTexture(
    const std::string& cacheKey, int size, const math::Color& color) {
    const auto it = m_textureCache.find(cacheKey);
    if (it != m_textureCache.end() && it->second) {
        return it->second;
    }

    if (size <= 0) {
        LOG_ERROR("CreateSolidTexture: invalid size parameter.");
        return GetPlaceholderTexture();
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = static_cast<unsigned char>(color.r * 255.0f);
        pixels[i + 1] = static_cast<unsigned char>(color.g * 255.0f);
        pixels[i + 2] = static_cast<unsigned char>(color.b * 255.0f);
        pixels[i + 3] = static_cast<unsigned char>(color.a * 255.0f);
    }

    auto texture = std::make_shared<render::Texture>();
    texture->CreateFromPixels(size, size, pixels.data());
    return CacheTexture(cacheKey, std::move(texture));
}

} // namespace legend::resource
