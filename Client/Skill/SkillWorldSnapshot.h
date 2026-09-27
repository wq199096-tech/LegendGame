#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "Client/Loot/GroundLoot.h"
#include "Client/Progression/PlayerProgression.h"
#include "Client/World/AggroTable.h"
#include "Engine/Combat/CombatStats.h"
#include "Engine/Entity/EntityId.h"
#include "Engine/Math/Vector2.h"

namespace legend::skill {

// 阶段8.1/8.2：Skill Check / Auto Skill Test 的世界快照。
// 用途：Integration Check 与 Auto Skill Test 在开始前 Capture、结束时（含 FAIL/timeout
// 路径）Restore——保证普通游戏世界不被验收代码污染。
// 阶段8.2 扩展：Player Progression 完整快照（level/currentExp/totalExp，指令四）+
// Base Stats（指令五）+ Monster AI（AggroTable/AIState/AITarget，指令十三/十五）。
struct SkillWorldSnapshot {
    struct ActorState {
        legend::entity::EntityId id = 0;
        legend::math::Vector2 position{0.0f, 0.0f};
        float hp = 0.0f;
        bool active = true;
        std::uint8_t actionState = 0; // legend::entity::CharacterActionState 序号
        // ---- 阶段8.2：Monster AI / Aggro（NPC 时 hasAI=false、字段留空，指令十五） ----
        bool hasAI = false;
        int aiState = 0; // legend::world::MonsterAIState 序号
        legend::entity::EntityId aiTargetId = 0; // 0 = 无目标
        legend::world::AggroSnapshot aggro;      // EntityId -> threat 全量
    };

    // Player
    legend::math::Vector2 playerPosition{0.0f, 0.0f};
    float playerHp = 0.0f;
    int playerLevel = 0;
    legend::progression::ExperienceValue playerTotalExp = 0;
    // ---- 阶段8.2：完整 Progression 快照（level/currentExp/totalExp，指令四）----
    legend::progression::PlayerProgressionSnapshot progression;
    // ---- 阶段8.2：Base Stats 快照（升级会改 base，必须恢复，指令五）----
    legend::combat::CombatStats playerBaseStats;

    // World
    bool captured = false; // CaptureSkillWorldSnapshot 已执行（Restore 的空操作守卫）
    int aliveMonsters = 0;
    std::size_t groundLootCount = 0;
    std::vector<ActorState> actors;                    // Monster + NPC 全量
    std::vector<legend::world::LootEntityId> groundLootIds; // Restore 时只清理新增
};

} // namespace legend::skill
