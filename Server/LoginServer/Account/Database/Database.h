#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <string>

namespace legend::account {

// 阶段10 指令四/十一：SQLite 连接 RAII 封装。
// 线程约束：连接只在 DB Worker 线程使用（指令六十五），不做内部加锁。
// 打开即设置 PRAGMA foreign_keys = ON；失败不静默（指令九十八）。
class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 打开/创建数据库文件。已打开时返回 false（防重复初始化）。
    bool Open(const std::string& path, std::string& error);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    sqlite3* Handle() const { return m_db; }
    const std::string& Path() const { return m_path; }

    // 执行无结果 SQL（DDL/PRAGMA/事务控制）。错误信息进 error（不直接下发客户端）。
    bool Execute(const char* sql, std::string& error);

    // 阶段18 指令九：INSERT 后取 INTEGER PRIMARY KEY rowid（DB Worker 线程内调用）。
    std::int64_t LastInsertRowid() const;

private:
    sqlite3* m_db = nullptr;
    std::string m_path;
};

// 阶段10 指令十一：prepared statements（禁止字符串拼接 SQL）。
class Statement {
public:
    Statement() = default;
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool Prepare(sqlite3* db, const char* sql, std::string& error);

    // 绑定（index 从 1 开始）。失败置 m_valid=false。
    void BindInt64(int index, std::int64_t value);
    void BindDouble(int index, double value);
    void BindText(int index, const std::string& value);

    // SQLITE_ROW -> true（有行）；SQLITE_DONE -> false（无行，成功）；
    // 其他错误 -> false + error。
    bool Step(std::string& error);

    std::int64_t ColumnInt64(int column) const;
    double ColumnDouble(int column) const;
    std::string ColumnText(int column) const;

    void Reset();

private:
    sqlite3_stmt* m_stmt = nullptr;
    bool m_valid = false;
};

// unix 秒（账号/角色/Session 时间戳统一）。
std::int64_t UnixNow();

} // namespace legend::account
