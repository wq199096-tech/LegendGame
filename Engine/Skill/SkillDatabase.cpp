#include "Engine/Skill/SkillDatabase.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>

#include "Engine/Debug/Logger.h"

namespace legend::skill {

namespace {
using json = nlohmann::json;
constexpr int kSupportedVersion = 1;

// 单条技能解析 + 校验；非法返回 false（调用方跳过 + 已打日志）
bool ParseSkillEntry(const json& entry, SkillDefinition& out) {
    if (!entry.is_object()) {
        LOG_WARN("SkillDatabase: skill entry is not an object, skipped.");
        return false;
    }
    if (!entry.contains("id") || !entry["id"].is_string() ||
        !entry.contains("name") || !entry["name"].is_string()) {
        LOG_WARN("SkillDatabase: skill entry missing id/name, skipped.");
        return false;
    }
    out.id = entry["id"].get<std::string>();
    out.name = entry["name"].get<std::string>();
    // targetType：未知字符串拒绝（不默认 SingleTarget）
    const std::string targetType = entry.value("targetType", std::string());
    if (!ParseSkillTargetType(targetType, out.targetType)) {
        LOG_WARN("SkillDatabase: skill '" + out.id + "' unknown targetType '" + targetType +
                 "', skipped.");
        return false;
    }
    // effect：阶段8只接受 Damage
    const std::string effect = entry.value("effect", std::string("Damage"));
    if (!ParseSkillEffectType(effect, out.effect)) {
        LOG_WARN("SkillDatabase: skill '" + out.id + "' unknown effect '" + effect +
                 "', skipped.");
        return false;
    }
    out.damageMultiplier = entry.value("damageMultiplier", 0.0f);
    out.manaCost = entry.value("manaCost", 0.0f);
    out.cooldown = entry.value("cooldown", 0.0f);
    out.castRange = entry.value("castRange", 0.0f);
    out.aoeRadius = entry.value("aoeRadius", 0.0f);
    out.animation = entry.value("animation", std::string());
    out.animationEvent = entry.value("event", std::string());
    out.requiresTarget = entry.value("requiresTarget", out.targetType == SkillTargetType::SingleTarget);
    if (!out.IsValid()) {
        LOG_WARN("SkillDatabase: skill '" + out.id + "' invalid definition (multiplier/range/"
                 "radius/animation/event), skipped.");
        return false;
    }
    return true;
}

} // namespace

bool SkillDatabase::LoadFromFile(const std::string& filePath) {
    m_skills.clear();
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("SkillDatabase: cannot open skills file: " + filePath);
        return false;
    }
    json root;
    try {
        file >> root;
    } catch (const json::parse_error& error) {
        LOG_ERROR("SkillDatabase: JSON parse error in '" + filePath + "': " + error.what());
        return false;
    }
    if (!root.is_object()) {
        LOG_ERROR("SkillDatabase: skills file root must be an object: " + filePath);
        return false;
    }
    if (!root.contains("version") || root["version"].get<int>() != kSupportedVersion) {
        LOG_ERROR("SkillDatabase: unsupported skills version in " + filePath + " (expected " +
                  std::to_string(kSupportedVersion) + ").");
        return false;
    }
    if (!root.contains("skills") || !root["skills"].is_array()) {
        LOG_ERROR("SkillDatabase: skills file missing 'skills' array: " + filePath);
        return false;
    }

    std::set<std::string> seenIds; // 重复 id 拒绝（后者跳过）
    for (const auto& entry : root["skills"]) {
        SkillDefinition definition;
        if (!ParseSkillEntry(entry, definition)) {
            continue; // 非法定义跳过（日志已在 Parse 内）
        }
        if (!seenIds.insert(definition.id).second) {
            LOG_WARN("SkillDatabase: duplicate skill id '" + definition.id + "', skipped.");
            continue;
        }
        m_skills[definition.id] = std::move(definition);
    }

    LOG_INFO("SkillDatabase loaded: " + std::to_string(m_skills.size()) + " skills (" + filePath +
             ")");
    return true;
}

const SkillDefinition* SkillDatabase::Get(const std::string& id) const {
    const auto it = m_skills.find(id);
    return it != m_skills.end() ? &it->second : nullptr;
}

} // namespace legend::skill
