#pragma once

#include "Server/WorldServer/Quest/PlayerQuestState.h"
#include "Shared/Quest/QuestProtocol.h"
#include "Shared/Quest/QuestTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace legend::world {

class QuestRegistry;

// ---------------------------------------------------------------------------
// 阶段19 指令十四/十五：PlayerQuestContainer —— 单个玩家的任务容器。
// PlayerSession 持有（指令十四：任务状态不散落 WorldServer 多个 map）；
// 仅 World io 线程访问（无锁）。职责：Find/Add/Remove/Snapshot/Count/Completed/
// InProgress（指令十五）。
// ---------------------------------------------------------------------------
class PlayerQuestContainer {
public:
    void Clear() { m_quests.clear(); }

    const PlayerQuestState* Find(QuestId questId) const {
        const auto it = m_quests.find(questId);
        return it != m_quests.end() ? &it->second : nullptr;
    }
    PlayerQuestState* MutableFind(QuestId questId) {
        const auto it = m_quests.find(questId);
        return it != m_quests.end() ? &it->second : nullptr;
    }

    // 插入/覆盖（接取或加载时用）。
    PlayerQuestState& Add(QuestId questId) { return m_quests[questId]; }
    // 移除（阶段19 Abandon 保留记录——本接口仅供测试/清理使用）。
    bool Remove(QuestId questId) { return m_quests.erase(questId) != 0; }

    std::size_t Count() const { return m_quests.size(); }

    // 指令二十一：进行中（InProgress + ReadyToTurnIn）任务数；Completed 不算。
    std::size_t CountActive() const {
        std::size_t count = 0;
        for (const auto& [questId, state] : m_quests) {
            if (state.state == QuestState::InProgress ||
                state.state == QuestState::ReadyToTurnIn) {
                ++count;
            }
        }
        return count;
    }

    // 指令六十五：前置校验用——是否 Completed（ReadyToTurnIn/Abandoned 不算）。
    bool IsCompleted(QuestId questId) const {
        const auto* state = Find(questId);
        return state != nullptr && state->state == QuestState::Completed;
    }

    // 是否有 InProgress / ReadyToTurnIn 的同任务（指令二十）。
    bool IsActiveOrReady(QuestId questId) const {
        const auto* state = Find(questId);
        return state != nullptr && (state->state == QuestState::InProgress ||
                                    state->state == QuestState::ReadyToTurnIn);
    }

    const std::unordered_map<QuestId, PlayerQuestState>& All() const { return m_quests; }

    // 指令四十五：快照构建（InProgress/ReadyToTurnIn/Completed 全量下发；
    // Abandoned/NotAccepted 不下发——任务状态不是公开信息，只发本人）。
    // 每个 Quest 最多 16 Objective（指令四十六，Encode 侧再截断保护）。
    std::vector<QuestSnapshotEntryData> Snapshot() const;

private:
    std::unordered_map<QuestId, PlayerQuestState> m_quests;
};

} // namespace legend::world
