#include "Server/WorldServer/Quest/PlayerQuestContainer.h"

#include "Server/WorldServer/Quest/QuestRegistry.h"

namespace legend::world {

std::vector<QuestSnapshotEntryData> PlayerQuestContainer::Snapshot() const {
    std::vector<QuestSnapshotEntryData> entries;
    for (const auto& [questId, state] : m_quests) {
        // 指令四十五：只下发 InProgress / ReadyToTurnIn / Completed。
        if (state.state != QuestState::InProgress &&
            state.state != QuestState::ReadyToTurnIn &&
            state.state != QuestState::Completed) {
            continue;
        }
        QuestSnapshotEntryData entry;
        entry.questId = questId;
        entry.state = static_cast<std::uint8_t>(state.state);
        const QuestDefinition* definition = QuestRegistry::Instance().FindQuest(questId);
        if (definition != nullptr) {
            entry.objectives.reserve(definition->objectives.size());
            for (const auto& objective : definition->objectives) {
                QuestSnapshotObjectiveData objectiveData;
                objectiveData.objectiveId = objective.objectiveId;
                objectiveData.current = state.ProgressOf(objective.objectiveId);
                objectiveData.required = objective.requiredCount;
                entry.objectives.push_back(objectiveData);
            }
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace legend::world
