#include "Client/World/MonsterAIController.h"

#include <cmath>

#include "Client/World/MonsterCharacter.h"
#include "Client/World/SpawnArea.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Map/Map.h"

namespace legend::world {

namespace {
constexpr float kLoseTargetMultiplier = 1.5f; // loseTargetRange = aggroRange x 1.5

float Distance(const math::Vector2& a, const math::Vector2& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

float RandomRange(std::mt19937& rng, float minValue, float maxValue) {
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(rng);
}
} // namespace

void MonsterAIController::Update(MonsterCharacter& monster, const map::Map& map,
                                 const entity::ActorRegistry& registry,
                                 const entity::CharacterController& controller,
                                 std::mt19937& rng, float deltaTime) {
    m_context.stateTimer += deltaTime;

    // ---- 感知节流：0.15s 一次，DistanceSquared 判定 ----
    m_context.perceptionTimer -= deltaTime;
    if (m_context.perceptionTimer <= 0.0f) {
        m_context.perceptionTimer = kPerceptionInterval;
        // ReturnHome 途中不重新 Aggro（防止无限来回抖动）
        if (monster.GetAIState() != MonsterAIState::ReturnHome) {
            RunPerception(monster, registry);
        }
    }

    // ---- 目标失效安全检查：Actor 不存在 / inactive / 太远 -> 清目标 ----
    const bool hasValidTarget = monster.GetTargetHandle().IsValid(registry);
    if (!hasValidTarget && !monster.GetTargetHandle().IsEmpty()) {
        monster.GetTargetHandle().Clear();
        m_aggro.Clear();
        if (monster.GetAIState() == MonsterAIState::Chase) {
            EnterIdle(monster, rng);
        }
    }

    switch (monster.GetAIState()) {
    case MonsterAIState::Idle: {
        // 站立不动；等待随机时间后进入 Wander
        controller.Move(monster, math::Vector2{0.0f, 0.0f}, deltaTime, map);
        if (!monster.GetTargetHandle().IsEmpty()) {
            EnterChase(monster); // 感知到目标立即 Chase
        } else if (m_context.stateTimer >= m_context.idleDuration) {
            EnterWander(monster, map, rng);
        }
        break;
    }
    case MonsterAIState::Wander: {
        // 已有目标 -> 立即 Chase
        if (!monster.GetTargetHandle().IsEmpty()) {
            EnterChase(monster);
            break;
        }
        if (!m_context.hasWanderTarget) {
            EnterIdle(monster, rng);
            break;
        }
        // Leash 保护：Wander 不会超范围，但保险检查
        if (Distance(monster.GetPosition(), monster.GetHomePosition()) >
            monster.GetLeashRange()) {
            EnterReturnHome(monster);
            break;
        }
        // 持续向 wander target 直线移动（非每帧随机方向）+ 地图碰撞
        const math::Vector2 toTarget =
            m_context.wanderTarget - monster.GetPosition();
        controller.Move(monster, toTarget, deltaTime, map);
        // 到达 / 超时 / 持续撞墙 -> 放弃目标回 Idle
        const bool arrived = (toTarget.x * toTarget.x + toTarget.y * toTarget.y) <
                             kArriveDistance * kArriveDistance;
        const bool timeout = m_context.stateTimer >= kWanderTimeout;
        m_context.stuckCheckTimer += deltaTime;
        bool stuck = false;
        if (m_context.stuckCheckTimer >= kStuckCheckInterval) {
            const math::Vector2 delta =
                monster.GetPosition() - m_context.lastStuckCheckPos;
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
        // Leash：距 home 超限 -> 清目标回出生点（怪物不能追遍整张地图）
        if (Distance(monster.GetPosition(), monster.GetHomePosition()) >
            monster.GetLeashRange()) {
            LOG_INFO("[AI] " + monster.GetName() + " leash exceeded (>" +
                     std::to_string(monster.GetLeashRange()) + "), return home.");
            monster.GetTargetHandle().Clear();
            m_aggro.Clear();
            EnterReturnHome(monster);
            break;
        }
        entity::Character* target = monster.GetTargetHandle().Resolve(registry);
        if (target == nullptr) {
            // 目标失效（已在上方统一清理，此处兜底）
            monster.GetTargetHandle().Clear();
            EnterIdle(monster, rng);
            break;
        }
        const float distToTarget = Distance(monster.GetPosition(), target->GetPosition());
        // 离开 loseTargetRange -> 放弃目标
        if (distToTarget > monster.GetAggroRange() * kLoseTargetMultiplier) {
            monster.GetTargetHandle().Clear();
            m_aggro.Clear();
            EnterIdle(monster, rng);
            break;
        }
        // stopDistance / resumeDistance 滞回：贴近后停步，拉开 resume 距离才继续
        if (distToTarget <= monster.GetStopDistance()) {
            m_context.chasePaused = true;
        } else if (distToTarget > monster.GetResumeDistance()) {
            m_context.chasePaused = false;
        }
        if (m_context.chasePaused) {
            controller.Move(monster, math::Vector2{0.0f, 0.0f}, deltaTime, map);
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
            EnterIdle(monster, rng); // 到家
        } else {
            controller.Move(monster, toHome, deltaTime, map);
        }
        break;
    }
    default:
        break;
    }
}

void MonsterAIController::RunPerception(MonsterCharacter& monster,
                                        const entity::ActorRegistry& registry) {
    ++m_context.scanCount;
    const float aggroSq = monster.GetAggroRange() * monster.GetAggroRange();
    for (const entity::Character* player : registry.GetByType(entity::ActorType::Player)) {
        if (!player->IsActive()) {
            continue;
        }
        const math::Vector2 delta = player->GetPosition() - monster.GetPosition();
        if (delta.LengthSq() <= aggroSq) {
            m_aggro.AddThreat(player->GetId(), 1.0f);
        }
    }
    // 目标选择：仇恨最高者
    if (monster.GetTargetHandle().IsEmpty()) {
        const legend::entity::EntityId best = m_aggro.GetHighestThreat();
        if (best != legend::entity::kInvalidEntityId) {
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
    m_context.idleDuration = RandomRange(rng, 2.0f, 4.0f);
}

void MonsterAIController::EnterWander(MonsterCharacter& monster, const map::Map& map,
                                      std::mt19937& rng) {
    // 从出生点附近 wanderRadius 内随机一个目标位置；不可达则留在 Idle
    math::Vector2 target{0.0f, 0.0f};
    if (SpawnArea::FindWalkablePosition(map, monster.GetFootprint(),
                                        monster.GetHomePosition(), monster.GetWanderRadius(),
                                        rng, 8, target)) {
        monster.SetAIState(MonsterAIState::Wander);
        m_context.stateTimer = 0.0f;
        m_context.wanderTarget = target;
        m_context.hasWanderTarget = true;
        m_context.stuckCheckTimer = 0.0f;
        m_context.lastStuckCheckPos = monster.GetPosition();
    } else {
        EnterIdle(monster, rng); // 全部尝试失败：继续等待
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
