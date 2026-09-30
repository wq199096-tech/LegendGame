#include "Shared/GameData/GameDataJson.h"

#include "Shared/WorldData/AtomicFile.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace legend::world {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

// ---- 字段严格读取 helper（23.21：错误必须带 文件/类型/ID/字段/原因）----
// （与 WorldDataJson.cpp 同构；两处独立匿名命名空间，避免跨文件耦合。）

bool ReadUint(const json& obj, const char* key, std::uint64_t& out, const std::string& ctx,
              std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_unsigned()) {
        error = ctx + ": missing/invalid unsigned field '" + key + "'";
        return false;
    }
    out = it->get<std::uint64_t>();
    return true;
}

bool ReadInt(const json& obj, const char* key, std::int64_t& out, const std::string& ctx,
             std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) {
        error = ctx + ": missing/invalid integer field '" + key + "'";
        return false;
    }
    out = it->get<std::int64_t>();
    return true;
}

bool ReadFloat(const json& obj, const char* key, double& out, const std::string& ctx,
               std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) {
        error = ctx + ": missing/invalid number field '" + key + "'";
        return false;
    }
    out = it->get<double>();
    return true;
}

bool ReadBool(const json& obj, const char* key, bool& out, const std::string& ctx,
              std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) {
        error = ctx + ": missing/invalid boolean field '" + key + "'";
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool ReadString(const json& obj, const char* key, std::string& out, const std::string& ctx,
                std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        error = ctx + ": missing/invalid string field '" + key + "'";
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool ReadJsonFile(const std::string& dir, const char* name, json& out, std::string& error) {
    const fs::path path = fs::path(dir) / name;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string(name) + ": file not found in '" + dir + "'";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out = json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        error = std::string(name) + ": invalid JSON syntax";
        return false;
    }
    if (!out.is_object()) {
        error = std::string(name) + ": top-level must be an object";
        return false;
    }
    return true;
}

bool ReadSchemaVersion(const json& obj, const char* name, std::string& error) {
    const auto it = obj.find("schemaVersion");
    if (it == obj.end() || !it->is_number_integer() || it->get<int>() != kGameDataSchemaVersion) {
        error = std::string(name) + ": unsupported schemaVersion (expected " +
                std::to_string(kGameDataSchemaVersion) + ")";
        return false;
    }
    return true;
}

// ---- 枚举字符串映射 ----

bool ItemTypeFromString(const std::string& text, ItemType& out) {
    if (text == "None") { out = ItemType::None; return true; }
    if (text == "Weapon") { out = ItemType::Weapon; return true; }
    if (text == "Armor") { out = ItemType::Armor; return true; }
    if (text == "Material") { out = ItemType::Material; return true; }
    return false;
}

std::string ItemTypeToString(ItemType type) {
    switch (type) {
        case ItemType::None: return "None";
        case ItemType::Weapon: return "Weapon";
        case ItemType::Armor: return "Armor";
        case ItemType::Material: return "Material";
    }
    return "Unknown";
}

bool EquipmentSlotFromString(const std::string& text, EquipmentSlot& out) {
    if (text == "None") { out = EquipmentSlot::None; return true; }
    if (text == "Weapon") { out = EquipmentSlot::Weapon; return true; }
    if (text == "Armor") { out = EquipmentSlot::Armor; return true; }
    return false;
}

std::string EquipmentSlotToString(EquipmentSlot slot) {
    switch (slot) {
        case EquipmentSlot::None: return "None";
        case EquipmentSlot::Weapon: return "Weapon";
        case EquipmentSlot::Armor: return "Armor";
    }
    return "Unknown";
}

bool SkillTargetTypeFromString(const std::string& text, SkillTargetType& out) {
    if (text == "None") { out = SkillTargetType::None; return true; }
    if (text == "Monster") { out = SkillTargetType::Monster; return true; }
    if (text == "Self") { out = SkillTargetType::Self; return true; }
    return false;
}

std::string SkillTargetTypeToString(SkillTargetType type) {
    switch (type) {
        case SkillTargetType::None: return "None";
        case SkillTargetType::Monster: return "Monster";
        case SkillTargetType::Self: return "Self";
    }
    return "Unknown";
}

bool StatusCategoryFromString(const std::string& text, StatusEffectCategory& out) {
    if (text == "Buff") { out = StatusEffectCategory::Buff; return true; }
    if (text == "Debuff") { out = StatusEffectCategory::Debuff; return true; }
    return false;
}

std::string StatusCategoryToString(StatusEffectCategory category) {
    return category == StatusEffectCategory::Buff ? "Buff" : "Debuff";
}

bool StackPolicyFromString(const std::string& text, StatusEffectStackPolicy& out) {
    if (text == "RefreshDuration") { out = StatusEffectStackPolicy::RefreshDuration; return true; }
    if (text == "AddStackRefresh") { out = StatusEffectStackPolicy::AddStackRefresh; return true; }
    return false;
}

std::string StackPolicyToString(StatusEffectStackPolicy policy) {
    return policy == StatusEffectStackPolicy::RefreshDuration ? "RefreshDuration"
                                                              : "AddStackRefresh";
}

bool ObjectiveTypeFromString(const std::string& text, QuestObjectiveType& out) {
    if (text == "KillMonster") { out = QuestObjectiveType::KillMonster; return true; }
    if (text == "CollectItem") { out = QuestObjectiveType::CollectItem; return true; }
    if (text == "ReachLevel") { out = QuestObjectiveType::ReachLevel; return true; }
    if (text == "ReachArea") { out = QuestObjectiveType::ReachArea; return true; }
    return false;
}

std::string ObjectiveTypeToString(QuestObjectiveType type) {
    switch (type) {
        case QuestObjectiveType::KillMonster: return "KillMonster";
        case QuestObjectiveType::CollectItem: return "CollectItem";
        case QuestObjectiveType::ReachLevel: return "ReachLevel";
        case QuestObjectiveType::ReachArea: return "ReachArea";
    }
    return "Unknown";
}

// ---- Parse ----

bool ParseItems(const json& root, GameDataSet& out, std::string& error) {
    const auto it = root.find("items");
    if (it == root.end() || !it->is_array()) {
        error = "items.json: missing/invalid array 'items'";
        return false;
    }
    for (const auto& e : *it) {
        ItemDefinition item;
        std::uint64_t id = 0;
        if (!ReadUint(e, "itemDefinitionId", id, "items.json: Item", error)) {
            return false;
        }
        const std::string ctx = "items.json: Item " + std::to_string(id);
        std::string typeText;
        std::string slotText;
        if (!ReadString(e, "name", item.name, ctx, error) ||
            !ReadString(e, "type", typeText, ctx, error)) {
            return false;
        }
        std::uint64_t maxStack = 0;
        if (!ReadUint(e, "maxStack", maxStack, ctx, error)) {
            return false;
        }
        item.maxStack = static_cast<std::uint32_t>(maxStack);
        if (!ReadString(e, "equipSlot", slotText, ctx, error)) {
            return false;
        }
        std::int64_t attackBonus = 0;
        std::int64_t defenseBonus = 0;
        if (!ReadInt(e, "attackBonus", attackBonus, ctx, error) ||
            !ReadInt(e, "defenseBonus", defenseBonus, ctx, error)) {
            return false;
        }
        // 23.25：负数属性拒绝（不允许编辑器保存负攻/负防——数值域 [0,∞)）。
        if (attackBonus < 0 || defenseBonus < 0) {
            error = ctx + ": negative attackBonus/defenseBonus";
            return false;
        }
        item.attackBonus = static_cast<std::uint32_t>(attackBonus);
        item.defenseBonus = static_cast<std::uint32_t>(defenseBonus);
        if (!ItemTypeFromString(typeText, item.type)) {
            error = ctx + ": unknown type '" + typeText + "'";
            return false;
        }
        if (!EquipmentSlotFromString(slotText, item.equipmentSlot)) {
            error = ctx + ": unknown equipSlot '" + slotText + "'";
            return false;
        }
        bool canBuy = true;
        bool canSell = true;
        bool enabled = true;
        std::string iconKey;
        if (!ReadBool(e, "canBuy", canBuy, ctx, error) ||
            !ReadBool(e, "canSell", canSell, ctx, error) ||
            !ReadString(e, "iconKey", iconKey, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        item.canBuy = canBuy;
        item.canSell = canSell;
        item.iconKey = iconKey;
        item.enabled = enabled;
        item.definitionId = static_cast<std::uint32_t>(id);
        out.items.push_back(item);
    }
    return true;
}

bool ParseMonsters(const json& root, GameDataSet& out, std::string& error) {
    const auto it = root.find("monsters");
    if (it == root.end() || !it->is_array()) {
        error = "monsters.json: missing/invalid array 'monsters'";
        return false;
    }
    for (const auto& e : *it) {
        MonsterDefinition monster;
        std::uint64_t id = 0;
        if (!ReadUint(e, "monsterDefinitionId", id, "monsters.json: Monster", error)) {
            return false;
        }
        const std::string ctx = "monsters.json: Monster " + std::to_string(id);
        std::uint64_t level = 0;
        double maxHp = 0;
        double moveSpeed = 0;
        double attackRange = 0;
        double aggroRange = 0;
        double leashRange = 0;
        double patrolRadius = 0;
        double collisionRadius = 0;
        std::uint64_t modelId = 0;
        std::uint64_t attackPower = 0;
        std::uint64_t defense = 0;
        double attackCooldownMs = 0;
        std::uint64_t expReward = 0;
        std::uint64_t goldReward = 0;
        std::uint64_t lootTableId = 0;
        bool enabled = true;
        if (!ReadString(e, "name", monster.name, ctx, error) ||
            !ReadUint(e, "level", level, ctx, error) ||
            !ReadFloat(e, "maxHp", maxHp, ctx, error) ||
            !ReadUint(e, "attackPower", attackPower, ctx, error) ||
            !ReadUint(e, "defense", defense, ctx, error) ||
            !ReadFloat(e, "moveSpeed", moveSpeed, ctx, error) ||
            !ReadFloat(e, "attackRange", attackRange, ctx, error) ||
            !ReadFloat(e, "aggroRange", aggroRange, ctx, error) ||
            !ReadFloat(e, "leashRange", leashRange, ctx, error) ||
            !ReadFloat(e, "patrolRadius", patrolRadius, ctx, error) ||
            !ReadFloat(e, "collisionRadius", collisionRadius, ctx, error) ||
            !ReadUint(e, "modelId", modelId, ctx, error) ||
            !ReadFloat(e, "attackCooldownMs", attackCooldownMs, ctx, error) ||
            !ReadUint(e, "expReward", expReward, ctx, error) ||
            !ReadUint(e, "goldReward", goldReward, ctx, error) ||
            !ReadUint(e, "lootTableId", lootTableId, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        monster.monsterTypeId = static_cast<std::uint32_t>(id);
        monster.level = static_cast<std::uint32_t>(level);
        monster.maxHp = static_cast<std::uint32_t>(maxHp);
        monster.attackPower = static_cast<std::uint32_t>(attackPower);
        monster.defense = static_cast<std::uint32_t>(defense);
        monster.moveSpeed = static_cast<float>(moveSpeed);
        monster.attackRange = static_cast<float>(attackRange);
        monster.aggroRadius = static_cast<float>(aggroRange);
        monster.leashRadius = static_cast<float>(leashRange);
        monster.patrolRadius = static_cast<float>(patrolRadius);
        monster.collisionRadius = static_cast<float>(collisionRadius);
        monster.modelId = static_cast<std::uint32_t>(modelId);
        monster.attackCooldownSeconds = static_cast<float>(attackCooldownMs / 1000.0);
        monster.rewardExp = static_cast<std::uint32_t>(expReward);
        monster.rewardGold = static_cast<std::uint32_t>(goldReward);
        monster.lootTableId = static_cast<std::uint32_t>(lootTableId);
        monster.enabled = enabled;
        // 阶段24：可选 visualId（缺省空 = Client fallback 视觉）。
        const auto visualIt = e.find("visualId");
        if (visualIt != e.end()) {
            if (!visualIt->is_string()) {
                error = ctx + ": visualId must be a string";
                return false;
            }
            monster.visualId = visualIt->get<std::string>();
        }
        out.monsters.push_back(monster);
    }
    return true;
}

bool ParseSkills(const json& root, GameDataSet& out, std::string& error) {
    const auto it = root.find("skills");
    if (it == root.end() || !it->is_array()) {
        error = "skills.json: missing/invalid array 'skills'";
        return false;
    }
    for (const auto& e : *it) {
        SkillDefinition skill;
        std::uint64_t id = 0;
        if (!ReadUint(e, "skillId", id, "skills.json: Skill", error)) {
            return false;
        }
        const std::string ctx = "skills.json: Skill " + std::to_string(id);
        std::string targetTypeText;
        double cooldownMs = 0;
        double castTimeMs = 0;
        std::uint64_t manaCost = 0;
        double range = 0;
        std::uint64_t baseDamage = 0;
        double radius = 0;
        std::uint64_t maxTargets = 0;
        bool enabled = true;
        if (!ReadString(e, "name", skill.name, ctx, error) ||
            !ReadString(e, "targetType", targetTypeText, ctx, error) ||
            !ReadFloat(e, "castTimeMs", castTimeMs, ctx, error) ||
            !ReadFloat(e, "cooldownMs", cooldownMs, ctx, error) ||
            !ReadUint(e, "manaCost", manaCost, ctx, error) ||
            !ReadFloat(e, "range", range, ctx, error) ||
            !ReadFloat(e, "radius", radius, ctx, error) ||
            !ReadUint(e, "baseDamage", baseDamage, ctx, error) ||
            !ReadUint(e, "maxTargets", maxTargets, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        if (!SkillTargetTypeFromString(targetTypeText, skill.targetType)) {
            error = ctx + ": unknown targetType '" + targetTypeText + "'";
            return false;
        }
        // 23.5：statusEffectIds 数组 → applyStatusEffectId（阶段23 单状态语义）。
        const auto statusIt = e.find("statusEffectIds");
        if (statusIt != e.end() && statusIt->is_array() && !statusIt->empty()) {
            if (!(*statusIt)[0].is_number_unsigned()) {
                error = ctx + ": invalid statusEffectIds element";
                return false;
            }
            skill.applyStatusEffectId =
                static_cast<StatusEffectId>((*statusIt)[0].get<std::uint64_t>());
            std::uint64_t stacks = 1;
            ReadUint(e, "applyStatusStacks", stacks, ctx, error); // 可选
            skill.applyStatusStacks = static_cast<std::uint8_t>(std::max<std::uint64_t>(1, stacks));
        }
        skill.skillId = static_cast<SkillId>(id);
        skill.cooldownSeconds = static_cast<float>(cooldownMs / 1000.0);
        skill.castTimeSeconds = static_cast<float>(castTimeMs / 1000.0);
        skill.manaCost = static_cast<std::uint32_t>(manaCost);
        skill.range = static_cast<float>(range);
        skill.baseDamage = static_cast<std::uint32_t>(baseDamage);
        skill.aoeRadius = static_cast<float>(radius);
        skill.maxTargets = static_cast<std::uint32_t>(maxTargets);
        skill.enabled = enabled;
        out.skills.push_back(skill);
    }
    return true;
}

bool ParseStatuses(const json& root, GameDataSet& out, std::string& error) {
    const auto it = root.find("statuses");
    if (it == root.end() || !it->is_array()) {
        error = "statuses.json: missing/invalid array 'statuses'";
        return false;
    }
    for (const auto& e : *it) {
        StatusEffectDefinition status;
        std::uint64_t id = 0;
        if (!ReadUint(e, "statusId", id, "statuses.json: Status", error)) {
            return false;
        }
        const std::string ctx = "statuses.json: Status " + std::to_string(id);
        std::string categoryText;
        std::string policyText;
        std::uint64_t durationMs = 0;
        std::uint64_t tickMs = 0;
        std::uint64_t maxStacks = 0;
        std::int64_t attackModifier = 0;
        std::int64_t defenseModifier = 0;
        double movementMultiplier = 0;
        std::uint64_t dotDamage = 0;
        bool enabled = true;
        if (!ReadString(e, "name", status.name, ctx, error) ||
            !ReadString(e, "type", categoryText, ctx, error) ||
            !ReadUint(e, "durationMs", durationMs, ctx, error) ||
            !ReadUint(e, "tickMs", tickMs, ctx, error) ||
            !ReadUint(e, "maxStacks", maxStacks, ctx, error) ||
            !ReadInt(e, "attackModifier", attackModifier, ctx, error) ||
            !ReadInt(e, "defenseModifier", defenseModifier, ctx, error) ||
            !ReadFloat(e, "movementMultiplier", movementMultiplier, ctx, error) ||
            !ReadUint(e, "dotDamage", dotDamage, ctx, error) ||
            !ReadString(e, "stackPolicy", policyText, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        if (!StatusCategoryFromString(categoryText, status.category)) {
            error = ctx + ": unknown type '" + categoryText + "'";
            return false;
        }
        if (!StackPolicyFromString(policyText, status.stackPolicy)) {
            error = ctx + ": unknown stackPolicy '" + policyText + "'";
            return false;
        }
        status.effectId = static_cast<StatusEffectId>(id);
        status.durationMs = static_cast<std::uint32_t>(durationMs);
        status.tickIntervalMs = static_cast<std::uint32_t>(tickMs);
        status.maxStacks = static_cast<std::uint8_t>(maxStacks);
        status.attackFlatModifier = static_cast<std::int32_t>(attackModifier);
        status.defenseFlatModifier = static_cast<std::int32_t>(defenseModifier);
        status.moveSpeedMultiplier = static_cast<float>(movementMultiplier);
        status.dotDamagePerStack = static_cast<std::uint32_t>(dotDamage);
        status.enabled = enabled;
        out.statuses.push_back(status);
    }
    return true;
}

bool ParseQuests(const json& root, GameDataSet& out, std::string& error) {
    const auto it = root.find("quests");
    if (it == root.end() || !it->is_array()) {
        error = "quests.json: missing/invalid array 'quests'";
        return false;
    }
    for (const auto& e : *it) {
        QuestDefinition quest;
        std::uint64_t id = 0;
        if (!ReadUint(e, "questId", id, "quests.json: Quest", error)) {
            return false;
        }
        const std::string ctx = "quests.json: Quest " + std::to_string(id);
        std::uint64_t minLevel = 0;
        std::uint64_t prerequisite = 0;
        bool repeatable = false;
        std::uint64_t startNpc = 0;
        std::uint64_t turnInNpc = 0;
        if (!ReadString(e, "name", quest.name, ctx, error) ||
            !ReadString(e, "description", quest.description, ctx, error) ||
            !ReadUint(e, "minLevel", minLevel, ctx, error) ||
            !ReadUint(e, "prerequisiteQuestId", prerequisite, ctx, error) ||
            !ReadBool(e, "repeatable", repeatable, ctx, error) ||
            !ReadUint(e, "startNpcDefinitionId", startNpc, ctx, error) ||
            !ReadUint(e, "turnInNpcDefinitionId", turnInNpc, ctx, error)) {
            return false;
        }
        // objectives（动态列表，23.7/23.8）。
        const auto objIt = e.find("objectives");
        if (objIt == e.end() || !objIt->is_array() || objIt->empty()) {
            error = ctx + ": missing/empty objectives array";
            return false;
        }
        for (const auto& o : *objIt) {
            QuestObjectiveDefinition objective;
            std::uint64_t objectiveId = 0;
            if (!ReadUint(o, "objectiveId", objectiveId, ctx + ".objective", error)) {
                return false;
            }
            const std::string octx = ctx + ": Objective " + std::to_string(objectiveId);
            std::string typeText;
            std::uint64_t targetId = 0;
            std::uint64_t requiredCount = 0;
            if (!ReadString(o, "type", typeText, octx, error) ||
                !ReadUint(o, "targetId", targetId, octx, error) ||
                !ReadUint(o, "requiredCount", requiredCount, octx, error)) {
                return false;
            }
            if (!ObjectiveTypeFromString(typeText, objective.type)) {
                error = octx + ": unknown type '" + typeText + "'";
                return false;
            }
            objective.objectiveId = static_cast<std::uint32_t>(objectiveId);
            objective.targetId = static_cast<std::uint32_t>(targetId);
            objective.requiredCount = static_cast<std::uint32_t>(requiredCount);
            if (objective.type == QuestObjectiveType::ReachArea) {
                std::uint64_t mapId = 0;
                double areaX = 0;
                double areaY = 0;
                double areaRadius = 0;
                if (!ReadUint(o, "mapId", mapId, octx, error) ||
                    !ReadFloat(o, "areaX", areaX, octx, error) ||
                    !ReadFloat(o, "areaY", areaY, octx, error) ||
                    !ReadFloat(o, "areaRadius", areaRadius, octx, error)) {
                    return false;
                }
                objective.mapId = static_cast<std::uint16_t>(mapId);
                objective.areaX = static_cast<float>(areaX);
                objective.areaY = static_cast<float>(areaY);
                objective.areaRadius = static_cast<float>(areaRadius);
            }
            quest.objectives.push_back(objective);
        }
        // reward（23.7：EXP/Gold/Items——阶段19 结构为 exp+gold+单 item）。
        const auto rewardIt = e.find("reward");
        if (rewardIt == e.end() || !rewardIt->is_object()) {
            error = ctx + ": missing reward object";
            return false;
        }
        std::uint64_t exp = 0;
        std::uint64_t gold = 0;
        std::uint64_t itemDefinitionId = 0;
        std::uint64_t itemQuantity = 0;
        if (!ReadUint(*rewardIt, "exp", exp, ctx + ".reward", error) ||
            !ReadUint(*rewardIt, "gold", gold, ctx + ".reward", error) ||
            !ReadUint(*rewardIt, "itemDefinitionId", itemDefinitionId, ctx + ".reward", error) ||
            !ReadUint(*rewardIt, "itemQuantity", itemQuantity, ctx + ".reward", error)) {
            return false;
        }
        quest.reward.exp = static_cast<std::uint32_t>(exp);
        quest.reward.gold = static_cast<std::uint32_t>(gold);
        quest.reward.itemDefinitionId = static_cast<std::uint32_t>(itemDefinitionId);
        quest.reward.itemQuantity = static_cast<std::uint32_t>(itemQuantity);
        quest.questId = static_cast<QuestId>(id);
        quest.minLevel = static_cast<std::uint32_t>(minLevel);
        quest.prerequisiteQuestId = static_cast<QuestId>(prerequisite);
        quest.repeatable = repeatable;
        quest.startNpcDefinitionId = static_cast<std::uint32_t>(startNpc);
        quest.turnInNpcDefinitionId = static_cast<std::uint32_t>(turnInNpc);
        out.quests.push_back(quest);
    }
    return true;
}

bool ParseShops(const json& root, GameDataSet& out, std::string& error) {
    const auto it = root.find("shops");
    if (it == root.end() || !it->is_array()) {
        error = "shops.json: missing/invalid array 'shops'";
        return false;
    }
    for (const auto& e : *it) {
        ShopDefinition shop;
        std::uint64_t id = 0;
        if (!ReadUint(e, "shopId", id, "shops.json: Shop", error)) {
            return false;
        }
        const std::string ctx = "shops.json: Shop " + std::to_string(id);
        if (!ReadString(e, "name", shop.name, ctx, error)) {
            return false;
        }
        const auto entriesIt = e.find("items");
        if (entriesIt == e.end() || !entriesIt->is_array() || entriesIt->empty()) {
            error = ctx + ": missing/empty items array";
            return false;
        }
        for (const auto& en : *entriesIt) {
            ShopEntry entry;
            std::uint64_t itemId = 0;
            std::uint64_t buyPrice = 0;
            std::uint64_t sellPrice = 0;
            bool canBuy = true;
            bool canSell = true;
            if (!ReadUint(en, "itemDefinitionId", itemId, ctx + ".entry", error) ||
                !ReadUint(en, "buyPrice", buyPrice, ctx + ".entry", error) ||
                !ReadUint(en, "sellPrice", sellPrice, ctx + ".entry", error) ||
                !ReadBool(en, "canBuy", canBuy, ctx + ".entry", error) ||
                !ReadBool(en, "canSell", canSell, ctx + ".entry", error)) {
                return false;
            }
            entry.itemDefinitionId = static_cast<std::uint32_t>(itemId);
            entry.buyPrice = static_cast<std::uint32_t>(buyPrice);
            entry.sellPrice = static_cast<std::uint32_t>(sellPrice);
            entry.canBuy = canBuy;
            entry.canSell = canSell;
            shop.entries.push_back(entry);
        }
        shop.shopId = static_cast<std::uint32_t>(id);
        out.shops.push_back(shop);
    }
    return true;
}

bool ParseTeleports(const json& root, GameDataSet& out, std::string& error) {
    const auto it = root.find("teleports");
    if (it == root.end() || !it->is_array()) {
        error = "teleports.json: missing/invalid array 'teleports'";
        return false;
    }
    for (const auto& e : *it) {
        TeleportDefinition teleport;
        std::uint64_t id = 0;
        if (!ReadUint(e, "teleportId", id, "teleports.json: Teleport", error)) {
            return false;
        }
        const std::string ctx = "teleports.json: Teleport " + std::to_string(id);
        std::uint64_t destMapId = 0;
        double destX = 0;
        double destY = 0;
        std::uint64_t goldCost = 0;
        std::uint64_t minLevel = 0;
        bool enabled = true;
        if (!ReadString(e, "name", teleport.name, ctx, error) ||
            !ReadUint(e, "destinationMapId", destMapId, ctx, error) ||
            !ReadFloat(e, "destinationX", destX, ctx, error) ||
            !ReadFloat(e, "destinationY", destY, ctx, error) ||
            !ReadUint(e, "goldCost", goldCost, ctx, error) ||
            !ReadUint(e, "minLevel", minLevel, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        if (destMapId == 0 || destMapId > 65535) {
            error = ctx + ": destinationMapId out of range";
            return false;
        }
        teleport.teleportId = static_cast<std::uint32_t>(id);
        teleport.destinationMapId = static_cast<std::uint16_t>(destMapId);
        teleport.destinationX = static_cast<float>(destX);
        teleport.destinationY = static_cast<float>(destY);
        teleport.goldCost = static_cast<std::uint32_t>(goldCost);
        teleport.minLevel = static_cast<std::uint32_t>(minLevel);
        teleport.enabled = enabled;
        out.teleports.push_back(teleport);
    }
    return true;
}

bool ParseLootTables(const json& root, GameDataSet& out, std::string& error) {
    const auto it = root.find("lootTables");
    if (it == root.end() || !it->is_array()) {
        error = "loot_tables.json: missing/invalid array 'lootTables'";
        return false;
    }
    for (const auto& e : *it) {
        LootTableDefinition table;
        std::uint64_t id = 0;
        if (!ReadUint(e, "lootTableId", id, "loot_tables.json: LootTable", error)) {
            return false;
        }
        const std::string ctx = "loot_tables.json: LootTable " + std::to_string(id);
        bool enabled = true;
        if (!ReadString(e, "name", table.name, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        const auto entriesIt = e.find("entries");
        if (entriesIt == e.end() || !entriesIt->is_array() || entriesIt->empty()) {
            error = ctx + ": missing/empty entries array";
            return false;
        }
        for (const auto& en : *entriesIt) {
            LootTableEntry entry;
            std::uint64_t itemId = 0;
            double dropChance = 0;
            std::uint64_t minQuantity = 0;
            std::uint64_t maxQuantity = 0;
            if (!ReadUint(en, "itemDefinitionId", itemId, ctx + ".entry", error) ||
                !ReadFloat(en, "dropChance", dropChance, ctx + ".entry", error) ||
                !ReadUint(en, "minQuantity", minQuantity, ctx + ".entry", error) ||
                !ReadUint(en, "maxQuantity", maxQuantity, ctx + ".entry", error)) {
                return false;
            }
            entry.itemDefinitionId = static_cast<std::uint32_t>(itemId);
            entry.dropChance = dropChance;
            entry.minQuantity = static_cast<std::uint32_t>(minQuantity);
            entry.maxQuantity = static_cast<std::uint32_t>(maxQuantity);
            table.entries.push_back(entry);
        }
        table.lootTableId = static_cast<std::uint32_t>(id);
        table.enabled = enabled;
        out.lootTables.push_back(table);
    }
    return true;
}

// ---- 序列化 ----

json ItemToJson(const ItemDefinition& item) {
    return json{
        {"itemDefinitionId", item.definitionId},
        {"name", item.name},
        {"type", ItemTypeToString(item.type)},
        {"maxStack", item.maxStack},
        {"equipSlot", EquipmentSlotToString(item.equipmentSlot)},
        {"attackBonus", item.attackBonus},
        {"defenseBonus", item.defenseBonus},
        {"canBuy", item.canBuy},
        {"canSell", item.canSell},
        {"iconKey", item.iconKey},
        {"enabled", item.enabled},
    };
}

json MonsterToJson(const MonsterDefinition& monster) {
    return json{
        {"monsterDefinitionId", monster.monsterTypeId},
        {"name", monster.name},
        {"level", monster.level},
        {"maxHp", monster.maxHp},
        {"attackPower", monster.attackPower},
        {"defense", monster.defense},
        {"moveSpeed", monster.moveSpeed},
        {"attackRange", monster.attackRange},
        {"aggroRange", monster.aggroRadius},
        {"leashRange", monster.leashRadius},
        {"patrolRadius", monster.patrolRadius},
        {"collisionRadius", monster.collisionRadius},
        {"modelId", monster.modelId},
        {"attackCooldownMs",
         static_cast<std::uint64_t>(monster.attackCooldownSeconds * 1000.0f)},
        {"expReward", monster.rewardExp},
        {"goldReward", monster.rewardGold},
        {"lootTableId", monster.lootTableId},
        {"visualId", monster.visualId},
        {"enabled", monster.enabled},
    };
}

json SkillToJson(const SkillDefinition& skill) {
    json statusIds = json::array();
    if (skill.applyStatusEffectId != 0) {
        statusIds.push_back(skill.applyStatusEffectId);
    }
    return json{
        {"skillId", skill.skillId},
        {"name", skill.name},
        {"targetType", SkillTargetTypeToString(skill.targetType)},
        {"castTimeMs", static_cast<std::uint64_t>(skill.castTimeSeconds * 1000.0f)},
        {"cooldownMs", static_cast<std::uint64_t>(skill.cooldownSeconds * 1000.0f)},
        {"manaCost", skill.manaCost},
        {"range", skill.range},
        {"radius", skill.aoeRadius},
        {"baseDamage", skill.baseDamage},
        {"maxTargets", skill.maxTargets},
        {"statusEffectIds", statusIds},
        {"enabled", skill.enabled},
    };
}

json StatusToJson(const StatusEffectDefinition& status) {
    return json{
        {"statusId", status.effectId},
        {"name", status.name},
        {"type", StatusCategoryToString(status.category)},
        {"durationMs", status.durationMs},
        {"tickMs", status.tickIntervalMs},
        {"maxStacks", status.maxStacks},
        {"attackModifier", status.attackFlatModifier},
        {"defenseModifier", status.defenseFlatModifier},
        {"movementMultiplier", status.moveSpeedMultiplier},
        {"dotDamage", status.dotDamagePerStack},
        {"stackPolicy", StackPolicyToString(status.stackPolicy)},
        {"enabled", status.enabled},
    };
}

json QuestToJson(const QuestDefinition& quest) {
    json objectives = json::array();
    for (const auto& o : quest.objectives) {
        json oj{
            {"objectiveId", o.objectiveId},
            {"type", ObjectiveTypeToString(o.type)},
            {"targetId", o.targetId},
            {"requiredCount", o.requiredCount},
        };
        if (o.type == QuestObjectiveType::ReachArea) {
            oj["mapId"] = o.mapId;
            oj["areaX"] = o.areaX;
            oj["areaY"] = o.areaY;
            oj["areaRadius"] = o.areaRadius;
        }
        objectives.push_back(oj);
    }
    return json{
        {"questId", quest.questId},
        {"name", quest.name},
        {"description", quest.description},
        {"minLevel", quest.minLevel},
        {"prerequisiteQuestId", quest.prerequisiteQuestId},
        {"repeatable", quest.repeatable},
        {"startNpcDefinitionId", quest.startNpcDefinitionId},
        {"turnInNpcDefinitionId", quest.turnInNpcDefinitionId},
        {"objectives", objectives},
        {"reward", {
            {"exp", quest.reward.exp},
            {"gold", quest.reward.gold},
            {"itemDefinitionId", quest.reward.itemDefinitionId},
            {"itemQuantity", quest.reward.itemQuantity},
        }},
    };
}

json ShopToJson(const ShopDefinition& shop) {
    json items = json::array();
    for (const auto& entry : shop.entries) {
        items.push_back({
            {"itemDefinitionId", entry.itemDefinitionId},
            {"buyPrice", entry.buyPrice},
            {"sellPrice", entry.sellPrice},
            {"canBuy", entry.canBuy},
            {"canSell", entry.canSell},
        });
    }
    return json{
        {"shopId", shop.shopId},
        {"name", shop.name},
        {"items", items},
    };
}

json TeleportToJson(const TeleportDefinition& teleport) {
    return json{
        {"teleportId", teleport.teleportId},
        {"name", teleport.name},
        {"destinationMapId", teleport.destinationMapId},
        {"destinationX", teleport.destinationX},
        {"destinationY", teleport.destinationY},
        {"goldCost", teleport.goldCost},
        {"minLevel", teleport.minLevel},
        {"enabled", teleport.enabled},
    };
}

json LootTableToJson(const LootTableDefinition& table) {
    json entries = json::array();
    for (const auto& entry : table.entries) {
        entries.push_back({
            {"itemDefinitionId", entry.itemDefinitionId},
            {"dropChance", entry.dropChance},
            {"minQuantity", entry.minQuantity},
            {"maxQuantity", entry.maxQuantity},
        });
    }
    return json{
        {"lootTableId", table.lootTableId},
        {"name", table.name},
        {"enabled", table.enabled},
        {"entries", entries},
    };
}

} // namespace

// ---------------------------------------------------------------------------
// LoadGameData
// ---------------------------------------------------------------------------
bool LoadGameData(const std::string& dir, GameDataSet& out, std::string& error) {
    out = GameDataSet{};

    json manifest;
    if (!ReadJsonFile(dir, "game_manifest.json", manifest, error) ||
        !ReadSchemaVersion(manifest, "game_manifest.json", error)) {
        return false;
    }
    const auto contentIt = manifest.find("contentVersion");
    if (contentIt != manifest.end() && contentIt->is_number_integer()) {
        out.manifest.contentVersion = contentIt->get<int>();
    }
    const auto filesIt = manifest.find("files");
    if (filesIt != manifest.end() && filesIt->is_array()) {
        for (const auto& f : *filesIt) {
            if (f.is_string()) {
                out.manifest.files.push_back(f.get<std::string>());
            }
        }
    }

    const auto load = [&](const char* name, auto parse) {
        json root;
        if (!ReadJsonFile(dir, name, root, error) ||
            !ReadSchemaVersion(root, name, error) || !parse(root, out, error)) {
            return false;
        }
        return true;
    };
    if (!load("items.json", ParseItems) || !load("monsters.json", ParseMonsters) ||
        !load("skills.json", ParseSkills) || !load("statuses.json", ParseStatuses) ||
        !load("quests.json", ParseQuests) || !load("shops.json", ParseShops) ||
        !load("teleports.json", ParseTeleports) ||
        !load("loot_tables.json", ParseLootTables)) {
        return false;
    }

    out.manifest.schemaVersion = kGameDataSchemaVersion;
    return true;
}

// ---------------------------------------------------------------------------
// ValidateGameData（23.11 交叉引用 / 23.14 ID 唯一 / 23.16 dropChance 范围）
// ---------------------------------------------------------------------------
bool ValidateGameData(const GameDataSet& game, const WorldDataSet& world, std::string& error,
                      bool crossReference) {
    if (game.manifest.schemaVersion != kGameDataSchemaVersion) {
        error = "game_manifest.json: unsupported schemaVersion " +
                std::to_string(game.manifest.schemaVersion);
        return false;
    }

    // ---- items ----
    std::set<std::uint32_t> itemIds;
    for (const auto& item : game.items) {
        const std::string ctx = "items.json: Item " + std::to_string(item.definitionId);
        if (item.definitionId == 0) {
            error = ctx + ": itemDefinitionId must be > 0";
            return false;
        }
        if (!itemIds.insert(item.definitionId).second) {
            error = ctx + ": duplicate itemDefinitionId";
            return false;
        }
        if (item.name.empty()) {
            error = ctx + ": name must not be empty";
            return false;
        }
        if (item.maxStack == 0 || item.maxStack > 999) {
            error = ctx + ": invalid maxStack";
            return false;
        }
    }
    auto itemExists = [&itemIds](std::uint32_t id) { return itemIds.count(id) != 0; };

    // ---- loot_tables（monsters 引用它，先建索引）----
    std::set<std::uint32_t> lootTableIds;
    for (const auto& table : game.lootTables) {
        const std::string ctx =
            "loot_tables.json: LootTable " + std::to_string(table.lootTableId);
        if (table.lootTableId == 0) {
            error = ctx + ": lootTableId must be > 0";
            return false;
        }
        if (!lootTableIds.insert(table.lootTableId).second) {
            error = ctx + ": duplicate lootTableId";
            return false;
        }
        if (table.entries.empty()) {
            error = ctx + ": entries must not be empty";
            return false;
        }
        for (const auto& entry : table.entries) {
            if (!itemExists(entry.itemDefinitionId)) {
                error = ctx + ": entry item " + std::to_string(entry.itemDefinitionId) +
                        " does not exist";
                return false;
            }
            if (entry.dropChance < 0.0 || entry.dropChance > 1.0) {
                error = ctx + ": dropChance out of [0,1]";
                return false;
            }
            if (entry.minQuantity == 0 || entry.maxQuantity < entry.minQuantity) {
                error = ctx + ": invalid quantity range";
                return false;
            }
        }
    }

    // ---- monsters ----
    std::set<std::uint32_t> monsterIds;
    for (const auto& monster : game.monsters) {
        const std::string ctx =
            "monsters.json: Monster " + std::to_string(monster.monsterTypeId);
        if (monster.monsterTypeId == 0) {
            error = ctx + ": monsterDefinitionId must be > 0";
            return false;
        }
        if (!monsterIds.insert(monster.monsterTypeId).second) {
            error = ctx + ": duplicate monsterDefinitionId";
            return false;
        }
        if (monster.name.empty() || monster.level == 0 || monster.maxHp == 0 ||
            !(monster.moveSpeed > 0.0f)) {
            error = ctx + ": invalid name/level/maxHp/moveSpeed";
            return false;
        }
        if (monster.aggroRadius > monster.leashRadius) {
            error = ctx + ": aggroRange exceeds leashRange";
            return false;
        }
        // 23.11：Monster lootTable 引用存在（0 = 无）。
        if (monster.lootTableId != 0 && lootTableIds.count(monster.lootTableId) == 0) {
            error = ctx + ": lootTable " + std::to_string(monster.lootTableId) +
                    " does not exist";
            return false;
        }
    }
    auto monsterExists = [&monsterIds](std::uint32_t id) { return monsterIds.count(id) != 0; };

    // ---- statuses ----
    std::set<std::uint32_t> statusIds;
    for (const auto& status : game.statuses) {
        const std::string ctx =
            "statuses.json: Status " + std::to_string(status.effectId);
        if (status.effectId == 0) {
            error = ctx + ": statusId must be > 0";
            return false;
        }
        if (!statusIds.insert(status.effectId).second) {
            error = ctx + ": duplicate statusId";
            return false;
        }
        if (status.name.empty() || status.maxStacks == 0 || status.maxStacks > 99 ||
            !(status.moveSpeedMultiplier > 0.0f)) {
            error = ctx + ": invalid name/maxStacks/movementMultiplier";
            return false;
        }
    }
    auto statusExists = [&statusIds](std::uint32_t id) { return statusIds.count(id) != 0; };

    // ---- skills ----
    for (const auto& skill : game.skills) {
        const std::string ctx = "skills.json: Skill " + std::to_string(skill.skillId);
        if (skill.skillId == 0) {
            error = ctx + ": skillId must be > 0";
            return false;
        }
        if (skill.name.empty() || skill.manaCost > 999 || skill.maxTargets == 0 ||
            skill.maxTargets > 16) {
            error = ctx + ": invalid name/manaCost/maxTargets";
            return false;
        }
        // 23.11：Skill 引用 Status 存在。
        if (skill.applyStatusEffectId != 0 && !statusExists(skill.applyStatusEffectId)) {
            error = ctx + ": statusEffect " + std::to_string(skill.applyStatusEffectId) +
                    " does not exist";
            return false;
        }
    }

    // ---- quests ----
    std::set<std::uint32_t> questIds;
    std::set<std::uint32_t> objectiveIds;
    for (const auto& quest : game.quests) {
        const std::string ctx = "quests.json: Quest " + std::to_string(quest.questId);
        if (quest.questId == 0) {
            error = ctx + ": questId must be > 0";
            return false;
        }
        if (!questIds.insert(quest.questId).second) {
            error = ctx + ": duplicate questId";
            return false;
        }
        if (quest.name.empty() || quest.minLevel == 0) {
            error = ctx + ": invalid name/minLevel";
            return false;
        }
        // 23.11：Quest 引用 Monster/Item/NPC 存在。
        for (const auto& objective : quest.objectives) {
            const std::string octx = ctx + ": Objective " +
                                     std::to_string(objective.objectiveId);
            if (objective.objectiveId == 0 ||
                !objectiveIds.insert(objective.objectiveId).second) {
                error = octx + ": objectiveId empty or duplicated across quests";
                return false;
            }
            if (objective.requiredCount == 0) {
                error = octx + ": requiredCount must be > 0";
                return false;
            }
            if (objective.type == QuestObjectiveType::KillMonster &&
                !monsterExists(objective.targetId)) {
                error = octx + ": kill target monster " + std::to_string(objective.targetId) +
                        " does not exist";
                return false;
            }
            if (objective.type == QuestObjectiveType::CollectItem &&
                !itemExists(objective.targetId)) {
                error = octx + ": collect target item " + std::to_string(objective.targetId) +
                        " does not exist";
                return false;
            }
        }
        if (quest.reward.itemDefinitionId != 0) {
            if (!itemExists(quest.reward.itemDefinitionId)) {
                error = ctx + ": reward item " +
                        std::to_string(quest.reward.itemDefinitionId) + " does not exist";
                return false;
            }
            if (quest.reward.itemQuantity == 0) {
                error = ctx + ": reward itemQuantity must be > 0";
                return false;
            }
        }
        // 23.11：Quest 引用的 NPC 存在（World 数据；crossReference=false 时跳过）。
        const auto npcExists = [&world](std::uint32_t npcId) {
            for (const auto& npc : world.npcs) {
                if (npc.npcDefinitionId == npcId) {
                    return true;
                }
            }
            return false;
        };
        if (crossReference && quest.startNpcDefinitionId != 0 &&
            !npcExists(quest.startNpcDefinitionId)) {
            error = ctx + ": startNpc " + std::to_string(quest.startNpcDefinitionId) +
                    " does not exist";
            return false;
        }
        if (crossReference && quest.turnInNpcDefinitionId != 0 &&
            !npcExists(quest.turnInNpcDefinitionId)) {
            error = ctx + ": turnInNpc " + std::to_string(quest.turnInNpcDefinitionId) +
                    " does not exist";
            return false;
        }
    }
    // 前置任务引用（第二遍：所有 questId 已知）。
    for (const auto& quest : game.quests) {
        if (quest.prerequisiteQuestId != 0 &&
            questIds.count(quest.prerequisiteQuestId) == 0) {
            error = "quests.json: Quest " + std::to_string(quest.questId) +
                    ": prerequisite quest " + std::to_string(quest.prerequisiteQuestId) +
                    " does not exist";
            return false;
        }
    }

    // ---- shops ----
    for (const auto& shop : game.shops) {
        const std::string ctx = "shops.json: Shop " + std::to_string(shop.shopId);
        if (shop.shopId == 0) {
            error = ctx + ": shopId must be > 0";
            return false;
        }
        if (shop.entries.empty()) {
            error = ctx + ": entries must not be empty";
            return false;
        }
        std::set<std::uint32_t> seenItems;
        for (const auto& entry : shop.entries) {
            if (!itemExists(entry.itemDefinitionId)) {
                error = ctx + ": item " + std::to_string(entry.itemDefinitionId) +
                        " does not exist";
                return false;
            }
            if (!seenItems.insert(entry.itemDefinitionId).second) {
                error = ctx + ": duplicate item " +
                        std::to_string(entry.itemDefinitionId) + " in shop";
                return false;
            }
        }
    }

    // ---- teleports ----
    for (const auto& teleport : game.teleports) {
        const std::string ctx =
            "teleports.json: Teleport " + std::to_string(teleport.teleportId);
        if (teleport.teleportId == 0) {
            error = ctx + ": teleportId must be > 0";
            return false;
        }
        if (teleport.minLevel < 1) {
            error = ctx + ": invalid minLevel";
            return false;
        }
        // 23.11：Teleport 目标 Map 存在（World 数据；crossReference=false 时跳过）。
        bool mapFound = false;
        for (const auto& map : world.maps) {
            if (map.mapId == teleport.destinationMapId) {
                mapFound = true;
                break;
            }
        }
        if (crossReference && !mapFound) {
            error = ctx + ": destination map " +
                    std::to_string(teleport.destinationMapId) + " does not exist";
            return false;
        }
    }

    // ---- 23.11：World monster_spawns 引用 Game monsters（crossReference 时）----
    if (crossReference) {
        for (const auto& spawn : world.monsterSpawns) {
            if (!monsterExists(spawn.monsterDefinitionId)) {
                error = "monster_spawns.json: MonsterSpawn " + std::to_string(spawn.spawnId) +
                        ": monster " + std::to_string(spawn.monsterDefinitionId) +
                        " does not exist in Data/Game";
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// SaveGameData（原子 + 备份，语义同 SaveWorldData）
// ---------------------------------------------------------------------------
bool SaveGameData(const std::string& dir, const GameDataSet& data, std::string& error) {
    // 保存前做 Game 内部校验（crossReference=false——NPC/Map/Spawn 交叉由调用方的
    // 全量 ValidateGameData 负责：WorldServer Start / Editor 实时 Validation）。
    static const WorldDataSet kEmptyWorld;
    if (!ValidateGameData(data, kEmptyWorld, error, /*crossReference=*/false)) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);

    legend::data::BackupFileForRotate(dir, "items.json");
    legend::data::BackupFileForRotate(dir, "monsters.json");
    legend::data::BackupFileForRotate(dir, "skills.json");
    legend::data::BackupFileForRotate(dir, "statuses.json");
    legend::data::BackupFileForRotate(dir, "quests.json");
    legend::data::BackupFileForRotate(dir, "shops.json");
    legend::data::BackupFileForRotate(dir, "teleports.json");
    legend::data::BackupFileForRotate(dir, "loot_tables.json");
    legend::data::BackupFileForRotate(dir, "game_manifest.json");

    GameDataSet normalized = data;
    normalized.manifest.schemaVersion = kGameDataSchemaVersion;
    normalized.manifest.contentVersion =
        data.manifest.contentVersion == 0 ? kGameDataContentVersion : data.manifest.contentVersion;
    normalized.manifest.files = {"items.json", "monsters.json", "skills.json", "statuses.json",
                                 "quests.json", "shops.json", "teleports.json",
                                 "loot_tables.json"};

    json manifestJson{
        {"schemaVersion", normalized.manifest.schemaVersion},
        {"contentVersion", normalized.manifest.contentVersion},
        {"files", normalized.manifest.files},
    };
    json itemsJson{{"schemaVersion", kGameDataSchemaVersion}, {"items", json::array()}};
    for (const auto& item : normalized.items) {
        itemsJson["items"].push_back(ItemToJson(item));
    }
    json monstersJson{{"schemaVersion", kGameDataSchemaVersion}, {"monsters", json::array()}};
    for (const auto& monster : normalized.monsters) {
        monstersJson["monsters"].push_back(MonsterToJson(monster));
    }
    json skillsJson{{"schemaVersion", kGameDataSchemaVersion}, {"skills", json::array()}};
    for (const auto& skill : normalized.skills) {
        skillsJson["skills"].push_back(SkillToJson(skill));
    }
    json statusesJson{{"schemaVersion", kGameDataSchemaVersion}, {"statuses", json::array()}};
    for (const auto& status : normalized.statuses) {
        statusesJson["statuses"].push_back(StatusToJson(status));
    }
    json questsJson{{"schemaVersion", kGameDataSchemaVersion}, {"quests", json::array()}};
    for (const auto& quest : normalized.quests) {
        questsJson["quests"].push_back(QuestToJson(quest));
    }
    json shopsJson{{"schemaVersion", kGameDataSchemaVersion}, {"shops", json::array()}};
    for (const auto& shop : normalized.shops) {
        shopsJson["shops"].push_back(ShopToJson(shop));
    }
    json teleportsJson{{"schemaVersion", kGameDataSchemaVersion}, {"teleports", json::array()}};
    for (const auto& teleport : normalized.teleports) {
        teleportsJson["teleports"].push_back(TeleportToJson(teleport));
    }
    json lootJson{{"schemaVersion", kGameDataSchemaVersion}, {"lootTables", json::array()}};
    for (const auto& table : normalized.lootTables) {
        lootJson["lootTables"].push_back(LootTableToJson(table));
    }

    return legend::data::WriteAtomicFile(dir, "items.json", itemsJson, error) &&
           legend::data::WriteAtomicFile(dir, "monsters.json", monstersJson, error) &&
           legend::data::WriteAtomicFile(dir, "skills.json", skillsJson, error) &&
           legend::data::WriteAtomicFile(dir, "statuses.json", statusesJson, error) &&
           legend::data::WriteAtomicFile(dir, "quests.json", questsJson, error) &&
           legend::data::WriteAtomicFile(dir, "shops.json", shopsJson, error) &&
           legend::data::WriteAtomicFile(dir, "teleports.json", teleportsJson, error) &&
           legend::data::WriteAtomicFile(dir, "loot_tables.json", lootJson, error) &&
           legend::data::WriteAtomicFile(dir, "game_manifest.json", manifestJson, error);
}

// ---------------------------------------------------------------------------
// MakeDefaultGameData（23.24 迁移：阶段15~20 硬编码 → 单一事实来源）
// ---------------------------------------------------------------------------
GameDataSet MakeDefaultGameData() {
    GameDataSet data;
    data.manifest.schemaVersion = kGameDataSchemaVersion;
    data.manifest.contentVersion = kGameDataContentVersion;
    data.manifest.files = {"items.json", "monsters.json", "skills.json", "statuses.json",
                           "quests.json", "shops.json", "teleports.json", "loot_tables.json"};

    // ---- Items（阶段18 指令三）----
    data.items.push_back({kItemRustySwordId, "Rusty Sword", ItemType::Weapon, 1, 3, 0,
                          EquipmentSlot::Weapon, true, true, "item_rusty_sword", true});
    data.items.push_back({kItemClothArmorId, "Cloth Armor", ItemType::Armor, 1, 0, 2,
                          EquipmentSlot::Armor, true, true, "item_cloth_armor", true});
    data.items.push_back({kItemSlimeCoreId, "Slime Core", ItemType::Material,
                          kSlimeCoreMaxStack, 0, 0, EquipmentSlot::None, true, true,
                          "item_slime_core", true});

    // ---- Monsters（阶段13/14/17；阶段24：visualId 视觉引用）----
    {
        MonsterDefinition slime = kTrainingSlimeDefinition;
        slime.visualId = "training_slime";
        data.monsters.push_back(std::move(slime));
    }

    // ---- Skills（阶段15/16：5 个）----
    data.skills.push_back(kQuickStrikeDefinition);
    data.skills.push_back(kFireBoltDefinition);
    data.skills.push_back(kWhirlwindDefinition);
    data.skills.push_back(kBattleFocusSkillDefinition);
    data.skills.push_back(kCripplingStrikeSkillDefinition);

    // ---- Statuses（阶段16：5 个）----
    data.statuses.push_back(kBattleFocusDefinition);
    data.statuses.push_back(kArmorBreakDefinition);
    data.statuses.push_back(kBurnDefinition);
    data.statuses.push_back(kPoisonDefinition);
    data.statuses.push_back(kSlowDefinition);

    // ---- Quests（阶段19/20：4001~4005）----
    {
        QuestDefinition quest;
        quest.questId = 4001;
        quest.name = "Slime Hunter";
        quest.description = "Defeat 5 Training Slimes.";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = 0;
        quest.repeatable = false;
        QuestObjectiveDefinition kill;
        kill.objectiveId = 40011;
        kill.type = QuestObjectiveType::KillMonster;
        kill.targetId = kTrainingSlimeTypeId;
        kill.requiredCount = 5;
        quest.objectives.push_back(kill);
        quest.reward.exp = 100;
        quest.reward.gold = 20;
        quest.startNpcDefinitionId = 5001;
        quest.turnInNpcDefinitionId = 5001;
        data.quests.push_back(std::move(quest));
    }
    {
        QuestDefinition quest;
        quest.questId = 4002;
        quest.name = "Core Collector";
        quest.description = "Collect 3 Slime Cores.";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = 4001;
        quest.repeatable = false;
        QuestObjectiveDefinition collect;
        collect.objectiveId = 40021;
        collect.type = QuestObjectiveType::CollectItem;
        collect.targetId = kItemSlimeCoreId;
        collect.requiredCount = 3;
        quest.objectives.push_back(collect);
        quest.reward.exp = 80;
        quest.reward.gold = 10;
        quest.startNpcDefinitionId = 5001;
        quest.turnInNpcDefinitionId = 5001;
        data.quests.push_back(std::move(quest));
    }
    {
        QuestDefinition quest;
        quest.questId = 4003;
        quest.name = "Growing Warrior";
        quest.description = "Reach level 3.";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = 4001;
        quest.repeatable = false;
        QuestObjectiveDefinition reach;
        reach.objectiveId = 40031;
        reach.type = QuestObjectiveType::ReachLevel;
        reach.targetId = 3;
        reach.requiredCount = 1;
        quest.objectives.push_back(reach);
        quest.reward.exp = 0;
        quest.reward.gold = 50;
        quest.startNpcDefinitionId = 5001;
        quest.turnInNpcDefinitionId = 5001;
        data.quests.push_back(std::move(quest));
    }
    {
        QuestDefinition quest;
        quest.questId = 4004;
        quest.name = "Explorer";
        quest.description = "Explore the far plains (1500,1500).";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = 0;
        quest.repeatable = false;
        QuestObjectiveDefinition area;
        area.objectiveId = 40041;
        area.type = QuestObjectiveType::ReachArea;
        area.targetId = 0;
        area.requiredCount = 1;
        area.mapId = 1;
        area.areaX = 1500.0f;
        area.areaY = 1500.0f;
        area.areaRadius = 100.0f;
        quest.objectives.push_back(area);
        quest.reward.exp = 50;
        quest.reward.gold = 10;
        quest.startNpcDefinitionId = 5004;
        quest.turnInNpcDefinitionId = 5004;
        data.quests.push_back(std::move(quest));
    }
    {
        QuestDefinition quest;
        quest.questId = 4005;
        quest.name = "Slime Cleanup";
        quest.description = "Kill 3 slimes and gather 2 cores.";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = 4002;
        quest.repeatable = false;
        QuestObjectiveDefinition kill;
        kill.objectiveId = 40051;
        kill.type = QuestObjectiveType::KillMonster;
        kill.targetId = kTrainingSlimeTypeId;
        kill.requiredCount = 3;
        quest.objectives.push_back(kill);
        QuestObjectiveDefinition collect;
        collect.objectiveId = 40052;
        collect.type = QuestObjectiveType::CollectItem;
        collect.targetId = kItemSlimeCoreId;
        collect.requiredCount = 2;
        quest.objectives.push_back(collect);
        quest.reward.exp = 150;
        quest.reward.gold = 30;
        quest.reward.itemDefinitionId = kItemRustySwordId;
        quest.reward.itemQuantity = 1;
        quest.startNpcDefinitionId = 5001;
        quest.turnInNpcDefinitionId = 5001;
        data.quests.push_back(std::move(quest));
    }

    // ---- Shops（阶段20 指令三十六：Shop 6001）----
    {
        ShopDefinition shop;
        shop.shopId = 6001;
        shop.name = "General Merchant";
        shop.entries.push_back({kItemSlimeCoreId, 10, 3, true, true});
        shop.entries.push_back({kItemRustySwordId, 100, 30, true, true});
        shop.entries.push_back({kItemClothArmorId, 120, 40, true, true});
        data.shops.push_back(std::move(shop));
    }

    // ---- Teleports（阶段20 指令五十八：7001/7002）----
    data.teleports.push_back({7001, "Far Plains", 1, 1500.0f, 1500.0f, 20, 1, true});
    data.teleports.push_back({7002, "Village Square", 1, 300.0f, 300.0f, 0, 1, true});

    // ---- Loot Tables（23.16：Training Slime 迁移——与阶段18 DropRoller::TrainingSlimeTable 一致）----
    {
        LootTableDefinition table;
        table.lootTableId = 1;
        table.name = "Training Slime Loot";
        table.enabled = true;
        table.entries.push_back({kItemSlimeCoreId, 1.00, 1, 1});
        table.entries.push_back({kItemRustySwordId, 0.20, 1, 1});
        table.entries.push_back({kItemClothArmorId, 0.20, 1, 1});
        data.lootTables.push_back(std::move(table));
    }
    return data;
}

} // namespace legend::world
