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

// ---------------------------------------------------------------------------
// 阶段10.1：schema_version 单行化（COUNT(*) 恒为 1）
// 新格式：id INTEGER PRIMARY KEY CHECK(id = 1) + version —— 以后 Migration
// 用 UPSERT 写版本，v2/v3 不会再产生多行。
// ---------------------------------------------------------------------------

// 旧 v1 库兼容（指令四）：schema_version(version) 旧结构 → 新结构。
// 只动 schema_version，绝不 DROP accounts/characters/sessions（指令五）。
bool BootstrapSchemaVersion(Database& db, std::string& error) {
    bool tableExists = false;
    bool hasIdColumn = false;
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(), "PRAGMA table_info(schema_version);", error)) {
            return false;
        }
        while (stmt.Step(error)) {
            tableExists = true;
            if (stmt.ColumnText(1) == "id") {
                hasIdColumn = true;
            }
        }
        if (!error.empty()) {
            return false;
        }
    }
    if (!tableExists || hasIdColumn) {
        return true; // 新库（无表）或已是新格式
    }
    // 旧结构（仅 version 列）：事务内重建，MAX(version) 携带旧版本号
    LOG_INFO("[DB] Upgrading legacy schema_version table to single-row format...");
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    const char* fixStatements[] = {
        "CREATE TABLE schema_version_new ("
        "  id INTEGER PRIMARY KEY CHECK(id = 1),"
        "  version INTEGER NOT NULL"
        ");",
        // 旧表为空时 COALESCE 取 0：后续 Migration 全部补跑（均 IF NOT EXISTS，安全）
        "INSERT INTO schema_version_new (id, version) "
        "SELECT 1, COALESCE(MAX(version), 0) FROM schema_version;",
        "DROP TABLE schema_version;",
        "ALTER TABLE schema_version_new RENAME TO schema_version;",
    };
    for (const char* sql : fixStatements) {
        if (!db.Execute(sql, error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    if (!db.Execute("COMMIT;", error)) {
        return false;
    }
    return true;
}

// 读版本：新格式下 id 是主键，最多一行（无行 = 空库，version 0）。
bool ReadSchemaVersion(Database& db, int& outVersion, std::string& error) {
    Statement stmt;
    if (!stmt.Prepare(db.Handle(), "SELECT version FROM schema_version WHERE id = 1;", error)) {
        return false;
    }
    if (stmt.Step(error)) {
        outVersion = static_cast<int>(stmt.ColumnInt64(0));
        return true;
    }
    if (!error.empty()) {
        return false;
    }
    outVersion = 0;
    return true;
}

// 写版本：UPSERT（指令三）——无论写多少次，schema_version 恒为单行。
bool WriteSchemaVersion(Database& db, int version, std::string& error) {
    Statement stmt;
    if (!stmt.Prepare(db.Handle(),
                      "INSERT INTO schema_version (id, version) VALUES (1, ?) "
                      "ON CONFLICT(id) DO UPDATE SET version = excluded.version;",
                      error)) {
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

    // 阶段10.1：旧 v1 结构兼容升级（先于建表/读版本；只动 schema_version）。
    if (!BootstrapSchemaVersion(db, error)) {
        return false;
    }

    // schema_version 表（指令六：新格式，id 固定 1，天然单行）。
    if (!db.Execute(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "  id INTEGER PRIMARY KEY CHECK(id = 1),"
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
