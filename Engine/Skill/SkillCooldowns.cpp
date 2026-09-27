#include "Engine/Skill/SkillCooldowns.h"

namespace legend::skill {

void SkillCooldowns::StartCooldown(const std::string& skillId, float duration) {
    if (duration <= 0.0f) {
        return; // 0 秒 CD 无需记录
    }
    m_remaining[skillId] = duration;
}

void SkillCooldowns::Update(float deltaTime) {
    if (m_remaining.empty()) {
        return;
    }
    for (auto it = m_remaining.begin(); it != m_remaining.end();) {
        it->second -= deltaTime;
        if (it->second <= 0.0f) {
            it = m_remaining.erase(it); // 归零即 Ready，删除记录（无负值残留）
        } else {
            ++it;
        }
    }
}

bool SkillCooldowns::IsReady(const std::string& skillId) const {
    return m_remaining.count(skillId) == 0;
}

float SkillCooldowns::GetRemaining(const std::string& skillId) const {
    const auto it = m_remaining.find(skillId);
    return it != m_remaining.end() ? it->second : 0.0f;
}

void SkillCooldowns::Reset(const std::string& skillId) {
    m_remaining.erase(skillId);
}

void SkillCooldowns::ResetAll() {
    m_remaining.clear();
}

} // namespace legend::skill
