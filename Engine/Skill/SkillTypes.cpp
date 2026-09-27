#include "Engine/Skill/SkillTypes.h"

namespace legend::skill {

bool ParseSkillTargetType(const std::string& name, SkillTargetType& out) {
    if (name == "SingleTarget") {
        out = SkillTargetType::SingleTarget;
        return true;
    }
    if (name == "SelfArea") {
        out = SkillTargetType::SelfArea;
        return true;
    }
    // GroundPoint / Direction / Projectile 为预留值：阶段8解析层直接拒绝
    return false;
}

bool ParseSkillEffectType(const std::string& name, SkillEffectType& out) {
    if (name == "Damage") {
        out = SkillEffectType::Damage;
        return true;
    }
    // Heal / Buff / Debuff / Shield 以后扩展，当前全部拒绝
    return false;
}

const char* SkillTargetTypeName(SkillTargetType type) {
    switch (type) {
    case SkillTargetType::SingleTarget:
        return "SingleTarget";
    case SkillTargetType::SelfArea:
        return "SelfArea";
    case SkillTargetType::GroundPoint:
        return "GroundPoint";
    case SkillTargetType::Direction:
        return "Direction";
    case SkillTargetType::Projectile:
        return "Projectile";
    default:
        return "Unknown";
    }
}

const char* SkillEffectTypeName(SkillEffectType type) {
    return type == SkillEffectType::Damage ? "Damage" : "Unknown";
}

} // namespace legend::skill
