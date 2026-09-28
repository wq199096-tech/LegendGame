#include "Server/LoginServer/Account/Database/Database.h"

#include "Engine/Debug/Logger.h"

#include <chrono>

namespace legend::account {

Database::~Database() {
    Close();
}

bool Database::Open(const std::string& path, std::string& error) {
    if (m_db != nullptr) {
        error = "database already open: " + m_path;
        return false;
    }
    // 指令九十八：打开失败必须上报，不得静默另建数据库覆盖。
    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        error = std::string("sqlite open failed: ") + (db ? sqlite3_errmsg(db) : "unknown");
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return false;
    }
    m_db = db;
    m_path = path;
    // 指令十一：外键约束 + 忙等待（并发进程访问同一文件时兜底）。
    // 阶段11 指令二十三：WAL —— LoginServer（写）与 WorldServer（读/位置更新）
    // 多进程并发访问同一 SQLite 文件。
    std::string pragmaError;
    if (!Execute("PRAGMA foreign_keys = ON;", pragmaError) ||
        !Execute("PRAGMA busy_timeout = 5000;", pragmaError) ||
        !Execute("PRAGMA journal_mode = WAL;", pragmaError)) {
        error = pragmaError;
        Close();
        return false;
    }
    return true;
}

void Database::Close() {
    if (m_db != nullptr) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    m_path.clear();
}

bool Database::Execute(const char* sql, std::string& error) {
    if (m_db == nullptr) {
        error = "database not open";
        return false;
    }
    char* message = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error = std::string("sqlite exec failed (") + sqlite3_errmsg(m_db) + "): " + sql;
        sqlite3_free(message);
        return false;
    }
    sqlite3_free(message);
    return true;
}

Statement::~Statement() {
    if (m_stmt != nullptr) {
        sqlite3_finalize(m_stmt);
    }
}

bool Statement::Prepare(sqlite3* db, const char* sql, std::string& error) {
    if (m_stmt != nullptr) {
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
    }
    m_valid = false;
    if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK) {
        error = std::string("sqlite prepare failed: ") + sqlite3_errmsg(db) + " [" + sql + "]";
        return false;
    }
    m_valid = true;
    return true;
}

void Statement::BindInt64(int index, std::int64_t value) {
    if (m_stmt == nullptr || sqlite3_bind_int64(m_stmt, index, value) != SQLITE_OK) {
        m_valid = false;
    }
}

void Statement::BindDouble(int index, double value) {
    if (m_stmt == nullptr || sqlite3_bind_double(m_stmt, index, value) != SQLITE_OK) {
        m_valid = false;
    }
}

void Statement::BindText(int index, const std::string& value) {
    if (m_stmt == nullptr ||
        sqlite3_bind_text(m_stmt, index, value.c_str(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        m_valid = false;
    }
}

bool Statement::Step(std::string& error) {
    if (m_stmt == nullptr || !m_valid) {
        error = "sqlite step on invalid statement";
        return false;
    }
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    m_valid = false;
    error = std::string("sqlite step failed: ") + sqlite3_errmsg(sqlite3_db_handle(m_stmt));
    return false;
}

std::int64_t Statement::ColumnInt64(int column) const {
    return m_stmt != nullptr ? sqlite3_column_int64(m_stmt, column) : 0;
}

double Statement::ColumnDouble(int column) const {
    return m_stmt != nullptr ? sqlite3_column_double(m_stmt, column) : 0.0;
}

std::string Statement::ColumnText(int column) const {
    if (m_stmt == nullptr) {
        return {};
    }
    const auto* text = sqlite3_column_text(m_stmt, column);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

void Statement::Reset() {
    if (m_stmt != nullptr) {
        sqlite3_reset(m_stmt);
        sqlite3_clear_bindings(m_stmt);
        m_valid = true;
    }
}

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace legend::account
