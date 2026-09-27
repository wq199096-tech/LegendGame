#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Client/Character/PlayerStatsComponent.h"
#include "Client/Progression/PlayerProgression.h"
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationLoader.h"
#include "Engine/Entity/Character.h"
#include "Engine/Item/EquipmentComponent.h"
#include "Engine/Item/EquipmentSystem.h"
#include "Engine/Item/Inventory.h"
#include "Engine/Skill/SkillLoadout.h"
#include "Engine/Skill/SkillResource.h"

// 玩家角色：类型标识 + Character Definition。
// 组合（非继承）：Character 基础 + PlayerProgression（成长）+ Inventory（背包）+
// EquipmentComponent（装备栏）+ PlayerStatsComponent（Base/Final 属性架构）。
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

    // 击杀奖励入口：加经验（内部连续升级；growth 加到 base stats 后 Recalculate）
    std::vector<legend::progression::LevelUpEvent> AddExperience(
        legend::progression::ExperienceValue amount);

    // ---- 阶段7：装备系统（EquipmentComponent + Base/Final Stats 架构） ----
    // ItemDatabase 注入（GameScene OnLoad 初始化后调用；未设置时装备接口返回失败）
    void SetItemDatabase(const legend::item::ItemDatabase* database);

    legend::item::EquipmentComponent& GetEquipment() { return m_equipment; }
    const legend::item::EquipmentComponent& GetEquipment() const { return m_equipment; }
    const legend::combat::CombatStats& GetBaseCombatStats() const {
        return m_stats.GetBaseStats();
    }

    // ---- 阶段8：技能资源 / 技能栏 ----
    legend::skill::SkillResource& GetSkillResource() { return m_skillResource; }
    const legend::skill::SkillResource& GetSkillResource() const { return m_skillResource; }
    legend::skill::SkillLoadout& GetLoadout() { return m_loadout; }
    const legend::skill::SkillLoadout& GetLoadout() const { return m_loadout; }

    // 装备背包实例（事务安全：失败时背包/装备栏状态不变）；成功后 Recalculate
    legend::item::EquipmentOpResult EquipInstance(legend::item::ItemInstanceId instanceId);
    // 卸下槽位装备回背包（背包满时失败、装备留槽）；成功后 Recalculate
    legend::item::EquipmentOpResult UnequipSlot(legend::item::EquipmentSlotType slot);
    // final = base + equipment bonuses；HP clamp 到新 maxHp（卸下超限时降，穿上时不补满）
    void RecalculateCombatStats();

    // ---- 阶段8.2：Skill 测试隔离（快照恢复转发） ----
    // Base Stats 直接恢复（禁止"升几级再减回去"）；Progression 快照由
    // GetProgression().CreateSnapshot/RestoreSnapshot 负责
    void RestoreBaseStats(const legend::combat::CombatStats& stats) {
        m_stats.RestoreBaseStats(stats);
    }

private:
    legend::animation::CharacterDefinition m_definition;
    legend::progression::PlayerProgression m_progression;
    legend::item::Inventory m_inventory;
    legend::item::EquipmentComponent m_equipment; // 阶段7：6 装备槽
    PlayerStatsComponent m_stats;                 // 阶段7：base stats + Recalculate
    const legend::item::ItemDatabase* m_itemDatabase = nullptr;
    legend::skill::SkillResource m_skillResource; // 阶段8：MP（独立组件，不进 CombatStats）
    legend::skill::SkillLoadout m_loadout;        // 阶段8：4 技能槽
};
