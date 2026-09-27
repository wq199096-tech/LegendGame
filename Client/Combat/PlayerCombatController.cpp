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
#include "Engine/Render/CharacterRenderer.h"

#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cmath>

namespace legend::world {

namespace {
constexpr float kTabSelectRange = 1000.0f; // TAB 选怪范围
} // namespace

void PlayerCombatController::Update(PlayerCharacter& player,
                                    const legend::entity::ActorRegistry& registry,
                                    legend::combat::CombatSystem& combat,
                                    const input::InputManager& input, float viewportWidth,
                                    float viewportHeight, float deltaTime) {
    player.TickCooldown(deltaTime);

    // ---- 阶段5.1：失效目标自动清理（死亡/Despawn/inactive） ----
    // Target 非空但 Resolve 无效 -> 立即 ClearTarget，不留失效 EntityId
    if (!m_target.IsEmpty() && !m_target.IsValid(registry)) {
        m_target.ClearTarget();
    }

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
        // 阶段5.1：不长期保存失效 EntityId——死亡/Despawn/inactive 立即清目标
        m_target.ClearTarget();
        return false;
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
        // 阶段5.1：复用渲染器的唯一 pivot 公式（禁止手写另一套数学）
        const auto rect = legend::render::CharacterRenderer::GetSpriteWorldRect(*monster);
        if (worldPoint.x < rect.left || worldPoint.x > rect.right || worldPoint.y < rect.top ||
            worldPoint.y > rect.bottom) {
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