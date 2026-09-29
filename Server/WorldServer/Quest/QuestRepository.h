#pragma once

#include "Server/WorldServer/Quest/PlayerQuestState.h"
#include "Server/LoginServer/Account/Database/Database.h"
#include "Shared/Quest/QuestTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

using QuestDatabase = legend::account::Database;

// ---------------------------------------------------------------------------
// 阶段19 指令十七/十八/七十：QuestRepository —— 任务持久化（全部 prepared
// statements）。只能在 DB Worker 线程调用（指令十八：禁止 World io 线程同步 SQLite）。
// 职责分离（指令七十一/七十二）：QuestService 规则 / QuestRepository 数据库 /
// WorldServer 编排——所有事件不得直接写 SQL。
// ---------------------------------------------------------------------------
class QuestRepository {
public:
    // 加载行（quest + objective 两查询合并；LoadCharacterQuests 输出）。
    struct QuestRow {
        QuestId questId = 0;
        std::uint8_t state = 0;
        std::int64_t acceptedAt = 0;
        std::int64_t completedAt = 0;
        std::int64_t turnedInAt = 0;
    };
    struct ObjectiveRow {
        QuestId questId = 0;
        std::uint32_t objectiveId = 0;
        std::uint32_t progress = 0;
    };

    // 指令六十八：EnterWorld 时从 SQLite 加载（不能只靠进程内存）。
    static bool LoadCharacterQuests(QuestDatabase& db, std::uint64_t characterId,
                                    std::vector<QuestRow>& outQuests,
                                    std::vector<ObjectiveRow>& outObjectives, std::string& error);

    // 指令十七：接取写 DB（新接取/重新接取统一 UPSERT：state=InProgress +
    // 时间戳重置 + 目标进度清零重建）。事务化。
    static bool InsertQuest(QuestDatabase& db, std::uint64_t characterId, QuestId questId,
                            std::int8_t state, std::int64_t acceptedAt,
                            const std::vector<std::uint32_t>& objectiveIds, std::string& error);

    // 指令十七：状态变化写 DB（ReadyToTurnIn / Completed 单独写；
    // MarkCompleted 附带 turned_in_at）。事务内单 UPDATE。
    static bool UpdateQuestState(QuestDatabase& db, std::uint64_t characterId, QuestId questId,
                                 std::int8_t state, std::int64_t timestamp, bool setTurnedInAt,
                                 std::string& error);
    // 指令六十七：MarkCompleted —— state=Completed + completed_at/turned_in_at。
    static bool MarkCompleted(QuestDatabase& db, std::uint64_t characterId, QuestId questId,
                              std::int64_t turnedInAt, std::string& error);

    // 指令七十三：目标进度变化才写（调用方保证有实际变化）。
    static bool UpdateObjectiveProgress(QuestDatabase& db, std::uint64_t characterId,
                                        QuestId questId, std::uint32_t objectiveId,
                                        std::uint32_t progress, std::string& error);

    // 指令四十三/十七：Abandon —— 保留记录 state=Abandoned + 进度清零。事务化。
    static bool MarkAbandoned(QuestDatabase& db, std::uint64_t characterId, QuestId questId,
                              std::int64_t nowUnix, std::string& error);

    // 指令六十二：离线 Kill 推进（不加载假 PlayerSession；DB 直接推进）。
    // candidates = (questId, objectiveId, required) —— 由 QuestRegistry 过滤出
    // Kill objective targetId == monsterTypeId 的全部目标；事务内：
    // 仅 InProgress 任务推进（progress 封顶 required），随后完成判定
    //（全部目标满足 -> state=ReadyToTurnIn，指令六十三）。
    struct KillCandidate {
        QuestId questId = 0;
        std::uint32_t objectiveId = 0;
        std::uint32_t requiredCount = 0;
    };
    // 返回是否有任何变化（供日志）。
    static bool OfflineAdvanceKill(QuestDatabase& db, std::uint64_t characterId,
                                   const std::vector<KillCandidate>& candidates,
                                   std::string& error);

    // 指令四十：TurnIn 原子事务 —— Quest Completed + 成长写回 + 物品奖励一次提交。
    // 奖励数值由 WorldServer io 线程基于内存权威状态预计算（与 GrantMonsterReward
    // 同模式；DbWorker FIFO 保证与其它成长写入串行）；物品落点槽位由 io 线程
    // 试算（指令三十九：无空间先拒绝，不先发 EXP/Gold 再因 Item 失败）；
    // instanceId 由 inventory_items 正常生成（指令八十：Client 不能指定）。
    // 失败返回 ok=false（整体回滚，任务仍 ReadyToTurnIn）。
    struct TurnInTransaction {
        std::uint64_t characterId = 0;
        QuestId questId = 0;
        std::int64_t turnedInAt = 0;
        // 结算后的成长绝对值（io 线程预计算）。
        std::uint32_t newLevel = 1;
        std::int64_t newExperience = 0;
        std::int64_t newGold = 0;
        // 物品奖励（rewardItemDefinitionId=0 表示无）。
        std::uint32_t rewardItemDefinitionId = 0;
        std::uint32_t rewardItemQuantity = 0;
        std::int64_t rewardItemSlotIndex = -1;
        std::int64_t rewardItemCreatedAt = 0;
    };
    struct TurnInResult {
        bool ok = false;
        std::uint64_t itemInstanceId = 0; // 无物品奖励为 0
    };
    static TurnInResult RunTurnInTransaction(QuestDatabase& db, const TurnInTransaction& tx,
                                             std::string& error);
};

} // namespace legend::world
