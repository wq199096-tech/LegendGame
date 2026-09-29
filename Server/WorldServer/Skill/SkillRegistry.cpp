#include "Server/WorldServer/Skill/SkillRegistry.h"

#include "Shared/GameData/GameDataJson.h"

namespace legend::world {

SkillRegistry::SkillRegistry() {
    // 阶段23 23.22：构造即装载出厂默认（保持"构造后可用"语义——早期调用点/
    // 测试依赖）；WorldServer::Start 随后用 Data/Game 数据覆盖注入。
    LoadDefaults();
}

const SkillDefinition* SkillRegistry::FindSkill(SkillId skillId) const {
    for (const auto& skill : m_skills) {
        if (skill.skillId == skillId) {
            return &skill;
        }
    }
    return nullptr;
}

void SkillRegistry::LoadFromDefinitions(std::vector<SkillDefinition> skills) {
    m_skills = std::move(skills);
}

void SkillRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultGameData().skills);
}

} // namespace legend::world
