#include "Client/World/MonsterAIController.h"

#include <cmath>

#include "Client/Combat/MonsterCombatController.h"
#include "Client/World/MonsterCharacter.h"
#include "Client/World/SpawnArea.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/Character.h"
#include "Engine/Map/Map.h"

namespace legend::world {

namespace {
constexpr float kLoseTargetMultiplier = 1.5f; // loseTargetRange = aggroRange x 1.5

// 全程使用 DistanceSquared 判定（避免无谓 sqrt）；F3 显示真实距离时才开方
float DistanceSq(const math::Vector2& a, const math::Vector2& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float RandomRange(std::mt19937& rng, float minValue, float maxValue) {
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(rng);
}
} // namespace

void MonsterAIController::Update(MonsterCharacter& monster, const map::Map& map,
                                 const entity::ActorRegistry& registry,
                                 const entity::CharacterController& controller,
                                 combat::CombatSystem& combat, std::mt19937& rng,
                                 float deltaTime) {
    // ---- 战斗状态推进（冷却 / 攻击事件 / 恢复 Normal）——每帧必须执行 ----
    m_combat.Update(monster, registry, combat, deltaTime);

    // ---- Movement Lock：Attacking / HitReact / Dead 禁止 AI 移动 ----
    // 动画仍由 WorldActorManager 驱动；HitReact 打断攻击（事件在 combat 内被丢弃）
    if (monster.GetActionState() != entity::CharacterActionState::Normal) {
        controller.Move(monster, math::Vector2{0.0f, 0.0f}, deltaTime, map);
        return;
    }

    m_context.stateTimer += deltaTime;

    // ---- 感知节流：0.15s 一次，DistanceSquared 判定 ----
    m_context.perceptionTimer -= deltaTime;
    if (m_context.perceptionTimer <= 0.0f) {
        m_context.perceptionTimer = kPerceptionInterval;
        // ReturnHome 途中不重新 Aggro（防止无限来回抖动）；被击伤害仇恨走 OnDamaged
        if (monster.GetAIState() != MonsterAIState::ReturnHome) {
            RunPerception(monster, registry);
        }
    }

    // ---- 目标失效安全检查：Actor 不存在 / inactive / 已死 -> 清目标 ----
    const bool hasValidTarget = monster.GetTargetHandle().IsValid(registry) &&
                                monster.GetTargetHandle().Resolve(registry)->IsCombatAlive();
    if (!hasValidTarget && !monster.GetTargetHandle().IsEmpty()) {
        monster.GetTargetHandle().Clear();
        m_aggro.Clear();
        if (monster.GetAIState() == MonsterAIState::Chase) {
            EnterIdle(monster, rng);
        }
    }

    switch (monster.GetAIState()) {
    case MonsterAIState::Idle: {
        controller.Move(monster, math::Vector2{0.0f, 0.0f}, deltaTime, map);
        if (!monster.GetTargetHandle().IsEmpty()) {
            EnterChase(monster);
        } else if (m_context.stateTimer >= m_context.idleDuration) {
            EnterWander(monster, map, rng);
        }
        break;
    }
    case MonsterAIState::Wander: {
        if (!monster.GetTargetHandle().IsEmpty()) {
            EnterChase(monster);
            break;
        }
        if (!m_context.hasWanderTarget) {
            EnterIdle(monster, rng);
            break;
        }
        // Leash 保护：Wander 不会超范围，但保险检查（DistanceSquared）
        if (DistanceSq(monster.GetPosition(), monster.GetHomePosition()) >
            monster.GetLeashRange() * monster.GetLeashRange()) {
            EnterReturnHome(monster);
            break;
        }
        const math::Vector2 toTarget = m_context.wanderTarget - monster.GetPosition();
        controller.Move(monster, toTarget, deltaTime, map);
        const bool arrived = (toTarget.x * toTarget.x + toTarget.y * toTarget.y) <
                             kArriveDistance * kArriveDistance;
        const bool timeout = m_context.stateTimer >= kWanderTimeout;
        m_context.stuckCheckTimer += deltaTime;
        bool stuck = false;
        if (m_context.stuckCheckTimer >= kStuckCheckInterval) {
            const math::Vector2 delta = monster.GetPosition() - m_context.lastStuckCheckPos;
            stuck = delta.LengthSq() < kStuckDistance * kStuckDistance;
            m_context.lastStuckCheckPos = monster.GetPosition();
            m_context.stuckCheckTimer = 0.0f;
        }
        if (arrived || timeout || stuck) {
            EnterIdle(monster, rng);
        }
        break;
    }
    case MonsterAIState::Chase: {
        // Leash：距 home 超限 -> 清目标回出生点
        if (DistanceSq(monster.GetPosition(), monster.GetHomePosition()) >
            monster.GetLeashRange() * monster.GetLeashRange()) {
            LOG_INFO("[AI] " + monster.GetName() + " leash exceeded (>" +
                     std::to_string(monster.GetLeashRange()) + "), return home.");
            monster.GetTargetHandle().Clear();
            m_aggro.Clear();
            EnterReturnHome(monster);
            break;
        }
        entity::Character* target = monster.GetTargetHandle().Resolve(registry);
        if (target == nullptr || !target->IsCombatAlive()) {
            monster.GetTargetHandle().Clear();
            EnterIdle(monster, rng);
            break;
        }
        const float distSqToTarget = DistanceSq(monster.GetPosition(), target->GetPosition());
        const float attackRange = monster.GetCombatStats().attackRange;
        // 离开 loseTargetRange -> 放弃目标
        const float loseTargetRange = monster.GetAggroRange() * kLoseTargetMultiplier;
        if (distSqToTarget > loseTargetRange * loseTargetRange) {
            monster.GetTargetHandle().Clear();
            m_aggro.Clear();
            EnterIdle(monster, rng);
            break;
        }
        // 攻击范围停步：Chase Combat Stop = attackRange（进入即停步并尝试攻击）
        if (distSqToTarget <= attackRange * attackRange) {
            controller.Move(monster, math::Vector2{0.0f, 0.0f}, deltaTime, map);
            m_combat.RequestAttack(monster, registry, combat); // 冷却/状态内部校验
            break;
        }
        // stop/resume 滞回：贴近（stopDistance）停步，拉开 resume 距离才继续追
        if (distSqToTarget <= monster.GetStopDistance() * monster.GetStopDistance()) {
            m_context.chasePaused = true;
        } else if (distSqToTarget > monster.GetResumeDistance() * monster.GetResumeDistance()) {
            m_context.chasePaused = false;
        }
        if (m_context.chasePaused) {
            controller.Move(monster, math::Vector2{0.0f, 0.0f}, deltaTime, map);
            m_combat.RequestAttack(monster, registry, combat); // 停步贴近时也尝试攻击
        } else {
            const math::Vector2 toTarget = target->GetPosition() - monster.GetPosition();
            controller.Move(monster, toTarget, deltaTime, map);
        }
        break;
    }
    case MonsterAIState::ReturnHome: {
        const math::Vector2 toHome = monster.GetHomePosition() - monster.GetPosition();
        if ((toHome.x * toHome.x + toHome.y * toHome.y) <
            kHomeArriveDistance * kHomeArriveDistance) {
            EnterIdle(monster, rng);
        } else {
            controller.Move(monster, toHome, deltaTime, map);
        }
        break;
    }
    default:
        break;
    }
}

void MonsterAIController::OnDamaged(MonsterCharacter& monster, entity::EntityId sourceId,
                                    float amount) {
    if (!monster.IsCombatAlive()) {
        return; // 尸体不产生仇恨
    }
    // 受到伤害 -> AddThreat(sourceId, damage)：被打必反击（含 ReturnHome 途中）
    m_aggro.AddThreat(sourceId, amount);
    monster.GetTargetHandle().Set(sourceId);
    if (monster.GetAIState() != MonsterAIState::Chase) {
        EnterChase(monster);
        LOG_INFO("[AI] " + monster.GetName() + "#" + std::to_string(monster.GetId()) +
                 " aggroed by damage from " + std::to_string(sourceId));
    }
}

void MonsterAIController::RunPerception(MonsterCharacter& monster,
                                        const entity::ActorRegistry& registry) {
    ++m_context.scanCount;
    const float aggroSq = monster.GetAggroRange() * monster.GetAggroRange();
    for (const entity::Character* player : registry.GetByType(entity::ActorType::Player)) {
        if (player == nullptr || !player->IsActive() || !player->IsCombatAlive()) {
            continue; // 死亡 Player 不进入 Aggro
        }
        const math::Vector2 delta = player->GetPosition() - monster.GetPosition();
        if (delta.LengthSq() <= aggroSq) {
            m_aggro.AddThreat(player->GetId(), 1.0f);
        }
    }
    if (monster.GetTargetHandle().IsEmpty()) {
        const entity::EntityId best = m_aggro.GetHighestThreat();
        if (best != entity::kInvalidEntityId) {
            monster.GetTargetHandle().Set(best);
            LOG_INFO("[AI] " + monster.GetName() + " acquired target (player " +
                     std::to_string(best) + "), enter chase.");
        }
    }
}

void MonsterAIController::EnterIdle(MonsterCharacter& monster, std::mt19937& rng) {
    monster.SetAIState(MonsterAIState::Idle);
    monster.GetTargetHandle().Clear();
    m_context.stateTimer = 0.0f;
    m_context.hasWanderTarget = false;
    m_context.chasePaused = false;
    m_context.idleDuration = RandomRange(rng, monster.GetWanderIntervalMin(),
                                         monster.GetWanderIntervalMax());
}

void MonsterAIController::EnterWander(MonsterCharacter& monster, const map::Map& map,
                                      std::mt19937& rng) {
    math::Vector2 target{0.0f, 0.0f};
    if (SpawnArea::FindWalkablePosition(map, monster.GetFootprint(), monster.GetHomePosition(),
                                        monster.GetWanderRadius(), rng, 8, target)) {
        monster.SetAIState(MonsterAIState::Wander);
        m_context.stateTimer = 0.0f;
        m_context.wanderTarget = target;
        m_context.hasWanderTarget = true;
        m_context.stuckCheckTimer = 0.0f;
        m_context.lastStuckCheckPos = monster.GetPosition();
    } else {
        EnterIdle(monster, rng);
    }
}

void MonsterAIController::EnterChase(MonsterCharacter& monster) {
    monster.SetAIState(MonsterAIState::Chase);
    m_context.stateTimer = 0.0f;
    m_context.chasePaused = false;
}

void MonsterAIController::EnterReturnHome(MonsterCharacter& monster) {
    monster.SetAIState(MonsterAIState::ReturnHome);
    m_context.stateTimer = 0.0f;
    m_context.hasWanderTarget = false;
    m_context.chasePaused = false;
}

} // namespace legend::world