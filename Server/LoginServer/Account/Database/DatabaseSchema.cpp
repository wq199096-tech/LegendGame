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

// 阶段17 指令三：Migration 2 —— characters 增加 gold（旧库升级，旧角色默认 0；
// level/exp 列在 Migration 1 已存在，不破坏旧数据库）。
const char* kMigration2Statements[] = {
    "ALTER TABLE characters ADD COLUMN gold INTEGER NOT NULL DEFAULT 0;",
};

// 阶段18 指令八：Migration 3 —— inventory_items / character_equipment。
// instance_id = INTEGER PRIMARY KEY AUTOINCREMENT（指令九：持久唯一，重启不碰撞）；
// 装备中的物品保留行，slot_index = 1000 + equipment_slot（1001 Weapon / 1002 Armor）；
// character_equipment.item_instance_id 外键引用 inventory_items(instance_id)。
const char* kMigration3Statements[] = {
    "CREATE TABLE IF NOT EXISTS inventory_items ("
    "  instance_id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  character_id INTEGER NOT NULL,"
    "  item_definition_id INTEGER NOT NULL,"
    "  quantity INTEGER NOT NULL,"
    "  slot_index INTEGER NOT NULL,"
    "  created_at INTEGER NOT NULL,"
    "  FOREIGN KEY(character_id) REFERENCES characters(id)"
    ");",
    "CREATE INDEX IF NOT EXISTS idx_inventory_items_character_id "
    "ON inventory_items(character_id);",
    "CREATE TABLE IF NOT EXISTS character_equipment ("
    "  character_id INTEGER NOT NULL,"
    "  equipment_slot INTEGER NOT NULL,"
    "  item_instance_id INTEGER NOT NULL,"
    "  PRIMARY KEY(character_id, equipment_slot),"
    "  FOREIGN KEY(character_id) REFERENCES characters(id),"
    "  FOREIGN KEY(item_instance_id) REFERENCES inventory_items(instance_id)"
    ");",
};

// 阶段19 指令十六/六十九：Migration 4 —— character_quests / character_quest_objectives。
// 只保存角色任务状态（state/时间戳/目标进度），任务定义绝不入库；
// Abandoned 记录保留（指令十七：推荐保留 state=Abandoned）；
// Completed 记录永久保留（指令六十七：一次性任务）。
const char* kMigration4Statements[] = {
    "CREATE TABLE IF NOT EXISTS character_quests ("
    "  character_id INTEGER NOT NULL,"
    "  quest_id INTEGER NOT NULL,"
    "  state INTEGER NOT NULL,"
    "  accepted_at INTEGER,"
    "  completed_at INTEGER,"
    "  turned_in_at INTEGER,"
    "  PRIMARY KEY(character_id, quest_id),"
    "  FOREIGN KEY(character_id) REFERENCES characters(id)"
    ");",
    "CREATE INDEX IF NOT EXISTS idx_character_quests_character_id "
    "ON character_quests(character_id);",
    "CREATE TABLE IF NOT EXISTS character_quest_objectives ("
    "  character_id INTEGER NOT NULL,"
    "  quest_id INTEGER NOT NULL,"
    "  objective_id INTEGER NOT NULL,"
    "  progress INTEGER NOT NULL,"
    "  PRIMARY KEY(character_id, quest_id, objective_id),"
    "  FOREIGN KEY(character_id) REFERENCES characters(id)"
    ");",
    "CREATE INDEX IF NOT EXISTS idx_character_quest_objectives_character_id "
    "ON character_quest_objectives(character_id);",
};

const char* kMigration5Statements[] = {
    "ALTER TABLE characters ADD COLUMN record_version INTEGER NOT NULL DEFAULT 1;",
};

struct Migration {
    int version;
    const char* const* statements;
    int count;
};

const Migration kMigrations[] = {
    {1, kMigration1Statements, static_cast<int>(std::size(kMigration1Statements))},
    {2, kMigration2Statements, static_cast<int>(std::size(kMigration2Statements))},
    {3, kMigration3Statements, static_cast<int>(std::size(kMigration3Statements))},
    {4, kMigration4Statements, static_cast<int>(std::size(kMigration4Statements))},
    {5, kMigration5Statements, static_cast<int>(std::size(kMigration5Statements))},
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

// 读版本 / 写版本已提升到 legend::account 作用域（见文件末尾公共定义）。

} // namespace

// 读版本：新格式下 id 是主键，最多一行（无行 = 空库，version 0）。
// Stage25.6：提升到 legend::account 作用域（DatabaseSchema.h 公共声明），
// DbServer 管理台在 DB 线程读取运行时 Migration 版本。
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
