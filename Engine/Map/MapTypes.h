#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace legend::map {

// Tile ID 定义（未来扩展为 Tileset Atlas + UV Rect，不与 OpenGL 纹理强绑定）
enum class TileId : uint16_t {
    Empty = 0,
    Grass = 1,
    Dirt = 2,
    Stone = 3,
    Water = 4,
};

constexpr int kMapFormatVersion = 1;
constexpr int kChunkSizeTiles = 16; // 每 Chunk 16x16 Tile

// 世界角色出生数据（map.json 顶层 monsterSpawns / npcSpawns，阶段4新增；
// 旧地图无此字段时保持为空数组，不报错）

// 怪物出生区域：在 center 附近 radius 范围内找合法点生成 count 只指定模板怪物
struct MapSpawnArea {
    uint32_t id = 0;
    std::string monsterId; // monster.json 模板 id，如 "slime"
    float x = 0.0f;        // center（世界坐标）
    float y = 0.0f;
    int count = 0;   // 生成数量
    float radius = 0.0f; // 随机分布半径
};

// NPC 出生点：静态站立角色，direction 为固定朝向
struct MapNPCSpawn {
    std::string name;         // 显示名，如 "Guard"
    std::string characterPath; // character.json 相对 Assets 根路径
    float x = 0.0f;
    float y = 0.0f;
    std::string direction = "south";
};

// 碰撞来源 bitmask（GetCollisionFlags 返回值）
enum CollisionSourceFlags : uint8_t {
    CollisionSourceTerrain = 1, // 地形派生（Water 等）
    CollisionSourceManual = 2,  // collision 层人工标记
    CollisionSourceObject = 4,  // blocking 物件 footprint
};

// ---- 坐标工具（负数/越界安全：一律使用 floor 除法） ----

inline int FloorDiv(int a, int b) {
    return (a >= 0) ? (a / b) : -((-a + b - 1) / b);
}

inline int FloorMod(int a, int b) {
    return a - FloorDiv(a, b) * b;
}

struct TilePoint {
    int x = 0;
    int y = 0;
};

// 世界坐标 -> Tile 索引（Tile 左上角为 tile* tileSize）
inline int WorldToTileIndex(float worldCoord, float tileSize) {
    return static_cast<int>(std::floor(worldCoord / tileSize));
}

inline TilePoint WorldToTile(float worldX, float worldY, float tileSize) {
    return {WorldToTileIndex(worldX, tileSize), WorldToTileIndex(worldY, tileSize)};
}

// Tile 索引 -> 世界坐标（左上角）
inline float TileToWorldMin(int tileIndex, float tileSize) {
    return static_cast<float>(tileIndex) * tileSize;
}

// Tile 索引 -> 世界坐标（中心）
inline float TileToWorldCenter(int tileIndex, float tileSize) {
    return static_cast<float>(tileIndex) * tileSize + tileSize * 0.5f;
}

// Tile 索引 -> Chunk 索引
inline int TileToChunkIndex(int tileIndex) {
    return FloorDiv(tileIndex, kChunkSizeTiles);
}

// 世界坐标 -> Chunk 索引
inline int WorldToChunkIndex(float worldCoord, float tileSize) {
    return TileToChunkIndex(WorldToTileIndex(worldCoord, tileSize));
}

// Chunk 内局部 Tile 索引
inline int TileToChunkLocal(int tileIndex) {
    return FloorMod(tileIndex, kChunkSizeTiles);
}

} // namespace legend::map
