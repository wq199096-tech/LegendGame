#include "Server/LoginServer/Account/AccountRepository.h"

#include "Engine/Debug/Logger.h"

namespace legend::account {
namespace {

RepositoryResult<std::uint64_t> MakeError(AccountErrorCode code, std::string message) {
    RepositoryResult<std::uint64_t> result;
    result.errorCode = code;
    result.errorMessage = std::move(message);
    return result;
}

// SQLite 约束错误 -> 业务错误码（其余一律 DatabaseError，细节只进日志）。
template <typename T>
RepositoryResult<T> MapSqlError(const char* context, const std::string& detail) {
    RepositoryResult<T> result;
    result.errorCode = AccountErrorCode::DatabaseError;
    result.errorMessage = std::string(context) + ": " + detail;
    return result;
}

bool IsUniqueConstraintError(const std::string& message) {
    // SQLITE_CONSTRAINT_UNIQUE 的 errmsg 固定含 "UNIQUE constraint failed"。
    return message.find("UNIQUE constraint failed") != std::string::npos;
}

} // namespace

namespace AccountRepository {

RepositoryResult<std::uint64_t> CreateAccount(Database& db, const std::string& username,
                                              const std::string& passwordHash) {
    const std::int64_t now = UnixNow();
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "INSERT INTO accounts (username, password_hash, created_at) "
                      "VALUES (?, ?, ?);",
                      error)) {
        return MapSqlError<std::uint64_t>("CreateAccount prepare", error);
    }
    stmt.BindText(1, username);
    stmt.BindText(2, passwordHash);
    stmt.BindInt64(3, now);
    stmt.Step(error);
    if (!error.empty()) {
        if (IsUniqueConstraintError(error)) {
            return MakeError(AccountErrorCode::UsernameAlreadyExists, error);
        }
        return MapSqlError<std::uint64_t>("CreateAccount insert", error);
    }
    return RepositoryResult<std::uint64_t>{true, static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db.Handle()))};
}

RepositoryResult<std::optional<AccountRow>> FindAccountByUsername(Database& db,
                                                                  const std::string& username) {
    RepositoryResult<std::optional<AccountRow>> result;
    Statement stmt;
    std::string error;
    // username 列 COLLATE NOCASE + UNIQUE：等值查询走索引（大小写不敏感，指令十二）。
    if (!stmt.Prepare(db.Handle(),
                      "SELECT id, username, password_hash, created_at, last_login_at, status, "
                      "failed_login_count, locked_until FROM accounts WHERE username = ?;",
                      error)) {
        return MapSqlError<std::optional<AccountRow>>("FindAccountByUsername prepare", error);
    }
    stmt.BindText(1, username);
    if (stmt.Step(error)) {
        AccountRow row;
        row.id = static_cast<std::uint64_t>(stmt.ColumnInt64(0));
        row.username = stmt.ColumnText(1);
        row.passwordHash = stmt.ColumnText(2);
        row.createdAt = stmt.ColumnInt64(3);
        row.lastLoginAt = stmt.ColumnInt64(4);
        row.status = static_cast<std::uint16_t>(stmt.ColumnInt64(5));
        row.failedLoginCount = stmt.ColumnInt64(6);
        row.lockedUntil = stmt.ColumnInt64(7);
        result.success = true;
        result.value = std::move(row);
        return result;
    }
    if (!error.empty()) {
        return MapSqlError<std::optional<AccountRow>>("FindAccountByUsername step", error);
    }
    result.success = true; // 未找到：success + 空 value
    return result;
}

RepositoryResult<int> UpdateLoginSuccess(Database& db, std::uint64_t accountId) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "UPDATE accounts SET failed_login_count = 0, locked_until = NULL, "
                      "last_login_at = ? WHERE id = ?;",
                      error)) {
        return MapSqlError<int>("UpdateLoginSuccess prepare", error);
    }
    stmt.BindInt64(1, UnixNow());
    stmt.BindInt64(2, static_cast<std::int64_t>(accountId));
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<int>("UpdateLoginSuccess step", error);
    }
    return RepositoryResult<int>{true, 0};
}

RepositoryResult<std::int64_t> UpdateLoginFailure(Database& db, std::uint64_t accountId,
                                                  int maxFailedLogins,
                                                  std::int64_t lockoutSeconds) {
    Statement stmt;
    std::string error;
    // 阶段10 指令二十九：连续失败 >= 阈值 -> 固定时长锁定（不做复杂风控）。
    if (!stmt.Prepare(db.Handle(),
                      "UPDATE accounts SET failed_login_count = failed_login_count + 1, "
                      "locked_until = CASE WHEN failed_login_count + 1 >= ? "
                      "THEN ? ELSE locked_until END WHERE id = ?;",
                      error)) {
        return MapSqlError<std::int64_t>("UpdateLoginFailure prepare", error);
    }
    stmt.BindInt64(1, maxFailedLogins);
    stmt.BindInt64(2, UnixNow() + lockoutSeconds);
    stmt.BindInt64(3, static_cast<std::int64_t>(accountId));
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<std::int64_t>("UpdateLoginFailure step", error);
    }

    Statement read;
    if (!read.Prepare(db.Handle(), "SELECT failed_login_count FROM accounts WHERE id = ?;",
                      error)) {
        return MapSqlError<std::int64_t>("UpdateLoginFailure read prepare", error);
    }
    read.BindInt64(1, static_cast<std::int64_t>(accountId));
    if (read.Step(error)) {
        return RepositoryResult<std::int64_t>{true, read.ColumnInt64(0)};
    }
    if (!error.empty()) {
        return MapSqlError<std::int64_t>("UpdateLoginFailure read step", error);
    }
    return RepositoryResult<std::int64_t>{true, 0};
}

} // namespace AccountRepository

namespace SessionRepository {

RepositoryResult<std::uint64_t> CreateSession(Database& db, std::uint64_t accountId,
                                              const std::string& tokenHash,
                                              std::int64_t expiresAt) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "INSERT INTO sessions (account_id, session_token_hash, created_at, "
                      "expires_at) VALUES (?, ?, ?, ?);",
                      error)) {
        return MapSqlError<std::uint64_t>("CreateSession prepare", error);
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(accountId));
    stmt.BindText(2, tokenHash);
    stmt.BindInt64(3, UnixNow());
    stmt.BindInt64(4, expiresAt);
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<std::uint64_t>("CreateSession insert", error);
    }
    return RepositoryResult<std::uint64_t>{
        true, static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db.Handle()))};
}

RepositoryResult<std::optional<SessionRow>> FindSessionByTokenHash(Database& db,
                                                                   const std::string& tokenHash) {
    RepositoryResult<std::optional<SessionRow>> result;
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "SELECT id, account_id, session_token_hash, created_at, expires_at, revoked "
                      "FROM sessions WHERE session_token_hash = ?;",
                      error)) {
        return MapSqlError<std::optional<SessionRow>>("FindSessionByTokenHash prepare", error);
    }
    stmt.BindText(1, tokenHash);
    if (stmt.Step(error)) {
        SessionRow row;
        row.id = static_cast<std::uint64_t>(stmt.ColumnInt64(0));
        row.accountId = static_cast<std::uint64_t>(stmt.ColumnInt64(1));
        row.tokenHash = stmt.ColumnText(2);
        row.createdAt = stmt.ColumnInt64(3);
        row.expiresAt = stmt.ColumnInt64(4);
        row.revoked = stmt.ColumnInt64(5) != 0;
        result.success = true;
        result.value = std::move(row);
        return result;
    }
    if (!error.empty()) {
        return MapSqlError<std::optional<SessionRow>>("FindSessionByTokenHash step", error);
    }
    result.success = true;
    return result;
}

RepositoryResult<int> RevokeSession(Database& db, std::uint64_t sessionId) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(), "UPDATE sessions SET revoked = 1 WHERE id = ?;", error)) {
        return MapSqlError<int>("RevokeSession prepare", error);
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(sessionId));
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<int>("RevokeSession step", error);
    }
    return RepositoryResult<int>{true, 1};
}

} // namespace SessionRepository

namespace CharacterRepository {

RepositoryResult<CharacterRow> CreateCharacter(Database& db, std::uint64_t accountId,
                                               const std::string& name, std::uint16_t classId,
                                               std::uint16_t gender, std::uint16_t visualId,
                                               std::size_t maxCharactersPerAccount) {
    RepositoryResult<CharacterRow> result;
    std::string error;
    // 阶段10 指令九十六：检查数量 + INSERT 同一事务（BEGIN IMMEDIATE 串行化写者）。
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return MapSqlError<CharacterRow>("CreateCharacter begin", error);
    }
    {
        Statement count;
        if (!count.Prepare(db.Handle(),
                           "SELECT COUNT(*) FROM characters WHERE account_id = ? AND deleted = 0;",
                           error)) {
            db.Execute("ROLLBACK;", error);
            return MapSqlError<CharacterRow>("CreateCharacter count prepare", error);
        }
        count.BindInt64(1, static_cast<std::int64_t>(accountId));
        std::int64_t characterCount = 0;
        if (count.Step(error)) {
            characterCount = count.ColumnInt64(0);
        }
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return MapSqlError<CharacterRow>("CreateCharacter count step", error);
        }
        if (static_cast<std::size_t>(characterCount) >= maxCharactersPerAccount) {
            db.Execute("ROLLBACK;", error);
            result.errorCode = AccountErrorCode::CharacterLimitReached;
            result.errorMessage = "character limit reached";
            return result;
        }
    }
    {
        Statement insert;
        const std::int64_t now = UnixNow();
        // 阶段25 指令四：新角色写"未出生哨兵"(-1,-1) —— WorldServer EnterWorld 检测到
        // 哨兵后按服务器权威地图出生点（Map1 300,300）落地并立即持久化。
        // 阶段26 指令十一/三十：visual_id 随创建写入（Migration 6）。
        if (!insert.Prepare(db.Handle(),
                            "INSERT INTO characters (account_id, name, class_id, gender, "
                            "visual_id, map_id, position_x, position_y, created_at) "
                            "VALUES (?, ?, ?, ?, ?, 1, -1, -1, ?);",
                            error)) {
            db.Execute("ROLLBACK;", error);
            return MapSqlError<CharacterRow>("CreateCharacter insert prepare", error);
        }
        insert.BindInt64(1, static_cast<std::int64_t>(accountId));
        insert.BindText(2, name);
        insert.BindInt64(3, classId);
        insert.BindInt64(4, gender);
        insert.BindInt64(5, visualId);
        insert.BindInt64(6, now);
        insert.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            if (IsUniqueConstraintError(error)) {
                result.errorCode = AccountErrorCode::CharacterNameTaken;
                result.errorMessage = "character name already taken";
                return result;
            }
            return MapSqlError<CharacterRow>("CreateCharacter insert", error);
        }
        result.success = true;
        result.value.id = static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db.Handle()));
        result.value.accountId = accountId;
        result.value.name = name;
        result.value.classId = classId;
        result.value.gender = gender;
        result.value.level = 1;
        result.value.exp = 0;
        result.value.mapId = 1;
        result.value.positionX = -1;
        result.value.positionY = -1;
        result.value.createdAt = now;
        result.value.lastPlayedAt = 0;
        result.value.deleted = false;
        result.value.visualId = visualId;
    }
    if (!db.Execute("COMMIT;", error)) {
        return MapSqlError<CharacterRow>("CreateCharacter commit", error);
    }
    return result;
}

RepositoryResult<std::vector<CharacterRow>> ListCharactersByAccount(Database& db,
                                                                    std::uint64_t accountId) {
    RepositoryResult<std::vector<CharacterRow>> result;
    Statement stmt;
    std::string error;
    // 阶段10 指令四十四：last_played_at DESC，未玩过的按 created_at ASC 排在最后。
    // 阶段17：SELECT 补 gold 列（Migration 2）。阶段26：visual_id（Migration 6）。
    if (!stmt.Prepare(db.Handle(),
                      "SELECT id, account_id, name, class_id, gender, level, exp, gold, map_id, "
                      "position_x, position_y, created_at, last_played_at, deleted, record_version, "
                      "visual_id "
                      "FROM characters WHERE account_id = ? AND deleted = 0 "
                      "ORDER BY last_played_at IS NULL ASC, last_played_at DESC, created_at ASC;",
                      error)) {
        return MapSqlError<std::vector<CharacterRow>>("ListCharactersByAccount prepare", error);
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(accountId));
    while (stmt.Step(error)) {
        CharacterRow row;
        row.id = static_cast<std::uint64_t>(stmt.ColumnInt64(0));
        row.accountId = static_cast<std::uint64_t>(stmt.ColumnInt64(1));
        row.name = stmt.ColumnText(2);
        row.classId = static_cast<std::uint16_t>(stmt.ColumnInt64(3));
        row.gender = static_cast<std::uint16_t>(stmt.ColumnInt64(4));
        row.level = static_cast<std::uint32_t>(stmt.ColumnInt64(5));
        row.exp = stmt.ColumnInt64(6);
        row.gold = stmt.ColumnInt64(7);
        row.mapId = static_cast<std::uint16_t>(stmt.ColumnInt64(8));
        row.positionX = stmt.ColumnDouble(9);
        row.positionY = stmt.ColumnDouble(10);
        row.createdAt = stmt.ColumnInt64(11);
        row.lastPlayedAt = stmt.ColumnInt64(12);
        row.deleted = stmt.ColumnInt64(13) != 0;
        row.recordVersion = static_cast<std::uint64_t>(stmt.ColumnInt64(14));
        row.visualId = static_cast<std::uint16_t>(stmt.ColumnInt64(15));
        result.value.push_back(std::move(row));
    }
    if (!error.empty()) {
        return MapSqlError<std::vector<CharacterRow>>("ListCharactersByAccount step", error);
    }
    result.success = true;
    return result;
}

RepositoryResult<std::optional<CharacterRow>> FindCharacterById(Database& db,
                                                                std::uint64_t characterId) {
    RepositoryResult<std::optional<CharacterRow>> result;
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "SELECT id, account_id, name, class_id, gender, level, exp, gold, map_id, "
                      "position_x, position_y, created_at, last_played_at, deleted, record_version, "
                      "visual_id "
                      "FROM characters WHERE id = ?;",
                      error)) {
        return MapSqlError<std::optional<CharacterRow>>("FindCharacterById prepare", error);
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
    if (stmt.Step(error)) {
        CharacterRow row;
        row.id = static_cast<std::uint64_t>(stmt.ColumnInt64(0));
        row.accountId = static_cast<std::uint64_t>(stmt.ColumnInt64(1));
        row.name = stmt.ColumnText(2);
        row.classId = static_cast<std::uint16_t>(stmt.ColumnInt64(3));
        row.gender = static_cast<std::uint16_t>(stmt.ColumnInt64(4));
        row.level = static_cast<std::uint32_t>(stmt.ColumnInt64(5));
        row.exp = stmt.ColumnInt64(6);
        row.gold = stmt.ColumnInt64(7);
        row.mapId = static_cast<std::uint16_t>(stmt.ColumnInt64(8));
        row.positionX = stmt.ColumnDouble(9);
        row.positionY = stmt.ColumnDouble(10);
        row.createdAt = stmt.ColumnInt64(11);
        row.lastPlayedAt = stmt.ColumnInt64(12);
        row.deleted = stmt.ColumnInt64(13) != 0;
        row.recordVersion = static_cast<std::uint64_t>(stmt.ColumnInt64(14));
        row.visualId = static_cast<std::uint16_t>(stmt.ColumnInt64(15));
        result.success = true;
        result.value = std::move(row);
        return result;
    }
    if (!error.empty()) {
        return MapSqlError<std::optional<CharacterRow>>("FindCharacterById step", error);
    }
    result.success = true;
    return result;
}

RepositoryResult<int> SoftDeleteCharacter(Database& db, std::uint64_t characterId) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(), "UPDATE characters SET deleted = 1, record_version = record_version + 1 WHERE id = ?;", error)) {
        return MapSqlError<int>("SoftDeleteCharacter prepare", error);
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<int>("SoftDeleteCharacter step", error);
    }
    return RepositoryResult<int>{true, 1};
}

RepositoryResult<int> UpdateLastPlayed(Database& db, std::uint64_t characterId) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "UPDATE characters SET last_played_at = ?, record_version = record_version + 1 WHERE id = ?;", error)) {
        return MapSqlError<int>("UpdateLastPlayed prepare", error);
    }
    stmt.BindInt64(1, UnixNow());
    stmt.BindInt64(2, static_cast<std::int64_t>(characterId));
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<int>("UpdateLastPlayed step", error);
    }
    return RepositoryResult<int>{true, 1};
}

RepositoryResult<int> UpdateWorldPosition(Database& db, std::uint64_t characterId,
                                          std::uint16_t mapId, float positionX, float positionY,
                                          std::int64_t lastPlayedAt) {
    // 阶段11 指令五十六：WorldServer 位置保存（prepared statement，禁止拼接 SQL）。
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "UPDATE characters SET map_id = ?, position_x = ?, position_y = ?, "
                      "last_played_at = ?, record_version = record_version + 1 WHERE id = ?;",
                      error)) {
        return MapSqlError<int>("UpdateWorldPosition prepare", error);
    }
    stmt.BindInt64(1, mapId);
    stmt.BindDouble(2, static_cast<double>(positionX));
    stmt.BindDouble(3, static_cast<double>(positionY));
    stmt.BindInt64(4, lastPlayedAt);
    stmt.BindInt64(5, static_cast<std::int64_t>(characterId));
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<int>("UpdateWorldPosition step", error);
    }
    return RepositoryResult<int>{true, 1};
}

// 阶段17 指令二/三十四：成长整体写（level/exp/gold；DbWorker 线程执行）。
RepositoryResult<int> SaveProgression(Database& db, std::uint64_t characterId,
                                      std::uint32_t level, std::int64_t exp, std::int64_t gold) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "UPDATE characters SET level = ?, exp = ?, gold = ?, record_version = record_version + 1 WHERE id = ?;",
                      error)) {
        return MapSqlError<int>("SaveProgression prepare", error);
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(level));
    stmt.BindInt64(2, exp);
    stmt.BindInt64(3, gold);
    stmt.BindInt64(4, static_cast<std::int64_t>(characterId));
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<int>("SaveProgression step", error);
    }
    return RepositoryResult<int>{true, 1};
}

// 阶段17 指令七：离线 killer 奖励入库（读改写事务：exp/gold 累加，不覆盖不丢失）。
RepositoryResult<int> AddProgressionRewards(Database& db, std::uint64_t characterId,
                                            std::int64_t expDelta, std::int64_t goldDelta) {
    std::string error;
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return MapSqlError<int>("AddProgressionRewards begin", error);
    }
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE characters SET exp = exp + ?, gold = gold + ?, record_version = record_version + 1 WHERE id = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return MapSqlError<int>("AddProgressionRewards prepare", error);
        }
        stmt.BindInt64(1, expDelta);
        stmt.BindInt64(2, goldDelta);
        stmt.BindInt64(3, static_cast<std::int64_t>(characterId));
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return MapSqlError<int>("AddProgressionRewards step", error);
        }
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return MapSqlError<int>("AddProgressionRewards commit", error);
    }
    return RepositoryResult<int>{true, 1};
}

// 阶段25.5：金币扣费/入账落库（单 UPDATE；record_version 递增保持并发纪律）。
RepositoryResult<int> SaveGold(Database& db, std::uint64_t characterId, std::int64_t gold) {
    std::string error;
    Statement stmt;
    if (!stmt.Prepare(db.Handle(),
                      "UPDATE characters SET gold = ?, record_version = record_version + 1 "
                      "WHERE id = ?;",
                      error)) {
        return MapSqlError<int>("SaveGold prepare", error);
    }
    stmt.BindInt64(1, gold);
    stmt.BindInt64(2, static_cast<std::int64_t>(characterId));
    stmt.Step(error);
    if (!error.empty()) {
        return MapSqlError<int>("SaveGold step", error);
    }
    return RepositoryResult<int>{true, 1};
}

} // namespace CharacterRepository

} // namespace legend::account
