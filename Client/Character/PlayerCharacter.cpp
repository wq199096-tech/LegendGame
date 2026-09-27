#include "Client/Character/PlayerCharacter.h"

#include <algorithm>

#include "Engine/Debug/Logger.h"
#include "Engine/Item/ItemDatabase.h"

PlayerCharacter::PlayerCharacter(
    legend::entity::EntityId id, const legend::animation::CharacterDefinition& definition,
    std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
    std::shared_ptr<legend::animation::SpriteSheet> spriteSheet)
    : legend::entity::Character(id, definition.name, legend::entity::ActorType::Player,
                                definition.moveSpeed, definition.footprint,
                                legend::entity::CharacterVisual{definition.visualWidth,
                                                                definition.visualHeight,
                                                                definition.pivot},
                                std::move(clips)),
      m_definition(definition) {
    SetSpriteSheet(std::move(spriteSheet));
    // 战斗组件：来自 character.json combat 块（NPC 无 combat 块则不参战）
    SetCombatEnabled(definition.hasCombat);
    if (definition.hasCombat) {
        GetCombatStats() = definition.combat;
    }
    // 阶段6：成长组件（growth 块，缺省 20/5/2）+ 空背包（20 格）
    m_progression.Initialize(definition);
    // 阶段7：base stats 副本（character.json combat）——final 由 base + equipment 重算
    m_stats.Initialize(definition.combat);
    // 阶段8：MP（skillResource 块；仅 Player 使用，初始满蓝）。
    // maxMana<=0 的 Player 配置由 GameScene::LoadPlayerCharacter 拒绝（指令一百零二），
    // 这里对非法值兜底为 100 并 LOG，避免 fallback 场景 0 蓝不可玩。
    if (definition.hasSkillResource && definition.maxMana > 0.0f) {
        m_skillResource.Initialize(definition.maxMana);
    } else {
        LOG_WARN("PlayerCharacter: missing/invalid skillResource block, fallback maxMana=100.");
        m_skillResource.Initialize(100.0f);
    }
}

std::vector<legend::progression::LevelUpEvent> PlayerCharacter::AddExperience(
    legend::progression::ExperienceValue amount) {
    auto events = m_progression.AddExperience(amount);
    if (!events.empty()) {
        const int levelsGained = static_cast<int>(events.size());
        // 阶段7 指令二十二：等级成长加到 Base Stats（不是 Final），然后 Recalculate
        m_stats.ApplyLevelGrowth(m_progression.GetGrowth(), levelsGained);
        RecalculateCombatStats();
        // 阶段6 语义保持：升级 MaxHP +X 时当前 HP 也 +X（clamp 到新 maxHp）
        legend::combat::CombatStats& stats = GetCombatStats();
        stats.hp = std::min(stats.hp + m_progression.GetGrowth().maxHpPerLevel * levelsGained,
                            stats.maxHp);
    }
    return events;
}

void PlayerCharacter::SetItemDatabase(const legend::item::ItemDatabase* database) {
    m_itemDatabase = database;
}

legend::item::EquipmentOpResult PlayerCharacter::EquipInstance(
    legend::item::ItemInstanceId instanceId) {
    if (m_itemDatabase == nullptr) {
        return {false, "no item database"};
    }
    const float attackBefore = GetCombatStats().attack;
    auto result =
        legend::item::EquipmentSystem::Equip(*m_itemDatabase, m_inventory, m_equipment,
                                             instanceId);
    if (result.success) {
        RecalculateCombatStats();
        // 阶段7 指令七十一：装备/属性日志（只在操作时打，不每帧刷）
        LOG_INFO("[Equip] instance #" + std::to_string(instanceId) + " equipped (" +
                 result.reason + "); [Stats] ATK " + std::to_string(attackBefore) + " -> " +
                 std::to_string(GetCombatStats().attack));
    }
    return result;
}

legend::item::EquipmentOpResult PlayerCharacter::UnequipSlot(
    legend::item::EquipmentSlotType slot) {
    if (m_itemDatabase == nullptr) {
        return {false, "no item database"};
    }
    const float attackBefore = GetCombatStats().attack;
    auto result =
        legend::item::EquipmentSystem::Unequip(*m_itemDatabase, m_inventory, m_equipment, slot);
    if (result.success) {
        RecalculateCombatStats();
        LOG_INFO("[Unequip] slot " +
                 std::string(legend::item::EquipmentSlotTypeName(slot)) + "; [Stats] ATK " +
                 std::to_string(attackBefore) + " -> " +
                 std::to_string(GetCombatStats().attack));
    }
    return result;
}

void PlayerCharacter::RecalculateCombatStats() {
    if (m_itemDatabase == nullptr) {
        return; // ItemDatabase 未注入（fallback/测试场景）：final 保持现状
    }
    m_stats.RecalculateFinalStats(GetCombatStats(), m_equipment, *m_itemDatabase);
    // HP 处理（阶段7 指令二十五）：穿上装备 HP 不变（不自动补满）；
    // 卸下装备 maxHp 减小时当前 HP clamp 到新 maxHp（不丢已有 HP 之外的量）
    legend::combat::CombatStats& stats = GetCombatStats();
    if (stats.hp > stats.maxHp) {
        stats.hp = stats.maxHp;
    }
    if (stats.hp < 0.0f) {
        stats.hp = 0.0f;
    }
}
