#include "Server/LoginServer/Account/Database/DatabaseSchema.h"

#include "Engine/Debug/Logger.h"

#include <vector>

namespace legend::account {
namespace {

// 阶段10 指令七/八/九/十：Migration 1 —— accounts / characters / sessions + 索引。
// username 列 UNIQUE COLLATE NOCASE：大小写不敏感唯一（指令十二，保存原始 username）。
const char* kMigration1Statements[] = {
    "CREATE TABLE IF NOT EXISTS accounts ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
    "  password_hash TEXT NOT NULL,"
    "  created_at INTEGER NOT NULL,"
    "  last_login_at INTEGER,"
    "  status INTEGER NOT NULL DEFAULT 0,"
    "  failed_login_count INTEGER NOT NULL DEFAULT 0,"
    "  locked_until INTEGER"
    ");",
    "CREATE TABLE IF NOT EXISTS characters ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  account_id INTEGER NOT NULL,"
    "  name TEXT NOT NULL UNIQUE,"
    "  class_id INTEGER NOT NULL,"
    "  gender INTEGER NOT NULL,"
    "  level INTEGER NOT NULL DEFAULT 1,"
    "  exp INTEGER NOT NULL DEFAULT 0,"
    "  map_id INTEGER NOT NULL DEFAULT 1,"
    "  position_x REAL NOT NULL DEFAULT 0,"
    "  position_y REAL NOT NULL DEFAULT 0,"
    "  created_at INTEGER NOT NULL,"
    "  last_played_at INTEGER,"
    "  deleted INTEGER NOT NULL DEFAULT 0,"
    "  FOREIGN KEY(account_id) REFERENCES accounts(id)"
    ");",
    "CREATE TABLE IF NOT EXISTS sessions ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  account_id INTEGER NOT NULL,"
    "  session_token_hash TEXT NOT NULL UNIQUE,"
    "  created_at INTEGER NOT NULL,"
    "  expires_at INTEGER NOT NULL,"
    "  revoked INTEGER NOT NULL DEFAULT 0,"
    "  FOREIGN KEY(account_id) REFERENCES accounts(id)"
    ");",
    "CREATE INDEX IF NOT EXISTS idx_accounts_username ON accounts(username);",
    "CREATE INDEX IF NOT EXISTS idx_characters_account_id ON characters(account_id);",
    "CREATE INDEX IF NOT EXISTS idx_characters_name ON characters(name);",
    "CREATE INDEX IF NOT EXISTS idx_sessions_account_id ON sessions(account_id);",
    "CREATE INDEX IF NOT EXISTS idx_sessions_token_hash ON sessions(session_token_hash);",
};

struct Migration {
    int version;
    const char* const* statements;
    int count;
};

const Migration kMigrations[] = {
    {1, kMigration1Statements, static_cast<int>(std::size(kMigration1Statements))},
};

bool ReadSchemaVersion(Database& db, int& outVersion, std::string& error) {
    Statement stmt;
    if (!stmt.Prepare(db.Handle(), "SELECT version FROM schema_version;", error)) {
        return false;
    }
    if (stmt.Step(error)) { // 有行
        outVersion = static_cast<int>(stmt.ColumnInt64(0));
        if (stmt.Step(error)) {
            error = "schema_version table has more than one row";
            return false;
        }
        if (!error.empty()) {
            return false;
        }
        return true;
    }
    if (!error.empty()) {
        return false; // Step 出错
    }
    outVersion = 0; // 无行 = 空库
    return true;
}

bool WriteSchemaVersion(Database& db, int version, std::string& error) {
    Statement stmt;
    if (!stmt.Prepare(db.Handle(), "INSERT INTO schema_version (version) VALUES (?);", error)) {
        return false;
    }
    stmt.BindInt64(1, version);
    stmt.Step(error); // SQLITE_DONE
    return error.empty();
}

} // namespace

bool InitializeSchema(Database& db, std::string& error) {
    // 指令九十八：损坏数据库必须在启动时失败（quick_check），禁止静默重建覆盖。
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(), "PRAGMA quick_check;", error)) {
            return false;
        }
        std::string checkResult;
        if (stmt.Step(error)) {
            checkResult = stmt.ColumnText(0);
        }
        if (!error.empty()) {
            return false;
        }
        if (checkResult != "ok") {
            error = "database integrity check failed: " + checkResult;
            return false;
        }
    }

    // schema_version 表（指令六）。
    if (!db.Execute(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "  version INTEGER NOT NULL"
            ");",
            error)) {
        return false;
    }

    int currentVersion = 0;
    if (!ReadSchemaVersion(db, currentVersion, error)) {
        return false;
    }
    if (currentVersion > kCurrentSchemaVersion) {
        error = "database schema version " + std::to_string(currentVersion) +
                " is newer than supported " + std::to_string(kCurrentSchemaVersion);
        return false;
    }

    for (const Migration& migration : kMigrations) {
        if (migration.version <= currentVersion) {
            continue; // 已应用（九十七：重启不重复创建、不丢数据）
        }
        LOG_INFO("[DB] Applying migration " + std::to_string(migration.version) + "...");
        if (!db.Execute("BEGIN IMMEDIATE;", error)) {
            return false;
        }
        for (int i = 0; i < migration.count; ++i) {
            if (!db.Execute(migration.statements[i], error)) {
                db.Execute("ROLLBACK;", error);
                return false;
            }
        }
        if (!WriteSchemaVersion(db, migration.version, error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        if (!db.Execute("COMMIT;", error)) {
            return false;
        }
    }
    return true;
}

} // namespace legend::account
