#pragma once

#include <cstdint>
#include <vector>

#include "Client/Loot/GroundLoot.h"
#include "Engine/Entity/EntityId.h"
#include "Engine/Math/Vector2.h"
#include "Engine/Progression/LevelSystem.h"

namespace legend::skill {

// 阶段8.1：Skill Check / Auto Skill Test 的世界快照。
// 用途：Integration Check 与 Auto Skill Test 在开始前 Capture、结束时（含 FAIL/timeout
// 路径）Restore——保证普通游戏世界不被验收代码污染（位置/HP/Active/ActionState/
// Player 位置/HP、测试新增 GroundLoot 清理、Combat recent events 清空）。
struct SkillWorldSnapshot {
    struct ActorState {
        legend::entity::EntityId id = 0;
        legend::math::Vector2 position{0.0f, 0.0f};
        float hp = 0.0f;
        bool active = true;
        std::uint8_t actionState = 0; // legend::entity::CharacterActionState 序号
    };

    // Player
    legend::math::Vector2 playerPosition{0.0f, 0.0f};
    float playerHp = 0.0f;
    int playerLevel = 0;
    legend::progression::ExperienceValue playerTotalExp = 0;

    // World
    bool captured = false; // CaptureSkillWorldSnapshot 已执行（Restore 的空操作守卫）
    int aliveMonsters = 0;
    std::size_t groundLootCount = 0;
    std::vector<ActorState> actors;                    // Monster + NPC 全量
    std::vector<legend::world::LootEntityId> groundLootIds; // Restore 时只清理新增
};

} // namespace legend::skill
