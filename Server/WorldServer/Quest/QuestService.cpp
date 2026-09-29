#include "Server/WorldServer/Quest/QuestService.h"

#include "Server/WorldServer/Quest/QuestRegistry.h"

#include <cmath>

namespace legend::world {

namespace {

// 单个目标推进（封顶 required；返回是否实际变化）。
bool AdvanceObjective(const QuestObjectiveDefinition& objective, PlayerQuestState& state,
                      std::uint32_t newProgress, QuestService::ObjectiveChange& outChange) {
    const std::uint32_t clamped =
        newProgress > objective.requiredCount ? objective.requiredCount : newProgress;
    const std::uint32_t oldProgress = state.ProgressOf(objective.objectiveId);
    if (clamped == oldProgress) {
        return false; // 指令五十八/七十三：无变化不写 DB/不发事件
    }
    state.SetObjectiveProgress(objective.objectiveId, clamped);
    outChange.questId = state.questId;
    outChange.objectiveId = objective.objectiveId;
    outChange.oldProgress = oldProgress;
    outChange.newProgress = clamped;
    return true;
}

} // namespace

std::vector<QuestService::ObjectiveChange> QuestService::AcceptQuest(
    const QuestRegistry& registry, PlayerQuestContainer& quests, QuestId questId,
    std::uint32_t playerLevel, const OwnedCountFn& ownedCountOf, std::int64_t nowUnix) {
    std::vector<ObjectiveChange> changes;
    const QuestDefinition* definition = registry.FindQuest(questId);
    if (definition == nullptr) {
        return changes;
    }
    PlayerQuestState& state = quests.Add(questId);
    state.questId = questId;
    state.state = QuestState::InProgress;
    state.acceptedAt = nowUnix;
    state.completedAt = 0;
    state.turnedInAt = 0;
    state.ResetProgress(); // 重新接取进度清零（指令十七/四十三）
    state.InitObjectives(*definition);
    // 指令二十三：ReachLevel / CollectItem 立即初始校验。
    for (const auto& objective : definition->objectives) {
        ObjectiveChange change;
        bool changed = false;
        if (objective.type == QuestObjectiveType::ReachLevel) {
            // 指令二十六：currentLevel >= target -> progress = requiredCount。
            if (playerLevel >= objective.targetId) {
                changed = AdvanceObjective(objective, state, objective.requiredCount, change);
            }
        } else if (objective.type == QuestObjectiveType::CollectItem) {
            // 指令二十三/二十四：接取时背包已有 -> 立即 min(owned, required)。
            const std::uint32_t owned =
                ownedCountOf ? ownedCountOf(objective.targetId) : 0;
            changed = AdvanceObjective(objective, state, owned, change);
        }
        if (changed) {
            changes.push_back(change);
        }
    }
    return changes;
}

std::vector<QuestService::ObjectiveChange> QuestService::OnMonsterKilled(
    const QuestRegistry& registry, PlayerQuestContainer& quests, std::uint32_t monsterTypeId) {
    std::vector<ObjectiveChange> changes;
    for (const auto& definition : registry.AllQuests()) {
        if (definition.repeatable) {
            continue;
        }
        PlayerQuestState* state = quests.MutableFind(definition.questId);
        if (state == nullptr || state->state != QuestState::InProgress) {
            continue; // 指令二十五：只有进行中任务推进
        }
        for (const auto& objective : definition.objectives) {
            if (objective.type != QuestObjectiveType::KillMonster ||
                objective.targetId != monsterTypeId) {
                continue;
            }
            ObjectiveChange change;
            if (AdvanceObjective(objective, *state,
                                 state->ProgressOf(objective.objectiveId) + 1, change)) {
                changes.push_back(change);
            }
        }
    }
    return changes;
}

std::vector<QuestService::ObjectiveChange> QuestService::OnInventoryChanged(
    const QuestRegistry& registry, PlayerQuestContainer& quests, const OwnedCountFn& ownedCountOf) {
    std::vector<ObjectiveChange> changes;
    if (!ownedCountOf) {
        return changes;
    }
    for (const auto& definition : registry.AllQuests()) {
        PlayerQuestState* state = quests.MutableFind(definition.questId);
        if (state == nullptr || state->state != QuestState::InProgress) {
            continue;
        }
        for (const auto& objective : definition.objectives) {
            if (objective.type != QuestObjectiveType::CollectItem) {
                continue;
            }
            // 指令二十四/七十四：“当前拥有数量”型，进度封顶 required。
            const std::uint32_t owned = ownedCountOf(objective.targetId);
            ObjectiveChange change;
            if (AdvanceObjective(objective, *state, owned, change)) {
                changes.push_back(change);
            }
        }
    }
    return changes;
}

std::vector<QuestService::ObjectiveChange> QuestService::OnPlayerLevelChanged(
    const QuestRegistry& registry, PlayerQuestContainer& quests, std::uint32_t level) {
    std::vector<ObjectiveChange> changes;
    for (const auto& definition : registry.AllQuests()) {
        PlayerQuestState* state = quests.MutableFind(definition.questId);
        if (state == nullptr || state->state != QuestState::InProgress) {
            continue;
        }
        for (const auto& objective : definition.objectives) {
            if (objective.type != QuestObjectiveType::ReachLevel) {
                continue;
            }
            // 指令二十六：currentLevel >= requiredLevel -> progress = 1。
            if (level < objective.targetId) {
                continue;
            }
            ObjectiveChange change;
            if (AdvanceObjective(objective, *state, objective.requiredCount, change)) {
                changes.push_back(change);
            }
        }
    }
    return changes;
}

std::vector<QuestService::ObjectiveChange> QuestService::OnPlayerMoved(
    const QuestRegistry& registry, PlayerQuestContainer& quests, std::uint16_t mapId, float x,
    float y) {
    std::vector<ObjectiveChange> changes;
    for (const auto& definition : registry.AllQuests()) {
        PlayerQuestState* state = quests.MutableFind(definition.questId);
        if (state == nullptr || state->state != QuestState::InProgress) {
            continue;
        }
        for (const auto& objective : definition.objectives) {
            if (objective.type != QuestObjectiveType::ReachArea) {
                continue;
            }
            // 指令二十七：mapId 相同 && distanceSquared <= radius^2。
            if (objective.mapId != mapId) {
                continue;
            }
            const float dx = x - objective.areaX;
            const float dy = y - objective.areaY;
            if (dx * dx + dy * dy > objective.areaRadius * objective.areaRadius) {
                continue;
            }
            ObjectiveChange change;
            if (AdvanceObjective(objective, *state, objective.requiredCount, change)) {
                changes.push_back(change);
            }
        }
    }
    return changes;
}

std::vector<QuestService::StateChange> QuestService::EvaluateQuestCompletion(
    const QuestRegistry& registry, PlayerQuestContainer& quests, std::int64_t nowUnix) {
    std::vector<StateChange> changes;
    for (const auto& definition : registry.AllQuests()) {
        PlayerQuestState* state = quests.MutableFind(definition.questId);
        if (state == nullptr || state->state != QuestState::InProgress) {
            continue;
        }
        // 指令二十九：所有 Objectives progress >= requiredCount -> ReadyToTurnIn。
        bool allComplete = true;
        for (const auto& objective : definition.objectives) {
            if (state->ProgressOf(objective.objectiveId) < objective.requiredCount) {
                allComplete = false;
                break;
            }
        }
        if (!allComplete) {
            continue;
        }
        StateChange change;
        change.questId = definition.questId;
        change.oldState = state->state;
        change.newState = QuestState::ReadyToTurnIn; // 不等于 Completed（指令二十九）
        state->state = QuestState::ReadyToTurnIn;
        state->completedAt = nowUnix;
        changes.push_back(change);
    }
    return changes;
}

QuestResultCode QuestService::ValidateTurnIn(const PlayerQuestContainer& quests, QuestId questId,
                                             const QuestRegistry& registry) {
    if (registry.FindQuest(questId) == nullptr) {
        return QuestResultCode::UnknownQuest; // 指令三十四
    }
    const PlayerQuestState* state = quests.Find(questId);
    if (state == nullptr || state->state == QuestState::NotAccepted ||
        state->state == QuestState::Abandoned) {
        return QuestResultCode::NotAccepted; // 指令二十二
    }
    if (state->state == QuestState::Completed) {
        return QuestResultCode::AlreadyCompleted; // 指令七十九
    }
    if (state->state != QuestState::ReadyToTurnIn) {
        return QuestResultCode::NotReady; // 指令二十二
    }
    return QuestResultCode::Success;
}

QuestResultCode QuestService::ValidateAbandon(const PlayerQuestContainer& quests, QuestId questId,
                                              const QuestRegistry& registry) {
    if (registry.FindQuest(questId) == nullptr) {
        return QuestResultCode::UnknownQuest;
    }
    const PlayerQuestState* state = quests.Find(questId);
    if (state == nullptr || state->state == QuestState::NotAccepted ||
        state->state == QuestState::Abandoned) {
        return QuestResultCode::NotAccepted;
    }
    if (state->state == QuestState::Completed) {
        return QuestResultCode::AlreadyCompleted; // 指令四十三：Completed 不能 Abandon
    }
    // InProgress / ReadyToTurnIn 允许（指令四十三）。
    return QuestResultCode::Success;
}

} // namespace legend::world
