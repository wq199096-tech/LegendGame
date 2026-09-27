#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/SpriteSheet.h"
#include "Engine/Combat/CombatStats.h"
#include "Engine/Entity/ActorType.h"
#include "Engine/Entity/Direction8.h"
#include "Engine/Entity/Entity.h"
#include "Engine/Math/Vector2.h"

namespace legend::animation {
class AnimationStateMachine;
}

namespace legend::entity {

// 角色脚底碰撞区域（相对 Feet Position 的偏移）
struct CharacterFootprint {
    float width = 28.0f;
    float height = 18.0f;
    float offsetX = 0.0f;
    float offsetY = 4.0f;
};

// 角色视觉尺寸与精灵轴心（pivot：精灵内归一化锚点，脚底点对齐处）
struct CharacterVisual {
    float width = 96.0f;
    float height = 96.0f;
    math::Vector2 pivot{0.5f, 0.85f};
};

// 角色动作状态：阶段5战斗扩展。不要用零散 bool 到处堆。
enum class CharacterActionState : uint8_t {
    Normal = 0,   // 可移动可攻击
    Attacking = 1, // 攻击动画播放中（禁移动，方向锁定）
    HitReact = 2,  // 受击硬直（禁移动，打断攻击）
    Dead = 3,      // 死亡（禁移动/攻击/被锁定；active 保持以播放死亡动画）
};

// 角色基类：移动状态 / 朝向 / 动画状态 / 战斗组件 / 数据。
// 不读取键盘，不做渲染。Player / NPC / Monster / Pet / Summon 复用此类。
class Character : public Entity {
public:
    Character(EntityId id, std::string name, ActorType actorType, float moveSpeed,
              const CharacterFootprint& footprint, const CharacterVisual& visual,
              std::shared_ptr<const std::unordered_map<std::string, animation::AnimationClip>> clips);

    ActorType GetActorType() const { return m_actorType; }

    // 统一动画更新入口：
    // 1) AnimationStateMachine 按最终 actionState/moving/direction 选择正确 Clip
    // 2) AnimationPlayer::Update(deltaTime) 真正推进当前帧（Loop/NonLoop）
    void UpdateAnimation(float deltaTime);

    // ---- 动作状态 ----
    CharacterActionState GetActionState() const { return m_actionState; }
    void SetActionState(CharacterActionState state) { m_actionState = state; }

    // 死亡入口：active 保持 true（播放死亡动画），但停止移动且战斗死亡
    void EnterDead();
    // 受击入口：打断当前攻击（攻击事件若未触发不再产生伤害）
    void EnterHitReact();
    void EnterAttacking() { m_actionState = CharacterActionState::Attacking; }
    void ReturnToNormal() { m_actionState = CharacterActionState::Normal; }

    Direction8 GetDirection() const { return m_direction; }
    void SetDirection(Direction8 direction) { m_direction = direction; }

    const math::Vector2& GetVelocity() const { return m_velocity; }
    void SetVelocity(const math::Vector2& velocity) { m_velocity = velocity; }

    bool IsMoving() const { return m_moving; }
    void SetMoving(bool moving) { m_moving = moving; }

    float GetMoveSpeed() const { return m_moveSpeed; }

    const CharacterFootprint& GetFootprint() const { return m_footprint; }
    const CharacterVisual& GetVisual() const { return m_visual; }

    animation::AnimationPlayer& GetAnimationPlayer() { return m_animationPlayer; }
    const animation::AnimationPlayer& GetAnimationPlayer() const { return m_animationPlayer; }

    const std::shared_ptr<animation::SpriteSheet>& GetSpriteSheet() const { return m_spriteSheet; }
    void SetSpriteSheet(std::shared_ptr<animation::SpriteSheet> sheet) {
        m_spriteSheet = std::move(sheet);
    }

    // ---- 战斗组件（阶段5） ----
    // combatEnabled=false 的角色（NPC）不参与战斗：不可被攻击、不可被锁定
    bool IsCombatEnabled() const { return m_combatEnabled; }
    void SetCombatEnabled(bool enabled) { m_combatEnabled = enabled; }
    // 战斗存活 = 参战 && HP > 0（死亡动画期间 active=true 但 alive=false，两者严格分离）
    bool IsCombatAlive() const { return m_combatEnabled && m_combatStats.IsAlive(); }

    combat::CombatStats& GetCombatStats() { return m_combatStats; }
    const combat::CombatStats& GetCombatStats() const { return m_combatStats; }

    // 攻击冷却：攻击成功开始时置为 attackInterval，每帧递减到 0
    float GetAttackCooldownRemaining() const { return m_attackCooldown; }
    void SetAttackCooldownRemaining(float seconds) { m_attackCooldown = seconds > 0.0f ? seconds : 0.0f; }
    void TickCooldown(float deltaTime) {
        if (m_attackCooldown > 0.0f) {
            m_attackCooldown -= deltaTime;
            if (m_attackCooldown < 0.0f) {
                m_attackCooldown = 0.0f;
            }
        }
    }

private:
    ActorType m_actorType = ActorType::Player;
    CharacterActionState m_actionState = CharacterActionState::Normal;
    Direction8 m_direction = Direction8::South;
    math::Vector2 m_velocity{0.0f, 0.0f};
    bool m_moving = false;
    float m_moveSpeed = 200.0f;
    CharacterFootprint m_footprint;
    CharacterVisual m_visual;
    animation::AnimationPlayer m_animationPlayer;
    std::shared_ptr<animation::SpriteSheet> m_spriteSheet;
    // 战斗组件
    bool m_combatEnabled = false;
    combat::CombatStats m_combatStats;
    float m_attackCooldown = 0.0f;
};

} // namespace legend::entity