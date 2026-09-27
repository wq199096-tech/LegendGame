#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/SpriteSheet.h"
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
    float offsetY = 20.0f;
};

// 角色视觉尺寸与精灵轴心（pivot：精灵内归一化锚点，脚底点对齐处）
struct CharacterVisual {
    float width = 96.0f;
    float height = 96.0f;
    math::Vector2 pivot{0.5f, 0.85f};
};

// 角色基类：移动状态 / 朝向 / 动画状态 / 数据。
// 不读取键盘（键盘属于 PlayerController），不做渲染（属于 CharacterRenderer）。
// 以后 Player / NPC / Monster / Pet / Summon 复用此类。
class Character : public Entity {
public:
    Character(EntityId id, std::string name, ActorType actorType, float moveSpeed,
              const CharacterFootprint& footprint, const CharacterVisual& visual,
              std::shared_ptr<const std::unordered_map<std::string, animation::AnimationClip>> clips);

    ActorType GetActorType() const { return m_actorType; }

    // 统一动画更新入口：
    // 1) AnimationStateMachine 按最终 moving/direction 选择正确 Clip（同名 Play 不重置进度）
    // 2) AnimationPlayer::Update(deltaTime) 真正推进当前帧（Loop/NonLoop）
    // 没有这一步推进，动画会永远卡在 Clip 第一帧
    void UpdateAnimation(float deltaTime);

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

private:
    ActorType m_actorType = ActorType::Player;
    Direction8 m_direction = Direction8::South;
    math::Vector2 m_velocity{0.0f, 0.0f};
    bool m_moving = false;
    float m_moveSpeed = 200.0f;
    CharacterFootprint m_footprint;
    CharacterVisual m_visual;
    animation::AnimationPlayer m_animationPlayer;
    std::shared_ptr<animation::SpriteSheet> m_spriteSheet;
};

} // namespace legend::entity
