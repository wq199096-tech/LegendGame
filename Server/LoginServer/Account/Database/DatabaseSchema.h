#pragma once

#include "Server/LoginServer/Account/Database/Database.h"

namespace legend::account {

// 阶段10 指令六：Schema Version（Migration 用，不靠 DROP TABLE 重建）。
// 阶段10.1：schema_version 恒单行（id=1 主键 + UPSERT 写版本），
// 旧 v1 结构（仅 version 列）由 InitializeSchema 自动兼容升级。
// 阶段17 指令三：Migration 2 —— characters 增加 gold 列（旧角色默认 0）。
// 阶段18 指令八：Migration 3 —— inventory_items / character_equipment 两表
//（旧库自动升级；instance_id = INTEGER PRIMARY KEY 持久唯一，重启不碰撞）。
// 阶段19 指令十六：Migration 4 —— character_quests / character_quest_objectives
//（旧库自动升级；只保存角色任务状态，任务定义仍在代码，指令六十九）。
inline constexpr int kCurrentSchemaVersion = 4;

// 初始化：quick_check 损坏检测 + 旧结构兼容升级 + 建表 + Migration 到
// kCurrentSchemaVersion。新库 -> 应用全部 Migration 并写入 version；
// 旧库 -> 增量应用更高版本 Migration；版本比当前新 -> 失败（禁止降级覆盖）。
bool InitializeSchema(Database& db, std::string& error);

} // namespace legend::account
