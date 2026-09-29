#include "Server/WorldServer/Quest/QuestRepository.h"

#include "Engine/Debug/Logger.h"
#include "Server/WorldServer/Item/InventoryRepository.h"
#include "Server/WorldServer/Progression/ProgressionService.h"
#include "Shared/Item/ItemTypes.h"

namespace legend::world {

using legend::account::Statement;

bool QuestRepository::LoadCharacterQuests(QuestDatabase& db, std::uint64_t characterId,
                                          std::vector<QuestRow>& outQuests,
                                          std::vector<ObjectiveRow>& outObjectives,
                                          std::string& error) {
    outQuests.clear();
    outObjectives.clear();
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "SELECT quest_id, state, accepted_at, completed_at, turned_in_at "
                          "FROM character_quests WHERE character_id = ?;",
                          error)) {
            return false;
        }
        stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
        while (stmt.Step(error)) {
            QuestRow row;
            row.questId = static_cast<QuestId>(stmt.ColumnInt64(0));
            row.state = static_cast<std::uint8_t>(stmt.ColumnInt64(1));
            row.acceptedAt = stmt.ColumnInt64(2);
            row.completedAt = stmt.ColumnInt64(3);
            row.turnedInAt = stmt.ColumnInt64(4);
            outQuests.push_back(row);
        }
        if (!error.empty()) {
            return false;
        }
    }
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "SELECT quest_id, objective_id, progress "
                          "FROM character_quest_objectives WHERE character_id = ?;",
                          error)) {
            return false;
        }
        stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
        while (stmt.Step(error)) {
            ObjectiveRow row;
            row.questId = static_cast<QuestId>(stmt.ColumnInt64(0));
            row.objectiveId = static_cast<std::uint32_t>(stmt.ColumnInt64(1));
            row.progress = static_cast<std::uint32_t>(stmt.ColumnInt64(2));
            outObjectives.push_back(row);
        }
        return error.empty();
    }
}

bool QuestRepository::InsertQuest(QuestDatabase& db, std::uint64_t characterId, QuestId questId,
                                  std::int8_t state, std::int64_t acceptedAt,
                                  const std::vector<std::uint32_t>& objectiveIds,
                                  std::string& error) {
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    // UPSERT：新接取 INSERT；重新接取（Abandoned 记录存在）UPDATE（指令十七）。
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "INSERT INTO character_quests "
                          "(character_id, quest_id, state, accepted_at, completed_at, turned_in_at) "
                          "VALUES (?, ?, ?, ?, 0, 0) "
                          "ON CONFLICT(character_id, quest_id) DO UPDATE SET "
                          "state = excluded.state, accepted_at = excluded.accepted_at, "
                          "completed_at = 0, turned_in_at = 0;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
        stmt.BindInt64(2, static_cast<std::int64_t>(questId));
        stmt.BindInt64(3, state);
        stmt.BindInt64(4, acceptedAt);
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    // 指令十七/四十三：重新接取进度清零——DELETE + INSERT 0。
    for (const std::uint32_t objectiveId : objectiveIds) {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "INSERT INTO character_quest_objectives "
                          "(character_id, quest_id, objective_id, progress) "
                          "VALUES (?, ?, ?, 0) "
                          "ON CONFLICT(character_id, quest_id, objective_id) DO UPDATE SET "
                          "progress = 0;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
        stmt.BindInt64(2, static_cast<std::int64_t>(questId));
        stmt.BindInt64(3, static_cast<std::int64_t>(objectiveId));
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return false;
    }
    return true;
}

bool QuestRepository::UpdateQuestState(QuestDatabase& db, std::uint64_t characterId,
                                       QuestId questId, std::int8_t state, std::int64_t timestamp,
                                       bool setTurnedInAt, std::string& error) {
    Statement stmt;
    const char* sql = setTurnedInAt
                          ? "UPDATE character_quests SET state = ?, turned_in_at = ? "
                            "WHERE character_id = ? AND quest_id = ?;"
                          : "UPDATE character_quests SET state = ? "
                            "WHERE character_id = ? AND quest_id = ?;";
    if (!stmt.Prepare(db.Handle(), sql, error)) {
        return false;
    }
    stmt.BindInt64(1, state);
    if (setTurnedInAt) {
        stmt.BindInt64(2, timestamp);
        stmt.BindInt64(3, static_cast<std::int64_t>(characterId));
        stmt.BindInt64(4, static_cast<std::int64_t>(questId));
    } else {
        stmt.BindInt64(2, static_cast<std::int64_t>(characterId));
        stmt.BindInt64(3, static_cast<std::int64_t>(questId));
    }
    stmt.Step(error);
    return error.empty();
}

bool QuestRepository::MarkCompleted(QuestDatabase& db, std::uint64_t characterId, QuestId questId,
                                    std::int64_t turnedInAt, std::string& error) {
    // 指令四十二/六十七：Completed + turnedInAt（completed_at 在 ReadyToTurnIn 时已写）。
    return UpdateQuestState(db, characterId, questId, static_cast<std::int8_t>(QuestState::Completed),
                            turnedInAt, true, error);
}

bool QuestRepository::UpdateObjectiveProgress(QuestDatabase& db, std::uint64_t characterId,
                                              QuestId questId, std::uint32_t objectiveId,
                                              std::uint32_t progress, std::string& error) {
    Statement stmt;
    if (!stmt.Prepare(db.Handle(),
                      "UPDATE character_quest_objectives SET progress = ? "
                      "WHERE character_id = ? AND quest_id = ? AND objective_id = ?;",
                      error)) {
        return false;
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(progress));
    stmt.BindInt64(2, static_cast<std::int64_t>(characterId));
    stmt.BindInt64(3, static_cast<std::int64_t>(questId));
    stmt.BindInt64(4, static_cast<std::int64_t>(objectiveId));
    stmt.Step(error);
    return error.empty();
}

bool QuestRepository::MarkAbandoned(QuestDatabase& db, std::uint64_t characterId, QuestId questId,
                                    std::int64_t nowUnix, std::string& error) {
    // 指令四十三/十七：保留记录 state=Abandoned；进度清零（重新接取时重置）。
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE character_quests SET state = ? "
                          "WHERE character_id = ? AND quest_id = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, static_cast<std::int8_t>(QuestState::Abandoned));
        stmt.BindInt64(2, static_cast<std::int64_t>(characterId));
        stmt.BindInt64(3, static_cast<std::int64_t>(questId));
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE character_quest_objectives SET progress = 0 "
                          "WHERE character_id = ? AND quest_id = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
        stmt.BindInt64(2, static_cast<std::int64_t>(questId));
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return false;
    }
    return true;
}

bool QuestRepository::OfflineAdvanceKill(QuestDatabase& db, std::uint64_t characterId,
                                         const std::vector<KillCandidate>& candidates,
                                         std::string& error) {
    // 指令六十二：离线 Kill Quest 推进（不加载假 PlayerSession）；
    // 指令六十三：离线完成 -> DB 直接 ReadyToTurnIn；指令六十四：不发网络事件。
    if (candidates.empty()) {
        return false;
    }
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    bool anyProgress = false;
    for (const auto& candidate : candidates) {
        // 仅 InProgress 任务推进（state=1）。
        std::int64_t questState = -1;
        {
            Statement stmt;
            if (!stmt.Prepare(db.Handle(),
                              "SELECT state FROM character_quests "
                              "WHERE character_id = ? AND quest_id = ?;",
                              error)) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
            stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
            stmt.BindInt64(2, static_cast<std::int64_t>(candidate.questId));
            if (stmt.Step(error)) {
                questState = stmt.ColumnInt64(0);
            }
            if (!error.empty()) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
            stmt.Reset();
        }
        if (questState != static_cast<std::int64_t>(QuestState::InProgress)) {
            continue;
        }
        // progress 封顶 required（指令七十三：满进度不再写）。
        {
            Statement stmt;
            if (!stmt.Prepare(db.Handle(),
                              "UPDATE character_quest_objectives "
                              "SET progress = MIN(progress + 1, ?) "
                              "WHERE character_id = ? AND quest_id = ? AND objective_id = ? "
                              "AND progress < ?;",
                              error)) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
            stmt.BindInt64(1, static_cast<std::int64_t>(candidate.requiredCount));
            stmt.BindInt64(2, static_cast<std::int64_t>(characterId));
            stmt.BindInt64(3, static_cast<std::int64_t>(candidate.questId));
            stmt.BindInt64(4, static_cast<std::int64_t>(candidate.objectiveId));
            stmt.BindInt64(5, static_cast<std::int64_t>(candidate.requiredCount));
            stmt.Step(error);
            if (!error.empty()) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
            anyProgress = sqlite3_changes(db.Handle()) > 0 || anyProgress;
        }
        // 完成判定：该任务全部目标 progress >= required -> ReadyToTurnIn。
        bool allComplete = true;
        {
            Statement stmt;
            if (!stmt.Prepare(db.Handle(),
                              "SELECT objective_id, progress FROM character_quest_objectives "
                              "WHERE character_id = ? AND quest_id = ?;",
                              error)) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
            stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
            stmt.BindInt64(2, static_cast<std::int64_t>(candidate.questId));
            while (stmt.Step(error)) {
                const std::uint32_t progress =
                    static_cast<std::uint32_t>(stmt.ColumnInt64(1));
                // required 从 candidates 同任务目标中取（objective 定义在代码）。
                std::uint32_t required = 0;
                bool found = false;
                for (const auto& c : candidates) {
                    if (c.questId == candidate.questId && c.objectiveId ==
                                                           static_cast<std::uint32_t>(stmt.ColumnInt64(0))) {
                        required = c.requiredCount;
                        found = true;
                        break;
                    }
                }
                if (!found || progress < required) {
                    allComplete = false;
                    break;
                }
            }
            if (!error.empty()) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
        }
        if (allComplete) {
            Statement stmt;
            if (!stmt.Prepare(db.Handle(),
                              "UPDATE character_quests SET state = ?, completed_at = ? "
                              "WHERE character_id = ? AND quest_id = ? AND state = ?;",
                              error)) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
            stmt.BindInt64(1, static_cast<std::int8_t>(QuestState::ReadyToTurnIn));
            stmt.BindInt64(2, legend::account::UnixNow());
            stmt.BindInt64(3, static_cast<std::int64_t>(characterId));
            stmt.BindInt64(4, static_cast<std::int64_t>(candidate.questId));
            stmt.BindInt64(5, static_cast<std::int8_t>(QuestState::InProgress));
            stmt.Step(error);
            if (!error.empty()) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
        }
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return false;
    }
    return anyProgress;
}

QuestRepository::TurnInResult QuestRepository::RunTurnInTransaction(QuestDatabase& db,
                                                                    const TurnInTransaction& tx,
                                                                    std::string& error) {
    TurnInResult result;
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return result;
    }
    // 1) Quest Completed（指令四十二：ReadyToTurnIn -> Completed + turned_in_at；
    // 指令六十七：Completed 记录永久保留）。
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE character_quests SET state = ?, completed_at = ?, "
                          "turned_in_at = ? WHERE character_id = ? AND quest_id = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return result;
        }
        stmt.BindInt64(1, static_cast<std::int8_t>(QuestState::Completed));
        stmt.BindInt64(2, tx.turnedInAt);
        stmt.BindInt64(3, tx.turnedInAt);
        stmt.BindInt64(4, static_cast<std::int64_t>(tx.characterId));
        stmt.BindInt64(5, static_cast<std::int64_t>(tx.questId));
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return result;
        }
    }
    // 2) 成长写回（EXP/Gold 奖励绝对值由 io 线程预计算；指令三十六/三十七）。
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE characters SET level = ?, exp = ?, gold = ? WHERE id = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return result;
        }
        stmt.BindInt64(1, static_cast<std::int64_t>(tx.newLevel));
        stmt.BindInt64(2, tx.newExperience);
        stmt.BindInt64(3, tx.newGold);
        stmt.BindInt64(4, static_cast<std::int64_t>(tx.characterId));
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return result;
        }
    }
    // 3) 物品奖励（指令三十八/八十：instanceId 由 DB 正常生成；槽位 io 线程试算）。
    if (tx.rewardItemDefinitionId != 0 && tx.rewardItemQuantity > 0) {
        const std::uint64_t instanceId = InventoryRepository::InsertItem(
            db, tx.characterId, tx.rewardItemDefinitionId, tx.rewardItemQuantity,
            tx.rewardItemSlotIndex, tx.rewardItemCreatedAt);
        if (instanceId == 0) {
            db.Execute("ROLLBACK;", error);
            error = error.empty() ? "insert reward item failed" : error;
            return result;
        }
        result.itemInstanceId = instanceId;
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace legend::world
