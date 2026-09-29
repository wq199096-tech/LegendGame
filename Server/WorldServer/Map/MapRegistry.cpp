#include "Server/WorldServer/Map/MapRegistry.h"

#include "Shared/WorldData/WorldDataJson.h"

#include <cmath>

namespace legend::world {

const MapRegistry& MapRegistry::Instance() {
    static const MapRegistry registry;
    return registry;
}

MapRegistry& MapRegistry::Mutable() {
    // Instance 返回同一实例的 const 引用；Load* 通过 Mutable 注入数据。
    return const_cast<MapRegistry&>(Instance());
}

MapRegistry::MapRegistry() {
    // 阶段22 22.11：硬编码迁入 Data/World（出厂数据由 MakeDefaultWorldData 提供，
    // WorldServer::Initialize 启动时统一 LoadDefaults/LoadFromJsonDir）。
}

const MapDefinition* MapRegistry::FindMap(std::uint16_t mapId) const {
    for (const auto& map : m_maps) {
        if (map.mapId == mapId) {
            return &map;
        }
    }
    return nullptr;
}

void MapRegistry::ClampToMap(std::uint16_t mapId, float& x, float& y) const {
    const MapDefinition* map = FindMap(mapId);
    if (map == nullptr) {
        map = TownMap();
    }
    if (map != nullptr) {
        map->Clamp(x, y);
    }
}

bool MapRegistry::ValidateMaps(std::string& error) const {
    for (std::size_t i = 0; i < m_maps.size(); ++i) {
        const MapDefinition& map = m_maps[i];
        for (std::size_t j = i + 1; j < m_maps.size(); ++j) {
            if (m_maps[j].mapId == map.mapId) {
                error = "duplicate mapId " + std::to_string(map.mapId);
                return false;
            }
        }
        if (!(map.minX < map.maxX) || !(map.minY < map.maxY)) {
            error = "map " + std::to_string(map.mapId) + " invalid bounds";
            return false;
        }
        if (!map.InBounds(map.spawnX, map.spawnY)) {
            error = "map " + std::to_string(map.mapId) + " spawn out of bounds";
            return false;
        }
        if (!map.InBounds(map.respawnX, map.respawnY)) {
            error = "map " + std::to_string(map.mapId) + " respawn out of bounds";
            return false;
        }
    }
    return true;
}

void MapRegistry::LoadFromDefinitions(std::vector<MapDefinition> maps) {
    Mutable().m_maps = std::move(maps);
}

void MapRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultWorldData().maps);
}

} // namespace legend::world
