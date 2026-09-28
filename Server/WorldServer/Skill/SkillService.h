#pragma once

#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Server/WorldServer/Monster/MonsterSpatialGrid.h"
#include "Shared/Skill/SkillDefinition.h"
#include "Shared/Skill/SkillTypes.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段15 指令三十五/三十六：SkillService —— 技能规则纯逻辑（无 IO/无网络状态）。
// WorldServer 负责编排（接收 SkillCastRequest -> 验证 -> 扣 Mana -> 广播）；
// WorldSession 只做协议状态（指令三十六）；所有验证基于服务器权威数据
//（指令九十五：CD 全部 steady_clock；指令九十六：Definition 只来自 Registry）。
// ---------------------------------------------------------------------------

// 指令三十五：一次施法的验证上下文（纯数据，纯函数可测——真实链路无法构造的
// 场景在此覆盖）。验证链顺序（指令三十九）：
//   CasterDead -> NotInWorld -> AlreadyCasting -> TargetDead -> InvalidTarget ->
//   DifferentMap -> OutOfRange -> Cooldown -> NotEnoughMana -> Success
struct SkillCastContext {
    bool casterAlive = true;
    bool casterInWorld = true;
    bool alreadyCasting = false;   // 指令十五：Casting 期间拒绝一切新技能
    SkillTargetType requestTargetType = SkillTargetType::None;
    SkillTargetType definitionTargetType = SkillTargetType::None;
    bool targetAlive = true;
    bool targetVisible = true;     // 指令四十九：目标必须在 visibleMonsters
    bool sameMap = true;
    float distanceSquared = 0.0f;  // 服务器权威位置
    float rangeSquared = 0.0f;
    bool cooldownReady = true;
    std::uint32_t currentMana = 0;
    std::uint32_t manaCost = 0;
};

SkillResultCode ValidateSkillCast(const SkillCastContext& context);

// 指令三十七：技能伤害公式 damage = max(1, baseDamage + attackPower - defense)。
// 不随机、不暴击。QuickStrike 30+20-2=48 / FireBolt 40+20-2=58 / Whirlwind
// 25+20-2=43；普通攻击继续走阶段14 CalculateDamage（指令三十八）。
std::uint32_t CalculateSkillDamage(std::uint32_t baseDamage, std::uint32_t attackPower,
                                   std::uint32_t defense);

// 指令四十四/四十五/四十七：AOE 目标解析。
// candidates 必须来自 MonsterSpatialGrid::QueryNearbyMonsters（指令四十四：
// 禁止遍历全部 Monster）；过滤 alive=false（指令四十七）与跨地图；按
// distanceSquared 升序、entityId 升序（指令四十五/一百二十二：稳定排序）；
// 最多 maxTargets（<= kSkillImpactMaxTargets）。
std::vector<std::shared_ptr<MonsterEntity>> ResolveAoeTargets(
    const std::vector<MonsterAoiCandidate>& candidates, std::uint16_t casterMapId,
    float radius, std::uint32_t maxTargets);

} // namespace legend::world
