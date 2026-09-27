#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Client/Loot/LootTable.h"
#include "Engine/Combat/CombatStats.h"

namespace legend::world {

// monster.json AI 参数块（版本 1）
struct MonsterAIDefinition {
    float aggroRange = 300.0f;
    float leashRange = 600.0f;
    float wanderRadius = 180.0f;
    float wanderIntervalMin = 2.0f;
    float wanderIntervalMax = 5.0f;
    float stopDistance = 60.0f;
    float resumeDistance = 80.0f;
};

// monster.json 怪物模板定义：数据驱动，禁止把怪物参数写死 C++
struct MonsterDefinition {
    std::string id;      // 如 "slime"
    std::string name;    // 显示名，如 "Slime"
    std::string characterPath; // character.json 相对 Assets 根路径
    MonsterAIDefinition ai;
    combat::CombatStats combat; // monster.json "combat" 块（阶段5数据驱动，不写死 AI）
    int expReward = 0;          // monster.json "rewards".exp（阶段6，>=0）
    std::vector<LootEntry> loot; // monster.json "loot"[]（阶段6，加载时校验非法 entry 剔除）
};

// Assets/Monsters/monster.json -> 模板表（version 1）。
// 失败返回 false + 明确日志，不崩溃；out 不含无效条目。
bool LoadMonsterRegistry(const std::string& filePath,
                         std::unordered_map<std::string, MonsterDefinition>& out);

// 跨字段统一校验（AI+Combat 全部解析完成后调用）：combat 有效性、
// resume>stop、leash>aggro、wanderIntervalMax>=Min、stopDistance<=attackRange+容差。
// 非法返回 false（调用方跳过该模板）。导出供配置校验测试使用。
bool ValidateMonsterDefinition(const MonsterDefinition& definition);

} // namespace legend::world
