#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Client/Progression/PlayerProgression.h"
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationLoader.h"
#include "Engine/Entity/Character.h"
#include "Engine/Item/Inventory.h"

// 玩家角色：类型标识 + Character Definition。
// 组合（非继承）：Character 基础 + PlayerProgression（成长）+ Inventory（背包）。
class PlayerCharacter final : public legend::entity::Character {
public:
    PlayerCharacter(legend::entity::EntityId id,
                    const legend::animation::CharacterDefinition& definition,
                    std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
                    std::shared_ptr<legend::animation::SpriteSheet> spriteSheet);

    const legend::animation::CharacterDefinition& GetDefinition() const { return m_definition; }

    // ---- 阶段6：成长 / 背包（独立组件） ----
    legend::progression::PlayerProgression& GetProgression() { return m_progression; }
    const legend::progression::PlayerProgression& GetProgression() const { return m_progression; }
    legend::item::Inventory& GetInventory() { return m_inventory; }
    const legend::item::Inventory& GetInventory() const { return m_inventory; }

    // 击杀奖励入口：加经验（内部连续升级 + 属性成长 MaxHP+X->HP+X）
    std::vector<legend::progression::LevelUpEvent> AddExperience(
        legend::progression::ExperienceValue amount);

private:
    legend::animation::CharacterDefinition m_definition;
    legend::progression::PlayerProgression m_progression;
    legend::item::Inventory m_inventory;
};
