#pragma once

#include <cstdint>
#include <string>

namespace legend::skill {

// 技能目标类型（阶段8只实现 SingleTarget / SelfArea；
// GroundPoint / Direction / Projectile 预留枚举值，不实现行为）
enum class SkillTargetType : uint8_t {
    SingleTarget = 0, // 需要选中目标（PlayerCombatController 的 CombatTarget）
    SelfArea = 1,     // 以施法者 Feet 为中心的 AOE（无需目标）
    // ---- 以下为预留值，阶段8不实现，解析层直接拒绝 ----
    GroundPoint = 2,
    Direction = 3,
    Projectile = 4,
};

// 技能效果类型（阶段8只做 Damage；Heal/Buff/Debuff/Shield 以后扩展）
enum class SkillEffectType : uint8_t {
    Damage = 0,
};

// 目标类型字符串 -> 枚举；未知字符串返回 false（非法 Definition 会被跳过）
bool ParseSkillTargetType(const std::string& name, SkillTargetType& out);
// 效果类型字符串 -> 枚举；未知字符串返回 false
bool ParseSkillEffectType(const std::string& name, SkillEffectType& out);

const char* SkillTargetTypeName(SkillTargetType type);
const char* SkillEffectTypeName(SkillEffectType type);

} // namespace legend::skill
