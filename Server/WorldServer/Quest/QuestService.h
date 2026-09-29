#pragma once

#include "Server/WorldServer/Quest/PlayerQuestContainer.h"
#include "Shared/Quest/QuestDefinition.h"
#include "Shared/Quest/QuestTypes.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace legend::world {

class QuestRegistry;

// ---------------------------------------------------------------------------
// 阶段19 指令二十八/二十九/七十一：QuestService —— 任务规则纯逻辑（无 IO/无网络）。
// WorldServer 负责编排（PlayerSession 查询、DbWorker 持久化、事件广播）；
// 本服务只做：接取初始化 / 事件推进（Kill/Collect/Level/Area）/ 完成判定 /
// TurnIn/Abandon 校验。每次推进返回实际变化（WorldServer 据此写 DB + 发事件）。
// ---------------------------------------------------------------------------
class QuestService {
public:
    // 目标进度变化（同一 tick 可能有多个：多目标任务/AOE 多杀）。
    struct ObjectiveChange {
        QuestId questId = 0;
        std::uint32_t objectiveId = 0;
        std::uint32_t oldProgress = 0;
        std::uint32_t newProgress = 0;
    };
    struct StateChange {
        QuestId questId = 0;
        QuestState oldState = QuestState::NotAccepted;
        QuestState newState = QuestState::NotAccepted;
    };

    // 当前持有数量回调：definitionId -> 背包内总数量（CollectItem 用，指令二十四）。
    using OwnedCountFn = std::function<std::uint32_t(std::uint32_t)>;

    // ------------------------------------------------------------------
    // 指令二十/二十三：接取初始化。调用方已完成 ValidateAccept；
    // state=InProgress + 全部进度 0 + ReachLevel/CollectItem 立即初始校验
    //（接 4003 已 Level3 -> 立即完成；接 Collect 任务背包已有 3 Core -> 立即 3/3）。
    // ReachArea 不做接取时校验（只在 MoveInput 后检查，指令二十七）。
    // ------------------------------------------------------------------
    static std::vector<ObjectiveChange> AcceptQuest(const QuestRegistry& registry,
                                                    PlayerQuestContainer& quests, QuestId questId,
                                                    std::uint32_t playerLevel,
                                                    const OwnedCountFn& ownedCountOf,
                                                    std::int64_t nowUnix);

    // 指令二十五：Kill 推进——只有最终 killer 才调用（WorldServer 保证）；
    // AOE 多杀每只分别调用一次（一只 Monster 只计一次）；进度封顶 required。
    static std::vector<ObjectiveChange> OnMonsterKilled(const QuestRegistry& registry,
                                                        PlayerQuestContainer& quests,
                                                        std::uint32_t monsterTypeId);

    // 指令二十四/五十四：Collect 重算（“当前拥有数量”型——拾取增加、未来使用/
    // 删除会降低）；进度 = min(当前持有, required)。
    static std::vector<ObjectiveChange> OnInventoryChanged(const QuestRegistry& registry,
                                                           PlayerQuestContainer& quests,
                                                           const OwnedCountFn& ownedCountOf);

    // 指令二十六/五十六/五十七：等级变化推进（LevelUp 与任务奖励升级共用）。
    static std::vector<ObjectiveChange> OnPlayerLevelChanged(const QuestRegistry& registry,
                                                             PlayerQuestContainer& quests,
                                                             std::uint32_t level);

    // 指令二十七：移动推进——只在服务器权威 MoveInput 位置真正变化后调用；
    // distanceSquared <= radius^2 且 mapId 相同 -> progress=1（指令五十八：
    // 已完成不重复变化）。
    static std::vector<ObjectiveChange> OnPlayerMoved(const QuestRegistry& registry,
                                                      PlayerQuestContainer& quests,
                                                      std::uint16_t mapId, float x, float y);

    // 指令二十九：完成判定——全部目标 progress >= required -> InProgress ->
    // ReadyToTurnIn（不自动领奖，指令七十八）。返回发生的状态变化。
    static std::vector<StateChange> EvaluateQuestCompletion(const QuestRegistry& registry,
                                                            PlayerQuestContainer& quests,
                                                            std::int64_t nowUnix);

    // 指令三十四：TurnIn 校验（奖励发放空间预检由 WorldServer 编排）。
    static QuestResultCode ValidateTurnIn(const PlayerQuestContainer& quests, QuestId questId,
                                          const QuestRegistry& registry);
    // 指令四十三：Abandon 校验——只允许 InProgress / ReadyToTurnIn；
    // Completed 不能 Abandon（指令四十三）。
    static QuestResultCode ValidateAbandon(const PlayerQuestContainer& quests, QuestId questId,
                                           const QuestRegistry& registry);
};

} // namespace legend::world
