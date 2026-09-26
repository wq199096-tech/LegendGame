#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Engine/Render/SpriteBatch.h"

namespace legend::render {
class Shader;
class Texture;
class Camera2D;
}

namespace legend::resource {
class ResourceManager;
}

namespace legend::entity {
class Character;
}

namespace legend::map {

class Map;
class MapChunk;
struct MapObject;

// 统一 Y-Sort 排序项：MapObject 与 Character（以及未来 NPC/Monster）进入同一排序队列
struct RenderSortItem {
    enum class Type { MapObject, Character };

    int sortLayer = 0;
    float sortY = 0.0f;
    int renderOrder = 0;
    Type type = Type::MapObject;
    const MapObject* mapObject = nullptr;
    const entity::Character* character = nullptr;

    // 排序键：sortLayer -> sortY -> renderOrder（与 MapRenderer::YSortCompare 规则一致）
    static bool Compare(const RenderSortItem& a, const RenderSortItem& b) {
        if (a.sortLayer != b.sortLayer) {
            return a.sortLayer < b.sortLayer;
        }
        if (a.sortY != b.sortY) {
            return a.sortY < b.sortY;
        }
        return a.renderOrder < b.renderOrder;
    }
};

// 地图渲染器：
// - Ground 按 Chunk 缓存顶点，仅渲染可见 Chunk（+1 Chunk 边距）
// - 所有 Tile/物件通过 SpriteBatch 批量提交
// - Y-Sort：物件 + 动态实体按底部 Y 排序渲染（前景遮挡）
class MapRenderer {
public:
    struct FrameStats {
        int visibleChunks = 0;
        int renderedTiles = 0;
        int drawCalls = 0;
        int renderedObjects = 0;
        int visibleObjects = 0;
        int totalObjects = 0;
    };

    bool Initialize(render::Shader& spriteShader);
    void Shutdown();

    // 纹理注册：Tile ID / 物件 textureId -> 纹理（未来替换为 Tileset Atlas）
    void RegisterTileTexture(uint16_t tileId, std::shared_ptr<render::Texture> texture);
    void RegisterObjectTexture(const std::string& textureId,
                               std::shared_ptr<render::Texture> texture);
    // 生成并注册默认占位纹理（游戏与编辑器共用）
    void CreateDefaultPlaceholderTextures(legend::resource::ResourceManager& resources);

    // 每帧：BeginFrame -> RenderGround -> (DrawMapObject/Flush) -> RenderCollisionOverlay -> EndFrame
    void BeginFrame(render::Camera2D& camera, float viewportWidth, float viewportHeight);
    void RenderGround(Map& map);
    // 物件视口剔除（AABB + 边距）；返回 false 表示不在可视范围内，调用方跳过排序/绘制
    bool IsObjectVisible(const MapObject& object, float margin = 192.0f) const;
    void DrawMapObject(const MapObject& object);
    // 物件剔除统计（由调用方在剔除后提供）
    void SetObjectCounts(int visibleObjects, int totalObjects) {
        m_currentStats.visibleObjects = visibleObjects;
        m_currentStats.totalObjects = totalObjects;
    }
    // 批渲染访问（CharacterRenderer 等需要直接提交的渲染器复用同一批次）
    render::SpriteBatch& GetBatch() { return m_batch; }
    void Flush();
    void RenderCollisionOverlay(Map& map);
    void EndFrame();

    const FrameStats& GetLastFrameStats() const { return m_lastStats; }

    // 当前帧可视世界范围（供编辑器网格等叠加层使用）
    float GetViewLeft() const { return m_viewLeft; }
    float GetViewTop() const { return m_viewTop; }
    float GetViewRight() const { return m_viewRight; }
    float GetViewBottom() const { return m_viewBottom; }

    // Y-Sort：sortLayer 分层 → 同层严格按 bottomY（底部 Y 决定前后）→ renderOrder 仅平局判定
    static bool YSortCompare(const MapObject* a, const MapObject* b);

private:
    struct ChunkCache {
        bool built = false;
        // 按 Tile 纹理分组的顶点缓存（一次重建、按组提交，保证批渲染分组）
        std::vector<std::pair<uint16_t, std::vector<float>>> byTexture;
    };

    void RebuildChunkCache(Map& map, MapChunk& chunk, ChunkCache& cache);

    render::SpriteBatch m_batch;
    render::Shader* m_shader = nullptr;

    std::unordered_map<uint16_t, std::shared_ptr<render::Texture>> m_tileTextures;
    std::unordered_map<std::string, std::shared_ptr<render::Texture>> m_objectTextures;
    std::unordered_map<uint64_t, ChunkCache> m_chunkCaches;

    // 当前帧视口信息
    float m_viewportWidth = 1.0f;
    float m_viewportHeight = 1.0f;
    float m_viewLeft = 0.0f;
    float m_viewTop = 0.0f;
    float m_viewRight = 0.0f;
    float m_viewBottom = 0.0f;

    FrameStats m_currentStats{};
    FrameStats m_lastStats{};
    std::string m_missingTextureWarned;
};

} // namespace legend::map
