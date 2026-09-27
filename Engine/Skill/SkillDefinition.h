#pragma once

#include <string>

#include "Engine/Skill/SkillTypes.h"

namespace legend::skill {

// 技能静态定义（skills.json 单条映射）：只存数据，不存运行时 CD / MP。
// 运行时状态在 SkillCooldowns / SkillResource（阶段8指令十八）。
struct SkillDefinition {
    std::string id;              // 唯一 id（如 power_slash）
    std::string name;            // 显示名（如 Power Slash）
    SkillTargetType targetType = SkillTargetType::SingleTarget;
    SkillEffectType effect = SkillEffectType::Damage; // 阶段8只 Damage
    float damageMultiplier = 1.0f; // rawDamage = attackSnapshot * multiplier
    float manaCost = 0.0f;
    float cooldown = 0.0f;
    float castRange = 0.0f;      // SingleTarget：Feet 距离上限
    float aoeRadius = 0.0f;      // SelfArea：AOE 半径
    std::string animation;       // 技能 Clip 名（如 skill_power_slash）
    std::string animationEvent;  // 施法事件名（如 skill_hit），触发时比较不硬编码
    bool requiresTarget = true;  // SingleTarget=true / SelfArea=false

    // 阶段8指令十一：非法 Definition 由 SkillDatabase 跳过并 LOG
    bool IsValid() const {
        if (id.empty() || name.empty() || animation.empty() || animationEvent.empty()) {
            return false;
        }
        if (damageMultiplier <= 0.0f) {
            return false;
        }
        if (manaCost < 0.0f || cooldown < 0.0f || castRange < 0.0f || aoeRadius < 0.0f) {
            return false;
        }
        if (targetType == SkillTargetType::SingleTarget && castRange <= 0.0f) {
            return false; // 单体目标必须指定射程
        }
        if (targetType == SkillTargetType::SelfArea && aoeRadius <= 0.0f) {
            return false; // 自身AOE必须指定半径
        }
        if (effect != SkillEffectType::Damage) {
            return false; // 阶段8只支持 Damage
        }
        return true;
    }
};

} // namespace legend::skill
