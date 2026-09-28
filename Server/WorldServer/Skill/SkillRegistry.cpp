#include "Server/WorldServer/Skill/SkillRegistry.h"

namespace legend::world {

SkillRegistry::SkillRegistry() {
    // 阶段15 指令三十四：硬编码三个固定测试技能（指令八/九/十）。
    m_skills.push_back(kQuickStrikeDefinition);
    m_skills.push_back(kFireBoltDefinition);
    m_skills.push_back(kWhirlwindDefinition);
}

const SkillDefinition* SkillRegistry::FindSkill(SkillId skillId) const {
    for (const auto& skill : m_skills) {
        if (skill.skillId == skillId) {
            return &skill;
        }
    }
    return nullptr;
}

} // namespace legend::world
