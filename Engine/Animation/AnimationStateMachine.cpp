#include "Engine/Animation/AnimationStateMachine.h"

namespace legend::animation {

std::string AnimationStateMachine::SelectClip(bool moving, entity::Direction8 direction) {
    return std::string(moving ? "walk_" : "idle_") + entity::Direction8Name(direction);
}

void AnimationStateMachine::Update(entity::Character& character) const {
    character.GetAnimationPlayer().Play(SelectClip(character.IsMoving(), character.GetDirection()));
}

} // namespace legend::animation
