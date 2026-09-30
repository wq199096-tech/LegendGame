#pragma once

#include "Shared/Monster/MonsterTypes.h"

#include <cstdint>
#include <string>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段13 指令四 → 阶段23 23.4/23.17：MonsterDefinition —— 怪物静态配置。
// 阶段22 起从 Server 层移至 Shared（GameData/Editor/Server 三方共用）；
// 阶段23 数据驱动（Data/Game/monsters.json）+ lootTableId 引用 + enabled 开关。
// ---------------------------------------------------------------------------
struct MonsterDefinition {
    std::uint32_t monsterTypeId = 0;
    std::string name;
    std::uint32_t level = 1;
    float moveSpeed = 0.0f;        // units/sec
    float aggroRadius = 0.0f;      // 进入该范围开始 Chase（指令三十六）
    float leashRadius = 0.0f;      // 离 spawn 超过该距离强制 Returning（指令四十二）
    float patrolRadius = 0.0f;     // Patrol 目标点距 spawn 上限（指令三十三）
    float collisionRadius = 0.0f;  // 预留（阶段13 无碰撞，指令四十八）
    std::uint32_t modelId = 0;     // 预留视觉 ID（指令四）
    // 阶段14 指令五：战斗属性
    std::uint32_t maxHp = 0;           // 满血
    std::uint32_t attackPower = 0;     // 攻击力
    std::uint32_t defense = 0;         // 防御力
    float attackRange = 0.0f;          // 普通攻击距离
    float attackCooldownSeconds = 0.0f; // 攻击冷却
    // 阶段17 指令六：击杀奖励（WorldServer 权威发放，Client 不能决定奖励倍率）。
    std::uint32_t rewardExp = 0;
    std::uint32_t rewardGold = 0;
    // 阶段23 23.4/23.17：掉落表引用（0 = 无掉落表）+ 数据驱动开关。
    std::uint32_t lootTableId = 0;
    bool enabled = true;
    // 阶段24：视觉实体引用（visual_entities.json 的 visualId；空 = Client 用 fallback 视觉）。
    // 纯视觉字段——服务器逻辑不使用。
    std::string visualId;
};

// 指令二：Training Slime 基础配置（阶段23 前为唯一硬编码；现为迁移数据源/单一事实）。
// level=1 / moveSpeed=80 / aggroRadius=350 / leashRadius=600 / patrolRadius=180。
// 阶段14 指令五：maxHp=80 / attackPower=10 / defense=2 / attackRange=60 / attackCooldown=1.2s。
// 阶段17 指令六：rewardExp=25 / rewardGold=3。
inline const MonsterDefinition kTrainingSlimeDefinition{
    kTrainingSlimeTypeId, kTrainingSlimeName, 1, 80.0f, 350.0f, 600.0f, 180.0f, 24.0f, 1,
    80u, 10u, 2u, 60.0f, 1.2f,
    25u, 3u,
    1u, true, // lootTableId=1（阶段23 Loot Table V1：Training Slime 表）
};

// 阶段25 指令十：Ancient Slime Guardian（第一只 Boss）。
// HP/攻击显著高于 Training Slime，但 Level 3~5 玩家可单人击杀；移动慢、体型大。
// 复用 Idle/Chase/Attack/Return/Death AI 状态机（不做复杂 Boss AI）。
inline const MonsterDefinition kAncientGuardianDefinition{
    kAncientGuardianTypeId, kAncientGuardianName, 4, 45.0f, 320.0f, 700.0f, 120.0f, 48.0f, 1,
    600u, 22u, 6u, 70.0f, 1.8f,
    300u, 120u,
    2001u, true, // lootTableId=2001（阶段25 指令十二：Boss 掉落表）
};

// 按 typeId 查找定义（阶段13 全局查找签名保留；阶段23 内部走 MonsterDefinitionRegistry）。
const MonsterDefinition* FindMonsterDefinition(std::uint32_t monsterTypeId);

} // namespace legend::world
