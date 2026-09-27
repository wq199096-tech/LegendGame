#pragma once

#include <string>
#include <unordered_map>
#include <vector>

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
};

// Assets/Monsters/monster.json -> 模板表（version 1）。
// 失败返回 false + 明确日志，不崩溃；out 不含无效条目。
bool LoadMonsterRegistry(const std::string& filePath,
                         std::unordered_map<std::string, MonsterDefinition>& out);

} // namespace legend::world
