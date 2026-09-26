#include "Engine/Map/Map.h"

#include "Engine/Debug/Logger.h"

namespace legend::map {

Map::Map() {
    m_ground = std::make_unique<TileLayer>();
    m_objects = std::make_unique<ObjectLayer>();
    m_collision = std::make_unique<CollisionLayer>();
    m_occlusion = std::make_unique<OcclusionLayer>();
}

void Map::SetSize(int widthTiles, int heightTiles, int tileSize) {
    m_width = widthTiles;
    m_height = heightTiles;
    m_tileSize = tileSize;

    m_ground->SetSize(m_width, m_height, 0);
    m_collision->SetSize(m_width, m_height);
    m_collision->SetTileSize(static_cast<float>(m_tileSize));
    m_objects->Clear();
    m_occlusion->SetOccluderIds({});

    RebuildChunkGrid();
}

void Map::RebuildChunkGrid() {
    m_chunkCountX = (m_width + MapChunk::kSize - 1) / MapChunk::kSize;
    m_chunkCountY = (m_height + MapChunk::kSize - 1) / MapChunk::kSize;
    m_chunks.clear();
    m_chunks.reserve(static_cast<size_t>(m_chunkCountX) * m_chunkCountY);
    for (int cy = 0; cy < m_chunkCountY; ++cy) {
        for (int cx = 0; cx < m_chunkCountX; ++cx) {
            m_chunks.emplace_back(cx, cy);
        }
    }
    LOG_INFO("Map chunk grid rebuilt: " + std::to_string(m_chunkCountX) + " x " +
             std::to_string(m_chunkCountY) + " chunks (" +
             std::to_string(m_width) + "x" + std::to_string(m_height) + " tiles).");
}

bool Map::SetGroundTile(int tileX, int tileY, uint16_t id) {
    if (!m_ground->IsValidPosition(tileX, tileY)) {
        return false;
    }
    m_ground->SetTile(tileX, tileY, id);
    // 标记所属 Chunk dirty，触发渲染缓存重建
    if (MapChunk* chunk = GetChunk(TileToChunkIndex(tileX), TileToChunkIndex(tileY))) {
        chunk->SetDirty(true);
    }
    return true;
}

} // namespace legend::map
