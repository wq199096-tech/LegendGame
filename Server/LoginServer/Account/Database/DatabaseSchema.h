#pragma once

#include "Server/LoginServer/Account/Database/Database.h"

namespace legend::account {

// 阶段10 指令六：Schema Version（Migration 用，不靠 DROP TABLE 重建）。
inline constexpr int kCurrentSchemaVersion = 1;

// 初始化：quick_check 损坏检测 + 建表 + Migration 到 kCurrentSchemaVersion。
// 新库 -> 应用全部 Migration 并写入 version；旧库 -> 增量应用更高版本 Migration；
// 版本比当前新 -> 失败（数据库来自更新版本的服务端，禁止降级覆盖）。
bool InitializeSchema(Database& db, std::string& error);

} // namespace legend::account
