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

std::vector<LevelUpEvent> PlayerProgression::AddExperience(ExperienceValue amount) {
    auto events = LevelSystem::AddExperience(m_level, m_currentExp, m_totalExp, amount);
    // 阶段7：growth 属性应用移到 PlayerStatsComponent（Base Stats 架构）——
    // 本组件只产生升级事件；HP/攻击/防御的成长由调用方 ApplyLevelGrowth 后 Recalculate。
    // 仍输出 [LevelUp] 日志（不带 stats 数值——final 属性在 Recalculate 后由调用方打印）。
    for (const LevelUpEvent& event : events) {
        LOG_INFO("[LevelUp] Player reached level " + std::to_string(event.newLevel) +
                 " (from " + std::to_string(event.oldLevel) + ")");
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
    const ExperienceValue required = GetRequiredExp();
    return required > 0 ? static_cast<float>(static_cast<double>(m_currentExp) /
                                             static_cast<double>(required))
                        : 0.0f;
}

// ---- 阶段8.2：快照 / 恢复（SkillWorldSnapshot 隔离验收用） ----

PlayerProgressionSnapshot PlayerProgression::CreateSnapshot() const {
    PlayerProgressionSnapshot snapshot;
    snapshot.level = m_level;
    snapshot.currentExp = m_currentExp;
    snapshot.totalExp = m_totalExp;
    return snapshot;
}

bool PlayerProgression::RestoreSnapshot(const PlayerProgressionSnapshot& snapshot) {
    // 校验（指令三）：level ∈ [1, kMaxLevel]、currentExp/totalExp >= 0
    if (snapshot.level < 1 || snapshot.level > kMaxLevel) {
        LOG_ERROR("PlayerProgression::RestoreSnapshot: invalid level " +
                  std::to_string(snapshot.level) + ", rejected.");
        return false;
    }
    if (snapshot.currentExp < 0 || snapshot.totalExp < 0) {
        LOG_ERROR("PlayerProgression::RestoreSnapshot: negative exp, rejected.");
        return false;
    }
    m_level = snapshot.level;
    m_currentExp = snapshot.currentExp;
    m_totalExp = snapshot.totalExp;
    // 清空 pending 升级事件——不留测试升级事件（指令十一）
    m_pendingLevelUps.clear();
    return true;
}

} // namespace legend::progression
