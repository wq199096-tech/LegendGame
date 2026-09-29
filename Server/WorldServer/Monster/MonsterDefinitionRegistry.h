#pragma once

#include "Shared/Monster/MonsterDefinition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段23 23.22：MonsterDefinitionRegistry —— 怪物定义注册表。
// 生产从 Data/Game/monsters.json 加载（WorldServer::Start 统一注入）；
// 目录缺失时用出厂默认（kTrainingSlimeDefinition——迁移源/单一事实）。
// FindMonsterDefinition() 全局函数保留（调用面零改动），内部查本注册表。
// ---------------------------------------------------------------------------
class MonsterDefinitionRegistry {
public:
    static const MonsterDefinitionRegistry& Instance();

    MonsterDefinitionRegistry();

    const MonsterDefinition* Find(std::uint32_t monsterTypeId) const;
    const std::vector<MonsterDefinition>& All() const { return m_definitions; }

    static void LoadFromDefinitions(std::vector<MonsterDefinition> definitions);
    static void LoadDefaults();

private:
    static MonsterDefinitionRegistry& Mutable();

    std::vector<MonsterDefinition> m_definitions;
};

} // namespace legend::world
