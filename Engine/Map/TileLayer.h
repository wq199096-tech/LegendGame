#pragma once

#include <cstdint>
#include <vector>

#include "Engine/Map/MapLayer.h"

namespace legend::map {

// Tile 数据层：地表 Tile 网格，纯数据，不产生 GameObject
class TileLayer : public MapLayer {
public:
    TileLayer();

    void SetSize(int width, int height, uint16_t fillValue = 0);
    void SetData(std::vector<uint16_t>&& tiles, int width, int height);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    bool IsValidPosition(int tileX, int tileY) const {
        return tileX >= 0 && tileY >= 0 && tileX < m_width && tileY < m_height;
    }

    // 越界返回 Empty(0)
    uint16_t GetTile(int tileX, int tileY) const {
        if (!IsValidPosition(tileX, tileY)) {
            return 0;
        }
        return m_tiles[static_cast<size_t>(tileY) * m_width + tileX];
    }

    void SetTile(int tileX, int tileY, uint16_t id) {
        if (!IsValidPosition(tileX, tileY)) {
            return;
        }
        m_tiles[static_cast<size_t>(tileY) * m_width + tileX] = id;
    }

    const std::vector<uint16_t>& Data() const { return m_tiles; }

private:
    std::vector<uint16_t> m_tiles;
    int m_width = 0;
    int m_height = 0;
};

} // namespace legend::map
