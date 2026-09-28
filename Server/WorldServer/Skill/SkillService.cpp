#include "Server/WorldServer/Skill/SkillService.h"

#include <algorithm>

namespace legend::world {

SkillResultCode ValidateSkillCast(const SkillCastContext& context) {
    // 指令九十三：死亡施法者拒绝（不扣 Mana）。
    if (!context.casterAlive) {
        return SkillResultCode::CasterDead;
    }
    if (!context.casterInWorld) {
        return SkillResultCode::NotInWorld;
    }
    // 指令十五：同一时间只能施放一个；Instant 在 Casting 期间同样拒绝。
    if (context.alreadyCasting) {
        return SkillResultCode::AlreadyCasting;
    }
    // 指令四十八：targetType=Player 等未定义类型一律 InvalidTarget（无 PvP）；
    // targetType 与 Definition 不一致（Client 伪造）同样 InvalidTarget。
    if (context.requestTargetType != context.definitionTargetType ||
        context.requestTargetType == SkillTargetType::None) {
        return SkillResultCode::InvalidTarget;
    }
    // 指令十八/四十二：死亡目标不能被技能命中。
    if (context.requestTargetType == SkillTargetType::Monster) {
        if (!context.targetAlive) {
            return SkillResultCode::TargetDead;
        }
        // 指令四十九：目标必须在 visibleMonsters（防远程猜 entityId）。
        if (!context.targetVisible) {
            return SkillResultCode::InvalidTarget;
        }
        // 指令二十：跨地图拒绝。
        if (!context.sameMap) {
            return SkillResultCode::DifferentMap;
        }
        // 指令八/九：距离上限（服务器权威位置）。
        if (context.distanceSquared > context.rangeSquared) {
            return SkillResultCode::OutOfRange;
        }
    }
    // 指令九十：CD 未就绪拒绝（Mana 不扣）。
    if (!context.cooldownReady) {
        return SkillResultCode::Cooldown;
    }
    // 指令八十九：Mana 不足拒绝（CD 不启动）。
    if (context.currentMana < context.manaCost) {
        return SkillResultCode::NotEnoughMana;
    }
    return SkillResultCode::Success;
}

std::uint32_t CalculateSkillDamage(std::uint32_t baseDamage, std::uint32_t attackPower,
                                   std::uint32_t defense) {
    // 指令三十七：base + attackPower - defense，保底 1（阶段15 不随机不暴击）。
    const std::uint32_t attack = baseDamage + attackPower;
    return (attack > defense) ? (attack - defense) : 1u;
}

std::vector<std::shared_ptr<MonsterEntity>> ResolveAoeTargets(
    const std::vector<MonsterAoiCandidate>& candidates, std::uint16_t casterMapId,
    float radius, std::uint32_t maxTargets) {
    // 指令八十五：AOE 最多 16 目标。
    const std::uint32_t limit =
        maxTargets == 0 || maxTargets > kSkillImpactMaxTargets ? kSkillImpactMaxTargets
                                                               : maxTargets;
    const float radiusSquared = radius * radius;
    // 指令四十五：候选 = alive + same map + distance <= radius；先过滤再排序。
    std::vector<MonsterAoiCandidate> filtered;
    filtered.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (candidate.monster == nullptr || !candidate.monster->Alive() ||
            candidate.monster->MapId() != casterMapId ||
            candidate.distanceSquared > radiusSquared) {
            continue; // 指令四十七：alive=false 跳过（AOE 不能打死人）
        }
        filtered.push_back(candidate);
    }
    // 指令四十五/一百二十二：distanceSquared 升序，相同距离 entityId 升序（稳定）。
    std::sort(filtered.begin(), filtered.end(),
              [](const MonsterAoiCandidate& a, const MonsterAoiCandidate& b) {
                  if (a.distanceSquared != b.distanceSquared) {
                      return a.distanceSquared < b.distanceSquared;
                  }
                  return a.monster->EntityId() < b.monster->EntityId();
              });
    std::vector<std::shared_ptr<MonsterEntity>> targets;
    targets.reserve(std::min<std::size_t>(filtered.size(), limit));
    for (const auto& candidate : filtered) {
        if (targets.size() >= limit) {
            break; // 指令八十五：超过上限截断（近者优先，指令一百一十九）
        }
        targets.push_back(candidate.monster);
    }
    return targets;
}

} // namespace legend::world
