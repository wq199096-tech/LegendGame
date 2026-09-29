#pragma once

#include "Shared/Quest/QuestDefinition.h"
#include "Shared/Quest/QuestTypes.h"

#include <cstdint>
#include <unordered_map>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段19 指令十三：PlayerQuestState —— 单个任务的玩家权威状态。
// 仅 World io 线程访问（PlayerQuestContainer 内，无锁）；持久化经 QuestRepository
//（DbWorker 线程，指令十八）。
// ---------------------------------------------------------------------------
class PlayerQuestState {
public:
    QuestId questId = 0;
    QuestState state = QuestState::NotAccepted;
    std::int64_t acceptedAt = 0;  // Unix 秒（0 = 未接取）
    std::int64_t completedAt = 0; // ReadyToTurnIn 时刻
    std::int64_t turnedInAt = 0;  // TurnIn 完成时刻

    // 目标进度（指令十三：key = objectiveId）。
    const std::unordered_map<std::uint32_t, std::uint32_t>& ObjectiveProgress() const {
        return m_objectiveProgress;
    }
    std::uint32_t ProgressOf(std::uint32_t objectiveId) const {
        const auto it = m_objectiveProgress.find(objectiveId);
        return it != m_objectiveProgress.end() ? it->second : 0;
    }
    // 写入进度（返回是否发生变化——已等于目标值时不再写，指令五十八/七十三）。
    bool SetObjectiveProgress(std::uint32_t objectiveId, std::uint32_t progress) {
        auto it = m_objectiveProgress.find(objectiveId);
        if (it != m_objectiveProgress.end() && it->second == progress) {
            return false;
        }
        m_objectiveProgress[objectiveId] = progress;
        return true;
    }
    // 接取/重新接取：全部进度清零（指令四十三/十七）。
    void ResetProgress() { m_objectiveProgress.clear(); }
    // 用注册表定义初始化全部目标（缺省 0）。
    void InitObjectives(const QuestDefinition& definition) {
        for (const auto& objective : definition.objectives) {
            m_objectiveProgress.emplace(objective.objectiveId, 0);
        }
    }

private:
    std::unordered_map<std::uint32_t, std::uint32_t> m_objectiveProgress;
};

} // namespace legend::world
