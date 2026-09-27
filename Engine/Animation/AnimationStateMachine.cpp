#include "Engine/Animation/AnimationStateMachine.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Entity/Character.h"

namespace legend::animation {

std::string AnimationStateMachine::SelectClip(const entity::Character& character) {
    // 优先级严格：Dead > HitReact > SkillCasting > Attacking > Walk > Idle
    using entity::CharacterActionState;
    switch (character.GetActionState()) {
    case CharacterActionState::Dead:
        return "death_" + std::string(entity::Direction8Name(character.GetDirection()));
    case CharacterActionState::HitReact:
        return "hit_" + std::string(entity::Direction8Name(character.GetDirection()));
    case CharacterActionState::SkillCasting:
        // 阶段8：施法使用 SkillDefinition.animation 指定的技能 Clip（施法开始时
        // 由 SkillSystem 写入 Character 的 Clip 覆盖；方向已在施法开始锁定）
        if (!character.GetActionClipOverride().empty()) {
            return character.GetActionClipOverride();
        }
        return "attack_" + std::string(entity::Direction8Name(character.GetDirection()));
    case CharacterActionState::Attacking:
        return "attack_" + std::string(entity::Direction8Name(character.GetDirection()));
    case CharacterActionState::Normal:
    default:
        break;
    }
    return SelectClip(character.IsMoving(), character.GetDirection());
}

std::string AnimationStateMachine::SelectClip(bool moving, entity::Direction8 direction) {
    return std::string(moving ? "walk_" : "idle_") + entity::Direction8Name(direction);
}

void AnimationStateMachine::Update(entity::Character& character) const {
    character.GetAnimationPlayer().Play(SelectClip(character));
}

} // namespace legend::animation