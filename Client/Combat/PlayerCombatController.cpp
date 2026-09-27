#include "Client/Combat/PlayerCombatController.h"

#include "Client/Character/PlayerCharacter.h"
#include "Engine/Core/Engine.h"
#include "Client/World/MonsterCharacter.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/Character.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Render/Camera2D.h"

#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cmath>

namespace legend::world {

namespace {
constexpr float kTabSelectRange = 1000.0f; // TAB 选怪范围

// 命中检测：鼠标世界点是否在角色视觉 AABB 内（以 Feet + pivot 推导）
bool HitTestVisual(const legend::entity::Character& character, const math::Vector2& worldPoint) {
    const auto& visual = character.GetVisual();
    const math::Vector2& feet = character.GetPosition();
    // 精灵矩形：底边对齐 feet.y，水平中心对齐 feet.x（pivot 语义）
    const float left = feet.x - visual.width * visual.pivot.x;
    const float right = left + visual.width;
    const float top = feet.y - visual.height * (1.0f - visual.pivot.y) - visual.height * visual.pivot.y;
    const float bottom = feet.y + visual.height * visual.pivot.y;
    return worldPoint.x >= left && worldPoint.x <= right && worldPoint.y >= top &&
           worldPoint.y <= bottom;
}
} // namespace

void PlayerCombatController::Update(PlayerCharacter& player,
                                    const legend::entity::ActorRegistry& registry,
                                    legend::combat::CombatSystem& combat,
                                    const input::InputManager& input, float viewportWidth,
                                    float viewportHeight, float deltaTime) {
    player.TickCooldown(deltaTime);

    // ---- 目标选择输入 ----
    if (input.IsKeyPressed(SDL_SCANCODE_TAB)) {
        SelectNearestMonster(player, registry);
    }
    if (input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
        auto& engine = legend::Engine::Get();
        HandleClickSelection(player, registry, engine.GetCamera(), viewportWidth, viewportHeight);
    }

    // ---- 状态推进 ----
    switch (player.GetActionState()) {
    case legend::entity::CharacterActionState::Attacking: {
        ConsumeAttackEvent(player, registry, combat);
        if (player.GetAnimationPlayer().IsFinished()) {
            player.ReturnToNormal(); // 攻击动画播完（不用写死 Timer）
        }
        break;
    }
    case legend::entity::CharacterActionState::HitReact: {
        (void)player.GetAnimationPlayer().ConsumeEvents(); // 受击打断：丢弃遗留攻击事件
        if (player.GetAnimationPlayer().IsFinished()) {
            player.ReturnToNormal();
        }
        break;
    }
    case legend::entity::CharacterActionState::Dead:
    case legend::entity::CharacterActionState::Normal:
    default:
        // 普通攻击输入：Space
        if (player.GetActionState() == legend::entity::CharacterActionState::Normal &&
            input.IsKeyPressed(SDL_SCANCODE_SPACE)) {
            RequestAttack(player, registry, combat);
        }
        break;
    }
}

bool PlayerCombatController::RequestAttack(PlayerCharacter& player,
                                           const legend::entity::ActorRegistry& registry,
                                           legend::combat::CombatSystem& combat) {
    if (!player.IsCombatAlive() ||
        player.GetActionState() != legend::entity::CharacterActionState::Normal) {
        return false;
    }
    if (player.GetAttackCooldownRemaining() > 0.0f) {
        return false; // 冷却中：立即再次请求 Rejected
    }
    auto* target = m_target.Resolve(registry);
    if (target == nullptr) {
        return false; // 无目标 / 目标死亡（死亡目标已自动清理）
    }
    const math::Vector2 delta = target->GetPosition() - player.GetPosition();
    const float range = player.GetCombatStats().attackRange;
    if (delta.LengthSq() > range * range) {
        return false; // 超出攻击距离：不造成伤害（手动走近）
    }
    // 攻击开始：自动面向目标并锁定本次攻击方向
    player.SetDirection(legend::entity::DirectionFromVector(delta, player.GetDirection()));
    player.EnterAttacking();
    player.SetAttackCooldownRemaining(player.GetCombatStats().attackInterval);
    LOG_INFO("[Combat] Player#" + std::to_string(player.GetId()) + " attacks " +
             target->GetName() + "#" + std::to_string(target->GetId()));
    return true;
}

void PlayerCombatController::SelectNearestMonster(
    PlayerCharacter& player, const legend::entity::ActorRegistry& registry) {
    const auto monsters = registry.GetByType(legend::entity::ActorType::Monster);
    const legend::entity::Character* best = nullptr;
    float bestDistSq = kTabSelectRange * kTabSelectRange;
    for (const legend::entity::Character* monster : monsters) {
        if (monster == nullptr || !monster->IsCombatAlive()) {
            continue; // 只选 active + alive 的 Monster
        }
        const math::Vector2 delta = monster->GetPosition() - player.GetPosition();
        const float distSq = delta.LengthSq();
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best = monster;
        }
    }
    if (best != nullptr) {
        m_target.SetTarget(best->GetId());
        LOG_INFO("[Combat] Player selected " + best->GetName() + "#" +
                 std::to_string(best->GetId()));
    } else {
        LOG_INFO("[Combat] TAB: no alive monster in range.");
    }
}

void PlayerCombatController::HandleClickSelection(
    PlayerCharacter& player, const legend::entity::ActorRegistry& registry,
    const legend::render::Camera2D& camera, float viewportWidth, float viewportHeight) {
    const auto& mousePos = legend::Engine::Get().GetInput().GetMousePosition();
    const math::Vector2 worldPoint =
        camera.ScreenToWorld({mousePos.x, mousePos.y}, viewportWidth, viewportHeight);

    const auto monsters = registry.GetByType(legend::entity::ActorType::Monster);
    const legend::entity::Character* best = nullptr;
    float bestDistSq = 0.0f;
    for (const legend::entity::Character* monster : monsters) {
        if (monster == nullptr || !monster->IsCombatAlive()) {
            continue; // NPC / 尸体不可选
        }
        if (!HitTestVisual(*monster, worldPoint)) {
            continue;
        }
        const float distSq = (monster->GetPosition() - worldPoint).LengthSq();
        if (best == nullptr || distSq < bestDistSq) {
            bestDistSq = distSq;
            best = monster;
        }
    }
    if (best != nullptr) {
        m_target.SetTarget(best->GetId());
        LOG_INFO("[Combat] Player selected " + best->GetName() + "#" +
                 std::to_string(best->GetId()));
    } else {
        m_target.ClearTarget(); // 点击空地清目标
    }
}

void PlayerCombatController::ConsumeAttackEvent(
    PlayerCharacter& player, const legend::entity::ActorRegistry& registry,
    legend::combat::CombatSystem& combat) {
    auto events = player.GetAnimationPlayer().ConsumeEvents();
    for (const auto& eventName : events) {
        if (eventName != "attack_hit") {
            continue;
        }
        // 事件时刻重新 Resolve：防延迟一帧后目标已删除/死亡
        auto* target = m_target.Resolve(registry);
        if (target == nullptr) {
            continue; // 攻击落空
        }
        legend::combat::DamageEvent event;
        combat.ResolveAttack(player, *target, event);
    }
}

} // namespace legend::world