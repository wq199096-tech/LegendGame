#pragma once

#include <memory>
#include <unordered_map>

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationLoader.h"
#include "Engine/Entity/Character.h"

// 玩家角色：类型标识 + Character Definition。
// 本阶段不含 HP/MP/Level/Attack，后续阶段扩展。
class PlayerCharacter final : public legend::entity::Character {
public:
    PlayerCharacter(legend::entity::EntityId id,
                    const legend::animation::CharacterDefinition& definition,
                    std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
                    std::shared_ptr<legend::animation::SpriteSheet> spriteSheet);

    const legend::animation::CharacterDefinition& GetDefinition() const { return m_definition; }

private:
    legend::animation::CharacterDefinition m_definition;
};
