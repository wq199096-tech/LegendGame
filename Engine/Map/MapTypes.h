#pragma once

#include <cmath>
#include <cstdint>

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
