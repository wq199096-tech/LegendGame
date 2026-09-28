#pragma once

#include "Server/LoginServer/Account/Database/Database.h"

namespace legend::account {

// 阶段10 指令六：Schema Version（Migration 用，不靠 DROP TABLE 重建）。
// 阶段10.1：schema_version 恒单行（id=1 主键 + UPSERT 写版本），
// 旧 v1 结构（仅 version 列）由 InitializeSchema 自动兼容升级。
inline constexpr int kCurrentSchemaVersion = 1;

// 初始化：quick_check 损坏检测 + 旧结构兼容升级 + 建表 + Migration 到
// kCurrentSchemaVersion。新库 -> 应用全部 Migration 并写入 version；
// 旧库 -> 增量应用更高版本 Migration；版本比当前新 -> 失败（禁止降级覆盖）。
bool InitializeSchema(Database& db, std::string& error);

} // namespace legend::account
