#pragma once

#include <cstdint>
#include <vector>

#include "Engine/Map/MapLayer.h"
#include "Engine/Map/MapTypes.h"

namespace legend::map {

// 碰撞层：Tile 级碰撞（0 = 可行走，非 0 = 阻挡），独立于地表数据
class CollisionLayer : public MapLayer {
public:
    CollisionLayer();

    void SetSize(int width, int height);
    void SetData(std::vector<uint8_t>&& tiles, int width, int height);
    void SetTileSize(float tileSize) { m_tileSize = tileSize; }
    float GetTileSize() const { return m_tileSize; }

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    bool IsValidPosition(int tileX, int tileY) const {
        return tileX >= 0 && tileY >= 0 && tileX < m_width && tileY < m_height;
    }

    // 越界视为阻挡（地图边界安全）
    bool IsBlocked(int tileX, int tileY) const {
        if (!IsValidPosition(tileX, tileY)) {
            return true;
        }
        return m_tiles[static_cast<size_t>(tileY) * m_width + tileX] != 0;
    }

    void SetBlocked(int tileX, int tileY, bool blocked) {
        if (!IsValidPosition(tileX, tileY)) {
            return;
        }
        m_tiles[static_cast<size_t>(tileY) * m_width + tileX] = blocked ? 1 : 0;
    }

    // ---- 世界坐标转换 ----

    TilePoint WorldToTile(float worldX, float worldY) const {
        return {WorldToTileIndex(worldX, m_tileSize), WorldToTileIndex(worldY, m_tileSize)};
    }

    // Tile 中心对应的世界坐标
    float TileToWorldX(int tileX) const { return TileToWorldCenter(tileX, m_tileSize); }
    float TileToWorldY(int tileY) const { return TileToWorldCenter(tileY, m_tileSize); }

    bool IsWorldPositionBlocked(float worldX, float worldY) const {
        const TilePoint tile = WorldToTile(worldX, worldY);
        return IsBlocked(tile.x, tile.y);
    }

    const std::vector<uint8_t>& Data() const { return m_tiles; }

private:
    std::vector<uint8_t> m_tiles;
    int m_width = 0;
    int m_height = 0;
    float m_tileSize = 64.0f;
};

} // namespace legend::map
