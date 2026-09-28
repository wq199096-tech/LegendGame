#pragma once

#include "Shared/Skill/SkillDefinition.h"
#include "Shared/Skill/SkillTypes.h"

#include <cstddef>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段15 指令三十四：SkillRegistry —— 技能定义注册表。
// 阶段15 硬编码三个 SkillDefinition（指令九十六：Definition 不能由 Client 上传，
// Client 只传 skillId；不上 JSON、不进数据库）。
// ---------------------------------------------------------------------------
class SkillRegistry {
public:
    SkillRegistry();

    // 查找技能定义；未知 skillId 返回 nullptr（-> UnknownSkill，指令八十八）。
    const SkillDefinition* FindSkill(SkillId skillId) const;
    std::size_t Count() const { return m_skills.size(); }

private:
    std::vector<SkillDefinition> m_skills; // 稳定地址（指针指向元素）
};

} // namespace legend::world
