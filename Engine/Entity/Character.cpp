#include "Engine/Entity/Character.h"

#include "Engine/Animation/AnimationStateMachine.h"

namespace legend::entity {

Character::Character(EntityId id, std::string name, ActorType actorType, float moveSpeed,
                     const CharacterFootprint& footprint, const CharacterVisual& visual,
                     std::shared_ptr<const std::unordered_map<std::string, animation::AnimationClip>> clips)
    : Entity(id, std::move(name)),
      m_actorType(actorType),
      m_moveSpeed(moveSpeed),
      m_footprint(footprint),
      m_visual(visual) {
    m_animationPlayer.SetClips(std::move(clips));
}

void Character::UpdateAnimation(float deltaTime) {
    // 1) 选择 Clip：优先级 Dead > HitReact > Attacking > Walk > Idle；同名 Play 保持进度
    legend::animation::AnimationStateMachine stateMachine;
    stateMachine.Update(*this);
    // 2) 推进帧：真正消耗 deltaTime，驱动 Frame 0 -> 1 -> ... -> Loop/Finished
    m_animationPlayer.Update(deltaTime);
}

void Character::EnterDead() {
    m_actionState = CharacterActionState::Dead;
    m_moving = false;
    m_velocity = math::Vector2{0.0f, 0.0f};
    m_actionClipOverride.clear(); // 死亡打断施法：技能 Clip 覆盖清除
    // active/visible 保持：死亡动画仍需渲染；Despawn 由 WorldActorManager 处理
}

void Character::EnterHitReact() {
    if (m_actionState == CharacterActionState::Dead) {
        return; // 死亡不可被打断
    }
    m_actionState = CharacterActionState::HitReact; // 覆盖 Attacking/SkillCasting：事件不再产生伤害
    m_moving = false;
    m_velocity = math::Vector2{0.0f, 0.0f};
    m_actionClipOverride.clear(); // 受击打断施法：技能 Clip 覆盖清除
}

} // namespace legend::entity