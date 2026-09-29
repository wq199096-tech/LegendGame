#include "Server/WorldServer/Map/MapRegistry.h"

#include <cmath>

namespace legend::world {

const MapRegistry& MapRegistry::Instance() {
    static const MapRegistry registry;
    return registry;
}

MapRegistry::MapRegistry() {
    // 指令三：Map1 Greenfield Village（Town；阶段20 的 4 NPC 保留在此图）。
    MapDefinition map1;
    map1.mapId = 1;
    map1.name = "Greenfield Village";
    map1.type = MapType::Town;
    map1.minX = 0.0f;
    map1.minY = 0.0f;
    map1.maxX = 2000.0f;
    map1.maxY = 2000.0f;
    map1.spawnX = 300.0f;
    map1.spawnY = 300.0f;
    map1.respawnX = 300.0f;
    map1.respawnY = 300.0f;
    m_maps.push_back(map1);

    // 指令三：Map2 Slime Meadow（Field；Training Slime x20）。
    MapDefinition map2;
    map2.mapId = 2;
    map2.name = "Slime Meadow";
    map2.type = MapType::Field;
    map2.minX = 0.0f;
    map2.minY = 0.0f;
    map2.maxX = 2000.0f;
    map2.maxY = 2000.0f;
    map2.spawnX = 200.0f;
    map2.spawnY = 500.0f;
    map2.respawnX = 200.0f;
    map2.respawnY = 500.0f;
    m_maps.push_back(map2);

    // 指令三：Map3 Ancient Ruins（Field；Training Slime x10）。
    MapDefinition map3;
    map3.mapId = 3;
    map3.name = "Ancient Ruins";
    map3.type = MapType::Field;
    map3.minX = 0.0f;
    map3.minY = 0.0f;
    map3.maxX = 2400.0f;
    map3.maxY = 1800.0f;
    map3.spawnX = 200.0f;
    map3.spawnY = 300.0f;
    map3.respawnX = 200.0f;
    map3.respawnY = 300.0f;
    m_maps.push_back(map3);
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

} // namespace legend::world
