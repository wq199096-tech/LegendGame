#include "Client/WorldNetwork/ClientQuestModel.h"

#include "Engine/Debug/Logger.h"

#include <sstream>

namespace legend::client {

void ClientQuestModel::ApplySnapshot(const std::vector<world::QuestSnapshotEntryData>& quests) {
    // 指令四十九：Snapshot 以服务器列表为准（客户端本地模拟的错误 progress 被纠正）。
    m_quests.clear();
    for (const auto& entry : quests) {
        ClientQuestState state;
        state.questId = entry.questId;
        state.state = static_cast<QuestState>(entry.state);
        for (const auto& objective : entry.objectives) {
            ClientQuestState::ObjectiveProgress progress;
            progress.current = objective.current;
            progress.required = objective.required;
            state.objectives[objective.objectiveId] = progress;
        }
        m_quests[state.questId] = std::move(state);
    }
}

void ClientQuestModel::ApplyProgress(QuestId questId, std::uint32_t objectiveId,
                                     std::uint32_t current, std::uint32_t required) {
    // 指令四十九：本地不推算，只应用服务器事件；未知任务忽略（等 Snapshot）。
    const auto it = m_quests.find(questId);
    if (it == m_quests.end()) {
        LOG_DEBUG("[Quest] progress for unknown quest " + std::to_string(questId) + " ignored");
        return;
    }
    auto& objective = it->second.objectives[objectiveId];
    objective.current = current;
    objective.required = required;
}

void ClientQuestModel::ApplyStateChange(QuestId questId, QuestState newState) {
    const auto it = m_quests.find(questId);
    if (it == m_quests.end()) {
        LOG_DEBUG("[Quest] state change for unknown quest " + std::to_string(questId) +
                  " ignored");
        return;
    }
    it->second.state = newState;
}

const ClientQuestState* ClientQuestModel::Find(QuestId questId) const {
    const auto it = m_quests.find(questId);
    return it != m_quests.end() ? &it->second : nullptr;
}

std::string ClientQuestModel::DebugText() const {
    // 指令五十一：F8 显示 "Quest 4001 Slime Hunter / State / Objective: Kill Slime 2/5"。
    // 名称来自 Shared 展示元数据（指令五十二）；状态/进度全部来自 Server。
    std::ostringstream oss;
    oss << "\n[Quest Debug]\n";
    const world::QuestId fixedQuests[] = {4001, 4002, 4003, 4004, 4005};
    for (const world::QuestId questId : fixedQuests) {
        const ClientQuestState* state = Find(questId);
        oss << "Quest " << questId << " " << world::SharedQuestDisplayName(questId) << " | State: "
            << (state != nullptr ? world::QuestStateName(static_cast<std::uint8_t>(state->state))
                                 : "NotAccepted")
            << "\n";
        if (state == nullptr) {
            continue;
        }
        for (const auto& [objectiveId, progress] : state->objectives) {
            oss << "  Objective " << world::SharedObjectiveDisplayLabel(objectiveId) << ": "
                << progress.current << "/" << progress.required << "\n";
        }
    }
    return oss.str();
}

} // namespace legend::client
