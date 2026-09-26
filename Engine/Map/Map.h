#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Engine/Map/CollisionLayer.h"
#include "Engine/Map/MapChunk.h"
#include "Engine/Map/ObjectLayer.h"
#include "Engine/Map/OcclusionLayer.h"
#include "Engine/Map/TileLayer.h"

namespace legend::map {

// 地图：数据驱动，拥有全部图层与 Chunk 网格
class Map {
public:
    Map();

    // ---- 元信息 ----
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    int GetTileSize() const { return m_tileSize; }
    int GetWidth() const { return m_width; }   // tiles
    int GetHeight() const { return m_height; } // tiles
    float GetWorldWidth() const { return static_cast<float>(m_width * m_tileSize); }
    float GetWorldHeight() const { return static_cast<float>(m_height * m_tileSize); }

    // 设置地图尺寸并重建全部图层数据（加载/新建时调用）
    void SetSize(int widthTiles, int heightTiles, int tileSize);

    // ---- 图层 ----
    TileLayer& GetGround() { return *m_ground; }
    const TileLayer& GetGround() const { return *m_ground; }
    ObjectLayer& GetObjects() { return *m_objects; }
    const ObjectLayer& GetObjects() const { return *m_objects; }
    CollisionLayer& GetCollision() { return *m_collision; }
    const CollisionLayer& GetCollision() const { return *m_collision; }
    OcclusionLayer& GetOcclusion() { return *m_occlusion; }
    const OcclusionLayer& GetOcclusion() const { return *m_occlusion; }

    // ---- Tile 快捷接口（修改后自动标记 Chunk dirty） ----
    uint16_t GetGroundTile(int tileX, int tileY) const { return m_ground->GetTile(tileX, tileY); }
    bool SetGroundTile(int tileX, int tileY, uint16_t id);

    // ---- 碰撞快捷接口 ----
    bool IsWorldBlocked(float worldX, float worldY) const {
        return m_collision->IsWorldPositionBlocked(static_cast<float>(worldX), static_cast<float>(worldY));
    }

    // ---- Chunk 网格 ----
    int GetChunkCountX() const { return m_chunkCountX; }
    int GetChunkCountY() const { return m_chunkCountY; }
    MapChunk* GetChunk(int chunkX, int chunkY) {
        if (chunkX < 0 || chunkY < 0 || chunkX >= m_chunkCountX || chunkY >= m_chunkCountY) {
            return nullptr;
        }
        return &m_chunks[static_cast<size_t>(chunkY) * m_chunkCountX + chunkX];
    }
    const MapChunk* GetChunk(int chunkX, int chunkY) const {
        return const_cast<Map*>(this)->GetChunk(chunkX, chunkY);
    }

private:
    void RebuildChunkGrid();

    std::string m_name;
    int m_tileSize = 64;
    int m_width = 0;
    int m_height = 0;

    std::unique_ptr<TileLayer> m_ground;
    std::unique_ptr<ObjectLayer> m_objects;
    std::unique_ptr<CollisionLayer> m_collision;
    std::unique_ptr<OcclusionLayer> m_occlusion;

    std::vector<MapChunk> m_chunks;
    int m_chunkCountX = 0;
    int m_chunkCountY = 0;
};

} // namespace legend::map
