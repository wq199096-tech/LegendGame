#include "Server/WorldServer/Skill/SkillRegistry.h"

namespace legend::world {

SkillRegistry::SkillRegistry() {
    // 阶段15 指令三十四：硬编码固定测试技能；阶段16 指令十九/二十：+Battle Focus/Crippling Strike。
    m_skills.push_back(kQuickStrikeDefinition);
    m_skills.push_back(kFireBoltDefinition);
    m_skills.push_back(kWhirlwindDefinition);
    m_skills.push_back(kBattleFocusSkillDefinition);
    m_skills.push_back(kCripplingStrikeSkillDefinition);
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
