#pragma once

#include <memory>
#include <unordered_map>

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationLoader.h"
#include "Engine/Entity/ActorType.h"
#include "Engine/Entity/Character.h"

// NPC 角色：静态站立（Idle + 固定朝向），本阶段不含任务/商店/对话。
// 完全复用 Character 的动画与渲染管线。
class NPCCharacter final : public legend::entity::Character {
public:
    NPCCharacter(legend::entity::EntityId id, std::string displayName,
                 const legend::animation::CharacterDefinition& definition,
                 std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
                 std::shared_ptr<legend::animation::SpriteSheet> spriteSheet,
                 legend::entity::Direction8 facing);

    const legend::animation::CharacterDefinition& GetDefinition() const { return m_definition; }

private:
    legend::animation::CharacterDefinition m_definition;
};
