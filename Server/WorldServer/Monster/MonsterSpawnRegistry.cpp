#include "Server/WorldServer/Monster/MonsterSpawnRegistry.h"

#include "Server/WorldServer/Map/MapRegistry.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/WorldData/WorldDataJson.h"

namespace legend::world {

const MonsterSpawnRegistry& MonsterSpawnRegistry::Instance() {
    static const MonsterSpawnRegistry registry;
    return registry;
}

MonsterSpawnRegistry& MonsterSpawnRegistry::Mutable() {
    // Instance 返回同一实例的 const 引用；Load* 通过 Mutable 注入数据。
    return const_cast<MonsterSpawnRegistry&>(Instance());
}

const MonsterSpawnDefinition* MonsterSpawnRegistry::FindSpawn(std::uint32_t spawnId) const {
    for (const auto& spawn : m_spawns) {
        if (spawn.spawnId == spawnId) {
            return &spawn;
        }
    }
    return nullptr;
}

bool MonsterSpawnRegistry::ValidateSpawns(const MapRegistry& maps, std::string& error) const {
    for (const auto& spawn : m_spawns) {
        if (spawn.monsterDefinitionId != kTrainingSlimeTypeId) {
            // 阶段22 单怪物类型（阶段23 数据驱动 Monster Definition 后放开）。
            error = "spawn " + std::to_string(spawn.spawnId) + ": unknown monsterDefinitionId " +
                    std::to_string(spawn.monsterDefinitionId);
            return false;
        }
        const MapDefinition* map = maps.FindMap(spawn.mapId);
        if (map == nullptr) {
            error = "spawn " + std::to_string(spawn.spawnId) + ": map " +
                    std::to_string(spawn.mapId) + " does not exist";
            return false;
        }
        const float minX = spawn.centerX - spawn.radius;
        const float maxX = spawn.centerX + spawn.radius;
        const float minY = spawn.centerY - spawn.radius;
        const float maxY = spawn.centerY + spawn.radius;
        if (minX < map->minX || maxX > map->maxX || minY < map->minY || maxY > map->maxY) {
            error = "spawn " + std::to_string(spawn.spawnId) + ": zone out of map bounds";
            return false;
        }
    }
    return true;
}

void MonsterSpawnRegistry::LoadFromDefinitions(std::vector<MonsterSpawnDefinition> spawns) {
    Mutable().m_spawns = std::move(spawns);
}

void MonsterSpawnRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultWorldData().monsterSpawns);
}

} // namespace legend::world
