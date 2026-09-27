#include "Client/World/MonsterDefinition.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include "Engine/Debug/Logger.h"

namespace legend::world {

namespace {
using json = nlohmann::json;
constexpr int kSupportedVersion = 1;

bool ParseCombatBlock(const json& cmb, combat::CombatStats& out) {
    out.maxHp = cmb.value("maxHp", 100.0f);
    out.hp = out.maxHp;
    out.attack = cmb.value("attack", 10.0f);
    out.defense = cmb.value("defense", 0.0f);
    out.attackRange = cmb.value("attackRange", 80.0f);
    out.attackInterval = cmb.value("attackInterval", 1.0f);
    if (!out.IsValid()) {
        LOG_ERROR("MonsterDefinition: invalid combat block (maxHp>0, range>0, interval>0 required).");
        return false;
    }
    return true;
}

bool ParseAiBlock(const json& ai, MonsterAIDefinition& out) {
    out.aggroRange = ai.value("aggroRange", 300.0f);
    out.leashRange = ai.value("leashRange", 600.0f);
    out.wanderRadius = ai.value("wanderRadius", 180.0f);
    out.wanderIntervalMin = ai.value("wanderIntervalMin", 2.0f);
    out.wanderIntervalMax = ai.value("wanderIntervalMax", 5.0f);
    out.stopDistance = ai.value("stopDistance", 60.0f);
    out.resumeDistance = ai.value("resumeDistance", 80.0f);
    // 参数合法性：resume 必须大于 stop（滞回）；leash 必须大于 aggro；
    // wanderIntervalMin >= 0 且 Max >= Min（Idle 等待时长数据驱动）
    if (out.resumeDistance <= out.stopDistance) {
        LOG_ERROR("MonsterDefinition: resumeDistance must be > stopDistance.");
        return false;
    }
    if (out.leashRange <= out.aggroRange) {
        LOG_ERROR("MonsterDefinition: leashRange must be > aggroRange.");
        return false;
    }
    if (out.wanderIntervalMin < 0.0f || out.wanderIntervalMax < out.wanderIntervalMin) {
        LOG_ERROR("MonsterDefinition: invalid wanderInterval range [" +
                  std::to_string(out.wanderIntervalMin) + ", " +
                  std::to_string(out.wanderIntervalMax) + "] (need Min >= 0, Max >= Min).");
        return false;
    }
    return true;
}
} // namespace

bool LoadMonsterRegistry(const std::string& filePath,
                         std::unordered_map<std::string, MonsterDefinition>& out) {
    out.clear();
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("MonsterDefinition: cannot open monster registry: " + filePath);
        return false;
    }
    json root;
    try {
        file >> root;
    } catch (const json::parse_error& error) {
        LOG_ERROR("MonsterDefinition: JSON parse error in '" + filePath + "': " + error.what());
        return false;
    }
    if (!root.is_object() || !root.contains("monsters") || !root["monsters"].is_array()) {
        LOG_ERROR("MonsterDefinition: registry must contain 'monsters' array: " + filePath);
        return false;
    }
    if (root.contains("version") && root["version"].get<int>() != kSupportedVersion) {
        LOG_ERROR("MonsterDefinition: unsupported registry version in " + filePath);
        return false;
    }

    int skipped = 0;
    for (const auto& entry : root["monsters"]) {
        if (!entry.is_object() || !entry.contains("id") || !entry.contains("character")) {
            ++skipped;
            continue;
        }
        MonsterDefinition definition;
        definition.id = entry["id"].get<std::string>();
        definition.name = entry.value("name", definition.id);
        definition.characterPath = entry["character"].get<std::string>();
        // combat 块：数据驱动战斗属性；缺省给保守默认（可配置原则）
        if (entry.contains("combat") && entry["combat"].is_object()) {
            const json& cb = entry["combat"];
            definition.combat.maxHp = cb.value("maxHp", 100.0f);
            definition.combat.hp = definition.combat.maxHp;
            definition.combat.attack = cb.value("attack", 10.0f);
            definition.combat.defense = cb.value("defense", 0.0f);
            definition.combat.attackRange = cb.value("attackRange", 70.0f);
            definition.combat.attackInterval = cb.value("attackInterval", 1.5f);
            if (!definition.combat.IsValid()) {
                LOG_ERROR("MonsterDefinition: '" + definition.id +
                          "' has invalid combat block, template skipped.");
                ++skipped;
                continue;
            }
            // 停步距离必须 <= attackRange（否则永远打不到目标）
            if (definition.ai.stopDistance > definition.combat.attackRange + 1.0f) {
                LOG_WARN("MonsterDefinition: '" + definition.id +
                         "' stopDistance > attackRange, chase may stop out of range.");
            }
        }
        if (entry.contains("ai") && entry["ai"].is_object()) {
            if (!ParseAiBlock(entry["ai"], definition.ai)) {
                ++skipped;
                continue;
            }
        }
        if (out.count(definition.id) > 0) {
            LOG_WARN("MonsterDefinition: duplicate id '" + definition.id + "', skipped.");
            ++skipped;
            continue;
        }
        out[definition.id] = std::move(definition);
    }
    if (skipped > 0) {
        LOG_WARN("MonsterDefinition: skipped " + std::to_string(skipped) + " invalid entries.");
    }
    LOG_INFO("Monster registry loaded: " + std::to_string(out.size()) + " template(s) (" +
             filePath + ")");
    return !out.empty();
}

} // namespace legend::world
