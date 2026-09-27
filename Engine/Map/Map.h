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

    // ---- 物件生成/移除（自动维护 Object Collision 引用计数） ----
    MapObject& SpawnObject(MapObject object);
    bool DespawnObject(uint32_t objectId);
    void RebuildObjectBlockCounts();

    // ---- 碰撞：三源合成 ----
    // Terrain（Water 地形）OR Manual（collision 层数据）OR Object（blocking 物件 footprint）
    bool IsTileBlocked(int tileX, int tileY) const;
    // 碰撞来源 bitmask（Terrain=1 / Manual=2 / Object=4），用于测试、日志与未来编辑器
    uint8_t GetCollisionFlags(int tileX, int tileY) const;
    bool IsWorldBlocked(float worldX, float worldY) const {
        return IsTileBlocked(WorldToTileIndex(worldX, static_cast<float>(m_tileSize)),
                             WorldToTileIndex(worldY, static_cast<float>(m_tileSize)));
    }

    // ---- 世界角色出生数据（可选字段；旧地图为空，编辑器保存时原样写回） ----
    const std::vector<MapSpawnArea>& GetMonsterSpawns() const { return m_monsterSpawns; }
    std::vector<MapSpawnArea>& GetMonsterSpawns() { return m_monsterSpawns; }
    const std::vector<MapNPCSpawn>& GetNPCSpawns() const { return m_npcSpawns; }
    std::vector<MapNPCSpawn>& GetNPCSpawns() { return m_npcSpawns; }

    // ---- Chunk 网格 ----
    int GetChunkCountX() const { return m_chunkCountX; }
    int GetChunkCountY() const { return m_chunkCountY; }
    // 帧开始时重置全部 Chunk 可见状态（保证 visible 真实反映当前帧）
    void ResetChunkVisibility();
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
    void AdjustObjectBlockCount(const MapObject& object, int delta);
    uint16_t GetObjectBlockCount(int tileX, int tileY) const {
        if (tileX < 0 || tileY < 0 || tileX >= m_width || tileY >= m_height) {
            return 0;
        }
        return m_objectBlockCounts[static_cast<size_t>(tileY) * m_width + tileX];
    }

    std::string m_name;
    int m_tileSize = 64;
    int m_width = 0;
    int m_height = 0;

    std::unique_ptr<TileLayer> m_ground;
    std::unique_ptr<ObjectLayer> m_objects;
    std::unique_ptr<CollisionLayer> m_collision;   // 仅存 Manual 碰撞
    std::unique_ptr<OcclusionLayer> m_occlusion;

    std::vector<uint16_t> m_objectBlockCounts; // Object Collision 引用计数（同格多物件叠加）
    std::vector<MapChunk> m_chunks;
    int m_chunkCountX = 0;
    int m_chunkCountY = 0;

    // 世界角色出生数据（非图层，随 map.json 顶层字段保存）
    std::vector<MapSpawnArea> m_monsterSpawns;
    std::vector<MapNPCSpawn> m_npcSpawns;
};

} // namespace legend::map
