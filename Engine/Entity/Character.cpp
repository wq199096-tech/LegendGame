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
    // 1) 选择 Clip：按最终 moving/direction（idle_/walk_ + 方向）；同名 Play 保持进度
    legend::animation::AnimationStateMachine stateMachine;
    stateMachine.Update(*this);
    // 2) 推进帧：真正消耗 deltaTime，驱动 Frame 0 -> 1 -> ... -> Loop
    m_animationPlayer.Update(deltaTime);
}

} // namespace legend::entity
