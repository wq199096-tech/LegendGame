#include "Client/World/MonsterDefinition.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include "Engine/Debug/Logger.h"

using json = nlohmann::json; // LoadMonsterRegistry 已移出匿名命名空间，别名提升到全局

namespace legend::world {

namespace {
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
    return true;
}

} // namespace

// 跨字段统一校验（导出供 [MonsterCombatConfigCheck] 构造非法配置验证）：
// 必须先完整解析 AI + Combat 再调用（禁止解析中途互相引用）。
// 任一规则非法 -> 该模板无效（调用方跳过）。
bool ValidateMonsterDefinition(const MonsterDefinition& definition) {
    const std::string& id = definition.id;
    // ---- combat 基础 ----
    if (!definition.combat.IsValid()) {
        LOG_ERROR("MonsterDefinition: '" + id +
                  "' invalid combat (need maxHp>0, attackRange>0, attackInterval>0).");
        return false;
    }
    // ---- AI 范围规则 ----
    if (definition.ai.resumeDistance <= definition.ai.stopDistance) {
        LOG_ERROR("MonsterDefinition: '" + id + "' resumeDistance must be > stopDistance.");
        return false;
    }
    if (definition.ai.leashRange <= definition.ai.aggroRange) {
        LOG_ERROR("MonsterDefinition: '" + id + "' leashRange must be > aggroRange.");
        return false;
    }
    if (definition.ai.wanderIntervalMin < 0.0f ||
        definition.ai.wanderIntervalMax < definition.ai.wanderIntervalMin) {
        LOG_ERROR("MonsterDefinition: '" + id + "' invalid wanderInterval range [" +
                  std::to_string(definition.ai.wanderIntervalMin) + ", " +
                  std::to_string(definition.ai.wanderIntervalMax) + "] (need Min >= 0, Max >= Min).");
        return false;
    }
    // ---- 跨字段：怪物进入攻击范围后才攻击，停步距离必须 <= attackRange（含极小容差）。
    //      禁止 attackRange 65 / stopDistance 100 这类"停住但打不到"的配置 ----
    if (definition.ai.stopDistance > definition.combat.attackRange + 1.0f) {
        LOG_ERROR("MonsterDefinition: '" + id + "' stopDistance (" +
                  std::to_string(definition.ai.stopDistance) + ") > attackRange (" +
                  std::to_string(definition.combat.attackRange) + "): monster would stop out of range.");
        return false;
    }
    return true;
}

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
        // ---- 先完整解析 AI 与 Combat（顺序固定，禁止中途互相引用） ----
        if (entry.contains("combat") && entry["combat"].is_object()) {
            if (!ParseCombatBlock(entry["combat"], definition.combat)) {
                ++skipped;
                continue;
            }
        }
        if (entry.contains("ai") && entry["ai"].is_object()) {
            if (!ParseAiBlock(entry["ai"], definition.ai)) {
                ++skipped;
                continue;
            }
        }
        // ---- 再统一跨字段校验（非法模板跳过） ----
        if (!ValidateMonsterDefinition(definition)) {
            ++skipped;
            continue;
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
