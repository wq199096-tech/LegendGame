#pragma once

#include "Shared/Monster/MonsterSpawnDefinition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段22 指令 22.7/22.11：MonsterSpawnRegistry —— 刷怪区配置注册表。
// 生产从 Data/World/monster_spawns.json 加载（WorldServer::Initialize 统一注入）；
// 目录缺失时用 MakeDefaultWorldData 的出厂配置（22.18 迁移源）。
// LoadFromDefinitions/LoadDefaults 为静态注入（Instance 返回 const&，保持调用面零改动）。
// ---------------------------------------------------------------------------
class MonsterSpawnRegistry {
public:
    static const MonsterSpawnRegistry& Instance();

    std::size_t Count() const { return m_spawns.size(); }
    const MonsterSpawnDefinition* FindSpawn(std::uint32_t spawnId) const;
    const std::vector<MonsterSpawnDefinition>& AllSpawns() const { return m_spawns; }

    // 启动校验（monsterDefinitionId 当前仅 Training Slime；zone 完全在地图边界内）。
    bool ValidateSpawns(const class MapRegistry& maps, std::string& error) const;

    // 数据注入（WorldServer::Initialize / 测试 fixture）。
    static void LoadFromDefinitions(std::vector<MonsterSpawnDefinition> spawns);
    static void LoadDefaults();

private:
    static MonsterSpawnRegistry& Mutable();

    std::vector<MonsterSpawnDefinition> m_spawns;
};

} // namespace legend::world
