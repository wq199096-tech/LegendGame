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
    m_objectBlockCounts.assign(static_cast<size_t>(m_width) * m_height, 0);

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

MapObject& Map::SpawnObject(MapObject object) {
    MapObject& stored = m_objects->AddObject(std::move(object));
    if (stored.blocking) {
        AdjustObjectBlockCount(stored, +1);
    }
    return stored;
}

bool Map::DespawnObject(uint32_t objectId) {
    MapObject* object = m_objects->FindObject(objectId);
    if (object == nullptr) {
        return false;
    }
    if (object->blocking) {
        AdjustObjectBlockCount(*object, -1);
    }
    m_objects->RemoveObject(objectId);
    return true;
}

void Map::AdjustObjectBlockCount(const MapObject& object, int delta) {
    const float tileSize = static_cast<float>(m_tileSize);
    // 内缩 1 单位，避免边缘恰好相邻的 Tile 被误阻挡
    const int x0 = WorldToTileIndex(object.x - object.width * 0.5f + 1.0f, tileSize);
    const int x1 = WorldToTileIndex(object.x + object.width * 0.5f - 1.0f, tileSize);
    const int y0 = WorldToTileIndex(object.y - object.height * 0.5f + 1.0f, tileSize);
    const int y1 = WorldToTileIndex(object.y + object.height * 0.5f - 1.0f, tileSize);
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            if (tx < 0 || ty < 0 || tx >= m_width || ty >= m_height) {
                continue;
            }
            auto& count = m_objectBlockCounts[static_cast<size_t>(ty) * m_width + tx];
            const int value = static_cast<int>(count) + delta;
            count = static_cast<uint16_t>(value > 0 ? value : 0);
        }
    }
}

void Map::RebuildObjectBlockCounts() {
    m_objectBlockCounts.assign(static_cast<size_t>(m_width) * m_height, 0);
    for (const auto& object : m_objects->Objects()) {
        if (object.blocking) {
            AdjustObjectBlockCount(object, +1);
        }
    }
}

bool Map::IsTileBlocked(int tileX, int tileY) const {
    // 越界视为阻挡（地图边界安全）
    if (!m_collision->IsValidPosition(tileX, tileY)) {
        return true;
    }
    // 1. Terrain：Water 地形默认阻挡
    if (m_ground->GetTile(tileX, tileY) == static_cast<uint16_t>(TileId::Water)) {
        return true;
    }
    // 2. Manual：collision 层数据（仅人工标记）
    if (m_collision->IsBlocked(tileX, tileY)) {
        return true;
    }
    // 3. Object：blocking 物件 footprint（引用计数，删除单件不影响其他来源）
    if (GetObjectBlockCount(tileX, tileY) > 0) {
        return true;
    }
    return false;
}

void Map::ResetChunkVisibility() {
    for (auto& chunk : m_chunks) {
        chunk.SetVisible(false);
    }
}

} // namespace legend::map
