#pragma once

#include "Shared/Quest/QuestTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段19 指令八/九/十：QuestDefinition —— 任务/目标/奖励静态定义。
// Definition 只存在于代码（指令六十九：绝不写数据库）；DB 只保存角色任务状态。
// ---------------------------------------------------------------------------

// 目标定义（指令八）：不同类型使用不同字段。
struct QuestObjectiveDefinition {
    std::uint32_t objectiveId = 0;
    QuestObjectiveType type = QuestObjectiveType::KillMonster;
    std::uint32_t targetId = 0;
    // KillMonster: targetId = monsterTypeId
    // CollectItem: targetId = itemDefinitionId
    // ReachLevel:  targetId = 目标等级（指令二十六）
    // ReachArea:   targetId 未用
    std::uint32_t requiredCount = 1; // ReachLevel/ReachArea 固定 1（指令二十六）
    std::uint16_t mapId = 1;         // ReachArea 专用
    float areaX = 0.0f;              // ReachArea 专用
    float areaY = 0.0f;
    float areaRadius = 0.0f;
};

// 奖励定义（指令九）：阶段19 每任务最多 EXP + Gold + 1 种 Item（无数组）。
struct QuestRewardDefinition {
    std::uint32_t exp = 0;
    std::uint32_t gold = 0;
    std::uint32_t itemDefinitionId = 0; // 0 = 无物品奖励
    std::uint32_t itemQuantity = 0;
};

// 任务定义（指令十）：repeatable 阶段19 全部 false。
struct QuestDefinition {
    QuestId questId = 0;
    std::string name;
    std::string description;
    std::uint32_t minLevel = 1;
    QuestId prerequisiteQuestId = 0; // 0 = 无前置
    bool repeatable = false;
    std::vector<QuestObjectiveDefinition> objectives;
    QuestRewardDefinition reward;
};

// ---------------------------------------------------------------------------
// 阶段19 指令五十二：客户端本地展示用名称元数据（仅 5 个固定测试任务）。
// 规则/状态/进度全部服务器权威（QuestRegistry 在 Server 层）；这里只提供
// Quest 名称与目标文案，供 Client F8 Debug 展示（Client 不链接 WorldCore）。
// ---------------------------------------------------------------------------
inline const char* SharedQuestDisplayName(QuestId questId) {
    switch (questId) {
        case 4001: return "Slime Hunter";
        case 4002: return "Core Collector";
        case 4003: return "Growing Warrior";
        case 4004: return "Explorer";
        case 4005: return "Slime Cleanup";
        default: return "Unknown Quest";
    }
}

// 目标展示文案（objectiveId 固定 40011/40021/40031/40041/40051/40052）。
inline const char* SharedObjectiveDisplayLabel(std::uint32_t objectiveId) {
    switch (objectiveId) {
        case 40011: return "Kill Slime";
        case 40021: return "Collect Core";
        case 40031: return "Reach Level";
        case 40041: return "Reach Area";
        case 40051: return "Kill Slime";
        case 40052: return "Collect Core";
        default: return "Objective";
    }
}

} // namespace legend::world
