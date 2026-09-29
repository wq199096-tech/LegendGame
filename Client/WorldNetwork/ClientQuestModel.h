#pragma once

#include "Client/WorldNetwork/ClientQuestState.h"
#include "Shared/Quest/QuestDefinition.h"
#include "Shared/Quest/QuestProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段19 指令四十八：ClientQuestModel —— 客户端任务镜像模型。
// 全部变更来自服务器事件（指令四十九：Snapshot/ProgressUpdated/StateChanged），
// 状态/进度仅展示；任务名称可本地使用 Shared QuestDefinition（指令五十二），
// 状态/进度仍来自 Server。
// ---------------------------------------------------------------------------
class ClientQuestModel {
public:
    void Clear() { m_quests.clear(); }

    // 指令四十五：Snapshot 全量覆盖（服务器为真相）。
    void ApplySnapshot(const std::vector<world::QuestSnapshotEntryData>& quests);
    // 指令三十：目标进度更新（未知 questId 忽略——等 Snapshot 建立）。
    void ApplyProgress(QuestId questId, std::uint32_t objectiveId, std::uint32_t current,
                       std::uint32_t required);
    // 指令三十一：任务状态变化（未知 questId 忽略；Abandoned 保留镜像供 Debug）。
    void ApplyStateChange(QuestId questId, QuestState newState);

    const ClientQuestState* Find(QuestId questId) const;
    const std::unordered_map<QuestId, ClientQuestState>& All() const { return m_quests; }
    std::size_t Count() const { return m_quests.size(); }

    // 指令五十一：F8 Quest Debug 文本（Quest 4001 Slime Hunter / State /
    // Objective: Kill Slime 2/5 ...）。
    std::string DebugText() const;

private:
    std::unordered_map<QuestId, ClientQuestState> m_quests;
};

} // namespace legend::client
