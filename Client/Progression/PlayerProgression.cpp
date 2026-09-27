#include "Client/Progression/PlayerProgression.h"

#include <algorithm>

#include "Engine/Animation/AnimationLoader.h"
#include "Engine/Debug/Logger.h"

namespace legend::progression {

void PlayerProgression::Initialize(const legend::animation::CharacterDefinition& definition) {
    // growth 块已在 Loader 中解析（缺省 20/5/2）
    m_growth = definition.growth;
    m_level = 1;
    m_currentExp = 0;
    m_totalExp = 0;
    m_pendingLevelUps.clear();
}

std::vector<LevelUpEvent> PlayerProgression::AddExperience(combat::CombatStats& stats,
                                                           int amount) {
    auto events = LevelSystem::AddExperience(m_level, m_currentExp, m_totalExp, amount);
    // 每升一级应用一次成长：MaxHP +X -> 当前 HP 同步 +X（不直接满血）；clamp 防越界
    for (const LevelUpEvent& event : events) {
        stats.maxHp += m_growth.maxHpPerLevel;
        stats.hp = std::min(stats.hp + m_growth.maxHpPerLevel, stats.maxHp);
        stats.attack += m_growth.attackPerLevel;
        stats.defense += m_growth.defensePerLevel;
        LOG_INFO("[LevelUp] Player reached level " + std::to_string(event.newLevel) +
                 " (from " + std::to_string(event.oldLevel) + ") HP " +
                 std::to_string(stats.hp) + "/" + std::to_string(stats.maxHp));
    }
    if (!events.empty()) {
        m_pendingLevelUps.insert(m_pendingLevelUps.end(), events.begin(), events.end());
    }
    return events;
}

float PlayerProgression::GetExpPercent() const {
    if (m_level >= kMaxLevel) {
        return 1.0f; // 满级
    }
    const int required = GetRequiredExp();
    return required > 0 ? static_cast<float>(m_currentExp) / static_cast<float>(required) : 0.0f;
}

} // namespace legend::progression
