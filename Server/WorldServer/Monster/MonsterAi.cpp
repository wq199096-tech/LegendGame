#include "Server/WorldServer/Monster/MonsterAi.h"

#include "Engine/Debug/Logger.h"

#include <algorithm>
#include <cmath>

namespace legend::world {
namespace {

constexpr float kPi = 3.14159265358979f;

float DistanceSquared(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

void ClampToMap(float& x, float& y) {
    // 指令四十七：位置必须 Clamp 0~2000。
    if (!(x >= kMapMinX)) {
        x = kMapMinX;
    } else if (x > kMapMaxX) {
        x = kMapMaxX;
    }
    if (!(y >= kMapMinY)) {
        y = kMapMinY;
    } else if (y > kMapMaxY) {
        y = kMapMaxY;
    }
}

// 朝目标直线移动一步；返回是否到达（距离 <= kMonsterArriveDistance）。
bool StepToward(MonsterEntity& monster, float targetX, float targetY, float speed, float dt) {
    const float dx = targetX - monster.PositionX();
    const float dy = targetY - monster.PositionY();
    const float distSq = dx * dx + dy * dy;
    const float arriveSq = kMonsterArriveDistance * kMonsterArriveDistance;
    if (distSq <= arriveSq) {
        return true; // 已在到达范围（指令三十五/四十六）
    }
    const float dist = std::sqrt(distSq);
    float step = speed * dt;
    if (step > dist) {
        step = dist; // 不越过目标
    }
    float newX = monster.PositionX() + dx / dist * step;
    float newY = monster.PositionY() + dy / dist * step;
    ClampToMap(newX, newY);
    monster.SetPosition(newX, newY);
    return (DistanceSquared(monster.PositionX(), monster.PositionY(), targetX, targetY) <=
            arriveSq);
}

void TransitionTo(MonsterEntity& monster, MonsterState state, const char* reason) {
    LOG_DEBUG(std::string("[Monster] #") + std::to_string(monster.EntityId()) + " state " +
              MonsterStateName(static_cast<std::uint8_t>(monster.State())) + " -> " +
              MonsterStateName(static_cast<std::uint8_t>(state)) + " (" + reason + ")");
    monster.SetState(state);
    monster.TouchStateEnterTime();
}

} // namespace

// 指令三十六~三十九：Aggro 扫描——aggroRadius 内最近玩家，同距离 characterId 最小。
// 查询经玩家 SpatialGrid（指令三十七：禁止全服遍历）。
std::shared_ptr<PlayerSession> SelectAggroTarget(const MonsterEntity& monster,
                                                 const MonsterDefinition& definition,
                                                 const WorldSpatialGrid& playerGrid) {
    auto candidates = playerGrid.QueryNearbyPlayers(monster.PositionX(), monster.PositionY(),
                                                    definition.aggroRadius, 0);
    std::shared_ptr<PlayerSession> best;
    float bestDistSq = 0.0f;
    for (const auto& candidate : candidates) {
        if (!candidate.player || candidate.player->MapId() != monster.MapId()) {
            continue;
        }
        const bool better = best == nullptr || candidate.distanceSquared < bestDistSq ||
                            (candidate.distanceSquared == bestDistSq &&
                             candidate.player->CharacterId() < best->CharacterId());
        if (better) {
            best = candidate.player;
            bestDistSq = candidate.distanceSquared;
        }
    }
    return best;
}

void StepMonsterAi(MonsterEntity& monster, const MonsterDefinition& definition, float deltaTime,
                   const WorldSpatialGrid& playerGrid, const WorldManager& players) {
    if (!monster.IsActive()) {
        return;
    }
    // 指令四十一：server tick dt clamp 最大 0.25s（防卡顿瞬移）。
    float dt = deltaTime;
    if (!(dt > 0.0f)) {
        dt = 0.0f;
    }
    if (dt > kMaxMonsterAiDeltaTime) {
        dt = kMaxMonsterAiDeltaTime;
    }
    monster.TouchAiTime();
    const auto now = std::chrono::steady_clock::now();

    switch (monster.State()) {
        case MonsterState::Idle: {
            // 指令三十二：Idle 持续 2 秒 -> Patrol。
            const float elapsed =
                std::chrono::duration<float>(now - monster.StateEnterTime()).count();
            if (elapsed >= kMonsterIdleDurationSeconds) {
                // 指令三十三/三十四：确定性目标点（entityId + patrolSequence 决定角度/半径，
                // 不用不可控 rand）。
                monster.AdvancePatrolSequence();
                const std::uint32_t seed =
                    static_cast<std::uint32_t>(monster.EntityId() * 137ull +
                                               monster.PatrolSequence() * 79ull);
                const float angle = static_cast<float>(seed % 360u) * kPi / 180.0f;
                const std::uint32_t radiusSeed =
                    static_cast<std::uint32_t>(monster.EntityId() * 31ull +
                                               monster.PatrolSequence() * 17ull);
                const float radius =
                    definition.patrolRadius *
                    (0.5f + static_cast<float>(radiusSeed % 50u) / 100.0f); // 50%~100%
                float targetX = monster.SpawnX() + std::cos(angle) * radius;
                float targetY = monster.SpawnY() + std::sin(angle) * radius;
                ClampToMap(targetX, targetY);
                monster.SetPatrolTarget(targetX, targetY);
                TransitionTo(monster, MonsterState::Patrol, "idle timeout");
            }
            break;
        }
        case MonsterState::Patrol: {
            // 指令三十六/三十九：Patrol 中持续 aggro 扫描。
            auto target = SelectAggroTarget(monster, definition, playerGrid);
            if (target) {
                monster.SetTargetCharacterId(target->CharacterId());
                TransitionTo(monster, MonsterState::Chase, "target acquired");
                break;
            }
            // 指令三十五：朝 patrol 目标点移动，到达(<=10) -> Idle。
            if (StepToward(monster, monster.PatrolTargetX(), monster.PatrolTargetY(),
                           definition.moveSpeed, dt)) {
                TransitionTo(monster, MonsterState::Idle, "patrol arrived");
            }
            break;
        }
        case MonsterState::Chase: {
            // 指令四十四：目标玩家离线 -> 立刻 Returning。
            auto target = players.FindByCharacter(monster.TargetCharacterId());
            if (!target) {
                monster.SetTargetCharacterId(0);
                TransitionTo(monster, MonsterState::Returning, "target disconnected");
                break;
            }
            // 指令四十二：Leash——离 spawn > leashRadius(600) -> Returning。
            const float distSpawnSq =
                DistanceSquared(monster.PositionX(), monster.PositionY(), monster.SpawnX(),
                                monster.SpawnY());
            if (distSpawnSq > definition.leashRadius * definition.leashRadius) {
                monster.SetTargetCharacterId(0);
                TransitionTo(monster, MonsterState::Returning, "leash");
                break;
            }
            // 指令四十三：目标离怪物 > aggroRadius*1.5(525) -> Returning。
            const float distTargetSq = DistanceSquared(monster.PositionX(), monster.PositionY(),
                                                       target->PositionX(), target->PositionY());
            const float lostRadius = definition.aggroRadius * 1.5f;
            if (distTargetSq > lostRadius * lostRadius) {
                monster.SetTargetCharacterId(0);
                TransitionTo(monster, MonsterState::Returning, "target lost");
                break;
            }
            // 指令四十：朝玩家服务器权威位置移动（speed=80，dt 为 server tick；不用 Client dt）。
            (void)StepToward(monster, target->PositionX(), target->PositionY(),
                             definition.moveSpeed, dt);
            break;
        }
        case MonsterState::Returning: {
            // 指令四十五/四十六：返回 spawn；到达(<=10) -> Idle(target=0)。
            // 回途不做 aggro 扫描（回途无敌，保证回到出生点语义稳定）。
            if (StepToward(monster, monster.SpawnX(), monster.SpawnY(), definition.moveSpeed,
                           dt)) {
                monster.SetTargetCharacterId(0);
                TransitionTo(monster, MonsterState::Idle, "returned home");
            }
            break;
        }
    }
}

} // namespace legend::world
